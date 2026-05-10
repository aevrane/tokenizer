#include "tokenizer/ParquetCorpusIngestion.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>

#include "tokenizer/SharedTokenizer.h"

namespace Mina::Tokenizer
{
namespace
{
constexpr const char* PROGRESS_FILENAME = "parquet_ingest_progress.json";
constexpr const char* FILE_LOG_FILENAME = "parquet_ingest_files.tsv";
constexpr const char* MANIFEST_FILENAME = "parquet_ingest_manifest.json";
constexpr const char* REPORT_FILENAME = "parquet_ingest_report.md";
constexpr const char* LOG_FILENAME = "parquet_ingest_log.md";

std::atomic<bool> g_stopRequested = false;

struct ScopedSignalHandler
{
    ScopedSignalHandler()
    {
        g_stopRequested.store(false);
        previous = std::signal(SIGINT, &ScopedSignalHandler::HandleSignal);
    }

    ~ScopedSignalHandler()
    {
        std::signal(SIGINT, previous);
    }

    static void HandleSignal(int)
    {
        g_stopRequested.store(true);
    }

    using SignalHandler = void(*)(int);
    SignalHandler previous = SIG_DFL;
};

struct DiscoveredFile
{
    std::filesystem::path absolutePath;
    std::string relativePath;
    std::uint64_t inputBytes = 0;
};

struct ProgressState
{
    std::string status = "running";
    std::string discoveryChecksum;
    std::string currentFileRelativePath;
    std::string lastSuccessfulFlushUtc;
    std::size_t discoveryCount = 0;
    std::size_t currentFileIndex = 0;
    std::size_t currentRowGroupIndex = 0;
    std::size_t currentBatchIndex = 0;
    std::size_t currentOutputShardIndex = 0;
    std::uint64_t currentOutputShardBytes = 0;
    std::uint64_t totalRowsProcessed = 0;
    std::uint64_t totalTextRowsWritten = 0;
    std::uint64_t totalTextBytesWritten = 0;
    std::size_t completedFileCount = 0;
    std::size_t skippedFileCount = 0;
    std::size_t resumedRunCount = 0;
};

struct FileRunStats
{
    std::size_t rowGroupCount = 0;
    std::uint64_t rowsProcessed = 0;
    std::uint64_t textRowsWritten = 0;
    std::uint64_t textBytesWritten = 0;
    std::string detectedTextColumn;
};

struct LoggedFileRecord
{
    std::string relativePath;
    std::string status;
    std::string detectedTextColumn;
    std::uint64_t inputBytes = 0;
    std::size_t rowGroupCount = 0;
    std::uint64_t rowsProcessed = 0;
    std::uint64_t textRowsWritten = 0;
    std::uint64_t textBytesWritten = 0;
    std::string note;
};

std::string MakeChecksum(const std::string& value)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : value)
    {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << "fnv1a64:" << std::hex << hash;
    return stream.str();
}

std::string QuoteJson(const std::string& value)
{
    std::ostringstream stream;
    stream << "\"";
    for (char character : value)
    {
        switch (character)
        {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                stream << character;
                break;
        }
    }
    stream << "\"";
    return stream.str();
}

std::string JsonUnescape(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const char character = value[index];
        if (character != '\\' || index + 1 >= value.size())
        {
            decoded.push_back(character);
            continue;
        }

        ++index;
        switch (value[index])
        {
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case '\\':
                decoded.push_back('\\');
                break;
            case '"':
                decoded.push_back('"');
                break;
            default:
                decoded.push_back(value[index]);
                break;
        }
    }

    return decoded;
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void WriteFileAtomic(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path tempPath = path.string() + ".tmp";

    {
        std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            throw std::runtime_error("Unable to write file: " + tempPath.string());
        }

        stream << content;
        if (!stream.good())
        {
            throw std::runtime_error("Failed while writing file: " + tempPath.string());
        }
    }

    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }

    std::filesystem::rename(tempPath, path);
}

void AppendFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to append file: " + path.string());
    }

    stream << content;
    if (!stream.good())
    {
        throw std::runtime_error("Failed while appending file: " + path.string());
    }
}

std::string EscapeTsv(const std::string& value)
{
    std::ostringstream stream;
    for (char character : value)
    {
        switch (character)
        {
            case '\\':
                stream << "\\\\";
                break;
            case '\t':
                stream << "\\t";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            default:
                stream << character;
                break;
        }
    }
    return stream.str();
}

std::string UnescapeTsv(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const char character = value[index];
        if (character != '\\' || index + 1 >= value.size())
        {
            decoded.push_back(character);
            continue;
        }

        ++index;
        switch (value[index])
        {
            case 't':
                decoded.push_back('\t');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case '\\':
                decoded.push_back('\\');
                break;
            default:
                decoded.push_back(value[index]);
                break;
        }
    }

    return decoded;
}

std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }

    return lines;
}

std::vector<std::string> SplitTabLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;

    for (char character : line)
    {
        if (character == '\t')
        {
            fields.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    fields.push_back(current);
    return fields;
}

std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const std::size_t keyPosition = json.find(needle);
    if (keyPosition == std::string::npos)
    {
        return std::string();
    }

    std::size_t position = keyPosition + needle.size();
    while (position < json.size() && (json[position] == ' ' || json[position] == '\n' || json[position] == '\r'))
    {
        ++position;
    }

    if (position >= json.size() || json[position] != '"')
    {
        return std::string();
    }

    ++position;
    std::string encoded;
    bool escaping = false;
    while (position < json.size())
    {
        const char character = json[position++];
        if (escaping)
        {
            encoded.push_back(character);
            escaping = false;
            continue;
        }

        if (character == '\\')
        {
            encoded.push_back(character);
            escaping = true;
            continue;
        }

        if (character == '"')
        {
            return JsonUnescape(encoded);
        }

        encoded.push_back(character);
    }

    return JsonUnescape(encoded);
}

std::uint64_t ExtractJsonUnsigned(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const std::size_t keyPosition = json.find(needle);
    if (keyPosition == std::string::npos)
    {
        return 0;
    }

    std::size_t position = keyPosition + needle.size();
    while (position < json.size() && (json[position] == ' ' || json[position] == '\n' || json[position] == '\r'))
    {
        ++position;
    }

    std::size_t end = position;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])) != 0)
    {
        ++end;
    }

    if (end == position)
    {
        return 0;
    }

    return static_cast<std::uint64_t>(std::stoull(json.substr(position, end - position)));
}

std::string UtcTimestampNow()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &timeValue);

    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string BuildProgressJson(const ProgressState& progress)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"status\": " << QuoteJson(progress.status) << ",\n";
    stream << "  \"discovery_checksum\": " << QuoteJson(progress.discoveryChecksum) << ",\n";
    stream << "  \"current_file_relative_path\": " << QuoteJson(progress.currentFileRelativePath) << ",\n";
    stream << "  \"last_successful_flush_utc\": " << QuoteJson(progress.lastSuccessfulFlushUtc) << ",\n";
    stream << "  \"discovery_count\": " << progress.discoveryCount << ",\n";
    stream << "  \"current_file_index\": " << progress.currentFileIndex << ",\n";
    stream << "  \"current_row_group_index\": " << progress.currentRowGroupIndex << ",\n";
    stream << "  \"current_batch_index\": " << progress.currentBatchIndex << ",\n";
    stream << "  \"current_output_shard_index\": " << progress.currentOutputShardIndex << ",\n";
    stream << "  \"current_output_shard_bytes\": " << progress.currentOutputShardBytes << ",\n";
    stream << "  \"total_rows_processed\": " << progress.totalRowsProcessed << ",\n";
    stream << "  \"total_text_rows_written\": " << progress.totalTextRowsWritten << ",\n";
    stream << "  \"total_text_bytes_written\": " << progress.totalTextBytesWritten << ",\n";
    stream << "  \"completed_file_count\": " << progress.completedFileCount << ",\n";
    stream << "  \"skipped_file_count\": " << progress.skippedFileCount << ",\n";
    stream << "  \"resumed_run_count\": " << progress.resumedRunCount << "\n";
    stream << "}\n";
    return stream.str();
}

ProgressState ParseProgressJson(const std::string& json)
{
    ProgressState progress;
    progress.status = ExtractJsonString(json, "status");
    progress.discoveryChecksum = ExtractJsonString(json, "discovery_checksum");
    progress.currentFileRelativePath = ExtractJsonString(json, "current_file_relative_path");
    progress.lastSuccessfulFlushUtc = ExtractJsonString(json, "last_successful_flush_utc");
    progress.discoveryCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "discovery_count"));
    progress.currentFileIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_file_index"));
    progress.currentRowGroupIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_row_group_index"));
    progress.currentBatchIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_batch_index"));
    progress.currentOutputShardIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_output_shard_index"));
    progress.currentOutputShardBytes = ExtractJsonUnsigned(json, "current_output_shard_bytes");
    progress.totalRowsProcessed = ExtractJsonUnsigned(json, "total_rows_processed");
    progress.totalTextRowsWritten = ExtractJsonUnsigned(json, "total_text_rows_written");
    progress.totalTextBytesWritten = ExtractJsonUnsigned(json, "total_text_bytes_written");
    progress.completedFileCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "completed_file_count"));
    progress.skippedFileCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "skipped_file_count"));
    progress.resumedRunCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "resumed_run_count"));

    if (progress.status.empty())
    {
        throw std::runtime_error("Malformed progress file: missing status.");
    }

    return progress;
}

std::filesystem::path CorpusShardPath(const std::filesystem::path& outputRoot, std::size_t shardIndex)
{
    std::ostringstream name;
    name << "shard-" << std::setw(6) << std::setfill('0') << shardIndex << ".txt";
    return outputRoot / "exports" / "tokenizer" / "parquet_corpus" / name.str();
}

void TruncateFileToSize(const std::filesystem::path& path, std::uint64_t sizeBytes)
{
    std::filesystem::create_directories(path.parent_path());
    if (!std::filesystem::exists(path))
    {
        std::ofstream create(path, std::ios::binary);
        if (!create.is_open())
        {
            throw std::runtime_error("Unable to create file for truncation: " + path.string());
        }
    }

    std::filesystem::resize_file(path, sizeBytes);
}

void RestoreCorpusShards(
    const std::filesystem::path& outputRoot,
    std::size_t shardIndex,
    std::uint64_t shardBytes)
{
    const std::filesystem::path corpusRoot = outputRoot / "exports" / "tokenizer" / "parquet_corpus";
    std::filesystem::create_directories(corpusRoot);
    const std::filesystem::path currentShard = CorpusShardPath(outputRoot, shardIndex);
    TruncateFileToSize(currentShard, shardBytes);

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(corpusRoot))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt")
        {
            continue;
        }

        const std::string stem = entry.path().stem().string();
        const std::string prefix = "shard-";
        if (stem.rfind(prefix, 0) != 0)
        {
            continue;
        }

        const std::size_t candidateIndex = static_cast<std::size_t>(std::stoull(stem.substr(prefix.size())));
        if (candidateIndex > shardIndex)
        {
            std::filesystem::remove(entry.path());
        }
    }
}

std::vector<DiscoveredFile> DiscoverParquetFiles(const ParquetIngestConfig& config)
{
    if (!std::filesystem::exists(config.parquetRoot))
    {
        throw std::runtime_error("Parquet root does not exist: " + config.parquetRoot.string());
    }

    std::vector<DiscoveredFile> discovered;

    if (config.recursiveDiscovery)
    {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(config.parquetRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".parquet")
            {
                continue;
            }

            DiscoveredFile file;
            file.absolutePath = entry.path();
            file.relativePath = std::filesystem::relative(entry.path(), config.parquetRoot).generic_string();
            file.inputBytes = static_cast<std::uint64_t>(entry.file_size());
            discovered.push_back(file);
        }
    }
    else
    {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(config.parquetRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".parquet")
            {
                continue;
            }

            DiscoveredFile file;
            file.absolutePath = entry.path();
            file.relativePath = std::filesystem::relative(entry.path(), config.parquetRoot).generic_string();
            file.inputBytes = static_cast<std::uint64_t>(entry.file_size());
            discovered.push_back(file);
        }
    }

    std::sort(
        discovered.begin(),
        discovered.end(),
        [](const DiscoveredFile& left, const DiscoveredFile& right)
        {
            return left.relativePath < right.relativePath;
        });

    if (config.maxFiles > 0 && discovered.size() > config.maxFiles)
    {
        discovered.resize(config.maxFiles);
    }

    return discovered;
}

std::string BuildDiscoveryChecksum(const std::vector<DiscoveredFile>& discovered)
{
    std::ostringstream stream;
    for (const DiscoveredFile& file : discovered)
    {
        stream << file.relativePath << "\t" << file.inputBytes << "\n";
    }
    return MakeChecksum(stream.str());
}

std::string ArrowTypeName(const std::shared_ptr<arrow::DataType>& type)
{
    return type == nullptr ? "unknown" : type->ToString();
}

std::optional<std::pair<int, std::string>> DetectTextColumn(
    const std::shared_ptr<arrow::Schema>& schema,
    const ParquetIngestConfig& config)
{
    if (schema == nullptr)
    {
        return std::nullopt;
    }

    auto isSupported = [](const std::shared_ptr<arrow::Field>& field) -> bool
    {
        if (field == nullptr || field->type() == nullptr)
        {
            return false;
        }

        const arrow::Type::type typeId = field->type()->id();
        return typeId == arrow::Type::STRING || typeId == arrow::Type::LARGE_STRING;
    };

    if (!config.explicitTextColumn.empty())
    {
        const int index = schema->GetFieldIndex(config.explicitTextColumn);
        if (index < 0)
        {
            throw std::runtime_error("Configured text column was not found: " + config.explicitTextColumn);
        }

        const std::shared_ptr<arrow::Field> field = schema->field(index);
        if (!isSupported(field))
        {
            throw std::runtime_error(
                "Configured text column is not a supported string column: " + config.explicitTextColumn
                + " type=" + ArrowTypeName(field->type()));
        }

        return std::make_pair(index, config.explicitTextColumn);
    }

    for (const std::string& candidate : config.candidateTextColumns)
    {
        const int index = schema->GetFieldIndex(candidate);
        if (index < 0)
        {
            continue;
        }

        const std::shared_ptr<arrow::Field> field = schema->field(index);
        if (isSupported(field))
        {
            return std::make_pair(index, candidate);
        }
    }

    for (int index = 0; index < schema->num_fields(); ++index)
    {
        const std::shared_ptr<arrow::Field> field = schema->field(index);
        if (!isSupported(field))
        {
            continue;
        }

        const std::string lowered = field->name();
        if (lowered.find("text") != std::string::npos
            || lowered.find("content") != std::string::npos
            || lowered.find("body") != std::string::npos)
        {
            return std::make_pair(index, field->name());
        }
    }

    return std::nullopt;
}

std::string BuildFileLogHeader()
{
    return "relative_path\tstatus\tdetected_text_column\tinput_bytes\trow_group_count\trows_processed\ttext_rows_written\ttext_bytes_written\tnote\n";
}

void AppendLoggedFileRecord(const std::filesystem::path& path, const LoggedFileRecord& record)
{
    if (!std::filesystem::exists(path))
    {
        WriteFileAtomic(path, BuildFileLogHeader());
    }

    std::ostringstream line;
    line << EscapeTsv(record.relativePath) << "\t"
         << EscapeTsv(record.status) << "\t"
         << EscapeTsv(record.detectedTextColumn) << "\t"
         << record.inputBytes << "\t"
         << record.rowGroupCount << "\t"
         << record.rowsProcessed << "\t"
         << record.textRowsWritten << "\t"
         << record.textBytesWritten << "\t"
         << EscapeTsv(record.note) << "\n";
    AppendFile(path, line.str());
}

std::vector<LoggedFileRecord> LoadLoggedFileRecords(const std::filesystem::path& path)
{
    std::vector<LoggedFileRecord> records;
    if (!std::filesystem::exists(path))
    {
        return records;
    }

    const std::vector<std::string> lines = SplitLines(ReadFile(path));
    for (std::size_t index = 1; index < lines.size(); ++index)
    {
        const std::vector<std::string> fields = SplitTabLine(lines[index]);
        if (fields.size() < 9)
        {
            continue;
        }

        LoggedFileRecord record;
        record.relativePath = UnescapeTsv(fields[0]);
        record.status = UnescapeTsv(fields[1]);
        record.detectedTextColumn = UnescapeTsv(fields[2]);
        record.inputBytes = static_cast<std::uint64_t>(std::stoull(fields[3]));
        record.rowGroupCount = static_cast<std::size_t>(std::stoull(fields[4]));
        record.rowsProcessed = static_cast<std::uint64_t>(std::stoull(fields[5]));
        record.textRowsWritten = static_cast<std::uint64_t>(std::stoull(fields[6]));
        record.textBytesWritten = static_cast<std::uint64_t>(std::stoull(fields[7]));
        record.note = UnescapeTsv(fields[8]);
        records.push_back(record);
    }

    return records;
}

std::vector<LoggedFileRecord> DeduplicateRecords(const std::vector<LoggedFileRecord>& input)
{
    std::map<std::string, LoggedFileRecord> deduplicated;
    for (const LoggedFileRecord& record : input)
    {
        deduplicated[record.relativePath] = record;
    }

    std::vector<LoggedFileRecord> ordered;
    ordered.reserve(deduplicated.size());
    for (const std::pair<const std::string, LoggedFileRecord>& entry : deduplicated)
    {
        ordered.push_back(entry.second);
    }
    return ordered;
}

std::vector<std::filesystem::path> ListOutputShards(const std::filesystem::path& outputRoot)
{
    std::vector<std::filesystem::path> shards;
    const std::filesystem::path corpusRoot = outputRoot / "exports" / "tokenizer" / "parquet_corpus";
    if (!std::filesystem::exists(corpusRoot))
    {
        return shards;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(corpusRoot))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt")
        {
            continue;
        }

        shards.push_back(std::filesystem::relative(entry.path(), outputRoot));
    }

    std::sort(shards.begin(), shards.end());
    return shards;
}

std::string BuildManifestJson(
    const ParquetIngestConfig& config,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::vector<LoggedFileRecord>& records,
    const std::vector<std::filesystem::path>& outputShards)
{
    std::set<std::string> textColumns;
    for (const LoggedFileRecord& record : records)
    {
        if (!record.detectedTextColumn.empty())
        {
            textColumns.insert(record.detectedTextColumn);
        }
    }

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"parquet_root\": " << QuoteJson(config.parquetRoot.generic_string()) << ",\n";
    stream << "  \"output_root\": " << QuoteJson(config.outputRoot.generic_string()) << ",\n";
    stream << "  \"status\": " << QuoteJson(progress.status) << ",\n";
    stream << "  \"resume_enabled\": " << (config.resume ? "true" : "false") << ",\n";
    stream << "  \"discovered_file_count\": " << discovered.size() << ",\n";
    stream << "  \"completed_file_count\": " << progress.completedFileCount << ",\n";
    stream << "  \"skipped_file_count\": " << progress.skippedFileCount << ",\n";
    stream << "  \"total_rows_processed\": " << progress.totalRowsProcessed << ",\n";
    stream << "  \"total_text_rows_written\": " << progress.totalTextRowsWritten << ",\n";
    stream << "  \"total_text_bytes_written\": " << progress.totalTextBytesWritten << ",\n";
    stream << "  \"discovery_checksum\": " << QuoteJson(progress.discoveryChecksum) << ",\n";
    stream << "  \"last_successful_flush_utc\": " << QuoteJson(progress.lastSuccessfulFlushUtc) << ",\n";
    stream << "  \"resumed_run_count\": " << progress.resumedRunCount << ",\n";
    stream << "  \"detected_text_columns\": [";

    bool first = true;
    for (const std::string& column : textColumns)
    {
        if (!first)
        {
            stream << ", ";
        }
        stream << QuoteJson(column);
        first = false;
    }
    stream << "],\n";

    stream << "  \"output_shards\": [";
    for (std::size_t index = 0; index < outputShards.size(); ++index)
    {
        if (index > 0)
        {
            stream << ", ";
        }
        stream << QuoteJson(outputShards[index].generic_string());
    }
    stream << "],\n";

    stream << "  \"files\": [\n";
    for (std::size_t index = 0; index < discovered.size(); ++index)
    {
        const DiscoveredFile& file = discovered[index];
        const auto recordIterator = std::find_if(
            records.begin(),
            records.end(),
            [&](const LoggedFileRecord& record) { return record.relativePath == file.relativePath; });

        stream << "    {\n";
        stream << "      \"relative_path\": " << QuoteJson(file.relativePath) << ",\n";
        stream << "      \"input_bytes\": " << file.inputBytes << ",\n";
        if (recordIterator == records.end())
        {
            stream << "      \"status\": \"pending\"\n";
        }
        else
        {
            stream << "      \"status\": " << QuoteJson(recordIterator->status) << ",\n";
            stream << "      \"detected_text_column\": " << QuoteJson(recordIterator->detectedTextColumn) << ",\n";
            stream << "      \"row_group_count\": " << recordIterator->rowGroupCount << ",\n";
            stream << "      \"rows_processed\": " << recordIterator->rowsProcessed << ",\n";
            stream << "      \"text_rows_written\": " << recordIterator->textRowsWritten << ",\n";
            stream << "      \"text_bytes_written\": " << recordIterator->textBytesWritten << ",\n";
            stream << "      \"note\": " << QuoteJson(recordIterator->note) << "\n";
        }
        stream << "    }";
        if (index + 1 < discovered.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string BuildReportMarkdown(
    const ParquetIngestConfig& config,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::vector<LoggedFileRecord>& records,
    const std::vector<std::filesystem::path>& outputShards)
{
    std::set<std::string> textColumns;
    std::vector<LoggedFileRecord> skipped;

    for (const LoggedFileRecord& record : records)
    {
        if (!record.detectedTextColumn.empty())
        {
            textColumns.insert(record.detectedTextColumn);
        }

        if (record.status == "skipped")
        {
            skipped.push_back(record);
        }
    }

    std::ostringstream stream;
    stream << "# Parquet Ingest Report\n\n";
    stream << "- parquet root: `" << config.parquetRoot.generic_string() << "`\n";
    stream << "- output root: `" << config.outputRoot.generic_string() << "`\n";
    stream << "- status: `" << progress.status << "`\n";
    stream << "- parquet files discovered: `" << discovered.size() << "`\n";
    stream << "- parquet files processed: `" << (progress.completedFileCount + progress.skippedFileCount) << "`\n";
    stream << "- parquet files completed: `" << progress.completedFileCount << "`\n";
    stream << "- parquet files skipped: `" << progress.skippedFileCount << "`\n";
    stream << "- total rows processed: `" << progress.totalRowsProcessed << "`\n";
    stream << "- total text rows written: `" << progress.totalTextRowsWritten << "`\n";
    stream << "- total text bytes written: `" << progress.totalTextBytesWritten << "`\n";
    stream << "- output shard files written: `" << outputShards.size() << "`\n";
    stream << "- last successful flush: `" << progress.lastSuccessfulFlushUtc << "`\n";
    stream << "- resumed runs: `" << progress.resumedRunCount << "`\n";
    stream << "- detected text columns used: ";

    bool first = true;
    for (const std::string& column : textColumns)
    {
        if (!first)
        {
            stream << ", ";
        }
        stream << "`" << column << "`";
        first = false;
    }
    if (first)
    {
        stream << "`none`";
    }
    stream << "\n\n";

    stream << "## Output Files\n\n";
    for (const std::filesystem::path& shard : outputShards)
    {
        stream << "- `" << shard.generic_string() << "`\n";
    }

    stream << "\n## Skipped Or Bad Files\n\n";
    if (skipped.empty())
    {
        stream << "- none\n";
    }
    else
    {
        for (const LoggedFileRecord& record : skipped)
        {
            stream << "- `" << record.relativePath << "` reason=`" << record.note << "`\n";
        }
    }

    stream << "\n## Notes\n\n";
    stream << "- The ingestion path streams Parquet row groups through Arrow record-batch readers and never loads an entire Parquet file into memory.\n";
    stream << "- Resume checkpoints are durable at batch boundaries. On restart the exporter truncates shard output back to the last committed byte count before continuing.\n";
    stream << "- Output corpus text is normalized with the shared tokenizer normalizer and written one training sample per line.\n";
    return stream.str();
}

void AppendLogLine(
    const std::filesystem::path& logPath,
    const std::string& message)
{
    AppendFile(logPath, "- " + UtcTimestampNow() + " " + message + "\n");
}

void SaveProgress(const std::filesystem::path& progressPath, const ProgressState& progress)
{
    WriteFileAtomic(progressPath, BuildProgressJson(progress));
}

void WriteManifestAndReport(
    const ParquetIngestConfig& config,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::filesystem::path& fileLogPath,
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& reportPath)
{
    const std::vector<LoggedFileRecord> records = DeduplicateRecords(LoadLoggedFileRecords(fileLogPath));
    const std::vector<std::filesystem::path> outputShards = ListOutputShards(config.outputRoot);
    WriteFileAtomic(manifestPath, BuildManifestJson(config, progress, discovered, records, outputShards));
    WriteFileAtomic(reportPath, BuildReportMarkdown(config, progress, discovered, records, outputShards));
}

std::string ExtractStringValue(const std::shared_ptr<arrow::Array>& array, std::int64_t rowIndex)
{
    if (array->IsNull(rowIndex))
    {
        return std::string();
    }

    switch (array->type_id())
    {
        case arrow::Type::STRING:
            return std::static_pointer_cast<arrow::StringArray>(array)->GetString(rowIndex);
        case arrow::Type::LARGE_STRING:
            return std::static_pointer_cast<arrow::LargeStringArray>(array)->GetString(rowIndex);
        default:
            throw std::runtime_error("Unsupported text column type during extraction.");
    }
}

std::uint64_t WriteBatchText(
    const std::filesystem::path& outputRoot,
    ProgressState& progress,
    const std::shared_ptr<arrow::Array>& array,
    std::uint64_t& textRowsWritten)
{
    std::string batchText;
    batchText.reserve(static_cast<std::size_t>(array->length()) * 32);

    for (std::int64_t rowIndex = 0; rowIndex < array->length(); ++rowIndex)
    {
        const std::string normalized = NormalizeForTokenizer(ExtractStringValue(array, rowIndex));
        if (normalized.empty())
        {
            continue;
        }

        batchText += normalized;
        batchText.push_back('\n');
        ++textRowsWritten;
    }

    if (batchText.empty())
    {
        return 0;
    }

    const std::filesystem::path currentShardPath = CorpusShardPath(outputRoot, progress.currentOutputShardIndex);
    std::ofstream stream(currentShardPath, std::ios::binary | std::ios::app);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to append corpus shard: " + currentShardPath.string());
    }
    stream << batchText;
    if (!stream.good())
    {
        throw std::runtime_error("Failed while writing corpus shard: " + currentShardPath.string());
    }

    progress.currentOutputShardBytes += static_cast<std::uint64_t>(batchText.size());
    progress.totalTextBytesWritten += static_cast<std::uint64_t>(batchText.size());
    progress.totalTextRowsWritten += textRowsWritten;
    return static_cast<std::uint64_t>(batchText.size());
}

void MaybeRotateShard(
    const ParquetIngestConfig& config,
    ProgressState& progress,
    std::uint64_t pendingBytes)
{
    if (pendingBytes == 0)
    {
        return;
    }

    if (progress.currentOutputShardBytes == 0)
    {
        return;
    }

    if (progress.currentOutputShardBytes + pendingBytes <= config.shardSizeBytes)
    {
        return;
    }

    ++progress.currentOutputShardIndex;
    progress.currentOutputShardBytes = 0;
}

std::uint64_t EstimateBatchBytes(const std::shared_ptr<arrow::Array>& array, std::uint64_t& estimatedTextRows)
{
    std::uint64_t estimatedBytes = 0;
    estimatedTextRows = 0;

    for (std::int64_t rowIndex = 0; rowIndex < array->length(); ++rowIndex)
    {
        const std::string normalized = NormalizeForTokenizer(ExtractStringValue(array, rowIndex));
        if (normalized.empty())
        {
            continue;
        }

        estimatedBytes += static_cast<std::uint64_t>(normalized.size() + 1);
        ++estimatedTextRows;
    }

    return estimatedBytes;
}

std::string DescribeArrowStatus(const arrow::Status& status)
{
    return status.ToString();
}

std::shared_ptr<arrow::Schema> GetSchemaOrThrow(parquet::arrow::FileReader& reader)
{
    std::shared_ptr<arrow::Schema> schema;
    const arrow::Status status = reader.GetSchema(&schema);
    if (!status.ok())
    {
        throw std::runtime_error("Failed to read Parquet schema: " + DescribeArrowStatus(status));
    }
    return schema;
}

std::unique_ptr<parquet::arrow::FileReader> OpenParquetReader(
    const std::filesystem::path& path,
    const ParquetIngestConfig& config)
{
    parquet::ReaderProperties readerProperties;
    readerProperties.set_buffer_size(1024 * 1024);
    readerProperties.enable_buffered_stream();

    parquet::ArrowReaderProperties arrowReaderProperties;
    arrowReaderProperties.set_batch_size(static_cast<int64_t>(config.batchSizeRows));

    parquet::arrow::FileReaderBuilder builder;
    const arrow::Status openStatus = builder.OpenFile(path.string(), false, readerProperties);
    if (!openStatus.ok())
    {
        throw std::runtime_error("Failed to open Parquet file: " + path.string() + " status=" + openStatus.ToString());
    }

    builder.properties(arrowReaderProperties);
    auto maybeReader = builder.Build();
    if (!maybeReader.ok())
    {
        throw std::runtime_error("Failed to build Parquet reader: " + path.string() + " status=" + maybeReader.status().ToString());
    }

    return std::move(maybeReader).ValueOrDie();
}
}

ParquetIngestResult ParquetCorpusIngestor::Run(const ParquetIngestConfig& config) const
{
    if (config.batchSizeRows == 0)
    {
        throw std::runtime_error("Parquet ingest batch size must be greater than zero.");
    }

    if (config.shardSizeBytes == 0)
    {
        throw std::runtime_error("Parquet ingest shard size must be greater than zero.");
    }

    const ScopedSignalHandler signalHandler;
    const std::vector<DiscoveredFile> discovered = DiscoverParquetFiles(config);
    const std::string discoveryChecksum = BuildDiscoveryChecksum(discovered);

    const std::filesystem::path manifestsRoot = config.outputRoot / "manifests" / "tokenizer";
    const std::filesystem::path reportsRoot = config.outputRoot / "reports" / "tokenizer";
    const std::filesystem::path progressPath = manifestsRoot / PROGRESS_FILENAME;
    const std::filesystem::path fileLogPath = manifestsRoot / FILE_LOG_FILENAME;
    const std::filesystem::path manifestPath = manifestsRoot / MANIFEST_FILENAME;
    const std::filesystem::path reportPath = reportsRoot / REPORT_FILENAME;
    const std::filesystem::path logPath = reportsRoot / LOG_FILENAME;

    ProgressState progress;
    progress.discoveryChecksum = discoveryChecksum;
    progress.discoveryCount = discovered.size();
    progress.lastSuccessfulFlushUtc = UtcTimestampNow();

    bool resumed = false;
    if (config.resume && std::filesystem::exists(progressPath))
    {
        progress = ParseProgressJson(ReadFile(progressPath));
        if (progress.discoveryChecksum != discoveryChecksum || progress.discoveryCount != discovered.size())
        {
            throw std::runtime_error("Resume checkpoint does not match the current Parquet discovery set.");
        }

        ++progress.resumedRunCount;
        progress.status = "running";
        resumed = true;
        RestoreCorpusShards(config.outputRoot, progress.currentOutputShardIndex, progress.currentOutputShardBytes);
        AppendLogLine(logPath, "resuming from file_index=" + std::to_string(progress.currentFileIndex)
            + " row_group=" + std::to_string(progress.currentRowGroupIndex)
            + " batch=" + std::to_string(progress.currentBatchIndex));
    }
    else
    {
        progress.status = "running";
        progress.discoveryChecksum = discoveryChecksum;
        progress.discoveryCount = discovered.size();
        progress.lastSuccessfulFlushUtc = UtcTimestampNow();
        RestoreCorpusShards(config.outputRoot, 0, 0);
        if (std::filesystem::exists(fileLogPath))
        {
            std::filesystem::remove(fileLogPath);
        }
        WriteFileAtomic(logPath, "# Parquet Ingest Log\n\n");
        AppendLogLine(logPath, "starting new ingest run");
        SaveProgress(progressPath, progress);
    }

    std::size_t processedBatchesThisRun = 0;
    std::size_t finishedFilesThisRun = 0;
    bool interrupted = false;
    const std::size_t resumeFileIndex = progress.currentFileIndex;
    const std::size_t resumeRowGroupIndex = progress.currentRowGroupIndex;
    const std::size_t resumeBatchIndex = progress.currentBatchIndex;

    for (std::size_t fileIndex = progress.currentFileIndex; fileIndex < discovered.size(); ++fileIndex)
    {
        const DiscoveredFile& file = discovered[fileIndex];
        const std::size_t fileStartShardIndex = progress.currentOutputShardIndex;
        const std::uint64_t fileStartShardBytes = progress.currentOutputShardBytes;
        const std::uint64_t totalRowsBeforeFile = progress.totalRowsProcessed;
        const std::uint64_t totalTextRowsBeforeFile = progress.totalTextRowsWritten;
        const std::uint64_t totalTextBytesBeforeFile = progress.totalTextBytesWritten;

        try
        {
            const bool resumeThisFile = resumed && fileIndex == resumeFileIndex;
            progress.currentFileIndex = fileIndex;
            progress.currentFileRelativePath = file.relativePath;
            if (!resumeThisFile)
            {
                progress.currentRowGroupIndex = 0;
                progress.currentBatchIndex = 0;
            }

            AppendLogLine(logPath, "opening file " + std::to_string(fileIndex + 1) + "/" + std::to_string(discovered.size())
                + " `" + file.relativePath + "`");

            std::unique_ptr<parquet::arrow::FileReader> reader = OpenParquetReader(file.absolutePath, config);
            const std::shared_ptr<arrow::Schema> schema = GetSchemaOrThrow(*reader);
            const std::optional<std::pair<int, std::string>> column = DetectTextColumn(schema, config);
            if (!column.has_value())
            {
                throw std::runtime_error("No supported text column was detected.");
            }

            FileRunStats fileStats;
            fileStats.detectedTextColumn = column->second;

            const std::shared_ptr<parquet::FileMetaData> metadata = reader->parquet_reader()->metadata();
            fileStats.rowGroupCount = static_cast<std::size_t>(metadata->num_row_groups());

            const std::vector<int> columnIndices = {column->first};
            const std::size_t startRowGroup = resumeThisFile ? resumeRowGroupIndex : 0;
            const std::size_t startBatchIndex = resumeThisFile ? resumeBatchIndex : 0;

            for (std::size_t rowGroupIndex = startRowGroup; rowGroupIndex < fileStats.rowGroupCount; ++rowGroupIndex)
            {
                const std::vector<int> rowGroups = {static_cast<int>(rowGroupIndex)};
                std::shared_ptr<arrow::RecordBatchReader> batchReader;
                const arrow::Status batchReaderStatus = reader->GetRecordBatchReader(rowGroups, columnIndices, &batchReader);
                if (!batchReaderStatus.ok())
                {
                    throw std::runtime_error("Failed to create record-batch reader for row group "
                        + std::to_string(rowGroupIndex) + ": " + batchReaderStatus.ToString());
                }

                std::size_t batchIndex = 0;
                for (arrow::Result<std::shared_ptr<arrow::RecordBatch>> maybeBatch : *batchReader)
                {
                    if (!maybeBatch.ok())
                    {
                        throw std::runtime_error("Failed to read Parquet batch: " + maybeBatch.status().ToString());
                    }

                    std::shared_ptr<arrow::RecordBatch> batch = std::move(maybeBatch).ValueOrDie();
                    if (resumeThisFile
                        && rowGroupIndex == startRowGroup
                        && batchIndex < startBatchIndex)
                    {
                        ++batchIndex;
                        continue;
                    }

                    if (batch == nullptr || batch->num_columns() == 0)
                    {
                        ++batchIndex;
                        continue;
                    }

                    const std::shared_ptr<arrow::Array> textArray = batch->column(0);
                    std::uint64_t estimatedTextRows = 0;
                    const std::uint64_t estimatedBatchBytes = EstimateBatchBytes(textArray, estimatedTextRows);
                    MaybeRotateShard(config, progress, estimatedBatchBytes);

                    const std::uint64_t rowsInBatch = static_cast<std::uint64_t>(batch->num_rows());
                    std::uint64_t textRowsWritten = 0;
                    const std::uint64_t writtenBytes = WriteBatchText(config.outputRoot, progress, textArray, textRowsWritten);

                    progress.totalRowsProcessed += rowsInBatch;
                    fileStats.rowsProcessed += rowsInBatch;
                    fileStats.textRowsWritten += textRowsWritten;
                    fileStats.textBytesWritten += writtenBytes;
                    progress.currentFileIndex = fileIndex;
                    progress.currentFileRelativePath = file.relativePath;
                    progress.currentRowGroupIndex = rowGroupIndex;
                    progress.currentBatchIndex = batchIndex + 1;
                    progress.lastSuccessfulFlushUtc = UtcTimestampNow();
                    SaveProgress(progressPath, progress);

                    AppendLogLine(logPath,
                        "file=" + file.relativePath
                        + " file_index=" + std::to_string(fileIndex + 1) + "/" + std::to_string(discovered.size())
                        + " row_group=" + std::to_string(rowGroupIndex + 1) + "/" + std::to_string(fileStats.rowGroupCount)
                        + " batch=" + std::to_string(batchIndex + 1)
                        + " rows_processed=" + std::to_string(progress.totalRowsProcessed)
                        + " text_bytes_written=" + std::to_string(progress.totalTextBytesWritten)
                        + " remaining_files=" + std::to_string(discovered.size() - fileIndex - 1));

                    ++batchIndex;
                    ++processedBatchesThisRun;

                    if (config.maxBatches > 0 && processedBatchesThisRun >= config.maxBatches)
                    {
                        interrupted = true;
                        break;
                    }

                    if (g_stopRequested.load())
                    {
                        interrupted = true;
                        break;
                    }
                }

                if (interrupted)
                {
                    break;
                }

                progress.currentRowGroupIndex = rowGroupIndex + 1;
                progress.currentBatchIndex = 0;
                progress.lastSuccessfulFlushUtc = UtcTimestampNow();
                SaveProgress(progressPath, progress);
            }

            if (interrupted)
            {
                break;
            }

            LoggedFileRecord record;
            record.relativePath = file.relativePath;
            record.status = "completed";
            record.detectedTextColumn = fileStats.detectedTextColumn;
            record.inputBytes = file.inputBytes;
            record.rowGroupCount = fileStats.rowGroupCount;
            record.rowsProcessed = fileStats.rowsProcessed;
            record.textRowsWritten = fileStats.textRowsWritten;
            record.textBytesWritten = fileStats.textBytesWritten;
            record.note = "processed";
            AppendLoggedFileRecord(fileLogPath, record);

            ++progress.completedFileCount;
            progress.currentFileIndex = fileIndex + 1;
            progress.currentFileRelativePath.clear();
            progress.currentRowGroupIndex = 0;
            progress.currentBatchIndex = 0;
            progress.lastSuccessfulFlushUtc = UtcTimestampNow();
            SaveProgress(progressPath, progress);

            ++finishedFilesThisRun;
            if (config.maxFiles > 0 && finishedFilesThisRun >= config.maxFiles)
            {
                interrupted = true;
                break;
            }
        }
        catch (const std::exception& exception)
        {
            const bool hadPartialFileOutput =
                progress.totalRowsProcessed != totalRowsBeforeFile
                || progress.totalTextRowsWritten != totalTextRowsBeforeFile
                || progress.totalTextBytesWritten != totalTextBytesBeforeFile
                || progress.currentOutputShardIndex != fileStartShardIndex
                || progress.currentOutputShardBytes != fileStartShardBytes;

            if (hadPartialFileOutput)
            {
                RestoreCorpusShards(config.outputRoot, fileStartShardIndex, fileStartShardBytes);
                progress.currentOutputShardIndex = fileStartShardIndex;
                progress.currentOutputShardBytes = fileStartShardBytes;
                progress.totalRowsProcessed = totalRowsBeforeFile;
                progress.totalTextRowsWritten = totalTextRowsBeforeFile;
                progress.totalTextBytesWritten = totalTextBytesBeforeFile;
            }

            if (config.stopOnFileError)
            {
                throw;
            }

            LoggedFileRecord record;
            record.relativePath = file.relativePath;
            record.status = "skipped";
            record.inputBytes = file.inputBytes;
            record.note = exception.what();
            AppendLoggedFileRecord(fileLogPath, record);

            ++progress.skippedFileCount;
            progress.currentFileIndex = fileIndex + 1;
            progress.currentFileRelativePath.clear();
            progress.currentRowGroupIndex = 0;
            progress.currentBatchIndex = 0;
            progress.lastSuccessfulFlushUtc = UtcTimestampNow();
            SaveProgress(progressPath, progress);
            AppendLogLine(logPath, "skipped file `" + file.relativePath + "` reason=`" + std::string(exception.what()) + "`");
        }
    }

    progress.status = interrupted ? "interrupted" : "completed";
    progress.lastSuccessfulFlushUtc = UtcTimestampNow();
    SaveProgress(progressPath, progress);
    WriteManifestAndReport(config, progress, discovered, fileLogPath, manifestPath, reportPath);
    AppendLogLine(logPath, std::string("run finished with status=") + progress.status);

    ParquetIngestResult result;
    result.resumed = resumed;
    result.completed = !interrupted;
    result.interrupted = interrupted;
    result.discoveredFileCount = discovered.size();
    result.completedFileCount = progress.completedFileCount;
    result.skippedFileCount = progress.skippedFileCount;
    result.totalRowsProcessed = progress.totalRowsProcessed;
    result.totalTextRowsWritten = progress.totalTextRowsWritten;
    result.totalTextBytesWritten = progress.totalTextBytesWritten;
    result.outputShards = ListOutputShards(config.outputRoot);
    result.progressPath = progressPath;
    result.fileLogPath = fileLogPath;
    result.manifestPath = manifestPath;
    result.reportPath = reportPath;
    result.logPath = logPath;
    return result;
}
}

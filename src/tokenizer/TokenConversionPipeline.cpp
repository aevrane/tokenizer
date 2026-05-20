#include "tokenizer/TokenConversionPipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "tokenizer/SharedTokenizer.h"

namespace Mina::Tokenizer
{
namespace
{
using Clock = std::chrono::steady_clock;

constexpr const char* PROGRESS_FILENAME = "progress.json";
constexpr const char* ORDERED_FILES_FILENAME = "ordered_files.tsv";
constexpr const char* FILE_RECORDS_FILENAME = "file_records.tsv";
constexpr const char* MANIFEST_FILENAME = "token_manifest.json";
constexpr const char* REPORT_FILENAME = "conversion_report.md";
constexpr const char* LOG_FILENAME = "conversion.log";
constexpr const char* TOKENS_DIRECTORY = "tokens";

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

struct TokenizerIdentity
{
    std::string tokenizerId;
    std::string tokenizerVersion;
    std::string tokenizerModelHash;
    std::filesystem::path modelPath;
    std::filesystem::path manifestPath;
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
    std::string tokenizerId;
    std::string tokenizerVersion;
    std::string tokenizerModelHash;
    std::string inputRoot;
    std::string outputRoot;
    std::string orderedFilesPath;
    std::string discoveryChecksum;
    std::string startTimeUtc;
    std::string lastUpdateUtc;
    std::string currentFileRelativePath;
    std::string currentFilePhase = "idle";
    std::string lastFullyCompletedFile;
    std::size_t totalFilesDiscovered = 0;
    std::size_t configuredWorkerCount = 1;
    std::size_t activeWorkerCount = 0;
    std::size_t currentFileIndex = 0;
    std::size_t currentSectionIndex = 0;
    std::size_t currentSectionCount = 0;
    std::size_t completedFileCount = 0;
    std::size_t skippedFileCount = 0;
    std::size_t failedFileCount = 0;
    std::size_t resumedRunCount = 0;
    std::uint64_t currentFileBytesProcessed = 0;
    std::uint64_t currentFileTotalBytes = 0;
    std::uint64_t currentFileLinesProcessed = 0;
    std::uint64_t currentFileTokensProduced = 0;
    std::uint64_t currentFileOutputBytesWritten = 0;
    std::uint64_t totalTokensWritten = 0;
    std::uint64_t totalUnknownTokens = 0;
};

struct RunState
{
    bool active = false;
    std::size_t workerSlot = 0;
    Clock::time_point runStart = Clock::now();
    Clock::time_point currentFileStart = Clock::now();
    std::string currentFileRelativePath;
    std::string currentFilePhase = "idle";
    std::size_t currentFileIndex = 0;
    std::size_t currentSectionIndex = 0;
    std::size_t currentSectionCount = 0;
    std::uint64_t currentFileBytesProcessed = 0;
    std::uint64_t currentFileTotalBytes = 0;
    std::uint64_t currentFileLinesProcessed = 0;
    std::uint64_t currentFileTokensProduced = 0;
    std::uint64_t currentFileOutputBytesWritten = 0;
};

struct FileOutput
{
    std::uint64_t tokenCount = 0;
    std::uint64_t unknownTokenCount = 0;
    std::uint64_t outputBytes = 0;
};

struct SectionBuffer
{
    std::size_t index = 0;
    std::size_t startLineIndex = 0;
    std::size_t endLineIndex = 0;
    std::size_t startTextOffset = 0;
    std::size_t endTextOffset = 0;
    std::uint64_t bytesProcessed = 0;
    std::uint64_t linesProcessed = 0;
};

struct EncodedSection
{
    std::size_t index = 0;
    std::uint64_t bytesProcessed = 0;
    std::uint64_t linesProcessed = 0;
    std::vector<std::uint16_t> tokenIds;
    std::uint64_t unknownTokenCount = 0;
};

struct SectionReadResult
{
    std::string normalizedText;
    std::vector<std::size_t> lineStartOffsets;
    std::vector<SectionBuffer> sections;
    std::size_t totalLines = 0;
    std::size_t sectionCount = 0;
};

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

    for (int attempt = 0; attempt < 20; ++attempt)
    {
        std::error_code ignored;
        if (std::filesystem::exists(path))
        {
            std::filesystem::remove(path, ignored);
        }

        std::error_code renameError;
        std::filesystem::rename(tempPath, path, renameError);
        if (!renameError)
        {
            return;
        }

        if (attempt == 19)
        {
            throw std::runtime_error("rename: " + renameError.message()
                + ": \"" + tempPath.string() + "\", \"" + path.string() + "\"");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

void AppendTextLine(const std::filesystem::path& path, const std::string& line)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to append file: " + path.string());
    }

    stream << line << '\n';
    if (!stream.good())
    {
        throw std::runtime_error("Failed while appending file: " + path.string());
    }
}

std::string UtcTimestampNow()
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);

    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &timeValue);
#else
    gmtime_r(&timeValue, &utcTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string FormatDuration(const Clock::duration duration)
{
    const std::uint64_t totalSeconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(duration).count());
    const std::uint64_t hours = totalSeconds / 3600ULL;
    const std::uint64_t minutes = (totalSeconds % 3600ULL) / 60ULL;
    const std::uint64_t seconds = totalSeconds % 60ULL;

    std::ostringstream stream;
    if (hours > 0)
    {
        stream << hours << "h ";
    }
    if (hours > 0 || minutes > 0)
    {
        stream << minutes << "m ";
    }
    stream << seconds << "s";
    return stream.str();
}

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
    while (end < json.size() && json[end] >= '0' && json[end] <= '9')
    {
        ++end;
    }

    if (end == position)
    {
        return 0;
    }

    return static_cast<std::uint64_t>(std::stoull(json.substr(position, end - position)));
}

TokenizerIdentity LoadTokenizerIdentity(const std::filesystem::path& tokenizerRoot)
{
    TokenizerIdentity identity;
    identity.modelPath = tokenizerRoot / "manifests" / "tokenizer" / "shared_tokenizer.model";
    identity.manifestPath = tokenizerRoot / "manifests" / "tokenizer" / "shared_tokenizer.manifest.json";

    if (!std::filesystem::exists(identity.modelPath))
    {
        throw std::runtime_error("Shared tokenizer model is missing: " + identity.modelPath.string());
    }
    if (!std::filesystem::exists(identity.manifestPath))
    {
        throw std::runtime_error("Shared tokenizer manifest is missing: " + identity.manifestPath.string());
    }

    const std::string manifestText = ReadFile(identity.manifestPath);
    identity.tokenizerId = ExtractJsonString(manifestText, "tokenizer_id");
    identity.tokenizerVersion = ExtractJsonString(manifestText, "tokenizer_version");
    identity.tokenizerModelHash = MakeChecksum(ReadFile(identity.modelPath));

    if (identity.tokenizerId.empty() || identity.tokenizerVersion.empty())
    {
        throw std::runtime_error("Shared tokenizer manifest is missing tokenizer_id or tokenizer_version.");
    }

    return identity;
}

bool IsEligibleTextFile(const std::filesystem::path& path)
{
    const std::string extension = path.extension().string();
    return extension == ".txt" || extension == ".text";
}

std::vector<DiscoveredFile> DiscoverFiles(const TokenConversionConfig& config)
{
    if (!std::filesystem::exists(config.inputRoot))
    {
        throw std::runtime_error("Input root does not exist: " + config.inputRoot.string());
    }

    std::vector<DiscoveredFile> discovered;
    if (config.recursiveDiscovery)
    {
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::recursive_directory_iterator(config.inputRoot))
        {
            if (!entry.is_regular_file() || !IsEligibleTextFile(entry.path()))
            {
                continue;
            }

            DiscoveredFile file;
            file.absolutePath = entry.path();
            file.relativePath = std::filesystem::relative(entry.path(), config.inputRoot).generic_string();
            file.inputBytes = static_cast<std::uint64_t>(entry.file_size());
            discovered.push_back(file);
        }
    }
    else
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(config.inputRoot))
        {
            if (!entry.is_regular_file() || !IsEligibleTextFile(entry.path()))
            {
                continue;
            }

            DiscoveredFile file;
            file.absolutePath = entry.path();
            file.relativePath = std::filesystem::relative(entry.path(), config.inputRoot).generic_string();
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

    return discovered;
}

std::filesystem::path OrderedFilesPath(const TokenConversionConfig& config)
{
    return config.outputRoot / ORDERED_FILES_FILENAME;
}

std::filesystem::path FileRecordsPath(const TokenConversionConfig& config)
{
    return config.outputRoot / FILE_RECORDS_FILENAME;
}

std::filesystem::path ProgressPath(const TokenConversionConfig& config)
{
    return config.outputRoot / PROGRESS_FILENAME;
}

std::filesystem::path ManifestPath(const TokenConversionConfig& config)
{
    return config.outputRoot / MANIFEST_FILENAME;
}

std::filesystem::path ReportPath(const TokenConversionConfig& config)
{
    return config.outputRoot / REPORT_FILENAME;
}

std::filesystem::path LogPath(const TokenConversionConfig& config)
{
    return config.outputRoot / LOG_FILENAME;
}

std::uint64_t ProgressCheckpointBytes(const std::uint64_t totalBytes)
{
    const std::uint64_t tenPercent = totalBytes / 10ULL;
    const std::uint64_t minimumCheckpoint = 32ULL * 1024ULL * 1024ULL;
    return std::max<std::uint64_t>(1ULL, std::max<std::uint64_t>(tenPercent, minimumCheckpoint));
}

std::filesystem::path OutputPathForRelativeInput(
    const TokenConversionConfig& config,
    const std::string& relativeInputPath)
{
    std::filesystem::path relative(relativeInputPath);
    relative.replace_extension(".tokens.bin");
    return config.outputRoot / TOKENS_DIRECTORY / relative;
}

std::filesystem::path TempOutputPathForRelativeInput(
    const TokenConversionConfig& config,
    const std::string& relativeInputPath)
{
    return std::filesystem::path(OutputPathForRelativeInput(config, relativeInputPath).string() + ".tmp");
}

std::string BuildDiscoveryChecksum(const std::vector<DiscoveredFile>& discovered)
{
    std::ostringstream stream;
    for (const DiscoveredFile& file : discovered)
    {
        stream << file.relativePath << '\t' << file.inputBytes << '\n';
    }

    return MakeChecksum(stream.str());
}

void WriteOrderedFilesManifest(
    const std::filesystem::path& path,
    const std::vector<DiscoveredFile>& discovered)
{
    std::ostringstream stream;
    stream << "index\trelative_path\tinput_bytes\n";
    for (std::size_t index = 0; index < discovered.size(); ++index)
    {
        stream << index << '\t'
               << EscapeTsv(discovered[index].relativePath) << '\t'
               << discovered[index].inputBytes << '\n';
    }
    WriteFileAtomic(path, stream.str());
}

std::string BuildProgressJson(const ProgressState& progress)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"status\": " << QuoteJson(progress.status) << ",\n";
    stream << "  \"tokenizer_id\": " << QuoteJson(progress.tokenizerId) << ",\n";
    stream << "  \"tokenizer_version\": " << QuoteJson(progress.tokenizerVersion) << ",\n";
    stream << "  \"tokenizer_model_hash\": " << QuoteJson(progress.tokenizerModelHash) << ",\n";
    stream << "  \"input_root\": " << QuoteJson(progress.inputRoot) << ",\n";
    stream << "  \"output_root\": " << QuoteJson(progress.outputRoot) << ",\n";
    stream << "  \"ordered_files_path\": " << QuoteJson(progress.orderedFilesPath) << ",\n";
    stream << "  \"discovery_checksum\": " << QuoteJson(progress.discoveryChecksum) << ",\n";
    stream << "  \"start_time_utc\": " << QuoteJson(progress.startTimeUtc) << ",\n";
    stream << "  \"last_update_time_utc\": " << QuoteJson(progress.lastUpdateUtc) << ",\n";
    stream << "  \"current_file_relative_path\": " << QuoteJson(progress.currentFileRelativePath) << ",\n";
    stream << "  \"current_file_phase\": " << QuoteJson(progress.currentFilePhase) << ",\n";
    stream << "  \"last_fully_completed_file\": " << QuoteJson(progress.lastFullyCompletedFile) << ",\n";
    stream << "  \"total_files_discovered\": " << progress.totalFilesDiscovered << ",\n";
    stream << "  \"configured_worker_count\": " << progress.configuredWorkerCount << ",\n";
    stream << "  \"active_worker_count\": " << progress.activeWorkerCount << ",\n";
    stream << "  \"current_file_index\": " << progress.currentFileIndex << ",\n";
    stream << "  \"current_section_index\": " << progress.currentSectionIndex << ",\n";
    stream << "  \"current_section_count\": " << progress.currentSectionCount << ",\n";
    stream << "  \"completed_file_count\": " << progress.completedFileCount << ",\n";
    stream << "  \"skipped_file_count\": " << progress.skippedFileCount << ",\n";
    stream << "  \"failed_file_count\": " << progress.failedFileCount << ",\n";
    stream << "  \"resumed_run_count\": " << progress.resumedRunCount << ",\n";
    stream << "  \"current_file_bytes_processed\": " << progress.currentFileBytesProcessed << ",\n";
    stream << "  \"current_file_total_bytes\": " << progress.currentFileTotalBytes << ",\n";
    stream << "  \"current_file_lines_processed\": " << progress.currentFileLinesProcessed << ",\n";
    stream << "  \"current_file_tokens_produced\": " << progress.currentFileTokensProduced << ",\n";
    stream << "  \"current_file_output_bytes_written\": " << progress.currentFileOutputBytesWritten << ",\n";
    stream << "  \"total_tokens_written\": " << progress.totalTokensWritten << ",\n";
    stream << "  \"total_unknown_tokens\": " << progress.totalUnknownTokens << "\n";
    stream << "}\n";
    return stream.str();
}

ProgressState ParseProgressJson(const std::string& json)
{
    ProgressState progress;
    progress.status = ExtractJsonString(json, "status");
    progress.tokenizerId = ExtractJsonString(json, "tokenizer_id");
    progress.tokenizerVersion = ExtractJsonString(json, "tokenizer_version");
    progress.tokenizerModelHash = ExtractJsonString(json, "tokenizer_model_hash");
    progress.inputRoot = ExtractJsonString(json, "input_root");
    progress.outputRoot = ExtractJsonString(json, "output_root");
    progress.orderedFilesPath = ExtractJsonString(json, "ordered_files_path");
    progress.discoveryChecksum = ExtractJsonString(json, "discovery_checksum");
    progress.startTimeUtc = ExtractJsonString(json, "start_time_utc");
    progress.lastUpdateUtc = ExtractJsonString(json, "last_update_time_utc");
    progress.currentFileRelativePath = ExtractJsonString(json, "current_file_relative_path");
    progress.currentFilePhase = ExtractJsonString(json, "current_file_phase");
    progress.lastFullyCompletedFile = ExtractJsonString(json, "last_fully_completed_file");
    progress.totalFilesDiscovered = static_cast<std::size_t>(ExtractJsonUnsigned(json, "total_files_discovered"));
    progress.configuredWorkerCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "configured_worker_count"));
    progress.activeWorkerCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "active_worker_count"));
    progress.currentFileIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_file_index"));
    progress.currentSectionIndex = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_section_index"));
    progress.currentSectionCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "current_section_count"));
    progress.completedFileCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "completed_file_count"));
    progress.skippedFileCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "skipped_file_count"));
    progress.failedFileCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "failed_file_count"));
    progress.resumedRunCount = static_cast<std::size_t>(ExtractJsonUnsigned(json, "resumed_run_count"));
    progress.currentFileBytesProcessed = ExtractJsonUnsigned(json, "current_file_bytes_processed");
    progress.currentFileTotalBytes = ExtractJsonUnsigned(json, "current_file_total_bytes");
    progress.currentFileLinesProcessed = ExtractJsonUnsigned(json, "current_file_lines_processed");
    progress.currentFileTokensProduced = ExtractJsonUnsigned(json, "current_file_tokens_produced");
    progress.currentFileOutputBytesWritten = ExtractJsonUnsigned(json, "current_file_output_bytes_written");
    progress.totalTokensWritten = ExtractJsonUnsigned(json, "total_tokens_written");
    progress.totalUnknownTokens = ExtractJsonUnsigned(json, "total_unknown_tokens");

    if (progress.currentFilePhase.empty())
    {
        progress.currentFilePhase = "idle";
    }
    if (progress.configuredWorkerCount == 0)
    {
        progress.configuredWorkerCount = 1;
    }

    if (progress.status.empty() || progress.tokenizerId.empty() || progress.tokenizerVersion.empty())
    {
        throw std::runtime_error("Malformed progress file: missing required metadata.");
    }

    return progress;
}

void SaveProgress(const std::filesystem::path& path, const ProgressState& progress)
{
    WriteFileAtomic(path, BuildProgressJson(progress));
}

void AppendLog(const std::filesystem::path& path, const std::string& level, const std::string& message)
{
    AppendTextLine(path, "[" + UtcTimestampNow() + "] [" + level + "] " + message);
}

std::map<std::string, TokenConversionFileRecord> LoadFileRecords(const std::filesystem::path& path)
{
    std::map<std::string, TokenConversionFileRecord> records;
    if (!std::filesystem::exists(path))
    {
        return records;
    }

    const std::vector<std::string> lines = SplitLines(ReadFile(path));
    bool header = true;
    for (const std::string& line : lines)
    {
        if (header)
        {
            header = false;
            continue;
        }

        const std::vector<std::string> fields = SplitTabLine(line);
        if (fields.size() < 8)
        {
            continue;
        }

        TokenConversionFileRecord record;
        record.relativeInputPath = UnescapeTsv(fields[0]);
        record.relativeOutputPath = UnescapeTsv(fields[1]);
        record.status = UnescapeTsv(fields[2]);
        record.inputBytes = static_cast<std::uint64_t>(std::stoull(fields[3]));
        record.outputBytes = static_cast<std::uint64_t>(std::stoull(fields[4]));
        record.tokenCount = static_cast<std::uint64_t>(std::stoull(fields[5]));
        record.unknownTokenCount = static_cast<std::uint64_t>(std::stoull(fields[6]));
        record.note = UnescapeTsv(fields[7]);
        records[record.relativeInputPath] = record;
    }

    return records;
}

void WriteFileRecords(
    const std::filesystem::path& path,
    const std::map<std::string, TokenConversionFileRecord>& records)
{
    std::ostringstream stream;
    stream << "relative_input_path\trelative_output_path\tstatus\tinput_bytes\toutput_bytes\ttoken_count\tunknown_token_count\tnote\n";

    for (const auto& pair : records)
    {
        const TokenConversionFileRecord& record = pair.second;
        stream << EscapeTsv(record.relativeInputPath) << '\t'
               << EscapeTsv(record.relativeOutputPath) << '\t'
               << EscapeTsv(record.status) << '\t'
               << record.inputBytes << '\t'
               << record.outputBytes << '\t'
               << record.tokenCount << '\t'
               << record.unknownTokenCount << '\t'
               << EscapeTsv(record.note) << '\n';
    }

    WriteFileAtomic(path, stream.str());
}

double Percentage(const std::uint64_t numerator, const std::uint64_t denominator)
{
    if (denominator == 0)
    {
        return 0.0;
    }

    return (static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator);
}

std::string BuildManifestJson(
    const TokenConversionConfig& config,
    const TokenizerIdentity& identity,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::map<std::string, TokenConversionFileRecord>& fileRecords)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"tokenizer_id\": " << QuoteJson(identity.tokenizerId) << ",\n";
    stream << "  \"tokenizer_version\": " << QuoteJson(identity.tokenizerVersion) << ",\n";
    stream << "  \"tokenizer_model_hash\": " << QuoteJson(identity.tokenizerModelHash) << ",\n";
    stream << "  \"token_id_type\": \"uint16_le\",\n";
    stream << "  \"input_root\": " << QuoteJson(config.inputRoot.string()) << ",\n";
    stream << "  \"output_root\": " << QuoteJson(config.outputRoot.string()) << ",\n";
    stream << "  \"status\": " << QuoteJson(progress.status) << ",\n";
    stream << "  \"total_files_discovered\": " << discovered.size() << ",\n";
    stream << "  \"completed_file_count\": " << progress.completedFileCount << ",\n";
    stream << "  \"skipped_file_count\": " << progress.skippedFileCount << ",\n";
    stream << "  \"failed_file_count\": " << progress.failedFileCount << ",\n";
    stream << "  \"total_tokens_written\": " << progress.totalTokensWritten << ",\n";
    stream << "  \"total_unknown_tokens\": " << progress.totalUnknownTokens << ",\n";
    stream << "  \"ordered_files\": [\n";

    for (std::size_t index = 0; index < discovered.size(); ++index)
    {
        const DiscoveredFile& file = discovered[index];
        const std::map<std::string, TokenConversionFileRecord>::const_iterator it = fileRecords.find(file.relativePath);
        const TokenConversionFileRecord* record = it == fileRecords.end() ? nullptr : &it->second;

        stream << "    {\n";
        stream << "      \"index\": " << index << ",\n";
        stream << "      \"relative_input_path\": " << QuoteJson(file.relativePath) << ",\n";
        stream << "      \"input_bytes\": " << file.inputBytes << ",\n";
        stream << "      \"relative_output_path\": "
               << QuoteJson(record
                                ? record->relativeOutputPath
                                : OutputPathForRelativeInput(config, file.relativePath)
                                      .lexically_relative(config.outputRoot)
                                      .generic_string())
               << ",\n";
        stream << "      \"status\": " << QuoteJson(record ? record->status : "pending") << ",\n";
        stream << "      \"token_count\": " << (record ? record->tokenCount : 0) << ",\n";
        stream << "      \"unknown_token_count\": " << (record ? record->unknownTokenCount : 0) << ",\n";
        stream << "      \"note\": " << QuoteJson(record ? record->note : std::string()) << '\n';
        stream << "    }";
        if (index + 1 < discovered.size())
        {
            stream << ",";
        }
        stream << '\n';
    }

    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::string BuildReportMarkdown(
    const TokenConversionConfig& config,
    const TokenizerIdentity& identity,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::map<std::string, TokenConversionFileRecord>& fileRecords)
{
    const std::uint64_t processedFiles =
        static_cast<std::uint64_t>(progress.completedFileCount + progress.skippedFileCount + progress.failedFileCount);
    const double unknownPercentage = Percentage(progress.totalUnknownTokens, progress.totalTokensWritten);
    const double skippedPercentage = Percentage(progress.skippedFileCount, discovered.size());
    const double successPercentage = Percentage(progress.completedFileCount, discovered.size());

    std::vector<const TokenConversionFileRecord*> skipped;
    std::vector<const TokenConversionFileRecord*> failed;
    std::vector<const TokenConversionFileRecord*> completed;

    for (const auto& pair : fileRecords)
    {
        if (pair.second.status == "skipped")
        {
            skipped.push_back(&pair.second);
        }
        else if (pair.second.status == "failed")
        {
            failed.push_back(&pair.second);
        }
        else if (pair.second.status == "completed")
        {
            completed.push_back(&pair.second);
        }
    }

    std::sort(
        completed.begin(),
        completed.end(),
        [](const TokenConversionFileRecord* left, const TokenConversionFileRecord* right)
        {
            return left->relativeInputPath < right->relativeInputPath;
        });

    std::ostringstream stream;
    stream << "# Token Conversion Report\n\n";
    stream << "- tokenizer identifier: `" << identity.tokenizerId << "`\n";
    stream << "- tokenizer version: `" << identity.tokenizerVersion << "`\n";
    stream << "- tokenizer model hash: `" << identity.tokenizerModelHash << "`\n";
    stream << "- input root: `" << config.inputRoot.string() << "`\n";
    stream << "- output root: `" << config.outputRoot.string() << "`\n";
    stream << "- total source files discovered: `" << discovered.size() << "`\n";
    stream << "- total source files completed: `" << progress.completedFileCount << "`\n";
    stream << "- total source files skipped: `" << progress.skippedFileCount << "`\n";
    stream << "- total source files failed: `" << progress.failedFileCount << "`\n";
    stream << "- total processed records: `" << processedFiles << "`\n";
    stream << "- total tokens written: `" << progress.totalTokensWritten << "`\n";
    stream << "- total unknown tokens encountered: `" << progress.totalUnknownTokens << "`\n";
    stream << "- unknown-token percentage: `" << std::fixed << std::setprecision(4) << unknownPercentage << "%`\n";
    stream << "- skipped-file percentage: `" << skippedPercentage << "%`\n";
    stream << "- success percentage: `" << successPercentage << "%`\n";
    stream << "- run start time: `" << progress.startTimeUtc << "`\n";
    stream << "- last update time: `" << progress.lastUpdateUtc << "`\n";
    stream << "- fully completed: `" << (progress.status == "completed" ? "yes" : "no") << "`\n";
    stream << "- last fully completed file: `"
           << (progress.lastFullyCompletedFile.empty() ? std::string("<none>") : progress.lastFullyCompletedFile)
           << "`\n\n";

    stream << "## Per-File Token Counts\n\n";
    if (completed.empty())
    {
        stream << "- No files have completed yet.\n";
    }
    else
    {
        for (const TokenConversionFileRecord* record : completed)
        {
            stream << "- `" << record->relativeInputPath << "` -> `" << record->relativeOutputPath
                   << "`: `" << record->tokenCount << "` tokens, `" << record->unknownTokenCount << "` unknowns\n";
        }
    }
    stream << '\n';

    stream << "## Skipped Files\n\n";
    if (skipped.empty())
    {
        stream << "- None.\n";
    }
    else
    {
        for (const TokenConversionFileRecord* record : skipped)
        {
            stream << "- `" << record->relativeInputPath << "`: " << record->note << '\n';
        }
    }
    stream << '\n';

    stream << "## Files With Errors\n\n";
    if (failed.empty())
    {
        stream << "- None.\n";
    }
    else
    {
        for (const TokenConversionFileRecord* record : failed)
        {
            stream << "- `" << record->relativeInputPath << "`: " << record->note << '\n';
        }
    }

    return stream.str();
}

void SaveManifestAndReport(
    const TokenConversionConfig& config,
    const TokenizerIdentity& identity,
    const ProgressState& progress,
    const std::vector<DiscoveredFile>& discovered,
    const std::map<std::string, TokenConversionFileRecord>& fileRecords)
{
    WriteFileAtomic(ManifestPath(config), BuildManifestJson(config, identity, progress, discovered, fileRecords));
    WriteFileAtomic(ReportPath(config), BuildReportMarkdown(config, identity, progress, discovered, fileRecords));
}

std::size_t ResolveWorkerCount(const TokenConversionConfig& config)
{
    if (config.workerCount > 0)
    {
        return config.workerCount;
    }

    const unsigned int detected = std::thread::hardware_concurrency();
    const std::size_t hardwareWorkers = detected == 0U ? 1ULL : static_cast<std::size_t>(detected);
    if (config.halfCpuUsage)
    {
        return std::max<std::size_t>(1, (hardwareWorkers + 1ULL) / 2ULL);
    }

    return std::max<std::size_t>(1, hardwareWorkers);
}

bool IsCompletedRecord(
    const TokenConversionConfig& config,
    const TokenConversionFileRecord& record)
{
    return record.status == "completed"
        && !record.relativeInputPath.empty()
        && std::filesystem::exists(OutputPathForRelativeInput(config, record.relativeInputPath));
}

std::size_t ComputeContiguousCompletedIndex(
    const TokenConversionConfig& config,
    const std::vector<DiscoveredFile>& discovered,
    const std::map<std::string, TokenConversionFileRecord>& fileRecords,
    std::string& lastFullyCompletedFile)
{
    std::size_t contiguousIndex = 0;
    lastFullyCompletedFile.clear();

    for (std::size_t index = 0; index < discovered.size(); ++index)
    {
        const auto it = fileRecords.find(discovered[index].relativePath);
        if (it == fileRecords.end() || !IsCompletedRecord(config, it->second))
        {
            break;
        }

        contiguousIndex = index + 1;
        lastFullyCompletedFile = discovered[index].relativePath;
    }

    return contiguousIndex;
}

std::string BuildActiveFileSummary(const std::vector<RunState>& workerStates)
{
    std::ostringstream stream;
    std::size_t displayed = 0;
    std::size_t totalActive = 0;

    for (const RunState& workerState : workerStates)
    {
        if (!workerState.active || workerState.currentFileRelativePath.empty())
        {
            continue;
        }

        ++totalActive;
        if (displayed < 3)
        {
            if (displayed > 0)
            {
                stream << " | ";
            }
            stream << "w" << workerState.workerSlot << ":" << workerState.currentFileRelativePath;
            ++displayed;
        }
    }

    if (totalActive > displayed)
    {
        if (displayed > 0)
        {
            stream << " | ";
        }
        stream << "+" << (totalActive - displayed) << " more";
    }

    return stream.str();
}

void RecomputeProgressState(
    const TokenConversionConfig& config,
    const std::vector<DiscoveredFile>& discovered,
    const std::map<std::string, TokenConversionFileRecord>& fileRecords,
    const std::vector<RunState>& workerStates,
    ProgressState& progress)
{
    progress.completedFileCount = 0;
    progress.skippedFileCount = 0;
    progress.failedFileCount = 0;
    progress.totalTokensWritten = 0;
    progress.totalUnknownTokens = 0;
    progress.activeWorkerCount = 0;

    for (const auto& pair : fileRecords)
    {
        const TokenConversionFileRecord& record = pair.second;
        if (record.status == "completed" && IsCompletedRecord(config, record))
        {
            ++progress.completedFileCount;
            progress.totalTokensWritten += record.tokenCount;
            progress.totalUnknownTokens += record.unknownTokenCount;
        }
        else if (record.status == "skipped")
        {
            ++progress.skippedFileCount;
        }
        else if (record.status == "failed")
        {
            ++progress.failedFileCount;
        }
    }

    progress.currentFileRelativePath.clear();
    progress.currentFilePhase = "idle";
    progress.currentSectionIndex = 0;
    progress.currentSectionCount = 0;
    progress.currentFileBytesProcessed = 0;
    progress.currentFileTotalBytes = 0;
    progress.currentFileLinesProcessed = 0;
    progress.currentFileTokensProduced = 0;
    progress.currentFileOutputBytesWritten = 0;

    progress.currentFileIndex =
        ComputeContiguousCompletedIndex(config, discovered, fileRecords, progress.lastFullyCompletedFile);

    for (const RunState& workerState : workerStates)
    {
        if (!workerState.active)
        {
            continue;
        }

        ++progress.activeWorkerCount;
        if (progress.currentFileRelativePath.empty())
        {
            progress.currentFileRelativePath = workerState.currentFileRelativePath;
            progress.currentFilePhase = workerState.currentFilePhase;
            progress.currentSectionIndex = workerState.currentSectionIndex;
            progress.currentSectionCount = workerState.currentSectionCount;
            progress.currentFileBytesProcessed = workerState.currentFileBytesProcessed;
            progress.currentFileTotalBytes = workerState.currentFileTotalBytes;
            progress.currentFileLinesProcessed = workerState.currentFileLinesProcessed;
            progress.currentFileTokensProduced = workerState.currentFileTokensProduced;
            progress.currentFileOutputBytesWritten = workerState.currentFileOutputBytesWritten;
            progress.currentFileIndex = workerState.currentFileIndex;
        }
    }

    if (progress.activeWorkerCount > 1)
    {
        progress.currentFileRelativePath = BuildActiveFileSummary(workerStates);
        progress.currentFilePhase = "parallel";
    }
    else if (progress.activeWorkerCount == 0 && progress.currentFileIndex < discovered.size())
    {
        progress.currentFileRelativePath = discovered[progress.currentFileIndex].relativePath;
    }

    progress.lastUpdateUtc = UtcTimestampNow();
}

void PrintProgressLine(
    const ProgressState& progress,
    const std::vector<RunState>& workerStates,
    const Clock::time_point runStart,
    const std::size_t totalFiles)
{
    const double percent = Percentage(progress.completedFileCount, totalFiles);
    const double filePercent = Percentage(progress.currentFileBytesProcessed, progress.currentFileTotalBytes);
    std::cout << "[progress] file " << (std::min(progress.currentFileIndex + 1, totalFiles))
              << "/" << totalFiles
              << " current=\"" << (progress.currentFileRelativePath.empty() ? "<none>" : progress.currentFileRelativePath) << "\""
              << " phase=" << progress.currentFilePhase
              << " workers=" << progress.activeWorkerCount << "/" << progress.configuredWorkerCount
              << " section=" << progress.currentSectionIndex << "/" << progress.currentSectionCount
              << " completed=" << std::fixed << std::setprecision(2) << percent << "%"
              << " file_bytes=" << progress.currentFileBytesProcessed << "/" << progress.currentFileTotalBytes
              << " file_bytes_pct=" << filePercent << "%"
              << " lines=" << progress.currentFileLinesProcessed
              << " file_tokens=" << progress.currentFileTokensProduced
              << " output_bytes=" << progress.currentFileOutputBytesWritten
              << " tokens_written=" << progress.totalTokensWritten;

    const Clock::time_point now = Clock::now();
    for (const RunState& workerState : workerStates)
    {
        if (!workerState.active)
        {
            continue;
        }

        std::cout << " [w" << workerState.workerSlot
                  << " " << workerState.currentFilePhase
                  << " " << workerState.currentSectionIndex << "/" << workerState.currentSectionCount
                  << " " << FormatDuration(now - workerState.currentFileStart) << "]";
    }

    std::cout << " elapsed=" << FormatDuration(now - runStart)
              << '\n';
}

void MaybePrintPeriodicProgress(
    const TokenConversionConfig& config,
    const ProgressState& progress,
    const std::vector<RunState>& workerStates,
    const Clock::time_point runStart,
    Clock::time_point& lastProgressPrint,
    const std::size_t totalFiles,
    const bool force)
{
    const Clock::time_point now = Clock::now();
    if (!force)
    {
        const std::uint64_t elapsedSeconds =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now - lastProgressPrint).count());
        if (elapsedSeconds < config.progressIntervalSeconds)
        {
            return;
        }
    }

    PrintProgressLine(progress, workerStates, runStart, totalFiles);
    lastProgressPrint = now;
}

bool IsPeriodicProgressDue(
    const TokenConversionConfig& config,
    const Clock::time_point lastProgressPrint,
    const Clock::time_point now = Clock::now())
{
    const std::uint64_t elapsedSeconds =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now - lastProgressPrint).count());
    return elapsedSeconds >= config.progressIntervalSeconds;
}

void RemoveIfExists(const std::filesystem::path& path)
{
    std::error_code ignored;
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path, ignored);
    }
}

void PromoteTempOutput(const std::filesystem::path& tempPath, const std::filesystem::path& finalPath)
{
    RemoveIfExists(finalPath);

    for (int attempt = 0; attempt < 20; ++attempt)
    {
        std::error_code renameError;
        std::filesystem::rename(tempPath, finalPath, renameError);
        if (!renameError)
        {
            return;
        }

        if (attempt == 19)
        {
            throw std::runtime_error("Failed to promote temp output file: " + renameError.message()
                + ": \"" + tempPath.string() + "\", \"" + finalPath.string() + "\"");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

void CleanupPartialOutputsForFile(const TokenConversionConfig& config, const std::string& relativeInputPath)
{
    RemoveIfExists(TempOutputPathForRelativeInput(config, relativeInputPath));
    RemoveIfExists(OutputPathForRelativeInput(config, relativeInputPath));
}

std::size_t CountFileLines(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to open file for line count: " + path.string());
    }

    std::size_t lineCount = 0;
    std::string line;
    while (std::getline(stream, line))
    {
        ++lineCount;
    }

    if (!stream.eof() && stream.fail())
    {
        throw std::runtime_error("Failed while counting lines in file: " + path.string());
    }

    return lineCount;
}

SectionReadResult ReadFileSections(
    const TokenConversionConfig& config,
    const DiscoveredFile& discoveredFile,
    RunState& runState,
    const std::function<void(const RunState&, bool)>& publishProgress)
{
    std::ifstream inputStream(discoveredFile.absolutePath, std::ios::binary);
    if (!inputStream.is_open())
    {
        throw std::runtime_error("Unable to open file: " + discoveredFile.absolutePath.string());
    }

    runState.currentFilePhase = "reading_sections";
    runState.currentFileBytesProcessed = 0;
    runState.currentFileTotalBytes = discoveredFile.inputBytes;
    runState.currentFileLinesProcessed = 0;
    runState.currentFileTokensProduced = 0;
    runState.currentFileOutputBytesWritten = 0;
    runState.currentSectionIndex = 0;
    runState.currentSectionCount = config.targetSectionCount;
    publishProgress(runState, true);

    std::string normalizedText;
    normalizedText.reserve(static_cast<std::size_t>(discoveredFile.inputBytes));
    std::vector<std::size_t> lineStartOffsets;
    std::vector<std::uint64_t> lineBytePositions;
    std::string rawLine;
    const std::uint64_t checkpointBytes = ProgressCheckpointBytes(discoveredFile.inputBytes);
    std::uint64_t nextDurableCheckpoint = checkpointBytes;
    Clock::time_point lastHeartbeat = Clock::now();

    while (std::getline(inputStream, rawLine))
    {
        ++runState.currentFileLinesProcessed;
        const std::string normalizedLine = NormalizeForTokenizer(rawLine);
        if (!lineStartOffsets.empty())
        {
            normalizedText.push_back('\n');
        }
        lineStartOffsets.push_back(normalizedText.size());
        normalizedText.append(normalizedLine);

        const std::uint64_t currentBytesProcessed =
            static_cast<std::uint64_t>(inputStream.tellg() >= 0 ? inputStream.tellg() : static_cast<std::streampos>(discoveredFile.inputBytes));
        runState.currentFileBytesProcessed = currentBytesProcessed;
        lineBytePositions.push_back(currentBytesProcessed);
        runState.currentSectionIndex = std::min<std::size_t>(runState.currentFileLinesProcessed, config.targetSectionCount);

        const Clock::time_point now = Clock::now();
        const bool durableCheckpointReached = currentBytesProcessed >= nextDurableCheckpoint;
        const bool heartbeatReached = IsPeriodicProgressDue(config, lastHeartbeat, now);
        if (durableCheckpointReached || heartbeatReached)
        {
            publishProgress(runState, durableCheckpointReached);
            if (durableCheckpointReached)
            {
                while (nextDurableCheckpoint <= currentBytesProcessed)
                {
                    nextDurableCheckpoint += checkpointBytes;
                }
            }
            if (heartbeatReached)
            {
                lastHeartbeat = now;
            }
        }
    }

    if (!inputStream.eof() && inputStream.fail())
    {
        throw std::runtime_error("Failed while reading input file: " + discoveredFile.relativePath);
    }

    SectionReadResult result;
    result.normalizedText = std::move(normalizedText);
    result.lineStartOffsets = std::move(lineStartOffsets);
    result.totalLines = result.lineStartOffsets.size();
    result.sectionCount =
        std::max<std::size_t>(1, std::min<std::size_t>(config.targetSectionCount, result.totalLines == 0 ? 1 : result.totalLines));
    result.sections.resize(result.sectionCount);
    for (std::size_t index = 0; index < result.sectionCount; ++index)
    {
        result.sections[index].index = index;
    }

    const std::size_t baseLinesPerSection = result.totalLines == 0 ? 0 : (result.totalLines / result.sectionCount);
    const std::size_t extraLines = result.totalLines == 0 ? 0 : (result.totalLines % result.sectionCount);

    std::size_t lineIndex = 0;
    for (std::size_t sectionIndex = 0; sectionIndex < result.sectionCount; ++sectionIndex)
    {
        const std::size_t linesForSection =
            result.totalLines == 0 ? 0 : (baseLinesPerSection + (sectionIndex < extraLines ? 1 : 0));
        SectionBuffer& section = result.sections[sectionIndex];
        section.startLineIndex = lineIndex;
        section.endLineIndex = lineIndex + linesForSection;
        if (linesForSection > 0)
        {
            section.startTextOffset = result.lineStartOffsets[section.startLineIndex];
            section.endTextOffset =
                section.endLineIndex < result.totalLines
                    ? result.lineStartOffsets[section.endLineIndex]
                    : result.normalizedText.size();
        }
        lineIndex += linesForSection;

        if (linesForSection > 0)
        {
            section.linesProcessed = lineIndex;
            section.bytesProcessed = lineBytePositions[lineIndex - 1];
        }
    }

    runState.currentSectionCount = result.sectionCount;
    runState.currentSectionIndex = 0;
    publishProgress(runState, true);
    return result;
}

void ValidateTokenizerForUint16(const SharedTokenizer& tokenizer);

std::optional<int> FindUnknownTokenId(const SharedTokenizer& tokenizer)
{
    const std::map<std::string, int>& specialTokenIds = tokenizer.GetSpecialTokenIds();
    const std::map<std::string, int>::const_iterator it = specialTokenIds.find("<unk>");
    if (it == specialTokenIds.end())
    {
        return std::nullopt;
    }

    return it->second;
}

FileOutput ConvertSingleFileSequential(
    const TokenConversionConfig& config,
    const SharedTokenizer& tokenizer,
    const std::optional<int> unknownTokenId,
    const DiscoveredFile& discoveredFile,
    RunState& runState,
    const std::function<void(const RunState&, bool)>& publishProgress)
{
    const std::filesystem::path finalPath = OutputPathForRelativeInput(config, discoveredFile.relativePath);
    const std::filesystem::path tempPath = TempOutputPathForRelativeInput(config, discoveredFile.relativePath);
    std::filesystem::create_directories(finalPath.parent_path());
    FileOutput output;
    const std::size_t totalLines = CountFileLines(discoveredFile.absolutePath);
    const std::size_t sectionCount = std::max<std::size_t>(1, std::min<std::size_t>(config.targetSectionCount, totalLines == 0 ? 1 : totalLines));
    const std::size_t baseLinesPerSection = totalLines == 0 ? 0 : (totalLines / sectionCount);
    const std::size_t extraLines = totalLines == 0 ? 0 : (totalLines % sectionCount);

    runState.currentFilePhase = "counting_lines";
    runState.currentFileBytesProcessed = 0;
    runState.currentFileTotalBytes = discoveredFile.inputBytes;
    runState.currentFileLinesProcessed = 0;
    runState.currentFileTokensProduced = 0;
    runState.currentFileOutputBytesWritten = 0;
    runState.currentSectionIndex = 0;
    runState.currentSectionCount = sectionCount;
    publishProgress(runState, true);

    {
        std::ifstream inputStream(discoveredFile.absolutePath, std::ios::binary);
        if (!inputStream.is_open())
        {
            throw std::runtime_error("Unable to open file: " + discoveredFile.absolutePath.string());
        }

        std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            throw std::runtime_error("Unable to open temp output file: " + tempPath.string());
        }

        std::size_t currentSection = 0;
        std::size_t linesInCurrentSection = 0;
        std::size_t sectionLineTarget = totalLines == 0 ? 0 : (baseLinesPerSection + (currentSection < extraLines ? 1 : 0));
        std::string rawLine;
        std::string sectionText;

        while (std::getline(inputStream, rawLine))
        {
            const std::string normalizedLine = NormalizeForTokenizer(rawLine);
            sectionText.append(normalizedLine);

            ++runState.currentFileLinesProcessed;
            ++linesInCurrentSection;

            const bool moreLinesRemain = runState.currentFileLinesProcessed < totalLines;
            if (moreLinesRemain)
            {
                sectionText.push_back('\n');
            }

            runState.currentFileBytesProcessed = static_cast<std::uint64_t>(inputStream.tellg() >= 0 ? inputStream.tellg() : static_cast<std::streampos>(discoveredFile.inputBytes));

            const bool sectionComplete =
                (sectionLineTarget > 0 && linesInCurrentSection >= sectionLineTarget)
                || (!moreLinesRemain);
            if (!sectionComplete)
            {
                continue;
            }

            runState.currentSectionIndex = currentSection + 1;
            runState.currentFilePhase = "tokenizing";
            publishProgress(runState, true);

            std::future<std::vector<int>> encodeFuture =
                std::async(std::launch::async, [&tokenizer, sectionText]() { return tokenizer.EncodeNormalized(sectionText); });

            while (encodeFuture.wait_for(std::chrono::seconds(1)) != std::future_status::ready)
            {
                if (g_stopRequested.load())
                {
                    AppendLog(LogPath(config), "warning", "SIGINT received while tokenizing current file section; the file will restart from the beginning on resume.");
                }

                publishProgress(runState, false);
            }

            const std::vector<int> tokenIds = encodeFuture.get();
            runState.currentFilePhase = "writing";

            for (int tokenId : tokenIds)
            {
                if (tokenId < 0 || tokenId > static_cast<int>(std::numeric_limits<std::uint16_t>::max()))
                {
                    throw std::runtime_error("Token id does not fit in uint16 for file: " + discoveredFile.relativePath);
                }

                const std::uint16_t value = static_cast<std::uint16_t>(tokenId);
                stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
            }

            if (!stream.good())
            {
                throw std::runtime_error("Failed while writing temp output file: " + tempPath.string());
            }

            std::uint64_t sectionUnknowns = 0;
            if (unknownTokenId.has_value())
            {
                for (int tokenId : tokenIds)
                {
                    if (tokenId == *unknownTokenId)
                    {
                        ++sectionUnknowns;
                    }
                }
            }

            output.tokenCount += static_cast<std::uint64_t>(tokenIds.size());
            output.unknownTokenCount += sectionUnknowns;
            runState.currentFileTokensProduced = output.tokenCount;
            runState.currentFileOutputBytesWritten += static_cast<std::uint64_t>(tokenIds.size() * sizeof(std::uint16_t));
            publishProgress(runState, true);

            sectionText.clear();
            linesInCurrentSection = 0;
            ++currentSection;
            sectionLineTarget =
                currentSection < sectionCount
                    ? (baseLinesPerSection + (currentSection < extraLines ? 1 : 0))
                    : 0;
        }

        if (!inputStream.eof() && inputStream.fail())
        {
            throw std::runtime_error("Failed while reading input file: " + discoveredFile.relativePath);
        }

        if (!stream.good())
        {
            throw std::runtime_error("Failed while writing temp output file: " + tempPath.string());
        }
    }

    output.outputBytes = output.tokenCount * sizeof(std::uint16_t);
    runState.currentFileOutputBytesWritten = output.outputBytes;
    PromoteTempOutput(tempPath, finalPath);
    return output;
}

FileOutput ConvertSingleFileParallelSections(
    const TokenConversionConfig& config,
    const std::filesystem::path& tokenizerModelPath,
    const std::optional<int> unknownTokenId,
    const DiscoveredFile& discoveredFile,
    RunState& runState,
    const std::size_t workerCount,
    const std::function<void(const RunState&, bool)>& publishProgress)
{
    const std::filesystem::path finalPath = OutputPathForRelativeInput(config, discoveredFile.relativePath);
    const std::filesystem::path tempPath = TempOutputPathForRelativeInput(config, discoveredFile.relativePath);
    std::filesystem::create_directories(finalPath.parent_path());

    const SectionReadResult readResult =
        ReadFileSections(config, discoveredFile, runState, publishProgress);
    const std::size_t sectionCount = readResult.sectionCount;
    const std::string& normalizedText = readResult.normalizedText;
    const std::vector<SectionBuffer>& sections = readResult.sections;

    std::vector<EncodedSection> encodedSections(sectionCount);
    std::mutex resultsMutex;
    std::mutex exceptionMutex;
    std::atomic<std::size_t> nextSectionIndex = 0;
    std::atomic<bool> workerFailed = false;
    std::exception_ptr workerException;

    const auto sectionWorker =
        [&](const std::size_t workerSlot)
        {
            try
            {
                const SharedTokenizer workerTokenizer = SharedTokenizer::LoadFromFile(tokenizerModelPath);
                ValidateTokenizerForUint16(workerTokenizer);

                RunState workerRunState = runState;
                workerRunState.workerSlot = workerSlot + 1;
                workerRunState.active = true;

                while (true)
                {
                    if (g_stopRequested.load() || workerFailed.load())
                    {
                        break;
                    }

                    const std::size_t sectionIndex = nextSectionIndex.fetch_add(1);
                    if (sectionIndex >= sections.size())
                    {
                        break;
                    }

                    const SectionBuffer& section = sections[sectionIndex];
                    workerRunState.currentFilePhase = "tokenizing";
                    workerRunState.currentSectionIndex = sectionIndex + 1;
                    workerRunState.currentSectionCount = sectionCount;
                    workerRunState.currentFileBytesProcessed = section.bytesProcessed;
                    workerRunState.currentFileLinesProcessed = section.linesProcessed;
                    workerRunState.currentFileTotalBytes = discoveredFile.inputBytes;
                    publishProgress(workerRunState, true);

                    std::string sectionText;
                    if (section.endTextOffset > section.startTextOffset)
                    {
                        sectionText.assign(
                            normalizedText.data() + section.startTextOffset,
                            section.endTextOffset - section.startTextOffset);
                    }

                    const std::vector<int> tokenIds = workerTokenizer.EncodeNormalized(sectionText);

                    EncodedSection encodedSection;
                    encodedSection.index = sectionIndex;
                    encodedSection.bytesProcessed = section.bytesProcessed;
                    encodedSection.linesProcessed = section.linesProcessed;
                    encodedSection.tokenIds.reserve(tokenIds.size());

                    for (int tokenId : tokenIds)
                    {
                        if (tokenId < 0 || tokenId > static_cast<int>(std::numeric_limits<std::uint16_t>::max()))
                        {
                            throw std::runtime_error("Token id does not fit in uint16 for file: " + discoveredFile.relativePath);
                        }

                        encodedSection.tokenIds.push_back(static_cast<std::uint16_t>(tokenId));
                        if (unknownTokenId.has_value() && tokenId == *unknownTokenId)
                        {
                            ++encodedSection.unknownTokenCount;
                        }
                    }

                    workerRunState.currentFilePhase = "encoded_section_ready";
                    publishProgress(workerRunState, false);

                    {
                        const std::lock_guard<std::mutex> lock(resultsMutex);
                        encodedSections[sectionIndex] = std::move(encodedSection);
                    }
                }

                workerRunState.active = false;
                workerRunState.currentFilePhase = "idle";
                publishProgress(workerRunState, true);
            }
            catch (...)
            {
                const std::lock_guard<std::mutex> lock(exceptionMutex);
                if (!workerException)
                {
                    workerException = std::current_exception();
                    workerFailed.store(true);
                }
            }
        };

    std::vector<std::thread> sectionWorkers;
    const std::size_t sectionWorkerCount = std::max<std::size_t>(1, std::min<std::size_t>(workerCount, sections.size()));
    sectionWorkers.reserve(sectionWorkerCount);
    for (std::size_t workerSlot = 0; workerSlot < sectionWorkerCount; ++workerSlot)
    {
        sectionWorkers.emplace_back(sectionWorker, workerSlot);
    }

    for (std::thread& worker : sectionWorkers)
    {
        worker.join();
    }
    if (workerException)
    {
        std::rethrow_exception(workerException);
    }

    FileOutput output;
    runState.currentFilePhase = "writing";
    runState.currentSectionCount = sectionCount;
    {
        std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            throw std::runtime_error("Unable to open temp output file: " + tempPath.string());
        }

        for (std::size_t sectionIndex = 0; sectionIndex < encodedSections.size(); ++sectionIndex)
        {
            const EncodedSection& section = encodedSections[sectionIndex];
            if (section.index != sectionIndex)
            {
                throw std::runtime_error("Parallel section tokenization did not produce a complete ordered section set for file: "
                    + discoveredFile.relativePath);
            }

            runState.currentSectionIndex = sectionIndex + 1;
            runState.currentFileBytesProcessed = section.bytesProcessed;
            runState.currentFileLinesProcessed = section.linesProcessed;

            for (const std::uint16_t tokenId : section.tokenIds)
            {
                stream.write(reinterpret_cast<const char*>(&tokenId), sizeof(tokenId));
            }

            if (!stream.good())
            {
                throw std::runtime_error("Failed while writing temp output file: " + tempPath.string());
            }

            output.tokenCount += static_cast<std::uint64_t>(section.tokenIds.size());
            output.unknownTokenCount += section.unknownTokenCount;
            runState.currentFileTokensProduced = output.tokenCount;
            runState.currentFileOutputBytesWritten += static_cast<std::uint64_t>(section.tokenIds.size() * sizeof(std::uint16_t));
            publishProgress(runState, true);
        }

        stream.flush();
        stream.close();
    }

    output.outputBytes = output.tokenCount * sizeof(std::uint16_t);
    runState.currentFileOutputBytesWritten = output.outputBytes;
    runState.currentFileBytesProcessed = discoveredFile.inputBytes;
    runState.currentFileTotalBytes = discoveredFile.inputBytes;
    PromoteTempOutput(tempPath, finalPath);
    return output;
}

void ValidateTokenizerForUint16(const SharedTokenizer& tokenizer)
{
    if (tokenizer.GetVocabSize() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1ULL)
    {
        throw std::runtime_error("Tokenizer vocabulary does not fit inside uint16 token ids.");
    }
}
}

TokenConversionResult TokenConversionPipeline::Run(const TokenConversionConfig& config) const
{
    const ScopedSignalHandler signalHandler;
    std::filesystem::create_directories(config.outputRoot);

    const TokenizerIdentity identity = LoadTokenizerIdentity(config.tokenizerRoot);
    const SharedTokenizer tokenizer = SharedTokenizer::LoadFromFile(identity.modelPath);
    ValidateTokenizerForUint16(tokenizer);

    const std::vector<DiscoveredFile> discovered = DiscoverFiles(config);
    const std::string discoveryChecksum = BuildDiscoveryChecksum(discovered);
    const std::size_t resolvedWorkerCount = ResolveWorkerCount(config);

    WriteOrderedFilesManifest(OrderedFilesPath(config), discovered);

    ProgressState progress;
    progress.tokenizerId = identity.tokenizerId;
    progress.tokenizerVersion = identity.tokenizerVersion;
    progress.tokenizerModelHash = identity.tokenizerModelHash;
    progress.inputRoot = config.inputRoot.string();
    progress.outputRoot = config.outputRoot.string();
    progress.orderedFilesPath = OrderedFilesPath(config).string();
    progress.discoveryChecksum = discoveryChecksum;
    progress.startTimeUtc = UtcTimestampNow();
    progress.lastUpdateUtc = progress.startTimeUtc;
    progress.totalFilesDiscovered = discovered.size();
    progress.configuredWorkerCount = resolvedWorkerCount;

    bool resumed = false;
    const std::filesystem::path progressPath = ProgressPath(config);
    if (config.resume && std::filesystem::exists(progressPath))
    {
        progress = ParseProgressJson(ReadFile(progressPath));
        if (progress.tokenizerId != identity.tokenizerId
            || progress.tokenizerVersion != identity.tokenizerVersion
            || progress.tokenizerModelHash != identity.tokenizerModelHash)
        {
            throw std::runtime_error("Progress file tokenizer metadata does not match the currently configured shared tokenizer.");
        }
        if (progress.inputRoot != config.inputRoot.string() || progress.outputRoot != config.outputRoot.string())
        {
            throw std::runtime_error("Progress file roots do not match the current converter configuration.");
        }
        if (progress.discoveryChecksum != discoveryChecksum || progress.totalFilesDiscovered != discovered.size())
        {
            throw std::runtime_error("Discovered input ordering does not match the existing progress file.");
        }

        progress.status = "running";
        progress.lastUpdateUtc = UtcTimestampNow();
        progress.configuredWorkerCount = resolvedWorkerCount;
        ++progress.resumedRunCount;
        resumed = true;
    }
    else
    {
        progress.status = "running";
        progress.currentFileIndex = 0;
    }

    std::map<std::string, TokenConversionFileRecord> fileRecords =
        config.resume ? LoadFileRecords(FileRecordsPath(config)) : std::map<std::string, TokenConversionFileRecord>();
    std::vector<RunState> workerStates(resolvedWorkerCount);
    RecomputeProgressState(config, discovered, fileRecords, workerStates, progress);
    WriteFileRecords(FileRecordsPath(config), fileRecords);
    SaveProgress(progressPath, progress);
    SaveManifestAndReport(config, identity, progress, discovered, fileRecords);

    std::vector<std::size_t> pendingFileIndexes;
    pendingFileIndexes.reserve(discovered.size());
    for (std::size_t fileIndex = 0; fileIndex < discovered.size(); ++fileIndex)
    {
        const auto it = fileRecords.find(discovered[fileIndex].relativePath);
        if (it != fileRecords.end() && IsCompletedRecord(config, it->second))
        {
            continue;
        }

        pendingFileIndexes.push_back(fileIndex);
    }

    for (std::size_t fileIndex : pendingFileIndexes)
    {
        CleanupPartialOutputsForFile(config, discovered[fileIndex].relativePath);
    }

    const std::size_t totalPendingFiles = pendingFileIndexes.size();
    if (config.maxFilesPerRun > 0 && pendingFileIndexes.size() > config.maxFilesPerRun)
    {
        pendingFileIndexes.resize(config.maxFilesPerRun);
    }

    RecomputeProgressState(config, discovered, fileRecords, workerStates, progress);
    SaveProgress(progressPath, progress);
    SaveManifestAndReport(config, identity, progress, discovered, fileRecords);

    std::cout << "[startup] tokenizer=" << identity.tokenizerId
              << " version=" << identity.tokenizerVersion
              << " model_hash=" << identity.tokenizerModelHash << '\n';
    std::cout << "[startup] input_root=" << config.inputRoot.string()
              << " output_root=" << config.outputRoot.string()
              << " eligible_files=" << discovered.size()
              << " workers=" << resolvedWorkerCount
              << " parallel_mode=" << (config.parallelizeSections ? "sections" : "files")
              << " cpu_mode=" << (config.workerCount > 0 ? "manual" : (config.halfCpuUsage ? "half" : "full"))
              << '\n';
    if (resumed)
    {
        std::cout << "[resume] restarting from file index " << (progress.currentFileIndex + 1)
                  << " of " << discovered.size();
        if (progress.currentFileIndex < discovered.size())
        {
            std::cout << " (" << discovered[progress.currentFileIndex].relativePath << ")";
        }
        std::cout << '\n';
    }

    std::mutex stateMutex;
    const Clock::time_point runStart = Clock::now();
    Clock::time_point lastProgressPrint = runStart;
    std::atomic<std::size_t> nextWorkIndex = 0;
    std::atomic<bool> stopScheduling = false;

    auto refreshProgressLocked =
        [&]()
        {
            RecomputeProgressState(config, discovered, fileRecords, workerStates, progress);
        };

    auto persistProgressLocked =
        [&]()
        {
            refreshProgressLocked();
            SaveProgress(progressPath, progress);
        };

    auto persistAllLocked =
        [&](const bool forcePrint)
        {
            refreshProgressLocked();
            WriteFileRecords(FileRecordsPath(config), fileRecords);
            SaveProgress(progressPath, progress);
            SaveManifestAndReport(config, identity, progress, discovered, fileRecords);
            MaybePrintPeriodicProgress(
                config,
                progress,
                workerStates,
                runStart,
                lastProgressPrint,
                discovered.size(),
                forcePrint);
        };

    if (config.parallelizeSections)
    {
        const std::optional<int> unknownTokenId = FindUnknownTokenId(tokenizer);
        bool interrupted = false;

        for (std::size_t pendingIndex = 0; pendingIndex < pendingFileIndexes.size(); ++pendingIndex)
        {
            if (g_stopRequested.load())
            {
                interrupted = true;
                break;
            }

            const std::size_t fileIndex = pendingFileIndexes[pendingIndex];
            const DiscoveredFile& file = discovered[fileIndex];

            RunState runState;
            runState.active = true;
            runState.workerSlot = 1;
            runState.runStart = runStart;
            runState.currentFileStart = Clock::now();
            runState.currentFileIndex = fileIndex;
            runState.currentFileRelativePath = file.relativePath;
            runState.currentFilePhase = "starting";
            runState.currentFileBytesProcessed = 0;
            runState.currentFileTotalBytes = file.inputBytes;
            runState.currentFileLinesProcessed = 0;
            runState.currentFileTokensProduced = 0;
            runState.currentFileOutputBytesWritten = 0;
            runState.currentSectionIndex = 0;
            runState.currentSectionCount = std::max<std::size_t>(1, config.targetSectionCount);

            {
                const std::lock_guard<std::mutex> lock(stateMutex);
                for (RunState& workerState : workerStates)
                {
                    workerState = RunState{};
                    workerState.active = false;
                }
                workerStates[0] = runState;
                std::cout << "[file] " << (fileIndex + 1) << "/" << discovered.size()
                          << " " << file.relativePath << '\n';
                persistProgressLocked();
            }

            CleanupPartialOutputsForFile(config, file.relativePath);

            try
            {
                const FileOutput output = ConvertSingleFileParallelSections(
                    config,
                    identity.modelPath,
                    unknownTokenId,
                    file,
                    runState,
                    resolvedWorkerCount,
                    [&](const RunState& updatedState, const bool forcePrint)
                    {
                        const std::lock_guard<std::mutex> lock(stateMutex);
                        if (updatedState.currentFilePhase == "reading_sections" || updatedState.currentFilePhase == "writing")
                        {
                            for (RunState& workerState : workerStates)
                            {
                                workerState = RunState{};
                                workerState.active = false;
                            }
                            workerStates[0] = updatedState;
                        }
                        else
                        {
                            const std::size_t slotIndex =
                                updatedState.workerSlot == 0 ? 0 : std::min<std::size_t>(updatedState.workerSlot - 1, workerStates.size() - 1);
                            workerStates[slotIndex] = updatedState;
                        }

                        const bool shouldPrint = forcePrint || IsPeriodicProgressDue(config, lastProgressPrint);
                        if (forcePrint || shouldPrint)
                        {
                            refreshProgressLocked();
                        }
                        if (forcePrint)
                        {
                            SaveProgress(progressPath, progress);
                        }
                        if (shouldPrint)
                        {
                            MaybePrintPeriodicProgress(
                                config,
                                progress,
                                workerStates,
                                runStart,
                                lastProgressPrint,
                                discovered.size(),
                                true);
                        }
                    });

                TokenConversionFileRecord record;
                record.relativeInputPath = file.relativePath;
                record.relativeOutputPath =
                    OutputPathForRelativeInput(config, file.relativePath).lexically_relative(config.outputRoot).generic_string();
                record.status = "completed";
                record.inputBytes = file.inputBytes;
                record.outputBytes = output.outputBytes;
                record.tokenCount = output.tokenCount;
                record.unknownTokenCount = output.unknownTokenCount;
                if (output.unknownTokenCount > 0)
                {
                    std::ostringstream note;
                    note << "Unknown-token usage detected at " << std::fixed << std::setprecision(4)
                         << Percentage(output.unknownTokenCount, output.tokenCount) << "%.";
                    record.note = note.str();
                }

                {
                    const std::lock_guard<std::mutex> lock(stateMutex);
                    for (RunState& workerState : workerStates)
                    {
                        workerState = RunState{};
                        workerState.active = false;
                    }
                    fileRecords[record.relativeInputPath] = record;
                    if (!record.note.empty())
                    {
                        std::cerr << "[warning] " << file.relativePath
                                  << " produced " << output.unknownTokenCount
                                  << " unknown tokens out of " << output.tokenCount << '\n';
                        AppendLog(LogPath(config), "warning", file.relativePath + ": " + record.note);
                    }

                    std::cout << "[done] " << file.relativePath
                              << " tokens=" << output.tokenCount
                              << " unknowns=" << output.unknownTokenCount << '\n';
                    persistAllLocked(true);
                }
            }
            catch (const std::exception& exception)
            {
                CleanupPartialOutputsForFile(config, file.relativePath);

                TokenConversionFileRecord record;
                record.relativeInputPath = file.relativePath;
                record.relativeOutputPath =
                    OutputPathForRelativeInput(config, file.relativePath).lexically_relative(config.outputRoot).generic_string();
                record.status = "failed";
                record.inputBytes = file.inputBytes;
                record.note = exception.what();

                {
                    const std::lock_guard<std::mutex> lock(stateMutex);
                    for (RunState& workerState : workerStates)
                    {
                        workerState = RunState{};
                        workerState.active = false;
                    }
                    fileRecords[record.relativeInputPath] = record;
                    std::cerr << "[error] " << file.relativePath << ": " << exception.what() << '\n';
                    AppendLog(LogPath(config), "error", file.relativePath + ": " + exception.what());
                    persistAllLocked(true);
                }

                if (config.stopOnFailure)
                {
                    interrupted = true;
                    break;
                }
            }
        }

        if (g_stopRequested.load())
        {
            AppendLog(LogPath(config), "warning", "SIGINT received; stopping after the current file boundary.");
        }

        if (!interrupted && config.maxFilesPerRun > 0 && totalPendingFiles > pendingFileIndexes.size())
        {
            interrupted = true;
        }

        {
            const std::lock_guard<std::mutex> lock(stateMutex);
            RecomputeProgressState(config, discovered, fileRecords, workerStates, progress);
            progress.status = interrupted ? "interrupted" : "completed";
            progress.lastUpdateUtc = UtcTimestampNow();
            WriteFileRecords(FileRecordsPath(config), fileRecords);
            SaveProgress(progressPath, progress);
            SaveManifestAndReport(config, identity, progress, discovered, fileRecords);
        }

        if (interrupted)
        {
            std::cout << "[interrupted] completed_files=" << progress.completedFileCount
                      << " failed_files=" << progress.failedFileCount
                      << " last_completed=\""
                      << (progress.lastFullyCompletedFile.empty() ? std::string("<none>") : progress.lastFullyCompletedFile)
                      << "\"\n";
        }
        else
        {
            std::cout << "[complete] completed_files=" << progress.completedFileCount
                      << " failed_files=" << progress.failedFileCount
                      << " skipped_files=" << progress.skippedFileCount
                      << " tokens_written=" << progress.totalTokensWritten
                      << " unknown_tokens=" << progress.totalUnknownTokens << '\n';
        }

        TokenConversionResult result;
        result.resumed = resumed;
        result.completed = !interrupted;
        result.interrupted = interrupted;
        result.tokenizerId = identity.tokenizerId;
        result.tokenizerVersion = identity.tokenizerVersion;
        result.tokenizerModelHash = identity.tokenizerModelHash;
        result.totalFilesDiscovered = discovered.size();
        result.completedFileCount = progress.completedFileCount;
        result.skippedFileCount = progress.skippedFileCount;
        result.failedFileCount = progress.failedFileCount;
        result.totalTokensWritten = progress.totalTokensWritten;
        result.totalUnknownTokens = progress.totalUnknownTokens;
        result.lastFullyCompletedFile = progress.lastFullyCompletedFile;
        result.progressPath = progressPath;
        result.orderedFilesPath = OrderedFilesPath(config);
        result.fileRecordsPath = FileRecordsPath(config);
        result.manifestPath = ManifestPath(config);
        result.reportPath = ReportPath(config);
        result.logPath = LogPath(config);
        for (const auto& pair : fileRecords)
        {
            result.fileRecords.push_back(pair.second);
        }
        std::sort(
            result.fileRecords.begin(),
            result.fileRecords.end(),
            [](const TokenConversionFileRecord& left, const TokenConversionFileRecord& right)
            {
                return left.relativeInputPath < right.relativeInputPath;
            });
        return result;
    }

    const auto workerFunction =
        [&](const std::size_t workerSlot)
        {
            const SharedTokenizer workerTokenizer = SharedTokenizer::LoadFromFile(identity.modelPath);
            ValidateTokenizerForUint16(workerTokenizer);
            const std::optional<int> workerUnknownTokenId = FindUnknownTokenId(workerTokenizer);

            while (true)
            {
                if (g_stopRequested.load() || stopScheduling.load())
                {
                    break;
                }

                const std::size_t workCursor = nextWorkIndex.fetch_add(1);
                if (workCursor >= pendingFileIndexes.size())
                {
                    break;
                }

                const std::size_t fileIndex = pendingFileIndexes[workCursor];
                const DiscoveredFile& file = discovered[fileIndex];

                RunState runState;
                runState.active = true;
                runState.workerSlot = workerSlot + 1;
                runState.runStart = runStart;
                runState.currentFileStart = Clock::now();
                runState.currentFileIndex = fileIndex;
                runState.currentFileRelativePath = file.relativePath;
                runState.currentFilePhase = "starting";
                runState.currentFileBytesProcessed = 0;
                runState.currentFileTotalBytes = file.inputBytes;
                runState.currentFileLinesProcessed = 0;
                runState.currentFileTokensProduced = 0;
                runState.currentFileOutputBytesWritten = 0;
                runState.currentSectionIndex = 0;
                runState.currentSectionCount = std::max<std::size_t>(1, config.targetSectionCount);

                {
                    const std::lock_guard<std::mutex> lock(stateMutex);
                    workerStates[workerSlot] = runState;
                    std::cout << "[file] [w" << (workerSlot + 1) << "] " << (fileIndex + 1) << "/" << discovered.size()
                              << " " << file.relativePath << '\n';
                    persistProgressLocked();
                }

                CleanupPartialOutputsForFile(config, file.relativePath);

                try
                {
                    const FileOutput output = ConvertSingleFileSequential(
                        config,
                        workerTokenizer,
                        workerUnknownTokenId,
                        file,
                        runState,
                        [&](const RunState& updatedState, const bool forcePrint)
                        {
                            const std::lock_guard<std::mutex> lock(stateMutex);
                            workerStates[workerSlot] = updatedState;
                            const bool shouldPrint = forcePrint || IsPeriodicProgressDue(config, lastProgressPrint);
                            if (forcePrint || shouldPrint)
                            {
                                refreshProgressLocked();
                            }
                            if (forcePrint)
                            {
                                SaveProgress(progressPath, progress);
                            }
                            if (shouldPrint)
                            {
                                MaybePrintPeriodicProgress(
                                    config,
                                    progress,
                                    workerStates,
                                    runStart,
                                    lastProgressPrint,
                                    discovered.size(),
                                    true);
                            }
                        });

                    TokenConversionFileRecord record;
                    record.relativeInputPath = file.relativePath;
                    record.relativeOutputPath =
                        OutputPathForRelativeInput(config, file.relativePath).lexically_relative(config.outputRoot).generic_string();
                    record.status = "completed";
                    record.inputBytes = file.inputBytes;
                    record.outputBytes = output.outputBytes;
                    record.tokenCount = output.tokenCount;
                    record.unknownTokenCount = output.unknownTokenCount;
                    if (output.unknownTokenCount > 0)
                    {
                        std::ostringstream note;
                        note << "Unknown-token usage detected at " << std::fixed << std::setprecision(4)
                             << Percentage(output.unknownTokenCount, output.tokenCount) << "%.";
                        record.note = note.str();
                    }

                    {
                        const std::lock_guard<std::mutex> lock(stateMutex);
                        workerStates[workerSlot].active = false;
                        workerStates[workerSlot].currentFileRelativePath.clear();
                        workerStates[workerSlot].currentFilePhase = "idle";
                        fileRecords[record.relativeInputPath] = record;
                        if (!record.note.empty())
                        {
                            std::cerr << "[warning] " << file.relativePath
                                      << " produced " << output.unknownTokenCount
                                      << " unknown tokens out of " << output.tokenCount << '\n';
                            AppendLog(LogPath(config), "warning", file.relativePath + ": " + record.note);
                        }

                        std::cout << "[done] [w" << (workerSlot + 1) << "] " << file.relativePath
                                  << " tokens=" << output.tokenCount
                                  << " unknowns=" << output.unknownTokenCount << '\n';
                        persistAllLocked(true);
                    }
                }
                catch (const std::exception& exception)
                {
                    CleanupPartialOutputsForFile(config, file.relativePath);

                    TokenConversionFileRecord record;
                    record.relativeInputPath = file.relativePath;
                    record.relativeOutputPath =
                        OutputPathForRelativeInput(config, file.relativePath).lexically_relative(config.outputRoot).generic_string();
                    record.status = "failed";
                    record.inputBytes = file.inputBytes;
                    record.note = exception.what();

                    {
                        const std::lock_guard<std::mutex> lock(stateMutex);
                        workerStates[workerSlot].active = false;
                        workerStates[workerSlot].currentFileRelativePath.clear();
                        workerStates[workerSlot].currentFilePhase = "idle";
                        fileRecords[record.relativeInputPath] = record;
                        std::cerr << "[error] " << file.relativePath << ": " << exception.what() << '\n';
                        AppendLog(LogPath(config), "error", file.relativePath + ": " + exception.what());
                        if (config.stopOnFailure)
                        {
                            stopScheduling.store(true);
                        }
                        persistAllLocked(true);
                    }
                }
            }
        };

    std::vector<std::thread> workers;
    workers.reserve(resolvedWorkerCount);
    for (std::size_t workerSlot = 0; workerSlot < resolvedWorkerCount; ++workerSlot)
    {
        workers.emplace_back(workerFunction, workerSlot);
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    if (g_stopRequested.load())
    {
        AppendLog(LogPath(config), "warning", "SIGINT received; stopping after the current file boundary.");
    }

    bool interrupted = false;
    if (g_stopRequested.load() || stopScheduling.load())
    {
        interrupted = true;
    }
    else if (config.maxFilesPerRun > 0 && totalPendingFiles > pendingFileIndexes.size())
    {
        interrupted = true;
    }

    progress.status = interrupted ? "interrupted" : "completed";
    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        RecomputeProgressState(config, discovered, fileRecords, workerStates, progress);
        progress.status = interrupted ? "interrupted" : "completed";
        progress.lastUpdateUtc = UtcTimestampNow();
        WriteFileRecords(FileRecordsPath(config), fileRecords);
        SaveProgress(progressPath, progress);
        SaveManifestAndReport(config, identity, progress, discovered, fileRecords);
    }

    if (interrupted)
    {
        std::cout << "[interrupted] completed_files=" << progress.completedFileCount
                  << " failed_files=" << progress.failedFileCount
                  << " last_completed=\""
                  << (progress.lastFullyCompletedFile.empty() ? std::string("<none>") : progress.lastFullyCompletedFile)
                  << "\"\n";
    }
    else
    {
        std::cout << "[complete] completed_files=" << progress.completedFileCount
                  << " failed_files=" << progress.failedFileCount
                  << " skipped_files=" << progress.skippedFileCount
                  << " tokens_written=" << progress.totalTokensWritten
                  << " unknown_tokens=" << progress.totalUnknownTokens << '\n';
    }

    TokenConversionResult result;
    result.resumed = resumed;
    result.completed = !interrupted;
    result.interrupted = interrupted;
    result.tokenizerId = identity.tokenizerId;
    result.tokenizerVersion = identity.tokenizerVersion;
    result.tokenizerModelHash = identity.tokenizerModelHash;
    result.totalFilesDiscovered = discovered.size();
    result.completedFileCount = progress.completedFileCount;
    result.skippedFileCount = progress.skippedFileCount;
    result.failedFileCount = progress.failedFileCount;
    result.totalTokensWritten = progress.totalTokensWritten;
    result.totalUnknownTokens = progress.totalUnknownTokens;
    result.lastFullyCompletedFile = progress.lastFullyCompletedFile;
    result.progressPath = progressPath;
    result.orderedFilesPath = OrderedFilesPath(config);
    result.fileRecordsPath = FileRecordsPath(config);
    result.manifestPath = ManifestPath(config);
    result.reportPath = ReportPath(config);
    result.logPath = LogPath(config);
    for (const auto& pair : fileRecords)
    {
        result.fileRecords.push_back(pair.second);
    }
    std::sort(
        result.fileRecords.begin(),
        result.fileRecords.end(),
        [](const TokenConversionFileRecord& left, const TokenConversionFileRecord& right)
        {
            return left.relativeInputPath < right.relativeInputPath;
        });
    return result;
}
}

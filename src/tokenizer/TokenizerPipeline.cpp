#include "tokenizer/TokenizerPipeline.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>
#include <unordered_map>

#include "sentencepiece_trainer.h"

namespace Mina::Tokenizer
{
namespace
{
std::string QuoteJson(const std::string& value);

struct CorpusText
{
    TokenizerSourceKind kind = TokenizerSourceKind::HeldOutSample;
    std::string sourceLabel;
    std::string split = "Validation";
    std::string text;
};

struct TokenizerInputFile
{
    std::filesystem::path absolutePath;
    std::string relativePath;
    TokenizerSourceKind kind = TokenizerSourceKind::ParquetCorpusText;
    std::string note;
    std::size_t weight = 1;
    bool trackStats = true;
};

struct BuiltCorpus
{
    std::vector<CorpusText> heldOutTexts;
    std::vector<TokenizerSourceFile> sourceFiles;
    std::vector<TokenizerInputFile> trainingFiles;
    std::filesystem::path scratchRoot;
    std::size_t trainingTextCount = 0;
    std::size_t heldOutTextCount = 0;
    std::size_t repeatedTrainingExampleCount = 0;
    std::size_t parquetTrainingTextCount = 0;
    std::size_t projectTrainingRepeatedCount = 0;
    std::size_t supplementalTrainingRepeatedCount = 0;
    std::size_t guaranteedTrainingTextCount = 0;
    std::size_t totalTrainingCharacters = 0;
    std::size_t totalHeldOutCharacters = 0;
};

struct SentencePieceTrainingResult
{
    std::string serializedModel;
    std::size_t linesScanned = 0;
    std::size_t selectedLines = 0;
};

struct CorpusDiscoveryEntry
{
    std::filesystem::path absolutePath;
    std::string relativePath;
    std::uintmax_t fileSize = 0;
    long long lastWriteTicks = 0;
    bool supplemental = false;
};

struct CorpusIndexEntry
{
    TokenizerSourceFile source;
    std::uintmax_t fileSize = 0;
    long long lastWriteTicks = 0;
};

struct CorpusIndexData
{
    std::vector<CorpusIndexEntry> entries;
};

struct CorpusIndexLoadResult
{
    CorpusIndexData data;
    std::vector<CorpusDiscoveryEntry> discoveries;
    std::filesystem::path indexPath;
    bool reusedExisting = false;
};

std::string CurrentTimestamp()
{
    using namespace std::chrono;
    const system_clock::time_point now = system_clock::now();
    const std::time_t raw = system_clock::to_time_t(now);
    std::tm utc;
    gmtime_s(&utc, &raw);

    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
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

std::vector<std::string> SplitTabs(const std::string& line)
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

long long FileTimeToTicks(const std::filesystem::file_time_type& fileTime)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(fileTime.time_since_epoch()).count();
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to open tokenizer input file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

bool WriteFile(const std::filesystem::path& path, const std::string& value, std::string& error)
{
    std::filesystem::create_directories(path.parent_path());

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        error = "Unable to write file: " + path.string();
        return false;
    }

    stream << value;
    if (!stream.good())
    {
        error = "Failed while writing file: " + path.string();
        return false;
    }

    return true;
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

std::string JsonUnescape(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const char character = value[index];
        if (character != '\\')
        {
            decoded.push_back(character);
            continue;
        }

        if (index + 1 >= value.size())
        {
            decoded.push_back(character);
            continue;
        }

        ++index;
        const char escaped = value[index];

        switch (escaped)
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
                decoded.push_back(escaped);
                break;
        }
    }

    return decoded;
}

std::string ExtractJsonStringField(const std::string& line, const std::string& key)
{
    const std::string needle = "\"" + key + "\":\"";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos)
    {
        return std::string();
    }

    std::string value;
    bool escaping = false;

    for (std::size_t index = start + needle.size(); index < line.size(); ++index)
    {
        const char character = line[index];
        if (escaping)
        {
            value.push_back(character);
            escaping = false;
            continue;
        }

        if (character == '\\')
        {
            value.push_back(character);
            escaping = true;
            continue;
        }

        if (character == '"')
        {
            return JsonUnescape(value);
        }

        value.push_back(character);
    }

    return JsonUnescape(value);
}

std::string ToString(TokenizerSourceKind kind)
{
    switch (kind)
    {
        case TokenizerSourceKind::ParquetCorpusText:
            return "parquet_corpus_text";
        case TokenizerSourceKind::SupplementalCorpusText:
            return "supplemental_corpus_text";
        case TokenizerSourceKind::BaseTraining:
            return "base_training";
        case TokenizerSourceKind::Contextualization:
            return "contextualization";
        case TokenizerSourceKind::CustomVerbalization:
            return "custom_verbalization";
        case TokenizerSourceKind::SmallSupervised:
            return "small_supervised";
        case TokenizerSourceKind::HeldOutSample:
            return "held_out_sample";
        case TokenizerSourceKind::CorpusInventory:
            return "corpus_inventory";
        default:
            return "unknown";
    }
}

class TrainingProgressReporter
{
public:
    explicit TrainingProgressReporter(const std::filesystem::path& outputRoot)
        : progressPath(outputRoot / "manifests" / "tokenizer" / "tokenizer_training_progress.json"),
          logPath(outputRoot / "reports" / "tokenizer" / "tokenizer_training_log.md")
    {
        std::filesystem::create_directories(progressPath.parent_path());
        std::filesystem::create_directories(logPath.parent_path());
    }

    void Initialize(std::size_t totalFilesIn)
    {
        std::lock_guard<std::mutex> lock(mutex);
        totalFiles = totalFilesIn;
        status = "running";
        phase = "starting";
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        AppendLogLocked("starting", "Tokenizer training started.");
    }

    void SetTotalFiles(std::size_t totalFilesIn)
    {
        std::lock_guard<std::mutex> lock(mutex);
        totalFiles = totalFilesIn;
        lastUpdate = CurrentTimestamp();
        WriteLocked();
    }

    void Phase(const std::string& phaseIn, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        phase = phaseIn;
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        AppendLogLocked(phaseIn, message);
        std::cout << "[tokenizer_train] " << phaseIn << ": " << message << '\n';
    }

    void ScanUpdate(
        const std::string& currentFileIn,
        std::size_t filesProcessedIn,
        std::size_t linesScannedIn,
        std::size_t selectedLinesIn,
        const std::string& message = std::string())
    {
        std::lock_guard<std::mutex> lock(mutex);
        currentFile = currentFileIn;
        filesProcessed = filesProcessedIn;
        linesScanned = linesScannedIn;
        selectedLines = selectedLinesIn;
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        if (!message.empty())
        {
            AppendLogLocked(phase, message);
            std::cout << "[tokenizer_train] " << phase << ": " << message << '\n';
        }
    }

    void Heartbeat(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        AppendLogLocked(phase, message);
    }

    void Complete()
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = "completed";
        phase = "completed";
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        AppendLogLocked("completed", "Tokenizer training completed successfully.");
    }

    void Fail(const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status = "failed";
        phase = "failed";
        lastError = error;
        lastUpdate = CurrentTimestamp();
        WriteLocked();
        AppendLogLocked("failed", error);
    }

private:
    void WriteLocked() const
    {
        std::ofstream stream(progressPath, std::ios::binary | std::ios::trunc);
        stream << "{\n";
        stream << "  \"status\": " << QuoteJson(status) << ",\n";
        stream << "  \"phase\": " << QuoteJson(phase) << ",\n";
        stream << "  \"current_file\": " << QuoteJson(currentFile) << ",\n";
        stream << "  \"files_processed\": " << filesProcessed << ",\n";
        stream << "  \"total_files\": " << totalFiles << ",\n";
        stream << "  \"lines_scanned\": " << linesScanned << ",\n";
        stream << "  \"selected_lines\": " << selectedLines << ",\n";
        stream << "  \"last_update\": " << QuoteJson(lastUpdate) << ",\n";
        stream << "  \"last_error\": " << QuoteJson(lastError) << "\n";
        stream << "}\n";
    }

    void AppendLogLocked(const std::string& phaseIn, const std::string& message) const
    {
        const bool exists = std::filesystem::exists(logPath);
        std::ofstream stream(logPath, std::ios::binary | std::ios::app);
        if (!exists)
        {
            stream << "# Tokenizer Training Log\n\n";
        }
        stream << "- `" << CurrentTimestamp() << "` phase=`" << phaseIn << "` " << message << "\n";
    }

private:
    std::filesystem::path progressPath;
    std::filesystem::path logPath;
    mutable std::mutex mutex;
    std::string status = "pending";
    std::string phase = "pending";
    std::string currentFile;
    std::string lastUpdate;
    std::string lastError;
    std::size_t filesProcessed = 0;
    std::size_t totalFiles = 0;
    std::size_t linesScanned = 0;
    std::size_t selectedLines = 0;
};

void AddHeldOutText(
    BuiltCorpus& corpus,
    TokenizerSourceKind kind,
    const std::string& sourceLabel,
    const std::string& split,
    const std::string& text)
{
    const std::string normalizedText = NormalizeForTokenizer(text);
    if (normalizedText.empty())
    {
        return;
    }

    CorpusText entry;
    entry.kind = kind;
    entry.sourceLabel = sourceLabel;
    entry.split = split;
    entry.text = normalizedText;
    corpus.heldOutTexts.push_back(std::move(entry));
    ++corpus.heldOutTextCount;
    corpus.totalHeldOutCharacters += normalizedText.size();
}

std::vector<CorpusDiscoveryEntry> DiscoverCorpusTextFiles(const std::filesystem::path& corpusTextRoot)
{
    std::vector<CorpusDiscoveryEntry> files;
    if (!std::filesystem::exists(corpusTextRoot))
    {
        return files;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(corpusTextRoot))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".txt")
        {
            continue;
        }

        CorpusDiscoveryEntry file;
        file.absolutePath = entry.path();
        file.relativePath = std::filesystem::relative(entry.path(), corpusTextRoot).generic_string();
        file.fileSize = entry.file_size();
        file.lastWriteTicks = FileTimeToTicks(entry.last_write_time());
        file.supplemental = entry.path().filename().string().rfind("supplemental-", 0) == 0;
        files.push_back(std::move(file));
    }

    std::sort(
        files.begin(),
        files.end(),
        [](const CorpusDiscoveryEntry& left, const CorpusDiscoveryEntry& right)
        {
            return left.relativePath < right.relativePath;
        });
    return files;
}

std::filesystem::path ResolveCorpusIndexPath(const TokenizerTrainingConfig& config)
{
    if (!config.corpusIndexPath.empty())
    {
        return config.corpusIndexPath;
    }

    const std::filesystem::path root = config.progressRoot.empty()
        ? std::filesystem::path("C:\\Tokenizer")
        : config.progressRoot;
    return root / "manifests" / "tokenizer" / "corpus_index.tsv";
}

std::string SourceKindToIndexField(const TokenizerSourceKind kind)
{
    return ToString(kind);
}

TokenizerSourceKind ParseSourceKind(const std::string& value)
{
    if (value == "parquet_corpus_text")
    {
        return TokenizerSourceKind::ParquetCorpusText;
    }
    if (value == "supplemental_corpus_text")
    {
        return TokenizerSourceKind::SupplementalCorpusText;
    }

    throw std::runtime_error("Unsupported tokenizer corpus index source kind: " + value);
}

void WriteCorpusIndexFile(
    const std::filesystem::path& indexPath,
    const std::filesystem::path& corpusTextRoot,
    const CorpusIndexData& data)
{
    std::filesystem::create_directories(indexPath.parent_path());
    std::ofstream stream(indexPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to write corpus index: " + indexPath.string());
    }

    stream << "# MINA_TOKENIZER_CORPUS_INDEX_V1\n";
    stream << "# corpus_root\t" << corpusTextRoot.generic_string() << "\n";
    stream << "relative_path\tkind\trecord_count\ttext_count\tcharacter_count\tchecksum\tfile_size\tlast_write_ticks\tnote\n";

    for (const CorpusIndexEntry& entry : data.entries)
    {
        stream << entry.source.relativePath << '\t'
               << SourceKindToIndexField(entry.source.kind) << '\t'
               << entry.source.recordCount << '\t'
               << entry.source.textCount << '\t'
               << entry.source.characterCount << '\t'
               << entry.source.checksum << '\t'
               << entry.fileSize << '\t'
               << entry.lastWriteTicks << '\t'
               << entry.source.note << '\n';
    }

    if (!stream.good())
    {
        throw std::runtime_error("Failed while writing corpus index: " + indexPath.string());
    }
}

bool TryLoadCorpusIndexFile(
    const std::filesystem::path& indexPath,
    const std::filesystem::path& corpusTextRoot,
    const std::vector<CorpusDiscoveryEntry>& discoveries,
    CorpusIndexData& data,
    std::string& reason)
{
    if (!std::filesystem::exists(indexPath))
    {
        reason = "No existing corpus index was found.";
        return false;
    }

    std::ifstream stream(indexPath, std::ios::binary);
    if (!stream.is_open())
    {
        reason = "Unable to open corpus index.";
        return false;
    }

    std::string line;
    if (!std::getline(stream, line) || line != "# MINA_TOKENIZER_CORPUS_INDEX_V1")
    {
        reason = "Corpus index format header is missing or unsupported.";
        return false;
    }

    if (!std::getline(stream, line))
    {
        reason = "Corpus index metadata is incomplete.";
        return false;
    }

    const std::vector<std::string> rootFields = SplitTabs(line);
    if (rootFields.size() != 2 || rootFields[0] != "# corpus_root" || rootFields[1] != corpusTextRoot.generic_string())
    {
        reason = "Corpus root does not match the existing index.";
        return false;
    }

    if (!std::getline(stream, line)
        || line != "relative_path\tkind\trecord_count\ttext_count\tcharacter_count\tchecksum\tfile_size\tlast_write_ticks\tnote")
    {
        reason = "Corpus index column header is missing or unsupported.";
        return false;
    }

    std::vector<CorpusIndexEntry> entries;
    std::map<std::string, CorpusDiscoveryEntry> discoveryByRelativePath;
    for (const CorpusDiscoveryEntry& discovery : discoveries)
    {
        discoveryByRelativePath.emplace(discovery.relativePath, discovery);
    }

    while (std::getline(stream, line))
    {
        if (line.empty())
        {
            continue;
        }

        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() != 9)
        {
            reason = "Corpus index row has an unexpected column count.";
            return false;
        }

        const auto discoveryIt = discoveryByRelativePath.find(fields[0]);
        if (discoveryIt == discoveryByRelativePath.end())
        {
            reason = "Corpus file list changed since the index was written.";
            return false;
        }

        const std::uintmax_t indexedFileSize = static_cast<std::uintmax_t>(std::stoull(fields[6]));
        const long long indexedLastWriteTicks = std::stoll(fields[7]);
        if (indexedFileSize != discoveryIt->second.fileSize || indexedLastWriteTicks != discoveryIt->second.lastWriteTicks)
        {
            reason = "Corpus file size or timestamp changed since the index was written.";
            return false;
        }

        CorpusIndexEntry entry;
        entry.source.relativePath = fields[0];
        entry.source.kind = ParseSourceKind(fields[1]);
        entry.source.includedInTraining = true;
        entry.source.recordCount = static_cast<std::size_t>(std::stoull(fields[2]));
        entry.source.textCount = static_cast<std::size_t>(std::stoull(fields[3]));
        entry.source.characterCount = static_cast<std::size_t>(std::stoull(fields[4]));
        entry.source.checksum = fields[5];
        entry.fileSize = indexedFileSize;
        entry.lastWriteTicks = indexedLastWriteTicks;
        entry.source.note = fields[8];
        entries.push_back(std::move(entry));
    }

    if (entries.size() != discoveries.size())
    {
        reason = "Corpus file count changed since the index was written.";
        return false;
    }

    data.entries = std::move(entries);
    reason.clear();
    return true;
}

std::filesystem::path ChooseScratchRoot(const std::filesystem::path& corpusTextRoot)
{
    if (!corpusTextRoot.empty() && corpusTextRoot.is_absolute())
    {
        const std::filesystem::path base = corpusTextRoot.has_parent_path()
            ? corpusTextRoot.parent_path()
            : corpusTextRoot.root_path();
        return base / "_mina_sentencepiece_scratch";
    }

    return std::filesystem::temp_directory_path() / "_mina_sentencepiece_scratch";
}

class ScopedScratchDirectory
{
public:
    explicit ScopedScratchDirectory(const std::filesystem::path& rootIn)
        : root(rootIn)
    {
        std::filesystem::create_directories(root);
    }

    ~ScopedScratchDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    [[nodiscard]]
    const std::filesystem::path& GetRoot() const noexcept
    {
        return root;
    }

private:
    std::filesystem::path root;
};

class MultiFileSentenceIterator final : public sentencepiece::SentenceIterator
{
public:
    explicit MultiFileSentenceIterator(const std::vector<TokenizerInputFile>& filesIn)
        : files(filesIn)
    {
        Advance();
    }

    [[nodiscard]]
    bool done() const override
    {
        return finished;
    }

    void Next() override
    {
        if (!finished)
        {
            Advance();
        }
    }

    [[nodiscard]]
    const std::string& value() const override
    {
        return currentValue;
    }

    [[nodiscard]]
    sentencepiece::util::Status status() const override
    {
        return iteratorStatus;
    }

private:
    void Advance()
    {
        currentValue.clear();

        while (true)
        {
            if (!stream.is_open())
            {
                if (!OpenNextFile())
                {
                    finished = true;
                    return;
                }
            }

            std::string rawLine;
            if (!std::getline(stream, rawLine))
            {
                CloseCurrentFile();
                continue;
            }

            if (!rawLine.empty() && rawLine.back() == '\r')
            {
                rawLine.pop_back();
            }

            const std::string normalized = NormalizeForTokenizer(rawLine);
            if (normalized.empty())
            {
                continue;
            }

            currentValue = normalized;
            return;
        }
    }

    bool OpenNextFile()
    {
        while (nextFileIndex < files.size())
        {
            stream.close();
            stream.clear();
            stream.open(files[nextFileIndex].absolutePath, std::ios::binary);
            if (!stream.is_open())
            {
                iteratorStatus = sentencepiece::util::Status(
                    sentencepiece::util::StatusCode::kNotFound,
                    "Unable to open training text file: " + files[nextFileIndex].absolutePath.string());
                finished = true;
                return false;
            }

            ++nextFileIndex;
            return true;
        }

        return false;
    }

    void CloseCurrentFile()
    {
        stream.close();
        stream.clear();
    }

private:
    std::vector<TokenizerInputFile> files;
    std::ifstream stream;
    std::size_t nextFileIndex = 0;
    std::string currentValue;
    bool finished = false;
    sentencepiece::util::Status iteratorStatus;
};

void AppendWeightedLine(
    std::ofstream& stream,
    const std::string& normalizedText,
    std::size_t weight)
{
    for (std::size_t repetition = 0; repetition < weight; ++repetition)
    {
        stream << normalizedText << '\n';
    }
}

CorpusIndexData ScanCorpusIndexData(
    const std::filesystem::path& corpusTextRoot,
    const std::vector<CorpusDiscoveryEntry>& corpusTextFiles,
    const TokenizerTrainingConfig& config,
    TrainingProgressReporter* reporter)
{
    CorpusIndexData data;
    std::size_t scannedCorpusLines = 0;
    std::size_t scannedCorpusFiles = 0;
    const std::size_t scanUpdateInterval = std::max<std::size_t>(1, config.progressEveryLines);
    std::size_t nextScanProgressThreshold = scanUpdateInterval;

    for (const CorpusDiscoveryEntry& discoveredFile : corpusTextFiles)
    {
        const bool isSupplementalCorpusText = discoveredFile.supplemental;
        const std::string& relativePath = discoveredFile.relativePath;
        if (reporter != nullptr)
        {
            reporter->ScanUpdate(
                relativePath,
                scannedCorpusFiles,
                scannedCorpusLines,
                0,
                "Scanning file " + std::to_string(scannedCorpusFiles + 1) + " of " +
                    std::to_string(corpusTextFiles.size()) + ": " + relativePath);
        }
        std::ifstream stream(discoveredFile.absolutePath, std::ios::binary);
        if (!stream.is_open())
        {
            throw std::runtime_error("Unable to open tokenizer corpus text file: " + discoveredFile.absolutePath.string());
        }

        std::uint64_t checksum = 1469598103934665603ULL;
        auto UpdateChecksum = [&checksum](const std::string& value)
        {
            for (unsigned char byte : value)
            {
                checksum ^= static_cast<std::uint64_t>(byte);
                checksum *= 1099511628211ULL;
            }
        };

        std::size_t recordCount = 0;
        std::size_t includedCount = 0;
        std::size_t characterCount = 0;
        std::string rawLine;

        while (std::getline(stream, rawLine))
        {
            if (!rawLine.empty() && rawLine.back() == '\r')
            {
                rawLine.pop_back();
            }

            UpdateChecksum(rawLine);
            UpdateChecksum("\n");
            ++recordCount;
            ++scannedCorpusLines;

            const std::string normalized = NormalizeForTokenizer(rawLine);
            if (!normalized.empty())
            {
                ++includedCount;
                characterCount += normalized.size();
            }

            if (reporter != nullptr && scannedCorpusLines >= nextScanProgressThreshold)
            {
                reporter->ScanUpdate(
                    relativePath,
                    scannedCorpusFiles,
                    scannedCorpusLines,
                    0,
                    "Still scanning " + relativePath + ". Processed " +
                        std::to_string(scannedCorpusLines) + " corpus lines so far.");
                while (scannedCorpusLines >= nextScanProgressThreshold)
                {
                    nextScanProgressThreshold += scanUpdateInterval;
                }
            }
        }

        std::ostringstream checksumStream;
        checksumStream << "fnv1a64:" << std::hex << checksum;

        CorpusIndexEntry entry;
        entry.source.relativePath = relativePath;
        entry.source.kind = isSupplementalCorpusText
            ? TokenizerSourceKind::SupplementalCorpusText
            : TokenizerSourceKind::ParquetCorpusText;
        entry.source.includedInTraining = true;
        entry.source.recordCount = recordCount;
        entry.source.textCount = includedCount;
        entry.source.characterCount = characterCount;
        entry.source.checksum = checksumStream.str();
        entry.source.note = isSupplementalCorpusText
            ? "Supplemental tokenizer corpus text placed beside the exported Parquet shards."
            : "Tokenizer-ready corpus shard exported from the Parquet ingestion pipeline.";
        entry.fileSize = discoveredFile.fileSize;
        entry.lastWriteTicks = discoveredFile.lastWriteTicks;
        data.entries.push_back(std::move(entry));

        ++scannedCorpusFiles;
        if (reporter != nullptr)
        {
            reporter->ScanUpdate(
                relativePath,
                scannedCorpusFiles,
                scannedCorpusLines,
                0,
                "Finished " + relativePath + ". Non-empty normalized lines: " +
                    std::to_string(includedCount) + ".");
        }
    }

    WriteCorpusIndexFile(ResolveCorpusIndexPath(config), corpusTextRoot, data);
    return data;
}

void ApplyCorpusIndexData(
    BuiltCorpus& corpus,
    const std::vector<CorpusDiscoveryEntry>& corpusTextFiles,
    const CorpusIndexData& data,
    const TokenizerTrainingConfig& config)
{
    std::map<std::string, std::filesystem::path> absoluteByRelativePath;
    for (const CorpusDiscoveryEntry& discovery : corpusTextFiles)
    {
        absoluteByRelativePath.emplace(discovery.relativePath, discovery.absolutePath);
    }

    for (const CorpusIndexEntry& entry : data.entries)
    {
        corpus.sourceFiles.push_back(entry.source);

        TokenizerInputFile trainingFile;
        trainingFile.absolutePath = absoluteByRelativePath.at(entry.source.relativePath);
        trainingFile.relativePath = entry.source.relativePath;
        trainingFile.kind = entry.source.kind;
        trainingFile.note = entry.source.note;
        trainingFile.weight = config.parquetCorpusWeight;
        trainingFile.trackStats = false;
        corpus.trainingFiles.push_back(std::move(trainingFile));

        corpus.trainingTextCount += entry.source.textCount;
        corpus.repeatedTrainingExampleCount += entry.source.textCount * config.parquetCorpusWeight;
        if (entry.source.kind == TokenizerSourceKind::SupplementalCorpusText)
        {
            corpus.supplementalTrainingRepeatedCount += entry.source.textCount * config.parquetCorpusWeight;
            corpus.guaranteedTrainingTextCount += entry.source.textCount * config.parquetCorpusWeight;
        }
        else
        {
            corpus.parquetTrainingTextCount += entry.source.textCount * config.parquetCorpusWeight;
        }
        corpus.totalTrainingCharacters += entry.source.characterCount;
    }
}

CorpusIndexLoadResult LoadOrRefreshCorpusIndex(
    const std::filesystem::path& corpusTextRoot,
    const TokenizerTrainingConfig& config,
    TrainingProgressReporter* reporter)
{
    CorpusIndexLoadResult result;
    result.indexPath = ResolveCorpusIndexPath(config);
    result.discoveries = DiscoverCorpusTextFiles(corpusTextRoot);
    if (result.discoveries.empty())
    {
        throw std::runtime_error("No tokenizer corpus text files were found under: " + corpusTextRoot.string());
    }

    if (reporter != nullptr)
    {
        reporter->SetTotalFiles(result.discoveries.size());
    }

    std::string cacheReason;
    if (config.reuseCorpusIndex
        && TryLoadCorpusIndexFile(result.indexPath, corpusTextRoot, result.discoveries, result.data, cacheReason))
    {
        result.reusedExisting = true;
        if (reporter != nullptr)
        {
            reporter->ScanUpdate(
                std::string(),
                result.discoveries.size(),
                0,
                0,
                "Using cached corpus index from " + result.indexPath.string() + ".");
        }
        return result;
    }

    if (reporter != nullptr)
    {
        const std::string message = cacheReason.empty()
            ? "Discovered " + std::to_string(result.discoveries.size()) + " corpus text files under " +
                corpusTextRoot.string() + ". Starting line-by-line scan."
            : "Refreshing corpus index. " + cacheReason + " Starting line-by-line scan.";
        reporter->ScanUpdate(std::string(), 0, 0, 0, message);
    }

    result.data = ScanCorpusIndexData(corpusTextRoot, result.discoveries, config, reporter);
    return result;
}

BuiltCorpus BuildCorpus(
    const std::filesystem::path& sourceRepoRoot,
    const std::filesystem::path& datasetRoot,
    const TokenizerTrainingConfig& config,
    TrainingProgressReporter* reporter)
{
    BuiltCorpus corpus;
    const std::filesystem::path corpusTextRoot =
        config.corpusTextRoot.empty()
            ? std::filesystem::path("D:\\CorpusShards")
            : config.corpusTextRoot;
    const CorpusIndexLoadResult corpusIndex = LoadOrRefreshCorpusIndex(corpusTextRoot, config, reporter);
    corpus.scratchRoot = ChooseScratchRoot(corpusTextRoot);
    const std::filesystem::path projectTrainingPath = corpus.scratchRoot / "project_training_mix.txt";
    std::filesystem::create_directories(projectTrainingPath.parent_path());

    std::ofstream projectTrainingStream(projectTrainingPath, std::ios::binary | std::ios::trunc);
    if (!projectTrainingStream.is_open())
    {
        throw std::runtime_error("Unable to create project training mix file: " + projectTrainingPath.string());
    }

    ApplyCorpusIndexData(corpus, corpusIndex.discoveries, corpusIndex.data, config);

    struct ExportRule
    {
        std::string relativePath;
        TokenizerSourceKind kind;
        bool includeInput = false;
        bool includeOutput = true;
        std::size_t inputWeight = 1;
        std::size_t outputWeight = 2;
        std::string note;
    };

    const std::vector<ExportRule> exportRules = {
        {"exports/base_training/base_training.jsonl", TokenizerSourceKind::BaseTraining, false, true, 0,
            config.baseTrainingOutputWeight,
            "Base-training target texts reinforce neutral prose without the repeated instruction prompt."},
        {"exports/contextualization/contextualization.jsonl", TokenizerSourceKind::Contextualization, true, true,
            config.contextualizationInputWeight, config.contextualizationOutputWeight,
            "Contextualization samples contribute attribution and uncertainty-preserving phrasing."},
        {"exports/custom_verbalization/custom_verbalization.jsonl", TokenizerSourceKind::CustomVerbalization, true, true,
            config.customVerbalizationInputWeight, config.customVerbalizationOutputWeight,
            "Project verbalization examples contribute contract-grounded structured phrasing."},
        {"exports/small_supervised/small_supervised.jsonl", TokenizerSourceKind::SmallSupervised, false, true, 0,
            config.smallSupervisedOutputWeight,
            "Small supervised samples contribute abstention and non-agentic refusal language without training on unsupported code/math prompts."}
    };

    for (const ExportRule& rule : exportRules)
    {
        const std::filesystem::path exportPath = sourceRepoRoot / rule.relativePath;
        const std::string exportText = ReadFile(exportPath);
        const std::vector<std::string> lines = SplitLines(exportText);
        std::size_t recordCount = 0;
        std::size_t includedCount = 0;
        std::size_t characterCount = 0;

        auto AppendTrainingText = [&](TokenizerSourceKind kind,
                                      const std::string& exampleLabel,
                                      const std::string& split,
                                      const std::string& text,
                                      std::size_t weight)
        {
            const std::string normalized = NormalizeForTokenizer(text);
            if (normalized.empty())
            {
                return;
            }

            if (split == "Validation" || split == "Test")
            {
                AddHeldOutText(corpus, TokenizerSourceKind::HeldOutSample, exampleLabel, split, normalized);
                return;
            }

            AppendWeightedLine(projectTrainingStream, normalized, weight);
            ++includedCount;
            ++corpus.trainingTextCount;
            corpus.repeatedTrainingExampleCount += weight;
            corpus.projectTrainingRepeatedCount += weight;
            corpus.guaranteedTrainingTextCount += weight;
            characterCount += normalized.size();
            corpus.totalTrainingCharacters += normalized.size();
        };

        for (const std::string& line : lines)
        {
            const std::string split = ExtractJsonStringField(line, "split");
            const std::string inputText = ExtractJsonStringField(line, "normalized_input");
            const std::string outputText = ExtractJsonStringField(line, "output_text");
            const std::string exampleId = ExtractJsonStringField(line, "example_id");

            if (rule.includeInput)
            {
                AppendTrainingText(rule.kind, exampleId + ".input", split, inputText, rule.inputWeight);
            }

            if (rule.includeOutput)
            {
                AppendTrainingText(rule.kind, exampleId + ".output", split, outputText, rule.outputWeight);
            }

            ++recordCount;
        }

        TokenizerSourceFile source;
        source.relativePath = rule.relativePath;
        source.kind = rule.kind;
        source.includedInTraining = true;
        source.recordCount = recordCount;
        source.textCount = includedCount;
        source.characterCount = characterCount;
        source.checksum = MakeChecksum(exportText);
        source.note = rule.note;
        corpus.sourceFiles.push_back(source);
    }

    projectTrainingStream.flush();
    if (!projectTrainingStream.good())
    {
        throw std::runtime_error("Failed while writing project training mix file: " + projectTrainingPath.string());
    }
    projectTrainingStream.close();

    if (std::filesystem::file_size(projectTrainingPath) > 0)
    {
        TokenizerInputFile projectMixFile;
        projectMixFile.absolutePath = projectTrainingPath;
        projectMixFile.relativePath = "scratch/project_training_mix.txt";
        projectMixFile.kind = TokenizerSourceKind::BaseTraining;
        projectMixFile.note = "Temporary staged project-specific training text.";
        projectMixFile.trackStats = false;
        corpus.trainingFiles.push_back(std::move(projectMixFile));
    }

    std::vector<std::filesystem::path> parquetFiles;
    if (std::filesystem::exists(datasetRoot))
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(datasetRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".parquet")
            {
                continue;
            }

            parquetFiles.push_back(entry.path().filename());
        }
    }

    std::sort(parquetFiles.begin(), parquetFiles.end());
    std::ostringstream parquetInventoryStream;
    std::size_t parquetBytes = 0;

    for (const std::filesystem::path& parquetFile : parquetFiles)
    {
        const std::filesystem::path absolutePath = datasetRoot / parquetFile;
        const std::uintmax_t fileSize = std::filesystem::exists(absolutePath) ? std::filesystem::file_size(absolutePath) : 0;
        parquetInventoryStream << parquetFile.generic_string() << ":" << fileSize << "\n";
        parquetBytes += static_cast<std::size_t>(fileSize);
    }

    TokenizerSourceFile inventorySource;
    inventorySource.relativePath = datasetRoot.generic_string();
    inventorySource.kind = TokenizerSourceKind::CorpusInventory;
    inventorySource.includedInTraining = false;
    inventorySource.recordCount = parquetFiles.size();
    inventorySource.textCount = 0;
    inventorySource.characterCount = parquetBytes;
    inventorySource.checksum = MakeChecksum(parquetInventoryStream.str());
    inventorySource.note =
        "Raw filtered corpus inventory recorded for auditability. The tokenizer trainer consumes tokenizer-ready shard text from the configured corpus-text root rather than decoding Parquet directly.";
    corpus.sourceFiles.push_back(inventorySource);

    return corpus;
}

void CheckSentencePieceStatus(const sentencepiece::util::Status& status, const std::string& context)
{
    if (!status.ok())
    {
        throw std::runtime_error(context + ": " + status.ToString());
    }
}

SentencePieceTrainingResult TrainSentencePieceModel(
    const BuiltCorpus& corpus,
    const TokenizerTrainingConfig& config,
    TrainingProgressReporter* reporter)
{
    const ScopedScratchDirectory scratch(corpus.scratchRoot);
    const unsigned int hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const std::filesystem::path sampledInputPath = scratch.GetRoot() / "sentencepiece_training_input.txt";
    const std::size_t guaranteedTrainingRepeatedCount =
        corpus.projectTrainingRepeatedCount + corpus.supplementalTrainingRepeatedCount;
    const std::size_t parquetSampleBudget =
        config.sampledSentenceCount > guaranteedTrainingRepeatedCount
            ? std::min(
                corpus.parquetTrainingTextCount,
                config.sampledSentenceCount - guaranteedTrainingRepeatedCount)
            : 0;
    const double parquetSelectionRate =
        corpus.parquetTrainingTextCount == 0
            ? 0.0
            : static_cast<double>(parquetSampleBudget) / static_cast<double>(corpus.parquetTrainingTextCount);

    if (reporter != nullptr)
    {
        reporter->Phase(
            "sampling_training_input",
            "Creating a bounded sampled SentencePiece input file in scratch space.");
    }

    std::ofstream sampledStream(sampledInputPath, std::ios::binary | std::ios::trunc);
    if (!sampledStream.is_open())
    {
        throw std::runtime_error("Unable to create sampled training input: " + sampledInputPath.string());
    }

    auto HashToUnitInterval = [](const std::string& value) -> double
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (unsigned char byte : value)
        {
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= 1099511628211ULL;
        }

        return static_cast<double>(hash) / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    };

    std::size_t filesProcessed = 0;
    std::size_t linesScanned = 0;
    std::size_t selectedLines = 0;
    const std::size_t samplingUpdateInterval = std::max<std::size_t>(1, config.progressEveryLines);
    std::size_t nextSamplingProgressThreshold = samplingUpdateInterval;

    for (const TokenizerInputFile& file : corpus.trainingFiles)
    {
        std::ifstream stream(file.absolutePath, std::ios::binary);
        if (!stream.is_open())
        {
            throw std::runtime_error("Unable to open training text file: " + file.absolutePath.string());
        }

        const bool sampleParquet = file.kind == TokenizerSourceKind::ParquetCorpusText;
        std::string rawLine;
        std::size_t lineNumber = 0;

        while (std::getline(stream, rawLine))
        {
            if (!rawLine.empty() && rawLine.back() == '\r')
            {
                rawLine.pop_back();
            }

            const std::string normalized = NormalizeForTokenizer(rawLine);
            if (normalized.empty())
            {
                continue;
            }

            ++lineNumber;
            ++linesScanned;

            bool include = true;
            if (sampleParquet && corpus.parquetTrainingTextCount > parquetSampleBudget)
            {
                include = HashToUnitInterval(file.relativePath + ":" + std::to_string(lineNumber) + ":" + normalized)
                    < parquetSelectionRate;
            }

            if (!include)
            {
                continue;
            }

            sampledStream << normalized << '\n';
            ++selectedLines;

            if (reporter != nullptr && linesScanned >= nextSamplingProgressThreshold)
            {
                reporter->ScanUpdate(
                    file.relativePath,
                    filesProcessed,
                    linesScanned,
                    selectedLines,
                    "Sampled " + std::to_string(selectedLines) + " lines after scanning " +
                        std::to_string(linesScanned) + " lines.");
                while (linesScanned >= nextSamplingProgressThreshold)
                {
                    nextSamplingProgressThreshold += samplingUpdateInterval;
                }
            }
        }

        ++filesProcessed;
        if (reporter != nullptr)
        {
            reporter->ScanUpdate(file.relativePath, filesProcessed, linesScanned, selectedLines);
        }
    }

    sampledStream.flush();
    if (!sampledStream.good())
    {
        throw std::runtime_error("Failed while writing sampled training input: " + sampledInputPath.string());
    }
    sampledStream.close();

    if (reporter != nullptr)
    {
        reporter->Phase(
            "sentencepiece_training",
            "Sampled input is ready. Starting SentencePiece training on the bounded corpus.");
    }

    const std::unordered_map<std::string, std::string> trainingArgs = {
        {"input", sampledInputPath.string()},
        {"model_type", "bpe"},
        {"vocab_size", std::to_string(config.targetVocabSize)},
        {"character_coverage", "1.0"},
        {"normalization_rule_name", "identity"},
        {"remove_extra_whitespaces", "false"},
        {"byte_fallback", "true"},
        {"hard_vocab_limit", "false"},
        {"max_sentencepiece_length", std::to_string(config.maxPieceLength)},
        {"max_sentence_length", std::to_string(config.maxSentenceLength)},
        {"unk_id", "0"},
        {"bos_id", "1"},
        {"eos_id", "2"},
        {"pad_id", "3"},
        {"unk_piece", "<unk>"},
        {"bos_piece", "<bos>"},
        {"eos_piece", "<eos>"},
        {"pad_piece", "<pad>"},
        {"num_threads", std::to_string(hardwareThreads)},
        {"minloglevel", std::to_string(config.sentencePieceMinLogLevel)},
        {"shuffle_input_sentence", "false"}
    };

    std::atomic<bool> keepHeartbeat = true;
    std::thread heartbeatThread;
    if (reporter != nullptr && config.heartbeatEnabled)
    {
        heartbeatThread = std::thread([&keepHeartbeat, reporter]()
        {
            while (keepHeartbeat.load())
            {
                std::this_thread::sleep_for(std::chrono::minutes(1));
                if (!keepHeartbeat.load())
                {
                    break;
                }
                reporter->Heartbeat("SentencePiece training is still running.");
            }
        });
    }

    SentencePieceTrainingResult result;
    try
    {
        CheckSentencePieceStatus(
            sentencepiece::SentencePieceTrainer::Train(trainingArgs, nullptr, &result.serializedModel),
            "SentencePiece training failed");
    }
    catch (...)
    {
        keepHeartbeat.store(false);
        if (heartbeatThread.joinable())
        {
            heartbeatThread.join();
        }
        throw;
    }

    keepHeartbeat.store(false);
    if (heartbeatThread.joinable())
    {
        heartbeatThread.join();
    }
    result.linesScanned = linesScanned;
    result.selectedLines = selectedLines;
    return result;
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

std::string BuildConfigJson(const TokenizerTrainingConfig& config)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"tokenizer_id\": \"shared-shell-tokenizer\",\n";
    stream << "  \"tokenizer_version\": \"tokenizer-v0001\",\n";
    stream << "  \"tokenizer_type\": \"sentencepiece_bpe_v1\",\n";
    stream << "  \"trainer_backend\": \"sentencepiece\",\n";
    stream << "  \"normalizer\": \"english_project_v1\",\n";
    stream << "  \"corpus_text_root\": " << QuoteJson(config.corpusTextRoot.generic_string()) << ",\n";
    stream << "  \"corpus_index_path\": " << QuoteJson(ResolveCorpusIndexPath(config).generic_string()) << ",\n";
    stream << "  \"reuse_corpus_index\": " << (config.reuseCorpusIndex ? "true" : "false") << ",\n";
    stream << "  \"target_vocab_size\": " << config.targetVocabSize << ",\n";
    stream << "  \"max_piece_length\": " << config.maxPieceLength << ",\n";
    stream << "  \"max_sentence_length\": " << config.maxSentenceLength << ",\n";
    stream << "  \"sampled_sentence_count\": " << config.sampledSentenceCount << ",\n";
    stream << "  \"progress_every_lines\": " << config.progressEveryLines << ",\n";
    stream << "  \"heartbeat_enabled\": " << (config.heartbeatEnabled ? "true" : "false") << ",\n";
    stream << "  \"sentencepiece_minloglevel\": " << config.sentencePieceMinLogLevel << ",\n";
    stream << "  \"weights\": {\n";
    stream << "    \"parquet_corpus\": " << config.parquetCorpusWeight << ",\n";
    stream << "    \"base_training_output\": " << config.baseTrainingOutputWeight << ",\n";
    stream << "    \"contextualization_input\": " << config.contextualizationInputWeight << ",\n";
    stream << "    \"contextualization_output\": " << config.contextualizationOutputWeight << ",\n";
    stream << "    \"custom_verbalization_input\": " << config.customVerbalizationInputWeight << ",\n";
    stream << "    \"custom_verbalization_output\": " << config.customVerbalizationOutputWeight << ",\n";
    stream << "    \"small_supervised_output\": " << config.smallSupervisedOutputWeight << "\n";
    stream << "  },\n";
    stream << "  \"special_tokens\": [";
    for (std::size_t index = 0; index < config.specialTokens.size(); ++index)
    {
        if (index > 0)
        {
            stream << ", ";
        }
        stream << QuoteJson(config.specialTokens[index]);
    }
    stream << "],\n";
    stream << "  \"sentencepiece_options\": {\n";
    stream << "    \"model_type\": \"bpe\",\n";
    stream << "    \"character_coverage\": 1.0,\n";
    stream << "    \"normalization_rule_name\": \"identity\",\n";
    stream << "    \"remove_extra_whitespaces\": false,\n";
    stream << "    \"byte_fallback\": true,\n";
    stream << "    \"hard_vocab_limit\": false,\n";
    stream << "    \"max_sentence_length\": " << config.maxSentenceLength << "\n";
    stream << "  }\n";
    stream << "}\n";
    return stream.str();
}

std::string BuildManifestJson(const TokenizerManifest& manifest)
{
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"tokenizer_id\": " << QuoteJson(manifest.tokenizerId) << ",\n";
    stream << "  \"tokenizer_version\": " << QuoteJson(manifest.tokenizerVersion) << ",\n";
    stream << "  \"tokenizer_type\": " << QuoteJson(manifest.tokenizerType) << ",\n";
    stream << "  \"normalizer_name\": " << QuoteJson(manifest.normalizerName) << ",\n";
    stream << "  \"requested_vocab_size\": " << manifest.requestedVocabSize << ",\n";
    stream << "  \"learned_vocab_size\": " << manifest.learnedVocabSize << ",\n";
    stream << "  \"special_token_count\": " << manifest.specialTokenCount << ",\n";
    stream << "  \"training_text_count\": " << manifest.trainingTextCount << ",\n";
    stream << "  \"scanned_training_text_count\": " << manifest.scannedTrainingTextCount << ",\n";
    stream << "  \"guaranteed_training_text_count\": " << manifest.guaranteedTrainingTextCount << ",\n";
    stream << "  \"held_out_text_count\": " << manifest.heldOutTextCount << ",\n";
    stream << "  \"repeated_training_example_count\": " << manifest.repeatedTrainingExampleCount << ",\n";
    stream << "  \"total_training_characters\": " << manifest.totalTrainingCharacters << ",\n";
    stream << "  \"total_held_out_characters\": " << manifest.totalHeldOutCharacters << ",\n";
    stream << "  \"byte_fallback_token_count\": " << manifest.byteFallbackTokenCount << ",\n";
    stream << "  \"held_out_token_count\": " << manifest.heldOutTokenCount << ",\n";
    stream << "  \"held_out_character_count\": " << manifest.heldOutCharacterCount << ",\n";
    stream << "  \"average_characters_per_token\": " << std::fixed << std::setprecision(4) << manifest.averageCharactersPerToken << ",\n";
    stream << "  \"average_held_out_tokens_per_example\": " << manifest.averageHeldOutTokensPerExample << ",\n";
    stream << "  \"average_held_out_characters_per_token\": " << manifest.averageHeldOutCharactersPerToken << ",\n";
    stream << "  \"special_tokens\": {\n";

    bool first = true;
    for (const std::pair<const std::string, int>& entry : manifest.specialTokenIds)
    {
        if (!first)
        {
            stream << ",\n";
        }

        stream << "    " << QuoteJson(entry.first) << ": " << entry.second;
        first = false;
    }

    stream << "\n  },\n";
    stream << "  \"source_files\": [\n";
    for (std::size_t index = 0; index < manifest.sourceFiles.size(); ++index)
    {
        const TokenizerSourceFile& source = manifest.sourceFiles[index];
        stream << "    {\n";
        stream << "      \"relative_path\": " << QuoteJson(source.relativePath) << ",\n";
        stream << "      \"kind\": " << QuoteJson(ToString(source.kind)) << ",\n";
        stream << "      \"included_in_training\": " << (source.includedInTraining ? "true" : "false") << ",\n";
        stream << "      \"record_count\": " << source.recordCount << ",\n";
        stream << "      \"text_count\": " << source.textCount << ",\n";
        stream << "      \"character_count\": " << source.characterCount << ",\n";
        stream << "      \"checksum\": " << QuoteJson(source.checksum) << ",\n";
        stream << "      \"note\": " << QuoteJson(source.note) << "\n";
        stream << "    }";
        if (index + 1 < manifest.sourceFiles.size())
        {
            stream << ",";
        }
        stream << "\n";
    }
    stream << "  ]\n";
    stream << "}\n";
    return stream.str();
}

std::vector<TokenizerSample> BuildReportSamples(const SharedTokenizer& tokenizer)
{
    const std::vector<std::pair<std::string, std::string>> sampleInputs = {
        {"neutral prose", "An estuary is a coastal body of water where river water mixes with seawater."},
        {"conversation", "The record does not support that claim, so I cannot state it as fact."},
        {"source attribution", "Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study."},
        {"uncertainty and abstention", "The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain."},
        {"project verbalization", "The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim."}
    };

    std::vector<TokenizerSample> samples;

    for (const std::pair<std::string, std::string>& entry : sampleInputs)
    {
        TokenizerSample sample;
        sample.label = entry.first;
        sample.category = entry.first;
        sample.inputText = entry.second;
        sample.normalizedText = NormalizeForTokenizer(entry.second);
        sample.tokenIds = tokenizer.Encode(entry.second);
        sample.decodedText = tokenizer.Decode(sample.tokenIds);
        sample.tokenPieces = tokenizer.DescribeTokens(sample.tokenIds);
        samples.push_back(std::move(sample));
    }

    return samples;
}

std::string BuildReportMarkdown(const TokenizerManifest& manifest)
{
    std::ostringstream stream;
    stream << "# Shared Tokenizer Report\n\n";
    stream << "- tokenizer type: `" << manifest.tokenizerType << "`\n";
    stream << "- trainer backend: `sentencepiece`\n";
    stream << "- normalizer: `" << manifest.normalizerName << "`\n";
    stream << "- requested vocab size: `" << manifest.requestedVocabSize << "`\n";
    stream << "- learned vocab size: `" << manifest.learnedVocabSize << "`\n";
    stream << "- special tokens: ";

    bool first = true;
    for (const std::pair<const std::string, int>& entry : manifest.specialTokenIds)
    {
        if (!first)
        {
            stream << ", ";
        }

        stream << "`" << entry.first << "=" << entry.second << "`";
        first = false;
    }

    stream << "\n";
    stream << "- training texts used in the final bounded SentencePiece input: `" << manifest.trainingTextCount << "`\n";
    stream << "- scanned training texts available before sampling: `" << manifest.scannedTrainingTextCount << "`\n";
    stream << "- guaranteed training texts included without sampling: `" << manifest.guaranteedTrainingTextCount << "`\n";
    stream << "- held-out texts used for inspection: `" << manifest.heldOutTextCount << "`\n";
    stream << "- repeated training examples after weighting: `" << manifest.repeatedTrainingExampleCount << "`\n";
    stream << "- total training characters: `" << manifest.totalTrainingCharacters << "`\n";
    stream << "- normalization rules: `project normalize in C++ before SentencePiece identity normalization`\n";
    stream << "- bounded sampled training input cap: `5,000,000` lines by default\n";
    stream << "- average characters per token on held-out sample: `" << std::fixed << std::setprecision(3)
           << manifest.averageCharactersPerToken << "`\n";
    stream << "- average tokens per held-out example: `" << manifest.averageHeldOutTokensPerExample << "`\n";
    stream << "- byte fallback tokens on held-out sample: `" << manifest.byteFallbackTokenCount << "`\n";
    stream << "- unknown tokens on held-out sample: `0` (byte fallback enabled)\n\n";
    stream << "## Source Files\n\n";

    for (const TokenizerSourceFile& source : manifest.sourceFiles)
    {
        stream << "- `" << source.relativePath << "`"
               << " kind=`" << ToString(source.kind) << "`"
               << " included=`" << (source.includedInTraining ? "true" : "false") << "`"
               << " records=`" << source.recordCount << "`"
               << " texts=`" << source.textCount << "`"
               << " checksum=`" << source.checksum << "`"
               << "\n";
    }

    stream << "\n## Notes\n\n";
    stream << "- The shared tokenizer is intended for both shell tracks, so runtime consumers should load the same model artifact rather than training shell-specific vocabularies.\n";
    stream << "- The scalable C++ trainer streams tokenizer-ready shard text from the configured corpus-text root, stages only the small project-specific JSONL-derived text mix in scratch space, and materializes one bounded sampled SentencePiece input file.\n";
    stream << "- Corpus files whose names start with `supplemental-` are always fully included in training and are not thinned by the shard sampler.\n";
    stream << "- For large corpora, the trainer now enforces the sentence cap before SentencePiece starts, rather than relying on SentencePiece to sample from the iterator input.\n";
    stream << "- Raw Parquet extraction remains a separate corpus-preparation step handled by tokenizer_parquet_ingest_tool.\n";
    return stream.str();
}

std::string BuildSamplesMarkdown(const std::vector<TokenizerSample>& samples)
{
    std::ostringstream stream;
    stream << "# Shared Tokenizer Samples\n\n";

    for (const TokenizerSample& sample : samples)
    {
        stream << "## " << sample.label << "\n\n";
        stream << "- input: `" << sample.inputText << "`\n";
        stream << "- normalized: `" << sample.normalizedText << "`\n";
        stream << "- decoded: `" << sample.decodedText << "`\n";
        stream << "- token ids: `";

        for (std::size_t index = 0; index < sample.tokenIds.size(); ++index)
        {
            if (index > 0)
            {
                stream << ", ";
            }

            stream << sample.tokenIds[index];
        }

        stream << "`\n";
        stream << "- token pieces: `";

        for (std::size_t index = 0; index < sample.tokenPieces.size(); ++index)
        {
            if (index > 0)
            {
                stream << " | ";
            }

            stream << sample.tokenPieces[index];
        }

        stream << "`\n\n";
    }

    return stream.str();
}
}

TokenizerCorpusIndexStatus TokenizerTrainer::EnsureCorpusIndex(const TokenizerTrainingConfig& config) const
{
    std::unique_ptr<TrainingProgressReporter> reporter;
    if (!config.progressRoot.empty())
    {
        reporter = std::make_unique<TrainingProgressReporter>(config.progressRoot);
    }

    try
    {
        if (reporter != nullptr)
        {
            reporter->Initialize(0);
            reporter->Phase("scanning_corpus", "Ensuring corpus index for tokenizer-ready shard text.");
        }

        const std::filesystem::path corpusTextRoot =
            config.corpusTextRoot.empty()
                ? std::filesystem::path("D:\\CorpusShards")
                : config.corpusTextRoot;
        const CorpusIndexLoadResult corpusIndex = LoadOrRefreshCorpusIndex(corpusTextRoot, config, reporter.get());

        if (reporter != nullptr)
        {
            reporter->Complete();
        }

        TokenizerCorpusIndexStatus status;
        status.indexPath = corpusIndex.indexPath;
        status.reusedExisting = corpusIndex.reusedExisting;
        status.fileCount = corpusIndex.discoveries.size();
        return status;
    }
    catch (const std::exception& exception)
    {
        if (reporter != nullptr)
        {
            reporter->Fail(exception.what());
        }
        throw;
    }
}

TokenizerArtifacts TokenizerTrainer::BuildArtifacts(
    const std::filesystem::path& sourceRepoRoot,
    const std::filesystem::path& datasetRoot,
    const TokenizerTrainingConfig& config) const
{
    std::unique_ptr<TrainingProgressReporter> reporter;
    if (!config.progressRoot.empty())
    {
        reporter = std::make_unique<TrainingProgressReporter>(config.progressRoot);
    }

    if (config.targetVocabSize <= config.specialTokens.size())
    {
        throw std::runtime_error("Tokenizer target vocab size must exceed the special-token count.");
    }
    try
    {
        if (reporter != nullptr)
        {
            reporter->Initialize(0);
            reporter->Phase("scanning_corpus", "Scanning shard corpus and reviewed project exports.");
        }

        const BuiltCorpus corpus = BuildCorpus(sourceRepoRoot, datasetRoot, config, reporter.get());
        if (reporter != nullptr)
        {
            reporter->SetTotalFiles(corpus.trainingFiles.size());
        }
        const SentencePieceTrainingResult trainingResult = TrainSentencePieceModel(corpus, config, reporter.get());
        const SharedTokenizer tokenizer = SharedTokenizer::LoadFromString(trainingResult.serializedModel);

        TokenizerManifest manifest;
        manifest.tokenizerType = tokenizer.GetTokenizerType();
        manifest.normalizerName = tokenizer.GetNormalizerName();
        manifest.requestedVocabSize = config.targetVocabSize;
        manifest.learnedVocabSize = tokenizer.GetVocabSize();
        manifest.specialTokenCount = tokenizer.GetSpecialTokenIds().size();
        manifest.trainingTextCount = trainingResult.selectedLines;
        manifest.scannedTrainingTextCount = corpus.trainingTextCount;
        manifest.guaranteedTrainingTextCount = corpus.guaranteedTrainingTextCount;
        manifest.heldOutTextCount = corpus.heldOutTextCount;
        manifest.repeatedTrainingExampleCount = corpus.repeatedTrainingExampleCount;
        manifest.totalTrainingCharacters = corpus.totalTrainingCharacters;
        manifest.totalHeldOutCharacters = corpus.totalHeldOutCharacters;
        manifest.sourceFiles = corpus.sourceFiles;
        manifest.specialTokenIds = tokenizer.GetSpecialTokenIds();

        for (const CorpusText& text : corpus.heldOutTexts)
        {
            const std::vector<int> tokenIds = tokenizer.Encode(text.text);
            manifest.heldOutTokenCount += tokenIds.size();
            manifest.heldOutCharacterCount += text.text.size();

            for (int tokenId : tokenIds)
            {
                const TokenDefinition& token = tokenizer.GetTokens().at(static_cast<std::size_t>(tokenId));
                if (token.piece.rfind("<0x", 0) == 0)
                {
                    ++manifest.byteFallbackTokenCount;
                }
            }
        }

        if (manifest.heldOutTokenCount > 0)
        {
            manifest.averageCharactersPerToken =
                static_cast<double>(manifest.heldOutCharacterCount) / static_cast<double>(manifest.heldOutTokenCount);
            manifest.averageHeldOutCharactersPerToken = manifest.averageCharactersPerToken;
        }

        if (manifest.heldOutTextCount > 0)
        {
            manifest.averageHeldOutTokensPerExample =
                static_cast<double>(manifest.heldOutTokenCount) / static_cast<double>(manifest.heldOutTextCount);
        }

        TokenizerArtifacts artifacts;
        artifacts.tokenizer = tokenizer;
        artifacts.manifest = manifest;
        artifacts.modelText = trainingResult.serializedModel;
        artifacts.vocabText = tokenizer.SerializeVocab();
        artifacts.configText = BuildConfigJson(config);
        artifacts.manifestText = BuildManifestJson(manifest);
        artifacts.samples = BuildReportSamples(tokenizer);
        artifacts.reportText = BuildReportMarkdown(manifest);
        artifacts.samplesText = BuildSamplesMarkdown(artifacts.samples);

        if (reporter != nullptr)
        {
            reporter->Phase("writing_reports", "Tokenizer model training finished. Writing artifacts and reports.");
            reporter->Complete();
        }

        return artifacts;
    }
    catch (const std::exception& exception)
    {
        if (reporter != nullptr)
        {
            reporter->Fail(exception.what());
        }
        throw;
    }
}

bool TokenizerTrainer::WriteArtifacts(
    const TokenizerArtifacts& artifacts,
    const TokenizerWriteOptions& options,
    std::string& error) const
{
    const std::filesystem::path manifestsRoot = options.manifestsRoot.empty()
        ? options.outputRoot / "manifests" / "tokenizer"
        : options.manifestsRoot;
    const std::filesystem::path reportsRoot = options.reportsRoot.empty()
        ? options.outputRoot / "reports" / "tokenizer"
        : options.reportsRoot;

    if (!WriteFile(manifestsRoot / "shared_tokenizer.model", artifacts.modelText, error))
    {
        return false;
    }

    if (!WriteFile(manifestsRoot / "shared_tokenizer.vocab.tsv", artifacts.vocabText, error))
    {
        return false;
    }

    if (!WriteFile(manifestsRoot / "shared_tokenizer.config.json", artifacts.configText, error))
    {
        return false;
    }

    if (!WriteFile(manifestsRoot / "shared_tokenizer.manifest.json", artifacts.manifestText, error))
    {
        return false;
    }

    if (options.writeReport && !WriteFile(reportsRoot / "tokenizer_report.md", artifacts.reportText, error))
    {
        return false;
    }

    if (options.writeSamples && !WriteFile(reportsRoot / "tokenizer_samples.md", artifacts.samplesText, error))
    {
        return false;
    }

    return true;
}
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mina::Tokenizer
{
struct TokenConversionConfig
{
    std::filesystem::path inputRoot = std::filesystem::path("D:\\CorpusShards");
    std::filesystem::path outputRoot = std::filesystem::path("D:\\ConvertedTokens");
    std::filesystem::path tokenizerRoot = std::filesystem::path("C:\\MINA\\tokenizer");
    bool recursiveDiscovery = true;
    bool resume = true;
    bool stopOnFailure = false;
    std::size_t maxFilesPerRun = 0;
    std::uint64_t progressIntervalSeconds = 240;
    std::size_t readBufferBytes = 1048576;
    std::size_t targetSectionCount = 20;
    std::size_t workerCount = 0;
    bool halfCpuUsage = false;
    bool parallelizeSections = false;
};

struct TokenConversionFileRecord
{
    std::string relativeInputPath;
    std::string relativeOutputPath;
    std::string status;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
    std::uint64_t tokenCount = 0;
    std::uint64_t unknownTokenCount = 0;
    std::string note;
};

struct TokenConversionResult
{
    bool resumed = false;
    bool completed = false;
    bool interrupted = false;
    std::string tokenizerId;
    std::string tokenizerVersion;
    std::string tokenizerModelHash;
    std::size_t totalFilesDiscovered = 0;
    std::size_t completedFileCount = 0;
    std::size_t skippedFileCount = 0;
    std::size_t failedFileCount = 0;
    std::uint64_t totalTokensWritten = 0;
    std::uint64_t totalUnknownTokens = 0;
    std::string lastFullyCompletedFile;
    std::filesystem::path progressPath;
    std::filesystem::path orderedFilesPath;
    std::filesystem::path fileRecordsPath;
    std::filesystem::path manifestPath;
    std::filesystem::path reportPath;
    std::filesystem::path logPath;
    std::vector<TokenConversionFileRecord> fileRecords;
};

class TokenConversionPipeline
{
public:
    [[nodiscard]]
    TokenConversionResult Run(const TokenConversionConfig& config) const;
};
}

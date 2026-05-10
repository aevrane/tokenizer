#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mina::Tokenizer
{
struct ParquetIngestConfig
{
    std::filesystem::path parquetRoot = std::filesystem::path("C:\\MINA\\datasets");
    std::filesystem::path outputRoot = std::filesystem::path("C:\\MINA\\tokenizer");
    std::string explicitTextColumn;
    std::vector<std::string> candidateTextColumns = {
        "raw_text",
        "text",
        "content",
        "body_text",
        "normalized_text"
    };
    std::size_t batchSizeRows = 65536;
    std::uint64_t shardSizeBytes = 268435456ULL;
    bool recursiveDiscovery = true;
    bool resume = true;
    bool stopOnFileError = false;
    std::size_t maxFiles = 0;
    std::size_t maxBatches = 0;
};

struct ParquetIngestResult
{
    bool resumed = false;
    bool completed = false;
    bool interrupted = false;
    std::size_t discoveredFileCount = 0;
    std::size_t completedFileCount = 0;
    std::size_t skippedFileCount = 0;
    std::uint64_t totalRowsProcessed = 0;
    std::uint64_t totalTextRowsWritten = 0;
    std::uint64_t totalTextBytesWritten = 0;
    std::vector<std::filesystem::path> outputShards;
    std::filesystem::path progressPath;
    std::filesystem::path fileLogPath;
    std::filesystem::path manifestPath;
    std::filesystem::path reportPath;
    std::filesystem::path logPath;
};

class ParquetCorpusIngestor
{
public:
    [[nodiscard]]
    ParquetIngestResult Run(const ParquetIngestConfig& config) const;
};
}

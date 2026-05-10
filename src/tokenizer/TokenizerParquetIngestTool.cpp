#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "tokenizer/ParquetCorpusIngestion.h"

namespace
{
void PrintUsage()
{
    std::cerr
        << "Usage: tokenizer_parquet_ingest_tool"
        << " --parquet-root <path>"
        << " [--output-root <path>]"
        << " [--text-column <name>]"
        << " [--batch-size <rows>]"
        << " [--shard-size-mb <megabytes>]"
        << " [--no-resume]"
        << " [--flat]"
        << " [--stop-on-file-error]"
        << " [--max-files <count>]"
        << " [--max-batches <count>]\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        Mina::Tokenizer::ParquetIngestConfig config;

        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];

            auto requireValue = [&](const std::string& name) -> std::string
            {
                if (index + 1 >= argc)
                {
                    throw std::runtime_error("Missing value for " + name);
                }
                ++index;
                return argv[index];
            };

            if (argument == "--parquet-root")
            {
                config.parquetRoot = std::filesystem::path(requireValue(argument));
            }
            else if (argument == "--output-root")
            {
                config.outputRoot = std::filesystem::path(requireValue(argument));
            }
            else if (argument == "--text-column")
            {
                config.explicitTextColumn = requireValue(argument);
            }
            else if (argument == "--batch-size")
            {
                config.batchSizeRows = static_cast<std::size_t>(std::stoull(requireValue(argument)));
            }
            else if (argument == "--shard-size-mb")
            {
                config.shardSizeBytes = static_cast<std::uint64_t>(std::stoull(requireValue(argument))) * 1024ULL * 1024ULL;
            }
            else if (argument == "--no-resume")
            {
                config.resume = false;
            }
            else if (argument == "--flat")
            {
                config.recursiveDiscovery = false;
            }
            else if (argument == "--stop-on-file-error")
            {
                config.stopOnFileError = true;
            }
            else if (argument == "--max-files")
            {
                config.maxFiles = static_cast<std::size_t>(std::stoull(requireValue(argument)));
            }
            else if (argument == "--max-batches")
            {
                config.maxBatches = static_cast<std::size_t>(std::stoull(requireValue(argument)));
            }
            else if (argument == "--help" || argument == "-h")
            {
                PrintUsage();
                return 0;
            }
            else
            {
                throw std::runtime_error("Unknown argument: " + argument);
            }
        }

        if (config.parquetRoot.empty())
        {
            PrintUsage();
            return 1;
        }

        const Mina::Tokenizer::ParquetCorpusIngestor ingestor;
        const Mina::Tokenizer::ParquetIngestResult result = ingestor.Run(config);

        std::cout
            << "tokenizer_parquet_ingest_tool finished with status="
            << (result.interrupted ? "interrupted" : "completed")
            << " discovered_files=" << result.discoveredFileCount
            << " completed_files=" << result.completedFileCount
            << " skipped_files=" << result.skippedFileCount
            << " total_rows=" << result.totalRowsProcessed
            << " text_bytes=" << result.totalTextBytesWritten
            << "\nprogress=" << result.progressPath.string()
            << "\nreport=" << result.reportPath.string()
            << "\nlog=" << result.logPath.string()
            << "\n";
        return result.interrupted ? 2 : 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "tokenizer_parquet_ingest_tool failed: " << exception.what() << '\n';
        return 1;
    }
}

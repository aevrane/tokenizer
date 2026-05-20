#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "tokenizer/TokenConversionPipeline.h"

namespace
{
void PrintUsage()
{
    std::cout
        << "tokenizer_convert_tool [options]\n\n"
        << "Options:\n"
        << "  --input-root <path>                 Default: D:\\CorpusShards\n"
        << "  --output-root <path>                Default: D:\\ConvertedTokens\n"
        << "  --tokenizer-root <path>             Default: C:\\MINA\\tokenizer\n"
        << "  --progress-interval-seconds <n>     Default: 240\n"
        << "  --section-count <n>                 Default: 20\n"
        << "  --worker-count <auto|n>             Default: auto\n"
        << "  --cpu-mode <full|half>              Default: full when worker-count is auto\n"
        << "  --parallel-mode <files|sections>    Default: files\n"
        << "  --max-files <n>                     Stop after n files for validation/resume testing\n"
        << "  --no-recursive                      Only scan the top-level input directory\n"
        << "  --no-resume                         Ignore any existing progress file and start fresh\n"
        << "  --stop-on-failure                   Stop on the first file failure\n"
        << "  --help                              Show this help text\n";
}

std::string RequireValue(int& index, int argc, char** argv, const std::string& option)
{
    if (index + 1 >= argc)
    {
        throw std::runtime_error("Missing value for " + option);
    }

    ++index;
    return argv[index];
}
}

int main(int argc, char** argv)
{
    try
    {
        Mina::Tokenizer::TokenConversionConfig config;

        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--help")
            {
                PrintUsage();
                return 0;
            }
            if (argument == "--input-root")
            {
                config.inputRoot = std::filesystem::path(RequireValue(index, argc, argv, argument));
            }
            else if (argument == "--output-root")
            {
                config.outputRoot = std::filesystem::path(RequireValue(index, argc, argv, argument));
            }
            else if (argument == "--tokenizer-root")
            {
                config.tokenizerRoot = std::filesystem::path(RequireValue(index, argc, argv, argument));
            }
            else if (argument == "--progress-interval-seconds")
            {
                config.progressIntervalSeconds = static_cast<std::uint64_t>(
                    std::stoull(RequireValue(index, argc, argv, argument)));
            }
            else if (argument == "--section-count")
            {
                config.targetSectionCount = static_cast<std::size_t>(
                    std::stoull(RequireValue(index, argc, argv, argument)));
            }
            else if (argument == "--worker-count")
            {
                const std::string value = RequireValue(index, argc, argv, argument);
                if (value == "auto")
                {
                    config.workerCount = 0;
                }
                else
                {
                    config.workerCount = static_cast<std::size_t>(std::stoull(value));
                }
            }
            else if (argument == "--cpu-mode")
            {
                const std::string value = RequireValue(index, argc, argv, argument);
                if (value == "full")
                {
                    config.halfCpuUsage = false;
                }
                else if (value == "half")
                {
                    config.halfCpuUsage = true;
                }
                else
                {
                    throw std::runtime_error("Unsupported value for --cpu-mode: " + value);
                }
            }
            else if (argument == "--parallel-mode")
            {
                const std::string value = RequireValue(index, argc, argv, argument);
                if (value == "files")
                {
                    config.parallelizeSections = false;
                }
                else if (value == "sections")
                {
                    config.parallelizeSections = true;
                }
                else
                {
                    throw std::runtime_error("Unsupported value for --parallel-mode: " + value);
                }
            }
            else if (argument == "--max-files")
            {
                config.maxFilesPerRun = static_cast<std::size_t>(
                    std::stoull(RequireValue(index, argc, argv, argument)));
            }
            else if (argument == "--no-recursive")
            {
                config.recursiveDiscovery = false;
            }
            else if (argument == "--no-resume")
            {
                config.resume = false;
            }
            else if (argument == "--stop-on-failure")
            {
                config.stopOnFailure = true;
            }
            else
            {
                throw std::runtime_error("Unknown argument: " + argument);
            }
        }

        const Mina::Tokenizer::TokenConversionPipeline pipeline;
        const Mina::Tokenizer::TokenConversionResult result = pipeline.Run(config);
        return result.completed ? 0 : 2;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "tokenizer_convert_tool failed: " << exception.what() << '\n';
        return 1;
    }
}

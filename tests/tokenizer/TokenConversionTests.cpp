#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tokenizer/SharedTokenizer.h"
#include "tokenizer/TokenConversionPipeline.h"

namespace
{
void AssertTrue(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::filesystem::path TempRoot(const std::string& name)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    return root;
}

void WriteTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to write file: " + path.string());
    }

    stream << content;
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to read file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::vector<std::uint16_t> ReadTokenFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to read token file: " + path.string());
    }

    std::vector<std::uint16_t> values;
    std::uint16_t value = 0;
    while (stream.read(reinterpret_cast<char*>(&value), sizeof(value)))
    {
        values.push_back(value);
    }

    return values;
}

Mina::Tokenizer::TokenConversionConfig BuildFixtureConfig(const std::filesystem::path& root)
{
    Mina::Tokenizer::TokenConversionConfig config;
    config.inputRoot = root / "input";
    config.outputRoot = root / "output";
    config.tokenizerRoot = std::filesystem::path("C:\\MINA\\tokenizer");
    config.progressIntervalSeconds = 1;
    config.resume = true;
    config.workerCount = 2;
    return config;
}

void CreateFixtureInput(const std::filesystem::path& inputRoot)
{
    WriteTextFile(inputRoot / "b.txt", "Second file.\nWith two lines.\n");
    WriteTextFile(inputRoot / "nested" / "a.txt", "First file in lexical order.\n");
    WriteTextFile(inputRoot / "ignore.md", "Not eligible.\n");
}

void RunDeterministicOrderingAndUint16ValidationTest()
{
    const std::filesystem::path root = TempRoot("mina_token_conversion_ordering_test");
    const Mina::Tokenizer::TokenConversionConfig config = BuildFixtureConfig(root);
    CreateFixtureInput(config.inputRoot);

    const Mina::Tokenizer::TokenConversionPipeline pipeline;
    const Mina::Tokenizer::TokenConversionResult result = pipeline.Run(config);
    AssertTrue(result.completed, "Fixture conversion should complete.");
    AssertTrue(result.totalFilesDiscovered == 2, "Only .txt fixture inputs should be discovered.");

    const std::string orderedFilesText = ReadTextFile(result.orderedFilesPath);
    std::istringstream orderedStream(orderedFilesText);
    std::string headerLine;
    std::string firstEntry;
    std::string secondEntry;
    std::getline(orderedStream, headerLine);
    std::getline(orderedStream, firstEntry);
    std::getline(orderedStream, secondEntry);
    AssertTrue(firstEntry.find("b.txt") != std::string::npos && secondEntry.find("nested/a.txt") != std::string::npos,
        "Ordered file manifest should be lexically sorted by relative path.");

    const Mina::Tokenizer::SharedTokenizer tokenizer =
        Mina::Tokenizer::SharedTokenizer::LoadFromFile(
            std::filesystem::path("C:\\MINA\\tokenizer") / "manifests" / "tokenizer" / "shared_tokenizer.model");

    const std::vector<int> expectedFirst = tokenizer.Encode(ReadTextFile(config.inputRoot / "nested" / "a.txt"));
    const std::vector<std::uint16_t> actualFirst =
        ReadTokenFile(config.outputRoot / "tokens" / "nested" / "a.tokens.bin");
    AssertTrue(actualFirst.size() == expectedFirst.size(), "Token file should preserve token count.");
    for (std::size_t index = 0; index < actualFirst.size(); ++index)
    {
        AssertTrue(actualFirst[index] == static_cast<std::uint16_t>(expectedFirst[index]),
            "Token file should store the tokenizer ids as uint16 values.");
    }

    AssertTrue(!std::filesystem::exists(config.outputRoot / "tokens" / "nested" / "a.tokens.bin.tmp"),
        "Successful conversion should not leave a temp token file behind.");
}

void RunResumeAndPartialCleanupTest()
{
    const std::filesystem::path root = TempRoot("mina_token_conversion_resume_test");
    Mina::Tokenizer::TokenConversionConfig firstConfig = BuildFixtureConfig(root);
    CreateFixtureInput(firstConfig.inputRoot);
    firstConfig.maxFilesPerRun = 1;

    const Mina::Tokenizer::TokenConversionPipeline pipeline;
    const Mina::Tokenizer::TokenConversionResult firstRun = pipeline.Run(firstConfig);
    AssertTrue(firstRun.interrupted, "First run should stop early to exercise resume.");
    AssertTrue(firstRun.completedFileCount == 1, "First run should complete exactly one file.");

    WriteTextFile(firstConfig.outputRoot / "tokens" / "nested" / "a.tokens.bin", "bad");
    WriteTextFile(firstConfig.outputRoot / "tokens" / "nested" / "a.tokens.bin.tmp", "stale");

    Mina::Tokenizer::TokenConversionConfig resumeConfig = BuildFixtureConfig(root);
    const Mina::Tokenizer::TokenConversionResult resumedRun = pipeline.Run(resumeConfig);
    AssertTrue(resumedRun.resumed, "Second run should resume from the saved progress file.");
    AssertTrue(resumedRun.completed, "Resumed run should finish cleanly.");
    AssertTrue(resumedRun.completedFileCount == 2, "Resume should finish the remaining file.");

    const std::filesystem::path finalPath = resumeConfig.outputRoot / "tokens" / "nested" / "a.tokens.bin";
    const std::uint64_t finalSize = std::filesystem::file_size(finalPath);
    AssertTrue(finalSize > 3, "Resume should replace the stale partial output with real token data.");
    AssertTrue(!std::filesystem::exists(resumeConfig.outputRoot / "tokens" / "nested" / "a.tokens.bin.tmp"),
        "Resume should delete stale temp token output before rewriting the file.");
}

void RunTokenizerVersionConsistencyCheckTest()
{
    const std::filesystem::path root = TempRoot("mina_token_conversion_version_test");
    const Mina::Tokenizer::TokenConversionConfig config = BuildFixtureConfig(root);
    CreateFixtureInput(config.inputRoot);

    const Mina::Tokenizer::TokenConversionPipeline pipeline;
    const Mina::Tokenizer::TokenConversionResult initialRun = pipeline.Run(config);
    AssertTrue(initialRun.completed, "Initial run should complete before tampering.");

    std::string progressText = ReadTextFile(config.outputRoot / "progress.json");
    const std::size_t keyPosition = progressText.find("\"tokenizer_version\":");
    AssertTrue(keyPosition != std::string::npos, "Progress file should contain tokenizer version metadata.");
    const std::size_t firstQuote = progressText.find('"', keyPosition + 20);
    const std::size_t secondQuote = progressText.find('"', firstQuote + 1);
    progressText.replace(firstQuote + 1, secondQuote - firstQuote - 1, "tampered-version");
    WriteTextFile(config.outputRoot / "progress.json", progressText);

    bool threw = false;
    try
    {
        const Mina::Tokenizer::TokenConversionResult ignored = pipeline.Run(config);
        static_cast<void>(ignored);
    }
    catch (const std::exception&)
    {
        threw = true;
    }

    AssertTrue(threw, "Resume should fail when the tokenizer version in progress.json no longer matches.");
}

void RunReportGenerationSanityTest()
{
    const std::filesystem::path root = TempRoot("mina_token_conversion_report_test");
    const Mina::Tokenizer::TokenConversionConfig config = BuildFixtureConfig(root);
    CreateFixtureInput(config.inputRoot);

    const Mina::Tokenizer::TokenConversionPipeline pipeline;
    const Mina::Tokenizer::TokenConversionResult result = pipeline.Run(config);

    const std::string report = ReadTextFile(result.reportPath);
    AssertTrue(report.find("Token Conversion Report") != std::string::npos, "Final report should be written.");
    AssertTrue(report.find("unknown-token percentage") != std::string::npos,
        "Final report should include unknown-token percentage.");
    AssertTrue(report.find("nested/a.txt") != std::string::npos, "Final report should include per-file counts.");

    const std::string manifest = ReadTextFile(result.manifestPath);
    AssertTrue(manifest.find("\"token_id_type\": \"uint16_le\"") != std::string::npos,
        "Manifest should declare the uint16 output format.");

    const std::string progress = ReadTextFile(result.progressPath);
    AssertTrue(progress.find("\"configured_worker_count\": 2") != std::string::npos,
        "Progress should record the configured parallel worker count.");
}

void RunParallelSectionConversionSanityTest()
{
    const std::filesystem::path root = TempRoot("mina_token_conversion_parallel_sections_test");
    Mina::Tokenizer::TokenConversionConfig config = BuildFixtureConfig(root);
    config.parallelizeSections = true;
    config.targetSectionCount = 20;
    config.maxFilesPerRun = 1;

    const std::string multilineSample =
        "Alpha section line one.\n"
        "Alpha section line two.\n"
        "Beta section line three.\n"
        "Gamma section line four.\n"
        "Delta section line five.\n"
        "Epsilon section line six.\n";
    WriteTextFile(config.inputRoot / "one.txt", multilineSample);
    WriteTextFile(config.inputRoot / "two.txt", "Second file to keep max-files meaningful.\n");

    const Mina::Tokenizer::TokenConversionPipeline pipeline;
    const Mina::Tokenizer::TokenConversionResult result = pipeline.Run(config);
    AssertTrue(result.interrupted, "Single-file benchmark run should stop after one file when max-files=1.");
    AssertTrue(result.completedFileCount == 1, "Parallel section mode should complete the requested single file.");

    const std::vector<std::uint16_t> actual =
        ReadTokenFile(config.outputRoot / "tokens" / "one.tokens.bin");
    
    Mina::Tokenizer::TokenConversionConfig sequentialConfig = BuildFixtureConfig(root / "sequential");
    sequentialConfig.parallelizeSections = false;
    sequentialConfig.targetSectionCount = 20;
    sequentialConfig.maxFilesPerRun = 1;
    WriteTextFile(sequentialConfig.inputRoot / "one.txt", multilineSample);
    WriteTextFile(sequentialConfig.inputRoot / "two.txt", "Second file to keep max-files meaningful.\n");
    const Mina::Tokenizer::TokenConversionResult sequentialResult = pipeline.Run(sequentialConfig);
    AssertTrue(sequentialResult.completedFileCount == 1,
        "Sequential section baseline should complete the requested benchmark file.");
    const std::vector<std::uint16_t> expected =
        ReadTokenFile(sequentialConfig.outputRoot / "tokens" / "one.tokens.bin");

    AssertTrue(actual.size() == expected.size(),
        "Parallel section mode should preserve token count versus sequential sectioned conversion.");
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        AssertTrue(actual[index] == expected[index],
            "Parallel section mode should preserve token ids versus sequential sectioned conversion.");
    }
}
}

int main()
{
    try
    {
        RunDeterministicOrderingAndUint16ValidationTest();
        RunResumeAndPartialCleanupTest();
        RunTokenizerVersionConsistencyCheckTest();
        RunReportGenerationSanityTest();
        RunParallelSectionConversionSanityTest();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>

#include "tokenizer/ParquetCorpusIngestion.h"
#include "tokenizer/TokenizerPipeline.h"

namespace
{
void AssertTrue(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename T>
T ValueOrThrow(arrow::Result<T> result, const std::string& context)
{
    if (!result.ok())
    {
        throw std::runtime_error(context + ": " + result.status().ToString());
    }

    return std::move(result).ValueOrDie();
}

void CheckStatus(const arrow::Status& status, const std::string& context)
{
    if (!status.ok())
    {
        throw std::runtime_error(context + ": " + status.ToString());
    }
}

std::filesystem::path GetFixtureRoot()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" / "tokenizer_repo";
}

std::filesystem::path MakeTempRoot(const std::string& name)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    return root;
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

void CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(source))
    {
        const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), source);
        const std::filesystem::path targetPath = destination / relativePath;
        if (entry.is_directory())
        {
            std::filesystem::create_directories(targetPath);
        }
        else if (entry.is_regular_file())
        {
            std::filesystem::create_directories(targetPath.parent_path());
            std::filesystem::copy_file(entry.path(), targetPath, std::filesystem::copy_options::overwrite_existing);
        }
    }
}

void WriteParquetFile(
    const std::filesystem::path& path,
    const std::string& textColumnName,
    const std::vector<std::string>& texts,
    std::size_t rowGroupSize)
{
    arrow::StringBuilder idBuilder;
    arrow::StringBuilder textBuilder;

    for (std::size_t index = 0; index < texts.size(); ++index)
    {
        CheckStatus(idBuilder.Append("source-" + std::to_string(index)), "Append id");
        CheckStatus(textBuilder.Append(texts[index]), "Append text");
    }

    std::shared_ptr<arrow::Array> ids;
    std::shared_ptr<arrow::Array> textArray;
    CheckStatus(idBuilder.Finish(&ids), "Finish id array");
    CheckStatus(textBuilder.Finish(&textArray), "Finish text array");

    const std::shared_ptr<arrow::Schema> schema = arrow::schema({
        arrow::field("source_id", arrow::utf8()),
        arrow::field(textColumnName, arrow::utf8())
    });
    const std::shared_ptr<arrow::Table> table = arrow::Table::Make(schema, {ids, textArray});

    const std::shared_ptr<parquet::WriterProperties> writerProps =
        parquet::WriterProperties::Builder().build();
    const std::shared_ptr<parquet::ArrowWriterProperties> arrowWriterProps =
        parquet::ArrowWriterProperties::Builder().store_schema()->build();

    std::shared_ptr<arrow::io::FileOutputStream> output =
        ValueOrThrow(arrow::io::FileOutputStream::Open(path.string()), "Open parquet output");

    CheckStatus(
        parquet::arrow::WriteTable(
            *table,
            arrow::default_memory_pool(),
            output,
            static_cast<int64_t>(rowGroupSize),
            writerProps,
            arrowWriterProps),
        "Write parquet table");
}

std::string ReadAllCorpusText(const std::filesystem::path& outputRoot)
{
    const std::filesystem::path corpusRoot = outputRoot / "exports" / "tokenizer" / "parquet_corpus";
    std::vector<std::filesystem::path> shards;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(corpusRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".txt")
        {
            shards.push_back(entry.path());
        }
    }

    std::sort(shards.begin(), shards.end());

    std::string corpus;
    for (const std::filesystem::path& shard : shards)
    {
        corpus += ReadFile(shard);
    }

    return corpus;
}

void RunTrainingFixtureTest()
{
    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.targetVocabSize = 512;
    config.maxPieceLength = 12;

    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    config.corpusTextRoot = fixtureRoot / "corpus";
    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerArtifacts artifacts = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);

    AssertTrue(artifacts.manifest.learnedVocabSize == 512, "Tokenizer vocab size should match requested fixture size.");
    AssertTrue(artifacts.manifest.trainingTextCount > 0, "Selected training input should not be empty.");
    AssertTrue(artifacts.manifest.scannedTrainingTextCount > 0, "Scanned training corpus should not be empty.");
    AssertTrue(artifacts.manifest.guaranteedTrainingTextCount > 0,
        "Guaranteed training inputs should not be empty for the fixture corpus.");
    AssertTrue(artifacts.manifest.trainingTextCount >= artifacts.manifest.guaranteedTrainingTextCount,
        "Selected training input should include the guaranteed training examples.");
    AssertTrue(artifacts.manifest.heldOutTextCount == 3, "Unexpected tokenizer held-out text count for fixture corpus.");
    AssertTrue(artifacts.manifest.specialTokenIds.at("<unk>") == 0, "Expected <unk> special token id.");
    AssertTrue(artifacts.manifest.specialTokenIds.at("<pad>") == 3, "Expected <pad> special token id.");
    AssertTrue(artifacts.manifest.sourceFiles.size() == 7, "Expected corpus text files, four exports, and corpus inventory.");
    AssertTrue(artifacts.reportText.find("unknown tokens on held-out sample: `0`") != std::string::npos,
        "Tokenizer report should state zero unknown tokens.");
    AssertTrue(artifacts.reportText.find("final bounded SentencePiece input") != std::string::npos,
        "Tokenizer report should distinguish selected training lines from scanned corpus size.");
    AssertTrue(artifacts.samplesText.find("source attribution") != std::string::npos,
        "Tokenizer sample report should include attribution category.");
    AssertTrue(artifacts.samplesText.find("[ws]The") != std::string::npos,
        "Tokenizer sample report should render SentencePiece whitespace pieces with ASCII-safe markers.");
    AssertTrue(artifacts.samplesText.find("\xC3\xA2\xE2\x80\x93") == std::string::npos,
        "Tokenizer sample report should not leak mojibake whitespace markers.");
    AssertTrue(artifacts.manifestText.find("\"kind\": \"parquet_corpus_text\"") != std::string::npos,
        "Tokenizer manifest should record the shard corpus input kind.");
    AssertTrue(artifacts.manifestText.find("\"kind\": \"supplemental_corpus_text\"") != std::string::npos,
        "Tokenizer manifest should record the supplemental corpus input kind.");
}

void RunEncodeDecodeRoundTripTest()
{
    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.targetVocabSize = 512;

    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    config.corpusTextRoot = fixtureRoot / "corpus";
    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerArtifacts artifacts = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);

    const std::string sample = "The available inputs do not provide enough evidence, so the shell should preserve uncertainty.";
    const std::vector<int> tokenIds = artifacts.tokenizer.Encode(sample);
    const std::string decoded = artifacts.tokenizer.Decode(tokenIds);

    AssertTrue(!tokenIds.empty(), "Encoded token ids should not be empty.");
    AssertTrue(decoded == Mina::Tokenizer::NormalizeForTokenizer(sample), "Tokenizer decode should round-trip normalized text.");
}

void RunDeterministicArtifactTest()
{
    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.targetVocabSize = 512;

    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    config.corpusTextRoot = fixtureRoot / "corpus";
    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerArtifacts first = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);
    const Mina::Tokenizer::TokenizerArtifacts second = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);

    AssertTrue(first.modelText == second.modelText, "Tokenizer model generation should be deterministic.");
    AssertTrue(first.vocabText == second.vocabText, "Tokenizer vocab generation should be deterministic.");
    AssertTrue(first.manifestText == second.manifestText, "Tokenizer manifest generation should be deterministic.");
}

void RunUnsupportedPromptExclusionTest()
{
    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.targetVocabSize = 512;

    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    config.corpusTextRoot = fixtureRoot / "corpus";
    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerArtifacts artifacts = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);

    const auto supervisedSource = std::find_if(
        artifacts.manifest.sourceFiles.begin(),
        artifacts.manifest.sourceFiles.end(),
        [](const Mina::Tokenizer::TokenizerSourceFile& source)
        {
            return source.relativePath == "exports/small_supervised/small_supervised.jsonl";
        });
    AssertTrue(supervisedSource != artifacts.manifest.sourceFiles.end(), "Expected small supervised source entry.");
    AssertTrue(supervisedSource->characterCount > 0, "Expected supervised outputs to be counted.");
    AssertTrue(artifacts.manifest.trainingTextCount > 0,
        "Small supervised prompts for code/math-heavy requests should not cause the selected training input to be empty.");
}

void RunSupplementalCorpusAlwaysIncludedTest()
{
    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.targetVocabSize = 512;
    config.sampledSentenceCount = 1;

    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    config.corpusTextRoot = fixtureRoot / "corpus";
    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerArtifacts artifacts = trainer.BuildArtifacts(
        fixtureRoot,
        fixtureRoot / "dataset",
        config);

    const auto supplementalSource = std::find_if(
        artifacts.manifest.sourceFiles.begin(),
        artifacts.manifest.sourceFiles.end(),
        [](const Mina::Tokenizer::TokenizerSourceFile& source)
        {
            return source.relativePath == "supplemental-conversations.txt";
        });
    AssertTrue(supplementalSource != artifacts.manifest.sourceFiles.end(), "Expected supplemental corpus source entry.");
    AssertTrue(supplementalSource->textCount == 1, "Expected one supplemental corpus line in the fixture.");
    AssertTrue(artifacts.reportText.find("names start with `supplemental-` are always fully included") != std::string::npos,
        "Tokenizer report should explain supplemental always-include behavior.");

    const std::vector<int> tokenIds = artifacts.tokenizer.Encode(
        "Conversation phrasing should remain concise, literal, and non-agentic.");
    AssertTrue(!tokenIds.empty(), "Supplemental fixture sentence should still be tokenizable after low-budget training.");
}

void RunCorpusIndexReuseAndInvalidationTest()
{
    const std::filesystem::path fixtureRoot = GetFixtureRoot();
    const std::filesystem::path tempRoot = MakeTempRoot("mina_tokenizer_corpus_index_test");
    const std::filesystem::path corpusRoot = tempRoot / "corpus";
    const std::filesystem::path progressRoot = tempRoot / "out";
    const std::filesystem::path indexPath = progressRoot / "manifests" / "tokenizer" / "corpus_index.tsv";

    CopyDirectoryRecursive(fixtureRoot / "corpus", corpusRoot);

    Mina::Tokenizer::TokenizerTrainingConfig config;
    config.corpusTextRoot = corpusRoot;
    config.progressRoot = progressRoot;
    config.corpusIndexPath = indexPath;
    config.reuseCorpusIndex = true;

    const Mina::Tokenizer::TokenizerTrainer trainer;
    const Mina::Tokenizer::TokenizerCorpusIndexStatus first = trainer.EnsureCorpusIndex(config);
    AssertTrue(!first.reusedExisting, "First corpus index ensure should scan and write a fresh index.");
    AssertTrue(first.fileCount == 2, "Fixture corpus should contain one shard file and one supplemental file.");
    AssertTrue(std::filesystem::exists(first.indexPath), "Corpus index file should be created.");

    const std::string firstIndexText = ReadFile(first.indexPath);
    AssertTrue(firstIndexText.find("supplemental_corpus_text") != std::string::npos,
        "Corpus index should record the supplemental source kind.");

    const Mina::Tokenizer::TokenizerCorpusIndexStatus second = trainer.EnsureCorpusIndex(config);
    AssertTrue(second.reusedExisting, "Second corpus index ensure should reuse the unchanged cached index.");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteTextFile(
        corpusRoot / "shard-000000.txt",
        ReadFile(corpusRoot / "shard-000000.txt") + "\nA new indexed line should force corpus index invalidation.\n");

    const Mina::Tokenizer::TokenizerCorpusIndexStatus third = trainer.EnsureCorpusIndex(config);
    AssertTrue(!third.reusedExisting, "Changing a corpus shard should force the cached index to refresh.");

    const std::string refreshedIndexText = ReadFile(third.indexPath);
    AssertTrue(refreshedIndexText != firstIndexText, "Refreshed corpus index should differ after shard mutation.");
}

void RunParquetIngestionResumeTest()
{
    const std::filesystem::path tempRoot = MakeTempRoot("mina_tokenizer_parquet_test");
    const std::filesystem::path datasetRoot = tempRoot / "dataset";
    const std::filesystem::path outputRoot = tempRoot / "output";

    std::filesystem::create_directories(datasetRoot / "nested");
    WriteParquetFile(
        datasetRoot / "a.parquet",
        "raw_text",
        {
            " First sample from raw text. ",
            "Second sample from raw text.",
            "Third sample from raw text.",
            "Fourth sample from raw text."
        },
        2);
    WriteParquetFile(
        datasetRoot / "nested" / "b.parquet",
        "content",
        {
            "Nested content one.",
            "Nested content two."
        },
        2);
    WriteTextFile(datasetRoot / "bad.parquet", "not parquet");

    Mina::Tokenizer::ParquetIngestConfig interruptedConfig;
    interruptedConfig.parquetRoot = datasetRoot;
    interruptedConfig.outputRoot = outputRoot;
    interruptedConfig.batchSizeRows = 1;
    interruptedConfig.shardSizeBytes = 64;
    interruptedConfig.maxBatches = 2;

    const Mina::Tokenizer::ParquetCorpusIngestor ingestor;
    const Mina::Tokenizer::ParquetIngestResult firstRun = ingestor.Run(interruptedConfig);

    AssertTrue(firstRun.interrupted, "First ingest run should stop early to exercise resume.");
    AssertTrue(std::filesystem::exists(firstRun.progressPath), "Progress file should exist after interrupted run.");
    const std::string interruptedProgress = ReadFile(firstRun.progressPath);
    AssertTrue(interruptedProgress.find("\"status\": \"interrupted\"") != std::string::npos,
        "Interrupted progress should record interrupted status.");

    Mina::Tokenizer::ParquetIngestConfig resumeConfig;
    resumeConfig.parquetRoot = datasetRoot;
    resumeConfig.outputRoot = outputRoot;
    resumeConfig.batchSizeRows = 1;
    resumeConfig.shardSizeBytes = 64;
    resumeConfig.resume = true;

    const Mina::Tokenizer::ParquetIngestResult resumedRun = ingestor.Run(resumeConfig);
    AssertTrue(resumedRun.completed, "Resumed ingest should finish cleanly.");
    AssertTrue(resumedRun.resumed, "Second ingest run should resume from saved progress.");
    AssertTrue(resumedRun.completedFileCount == 2, "Expected two valid Parquet files to complete.");
    AssertTrue(resumedRun.skippedFileCount == 1, "Expected one bad Parquet file to be skipped.");
    AssertTrue(std::filesystem::exists(resumedRun.manifestPath), "Ingest manifest should be written.");
    AssertTrue(std::filesystem::exists(resumedRun.reportPath), "Ingest report should be written.");
    AssertTrue(std::filesystem::exists(resumedRun.logPath), "Ingest log should be written.");
    AssertTrue(resumedRun.outputShards.size() >= 2, "Small shard size should force multiple corpus shards.");

    const std::vector<std::string> expectedLines = {
        Mina::Tokenizer::NormalizeForTokenizer(" First sample from raw text. "),
        Mina::Tokenizer::NormalizeForTokenizer("Second sample from raw text."),
        Mina::Tokenizer::NormalizeForTokenizer("Third sample from raw text."),
        Mina::Tokenizer::NormalizeForTokenizer("Fourth sample from raw text."),
        Mina::Tokenizer::NormalizeForTokenizer("Nested content one."),
        Mina::Tokenizer::NormalizeForTokenizer("Nested content two.")
    };
    const std::vector<std::string> actualLines = SplitLines(ReadAllCorpusText(outputRoot));
    AssertTrue(actualLines == expectedLines, "Resumed ingest should export each normalized sample exactly once.");

    const std::string reportText = ReadFile(resumedRun.reportPath);
    AssertTrue(reportText.find("`raw_text`") != std::string::npos, "Report should mention detected raw_text column.");
    AssertTrue(reportText.find("`content`") != std::string::npos, "Report should mention fallback content column.");
    AssertTrue(reportText.find("bad.parquet") != std::string::npos, "Report should mention skipped bad file.");

    const std::string manifestText = ReadFile(resumedRun.manifestPath);
    AssertTrue(manifestText.find("\"completed_file_count\": 2") != std::string::npos,
        "Manifest should record completed file count.");
    AssertTrue(manifestText.find("\"skipped_file_count\": 1") != std::string::npos,
        "Manifest should record skipped file count.");
}
}

int main()
{
    try
    {
        RunTrainingFixtureTest();
        RunEncodeDecodeRoundTripTest();
        RunDeterministicArtifactTest();
        RunUnsupportedPromptExclusionTest();
        RunSupplementalCorpusAlwaysIncludedTest();
        RunCorpusIndexReuseAndInvalidationTest();
        RunParquetIngestionResumeTest();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

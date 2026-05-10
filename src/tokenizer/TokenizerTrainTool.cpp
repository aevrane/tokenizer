#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "tokenizer/TokenizerPipeline.h"

namespace
{
void ConfigureConsoleUtf8()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void PrintUsage()
{
    std::cout
        << "Usage: tokenizer_train_tool --source-repo-root <path> [options]\n"
        << "Legacy positional form is also supported:\n"
        << "  tokenizer_train_tool <source-repo-root> [dataset-root] [vocab-size] [output-root] [corpus-text-root]\n\n"
        << "Core options:\n"
        << "  --source-repo-root <path>\n"
        << "  --dataset-root <path>               Default: C:\\Datasets\n"
        << "  --output-root <path>                Default: C:\\Tokenizer\n"
        << "  --corpus-root <path>                Default: C:\\CorpusShards\n"
        << "  --index-path <path>                 Default: <output-root>\\manifests\\tokenizer\\corpus_index.tsv\n"
        << "  --vocab-size <count>                Default: 32000\n"
        << "  --sampled-sentence-count <count>    Default: 5000000\n"
        << "  --max-piece-length <count>          Default: 24\n"
        << "  --max-sentence-length <count>       Default: 16384\n\n"
        << "Weights:\n"
        << "  --parquet-weight <count>                    Default: 1\n"
        << "  --base-training-output-weight <count>       Default: 2\n"
        << "  --contextualization-input-weight <count>    Default: 1\n"
        << "  --contextualization-output-weight <count>   Default: 2\n"
        << "  --custom-verbalization-input-weight <count> Default: 1\n"
        << "  --custom-verbalization-output-weight <count> Default: 2\n"
        << "  --small-supervised-output-weight <count>    Default: 2\n\n"
        << "Logging and debug:\n"
        << "  --progress-every-lines <count>      Default: 1000000\n"
        << "  --verbose-trainer                   Enable SentencePiece INFO logging\n"
        << "  --quiet-trainer                     Suppress SentencePiece INFO logging\n"
        << "  --no-heartbeat                      Disable once-per-minute training heartbeat messages\n"
        << "  --scan-only                         Build or refresh the corpus scan/cache index and exit\n"
        << "  --reuse-index                       Reuse a valid cached corpus index when possible (default)\n"
        << "  --no-index-reuse                    Always rescan corpus text files and rewrite the index\n"
        << "  --force-rescan                      Alias for --no-index-reuse\n"
        << "  --print-config                      Print resolved config before running\n"
        << "  --dry-run                           Print resolved config and exit without training\n\n"
        << "Artifact writing:\n"
        << "  --manifest-root <path>              Override manifests/tokenizer output directory\n"
        << "  --report-root <path>                Override reports/tokenizer output directory\n"
        << "  --write-report\n"
        << "  --no-write-report\n"
        << "  --write-samples\n"
        << "  --no-write-samples\n\n"
        << "Other:\n"
        << "  --help, -h\n";
}

bool LooksLikeFlag(const char* value)
{
    return value != nullptr && value[0] == '-';
}

std::string Quote(const std::string& value)
{
    return "\"" + value + "\"";
}

std::string BuildResolvedConfigText(
    const std::filesystem::path& sourceRepoRoot,
    const std::filesystem::path& datasetRoot,
    const Mina::Tokenizer::TokenizerTrainingConfig& config,
    const Mina::Tokenizer::TokenizerWriteOptions& writeOptions,
    const bool scanOnly)
{
    const std::filesystem::path manifestsRoot = writeOptions.manifestsRoot.empty()
        ? writeOptions.outputRoot / "manifests" / "tokenizer"
        : writeOptions.manifestsRoot;
    const std::filesystem::path reportsRoot = writeOptions.reportsRoot.empty()
        ? writeOptions.outputRoot / "reports" / "tokenizer"
        : writeOptions.reportsRoot;

    std::ostringstream stream;
    stream << "resolved_config:\n";
    stream << "  source_repo_root: " << Quote(sourceRepoRoot.string()) << '\n';
    stream << "  dataset_root: " << Quote(datasetRoot.string()) << '\n';
    stream << "  corpus_root: " << Quote(config.corpusTextRoot.string()) << '\n';
    stream << "  corpus_index_path: " << Quote(config.corpusIndexPath.string()) << '\n';
    stream << "  output_root: " << Quote(writeOptions.outputRoot.string()) << '\n';
    stream << "  manifest_root: " << Quote(manifestsRoot.string()) << '\n';
    stream << "  report_root: " << Quote(reportsRoot.string()) << '\n';
    stream << "  scan_only: " << (scanOnly ? "true" : "false") << '\n';
    stream << "  reuse_corpus_index: " << (config.reuseCorpusIndex ? "true" : "false") << '\n';
    stream << "  vocab_size: " << config.targetVocabSize << '\n';
    stream << "  sampled_sentence_count: " << config.sampledSentenceCount << '\n';
    stream << "  max_piece_length: " << config.maxPieceLength << '\n';
    stream << "  max_sentence_length: " << config.maxSentenceLength << '\n';
    stream << "  progress_every_lines: " << config.progressEveryLines << '\n';
    stream << "  heartbeat_enabled: " << (config.heartbeatEnabled ? "true" : "false") << '\n';
    stream << "  sentencepiece_minloglevel: " << config.sentencePieceMinLogLevel << '\n';
    stream << "  parquet_weight: " << config.parquetCorpusWeight << '\n';
    stream << "  base_training_output_weight: " << config.baseTrainingOutputWeight << '\n';
    stream << "  contextualization_input_weight: " << config.contextualizationInputWeight << '\n';
    stream << "  contextualization_output_weight: " << config.contextualizationOutputWeight << '\n';
    stream << "  custom_verbalization_input_weight: " << config.customVerbalizationInputWeight << '\n';
    stream << "  custom_verbalization_output_weight: " << config.customVerbalizationOutputWeight << '\n';
    stream << "  small_supervised_output_weight: " << config.smallSupervisedOutputWeight << '\n';
    stream << "  write_report: " << (writeOptions.writeReport ? "true" : "false") << '\n';
    stream << "  write_samples: " << (writeOptions.writeSamples ? "true" : "false") << '\n';
    return stream.str();
}
}

int main(int argc, char** argv)
{
    try
    {
        ConfigureConsoleUtf8();

        std::filesystem::path sourceRepoRoot;
        std::filesystem::path datasetRoot = std::filesystem::path("C:\\Datasets");
        Mina::Tokenizer::TokenizerWriteOptions writeOptions;
        writeOptions.outputRoot = std::filesystem::path("C:\\Tokenizer");
        std::filesystem::path corpusTextRoot = std::filesystem::path("C:\\CorpusShards");
        std::filesystem::path corpusIndexPath;
        std::size_t vocabSize = 32000;
        std::size_t sampledSentenceCount = 5000000;
        std::size_t maxPieceLength = 24;
        std::size_t maxSentenceLength = 16384;
        std::size_t progressEveryLines = 1000000;
        std::size_t parquetWeight = 1;
        std::size_t baseTrainingOutputWeight = 2;
        std::size_t contextualizationInputWeight = 1;
        std::size_t contextualizationOutputWeight = 2;
        std::size_t customVerbalizationInputWeight = 1;
        std::size_t customVerbalizationOutputWeight = 2;
        std::size_t smallSupervisedOutputWeight = 2;
        bool heartbeatEnabled = true;
        int sentencePieceMinLogLevel = 1;
        bool reuseCorpusIndex = true;
        bool scanOnly = false;
        bool printConfig = false;
        bool dryRun = false;

        auto requireValue = [&](int& index, const std::string& name) -> std::string
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("Missing value for " + name);
            }
            ++index;
            return argv[index];
        };

        if (argc < 2)
        {
            PrintUsage();
            return 1;
        }

        if (!LooksLikeFlag(argv[1]))
        {
            sourceRepoRoot = std::filesystem::path(argv[1]);
            if (argc > 2)
            {
                datasetRoot = std::filesystem::path(argv[2]);
            }
            if (argc > 3)
            {
                vocabSize = static_cast<std::size_t>(std::stoull(argv[3]));
            }
            if (argc > 4)
            {
                writeOptions.outputRoot = std::filesystem::path(argv[4]);
            }
            if (argc > 5)
            {
                corpusTextRoot = std::filesystem::path(argv[5]);
            }
        }
        else
        {
            for (int index = 1; index < argc; ++index)
            {
                const std::string argument = argv[index];
                if (argument == "--source-repo-root")
                {
                    sourceRepoRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--dataset-root")
                {
                    datasetRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--output-root")
                {
                    writeOptions.outputRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--corpus-root")
                {
                    corpusTextRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--index-path")
                {
                    corpusIndexPath = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--vocab-size")
                {
                    vocabSize = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--sampled-sentence-count")
                {
                    sampledSentenceCount = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--max-piece-length")
                {
                    maxPieceLength = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--max-sentence-length")
                {
                    maxSentenceLength = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--progress-every-lines")
                {
                    progressEveryLines = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--parquet-weight")
                {
                    parquetWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--base-training-output-weight")
                {
                    baseTrainingOutputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--contextualization-input-weight")
                {
                    contextualizationInputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--contextualization-output-weight")
                {
                    contextualizationOutputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--custom-verbalization-input-weight")
                {
                    customVerbalizationInputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--custom-verbalization-output-weight")
                {
                    customVerbalizationOutputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--small-supervised-output-weight")
                {
                    smallSupervisedOutputWeight = static_cast<std::size_t>(std::stoull(requireValue(index, argument)));
                }
                else if (argument == "--manifest-root")
                {
                    writeOptions.manifestsRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--report-root")
                {
                    writeOptions.reportsRoot = std::filesystem::path(requireValue(index, argument));
                }
                else if (argument == "--write-report")
                {
                    writeOptions.writeReport = true;
                }
                else if (argument == "--no-write-report")
                {
                    writeOptions.writeReport = false;
                }
                else if (argument == "--write-samples")
                {
                    writeOptions.writeSamples = true;
                }
                else if (argument == "--no-write-samples")
                {
                    writeOptions.writeSamples = false;
                }
                else if (argument == "--verbose-trainer")
                {
                    sentencePieceMinLogLevel = 0;
                }
                else if (argument == "--quiet-trainer")
                {
                    sentencePieceMinLogLevel = 1;
                }
                else if (argument == "--no-heartbeat")
                {
                    heartbeatEnabled = false;
                }
                else if (argument == "--scan-only")
                {
                    scanOnly = true;
                }
                else if (argument == "--reuse-index")
                {
                    reuseCorpusIndex = true;
                }
                else if (argument == "--no-index-reuse" || argument == "--force-rescan")
                {
                    reuseCorpusIndex = false;
                }
                else if (argument == "--print-config")
                {
                    printConfig = true;
                }
                else if (argument == "--dry-run")
                {
                    dryRun = true;
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
        }

        if (sourceRepoRoot.empty())
        {
            PrintUsage();
            return 1;
        }

        Mina::Tokenizer::TokenizerTrainingConfig config;
        config.targetVocabSize = vocabSize;
        config.sampledSentenceCount = sampledSentenceCount;
        config.maxPieceLength = maxPieceLength;
        config.maxSentenceLength = maxSentenceLength;
        config.progressEveryLines = progressEveryLines;
        config.corpusTextRoot = corpusTextRoot;
        config.progressRoot = writeOptions.outputRoot;
        config.corpusIndexPath = corpusIndexPath;
        if (config.corpusIndexPath.empty())
        {
            config.corpusIndexPath = writeOptions.outputRoot / "manifests" / "tokenizer" / "corpus_index.tsv";
        }
        config.parquetCorpusWeight = parquetWeight;
        config.baseTrainingOutputWeight = baseTrainingOutputWeight;
        config.contextualizationInputWeight = contextualizationInputWeight;
        config.contextualizationOutputWeight = contextualizationOutputWeight;
        config.customVerbalizationInputWeight = customVerbalizationInputWeight;
        config.customVerbalizationOutputWeight = customVerbalizationOutputWeight;
        config.smallSupervisedOutputWeight = smallSupervisedOutputWeight;
        config.heartbeatEnabled = heartbeatEnabled;
        config.sentencePieceMinLogLevel = sentencePieceMinLogLevel;
        config.reuseCorpusIndex = reuseCorpusIndex;

        if (printConfig || dryRun)
        {
            std::cout << BuildResolvedConfigText(sourceRepoRoot, datasetRoot, config, writeOptions, scanOnly);
        }
        if (dryRun)
        {
            return 0;
        }

        const Mina::Tokenizer::TokenizerTrainer trainer;
        if (scanOnly)
        {
            const Mina::Tokenizer::TokenizerCorpusIndexStatus status = trainer.EnsureCorpusIndex(config);
            std::cout << "tokenizer_train_tool "
                      << (status.reusedExisting ? "reused" : "wrote")
                      << " corpus index at " << status.indexPath.string()
                      << " for " << status.fileCount << " corpus text files.\n";
            return 0;
        }

        const Mina::Tokenizer::TokenizerArtifacts artifacts = trainer.BuildArtifacts(sourceRepoRoot, datasetRoot, config);

        std::string error;
        if (!trainer.WriteArtifacts(artifacts, writeOptions, error))
        {
            std::cerr << "tokenizer_train_tool failed: " << error << '\n';
            return 1;
        }

        const std::filesystem::path manifestsRoot = writeOptions.manifestsRoot.empty()
            ? writeOptions.outputRoot / "manifests" / "tokenizer"
            : writeOptions.manifestsRoot;
        std::cout << "tokenizer_train_tool wrote shared tokenizer artifacts to "
                  << manifestsRoot.string() << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "tokenizer_train_tool failed: " << exception.what() << '\n';
        return 1;
    }
}

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "tokenizer/SharedTokenizer.h"

namespace Mina::Tokenizer
{
enum class TokenizerSourceKind
{
    ParquetCorpusText = 0,
    SupplementalCorpusText,
    BaseTraining,
    Contextualization,
    CustomVerbalization,
    SmallSupervised,
    HeldOutSample,
    CorpusInventory
};

struct TokenizerTrainingConfig
{
    std::size_t targetVocabSize = 32000;
    std::size_t maxPieceLength = 24;
    std::size_t maxSentenceLength = 16384;
    std::size_t sampledSentenceCount = 5000000;
    std::size_t progressEveryLines = 1000000;
    std::filesystem::path progressRoot;
    std::filesystem::path corpusTextRoot;
    std::filesystem::path corpusIndexPath;
    std::size_t parquetCorpusWeight = 1;
    std::size_t baseTrainingOutputWeight = 2;
    std::size_t contextualizationInputWeight = 1;
    std::size_t contextualizationOutputWeight = 2;
    std::size_t customVerbalizationInputWeight = 1;
    std::size_t customVerbalizationOutputWeight = 2;
    std::size_t smallSupervisedOutputWeight = 2;
    bool heartbeatEnabled = true;
    int sentencePieceMinLogLevel = 1;
    bool reuseCorpusIndex = true;
    std::vector<std::string> specialTokens = {"<pad>", "<bos>", "<eos>", "<unk>"};
};

struct TokenizerSourceFile
{
    std::string relativePath;
    TokenizerSourceKind kind = TokenizerSourceKind::ParquetCorpusText;
    bool includedInTraining = false;
    std::size_t recordCount = 0;
    std::size_t textCount = 0;
    std::size_t characterCount = 0;
    std::string checksum;
    std::string note;
};

struct TokenizerSample
{
    std::string label;
    std::string category;
    std::string inputText;
    std::string normalizedText;
    std::string decodedText;
    std::vector<int> tokenIds;
    std::vector<std::string> tokenPieces;
};

struct TokenizerManifest
{
    std::string tokenizerId = "shared-shell-tokenizer";
    std::string tokenizerVersion = "tokenizer-v0001";
    std::string tokenizerType = "sentencepiece_bpe_v1";
    std::string normalizerName = "english_project_v1";
    std::size_t requestedVocabSize = 32000;
    std::size_t learnedVocabSize = 0;
    std::size_t specialTokenCount = 0;
    std::size_t trainingTextCount = 0;
    std::size_t scannedTrainingTextCount = 0;
    std::size_t guaranteedTrainingTextCount = 0;
    std::size_t heldOutTextCount = 0;
    std::size_t repeatedTrainingExampleCount = 0;
    std::size_t totalTrainingCharacters = 0;
    std::size_t totalHeldOutCharacters = 0;
    std::size_t byteFallbackTokenCount = 0;
    std::size_t heldOutTokenCount = 0;
    std::size_t heldOutCharacterCount = 0;
    double averageCharactersPerToken = 0.0;
    double averageHeldOutTokensPerExample = 0.0;
    double averageHeldOutCharactersPerToken = 0.0;
    std::map<std::string, int> specialTokenIds;
    std::vector<TokenizerSourceFile> sourceFiles;
};

struct TokenizerArtifacts
{
    SharedTokenizer tokenizer;
    TokenizerManifest manifest;
    std::string modelText;
    std::string vocabText;
    std::string configText;
    std::string manifestText;
    std::string reportText;
    std::string samplesText;
    std::vector<TokenizerSample> samples;
};

struct TokenizerWriteOptions
{
    std::filesystem::path outputRoot = std::filesystem::path("C:\\Tokenizer");
    std::filesystem::path manifestsRoot;
    std::filesystem::path reportsRoot;
    bool writeReport = true;
    bool writeSamples = true;
};

struct TokenizerCorpusIndexStatus
{
    std::filesystem::path indexPath;
    bool reusedExisting = false;
    std::size_t fileCount = 0;
};

class TokenizerTrainer
{
public:
    [[nodiscard]]
    TokenizerCorpusIndexStatus EnsureCorpusIndex(const TokenizerTrainingConfig& config) const;

    [[nodiscard]]
    TokenizerArtifacts BuildArtifacts(
        const std::filesystem::path& sourceRepoRoot,
        const std::filesystem::path& datasetRoot,
        const TokenizerTrainingConfig& config) const;

    [[nodiscard]]
    bool WriteArtifacts(
        const TokenizerArtifacts& artifacts,
        const TokenizerWriteOptions& options,
        std::string& error) const;
};
}

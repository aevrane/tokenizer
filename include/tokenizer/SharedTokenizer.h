#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Mina::Tokenizer
{
struct TokenDefinition
{
    int id = -1;
    bool special = false;
    std::string label;
    std::string piece;
};

std::string NormalizeForTokenizer(const std::string& input);

class SharedTokenizer
{
public:
    SharedTokenizer() = default;
    SharedTokenizer(
        const std::string& serializedModel,
        const std::vector<TokenDefinition>& tokens,
        const std::map<std::string, int>& specialTokenIds,
        const std::string& tokenizerType,
        const std::string& normalizerName);

public:
    [[nodiscard]]
    static SharedTokenizer LoadFromFile(const std::filesystem::path& modelPath);

    [[nodiscard]]
    static SharedTokenizer LoadFromString(const std::string& serializedModel);

    [[nodiscard]]
    std::vector<int> Encode(const std::string& input) const;

    [[nodiscard]]
    std::vector<int> EncodeNormalized(const std::string& normalizedInput) const;

    [[nodiscard]]
    std::string Decode(const std::vector<int>& tokenIds) const;

    [[nodiscard]]
    std::vector<std::string> DescribeTokens(const std::vector<int>& tokenIds) const;

    [[nodiscard]]
    std::size_t GetVocabSize() const noexcept;

    [[nodiscard]]
    const std::vector<TokenDefinition>& GetTokens() const noexcept;

    [[nodiscard]]
    const std::map<std::string, int>& GetSpecialTokenIds() const noexcept;

    [[nodiscard]]
    const std::string& GetTokenizerType() const noexcept;

    [[nodiscard]]
    const std::string& GetNormalizerName() const noexcept;

    [[nodiscard]]
    std::string SerializeModel() const;

    [[nodiscard]]
    std::string SerializeVocab() const;

private:
    struct Impl;

    [[nodiscard]]
    static SharedTokenizer LoadLegacyTextModel(const std::string& serializedModel);

private:
    [[nodiscard]]
    static std::string EncodeHex(const std::string& value);

    [[nodiscard]]
    static std::string DecodeHex(const std::string& value);

    [[nodiscard]]
    static std::string EscapeVisible(const std::string& value);

private:
    std::vector<TokenDefinition> tokens;
    std::map<std::string, int> specialTokenIds;
    std::string serializedModel;
    std::string tokenizerType = "byte_bpe_v1";
    std::string normalizerName = "english_project_v1";
    std::shared_ptr<Impl> impl;
};
}

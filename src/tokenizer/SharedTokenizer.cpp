#include "tokenizer/SharedTokenizer.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "sentencepiece_processor.h"

namespace Mina::Tokenizer
{
namespace
{
std::string ReplaceAll(std::string value, const std::string& needle, const std::string& replacement)
{
    std::size_t position = value.find(needle);
    while (position != std::string::npos)
    {
        value.replace(position, needle.size(), replacement);
        position = value.find(needle, position + replacement.size());
    }

    return value;
}

std::vector<std::string> SplitTabLine(const std::string& line)
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

int HexValue(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f')
    {
        return 10 + (character - 'a');
    }

    if (character >= 'A' && character <= 'F')
    {
        return 10 + (character - 'A');
    }

    throw std::runtime_error("Invalid hex digit in tokenizer model.");
}

void CheckSentencePieceStatus(const sentencepiece::util::Status& status, const std::string& context)
{
    if (!status.ok())
    {
        throw std::runtime_error(context + ": " + status.ToString());
    }
}

std::map<std::string, int> BuildSpecialTokenIds(const sentencepiece::SentencePieceProcessor& processor)
{
    std::map<std::string, int> specialTokenIds;

    if (processor.pad_id() >= 0)
    {
        specialTokenIds["<pad>"] = processor.pad_id();
    }

    if (processor.bos_id() >= 0)
    {
        specialTokenIds["<bos>"] = processor.bos_id();
    }

    if (processor.eos_id() >= 0)
    {
        specialTokenIds["<eos>"] = processor.eos_id();
    }

    if (processor.unk_id() >= 0)
    {
        specialTokenIds["<unk>"] = processor.unk_id();
    }

    return specialTokenIds;
}

std::vector<TokenDefinition> BuildTokenDefinitions(const sentencepiece::SentencePieceProcessor& processor)
{
    std::vector<TokenDefinition> tokens;
    tokens.reserve(static_cast<std::size_t>(processor.GetPieceSize()));

    for (int id = 0; id < processor.GetPieceSize(); ++id)
    {
        TokenDefinition token;
        token.id = id;
        token.piece = processor.IdToPiece(id);
        token.special =
            processor.IsControl(id) || processor.IsUnknown(id)
            || id == processor.pad_id() || id == processor.bos_id() || id == processor.eos_id();
        token.label = token.special ? token.piece : ("piece_" + std::to_string(id));
        if (token.special)
        {
            token.piece.clear();
        }
        tokens.push_back(std::move(token));
    }

    return tokens;
}
}

std::string NormalizeForTokenizer(const std::string& input)
{
    std::string normalized = input;

    if (normalized.size() >= 3
        && static_cast<unsigned char>(normalized[0]) == 0xEF
        && static_cast<unsigned char>(normalized[1]) == 0xBB
        && static_cast<unsigned char>(normalized[2]) == 0xBF)
    {
        normalized = normalized.substr(3);
    }

    normalized = ReplaceAll(normalized, "\r\n", "\n");
    normalized = ReplaceAll(normalized, "\r", "\n");
    normalized = ReplaceAll(normalized, "\t", " ");
    normalized = ReplaceAll(normalized, "\xE2\x80\x98", "'");
    normalized = ReplaceAll(normalized, "\xE2\x80\x99", "'");
    normalized = ReplaceAll(normalized, "\xE2\x80\x9C", "\"");
    normalized = ReplaceAll(normalized, "\xE2\x80\x9D", "\"");
    normalized = ReplaceAll(normalized, "\xE2\x80\x93", "-");
    normalized = ReplaceAll(normalized, "\xE2\x80\x94", " - ");
    normalized = ReplaceAll(normalized, "\xE2\x80\xA6", "...");

    std::string collapsed;
    collapsed.reserve(normalized.size());

    bool previousSpace = false;
    bool previousNewline = false;

    for (char character : normalized)
    {
        if (character == '\n')
        {
            while (!collapsed.empty() && collapsed.back() == ' ')
            {
                collapsed.pop_back();
            }

            if (!previousNewline)
            {
                collapsed.push_back('\n');
            }

            previousSpace = false;
            previousNewline = true;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            if (!previousSpace && !previousNewline)
            {
                collapsed.push_back(' ');
                previousSpace = true;
            }

            continue;
        }

        collapsed.push_back(character);
        previousSpace = false;
        previousNewline = false;
    }

    while (!collapsed.empty() && (collapsed.back() == ' ' || collapsed.back() == '\n'))
    {
        collapsed.pop_back();
    }

    std::size_t first = 0;
    while (first < collapsed.size() && (collapsed[first] == ' ' || collapsed[first] == '\n'))
    {
        ++first;
    }

    return collapsed.substr(first);
}

struct SharedTokenizer::Impl
{
    sentencepiece::SentencePieceProcessor processor;
};

SharedTokenizer::SharedTokenizer(
    const std::string& serializedModelIn,
    const std::vector<TokenDefinition>& tokensIn,
    const std::map<std::string, int>& specialTokenIdsIn,
    const std::string& tokenizerTypeIn,
    const std::string& normalizerNameIn)
    : tokens(tokensIn),
      specialTokenIds(specialTokenIdsIn),
      serializedModel(serializedModelIn),
      tokenizerType(tokenizerTypeIn),
      normalizerName(normalizerNameIn),
      impl(std::make_shared<Impl>())
{
    CheckSentencePieceStatus(
        impl->processor.LoadFromSerializedProto(serializedModel),
        "Unable to load serialized SentencePiece model");
}

SharedTokenizer SharedTokenizer::LoadFromFile(const std::filesystem::path& modelPath)
{
    std::ifstream stream(modelPath, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to open tokenizer model: " + modelPath.string());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return LoadFromString(buffer.str());
}

SharedTokenizer SharedTokenizer::LoadFromString(const std::string& serializedModel)
{
    if (serializedModel.rfind("MODEL_VERSION\t", 0) == 0)
    {
        return LoadLegacyTextModel(serializedModel);
    }

    auto impl = std::make_shared<Impl>();
    CheckSentencePieceStatus(
        impl->processor.LoadFromSerializedProto(serializedModel),
        "Unable to load serialized SentencePiece model");

    SharedTokenizer tokenizer;
    tokenizer.serializedModel = serializedModel;
    tokenizer.tokenizerType = "sentencepiece_bpe_v1";
    tokenizer.normalizerName = "english_project_v1";
    tokenizer.specialTokenIds = BuildSpecialTokenIds(impl->processor);
    tokenizer.tokens = BuildTokenDefinitions(impl->processor);
    tokenizer.impl = std::move(impl);
    return tokenizer;
}

std::vector<int> SharedTokenizer::Encode(const std::string& input) const
{
    if (!impl)
    {
        throw std::runtime_error("Tokenizer model is not loaded.");
    }

    std::vector<int> tokenIds;
    CheckSentencePieceStatus(
        impl->processor.Encode(NormalizeForTokenizer(input), &tokenIds),
        "Tokenizer encode failed");
    return tokenIds;
}

std::string SharedTokenizer::Decode(const std::vector<int>& tokenIds) const
{
    if (!impl)
    {
        throw std::runtime_error("Tokenizer model is not loaded.");
    }

    std::string decoded;
    CheckSentencePieceStatus(
        impl->processor.Decode(tokenIds, &decoded),
        "Tokenizer decode failed");
    return decoded;
}

std::vector<std::string> SharedTokenizer::DescribeTokens(const std::vector<int>& tokenIds) const
{
    if (!impl)
    {
        throw std::runtime_error("Tokenizer model is not loaded.");
    }

    std::vector<std::string> descriptions;
    descriptions.reserve(tokenIds.size());

    for (int tokenId : tokenIds)
    {
        if (tokenId < 0 || tokenId >= impl->processor.GetPieceSize())
        {
            throw std::runtime_error("Unknown token id during token description.");
        }

        const std::string& piece = impl->processor.IdToPiece(tokenId);
        const bool special =
            impl->processor.IsControl(tokenId) || impl->processor.IsUnknown(tokenId)
            || tokenId == impl->processor.pad_id() || tokenId == impl->processor.bos_id()
            || tokenId == impl->processor.eos_id();
        descriptions.push_back(special ? piece : EscapeVisible(piece));
    }

    return descriptions;
}

std::size_t SharedTokenizer::GetVocabSize() const noexcept
{
    return tokens.size();
}

const std::vector<TokenDefinition>& SharedTokenizer::GetTokens() const noexcept
{
    return tokens;
}

const std::map<std::string, int>& SharedTokenizer::GetSpecialTokenIds() const noexcept
{
    return specialTokenIds;
}

const std::string& SharedTokenizer::GetTokenizerType() const noexcept
{
    return tokenizerType;
}

const std::string& SharedTokenizer::GetNormalizerName() const noexcept
{
    return normalizerName;
}

std::string SharedTokenizer::SerializeModel() const
{
    return serializedModel;
}

std::string SharedTokenizer::SerializeVocab() const
{
    std::ostringstream stream;
    stream << "id\tkind\tlabel\tpiece_hex\tvisible_piece\n";

    for (const TokenDefinition& token : tokens)
    {
        const std::string visiblePiece = token.special ? token.label : EscapeVisible(token.piece);
        stream << token.id
               << "\t" << (token.special ? "special" : "piece")
               << "\t" << token.label
               << "\t" << (token.special ? std::string() : EncodeHex(token.piece))
               << "\t" << visiblePiece
               << "\n";
    }

    return stream.str();
}

SharedTokenizer SharedTokenizer::LoadLegacyTextModel(const std::string& serializedLegacyModel)
{
    std::istringstream stream(serializedLegacyModel);
    std::string line;
    std::string tokenizerType = "byte_bpe_v1";
    std::string normalizerName = "english_project_v1";
    std::vector<TokenDefinition> legacyTokens;
    std::map<std::string, int> legacySpecialTokenIds;

    while (std::getline(stream, line))
    {
        if (line.empty())
        {
            continue;
        }

        if (line.rfind("TOKENIZER_TYPE\t", 0) == 0)
        {
            tokenizerType = line.substr(std::string("TOKENIZER_TYPE\t").size());
            continue;
        }

        if (line.rfind("NORMALIZER\t", 0) == 0)
        {
            normalizerName = line.substr(std::string("NORMALIZER\t").size());
            continue;
        }

        if (line.rfind("SPECIAL\t", 0) == 0)
        {
            const std::vector<std::string> fields = SplitTabLine(line);
            if (fields.size() != 3)
            {
                throw std::runtime_error("Malformed SPECIAL line in tokenizer model.");
            }

            legacySpecialTokenIds[fields[1]] = std::stoi(fields[2]);
            continue;
        }

        if (line.rfind("TOKEN\t", 0) == 0)
        {
            const std::vector<std::string> fields = SplitTabLine(line);
            if (fields.size() != 5)
            {
                throw std::runtime_error("Malformed TOKEN line in tokenizer model.");
            }

            TokenDefinition token;
            token.id = std::stoi(fields[1]);
            token.special = fields[2] == "special";
            token.label = fields[3];
            token.piece = token.special ? std::string() : DecodeHex(fields[4]);
            legacyTokens.push_back(std::move(token));
        }
    }

    throw std::runtime_error(
        "Legacy byte_bpe_v1 tokenizer models are no longer executable by the scalable tokenizer runtime. "
        "Retrain the shared tokenizer to produce a SentencePiece-backed model.");
}

std::string SharedTokenizer::EncodeHex(const std::string& value)
{
    static const char HEX[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);

    for (unsigned char byte : value)
    {
        encoded.push_back(HEX[(byte >> 4) & 0x0F]);
        encoded.push_back(HEX[byte & 0x0F]);
    }

    return encoded;
}

std::string SharedTokenizer::DecodeHex(const std::string& value)
{
    if ((value.size() % 2) != 0)
    {
        throw std::runtime_error("Malformed tokenizer hex payload.");
    }

    std::string decoded;
    decoded.reserve(value.size() / 2);

    for (std::size_t index = 0; index < value.size(); index += 2)
    {
        const int high = HexValue(value[index]);
        const int low = HexValue(value[index + 1]);
        decoded.push_back(static_cast<char>((high << 4) | low));
    }

    return decoded;
}

std::string SharedTokenizer::EscapeVisible(const std::string& value)
{
    std::string escaped = ReplaceAll(value, "\xE2\x96\x81", "[ws]");
    std::ostringstream stream;

    for (unsigned char character : escaped)
    {
        if (character == '\n')
        {
            stream << "\\n";
            continue;
        }

        if (character == '\r')
        {
            stream << "\\r";
            continue;
        }

        if (character == '\t')
        {
            stream << "\\t";
            continue;
        }

        if (character >= 0x20 && character != 0x7F)
        {
            stream << static_cast<char>(character);
            continue;
        }

        stream << "\\x";
        static const char HEX[] = "0123456789abcdef";
        stream << HEX[(character >> 4) & 0x0F] << HEX[character & 0x0F];
    }

    return stream.str();
}
}

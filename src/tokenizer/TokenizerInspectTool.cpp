#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "tokenizer/SharedTokenizer.h"

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
        << "Usage: tokenizer_inspect_tool [--model-path <path>] [--text <value>] [--text-file <path>]\n"
        << "                            [--token-file <path>] [--token-count <count>]\n"
        << "Defaults:\n"
        << "  model-path: C:\\Tokenizer\\manifests\\tokenizer\\shared_tokenizer.model\n";
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to read text file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::vector<int> ReadTokenFile(const std::filesystem::path& path, const std::size_t maxTokenCount)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        throw std::runtime_error("Unable to read token file: " + path.string());
    }

    std::vector<int> tokenIds;
    std::uint16_t tokenId = 0;
    while (stream.read(reinterpret_cast<char*>(&tokenId), sizeof(tokenId)))
    {
        tokenIds.push_back(static_cast<int>(tokenId));
        if (maxTokenCount > 0 && tokenIds.size() >= maxTokenCount)
        {
            break;
        }
    }

    return tokenIds;
}
}

int main(int argc, char** argv)
{
    try
    {
        ConfigureConsoleUtf8();

        std::filesystem::path modelPath = std::filesystem::path("C:\\Tokenizer\\manifests\\tokenizer\\shared_tokenizer.model");
        std::filesystem::path tokenFilePath;
        std::string text = "The available inputs do not provide enough evidence, so the shell should preserve uncertainty.";
        std::size_t tokenCount = 0;
        bool decodeTokenFile = false;

        if (argc > 1 && argv[1][0] != '-')
        {
            modelPath = std::filesystem::path(argv[1]);
            if (argc > 2)
            {
                text = argv[2];
            }
        }
        else
        {
            for (int index = 1; index < argc; ++index)
            {
                const std::string argument = argv[index];
                if ((argument == "--model-path" || argument == "--model") && index + 1 < argc)
                {
                    modelPath = std::filesystem::path(argv[++index]);
                }
                else if (argument == "--text" && index + 1 < argc)
                {
                    text = argv[++index];
                }
                else if (argument == "--text-file" && index + 1 < argc)
                {
                    text = ReadTextFile(std::filesystem::path(argv[++index]));
                }
                else if (argument == "--token-file" && index + 1 < argc)
                {
                    tokenFilePath = std::filesystem::path(argv[++index]);
                    decodeTokenFile = true;
                }
                else if (argument == "--token-count" && index + 1 < argc)
                {
                    tokenCount = static_cast<std::size_t>(std::stoull(argv[++index]));
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

        const Mina::Tokenizer::SharedTokenizer tokenizer = Mina::Tokenizer::SharedTokenizer::LoadFromFile(modelPath);
        const std::vector<int> tokenIds =
            decodeTokenFile
                ? ReadTokenFile(tokenFilePath, tokenCount)
                : tokenizer.Encode(text);
        const std::vector<std::string> tokenPieces = tokenizer.DescribeTokens(tokenIds);

        if (!decodeTokenFile)
        {
            std::cout << "normalized: " << Mina::Tokenizer::NormalizeForTokenizer(text) << '\n';
        }
        std::cout << "decoded: " << tokenizer.Decode(tokenIds) << '\n';
        std::cout << "vocab_size: " << tokenizer.GetVocabSize() << '\n';
        std::cout << "special_tokens:";

        for (const std::pair<const std::string, int>& entry : tokenizer.GetSpecialTokenIds())
        {
            std::cout << ' ' << entry.first << '=' << entry.second;
        }

        std::cout << '\n';
        std::cout << "token_ids:";
        for (int tokenId : tokenIds)
        {
            std::cout << ' ' << tokenId;
        }

        std::cout << '\n';
        std::cout << "token_pieces:";
        for (const std::string& piece : tokenPieces)
        {
            std::cout << ' ' << piece;
        }

        std::cout << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "tokenizer_inspect_tool failed: " << exception.what() << '\n';
        return 1;
    }
}

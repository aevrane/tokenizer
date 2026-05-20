# Shared Tokenizer

A standalone C++ tokenizer project for reusable shell-model tokenization.

This repository provides:

- Parquet-to-text corpus ingestion in C++
- shard and supplemental corpus handling
- reusable corpus scan/cache indexing
- shared SentencePiece BPE tokenizer training in C++
- resumable text-shard to uint16 token conversion in C++
- tokenizer runtime encode/decode/inspect support
- manifests, reports, and debugging artifacts

This project is meant to be used across multiple repositories so every consumer shares the same tokenizer contract.

For the full reference, read:

- [docs/tokenizer.md](docs/tokenizer.md)

## Status

Current project scope:

- real Arrow/Parquet ingestion
- resumable shard export
- supplemental corpus inclusion
- cached corpus indexing
- shared tokenizer training
- report and sample generation

Not yet included:

- corpus sanitation or dedup pipeline
- published license selection

## Quick Start

### 1. Install Dependencies

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\vcpkg.exe install --x-manifest-root=C:\Tokenizer --triplet x64-windows
```

### 2. Configure And Build

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S C:\Tokenizer -B C:\Tokenizer\build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:\Tokenizer\vcpkg_installed\x64-windows"
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Debug
```

### 3. Set Runtime DLL Paths In PowerShell

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

### 4. Ingest Parquet, Scan The Corpus, Train, And Inspect

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer
```

### 5. Use Release For Real Token Conversion Runs

`Debug` builds are appropriate for development, tests, and short local validation.

For real corpus conversion runs, use `Release`.

The shard-to-token pipeline is CPU-heavy and Release mode is materially faster for large corpora. In the real corpus benchmark used during development, switching from `Debug` to `Release` improved end-to-end shard conversion throughput by more than `10x`.

Build the release converter:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Release --target tokenizer_convert_tool tokenizer_inspect_tool
```

Set Release runtime DLL paths in PowerShell:

```text
$env:PATH = "C:\Tokenizer\build\Release;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

## Common Recipes

### Build Everything

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Debug
```

### Run The Test Suite

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\Tokenizer\build -C Debug --output-on-failure
```

### Ingest A New Parquet Corpus

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer --batch-size 65536 --shard-size-mb 256
```

### Resume A Parquet Ingest

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer
```

### Build Or Refresh Only The Corpus Index

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only
```

### Train From An Existing Cached Index

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
```

### Force A Rescan Before Training

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --force-rescan
```

### Train With Verbose SentencePiece Logging

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --verbose-trainer
```

### Inspect A Trained Tokenizer

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text "Source public://pmc/PMC4457059 reports that the claim is tied to the described study."
```

### Inspect A Multiline Text Sample From A File

```text
C:\Tokenizer\build\Release\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text-file C:\Temp\sample.txt
```

### Decode The First N Tokens From A Token File

```text
C:\Tokenizer\build\Release\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --token-file D:\ConvertedTokens\tokens\supplemental-conversations_with_kevin_deegan.tokens.bin --token-count 436
```

### Convert Text Shards Into Binary Token Files

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --worker-count auto --cpu-mode full
```

### Benchmark One File With Parallel Section Workers

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --parallel-mode sections --section-count 20 --worker-count auto --cpu-mode full --max-files 1 --progress-interval-seconds 60
```

### Run The Full Corpus Conversion In Release

```text
$env:PATH = "C:\Tokenizer\build\Release;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Release\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --parallel-mode sections --section-count 20 --worker-count auto --cpu-mode full --progress-interval-seconds 180
```

### Resume A Token Conversion Run

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer
```

### Inspect Token Conversion Progress And Reports

```text
Get-Content D:\ConvertedTokens\progress.json
Get-Content D:\ConvertedTokens\conversion_report.md
Get-Content D:\ConvertedTokens\token_manifest.json
Get-Content D:\ConvertedTokens\conversion.log -Tail 50 -Wait
```

### View Reports

```text
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_report.md
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_samples.md
```

### Watch Long-Running Progress

```text
Get-Content C:\Tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
```

## Repository Layout

- `include/` public headers
- `src/` implementation and CLI tools
- `tests/` local fixture and integration coverage
- `docs/` detailed documentation
- `exports/` generated corpus shards
- `manifests/` progress files, manifests, config, model outputs
- `reports/` human-readable logs and Markdown reports

## Defaults

Training defaults:

- dataset root: `C:\Datasets`
- corpus root: `C:\CorpusShards`
- output root: `C:\Tokenizer`
- default model path: `C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model`

Conversion defaults:

- input root: `D:\CorpusShards`
- output root: `D:\ConvertedTokens`
- tokenizer root: `C:\MINA\tokenizer`
- section count: `20`
- worker count: auto-detected logical processors
- recommended production build: `Release`

Parquet ingest defaults:

- parquet root: `C:\Datasets`
- output root: `C:\Tokenizer`

## Documentation

Detailed docs:

- [docs/tokenizer.md](docs/tokenizer.md)

Additional repository guidance:

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [SECURITY.md](SECURITY.md)

## Publishing Notes

This repository is close to being publishable as a standalone project, but one major non-technical choice is still intentionally unresolved:

- no open-source license has been selected yet

That choice should be made explicitly by the project owner rather than guessed in code or docs.

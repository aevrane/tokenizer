# MINA Shared Tokenizer

A standalone C++ tokenizer project for MINA shell consumers.

This repository provides:

- Parquet-to-text corpus ingestion in C++
- shard and supplemental corpus handling
- reusable corpus scan/cache indexing
- shared SentencePiece BPE tokenizer training in C++
- tokenizer runtime encode/decode/inspect support
- manifests, reports, and debugging artifacts

This project is meant to be used across multiple repos so that every shell track shares the same tokenizer contract.

For the full reference, read:

- [docs/tokenizer.md](C:/MINA/tokenizer/docs/tokenizer.md)

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
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\vcpkg.exe install --x-manifest-root=C:\MINA\tokenizer --triplet x64-windows
```

### 2. Configure And Build

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S C:\MINA\tokenizer -B C:\MINA\tokenizer\build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:\MINA\tokenizer\vcpkg_installed\x64-windows"
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\MINA\tokenizer\build --config Debug
```

### 3. Set Runtime DLL Paths In PowerShell

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

### 4. Ingest Parquet, Scan The Corpus, Train, And Inspect

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

## Common Recipes

### Build Everything

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\MINA\tokenizer\build --config Debug
```

### Run The Test Suite

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\MINA\tokenizer\build -C Debug --output-on-failure
```

### Ingest A New Parquet Corpus

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer --batch-size 65536 --shard-size-mb 256
```

### Resume A Parquet Ingest

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer
```

### Build Or Refresh Only The Corpus Index

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only
```

### Train From An Existing Cached Index

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
```

### Force A Rescan Before Training

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --force-rescan
```

### Train With Verbose SentencePiece Logging

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --verbose-trainer
```

### Inspect A Trained Tokenizer

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model --text "Source public://pmc/PMC4457059 reports that the claim is tied to the described study."
```

### View Reports

```text
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_report.md
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_samples.md
```

### Watch Long-Running Progress

```text
Get-Content C:\MINA\tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
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

Parquet ingest defaults:

- parquet root: `C:\MINA\datasets`
- output root: `C:\MINA\tokenizer`

## Documentation

Detailed docs:

- [docs/tokenizer.md](C:/MINA/tokenizer/docs/tokenizer.md)

Additional repository guidance:

- [CONTRIBUTING.md](C:/MINA/tokenizer/CONTRIBUTING.md)
- [SECURITY.md](C:/MINA/tokenizer/SECURITY.md)

## Publishing Notes

This repository is close to being publishable as a standalone project, but one major non-technical choice is still intentionally unresolved:

- no open-source license has been selected yet

That choice should be made explicitly by the project owner rather than guessed in code or docs.

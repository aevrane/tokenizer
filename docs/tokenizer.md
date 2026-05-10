# Shared Tokenizer Documentation

## Purpose

`C:\MINA\tokenizer` is a standalone shared-tokenizer project for multiple MINA shell consumers.

It provides three main capabilities:

1. Parquet corpus ingestion in C++
2. Shared tokenizer training in C++
3. Shared tokenizer runtime load/encode/decode/inspect support in C++

The intended usage pattern is:

1. ingest filtered Parquet text into tokenizer-ready shard files
2. optionally add supplemental text files beside those shards
3. scan or rescan the shard corpus into a reusable cache index
4. train one shared tokenizer from that indexed corpus plus reviewed project text
5. inspect the resulting tokenizer artifacts and reports
6. load the same model artifact in every shell track

## Project Layout

Important directories:

- `C:\MINA\tokenizer\src`
- `C:\MINA\tokenizer\include`
- `C:\MINA\tokenizer\docs`
- `C:\MINA\tokenizer\tests`
- `C:\MINA\tokenizer\exports\tokenizer\parquet_corpus`
- `C:\MINA\tokenizer\manifests\tokenizer`
- `C:\MINA\tokenizer\reports\tokenizer`

Important executables after a Debug build:

- `C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe`
- `C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe`
- `C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe`
- `C:\MINA\tokenizer\build\Debug\tokenizer_tests.exe`

## Dependencies

This project uses a local `vcpkg.json` manifest.

Required dependencies:

- `arrow[parquet]`
- `sentencepiece`

Install dependencies:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\vcpkg.exe install --x-manifest-root=C:\MINA\tokenizer --triplet x64-windows
```

## Build

Configure:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S C:\MINA\tokenizer -B C:\MINA\tokenizer\build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:\MINA\tokenizer\vcpkg_installed\x64-windows"
```

Build:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\MINA\tokenizer\build --config Debug
```

Run tests:

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\MINA\tokenizer\build -C Debug --output-on-failure
```

## Runtime DLL Setup

If you run the executables manually in PowerShell, set:

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

If you run them manually in `cmd.exe`, set:

```text
set PATH=C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;%PATH%
```

## High-Level Workflow

Typical full workflow:

1. ingest Parquet data into `.txt` shard files
2. place any guaranteed supplemental text beside the shards
3. run a scan-only corpus index pass
4. train the tokenizer from the cached index
5. inspect the resulting model and reports

Minimal real workflow example:

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

## Parquet Ingestion

### What The Ingest Tool Does

`tokenizer_parquet_ingest_tool.exe`:

- discovers Parquet files
- reads them in streaming Arrow/Parquet batches
- extracts the selected text column
- normalizes text with the shared tokenizer normalizer
- writes one-sample-per-line `.txt` corpus shards
- writes resumable progress and final reports

### Default Ingest Paths

Parquet ingester defaults:

- parquet root: `C:\MINA\datasets`
- output root: `C:\MINA\tokenizer`

Generated output paths under the output root:

- corpus shards: `exports\tokenizer\parquet_corpus`
- progress file: `manifests\tokenizer\parquet_ingest_progress.json`
- file status TSV: `manifests\tokenizer\parquet_ingest_files.tsv`
- manifest: `manifests\tokenizer\parquet_ingest_manifest.json`
- progress log: `reports\tokenizer\parquet_ingest_log.md`
- final report: `reports\tokenizer\parquet_ingest_report.md`

### Default Text Column Behavior

If `--text-column` is not set, the ingester tries these columns in order:

1. `raw_text`
2. `text`
3. `content`
4. `body_text`
5. `normalized_text`

### Ingest CLI Syntax

```text
tokenizer_parquet_ingest_tool --parquet-root <path> [--output-root <path>] [--text-column <name>] [--batch-size <rows>] [--shard-size-mb <megabytes>] [--no-resume] [--flat] [--stop-on-file-error] [--max-files <count>] [--max-batches <count>]
```

### Full Ingest Example

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer --batch-size 65536 --shard-size-mb 256
```

### Every Ingest Flag

`--parquet-root <path>`

- required
- sets the directory that contains the source Parquet files
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root D:\FilteredParquet
```

`--output-root <path>`

- changes where shards, manifests, and reports are written
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root D:\TokenizerWorkspace
```

`--text-column <name>`

- forces one specific text column instead of auto-detection
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --text-column text
```

`--batch-size <rows>`

- controls Arrow batch size in rows
- larger values usually favor throughput
- smaller values checkpoint more often and use less working memory
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --batch-size 32768
```

`--shard-size-mb <megabytes>`

- sets the target maximum shard size before the tool rotates to a new text file
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --shard-size-mb 512
```

`--no-resume`

- disables resume behavior and starts a fresh run
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --no-resume
```

`--flat`

- disables recursive directory traversal
- only the top-level Parquet directory is searched
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --flat
```

`--stop-on-file-error`

- makes the run fail immediately when a bad Parquet file is encountered
- default behavior is to skip bad files and report them
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --stop-on-file-error
```

`--max-files <count>`

- limits processing to the first `N` discovered Parquet files
- useful for smoke tests
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --max-files 2
```

`--max-batches <count>`

- limits processing to the first `N` Arrow batches
- useful for resume and output smoke tests
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --max-batches 10
```

### Stop And Resume Behavior

You can safely stop the ingester with `Ctrl+C`.

On stop:

- the current batch finishes
- the progress file remains durable
- the tool records the last committed byte offset

On resume:

- rerun the same command without `--no-resume`
- the tool truncates the current shard back to the last committed offset
- processing continues without duplicating exported text

Resume example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer
```

### How To Inspect Ingest Progress

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\parquet_ingest_progress.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
Get-Content C:\MINA\tokenizer\manifests\tokenizer\parquet_ingest_files.tsv
```

### How To Inspect Final Ingest Outputs

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\parquet_ingest_manifest.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\parquet_ingest_report.md
Get-ChildItem C:\MINA\tokenizer\exports\tokenizer\parquet_corpus
```

## Shard Files And Supplemental Files

### Shard Files

Shard files are ordinary tokenizer corpus `.txt` files, usually named:

- `shard-000000.txt`
- `shard-000001.txt`
- `shard-000002.txt`

These are treated as large corpus text and are subject to deterministic bounded sampling during tokenizer training.

### Supplemental Files

Supplemental files are corpus `.txt` files whose names begin with:

- `supplemental-`

Examples:

- `supplemental-conversations_with_kevin_deegan.txt`
- `supplemental-domain-glossary.txt`
- `supplemental-policy-language.txt`

Supplemental behavior:

- every non-empty normalized line is guaranteed inclusion
- supplemental files are not thinned by the shard sampler
- they live beside the shard files in the same corpus root

Example placement:

```text
D:\CorpusShards\shard-000000.txt
D:\CorpusShards\shard-000001.txt
D:\CorpusShards\supplemental-conversations_with_kevin_deegan.txt
```

## Tokenizer Training

### What The Training Tool Does

`tokenizer_train_tool.exe`:

1. scans or reuses a cache index of the corpus text root
2. classifies shard files and supplemental files
3. stages the reviewed project-specific export mix
4. constructs a bounded SentencePiece input file
5. trains a SentencePiece BPE tokenizer in C++
6. writes model, vocab, config, manifest, report, and sample outputs

### Default Training Paths

Training defaults:

- source repo root: required, no default
- dataset root: `C:\Datasets`
- corpus root: `C:\CorpusShards`
- output root: `C:\Tokenizer`
- corpus index path: `C:\Tokenizer\manifests\tokenizer\corpus_index.tsv`
- manifests root: `C:\Tokenizer\manifests\tokenizer`
- reports root: `C:\Tokenizer\reports\tokenizer`
- default inspect model: `C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model`

### Training Modes

There are three main ways to run the trainer:

1. `scan-only`
2. full training with named flags
3. legacy positional training

### Scan-Only Mode

This creates or refreshes the corpus cache index and exits before training.

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only
```

### Train From Existing Index

If the cached index is still valid, the trainer reuses it automatically.

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
```

### Force A Rescan Before Training

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --force-rescan
```

### Legacy Positional Form

Legacy positional form is still supported:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe C:\Sources\MINA-Codex C:\MINA\datasets 32000 C:\MINA\tokenizer D:\CorpusShards
```

This form is shorter, but the named-flag form is recommended for clarity and future publishing.

### Full Training Example

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --vocab-size 32000
```

### Every Training Flag

#### Core Paths And Size Controls

`--source-repo-root <path>`

- required for training
- points at the repo that contains the reviewed export buckets
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex
```

`--dataset-root <path>`

- records the dataset root associated with the run
- may be empty or informational if raw Parquet is not currently present on disk
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root D:\ArchivedDatasets
```

`--output-root <path>`

- changes the root used for manifests and reports unless those are overridden separately
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --output-root D:\TokenizerRun
```

`--corpus-root <path>`

- points at the shard and supplemental text directory
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --corpus-root D:\CorpusShards
```

`--index-path <path>`

- overrides the corpus cache index file location
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --corpus-root D:\CorpusShards --index-path D:\TokenizerState\corpus_index.tsv --scan-only
```

`--vocab-size <count>`

- target vocabulary size
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --vocab-size 24000
```

`--sampled-sentence-count <count>`

- cap for the bounded SentencePiece input built from the shard corpus plus guaranteed text
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --sampled-sentence-count 8000000
```

`--max-piece-length <count>`

- maximum learned SentencePiece subword length
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --max-piece-length 32
```

`--max-sentence-length <count>`

- maximum line length accepted by SentencePiece
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --max-sentence-length 20000
```

#### Reviewed Export Weights

`--parquet-weight <count>`

- weight applied to ordinary shard corpus lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --parquet-weight 1
```

`--base-training-output-weight <count>`

- weight for reviewed base-training output lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --base-training-output-weight 3
```

`--contextualization-input-weight <count>`

- weight for contextualization input lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --contextualization-input-weight 2
```

`--contextualization-output-weight <count>`

- weight for contextualization output lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --contextualization-output-weight 3
```

`--custom-verbalization-input-weight <count>`

- weight for custom verbalization input lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --custom-verbalization-input-weight 2
```

`--custom-verbalization-output-weight <count>`

- weight for custom verbalization output lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --custom-verbalization-output-weight 4
```

`--small-supervised-output-weight <count>`

- weight for small supervised output lines
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --small-supervised-output-weight 3
```

#### Progress, Logging, And Debug

`--progress-every-lines <count>`

- controls scan and sampling progress update cadence
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --progress-every-lines 500000
```

`--verbose-trainer`

- allows SentencePiece INFO logging through to the terminal
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --verbose-trainer
```

`--quiet-trainer`

- suppresses SentencePiece INFO logging
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --quiet-trainer
```

`--no-heartbeat`

- disables the once-per-minute “still running” training heartbeat during the SentencePiece phase
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --no-heartbeat
```

`--scan-only`

- scans or refreshes the corpus cache index and exits without training
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --corpus-root D:\CorpusShards --scan-only
```

`--reuse-index`

- enables use of an unchanged corpus cache index
- this is the default, but it can still be stated explicitly
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --reuse-index
```

`--no-index-reuse`

- disables cache reuse and forces a full rescan before training
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --no-index-reuse
```

`--force-rescan`

- alias for `--no-index-reuse`
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --force-rescan
```

`--print-config`

- prints the resolved training configuration before running
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --print-config --dry-run
```

`--dry-run`

- prints configuration and exits without scanning or training
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dry-run --print-config
```

#### Artifact Writing Controls

`--manifest-root <path>`

- overrides where manifest files are written
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --manifest-root D:\TokenizerArtifacts\manifests
```

`--report-root <path>`

- overrides where report files are written
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --report-root D:\TokenizerArtifacts\reports
```

`--write-report`

- explicitly enables writing the Markdown report
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --write-report
```

`--no-write-report`

- disables writing `tokenizer_report.md`
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --no-write-report
```

`--write-samples`

- explicitly enables writing the sample tokenization report
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --write-samples
```

`--no-write-samples`

- disables writing `tokenizer_samples.md`
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --no-write-samples
```

#### General

`--help` or `-h`

- prints usage
- example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --help
```

### Training Progress Files

During training, inspect:

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\tokenizer_training_progress.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
```

The progress JSON includes:

- status
- phase
- current file
- files processed
- total files
- lines scanned
- selected lines
- last update
- last error

### Training Artifacts

After a successful training run, inspect:

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.config.json
Get-Content C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.manifest.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_report.md
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_samples.md
```

Model files:

- `shared_tokenizer.model`
- `shared_tokenizer.vocab.tsv`
- `shared_tokenizer.config.json`
- `shared_tokenizer.manifest.json`

## Tokenizer Inspection

### Inspect CLI Syntax

```text
tokenizer_inspect_tool [--model-path <path>] [--text <value>]
```

### Default Inspect Behavior

Defaults:

- model path: `C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model`
- text: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty.`

### Inspect Examples

Inspect the default model:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe
```

Inspect a specific model:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model
```

Inspect a specific sentence:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

Inspect a specific model and sentence:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model --text "Source public://pmc/PMC4457059 reports that the claim is tied to the described study."
```

Show help:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --help
```

## Full End-To-End Examples

### Example 1: Fresh Beginner Run

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\MINA\tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\MINA\datasets --output-root C:\MINA\tokenizer
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root C:\MINA\tokenizer\exports\tokenizer\parquet_corpus --scan-only
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root C:\MINA\tokenizer\exports\tokenizer\parquet_corpus
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

### Example 2: Stable Corpus On Another Drive

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
```

### Example 3: Debug A Training Configuration Without Running It

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --sampled-sentence-count 8000000 --verbose-trainer --print-config --dry-run
```

### Example 4: Force A Rescan Then Train Quietly

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --force-rescan --quiet-trainer
```

## Troubleshooting

### `libprotobuf-lite.dll` Or `abseil_dll.dll` Was Not Found

Cause:

- the executable is running without the vcpkg DLL directories on `PATH`

Fix in PowerShell:

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

Fix in `cmd.exe`:

```text
set PATH=C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;%PATH%
```

### `cmake` Or `ctest` Is Not Recognized

Cause:

- those tools are not on the current shell `PATH`

Fix:

- run the full executable paths shown in this document
- or add your Visual Studio CMake directory to `PATH`

### PowerShell Syntax Was Run In `cmd.exe`

Symptom:

- commands like `$env:PATH = ...` fail with syntax errors

Fix:

- use PowerShell for `$env:PATH = ...`
- use `set PATH=...` in `cmd.exe`

### `No tokenizer corpus text files were found`

Cause:

- the `--corpus-root` directory is wrong or empty
- the directory contains no `.txt` files

Fix:

```text
Get-ChildItem D:\CorpusShards
Get-ChildItem C:\MINA\tokenizer\exports\tokenizer\parquet_corpus
```

Then rerun with the correct `--corpus-root`.

### The Scan Takes Too Long

Cause:

- first-time scan must read every shard line to build the cache index

Fix:

- run `--scan-only` once
- reuse the cached index on later training runs
- only force a rescan after the corpus changes

### The Trainer Keeps Rescanning Instead Of Reusing The Index

Cause:

- corpus files changed in size or timestamp
- different corpus root or index path was used
- `--no-index-reuse` or `--force-rescan` was set

Fix:

- keep the corpus root stable between runs
- keep the same index path
- avoid mutating shard files between repeated experiments

### Inspect Tool Shows An Old Or Wrong Model

Cause:

- the default model path points somewhere else
- you trained to a custom `--manifest-root`

Fix:

- pass `--model-path` explicitly

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path D:\TokenizerArtifacts\manifests\shared_tokenizer.model --text "Test sentence."
```

### Training Output Looks Quiet Or Stuck

Cause:

- quiet SentencePiece logging is enabled
- progress is being written to files instead of constantly spamming the console

Fix:

- use `--verbose-trainer` if you want SentencePiece internals
- inspect:

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\tokenizer_training_progress.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
```

### Parquet Ingest Appears Idle

Cause:

- the tool writes durable progress to files rather than printing every row group to the console

Fix:

```text
Get-Content C:\MINA\tokenizer\manifests\tokenizer\parquet_ingest_progress.json
Get-Content C:\MINA\tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
```

### The Corpus Changed After Indexing

If shard or supplemental files changed:

1. rerun `--scan-only --force-rescan`
2. then rerun training

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards --scan-only --force-rescan
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --dataset-root C:\MINA\datasets --output-root C:\MINA\tokenizer --corpus-root D:\CorpusShards
```

### Long Lines Are Skipped During SentencePiece Training

Cause:

- a line exceeded `--max-sentence-length`

Fix:

- increase `--max-sentence-length`

Example:

```text
C:\MINA\tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\Sources\MINA-Codex --max-sentence-length 20000
```

## Testing Coverage

The tokenizer test suite covers:

- deterministic fixture training
- encode/decode round-trip
- artifact determinism
- vocab size and special token correctness
- manifest and report generation
- supplemental corpus always-included behavior
- corpus index creation
- corpus index reuse
- corpus index invalidation after shard mutation
- Parquet ingestion with resume and bad-file handling

Run:

```text
$env:PATH = "C:\MINA\tokenizer\build\Debug;C:\MINA\tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\MINA\tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\MINA\tokenizer\build -C Debug --output-on-failure
```

## Runtime Consumption

All shell tracks should load the same tokenizer artifact through `Mina::Tokenizer::SharedTokenizer`.

Recommended runtime model artifact:

- `C:\MINA\tokenizer\manifests\tokenizer\shared_tokenizer.model`

Do not train shell-specific tokenizers if the project goal is one shared tokenizer contract.

# Shared Tokenizer Documentation

## Purpose

`C:\Tokenizer` is a standalone shared-tokenizer project.

It provides three main capabilities:

1. Parquet corpus ingestion in C++
2. Shared tokenizer training in C++
3. Shared tokenizer runtime load/encode/decode/inspect support in C++
4. Resumable text-shard to uint16 token conversion in C++

The intended usage pattern is:

1. ingest filtered Parquet text into tokenizer-ready shard files
2. optionally add supplemental text files beside those shards
3. scan or rescan the shard corpus into a reusable cache index
4. train one shared tokenizer from that indexed corpus plus reviewed project text
5. inspect the resulting tokenizer artifacts and reports
6. load the same model artifact in every consumer

## Project Layout

Important directories:

- `C:\Tokenizer\src`
- `C:\Tokenizer\include`
- `C:\Tokenizer\docs`
- `C:\Tokenizer\tests`
- `C:\Tokenizer\exports\tokenizer\parquet_corpus`
- `C:\Tokenizer\manifests\tokenizer`
- `C:\Tokenizer\reports\tokenizer`

Important executables after a Debug build:

- `C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe`
- `C:\Tokenizer\build\Debug\tokenizer_train_tool.exe`
- `C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe`
- `C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe`
- `C:\Tokenizer\build\Debug\tokenizer_tests.exe`

Important executables after a Release build:

- `C:\Tokenizer\build\Release\tokenizer_convert_tool.exe`
- `C:\Tokenizer\build\Release\tokenizer_inspect_tool.exe`

## Dependencies

This project uses a local `vcpkg.json` manifest.

Required dependencies:

- `arrow[parquet]`
- `sentencepiece`

Install dependencies:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\vcpkg.exe install --x-manifest-root=C:\Tokenizer --triplet x64-windows
```

## Build

Configure:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S C:\Tokenizer -B C:\Tokenizer\build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:\Tokenizer\vcpkg_installed\x64-windows"
```

Build:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Debug
```

Release build for production corpus conversion:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Release --target tokenizer_convert_tool tokenizer_inspect_tool
```

Run tests:

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\Tokenizer\build -C Debug --output-on-failure
```

## Runtime DLL Setup

If you run the executables manually in PowerShell, set:

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

If you run them manually in `cmd.exe`, set:

```text
set PATH=C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;%PATH%
```

For Release builds:

```text
$env:PATH = "C:\Tokenizer\build\Release;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
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
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer
```

## Text Shard Conversion

### What The Conversion Tool Does

`tokenizer_convert_tool.exe`:

- discovers eligible `.txt` and `.text` shard files in deterministic lexical order
- loads the already-trained shared tokenizer from `C:\MINA\tokenizer`
- encodes each input file into a matching raw `uint16` little-endian token stream
- writes one binary token file per input text file under `D:\ConvertedTokens\tokens`
- writes temp files first and only renames them into place after a full-file conversion succeeds
- records durable progress, a file-order manifest, a per-file manifest, and a Markdown report
- supports safe stop and resume at file boundaries

This tool is specifically for preparing already-ingested plain-text shard files for downstream language-model training.

It does **not**:

- retrain the tokenizer
- change tokenizer vocabulary or normalization rules
- attempt mid-file resume from a token offset
- write final output files before the full source file succeeds

### Primary Use Cases

Use the conversion tool when:

1. you already have tokenizer-ready text shard files
2. you want deterministic binary token files for training
3. you need stop-and-resume safety for long multi-file runs
4. you want one binary output file per source text shard
5. you need durable manifests and reports for later training jobs

Typical examples:

- convert `D:\CorpusShards` into `D:\ConvertedTokens` for a transformer training job
- convert only the first `1` or `5` files as a smoke test before launching the full corpus run
- stop a long run with `Ctrl+C`, inspect unknown-token behavior, then resume
- verify that token file sizes and token counts look plausible before training begins
- decode a token-file prefix back into normalized text for inspection and spot verification

### Input Assumptions

The converter expects ordinary text shard files, typically one logical training record per line.

Important assumption:

- the current sectioned conversion path divides a file by **line count**
- each section is tokenized separately
- the resulting token stream is equivalent to tokenizing the file as newline-delimited records

That is appropriate for corpus shard files produced by the project ingestion flow, where line boundaries are already meaningful record boundaries.

### Output Format

Each `.tokens.bin` file is a raw sequence of little-endian `uint16` token ids:

- no header
- no footer
- no per-record framing
- exactly `2` bytes per token

If a file contains `N` tokens, its output size is:

```text
2 * N bytes
```

This is why binary token files are often materially smaller than the original text files.

### Deterministic Mapping Rules

The converter preserves a deterministic mapping from input file to output file:

- input discovery is lexical by relative path
- output file paths mirror the input tree under `tokens\`
- `.txt` and `.text` files are converted to `.tokens.bin`

Examples:

```text
D:\CorpusShards\shard-000000.txt
-> D:\ConvertedTokens\tokens\shard-000000.tokens.bin

D:\CorpusShards\nested\batch-01.txt
-> D:\ConvertedTokens\tokens\nested\batch-01.tokens.bin
```

### Default Conversion Paths

- input root: `D:\CorpusShards`
- output root: `D:\ConvertedTokens`
- tokenizer root: `C:\MINA\tokenizer`

### Conversion Output Layout

Under the output root:

- token files: `tokens\**\*.tokens.bin`
- durable progress: `progress.json`
- discovered ordering manifest: `ordered_files.tsv`
- durable per-file status log: `file_records.tsv`
- machine-readable token manifest: `token_manifest.json`
- human-readable report: `conversion_report.md`
- appended runtime log: `conversion.log`

### Conversion CLI Syntax

```text
tokenizer_convert_tool --input-root <path> [--output-root <path>] [--tokenizer-root <path>] [--progress-interval-seconds <n>] [--section-count <count>] [--worker-count <auto|n>] [--cpu-mode <full|half>] [--parallel-mode <files|sections>] [--max-files <count>] [--no-recursive] [--no-resume] [--stop-on-failure]
```

### Real Conversion Example

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer
```

### Resume A Conversion Run

Run the same command again. The converter resumes from the next not-yet-completed file and deletes any stale temp or partial output for the current in-progress file before rewriting it.

### Every Conversion Flag

`--input-root <path>`

- sets the source text shard root
- default: `D:\CorpusShards`
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root E:\ShardExports
```

`--output-root <path>`

- sets the token output workspace
- default: `D:\ConvertedTokens`
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root E:\TokenRuns\run-01
```

`--tokenizer-root <path>`

- points to the shared tokenizer repo/artifact root
- used to load:
  - `manifests\tokenizer\shared_tokenizer.model`
  - `manifests\tokenizer\shared_tokenizer.manifest.json`
- default: `C:\MINA\tokenizer`

`--progress-interval-seconds <n>`

- controls time-based terminal heartbeat frequency
- default: `240`
- useful values:
  - `60` for once per minute
  - `180` for every 3 minutes
  - `300` for every 5 minutes

`--section-count <count>`

- divides each file into approximately this many line-based tokenization sections
- default: `20`
- higher values:
  - more frequent intra-file progress
  - smaller tokenization batches
  - somewhat more overhead
- lower values:
  - less frequent intra-file progress
  - larger tokenization batches
- potentially better throughput on some corpora

`--worker-count <auto|n>`

- controls how many independent files may be converted in parallel
- default: `auto`
- when `auto` is used, the converter derives the worker count from the local logical processor count
- each worker loads its own tokenizer instance and processes separate files

Examples:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --worker-count auto
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --worker-count 4
```

`--cpu-mode <full|half>`

- only applies when `--worker-count auto` is in effect
- `full`
  - use all detected logical processors
- `half`
  - use about half of the detected logical processors, rounded up
- useful when you want the converter to stay responsive while leaving CPU headroom for other work

`--parallel-mode <files|sections>`

- selects how parallel workers are applied
- `files`
  - default mode
  - up to `N` different shard files are processed at the same time
  - best for overall corpus throughput
- `sections`
  - one file is processed at a time
  - workers cooperate on different sections of that file
  - best when you want faster single-file completion and cleaner file-boundary stopping points
  - this became the preferred production mode for large-corpus conversion after benchmarking on the real shard set

`--max-files <count>`

- process only the first `N` discovered files, then stop cleanly
- useful for:
  - smoke tests
  - size sanity checks
  - overnight batching
  - cautious staged rollout

Examples:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --max-files 1
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --max-files 5
```

`--no-recursive`

- disables recursive discovery
- only the top-level input directory is scanned

`--no-resume`

- ignore any existing progress file
- start a fresh run
- use this only when you intentionally want to discard prior run state

`--stop-on-failure`

- stop immediately when one source file fails
- default behavior is to record the error and continue to later files

### Sectioned Conversion Behavior

The current converter no longer tokenizes an entire large shard in one monolithic call.

Instead, for each file it now:

1. performs a single read pass over the source text
2. normalizes line content during that read pass
3. stores one contiguous normalized text buffer plus line start offsets
4. divides the file into roughly `N` sections by line count, where `N` is `--section-count`
5. lets section workers slice their assigned section text from the shared normalized buffer
6. appends ordered section output to the temp `.tokens.bin.tmp` file
7. renames the temp file to the final `.tokens.bin` only after all sections complete

This gives:

- real mid-file progress
- lower peak memory pressure than full-file one-shot tokenization
- lower allocator churn than storing one normalized string per line
- better operator visibility on very large shard files

Across the corpus, the converter can run in two parallel layouts:

- `--parallel-mode files`
  - each worker claims the next pending file in deterministic discovery order
  - workers do not share output files
  - output naming remains deterministic
- `--parallel-mode sections`
  - a single file is partitioned into line-based sections
  - section workers tokenize different sections of that one file in parallel
  - the temp token file is still written in proper section order before final rename

In both modes:

- resume still works at file boundaries only
- any file left partially processed is restarted from the beginning on resume

### Conversion Performance Notes

The shard conversion pipeline is highly sensitive to build configuration.

Observed production behavior:

- `Debug` builds are suitable for validation, tests, and short smoke runs
- `Release` builds are the correct mode for real corpus conversion
- on the live corpus used during development, moving the section-mode converter from `Debug` to `Release` improved throughput by more than `10x`

Operational recommendation:

1. use `Debug` for correctness work
2. use `Release` for long production conversion runs
3. avoid changing converter code once a stable Release build is ready for the full corpus pass

### Terminal Progress Semantics

You should now expect progress lines that include fields like:

- `file 1/303`
- `current="w1:shard-000000.txt | w2:shard-000001.txt"`
- `phase=counting_lines|tokenizing|writing|parallel`
- `workers=2/4`
- `section=1/10`
  - with the new default this is usually `1/20`
- `file_bytes=26217304/267791632`
- `file_bytes_pct=9.79%`
- `lines=136758`
- `file_tokens=0`
- `output_bytes=0`
- `tokens_written=0`
- `file_elapsed=3m 0s`
- `elapsed=3m 0s`

Interpretation:

- `phase=counting_lines`
  - it is making the initial line pass
- `phase=tokenizing`
  - it is currently tokenizing one line-based section
- `phase=writing`
  - it already has token ids for the section and is appending them to the temp output file
- `phase=parallel`
  - more than one worker is active, and `current` is a short active-file summary
- `section=3/10`
  - it is working on the third section of the current file
- `workers=2/4`
  - two workers are currently active out of four configured workers
- `file_tokens`
  - cumulative tokens produced for the current file so far
- `tokens_written`
  - cumulative tokens durably completed across fully finished source files

In `--parallel-mode sections`, the active worker summary may show the same filename more than once because multiple workers are cooperating on different sections of that one file.

Important note:

- `tokens_written` is intentionally conservative
- it advances only after a whole source file is complete
- section-level writes go to the temp file, not the final file

### Durable Progress File Semantics

`progress.json` is the main durable checkpoint file.

Important fields:

- `status`
- `tokenizer_id`
- `tokenizer_version`
- `tokenizer_model_hash`
- `current_file_relative_path`
- `current_file_phase`
- `current_section_index`
- `current_section_count`
- `current_file_bytes_processed`
- `current_file_total_bytes`
- `current_file_lines_processed`
- `current_file_tokens_produced`
- `current_file_output_bytes_written`
- `configured_worker_count`
- `active_worker_count`
- `completed_file_count`
- `failed_file_count`
- `total_tokens_written`

This file is intended for operator inspection first and automation second.

Important implementation detail:

- `progress.json` is no longer rewritten on every non-durable heartbeat during `reading_sections`
- terminal output can still update every configured interval without forcing a durable progress-file write
- durable progress is still refreshed on meaningful boundaries and forced events

This change was added specifically to avoid Windows rename contention on `progress.json.tmp -> progress.json` during long runs.

### Stop And Resume Guarantees

You can safely stop the converter with `Ctrl+C`.

Guaranteed behavior:

- only fully completed source files count as completed
- if interruption happens during file `102`, resume restarts file `102` from the beginning
- any stale temp or partial output for pending files is deleted before reprocessing
- final `.tokens.bin` files represent only fully completed source files

Resume intentionally does **not**:

- restart from a partial token offset
- trust a partially written temp file
- try to recover partially completed section output from the previous run

This is a deliberate safety choice.

### Inspect Conversion Progress

```text
Get-Content D:\ConvertedTokens\progress.json
Get-Content D:\ConvertedTokens\conversion_report.md
Get-Content D:\ConvertedTokens\token_manifest.json
Get-Content D:\ConvertedTokens\conversion.log -Tail 50 -Wait
```

Additional useful checks:

```text
Get-Process tokenizer_convert_tool | Select-Object Id,CPU,WorkingSet64,StartTime
Get-ChildItem D:\ConvertedTokens\tokens -Recurse
Get-Content D:\ConvertedTokens\file_records.tsv
```

### Interpreting Common Situations

#### Situation: Progress lines repeat with the same section for several minutes

If `phase=tokenizing` and the same `section=X/Y` line repeats:

- the converter is inside the tokenizer for that section
- CPU time should continue increasing
- the temp output file may not grow until tokenization finishes and the write phase begins

This is normal for large sections.

#### Situation: `file_bytes_pct=100.00%` but `file_tokens=0`

That means:

- reading is complete
- the current tokenization section is still running
- no section output has been written yet

This is normal during a long tokenization section.

#### Situation: Temp output file exists but final output file does not

That means:

- the converter is still working on the current source file
- or it was interrupted mid-file

On resume, the stale temp file will be discarded and the current source file will restart from the beginning.

#### Situation: Token file size is much smaller than the original text

That is usually expected.

Reason:

- the token file stores `uint16` ids, so each token is `2` bytes
- English-like text commonly averages more than `2` text bytes per token

#### Situation: CPU is climbing but counters look static

Check whether:

- `phase=tokenizing`
- `CPU` time keeps increasing in `Get-Process`

If yes, the tokenizer is active even if section-level counters have not advanced yet.

### Troubleshooting

#### Real conversion is much slower than benchmarks

Check whether you are accidentally running `Debug`.

The converter is substantially faster in `Release`.

Recommended production command pattern:

```text
$env:PATH = "C:\Tokenizer\build\Release;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Release\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --parallel-mode sections --section-count 20 --worker-count auto --cpu-mode full --progress-interval-seconds 180
```

#### `reading_sections` takes far too long

Likely causes:

- running a `Debug` build on a very large shard
- too-frequent progress bookkeeping in an older build
- stale executable still being launched after a rebuild

Expected healthy behavior in the optimized implementation:

- `reading_sections` should move quickly relative to full-file tokenization
- Release mode should be dramatically faster than Debug mode

If read progress still appears pathological:

1. confirm the executable path is the current `Release` binary
2. confirm the runtime `PATH` points at `build\Release`
3. inspect `progress.json` and `conversion.log`

#### `progress.json.tmp` rename fails with access denied on Windows

This was an observed real-world failure mode during development.

Current mitigations:

- retry loop on metadata-file temp-to-final rename
- fewer durable `progress.json` writes during non-durable heartbeat updates

Operational advice:

- avoid keeping `progress.json` open in editors or preview panes during the run
- avoid aggressive tail/watch tooling on `progress.json`
- prefer watching terminal progress or `conversion.log` for live activity

#### The converter starts but no files complete for a long time

Check:

1. `Get-Content D:\ConvertedTokens\progress.json`
2. `Get-Process tokenizer_convert_tool | Select-Object Id,CPU,WorkingSet64,StartTime`

If `CPU` is increasing and `current_file_phase=tokenizing`, the current section is still being processed.

Actions:

- lower `--section-count` only if you want fewer, larger sections
- raise `--section-count` if you want smaller tokenization batches and more visibility
- raise `--worker-count` or use `--cpu-mode full` if the machine has spare CPU capacity
- use `--cpu-mode half` if you want to leave headroom for other applications
- use `--parallel-mode sections` if you want to finish individual files faster instead of maximizing whole-corpus throughput
- use `--max-files 1` to validate behavior on one shard first

#### The process exits immediately

Common causes:

- runtime DLL path not set
- wrong `--tokenizer-root`
- missing shared tokenizer model or manifest

Fix:

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

Then rerun the converter.

#### Resume refuses to continue and reports metadata mismatch

Possible causes:

- changed tokenizer model
- changed tokenizer manifest version/id
- changed input root or output root
- changed discovered input ordering

Fix:

- if you want to continue the original run, restore the original tokenizer and input tree
- if you intentionally changed the setup, start a fresh run with `--no-resume`

#### Unknown-token counts are unexpectedly high

Inspect:

- terminal warnings
- `conversion_report.md`
- `token_manifest.json`

Unexpected unknown-token spikes may indicate:

- data outside the expected corpus profile
- normalization mismatches
- broken shard content
- a tokenizer artifact mismatch

#### Memory usage is higher than expected

The current converter is section-based rather than full-file based, so peak memory should be lower than the earlier monolithic implementation.

If memory is still too high:

- increase `--section-count`
- lower `--worker-count`
- use `--cpu-mode half`
- prefer `--parallel-mode files` over `--parallel-mode sections`
- reduce concurrent system load
- run fewer files for validation with `--max-files`

### Recommended Operating Patterns

For a first smoke test:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --max-files 1 --progress-interval-seconds 60
```

For a single-file speed benchmark:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --parallel-mode sections --section-count 20 --worker-count auto --cpu-mode full --max-files 1 --progress-interval-seconds 60
```

For a cautious batch:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --max-files 5 --section-count 20 --progress-interval-seconds 180 --cpu-mode half
```

For a full run:

```text
C:\Tokenizer\build\Debug\tokenizer_convert_tool.exe --input-root D:\CorpusShards --output-root D:\ConvertedTokens --tokenizer-root C:\MINA\tokenizer --section-count 20 --progress-interval-seconds 180 --worker-count auto --cpu-mode full
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

- parquet root: `C:\Datasets`
- output root: `C:\Tokenizer`

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
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer --batch-size 65536 --shard-size-mb 256
```

### Every Ingest Flag

`--parquet-root <path>`

- required
- sets the directory that contains the source Parquet files
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root D:\FilteredParquet
```

`--output-root <path>`

- changes where shards, manifests, and reports are written
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root D:\TokenizerWorkspace
```

`--text-column <name>`

- forces one specific text column instead of auto-detection
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --text-column text
```

`--batch-size <rows>`

- controls Arrow batch size in rows
- larger values usually favor throughput
- smaller values checkpoint more often and use less working memory
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --batch-size 32768
```

`--shard-size-mb <megabytes>`

- sets the target maximum shard size before the tool rotates to a new text file
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --shard-size-mb 512
```

`--no-resume`

- disables resume behavior and starts a fresh run
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --no-resume
```

`--flat`

- disables recursive directory traversal
- only the top-level Parquet directory is searched
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --flat
```

`--stop-on-file-error`

- makes the run fail immediately when a bad Parquet file is encountered
- default behavior is to skip bad files and report them
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --stop-on-file-error
```

`--max-files <count>`

- limits processing to the first `N` discovered Parquet files
- useful for smoke tests
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --max-files 2
```

`--max-batches <count>`

- limits processing to the first `N` Arrow batches
- useful for resume and output smoke tests
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --max-batches 10
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
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer
```

### How To Inspect Ingest Progress

```text
Get-Content C:\Tokenizer\manifests\tokenizer\parquet_ingest_progress.json
Get-Content C:\Tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
Get-Content C:\Tokenizer\manifests\tokenizer\parquet_ingest_files.tsv
```

### How To Inspect Final Ingest Outputs

```text
Get-Content C:\Tokenizer\manifests\tokenizer\parquet_ingest_manifest.json
Get-Content C:\Tokenizer\reports\tokenizer\parquet_ingest_report.md
Get-ChildItem C:\Tokenizer\exports\tokenizer\parquet_corpus
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

- `supplemental-conversations.txt`
- `supplemental-domain-glossary.txt`
- `supplemental-policy-language.txt`

Supplemental behavior:

- every non-empty normalized line is guaranteed inclusion
- supplemental files are not thinned by the shard sampler
- they live beside the shard files in the same corpus root

Example placement:

```text
C:\CorpusShards\shard-000000.txt
C:\CorpusShards\shard-000001.txt
C:\CorpusShards\supplemental-conversations.txt
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
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only
```

### Train From Existing Index

If the cached index is still valid, the trainer reuses it automatically.

Example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
```

### Force A Rescan Before Training

Example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --force-rescan
```

### Legacy Positional Form

Legacy positional form is still supported:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe C:\SourceRepo C:\Datasets 32000 C:\Tokenizer C:\CorpusShards
```

This form is shorter, but the named-flag form is recommended for clarity and future publishing.

### Full Training Example

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --vocab-size 32000
```

### Every Training Flag

#### Core Paths And Size Controls

`--source-repo-root <path>`

- required for training
- points at the repo that contains the reviewed export buckets
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo
```

`--dataset-root <path>`

- records the dataset root associated with the run
- may be empty or informational if raw Parquet is not currently present on disk
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root D:\ArchivedDatasets
```

`--output-root <path>`

- changes the root used for manifests and reports unless those are overridden separately
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --output-root D:\TokenizerRun
```

`--corpus-root <path>`

- points at the shard and supplemental text directory
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --corpus-root C:\CorpusShards
```

`--index-path <path>`

- overrides the corpus cache index file location
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --corpus-root C:\CorpusShards --index-path D:\TokenizerState\corpus_index.tsv --scan-only
```

`--vocab-size <count>`

- target vocabulary size
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --vocab-size 24000
```

`--sampled-sentence-count <count>`

- cap for the bounded SentencePiece input built from the shard corpus plus guaranteed text
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --sampled-sentence-count 8000000
```

`--max-piece-length <count>`

- maximum learned SentencePiece subword length
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --max-piece-length 32
```

`--max-sentence-length <count>`

- maximum line length accepted by SentencePiece
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --max-sentence-length 20000
```

#### Reviewed Export Weights

`--parquet-weight <count>`

- weight applied to ordinary shard corpus lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --parquet-weight 1
```

`--base-training-output-weight <count>`

- weight for reviewed base-training output lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --base-training-output-weight 3
```

`--contextualization-input-weight <count>`

- weight for contextualization input lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --contextualization-input-weight 2
```

`--contextualization-output-weight <count>`

- weight for contextualization output lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --contextualization-output-weight 3
```

`--custom-verbalization-input-weight <count>`

- weight for custom verbalization input lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --custom-verbalization-input-weight 2
```

`--custom-verbalization-output-weight <count>`

- weight for custom verbalization output lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --custom-verbalization-output-weight 4
```

`--small-supervised-output-weight <count>`

- weight for small supervised output lines
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --small-supervised-output-weight 3
```

#### Progress, Logging, And Debug

`--progress-every-lines <count>`

- controls scan and sampling progress update cadence
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --progress-every-lines 500000
```

`--verbose-trainer`

- allows SentencePiece INFO logging through to the terminal
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --verbose-trainer
```

`--quiet-trainer`

- suppresses SentencePiece INFO logging
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --quiet-trainer
```

`--no-heartbeat`

- disables the once-per-minute "still running" training heartbeat during the SentencePiece phase
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --no-heartbeat
```

`--scan-only`

- scans or refreshes the corpus cache index and exits without training
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --corpus-root C:\CorpusShards --scan-only
```

`--reuse-index`

- enables use of an unchanged corpus cache index
- this is the default, but it can still be stated explicitly
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --reuse-index
```

`--no-index-reuse`

- disables cache reuse and forces a full rescan before training
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --no-index-reuse
```

`--force-rescan`

- alias for `--no-index-reuse`
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --force-rescan
```

`--print-config`

- prints the resolved training configuration before running
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --print-config --dry-run
```

`--dry-run`

- prints configuration and exits without scanning or training
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dry-run --print-config
```

#### Artifact Writing Controls

`--manifest-root <path>`

- overrides where manifest files are written
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --manifest-root D:\TokenizerArtifacts\manifests
```

`--report-root <path>`

- overrides where report files are written
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --report-root D:\TokenizerArtifacts\reports
```

`--write-report`

- explicitly enables writing the Markdown report
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --write-report
```

`--no-write-report`

- disables writing `tokenizer_report.md`
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --no-write-report
```

`--write-samples`

- explicitly enables writing the sample tokenization report
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --write-samples
```

`--no-write-samples`

- disables writing `tokenizer_samples.md`
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --no-write-samples
```

#### General

`--help` or `-h`

- prints usage
- example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --help
```

### Training Progress Files

During training, inspect:

```text
Get-Content C:\Tokenizer\manifests\tokenizer\tokenizer_training_progress.json
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
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
Get-Content C:\Tokenizer\manifests\tokenizer\shared_tokenizer.config.json
Get-Content C:\Tokenizer\manifests\tokenizer\shared_tokenizer.manifest.json
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_report.md
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_samples.md
```

Model files:

- `shared_tokenizer.model`
- `shared_tokenizer.vocab.tsv`
- `shared_tokenizer.config.json`
- `shared_tokenizer.manifest.json`

## Tokenizer Inspection

### Inspect CLI Syntax

```text
tokenizer_inspect_tool [--model-path <path>] [--text <value>] [--text-file <path>] [--token-file <path>] [--token-count <count>]
```

### Default Inspect Behavior

Defaults:

- model path: `C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model`
- text: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty.`

### Inspect Examples

Inspect the default model:

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe
```

Inspect a specific model:

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model
```

Inspect a specific sentence:

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

Inspect a specific model and sentence:

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text "Source public://pmc/PMC4457059 reports that the claim is tied to the described study."
```

Inspect multiline text from a file:

```text
C:\Tokenizer\build\Release\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text-file C:\Temp\sample.txt
```

Decode the first `N` tokens from a binary token file:

```text
C:\Tokenizer\build\Release\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --token-file D:\ConvertedTokens\tokens\supplemental-conversations_with_kevin_deegan.tokens.bin --token-count 436
```

### Inspect Tool Use Cases

The inspect tool is now useful for:

- ordinary text encode/decode inspection
- multiline text-file inspection without fragile shell quoting
- decoding a prefix of a real `.tokens.bin` file back into normalized text
- visual spot-checking of converted training inputs against their source text

Show help:

```text
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --help
```

## Full End-To-End Examples

### Example 1: Fresh Beginner Run

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_parquet_ingest_tool.exe --parquet-root C:\Datasets --output-root C:\Tokenizer
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\Tokenizer\exports\tokenizer\parquet_corpus --scan-only
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\Tokenizer\exports\tokenizer\parquet_corpus
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model --text "The available inputs do not provide enough evidence, so the shell should preserve uncertainty."
```

### Example 2: Stable Corpus On Another Drive

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
```

### Example 3: Debug A Training Configuration Without Running It

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --sampled-sentence-count 8000000 --verbose-trainer --print-config --dry-run
```

### Example 4: Force A Rescan Then Train Quietly

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --force-rescan --quiet-trainer
```

## Troubleshooting

### `libprotobuf-lite.dll` Or `abseil_dll.dll` Was Not Found

Cause:

- the executable is running without the vcpkg DLL directories on `PATH`

Fix in PowerShell:

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
```

Fix in `cmd.exe`:

```text
set PATH=C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;%PATH%
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
Get-ChildItem C:\CorpusShards
Get-ChildItem C:\Tokenizer\exports\tokenizer\parquet_corpus
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
C:\Tokenizer\build\Debug\tokenizer_inspect_tool.exe --model-path D:\TokenizerArtifacts\manifests\shared_tokenizer.model --text "Test sentence."
```

### Training Output Looks Quiet Or Stuck

Cause:

- quiet SentencePiece logging is enabled
- progress is being written to files instead of constantly spamming the console

Fix:

- use `--verbose-trainer` if you want SentencePiece internals
- inspect:

```text
Get-Content C:\Tokenizer\manifests\tokenizer\tokenizer_training_progress.json
Get-Content C:\Tokenizer\reports\tokenizer\tokenizer_training_log.md -Tail 50 -Wait
```

### Parquet Ingest Appears Idle

Cause:

- the tool writes durable progress to files rather than printing every row group to the console

Fix:

```text
Get-Content C:\Tokenizer\manifests\tokenizer\parquet_ingest_progress.json
Get-Content C:\Tokenizer\reports\tokenizer\parquet_ingest_log.md -Tail 50 -Wait
```

### The Corpus Changed After Indexing

If shard or supplemental files changed:

1. rerun `--scan-only --force-rescan`
2. then rerun training

Example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards --scan-only --force-rescan
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --dataset-root C:\Datasets --output-root C:\Tokenizer --corpus-root C:\CorpusShards
```

### Long Lines Are Skipped During SentencePiece Training

Cause:

- a line exceeded `--max-sentence-length`

Fix:

- increase `--max-sentence-length`

Example:

```text
C:\Tokenizer\build\Debug\tokenizer_train_tool.exe --source-repo-root C:\SourceRepo --max-sentence-length 20000
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
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\Tokenizer\build -C Debug --output-on-failure
```

## Runtime Consumption

All consumers should load the same tokenizer artifact through the shared tokenizer runtime class.

Recommended runtime model artifact:

- `C:\Tokenizer\manifests\tokenizer\shared_tokenizer.model`

Do not train consumer-specific tokenizers if the project goal is one shared tokenizer contract.

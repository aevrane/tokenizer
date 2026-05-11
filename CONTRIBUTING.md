# Contributing

## Scope

This repository is for the shared tokenizer pipeline. Keep changes focused on:

- Parquet ingestion
- corpus preparation
- tokenizer training
- tokenizer runtime support
- manifests, reports, and inspection tooling

Do not mix unrelated shell, runtime, or model-training changes into this repository.

## Development Workflow

1. install dependencies with the local `vcpkg.json`
2. configure and build the project
3. run the tokenizer test suite
4. make the smallest focused change
5. update docs if the CLI, defaults, artifacts, or workflow changed

## Build

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe -S C:\Tokenizer -B C:\Tokenizer\build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DCMAKE_PREFIX_PATH="C:\Tokenizer\vcpkg_installed\x64-windows"
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe --build C:\Tokenizer\build --config Debug
```

## Test

```text
$env:PATH = "C:\Tokenizer\build\Debug;C:\Tokenizer\vcpkg_installed\x64-windows\debug\bin;C:\Tokenizer\vcpkg_installed\x64-windows\bin;" + $env:PATH
C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir C:\Tokenizer\build -C Debug --output-on-failure
```

## Change Expectations

- prefer explicit C++ over hidden scripting
- preserve reproducibility and auditability
- keep shard vs supplemental behavior explicit
- keep training and ingestion artifacts inspectable
- document new flags and workflows
- do not silently widen contracts

## Documentation Expectations

If you change:

- CLI flags
- default paths
- output artifact names
- progress files
- cache-index behavior
- corpus inclusion rules

then update:

- `README.md`
- `docs/tokenizer.md`

## License Note

This repository does not yet declare a public license. Do not assume a license on behalf of the project owner.

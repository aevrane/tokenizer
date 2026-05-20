cmake_minimum_required(VERSION 3.20)

project(MinaSharedTokenizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Arrow CONFIG REQUIRED)
find_package(Parquet CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(MINA_ARROW_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Arrow::arrow_static,Arrow::arrow_shared>")
set(MINA_PARQUET_TARGET "$<IF:$<BOOL:${ARROW_BUILD_STATIC}>,Parquet::parquet_static,Parquet::parquet_shared>")
set(MINA_VCPKG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/include")
set(MINA_SENTENCEPIECE_RELEASE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/sentencepiece.lib")
set(MINA_SENTENCEPIECE_RELEASE_TRAIN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/lib/sentencepiece_train.lib")
set(MINA_SENTENCEPIECE_DEBUG_LIB "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/debug/lib/sentencepiece.lib")
set(MINA_SENTENCEPIECE_DEBUG_TRAIN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/debug/lib/sentencepiece_train.lib")

if (NOT EXISTS "${MINA_SENTENCEPIECE_RELEASE_LIB}" OR NOT EXISTS "${MINA_SENTENCEPIECE_RELEASE_TRAIN_LIB}" OR
    NOT EXISTS "${MINA_SENTENCEPIECE_DEBUG_LIB}" OR NOT EXISTS "${MINA_SENTENCEPIECE_DEBUG_TRAIN_LIB}")
    message(FATAL_ERROR "SentencePiece libraries were not found under the local vcpkg installation.")
endif()

add_library(mina_shared_tokenizer
    src/tokenizer/ParquetCorpusIngestion.cpp
    src/tokenizer/SharedTokenizer.cpp
    src/tokenizer/TokenConversionPipeline.cpp
    src/tokenizer/TokenizerPipeline.cpp
)

target_include_directories(mina_shared_tokenizer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MINA_VCPKG_INCLUDE_DIR}
)
target_link_libraries(mina_shared_tokenizer
    PUBLIC
        ${MINA_ARROW_TARGET}
        ${MINA_PARQUET_TARGET}
        debug ${MINA_SENTENCEPIECE_DEBUG_LIB}
        optimized ${MINA_SENTENCEPIECE_RELEASE_LIB}
        debug ${MINA_SENTENCEPIECE_DEBUG_TRAIN_LIB}
        optimized ${MINA_SENTENCEPIECE_RELEASE_TRAIN_LIB}
        protobuf::libprotobuf-lite
        absl::strings
        absl::flags
        absl::flags_parse
        absl::log
        absl::check
        Threads::Threads
)

add_executable(tokenizer_train_tool
    src/tokenizer/TokenizerTrainTool.cpp
)
target_link_libraries(tokenizer_train_tool
    PRIVATE
        mina_shared_tokenizer
)
if (MSVC)
    target_compile_options(tokenizer_train_tool PRIVATE /FS)
endif()

add_executable(tokenizer_inspect_tool
    src/tokenizer/TokenizerInspectTool.cpp
)
target_link_libraries(tokenizer_inspect_tool
    PRIVATE
        mina_shared_tokenizer
)

add_executable(tokenizer_parquet_ingest_tool
    src/tokenizer/TokenizerParquetIngestTool.cpp
)
target_link_libraries(tokenizer_parquet_ingest_tool
    PRIVATE
        mina_shared_tokenizer
)

add_executable(tokenizer_convert_tool
    src/tokenizer/TokenizerConvertTool.cpp
)
target_link_libraries(tokenizer_convert_tool
    PRIVATE
        mina_shared_tokenizer
)

add_executable(tokenizer_tests
    tests/tokenizer/TokenizerTests.cpp
)
target_link_libraries(tokenizer_tests
    PRIVATE
        mina_shared_tokenizer
)

add_executable(token_conversion_tests
    tests/tokenizer/TokenConversionTests.cpp
)
target_link_libraries(token_conversion_tests
    PRIVATE
        mina_shared_tokenizer
)

enable_testing()

add_test(
    NAME tokenizer_tests
    COMMAND
        ${CMAKE_COMMAND} -E env
            "PATH=${CMAKE_CURRENT_BINARY_DIR}/Debug;${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/debug/bin;${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/bin;$ENV{PATH}"
            $<TARGET_FILE:tokenizer_tests>
)

add_test(
    NAME token_conversion_tests
    COMMAND
        ${CMAKE_COMMAND} -E env
            "PATH=${CMAKE_CURRENT_BINARY_DIR}/Debug;${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/debug/bin;${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows/bin;$ENV{PATH}"
            $<TARGET_FILE:token_conversion_tests>
)

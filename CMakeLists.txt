cmake_minimum_required(VERSION 3.25)
project(ome VERSION 0.1.0 DESCRIPTION "Limit order book / matching engine" LANGUAGES CXX)

option(OME_BUILD_TESTS "Build the test suite" ON)
option(OME_BUILD_BENCH "Build the benchmarks (fetches Google Benchmark)" ON)
option(OME_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(OME_FUZZ "Link the fuzz targets against libFuzzer (Clang only)" OFF)
option(OME_CLANG_TIDY "Run clang-tidy on the project's own sources while compiling" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy / editors

# MinGW executables otherwise depend on libstdc++/libgcc DLLs being on PATH, and pick up
# incompatible copies (e.g. Git for Windows') when they aren't. The sanitizer runtimes on
# MinGW are DLLs only, so sanitizer builds stay dynamic.
if(MINGW AND NOT OME_SANITIZE)
  add_link_options(-static)
endif()

# WebAssembly: C++ exceptions (invalid arguments from JS) must reach JS as errors rather
# than abort, so every translation unit uses native wasm exception handling.
if(EMSCRIPTEN)
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
endif()

if(OME_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "OME_SANITIZE is only supported with GCC/Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# Coverage instrumentation must cover the code under test (the library), not just the fuzz
# targets, so it is applied globally. OME_FUZZ_LINK is used by tests/CMakeLists.txt.
if(OME_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "OME_FUZZ needs Clang (libFuzzer)")
  endif()
  if(MINGW)
    # Clang rejects -fsanitize=fuzzer for the MinGW target, but llvm-mingw ships the
    # libFuzzer runtime, so instrument and link it by hand.
    add_compile_options(-fsanitize-coverage=inline-8bit-counters,pc-table,trace-cmp)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-runtime-dir
                    OUTPUT_VARIABLE OME_CLANG_RT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(OME_FUZZ_LINK "${OME_CLANG_RT_DIR}/libclang_rt.fuzzer-x86_64.a")
  else()
    add_compile_options(-fsanitize=fuzzer-no-link)
    set(OME_FUZZ_LINK -fsanitize=fuzzer)
  endif()
endif()

# Warnings for our own targets only (not for fetched dependencies).
add_library(ome_warnings INTERFACE)
if(MSVC)
  target_compile_options(ome_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(ome_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

# Set before our targets are created; tests/CMakeLists.txt clears it around GoogleTest.
if(OME_CLANG_TIDY)
  find_program(OME_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY ${OME_CLANG_TIDY_EXE})
endif()

# --- Library -------------------------------------------------------------------------
add_library(ome
  src/lobster.cpp
  src/order_book.cpp
  src/text_format.cpp
  src/to_string.cpp
)
add_library(ome::ome ALIAS ome)
target_include_directories(ome PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ome PUBLIC cxx_std_20)
target_link_libraries(ome PRIVATE ome_warnings)

# --- CLI -----------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
  add_executable(ome-cli apps/cli/main.cpp)
  target_link_libraries(ome-cli PRIVATE ome ome_warnings)

  # Replays a LOBSTER (Nasdaq) message file and checks executions against the book.
  add_executable(ome-lobster apps/lobster/main.cpp)
  target_link_libraries(ome-lobster PRIVATE ome ome_warnings)
endif()

# --- WebAssembly (the `wasm` preset: Emscripten toolchain) ---------------------------
# Produces web/src/engine/wasm/ome.{js,wasm}: an ES module exporting createOme(),
# used by the web UI and the MCP server.
if(EMSCRIPTEN)
  set(OME_WASM_OUT ${CMAKE_CURRENT_SOURCE_DIR}/web/src/engine/wasm)
  add_executable(ome-wasm wasm/bindings.cpp)
  target_link_libraries(ome-wasm PRIVATE ome ome_warnings)
  set_target_properties(ome-wasm PROPERTIES
    OUTPUT_NAME ome SUFFIX .js RUNTIME_OUTPUT_DIRECTORY ${OME_WASM_OUT})
  target_link_options(ome-wasm PRIVATE
    -lembind
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createOme
    -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1
    # Lets JS read the message of a thrown C++ exception (e.g. an invalid argument).
    -sEXPORTED_RUNTIME_METHODS=getExceptionMessage,decrementExceptionRefcount)
  # TypeScript types are hand-written (wasm/ome.d.ts) because embind's generated ones type
  # every returned JS object as `any`; copy them next to ome.js.
  add_custom_command(TARGET ome-wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/wasm/ome.d.ts ${OME_WASM_OUT}/ome.d.ts)
endif()

# --- Benchmarks ----------------------------------------------------------------------
# Google Benchmark globs its own source directory, which silently finds nothing when the
# path contains glob characters (e.g. "E:/[7] Software/..."). Fail clearly instead.
if(DEFINED FETCHCONTENT_BASE_DIR)
  set(OME_DEPS_DIR "${FETCHCONTENT_BASE_DIR}")
else()
  set(OME_DEPS_DIR "${CMAKE_BINARY_DIR}/_deps")
endif()
string(FIND "${OME_DEPS_DIR}" "[" OME_BRACKET_POS)
if(OME_BUILD_BENCH AND NOT OME_BRACKET_POS EQUAL -1)
  message(FATAL_ERROR
    "Dependencies would be fetched to '${OME_DEPS_DIR}', which contains '[': Google "
    "Benchmark's build cannot handle that. Configure with "
    "-DFETCHCONTENT_BASE_DIR=<a path without brackets> or -DOME_BUILD_BENCH=OFF.")
endif()
if(OME_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# --- Tests ---------------------------------------------------------------------------
if(OME_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- Formatting ----------------------------------------------------------------------
# `cmake --build <dir> --target format` rewrites; `format-check` fails on any difference.
find_program(OME_CLANG_FORMAT_EXE NAMES clang-format)
if(OME_CLANG_FORMAT_EXE)
  # "[" in the source path would start a glob character class, so escape it.
  string(REPLACE "[" "[[]" root "${CMAKE_CURRENT_SOURCE_DIR}")
  file(GLOB_RECURSE OME_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${root}/include/*.hpp" "${root}/src/*.cpp" "${root}/apps/*.cpp" "${root}/bench/*.cpp"
    "${root}/wasm/*.cpp"
    "${root}/tests/*.cpp" "${root}/tests/*.hpp")
  add_custom_target(format COMMAND ${OME_CLANG_FORMAT_EXE} -i ${OME_FORMAT_SOURCES})
  add_custom_target(format-check
    COMMAND ${OME_CLANG_FORMAT_EXE} --dry-run --Werror ${OME_FORMAT_SOURCES})
endif()

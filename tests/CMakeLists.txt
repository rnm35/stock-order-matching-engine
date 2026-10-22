include(FetchContent)
# Git rather than a release tarball: CMake's archive extraction globs the build path, which
# breaks when the path contains glob characters (e.g. "E:/[7] Software/...").
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
  SYSTEM  # its headers are not ours to warn about
)
# Match the CRT of our targets on MSVC; don't install gtest alongside the project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# Don't run clang-tidy on GoogleTest itself.
set(OME_SAVED_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")
FetchContent_MakeAvailable(googletest)
set(CMAKE_CXX_CLANG_TIDY "${OME_SAVED_CLANG_TIDY}")
# Newer Clang warns about char8_t handling inside GoogleTest's own sources; not our code.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-character-conversion OME_HAS_WNO_CHARACTER_CONVERSION)
if(OME_HAS_WNO_CHARACTER_CONVERSION)
  target_compile_options(gtest PRIVATE -Wno-character-conversion)
  target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
endif()

# --- Test support: the naive reference book and the differential harness ----------------
add_library(ome_test_support STATIC
  support/reference_book.cpp
  support/differential.cpp
)
target_include_directories(ome_test_support PUBLIC support)
target_link_libraries(ome_test_support PUBLIC ome PRIVATE ome_warnings)

# --- Unit tests ------------------------------------------------------------------------
add_executable(ome_tests
  order_book_test.cpp
  legacy_mode_test.cpp
  text_format_test.cpp
  differential_test.cpp
  lobster_test.cpp
)
target_link_libraries(ome_tests PRIVATE ome_test_support ome_warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ome_tests DISCOVERY_MODE PRE_TEST)

# --- Differential test: OrderBook vs ReferenceBook on random streams --------------------
# The ctest run is sized to take a few seconds; run ome-difftest by hand for millions.
add_executable(ome-difftest differential/difftest_main.cpp)
target_link_libraries(ome-difftest PRIVATE ome_test_support ome_warnings)
add_test(NAME diff.random_streams
  COMMAND ome-difftest --ops 100000 --seed 1 --seeds 3 --mode both)

# --- Fuzz targets ----------------------------------------------------------------------
# With OME_FUZZ (Clang) they are real libFuzzer binaries; otherwise they get a plain
# random-input main() so they still build and run everywhere. Either way ctest runs a
# fixed number of inputs; for open-ended fuzzing run the binary directly (see README).
# Inputs per ctest run: book_fuzz runs the O(n) reference book, so it gets fewer.
set(parse_fuzz_RUNS 200000)
set(book_fuzz_RUNS 50000)
foreach(target parse_fuzz book_fuzz)
  add_executable(${target} fuzz/${target}.cpp)
  target_link_libraries(${target} PRIVATE ome_test_support ome_warnings)
  if(OME_FUZZ)
    target_link_libraries(${target} PRIVATE ${OME_FUZZ_LINK})
  else()
    target_sources(${target} PRIVATE fuzz/standalone_main.cpp)
  endif()
  # libFuzzer adds new inputs to the FIRST corpus directory, so that one is a scratch dir in
  # the build tree; the checked-in seeds are only read.
  set(scratch_corpus ${CMAKE_CURRENT_BINARY_DIR}/corpus/${target})
  file(MAKE_DIRECTORY ${scratch_corpus})
  add_test(NAME fuzz.${target}
    COMMAND ${target} -runs=${${target}_RUNS} ${scratch_corpus}
            ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/${target})
endforeach()

# --- CLI end-to-end tests ------------------------------------------------------------
# Each runs ome-cli on an input file and compares the output file with the expected one.
# See run_cli_test.cmake for the options.
set(CLI_TEST_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/run_cli_test.cmake)
set(DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)

function(ome_cli_test name)
  cmake_parse_arguments(T "" "" "ARGS" ${ARGN})
  add_test(NAME cli.${name}
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:ome-cli>
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli/${name}
      ${T_ARGS}
      -P ${CLI_TEST_SCRIPT})
endfunction()

# README example, both modes.
ome_cli_test(readme_legacy ARGS -DINPUT=${DATA}/readme_example.txt
  -DEXPECTED=${DATA}/readme_example.legacy.expected.txt)
ome_cli_test(readme_modern ARGS -DINPUT=${DATA}/readme_example.txt -DMODE=modern
  -DEXPECTED=${DATA}/readme_example.modern.expected.txt)
# Bug 1: FIFO by arrival, not by order-id string; partial fills keep their place.
ome_cli_test(time_priority ARGS -DINPUT=${DATA}/time_priority.txt
  -DEXPECTED=${DATA}/time_priority.expected.txt)
# Bug 2 is covered by every test above: stderr must be empty for a valid file.
# Bad lines are reported and skipped; good lines still trade.
ome_cli_test(invalid_lines ARGS -DINPUT=${DATA}/invalid_lines.txt
  -DEXPECTED=${DATA}/invalid_lines.expected.txt -DALLOW_STDERR=ON)
# Bug 3: input names without a digit, and the default output name.
ome_cli_test(default_output_no_digit ARGS -DINPUT=${DATA}/time_priority.txt
  -DINPUT_NAME=orders.txt -DDEFAULT_OUTPUT_NAME=output_orders.txt
  -DEXPECTED=${DATA}/time_priority.expected.txt)
ome_cli_test(default_output_input_prefix ARGS -DINPUT=${DATA}/time_priority.txt
  -DINPUT_NAME=input1.txt -DDEFAULT_OUTPUT_NAME=output1.txt
  -DEXPECTED=${DATA}/time_priority.expected.txt)
# Bug 4: a missing input file is an error, not an empty run.
ome_cli_test(missing_input ARGS -DINPUT=${DATA}/does_not_exist.txt -DEXPECT_FAIL=ON)

# Golden files: expected outputs produced by the original coursework program with bug 1
# fixed (tools/make_golden.py), so legacy mode is checked against the original itself.
# "[" in the source path would start a glob character class, so escape it.
string(REPLACE "[" "[[]" GOLDEN_GLOB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/golden")
file(GLOB GOLDEN_INPUTS CONFIGURE_DEPENDS "${GOLDEN_GLOB_DIR}/*.txt")
list(FILTER GOLDEN_INPUTS EXCLUDE REGEX "\\.expected\\.txt$")
foreach(input ${GOLDEN_INPUTS})
  get_filename_component(name ${input} NAME_WE)
  ome_cli_test(golden.${name} ARGS -DINPUT=${input}
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}.expected.txt)
endforeach()

# LOBSTER replay end to end on a hand-made message file: all three executions must be
# reproduced against the same resting orders (the comment line counts as malformed).
add_test(NAME lobster.replay_sample COMMAND ome-lobster ${DATA}/lobster_sample_message.csv)
set_tests_properties(lobster.replay_sample PROPERTIES
  PASS_REGULAR_EXPRESSION "executions reproduced exactly +3 / 3")

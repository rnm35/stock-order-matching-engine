# Runs ome-cli once and checks the result. Invoked by ctest via `cmake -P`.
#
#   CLI                  path to ome-cli (required)
#   WORK_DIR             scratch directory, recreated each run (required)
#   INPUT                input file; copied into WORK_DIR (it need not exist with EXPECT_FAIL)
#   INPUT_NAME           name to give the copy (default: INPUT's file name)
#   MODE                 passed as --mode
#   EXPECTED             file the output must equal (line endings normalised)
#   DEFAULT_OUTPUT_NAME  don't pass -o; expect the CLI to write WORK_DIR/<this name>
#   ALLOW_STDERR         don't fail if the CLI writes to stderr
#   EXPECT_FAIL          expect a non-zero exit code and nothing else

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

if(NOT INPUT_NAME)
  get_filename_component(INPUT_NAME "${INPUT}" NAME)
endif()
set(input "${WORK_DIR}/${INPUT_NAME}")
if(EXISTS "${INPUT}")
  configure_file("${INPUT}" "${input}" COPYONLY)
endif()

set(args "${input}" --quiet)
if(MODE)
  list(APPEND args --mode ${MODE})
endif()
if(DEFAULT_OUTPUT_NAME)
  set(actual "${WORK_DIR}/${DEFAULT_OUTPUT_NAME}")
else()
  set(actual "${WORK_DIR}/actual.txt")
  list(APPEND args -o "${actual}")
endif()

execute_process(COMMAND "${CLI}" ${args}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)

if(EXPECT_FAIL)
  if(rc EQUAL 0)
    message(FATAL_ERROR "expected ome-cli to fail, but it exited 0")
  endif()
  return()
endif()

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ome-cli exited ${rc}\nstderr:\n${err}")
endif()
if(NOT ALLOW_STDERR AND NOT err STREQUAL "")
  message(FATAL_ERROR "unexpected stderr output:\n${err}")
endif()
if(NOT EXISTS "${actual}")
  message(FATAL_ERROR "expected output file ${actual} was not written")
endif()

file(READ "${actual}" got)
file(READ "${EXPECTED}" want)
string(REPLACE "\r\n" "\n" got "${got}")
string(REPLACE "\r\n" "\n" want "${want}")
if(NOT got STREQUAL want)
  message(FATAL_ERROR "output mismatch\n--- expected (${EXPECTED})\n${want}--- got (${actual})\n${got}")
endif()

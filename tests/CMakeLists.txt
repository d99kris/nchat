# Tests
#
# Unit tests for utility functions with self-contained logic, built as small
# executables linking libncutil and run by ctest (via ./make.sh tests).

# Test macro add_unit_test
macro(add_unit_test testname)
  add_executable(${testname} ${testname}.cpp)

  # Headers
  target_include_directories(${testname} PRIVATE "../lib/common/src")
  target_include_directories(${testname} PRIVATE "../lib/ncutil/src")

  # Compiler flags
  set_target_properties(${testname} PROPERTIES COMPILE_FLAGS
                        "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                         -Wcast-qual -Wno-missing-braces -Wswitch-default \
                         -Wunreachable-code -Wundef -Wuninitialized \
                         -Wcast-align")

  # Linking
  target_link_libraries(${testname} PUBLIC ncutil)

  # Common
  target_common_config(${testname})

  # Test registration. The timeout ensures a hang in a tested function fails
  # the test run, rather than blocking it indefinitely.
  add_test(NAME ${testname} COMMAND ${testname})
  set_tests_properties(${testname} PROPERTIES TIMEOUT 60)
endmacro()

# Unit tests
add_unit_test(test001)

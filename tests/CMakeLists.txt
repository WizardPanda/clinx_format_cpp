set(CLXCPP_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(clxcpp-tests
  test_main.cpp
  test_core.cpp
  test_tiff.cpp
  test_png.cpp
  test_parity.cpp
)
target_link_libraries(clxcpp-tests PRIVATE clxcpp)
target_include_directories(clxcpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(clxcpp-tests PRIVATE
  CLXCPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  CLXCPP_TEST_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference"
)

add_test(NAME clxcpp-tests COMMAND clxcpp-tests)
set_tests_properties(clxcpp-tests PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

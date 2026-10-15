cmake_minimum_required(VERSION 3.20)
project(ocz_rtx_hotfix_02_policy_tests LANGUAGES CXX)

enable_testing()
add_executable(ocz_rtx_hotfix_02_policy_tests policy_tests.cpp)
target_compile_features(ocz_rtx_hotfix_02_policy_tests PRIVATE cxx_std_17)

get_filename_component(DEV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_include_directories(ocz_rtx_hotfix_02_policy_tests PRIVATE
    "${DEV_ROOT}/source/shipwright/libultraship/include")

add_test(NAME ocz_rtx_hotfix_02_policy_tests COMMAND ocz_rtx_hotfix_02_policy_tests)

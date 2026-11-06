cmake_minimum_required(VERSION 3.16)
project(firmware_adapter_tests C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(count_adapter_fixture fixture.c
    "${REPO_ROOT}/firmware/vision_count/counting_app.c"
    "${REPO_ROOT}/core/greeter.c" "${REPO_ROOT}/core/audio_queue.c")
target_include_directories(count_adapter_fixture PRIVATE
    "${REPO_ROOT}/core" "${REPO_ROOT}/firmware/vision_count" "${REPO_ROOT}/firmware/vision_check")
target_compile_options(count_adapter_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-fast-math)
target_link_libraries(count_adapter_fixture PRIVATE m)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(NOT REPLAY_LIBRARY)
    message(FATAL_ERROR "REPLAY_LIBRARY must point to the cloud host replay DLL")
endif()
enable_testing()
foreach(scenario roundtrip dwell foldback invalid capacity)
    add_test(NAME adapter_${scenario} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/verify_adapter.py"
        "$<TARGET_FILE:count_adapter_fixture>" "${REPLAY_LIBRARY}" "${scenario}")
endforeach()

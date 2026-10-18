# Host-side unit tests: build with the native compiler, not the Arm toolchain.
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.20)
project(mr2_dashboard_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(host_tests
    test_main.c
    mock_eve.c
    mock_can.c
    ${SRC}/eve/eve.c
    ${SRC}/eve/eve_panels.c
    ${SRC}/app/dashboard.c
    ${SRC}/app/demo_data.c
    ${SRC}/app/ecu_link.c
    ${SRC}/app/fixmath.c
    ${SRC}/app/guide.c
    ${SRC}/app/logo.c
    ${SRC}/app/rusefi_can.c
    ${SRC}/app/ui.c
)

target_include_directories(host_tests PRIVATE . ${SRC}/eve ${SRC}/can ${SRC}/app)
target_compile_definitions(host_tests PRIVATE _GNU_SOURCE)
target_compile_options(host_tests PRIVATE -Wall -Wextra -Wno-unused-parameter
                       -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(host_tests PRIVATE -fsanitize=address,undefined)
target_link_libraries(host_tests PRIVATE m)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)

# Dumps the dashboard's command streams for tools/render_preview.py:
#   ./build-tests/preview_dump build-tests && tools/render_preview.py build-tests/*.bin
add_executable(preview_dump
    preview_dump.c
    mock_eve.c
    ${SRC}/eve/eve.c
    ${SRC}/eve/eve_panels.c
    ${SRC}/app/dashboard.c
    ${SRC}/app/fixmath.c
    ${SRC}/app/guide.c
    ${SRC}/app/logo.c
    ${SRC}/app/ui.c
)
target_include_directories(preview_dump PRIVATE . ${SRC} ${SRC}/eve ${SRC}/app)

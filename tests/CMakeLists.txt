# Host-side unit tests for the hardware-independent firmware modules.
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.20)
project(scope_host_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../Core/App)
add_library(scope_core STATIC
  ${APP}/crc32.c ${APP}/protocol.c ${APP}/trigger.c ${APP}/acq_engine.c
  ${APP}/measure.c ${APP}/decimate.c ${APP}/calib.c ${APP}/timebase.c
  ${APP}/timebase_table.c ${APP}/gfx.c ${APP}/font5x7.c ${APP}/ui_format.c
  ${APP}/ui_menu.c ${APP}/ui_render.c ${APP}/scope_settings.c)
target_include_directories(scope_core PUBLIC ${APP})

option(SCOPE_SANITIZE "Build tests with ASan + UBSan" ON)
set(WARN -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef -Werror)
target_compile_options(scope_core PRIVATE ${WARN})
if(SCOPE_SANITIZE)
  set(SAN -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_compile_options(scope_core PUBLIC ${SAN})
  target_link_options(scope_core PUBLIC ${SAN})
endif()
target_link_libraries(scope_core PUBLIC m)

foreach(t test_crc_protocol test_trigger_acq test_measure_dsp test_ui)
  add_executable(${t} ${t}.c)
  target_link_libraries(${t} PRIVATE scope_core)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Renders firmware screens to PBM images (used for the README screenshots).
add_executable(render_preview ${CMAKE_CURRENT_SOURCE_DIR}/../tools/render_preview.c)
target_link_libraries(render_preview PRIVATE scope_core)

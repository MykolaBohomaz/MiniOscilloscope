# Command-line / CI firmware build. STM32CubeIDE users can keep using the
# .project/.cproject files; both builds compile the same sources.
#
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#   cmake --build build
cmake_minimum_required(VERSION 3.20)
project(stm32l432_scope C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

file(GLOB HAL_SOURCES  ${CMAKE_SOURCE_DIR}/Drivers/STM32L4xx_HAL_Driver/Src/*.c)
file(GLOB CUBE_SOURCES ${CMAKE_SOURCE_DIR}/Core/Src/*.c)
file(GLOB APP_SOURCES  ${CMAKE_SOURCE_DIR}/Core/App/*.c)

add_executable(scope.elf
  ${CUBE_SOURCES} ${APP_SOURCES} ${HAL_SOURCES}
  ${CMAKE_SOURCE_DIR}/Core/Startup/startup_stm32l432kcux.s)

target_include_directories(scope.elf PRIVATE
  Core/Inc Core/App
  Drivers/STM32L4xx_HAL_Driver/Inc
  Drivers/STM32L4xx_HAL_Driver/Inc/Legacy
  Drivers/CMSIS/Device/ST/STM32L4xx/Include
  Drivers/CMSIS/Include)

target_compile_definitions(scope.elf PRIVATE USE_HAL_DRIVER STM32L432xx)
target_compile_options(scope.elf PRIVATE
  ${MCU_FLAGS} -ffunction-sections -fdata-sections -fno-common
  $<$<CONFIG:Debug>:-Og -g3>
  $<$<CONFIG:Release>:-O2 -g>)

# Application code is held to a stricter standard than vendor code.
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_OPTIONS
  "-Wall;-Wextra;-Wshadow;-Wdouble-promotion;-Wundef;-Wformat=2;-Werror")

target_link_options(scope.elf PRIVATE
  ${MCU_FLAGS}
  -T${CMAKE_SOURCE_DIR}/STM32L432KCUX_FLASH.ld
  --specs=nano.specs
  -Wl,--gc-sections -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/scope.map)
target_link_libraries(scope.elf PRIVATE m)

add_custom_command(TARGET scope.elf POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   scope.elf scope.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary scope.elf scope.bin
  COMMAND ${CMAKE_SIZE} scope.elf
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

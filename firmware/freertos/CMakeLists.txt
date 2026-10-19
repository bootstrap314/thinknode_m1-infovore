cmake_minimum_required(VERSION 3.20)
project(thinknode_m1_freertos LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
include(../../cmake/common.cmake)
foreach(dep NRF5_SDK_DIR FREERTOS_DIR)
  if(NOT IS_DIRECTORY "${${dep}}")
    message(FATAL_ERROR "${dep} must point to the pinned dependency")
  endif()
endforeach()
set(SDK ${NRF5_SDK_DIR})
set(SDK_SOURCES
  modules/nrfx/mdk/gcc_startup_nrf52840.S modules/nrfx/mdk/system_nrf52840.c
  modules/nrfx/soc/nrfx_atomic.c modules/nrfx/drivers/src/nrfx_clock.c
  modules/nrfx/drivers/src/nrfx_systick.c modules/nrfx/drivers/src/nrfx_power.c modules/nrfx/drivers/src/nrfx_usbd.c
  integration/nrfx/legacy/nrf_drv_clock.c integration/nrfx/legacy/nrf_drv_power.c
  components/libraries/util/app_error.c components/libraries/util/app_error_handler_gcc.c
  components/libraries/util/app_error_weak.c components/libraries/util/app_util_platform.c
  components/libraries/util/nrf_assert.c components/libraries/atomic/nrf_atomic.c
  components/libraries/atomic_fifo/nrf_atfifo.c components/libraries/atomic_flags/nrf_atflags.c
  components/libraries/balloc/nrf_balloc.c components/libraries/memobj/nrf_memobj.c
  components/libraries/ringbuf/nrf_ringbuf.c components/libraries/strerror/nrf_strerror.c
  components/libraries/experimental_section_vars/nrf_section_iter.c
  components/libraries/crc16/crc16.c components/libraries/fds/fds.c
  components/libraries/fstorage/nrf_fstorage.c components/libraries/fstorage/nrf_fstorage_sd.c
  components/libraries/timer/app_timer_freertos.c
  components/libraries/usbd/app_usbd.c components/libraries/usbd/app_usbd_core.c
  components/libraries/usbd/app_usbd_string_desc.c
  components/libraries/usbd/class/cdc/acm/app_usbd_cdc_acm.c
  components/ble/common/ble_advdata.c components/ble/common/ble_conn_state.c
  components/ble/common/ble_srv_common.c
  components/softdevice/common/nrf_sdh.c components/softdevice/common/nrf_sdh_ble.c
  components/softdevice/common/nrf_sdh_soc.c components/softdevice/common/nrf_sdh_freertos.c
  external/freertos/portable/GCC/nrf52/port.c
  external/freertos/portable/CMSIS/nrf52/port_cmsis_systick.c
  external/utf_converter/utf.c)
list(TRANSFORM SDK_SOURCES PREPEND "${SDK}/")
file(GLOB PEERS ${SDK}/components/ble/peer_manager/*.c)
list(FILTER PEERS EXCLUDE REGEX "nrf_ble_lesc.c")
set(KERNEL_SOURCES tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c portable/MemMang/heap_4.c)
list(TRANSFORM KERNEL_SOURCES PREPEND "${FREERTOS_DIR}/")
add_executable(firmware platform.cpp usb.c port_cmsis.c ${M1_COMMON} ${RADIO_SOURCES} ${SDK_SOURCES} ${PEERS} ${KERNEL_SOURCES})
# Restrict include discovery to SDK interfaces; no SDK examples or alternate ports.
file(GLOB_RECURSE SDK_HEADERS ${SDK}/components/libraries/*.h ${SDK}/components/ble/*.h)
set(SDK_INCLUDES "")
foreach(header ${SDK_HEADERS})
  get_filename_component(dir ${header} DIRECTORY)
  list(APPEND SDK_INCLUDES ${dir})
endforeach()
list(REMOVE_DUPLICATES SDK_INCLUDES)
target_include_directories(firmware PRIVATE . ../common ../../include ${RADIOLIB_DIR}/src
  ${FREERTOS_DIR}/include ${SDK}/external/freertos/portable/GCC/nrf52
  ${SDK}/external/freertos/portable/CMSIS/nrf52 ${SDK}/components ${SDK}/components/boards
  ${SDK}/components/toolchain/cmsis/include ${SDK}/components/softdevice/common
  ${SDK}/components/softdevice/s140/headers ${SDK}/components/softdevice/s140/headers/nrf52
  ${SDK}/modules/nrfx ${SDK}/modules/nrfx/mdk ${SDK}/modules/nrfx/hal
  ${SDK}/modules/nrfx/drivers/include ${SDK}/integration/nrfx ${SDK}/integration/nrfx/legacy
  ${SDK}/external/fprintf ${SDK}/external/utf_converter ${SDK_INCLUDES})
target_compile_definitions(firmware PRIVATE NRF52840_XXAA NRF_SD_BLE_API_VERSION=6 S140
  SOFTDEVICE_PRESENT FREERTOS FLOAT_ABI_HARD __HEAP_SIZE=16384 __STACK_SIZE=4096
  M1_RTOS="freertos" M1_VERSION="${M1_VERSION}" ${RADIO_DEFINES})
set(ARCH_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
target_compile_options(firmware PRIVATE ${ARCH_FLAGS} -Os -g3 -ffunction-sections -fdata-sections
  -fno-strict-aliasing -fshort-enums -Wall -Wextra -Wno-unused-parameter
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>)
target_link_options(firmware PRIVATE ${ARCH_FLAGS} -T${CMAKE_CURRENT_SOURCE_DIR}/thinknode_m1.ld
  -L${SDK}/modules/nrfx/mdk --specs=nano.specs --specs=nosys.specs
  -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map -Wl,--print-memory-usage -u _printf_float)
target_link_libraries(firmware PRIVATE m c nosys)
set_target_properties(firmware PROPERTIES SUFFIX .elf)
add_custom_command(TARGET firmware POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:firmware> ${CMAKE_CURRENT_BINARY_DIR}/firmware.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:firmware> ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:firmware>)

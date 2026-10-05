cmake_minimum_required(VERSION 3.16)
project(drone_fc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(FC_TARGET "sim" CACHE STRING "Build target: sim | stm32 | hil")
set(FC_BUILD_TESTS "ON" CACHE STRING "Build unit tests")

add_compile_options(-Wall -Wextra -Werror-implicit-function-declaration)

# ---- Shared flight-control application + core (hardware-independent, FW-001) ----
file(GLOB_RECURSE FC_CORE_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/common/*.c
  ${CMAKE_CURRENT_LIST_DIR}/flight_controller/*.c
)

# include every subdirectory of common/ and flight_controller/ (module-style includes)
file(GLOB_RECURSE FC_INC_DIRS ${CMAKE_CURRENT_LIST_DIR}/common/* ${CMAKE_CURRENT_LIST_DIR}/flight_controller/*)
set(FC_PUBLIC_DIRS ${CMAKE_CURRENT_LIST_DIR}/common ${CMAKE_CURRENT_LIST_DIR}/flight_controller ${CMAKE_CURRENT_LIST_DIR}/hal)
foreach(_f ${FC_INC_DIRS})
    get_filename_component(_d "${_f}" DIRECTORY)
    list(APPEND FC_PUBLIC_DIRS "${_d}")
endforeach()
list(REMOVE_DUPLICATES FC_PUBLIC_DIRS)

add_library(fc_core STATIC ${FC_CORE_SOURCES})
target_include_directories(fc_core PUBLIC ${FC_PUBLIC_DIRS})

# ---- Deterministic world model (Phase 22): NO HAL symbols, shared by the SIM
#      backend and the HIL rig process so both hosts run ONE physics/sensor
#      implementation (never a second, drifting copy). ----
set(FC_SIM_MODEL_SRC ${CMAKE_CURRENT_LIST_DIR}/hal/sim/sim_model.c)
add_library(fc_sim_model STATIC ${FC_SIM_MODEL_SRC})
target_include_directories(fc_sim_model PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/hal
  ${CMAKE_CURRENT_LIST_DIR}/hal/sim
)
target_link_libraries(fc_sim_model PUBLIC fc_core)

# ---- HAL interface (header-only contract) + selected backend ----
if(FC_TARGET STREQUAL "sim")
  file(GLOB_RECURSE FC_HAL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/hal/sim/*.c)
  list(REMOVE_ITEM FC_HAL_SOURCES ${FC_SIM_MODEL_SRC})
  set(FC_ENTRY ${CMAKE_CURRENT_LIST_DIR}/hal/sim/main_sim.c)
  add_definitions(-DFC_TARGET_SIM=1)
elseif(FC_TARGET STREQUAL "stm32")
  # Hardware-gated: requires arm-none-eabi toolchain + STM32 HAL drivers (Phase 08+).
  file(GLOB_RECURSE FC_HAL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/hal/stm32/*.c)
  set(FC_ENTRY "")  # provided by hal/stm32 startup
  add_definitions(-DFC_TARGET_STM32=1)
elseif(FC_TARGET STREQUAL "hil")
  # Host-executable HIL target (Phase 22, DEC-019): the REAL application behind
  # the HAL, with the vehicle/sensor world on the far side of a framed link -
  # the same software arrangement as the STM32 rig, without the board.
  file(GLOB_RECURSE FC_HAL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/hal/hil/*.c)
  list(REMOVE_ITEM FC_HAL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/hal/hil/hil_main.c)
  set(FC_ENTRY ${CMAKE_CURRENT_LIST_DIR}/hal/hil/hil_main.c)
  add_definitions(-DFC_TARGET_HIL=1)
else()
  message(FATAL_ERROR "Unknown FC_TARGET: ${FC_TARGET}")
endif()

add_library(fc_hal STATIC ${FC_HAL_SOURCES})
target_include_directories(fc_hal PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/hal
  ${CMAKE_CURRENT_LIST_DIR}/hal/${FC_TARGET}
)
target_link_libraries(fc_hal PUBLIC fc_core)

if(FC_TARGET STREQUAL "sim")
  target_link_libraries(fc_hal PUBLIC fc_sim_model)
  add_executable(fc_sim ${FC_ENTRY})
  target_link_libraries(fc_sim PRIVATE fc_hal m)
endif()

if(FC_TARGET STREQUAL "hil")
  add_executable(fc_hil ${FC_ENTRY})
  target_link_libraries(fc_hil PRIVATE fc_hal m)
  # Host rig: the far side of the link. Links the shared world model, NOT the
  # HIL HAL backend, so no HAL symbol is shared between the two processes.
  set(HIL_RIG_DIR ${CMAKE_CURRENT_LIST_DIR}/../07_simulation/hardware_in_the_loop)
  add_executable(hil_rig ${HIL_RIG_DIR}/hil_rig.c)
  target_include_directories(hil_rig PRIVATE ${HIL_RIG_DIR})
  target_link_libraries(hil_rig PRIVATE fc_sim_model m)
  if(WIN32)
    # Winsock for the loopback link on the host rig (both endpoints)
    target_link_libraries(fc_hil PRIVATE ws2_32)
    target_link_libraries(hil_rig PRIVATE ws2_32)
  endif()
endif()

if(FC_BUILD_TESTS AND FC_TARGET STREQUAL "sim")
  enable_testing()
  add_subdirectory(tests)
endif()
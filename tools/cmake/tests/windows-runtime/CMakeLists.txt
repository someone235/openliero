cmake_minimum_required(VERSION 3.25)
project(WindowsRuntimeFixture LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Configure this fixture with the Windows cross-toolchain.")
endif()
add_library(leaf SHARED leaf.cpp)
set_target_properties(leaf PROPERTIES OUTPUT_NAME LeafRuntime DEBUG_POSTFIX d)
add_library(bridge SHARED bridge.cpp)
target_link_libraries(bridge PRIVATE leaf)
add_executable(runtime_fixture main.cpp)
target_link_libraries(runtime_fixture PRIVATE bridge)

set(OPENLIERO_INSTALL_BINDIR .)
include("${CMAKE_CURRENT_LIST_DIR}/../../InstallWindowsRuntimeDependencies.cmake")
install(TARGETS runtime_fixture DESTINATION .)
openliero_install_windows_runtime_dependencies(runtime_fixture)

enable_testing()
foreach(mode IN ITEMS complete missing conflict)
  add_test(NAME runtime_${mode}
    COMMAND "${CMAKE_COMMAND}"
      "-DMODE=${mode}" "-DBINARY=$<TARGET_FILE:runtime_fixture>"
      "-DLEAF=$<TARGET_FILE:leaf>" "-DBRIDGE=$<TARGET_FILE:bridge>"
      "-DINSPECTOR=${CMAKE_OBJDUMP}"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/checks/$<CONFIG>/${mode}"
      -P "${CMAKE_CURRENT_LIST_DIR}/check.cmake")
endforeach()

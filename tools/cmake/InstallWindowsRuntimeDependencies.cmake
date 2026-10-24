include_guard(GLOBAL)

function(openliero_install_windows_runtime_dependencies)
  set(OPENLIERO_RUNTIME_EXECUTABLES)
  set(OPENLIERO_RUNTIME_TARGET_DLLS)
  foreach(target IN LISTS ARGN)
    list(APPEND OPENLIERO_RUNTIME_EXECUTABLES "$<TARGET_FILE:${target}>")
    list(APPEND OPENLIERO_RUNTIME_TARGET_DLLS "$<TARGET_RUNTIME_DLLS:${target}>")
  endforeach()

  set(OPENLIERO_RUNTIME_COMPILER_DIRECTORIES)
  foreach(compiler IN ITEMS CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
    if(NOT ${compiler})
      continue()
    endif()
    get_filename_component(directory "${${compiler}}" DIRECTORY)
    list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES "${directory}")
    get_filename_component(real_compiler "${${compiler}}" REALPATH)
    get_filename_component(directory "${real_compiler}" DIRECTORY)
    list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES "${directory}")
  endforeach()
  list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES
    ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES} ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
  # GCC installations often keep their runtime DLLs outside the compiler's bin/.
  if(MINGW)
    foreach(directory IN LISTS OPENLIERO_RUNTIME_COMPILER_DIRECTORIES)
      # Linux-hosted LLVM-MinGW keeps PE runtimes in the target's bin/, while
      # the compiler bin/ contains Linux executables and no Windows DLLs.
      get_filename_component(target_bin
        "${directory}/../${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32/bin" ABSOLUTE)
      if(IS_DIRECTORY "${target_bin}")
        list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES "${target_bin}")
      endif()
    endforeach()
    foreach(name IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libc++.dll libunwind.dll)
      execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${name}"
        OUTPUT_VARIABLE library OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(IS_ABSOLUTE "${library}" AND EXISTS "${library}")
        get_filename_component(directory "${library}" DIRECTORY)
        list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES "${directory}")
      endif()
    endforeach()
  endif()
  if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    list(APPEND OPENLIERO_RUNTIME_COMPILER_DIRECTORIES
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
  endif()
  list(REMOVE_DUPLICATES OPENLIERO_RUNTIME_COMPILER_DIRECTORIES)

  # Discover the selected MSVC toolset's redistributables, including debug DLLs
  # for multi-config generators. The executable scan selects only DLLs it uses.
  set(OPENLIERO_RUNTIME_COMPILER_DLLS)
  if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
    foreach(library IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      if(library MATCHES "\\.[Dd][Ll][Ll]$")
        list(APPEND OPENLIERO_RUNTIME_COMPILER_DLLS "${library}")
      endif()
    endforeach()
  endif()

  set(OPENLIERO_RUNTIME_LIBRARY_DIRECTORIES "" CACHE STRING
    "Additional dependency DLL directories for Windows installation")

  # Pin the inspection tool at configure time, so installation does not depend
  # on an unrelated compiler or developer tools being present on PATH.
  if(CMAKE_OBJDUMP AND EXISTS "${CMAKE_OBJDUMP}")
    set(OPENLIERO_RUNTIME_INSPECTOR "${CMAKE_OBJDUMP}")
  else()
    find_program(OPENLIERO_RUNTIME_INSPECTOR NAMES dumpbin llvm-objdump objdump
      HINTS ${OPENLIERO_RUNTIME_COMPILER_DIRECTORIES} REQUIRED)
  endif()
  get_filename_component(inspector_name "${OPENLIERO_RUNTIME_INSPECTOR}" NAME_WE)
  if(inspector_name STREQUAL "dumpbin")
    set(OPENLIERO_RUNTIME_INSPECTOR_TOOL dumpbin)
  else()
    set(OPENLIERO_RUNTIME_INSPECTOR_TOOL objdump)
  endif()

  set(script "${CMAKE_CURRENT_BINARY_DIR}/InstallWindowsRuntimeDependencies")
  configure_file(
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/InstallWindowsRuntimeDependencies.cmake.in"
    "${script}.cmake.in" @ONLY)
  file(GENERATE OUTPUT "${script}-$<CONFIG>.cmake" INPUT "${script}.cmake.in")
  install(SCRIPT "${script}-$<CONFIG>.cmake")
endfunction()

# Chainloaded by vcpkg for both the game and its Windows dependencies.
include_guard(GLOBAL)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(CMAKE_C_COMPILER NAMES x86_64-w64-mingw32-clang x86_64-w64-mingw32-gcc
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES x86_64-w64-mingw32-clang++ x86_64-w64-mingw32-g++
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES x86_64-w64-mingw32-windres
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)
find_program(CMAKE_OBJDUMP NAMES x86_64-w64-mingw32-objdump llvm-objdump
  HINTS "$ENV{LLVM_MINGW_ROOT}/bin" REQUIRED)

# Honor vcpkg's per-triplet flags when this file configures a dependency.
foreach(language IN ITEMS C CXX)
  string(APPEND CMAKE_${language}_FLAGS_INIT " ${VCPKG_${language}_FLAGS}")
  foreach(config IN ITEMS DEBUG RELEASE)
    string(APPEND CMAKE_${language}_FLAGS_${config}_INIT " ${VCPKG_${language}_FLAGS_${config}}")
  endforeach()
endforeach()
foreach(kind IN ITEMS EXE SHARED MODULE)
  string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS}")
  foreach(config IN ITEMS DEBUG RELEASE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_${config}_INIT " ${VCPKG_LINKER_FLAGS_${config}}")
  endforeach()
endforeach()

# Target headers/libraries must never resolve to the Linux host's packages.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

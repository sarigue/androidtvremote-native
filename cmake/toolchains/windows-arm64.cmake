set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{LLVM_MINGW_ROOT} OR "$ENV{LLVM_MINGW_ROOT}" STREQUAL "")
    message(FATAL_ERROR "Windows ARM64 cross-build requires LLVM_MINGW_ROOT pointing to an llvm-mingw installation")
endif()

set(_llvm_mingw_bin "$ENV{LLVM_MINGW_ROOT}/bin")
set(CMAKE_C_COMPILER "${_llvm_mingw_bin}/aarch64-w64-mingw32-clang" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${_llvm_mingw_bin}/aarch64-w64-mingw32-clang++" CACHE FILEPATH "")
set(CMAKE_RC_COMPILER "${_llvm_mingw_bin}/aarch64-w64-mingw32-windres" CACHE FILEPATH "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

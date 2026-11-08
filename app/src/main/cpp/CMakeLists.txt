cmake_minimum_required(VERSION 3.22.1)
project(dwgviewer LANGUAGES C CXX)

set(LIBREDWG_LIBONLY ON CACHE BOOL "Build only the DWG reader" FORCE)
set(LIBREDWG_DISABLE_WRITE ON CACHE BOOL "Read-only viewer" FORCE)
set(LIBREDWG_DISABLE_JSON ON CACHE BOOL "Use compact viewer scene format" FORCE)
set(DISABLE_WERROR ON CACHE BOOL "Use the Android NDK compiler" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "Avoid cross-toolchain IPO issues" FORCE)

add_subdirectory(../../../../third_party/libredwg "${CMAKE_BINARY_DIR}/libredwg")
target_compile_definitions(redwg PRIVATE USE_DEPRECATED_API)
add_library(dwgviewer SHARED bridge.cpp)
target_include_directories(dwgviewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/libredwg/src"
    "${CMAKE_BINARY_DIR}/libredwg/src")
target_link_libraries(dwgviewer PRIVATE redwg log m)


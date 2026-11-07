# Android native part: the WC2009 HD core (repo root) + the JNI bridge
cmake_minimum_required(VERSION 3.22)
project(wc09android C CXX)

set(BUILD_QT_SDL OFF CACHE BOOL "" FORCE)
set(MELONDS_GLES ON CACHE BOOL "" FORCE)
set(BUILD_WC09_MOBILE ON CACHE BOOL "" FORCE)
set(ENABLE_GDBSTUB OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO_RELEASE OFF CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

get_filename_component(WC09_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)
add_subdirectory(${WC09_ROOT} melonds)

add_library(wc09jni SHARED jni.cpp)
target_link_libraries(wc09jni PRIVATE wc09mobile aaudio android log GLESv3 EGL)
target_compile_options(wc09jni PRIVATE -O2)

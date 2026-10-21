cmake_minimum_required(VERSION 3.20)
project(CorridorKeyAVX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{AVX2SDK_ROOT} AND NOT DEFINED AVX2SDK_ROOT)
  set(AVX2SDK_ROOT "${CMAKE_SOURCE_DIR}/../AVX2SDK" CACHE PATH "Path to Avid AVX2 SDK")
else()
  if(DEFINED ENV{AVX2SDK_ROOT})
    set(AVX2SDK_ROOT "$ENV{AVX2SDK_ROOT}" CACHE PATH "Path to Avid AVX2 SDK")
  endif()
endif()

message(STATUS "AVX2SDK_ROOT=${AVX2SDK_ROOT}")

set(AVX_INC "${AVX2SDK_ROOT}/inc")
set(AVX_UTILS "${AVX2SDK_ROOT}/utils")

if(NOT EXISTS "${AVX_INC}/avx2api.h")
  message(FATAL_ERROR "avx2api.h not found under ${AVX_INC}. Set AVX2SDK_ROOT.")
endif()

add_library(CorridorKey SHARED
  src/CorridorKeyPlugin.cpp
  src/CorridorKeyEffect.cpp
  src/CorridorKey_win.cpp
  src/IpcClient.cpp
  "${AVX_UTILS}/CACFClassFactory.cpp"
  "${AVX_UTILS}/CACFUnknown.cpp"
)

target_include_directories(CorridorKey PRIVATE
  "${AVX_INC}"
  "${AVX_UTILS}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(CorridorKey PRIVATE
  WIN32
  _WINDOWS
  UNICODE
  _UNICODE
  _USRDLL
)

target_link_libraries(CorridorKey PRIVATE
  kernel32
  user32
  advapi32
)

set_target_properties(CorridorKey PROPERTIES
  OUTPUT_NAME "CorridorKey"
  SUFFIX ".avx"
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(CorridorKey PROPERTIES
  LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/CorridorKey.def\""
)

# Copy next to build for WindTunnel / local install scripts
add_custom_command(TARGET CorridorKey POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../packaging/payload"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:CorridorKey>"
    "${CMAKE_SOURCE_DIR}/../packaging/payload/CorridorKey.avx"
)

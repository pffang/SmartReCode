cmake_minimum_required(VERSION 3.20)
project(SmartRecoder VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

include(FetchContent)
FetchContent_Declare(
  uchardet
  GIT_REPOSITORY https://gitlab.freedesktop.org/uchardet/uchardet.git
  GIT_TAG v0.0.8)
FetchContent_Declare(
  libiconv
  GIT_REPOSITORY https://github.com/pffang/libiconv-for-Windows.git
  GIT_TAG 1.19)
set(BUILD_BINARY OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(uchardet libiconv)

# libiconv-for-Windows provides an MSBuild project, not a CMake project.
add_library(libiconv STATIC
  "${libiconv_SOURCE_DIR}/libiconv/lib/iconv.c"
  "${libiconv_SOURCE_DIR}/libiconv/libcharset/lib/localcharset.c"
  "${libiconv_SOURCE_DIR}/libiconv/libcharset/lib/relocatable-stub.c")
target_include_directories(libiconv
  PUBLIC "${libiconv_SOURCE_DIR}/include"
  PRIVATE "${libiconv_SOURCE_DIR}/libiconv"
          "${libiconv_SOURCE_DIR}/libiconv/include"
          "${libiconv_SOURCE_DIR}/libiconv/libcharset/include")
target_compile_definitions(libiconv PRIVATE USING_STATIC_LIBICONV _CRT_SECURE_NO_WARNINGS)

add_library(recoder STATIC recoder.cpp)
target_link_libraries(recoder PUBLIC libuchardet libiconv)
if(MSVC)
  target_compile_options(recoder PRIVATE /utf-8)
endif()

option(SMARTRECODER_BUILD_GUI "Build the WinUI 3 application" ON)
if(SMARTRECODER_BUILD_GUI)
  if(NOT WIN32)
    message(FATAL_ERROR "The WinUI 3 application requires Windows.")
  endif()
  set(WINAPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.winapp" CACHE PATH
      "Windows App SDK files restored by winapp")
  if(NOT EXISTS "${WINAPP_ROOT}/include/winrt/Microsoft.UI.Xaml.h")
    message(FATAL_ERROR "Windows App SDK headers are missing. Run 'winapp restore' in the project directory.")
  endif()
  if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64" OR
     CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$")
    set(WINAPP_ARCH arm64)
  elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR
         CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR
         (NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
          CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$"))
    set(WINAPP_ARCH x64)
  else()
    message(FATAL_ERROR "Only x64 and ARM64 are supported by this configuration.")
  endif()
  add_executable(SmartRecoder WIN32 main.cpp app.manifest)
  target_include_directories(SmartRecoder PRIVATE "${WINAPP_ROOT}/include")
  target_link_directories(SmartRecoder PRIVATE "${WINAPP_ROOT}/lib/${WINAPP_ARCH}")
  target_link_libraries(SmartRecoder PRIVATE recoder WindowsApp OneCoreUap Microsoft.WindowsAppRuntime.Bootstrap ole32 shell32)
  if(MSVC)
    target_compile_options(SmartRecoder PRIVATE /bigobj /utf-8)
  endif()
  add_custom_command(TARGET SmartRecoder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${WINAPP_ROOT}/bin/${WINAPP_ARCH}" "$<TARGET_FILE_DIR:SmartRecoder>")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(recoder_tests tests.cpp)
  target_link_libraries(recoder_tests PRIVATE recoder)
  add_test(NAME recoder_tests COMMAND recoder_tests)
endif()

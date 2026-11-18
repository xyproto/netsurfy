cmake_minimum_required(VERSION 3.20)

project(NetSurfy VERSION 1.1.6 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows)$")
  message(FATAL_ERROR "NetSurfy supports Linux and Windows")
endif()

if(WIN32)
  if(NOT MINGW)
    message(FATAL_ERROR "The Windows build requires a MinGW-w64 toolchain")
  endif()
  enable_language(RC)
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
endforeach()
string(REPLACE "-O2" "-O3" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT netsurfy_ipo_supported OUTPUT netsurfy_ipo_error LANGUAGES C)
if(NOT netsurfy_ipo_supported)
  message(WARNING "LTO is unavailable: ${netsurfy_ipo_error}")
endif()
foreach(config RELEASE RELWITHDEBINFO)
  if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config}
      AND NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config} ${netsurfy_ipo_supported})
  endif()
endforeach()

include(GNUInstallDirs)

option(NETSURFY_USE_QUICKJS "JavaScript support (QuickJS-ng)" ON)
option(NETSURFY_FETCH_QUICKJS "Download and build QuickJS-ng if it is not installed" ON)
set(NETSURFY_QUICKJS_VERSION "0.18.0" CACHE STRING "QuickJS-ng version to build when not installed")
set(NETSURFY_QUICKJS_SHA256 "28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd" CACHE STRING "SHA256 of the QuickJS-ng source archive")
option(NETSURFY_USE_WEBP "WebP image support" ON)
option(NETSURFY_USE_JPEGXL "JPEG XL image support" OFF)
option(NETSURFY_USE_HARU_PDF "PDF export (libharu)" OFF)
option(NETSURFY_FS_BACKING_STORE "Disc cache backing store" ON)
option(NETSURFY_USE_SANITIZERS "Build with ASan and UBSan" OFF)
option(NETSURFY_RECOVER_SANITIZERS "Continue after sanitizer errors" ON)
option(NETSURFY_BUILD_HEADLESS "Headless headless frontend for automated testing" OFF)
option(BUILD_TESTING "Build the unit tests (requires check)" ON)

set(NETSURFY_LOG_LEVEL "VERBOSE" CACHE STRING "Compiled-in log level")
set(NETSURFY_BUILTIN_LOG_FILTER "(level:WARNING || cat:jserrors)" CACHE STRING "Default log filter")
set(NETSURFY_BUILTIN_VERBOSE_FILTER "(level:VERBOSE || cat:jserrors)" CACHE STRING "Verbose log filter")
if(WIN32)
  set(netsurfy_ua_format "Mozilla/5.0 (%s) NetSurfy/%d.%d")
  set(netsurfy_res_path "${CMAKE_BINARY_DIR}/src/gen/resources/:${CMAKE_INSTALL_FULL_DATADIR}/netsurfy/")
else()
  set(netsurfy_ua_format "Mozilla/5.0 (Arch Linux; %s) NetSurfy/%d.%d")
  set(netsurfy_res_path "${CMAKE_INSTALL_FULL_DATADIR}/netsurfy/:./src/frontends/gtk/res/")
endif()
set(NETSURFY_UA_FORMAT_STRING "${netsurfy_ua_format}" CACHE STRING "User agent format")
set(NETSURFY_HOMEPAGE "https://html.duckduckgo.com/" CACHE STRING "Default homepage")
set(NETSURFY_GTK_RES_PATH "${netsurfy_res_path}" CACHE STRING "Resource search path")
set(NETSURFY_HOST_C_COMPILER "" CACHE FILEPATH "C compiler for build-time tools when cross compiling")
set(NETSURFY_MESSAGES_LANGUAGES de en fr it nl zh_CN)

find_package(PkgConfig REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(EXPAT REQUIRED)
if(WIN32)
  find_package(Iconv REQUIRED)
  find_library(REGEX_LIBRARY NAMES regex REQUIRED)
endif()
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
if(NETSURFY_USE_QUICKJS)
  find_package(qjs CONFIG QUIET)
  if(NOT qjs_FOUND)
    if(NOT NETSURFY_FETCH_QUICKJS)
      message(FATAL_ERROR "QuickJS-ng not found: install it, set CMAKE_PREFIX_PATH, or enable NETSURFY_FETCH_QUICKJS")
    endif()
    message(STATUS "QuickJS-ng not found, building bundled v${NETSURFY_QUICKJS_VERSION}")
    include(FetchContent)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(QJS_ENABLE_INSTALL OFF)
    set(QJS_BUILD_EXAMPLES OFF)
    set(QJS_BUILD_LIBC OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(quickjs_exclude)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
      set(quickjs_exclude EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(quickjs
      URL https://github.com/quickjs-ng/quickjs/archive/refs/tags/v${NETSURFY_QUICKJS_VERSION}.tar.gz
      URL_HASH SHA256=${NETSURFY_QUICKJS_SHA256}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      ${quickjs_exclude})
    FetchContent_MakeAvailable(quickjs)
    unset(BUILD_SHARED_LIBS)
  endif()
endif()
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4 gthread-2.0 gmodule-export-2.0)
if(NETSURFY_USE_WEBP)
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
endif()
if(NETSURFY_USE_JPEGXL)
  pkg_check_modules(JPEGXL REQUIRED IMPORTED_TARGET libjxl)
endif()
if(NETSURFY_USE_HARU_PDF)
  find_library(HPDF_LIBRARY hpdf REQUIRED)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_definitions(STMTEXPR=1)
endif()

if(NETSURFY_USE_SANITIZERS)
  set(sanitizer_flags -fsanitize=address -fsanitize=undefined)
  if(NOT NETSURFY_RECOVER_SANITIZERS)
    list(APPEND sanitizer_flags -fno-sanitize-recover)
  endif()
  add_compile_options(${sanitizer_flags})
  add_link_options(${sanitizer_flags})
endif()

set(NETSURFY_WARNING_FLAGS
  -W -Wall -Wundef -Wpointer-arith -Wcast-align -Wwrite-strings
  -Wmissing-declarations -Wuninitialized -Wno-unused-parameter
  -Wredundant-decls -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  list(APPEND NETSURFY_WARNING_FLAGS -Wimplicit-fallthrough=5)
endif()

set(netsurfy_config_header)
foreach(setting NETSURFY_UA_FORMAT_STRING NETSURFY_HOMEPAGE
    NETSURFY_BUILTIN_LOG_FILTER NETSURFY_BUILTIN_VERBOSE_FILTER)
  string(REPLACE "\\" "\\\\" value "${${setting}}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(APPEND netsurfy_config_header "#define ${setting} \"${value}\"\n")
endforeach()
set(NETSURFY_CONFIG_HEADER ${CMAKE_BINARY_DIR}/netsurfy_config.h)
file(WRITE ${NETSURFY_CONFIG_HEADER}.tmp "${netsurfy_config_header}")
file(COPY_FILE ${NETSURFY_CONFIG_HEADER}.tmp ${NETSURFY_CONFIG_HEADER} ONLY_IF_DIFFERENT)
file(REMOVE ${NETSURFY_CONFIG_HEADER}.tmp)

set(NETSURFY_CORE_DEFINES
  WITH_JPEG
  LIBICONV_PLUG
  WITH_CURL
  WITH_OPENSSL
  WITH_UTF8PROC
  WITH_PNG
  WITH_BMP
  WITH_GIF
  WITH_NS_SVG
  WITH_NSSPRITE
  WITH_NSPSL
  WITH_NSLOG
  $<$<BOOL:${NETSURFY_USE_WEBP}>:WITH_WEBP>
  $<$<BOOL:${NETSURFY_USE_JPEGXL}>:WITH_JPEGXL>
  $<$<BOOL:${NETSURFY_USE_HARU_PDF}>:WITH_PDF_EXPORT>
  NETSURFY_LOG_LEVEL=${NETSURFY_LOG_LEVEL})

set(NETSURFY_CORE_LIBRARIES
  libcss
  libdom
  libhubbub
  libparserutils
  libwapcaplet
  libnsutils
  libnsbmp
  libnsgif
  libsvgtiny
  librosprite
  libnspsl
  libnslog
  libutf8proc
  CURL::libcurl
  OpenSSL::SSL
  OpenSSL::Crypto
  JPEG::JPEG
  PNG::PNG
  ZLIB::ZLIB
  $<$<BOOL:${NETSURFY_USE_WEBP}>:PkgConfig::WEBP>
  $<$<BOOL:${NETSURFY_USE_JPEGXL}>:PkgConfig::JPEGXL>
  $<$<BOOL:${NETSURFY_USE_HARU_PDF}>:${HPDF_LIBRARY}>
  $<$<PLATFORM_ID:Windows>:ws2_32>
  $<$<PLATFORM_ID:Windows>:${REGEX_LIBRARY}>
  m)

add_subdirectory(src)

if(BUILD_TESTING)
  enable_testing()
  pkg_check_modules(CHECK IMPORTED_TARGET check)
  if(CHECK_FOUND)
    add_subdirectory(test)
  else()
    message(STATUS "check not found, unit tests disabled")
  endif()
  if(NETSURFY_BUILD_HEADLESS)
    file(GLOB browser_tests CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/test/browser/*.html)
    foreach(page ${browser_tests})
      get_filename_component(name ${page} NAME_WE)
      add_test(NAME browser_${name}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/browser/run.py
          --headless $<TARGET_FILE:netsurfy-headless> ${page})
    endforeach()
  endif()
endif()

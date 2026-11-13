cmake_minimum_required(VERSION 3.20)

project(NetSurfy VERSION 1.1.2 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "NetSurfy only supports Linux")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

option(NETSURFY_USE_QUICKJS "JavaScript support (QuickJS-ng)" ON)
option(NETSURFY_USE_WEBP "WebP image support" ON)
option(NETSURFY_USE_JPEGXL "JPEG XL image support" OFF)
option(NETSURFY_USE_HARU_PDF "PDF export (libharu)" OFF)
option(NETSURFY_FS_BACKING_STORE "Disc cache backing store" ON)
option(NETSURFY_USE_SANITIZERS "Build with ASan and UBSan" OFF)
option(NETSURFY_RECOVER_SANITIZERS "Continue after sanitizer errors" ON)
option(BUILD_TESTING "Build the unit tests (requires check)" ON)

set(NETSURFY_LOG_LEVEL "VERBOSE" CACHE STRING "Compiled-in log level")
set(NETSURFY_BUILTIN_LOG_FILTER "(level:WARNING || cat:jserrors)" CACHE STRING "Default log filter")
set(NETSURFY_BUILTIN_VERBOSE_FILTER "(level:VERBOSE || cat:jserrors)" CACHE STRING "Verbose log filter")
set(NETSURFY_UA_FORMAT_STRING "Mozilla/5.0 (Arch Linux; %s) NetSurfy/%d.%d" CACHE STRING "User agent format")
set(NETSURFY_HOMEPAGE "https://html.duckduckgo.com/" CACHE STRING "Default homepage")
set(NETSURFY_GTK_RES_PATH "${CMAKE_INSTALL_FULL_DATADIR}/netsurfy/:./src/frontends/gtk/res/" CACHE STRING "Resource search path")
set(NETSURFY_HOST_C_COMPILER "" CACHE FILEPATH "C compiler for build-time tools when cross compiling")
set(NETSURFY_MESSAGES_LANGUAGES de en fr it nl zh_CN)

find_package(PkgConfig REQUIRED)
find_package(Perl REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(EXPAT REQUIRED)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
if(NETSURFY_USE_QUICKJS)
  find_package(qjs CONFIG REQUIRED)
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
  m)

add_subdirectory(src)

if(BUILD_TESTING)
  pkg_check_modules(CHECK IMPORTED_TARGET check)
  if(CHECK_FOUND)
    enable_testing()
    add_subdirectory(test)
  else()
    message(STATUS "check not found, unit tests disabled")
  endif()
endif()

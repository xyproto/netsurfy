set(SRC ${PROJECT_SOURCE_DIR}/src)
set(TESTROOT ${CMAKE_CURRENT_BINARY_DIR})

set(NSURL_SOURCES
  ${SRC}/utils/nsurl/nsurl.c
  ${SRC}/utils/nsurl/parse.c
  ${SRC}/utils/idna.c
  ${SRC}/utils/punycode.c)

set(nsurl_SRCS ${NSURL_SOURCES} ${SRC}/utils/corestrings.c log.c nsurl.c)
set(urldbtest_SRCS ${NSURL_SOURCES}
  ${SRC}/utils/bloom.c ${SRC}/utils/nsoption.c ${SRC}/utils/corestrings.c
  ${SRC}/utils/time.c ${SRC}/utils/hashtable.c ${SRC}/utils/messages.c
  ${SRC}/utils/utils.c ${SRC}/utils/http/primitives.c ${SRC}/utils/http/generics.c
  ${SRC}/utils/http/strict-transport-security.c ${SRC}/content/urldb.c
  log.c urldbtest.c)
set(bloom_SRCS ${SRC}/utils/bloom.c bloom.c)
set(hashtable_SRCS ${SRC}/utils/hashtable.c log.c hashtable.c)
set(hashmap_SRCS ${NSURL_SOURCES} ${SRC}/utils/hashmap.c ${SRC}/utils/corestrings.c
  log.c hashmap.c)
set(urlescape_SRCS ${SRC}/utils/url.c log.c urlescape.c)
set(utils_SRCS ${NSURL_SOURCES} ${SRC}/utils/utils.c ${SRC}/utils/messages.c
  ${SRC}/utils/hashtable.c ${SRC}/utils/corestrings.c log.c utils.c)
set(time_SRCS ${SRC}/utils/time.c log.c time.c)
set(mimesniff_SRCS ${NSURL_SOURCES} ${SRC}/utils/hashtable.c ${SRC}/utils/corestrings.c
  ${SRC}/utils/http/generics.c ${SRC}/utils/http/content-type.c
  ${SRC}/utils/http/primitives.c ${SRC}/utils/messages.c
  ${SRC}/utils/http/parameter.c ${SRC}/content/mimesniff.c
  log.c mimesniff.c)

add_library(malloc_fig SHARED malloc_fig.c)
target_include_directories(malloc_fig PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(malloc_fig PRIVATE ${CMAKE_DL_LIBS})

set(TESTS nsurl urldbtest bloom hashtable hashmap urlescape utils time
  mimesniff)
foreach(test ${TESTS})
  add_executable(test_${test} ${${test}_SRCS})
  set_target_properties(test_${test} PROPERTIES OUTPUT_NAME ${test})
  target_include_directories(test_${test} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRC}/include
    ${SRC}/content/handlers
    ${SRC}/frontends
    ${SRC}
    ${PROJECT_SOURCE_DIR})
  target_compile_definitions(test_${test} PRIVATE
    _DEFAULT_SOURCE
    _POSIX_C_SOURCE=200809L
    _XOPEN_SOURCE=600
    nsgtk
    "NETSURFY_BUILTIN_LOG_FILTER=\"level:WARNING\""
    "NETSURFY_BUILTIN_VERBOSE_FILTER=\"level:DEBUG\""
    "TESTROOT=\"${TESTROOT}\""
    WITH_UTF8PROC)
  target_compile_options(test_${test} PRIVATE
    -O0 -g -W -Wall -Wundef -Wpointer-arith -Wcast-align -Wwrite-strings
    -Wmissing-declarations -Wuninitialized -Wno-unused-parameter)
  target_link_libraries(test_${test} PRIVATE
    CURL::libcurl libparserutils libwapcaplet libdom libnsutils libutf8proc
    ZLIB::ZLIB PkgConfig::CHECK)
  add_test(NAME ${test} COMMAND test_${test} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endforeach()
target_link_libraries(test_hashmap PRIVATE malloc_fig)

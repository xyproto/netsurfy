cmake_minimum_required(VERSION 3.20)
project(netsurfy_host_tools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(ZLIB REQUIRED)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(NSGENBIND_DIR ${SRC_ROOT}/libs/nsgenbind/src)
set(NSGENBIND_GEN ${CMAKE_CURRENT_BINARY_DIR}/nsgenbind-gen)
file(MAKE_DIRECTORY ${NSGENBIND_GEN})

foreach(grammar nsgenbind webidl)
  bison_target(${grammar}_parser ${NSGENBIND_DIR}/${grammar}-parser.y
    ${NSGENBIND_GEN}/${grammar}-parser.c
    DEFINES_FILE ${NSGENBIND_GEN}/${grammar}-parser.h
    COMPILE_FLAGS "-t --define=api.prefix={${grammar}_}")
  flex_target(${grammar}_lexer ${NSGENBIND_DIR}/${grammar}-lexer.l
    ${NSGENBIND_GEN}/${grammar}-lexer.c
    DEFINES_FILE ${NSGENBIND_GEN}/${grammar}-lexer.h)
  add_flex_bison_dependency(${grammar}_lexer ${grammar}_parser)
  list(APPEND NSGENBIND_GENERATED
    ${BISON_${grammar}_parser_OUTPUTS} ${FLEX_${grammar}_lexer_OUTPUTS})
endforeach()

add_executable(nsgenbind
  ${NSGENBIND_GENERATED}
  ${NSGENBIND_DIR}/nsgenbind.c
  ${NSGENBIND_DIR}/utils.c
  ${NSGENBIND_DIR}/output.c
  ${NSGENBIND_DIR}/webidl-ast.c
  ${NSGENBIND_DIR}/nsgenbind-ast.c
  ${NSGENBIND_DIR}/ir.c
  ${NSGENBIND_DIR}/duk-libdom.c
  ${NSGENBIND_DIR}/duk-libdom-interface.c
  ${NSGENBIND_DIR}/duk-libdom-dictionary.c
  ${NSGENBIND_DIR}/duk-libdom-generated.c
  ${NSGENBIND_DIR}/duk-libdom-common.c)
target_include_directories(nsgenbind PRIVATE ${NSGENBIND_DIR} ${NSGENBIND_GEN})
target_compile_definitions(nsgenbind PRIVATE
  _BSD_SOURCE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L YYENABLE_NLS=0)

add_executable(split-messages split-messages.c)
target_include_directories(split-messages PRIVATE ${SRC_ROOT})
target_link_libraries(split-messages PRIVATE ZLIB::ZLIB)

add_executable(css_gen_parser
  ${SRC_ROOT}/libs/libcss/src/parse/properties/css_property_parser_gen.c)

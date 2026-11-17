set(NETSURFY_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${NETSURFY_GEN})

set(HOST_TOOLS_DIR ${CMAKE_BINARY_DIR}/host-tools)
if(NETSURFY_HOST_C_COMPILER)
  set(host_compiler -DCMAKE_C_COMPILER=${NETSURFY_HOST_C_COMPILER})
elseif(NOT CMAKE_CROSSCOMPILING)
  set(host_compiler -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/tools -B ${HOST_TOOLS_DIR}
    -G ${CMAKE_GENERATOR} -DCMAKE_BUILD_TYPE=Release ${host_compiler}
  RESULT_VARIABLE host_tools_result
  OUTPUT_VARIABLE host_tools_output
  ERROR_VARIABLE host_tools_output)
if(host_tools_result EQUAL 0)
  execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${HOST_TOOLS_DIR}
    RESULT_VARIABLE host_tools_result
    OUTPUT_VARIABLE host_tools_output
    ERROR_VARIABLE host_tools_output)
endif()
if(NOT host_tools_result EQUAL 0)
  message(FATAL_ERROR "Building the host tools in ${HOST_TOOLS_DIR} failed:\n${host_tools_output}")
endif()
file(GLOB_RECURSE host_tool_sources CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/split-messages.c
  ${CMAKE_CURRENT_SOURCE_DIR}/libs/nsgenbind/src/*
  ${CMAKE_CURRENT_SOURCE_DIR}/libs/libcss/src/parse/properties/css_property_parser_gen.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${host_tool_sources})
if(CMAKE_HOST_WIN32)
  set(host_executable_suffix .exe)
endif()
set(NETSURFY_NSGENBIND ${HOST_TOOLS_DIR}/nsgenbind${host_executable_suffix})
set(NETSURFY_SPLIT_MESSAGES ${HOST_TOOLS_DIR}/split-messages${host_executable_suffix})
set(NETSURFY_CSS_GEN_PARSER ${HOST_TOOLS_DIR}/css_gen_parser${host_executable_suffix})

add_subdirectory(libs)

set(NETSURFY_CORE_SOURCES
  content/content.c
  content/content_factory.c
  content/fetch.c
  content/fetchers/about/about.c
  content/fetchers/about/blank.c
  content/fetchers/about/certificate.c
  content/fetchers/about/chart.c
  content/fetchers/about/choices.c
  content/fetchers/about/config.c
  content/fetchers/about/imagecache.c
  content/fetchers/about/nscolours.c
  content/fetchers/about/query_auth.c
  content/fetchers/about/query.c
  content/fetchers/about/start.c
  content/fetchers/about/query_fetcherror.c
  content/fetchers/about/query_privacy.c
  content/fetchers/about/query_timeout.c
  content/fetchers/about/testament.c
  content/fetchers/about/websearch.c
  content/fetchers/curl.c
  content/fetchers/data.c
  content/fetchers/file/dirlist.c
  content/fetchers/file/file.c
  content/fetchers/resource.c
  content/handlers/css/css.c
  content/handlers/css/dump.c
  content/handlers/css/hints.c
  content/handlers/css/internal.c
  content/handlers/css/select.c
  content/handlers/html/box_construct.c
  content/handlers/html/box_inspect.c
  content/handlers/html/box_manipulate.c
  content/handlers/html/box_normalise.c
  content/handlers/html/box_special.c
  content/handlers/html/box_textarea.c
  content/handlers/html/css.c
  content/handlers/html/css_fetcher.c
  content/handlers/html/dom_event.c
  content/handlers/html/font.c
  content/handlers/html/form.c
  content/handlers/html/forms.c
  content/handlers/html/html.c
  content/handlers/html/imagemap.c
  content/handlers/html/interaction.c
  content/handlers/html/layout.c
  content/handlers/html/layout_flex.c
  content/handlers/html/object.c
  content/handlers/html/redraw_border.c
  content/handlers/html/redraw.c
  content/handlers/html/script.c
  content/handlers/html/table.c
  content/handlers/html/textselection.c
  content/handlers/image/bmp.c
  content/handlers/image/gif.c
  content/handlers/image/ico.c
  content/handlers/image/image.c
  content/handlers/image/image_cache.c
  content/handlers/image/jpeg.c
  content/handlers/image/nssprite.c
  content/handlers/image/png.c
  content/handlers/image/svg.c
  content/handlers/text/textplain.c
  content/hlcache.c
  content/llcache.c
  content/mimesniff.c
  content/no_backing_store.c
  content/textsearch.c
  content/urldb.c
  desktop/bitmap.c
  desktop/browser.c
  desktop/browser_history.c
  desktop/browser_window.c
  desktop/cookie_manager.c
  desktop/cw_helper.c
  desktop/download.c
  desktop/font_haru.c
  desktop/frames.c
  desktop/global_history.c
  desktop/gui_factory.c
  desktop/hotlist.c
  desktop/knockout.c
  desktop/local_history.c
  desktop/mouse.c
  desktop/netsurfy.c
  desktop/page-info.c
  desktop/plot_style.c
  desktop/print.c
  desktop/save_complete.c
  desktop/save_pdf.c
  desktop/save_text.c
  desktop/scrollbar.c
  desktop/search.c
  desktop/searchweb.c
  desktop/selection.c
  desktop/system_colour.c
  desktop/textarea.c
  desktop/textinput.c
  desktop/treeview.c
  desktop/version.c
  utils/bloom.c
  utils/corestrings.c
  utils/file.c
  utils/filepath.c
  utils/hashmap.c
  utils/hashtable.c
  utils/http/cache-control.c
  utils/http/challenge.c
  utils/http/content-disposition.c
  utils/http/content-type.c
  utils/http/generics.c
  utils/http/parameter.c
  utils/http/primitives.c
  utils/http/strict-transport-security.c
  utils/http/www-authenticate.c
  utils/idna.c
  utils/libdom.c
  utils/log.c
  utils/messages.c
  utils/nscolour.c
  utils/nsoption.c
  utils/nsurl/nsurl.c
  utils/nsurl/parse.c
  utils/punycode.c
  utils/ssl_certs.c
  utils/talloc.c
  utils/time.c
  utils/url.c
  utils/useragent.c
  utils/utf8.c
  utils/utils.c
)
if(NETSURFY_FS_BACKING_STORE)
  list(APPEND NETSURFY_CORE_SOURCES content/fs_backing_store.c)
endif()
if(NETSURFY_USE_WEBP)
  list(APPEND NETSURFY_CORE_SOURCES content/handlers/image/webp.c)
endif()
if(NETSURFY_USE_JPEGXL)
  list(APPEND NETSURFY_CORE_SOURCES content/handlers/image/jpegxl.c)
endif()

set(NETSURFY_GTK_SOURCES
  frontends/gtk/about.c
  frontends/gtk/accelerator.c
  frontends/gtk/bitmap.c
  frontends/gtk/compat.c
  frontends/gtk/completion.c
  frontends/gtk/cookies.c
  frontends/gtk/corewindow.c
  frontends/gtk/download.c
  frontends/gtk/fetch.c
  frontends/gtk/gdk.c
  frontends/gtk/gettext.c
  frontends/gtk/global_history.c
  frontends/gtk/gui.c
  frontends/gtk/hotlist.c
  frontends/gtk/layout_pango.c
  frontends/gtk/local_history.c
  frontends/gtk/menu.c
  frontends/gtk/misc.c
  frontends/gtk/page_info.c
  frontends/gtk/plotters.c
  frontends/gtk/preferences.c
  frontends/gtk/print.c
  frontends/gtk/resources.c
  frontends/gtk/scaffolding.c
  frontends/gtk/schedule.c
  frontends/gtk/search.c
  frontends/gtk/selection.c
  frontends/gtk/tabs.c
  frontends/gtk/throbber.c
  frontends/gtk/toolbar.c
  frontends/gtk/viewdata.c
  frontends/gtk/viewsource.c
  frontends/gtk/window.c
)

add_custom_target(testament
  COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
    -DTARGET=${NETSURFY_GEN}/testament.h
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/git-testament.cmake
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  BYPRODUCTS ${NETSURFY_GEN}/testament.h
  VERBATIM)

set(JAVASCRIPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/content/handlers/javascript)
if(NETSURFY_USE_QUICKJS)
  set(QUICKJS_GEN ${NETSURFY_GEN}/quickjs)
  file(GLOB quickjs_bindings CONFIGURE_DEPENDS
    ${JAVASCRIPT_DIR}/quickjs/*.bnd ${JAVASCRIPT_DIR}/WebIDL/*.idl)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${quickjs_bindings})
  file(REMOVE_RECURSE ${QUICKJS_GEN})
  file(MAKE_DIRECTORY ${QUICKJS_GEN})
  execute_process(
    COMMAND ${NETSURFY_NSGENBIND} -I ${JAVASCRIPT_DIR}/WebIDL
      ${JAVASCRIPT_DIR}/quickjs/netsurfy.bnd ${QUICKJS_GEN}
    RESULT_VARIABLE nsgenbind_result
    OUTPUT_QUIET)
  if(NOT nsgenbind_result EQUAL 0)
    message(FATAL_ERROR "nsgenbind failed to generate the JavaScript bindings")
  endif()
  file(STRINGS ${QUICKJS_GEN}/Makefile nsgenbind_sources_line
    REGEX "^NSGENBIND_SOURCES:=")
  string(REGEX REPLACE "^NSGENBIND_SOURCES:=" "" nsgenbind_sources "${nsgenbind_sources_line}")
  separate_arguments(nsgenbind_sources UNIX_COMMAND "${nsgenbind_sources}")
  list(TRANSFORM nsgenbind_sources PREPEND ${QUICKJS_GEN}/)

  add_custom_command(
    OUTPUT ${QUICKJS_GEN}/prelude.js.inc
    COMMAND ${CMAKE_COMMAND} -DINPUT=${JAVASCRIPT_DIR}/quickjs/prelude.js
      -DOUTPUT=${QUICKJS_GEN}/prelude.js.inc -DNAME=prelude_js
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.cmake
    DEPENDS ${JAVASCRIPT_DIR}/quickjs/prelude.js
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.cmake
    VERBATIM)

  set(NETSURFY_JAVASCRIPT_SOURCES
    content/handlers/javascript/content.c
    content/handlers/javascript/quickjs/qjsky.c
    content/handlers/javascript/quickjs/geometry.c
    content/handlers/javascript/quickjs/mutation.c
    content/handlers/javascript/quickjs/urlutils.c
    content/handlers/javascript/fetcher.c
    ${QUICKJS_GEN}/prelude.js.inc
    ${nsgenbind_sources})
else()
  set(NETSURFY_JAVASCRIPT_SOURCES
    content/handlers/javascript/fetcher.c
    content/handlers/javascript/none/none.c)
endif()

set(GTK_RES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontends/gtk/res)
if(WIN32)
  function(netsurfy_copy_resource source destination)
    if(NOT IS_DIRECTORY "${source}")
      file(SIZE "${source}" size)
      if(size LESS 512)
        file(READ "${source}" link)
        if(link MATCHES "^[a-zA-Z0-9_.-]+/[^ \t\r\n]+$")
          get_filename_component(directory "${source}" DIRECTORY)
          if(EXISTS "${directory}/${link}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
            netsurfy_copy_resource("${directory}/${link}" "${destination}")
            return()
          endif()
        endif()
      endif()
    endif()
    if(IS_DIRECTORY "${source}")
      file(GLOB children CONFIGURE_DEPENDS "${source}/*")
      foreach(child IN LISTS children)
        get_filename_component(name "${child}" NAME)
        netsurfy_copy_resource("${child}" "${destination}/${name}")
      endforeach()
    else()
      configure_file("${source}" "${destination}" COPYONLY)
    endif()
  endfunction()
  netsurfy_copy_resource("${GTK_RES_DIR}" "${NETSURFY_GEN}/resources")
  set(GTK_RES_DIR ${NETSURFY_GEN}/resources)
endif()
set(MESSAGES_DIR ${NETSURFY_GEN}/messages)
set(NETSURFY_MESSAGES)
foreach(language ${NETSURFY_MESSAGES_LANGUAGES})
  add_custom_command(
    OUTPUT ${MESSAGES_DIR}/${language}/Messages
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MESSAGES_DIR}/${language}
    COMMAND ${NETSURFY_SPLIT_MESSAGES} -l ${language} -p gtk -f messages
      -o ${MESSAGES_DIR}/${language}/Messages -z ${PROJECT_SOURCE_DIR}/resources/FatMessages
    DEPENDS ${PROJECT_SOURCE_DIR}/resources/FatMessages ${NETSURFY_SPLIT_MESSAGES}
    VERBATIM)
  list(APPEND NETSURFY_MESSAGES ${MESSAGES_DIR}/${language}/Messages)
endforeach()
add_custom_command(
  OUTPUT ${MESSAGES_DIR}/Messages
  COMMAND ${CMAKE_COMMAND} -E copy ${MESSAGES_DIR}/en/Messages ${MESSAGES_DIR}/Messages
  DEPENDS ${MESSAGES_DIR}/en/Messages
  VERBATIM)
list(APPEND NETSURFY_MESSAGES ${MESSAGES_DIR}/Messages)

function(netsurfy_gresource name sourcedir)
  set(dependencies ${ARGN})
  if(NOT dependencies)
    execute_process(
      COMMAND ${GLIB_COMPILE_RESOURCES} --sourcedir ${sourcedir} --sourcedir ${PROJECT_SOURCE_DIR}
        --generate-dependencies ${GTK_RES_DIR}/${name}.gresource.xml
      OUTPUT_VARIABLE dependencies
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\r" "" dependencies "${dependencies}")
    string(REPLACE "\n" ";" dependencies "${dependencies}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${GTK_RES_DIR}/${name}.gresource.xml)
  add_custom_command(
    OUTPUT ${NETSURFY_GEN}/${name}_gresource.c
    COMMAND ${GLIB_COMPILE_RESOURCES} --generate-source --sourcedir ${sourcedir} --sourcedir ${PROJECT_SOURCE_DIR}
      --target=${NETSURFY_GEN}/${name}_gresource.c ${GTK_RES_DIR}/${name}.gresource.xml
    DEPENDS ${GTK_RES_DIR}/${name}.gresource.xml ${dependencies}
    VERBATIM)
endfunction()
netsurfy_gresource(netsurfy ${GTK_RES_DIR})
netsurfy_gresource(ui ${GTK_RES_DIR}/gtk4)
netsurfy_gresource(messages ${MESSAGES_DIR} ${NETSURFY_MESSAGES})

add_executable(netsurfy
  ${NETSURFY_CORE_SOURCES}
  ${NETSURFY_JAVASCRIPT_SOURCES}
  ${NETSURFY_GTK_SOURCES}
  ${NETSURFY_GEN}/netsurfy_gresource.c
  ${NETSURFY_GEN}/ui_gresource.c
  ${NETSURFY_GEN}/messages_gresource.c)
add_dependencies(netsurfy testament)
set_target_properties(netsurfy PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(WIN32)
  set_target_properties(netsurfy PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(netsurfy PRIVATE frontends/gtk/netsurfy.rc)
  target_include_directories(netsurfy PRIVATE ${PROJECT_SOURCE_DIR}/img)
endif()

target_include_directories(netsurfy PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/frontends
  ${CMAKE_CURRENT_SOURCE_DIR}/content/handlers
  ${NETSURFY_GEN})

target_compile_definitions(netsurfy PRIVATE
  ${NETSURFY_CORE_DEFINES}
  gtk
  nsgtk
  G_DISABLE_SINGLE_INCLUDES
  G_DISABLE_DEPRECATED
  GTK_DISABLE_SINGLE_INCLUDES
  GTK_MULTIHEAD_SAFE
  PANGO_DISABLE_DEPRECATED
  _XOPEN_SOURCE=700
  _POSIX_C_SOURCE=200809L
  _BSD_SOURCE
  _DEFAULT_SOURCE
  "GTK_RESPATH=\"${NETSURFY_GTK_RES_PATH}\""
  WITH_GRESOURCE)

target_compile_options(netsurfy PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:${NETSURFY_WARNING_FLAGS};-include;${NETSURFY_CONFIG_HEADER}>")

target_link_libraries(netsurfy PRIVATE
  ${NETSURFY_CORE_LIBRARIES}
  $<$<BOOL:${NETSURFY_USE_QUICKJS}>:qjs>
  PkgConfig::GTK)

install(TARGETS netsurfy RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(NETSURFY_BUILD_HEADLESS)
  add_executable(netsurfy-headless
    ${NETSURFY_CORE_SOURCES}
    ${NETSURFY_JAVASCRIPT_SOURCES}
    frontends/headless/401login.c
    frontends/headless/bitmap.c
    frontends/headless/browser.c
    frontends/headless/dispatch.c
    frontends/headless/download.c
    frontends/headless/duck.c
    frontends/headless/fetch.c
    frontends/headless/filetype.c
    frontends/headless/layout.c
    frontends/headless/main.c
    frontends/headless/output.c
    frontends/headless/plot.c
    frontends/headless/schedule.c)
  add_dependencies(netsurfy-headless testament)
  set_target_properties(netsurfy-headless PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_include_directories(netsurfy-headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/frontends
    ${CMAKE_CURRENT_SOURCE_DIR}/content/handlers
    ${NETSURFY_GEN})
  target_compile_definitions(netsurfy-headless PRIVATE
    ${NETSURFY_CORE_DEFINES}
    nsheadless
    _XOPEN_SOURCE=700
    _POSIX_C_SOURCE=200809L
    _BSD_SOURCE
    _DEFAULT_SOURCE
    "HEADLESS_RESPATH=\"${MESSAGES_DIR}:${GTK_RES_DIR}\"")
  target_compile_options(netsurfy-headless PRIVATE ${NETSURFY_WARNING_FLAGS} -include ${NETSURFY_CONFIG_HEADER})
  target_link_libraries(netsurfy-headless PRIVATE
    ${NETSURFY_CORE_LIBRARIES}
    $<$<BOOL:${NETSURFY_USE_QUICKJS}>:qjs>)
endif()

set(NETSURFY_DATADIR ${CMAKE_INSTALL_DATADIR}/netsurfy)
function(netsurfy_install_resolved destination)
  foreach(path ${ARGN})
    get_filename_component(name ${path} NAME)
    get_filename_component(real_path ${path} REALPATH)
    install(FILES ${real_path} DESTINATION ${destination} RENAME ${name})
  endforeach()
endfunction()

set(GTK_RESOURCES
  languages SearchEngines ca-bundle.txt default.css adblock.css quirks.css
  internal.css credits.html licence.html favicon.png netsurfy.png)
list(TRANSFORM GTK_RESOURCES PREPEND ${GTK_RES_DIR}/)
list(APPEND GTK_RESOURCES
  ${PROJECT_SOURCE_DIR}/img/gtk/default.ico
  ${PROJECT_SOURCE_DIR}/img/gtk/netsurfy.xpm
  ${PROJECT_SOURCE_DIR}/img/gtk/netsurfy-16x16.xpm)
file(GLOB GTK_UI_FILES ${GTK_RES_DIR}/gtk4/*.ui)
netsurfy_install_resolved(${NETSURFY_DATADIR} ${GTK_RESOURCES} ${GTK_UI_FILES})
install(FILES ${MESSAGES_DIR}/Messages DESTINATION ${NETSURFY_DATADIR})

file(GLOB GTK_ICONS ${GTK_RES_DIR}/icons/*.png)
netsurfy_install_resolved(${NETSURFY_DATADIR}/icons ${GTK_ICONS})
file(GLOB GTK_THROBBER ${GTK_RES_DIR}/throbber/*.png)
netsurfy_install_resolved(${NETSURFY_DATADIR}/throbber ${GTK_THROBBER})
install(FILES ${PROJECT_SOURCE_DIR}/img/icons/256x256/apps/netsurfy.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)

foreach(language ${NETSURFY_MESSAGES_LANGUAGES})
  file(GLOB language_files ${GTK_RES_DIR}/${language}/*)
  netsurfy_install_resolved(${NETSURFY_DATADIR}/${language} ${language_files})
  install(FILES ${MESSAGES_DIR}/${language}/Messages DESTINATION ${NETSURFY_DATADIR}/${language})
endforeach()

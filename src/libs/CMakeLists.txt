set(NSLIBS_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${NSLIBS_GEN})

add_compile_definitions(NDEBUG)

function(nslib name)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEFINES;PRIVATE_INCLUDES;LINK" ${ARGN})
  add_library(${name} STATIC ${ARG_SOURCES})
  string(REGEX REPLACE "^lib" "" output_name ${name})
  set_target_properties(${name} PROPERTIES OUTPUT_NAME ${output_name})
  target_include_directories(${name}
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/${name}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name}/src ${ARG_PRIVATE_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
  target_link_libraries(${name} PUBLIC ${ARG_LINK})
endfunction()

set(BSD_DEFINES _BSD_SOURCE _DEFAULT_SOURCE)
set(GNU_DEFINES _GNU_SOURCE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)

nslib(libwapcaplet
  SOURCES
  libwapcaplet/src/libwapcaplet.c
  DEFINES ${BSD_DEFINES})

add_custom_command(
  OUTPUT ${NSLIBS_GEN}/libparserutils/aliases.inc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${NSLIBS_GEN}/libparserutils
  COMMAND ${PERL_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/libparserutils/build/make-aliases.pl
    ${CMAKE_CURRENT_SOURCE_DIR}/libparserutils/build/Aliases
    ${NSLIBS_GEN}/libparserutils/aliases.inc
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/libparserutils/build/make-aliases.pl
    ${CMAKE_CURRENT_SOURCE_DIR}/libparserutils/build/Aliases
  VERBATIM)

nslib(libparserutils
  SOURCES
  libparserutils/src/charset/aliases.c
  libparserutils/src/charset/codec.c
  libparserutils/src/charset/codecs/codec_ascii.c
  libparserutils/src/charset/codecs/codec_8859.c
  libparserutils/src/charset/codecs/codec_ext8.c
  libparserutils/src/charset/codecs/codec_utf8.c
  libparserutils/src/charset/codecs/codec_utf16.c
  libparserutils/src/charset/encodings/utf8.c
  libparserutils/src/charset/encodings/utf16.c
  libparserutils/src/input/filter.c
  libparserutils/src/input/inputstream.c
  libparserutils/src/utils/buffer.c
  libparserutils/src/utils/errors.c
  libparserutils/src/utils/stack.c
  libparserutils/src/utils/vector.c
  ${NSLIBS_GEN}/libparserutils/aliases.inc
  DEFINES ${BSD_DEFINES}
  PRIVATE_INCLUDES ${NSLIBS_GEN}/libparserutils)

add_custom_command(
  OUTPUT ${NSLIBS_GEN}/libhubbub/entities.inc
  COMMAND ${CMAKE_COMMAND} -E make_directory ${NSLIBS_GEN}/libhubbub
  COMMAND ${PERL_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/libhubbub/build/make-entities.pl
    ${CMAKE_CURRENT_SOURCE_DIR}/libhubbub/build/Entities
    ${NSLIBS_GEN}/libhubbub/entities.inc
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/libhubbub/build/make-entities.pl
    ${CMAKE_CURRENT_SOURCE_DIR}/libhubbub/build/Entities
  VERBATIM)

nslib(libhubbub
  SOURCES
  libhubbub/src/parser.c
  libhubbub/src/charset/detect.c
  libhubbub/src/tokeniser/entities.c
  libhubbub/src/tokeniser/tokeniser.c
  libhubbub/src/treebuilder/treebuilder.c
  libhubbub/src/treebuilder/initial.c
  libhubbub/src/treebuilder/before_html.c
  libhubbub/src/treebuilder/before_head.c
  libhubbub/src/treebuilder/in_head.c
  libhubbub/src/treebuilder/in_head_noscript.c
  libhubbub/src/treebuilder/after_head.c
  libhubbub/src/treebuilder/in_body.c
  libhubbub/src/treebuilder/in_table.c
  libhubbub/src/treebuilder/in_caption.c
  libhubbub/src/treebuilder/in_column_group.c
  libhubbub/src/treebuilder/in_table_body.c
  libhubbub/src/treebuilder/in_row.c
  libhubbub/src/treebuilder/in_cell.c
  libhubbub/src/treebuilder/in_select.c
  libhubbub/src/treebuilder/in_select_in_table.c
  libhubbub/src/treebuilder/in_foreign_content.c
  libhubbub/src/treebuilder/after_body.c
  libhubbub/src/treebuilder/in_frameset.c
  libhubbub/src/treebuilder/after_frameset.c
  libhubbub/src/treebuilder/after_after_body.c
  libhubbub/src/treebuilder/after_after_frameset.c
  libhubbub/src/treebuilder/generic_rcdata.c
  libhubbub/src/treebuilder/element-type.c
  libhubbub/src/utils/errors.c
  libhubbub/src/utils/string.c
  ${NSLIBS_GEN}/libhubbub/entities.inc
  DEFINES ${BSD_DEFINES}
  PRIVATE_INCLUDES ${NSLIBS_GEN}/libhubbub
  LINK libparserutils)

foreach(binding_header hubbub/errors.h hubbub/parser.h xml/xmlerror.h xml/xmlparser.h)
  configure_file(libdom/bindings/${binding_header}
    ${NSLIBS_GEN}/libdom/dom/bindings/${binding_header} COPYONLY)
endforeach()

nslib(libdom
  SOURCES
  libdom/bindings/hubbub/parser.c
  libdom/bindings/xml/expat_xmlparser.c
  libdom/src/core/string.c
  libdom/src/core/node.c
  libdom/src/core/attr.c
  libdom/src/core/characterdata.c
  libdom/src/core/element.c
  libdom/src/core/implementation.c
  libdom/src/core/text.c
  libdom/src/core/typeinfo.c
  libdom/src/core/comment.c
  libdom/src/core/namednodemap.c
  libdom/src/core/nodelist.c
  libdom/src/core/cdatasection.c
  libdom/src/core/document_type.c
  libdom/src/core/entity_ref.c
  libdom/src/core/pi.c
  libdom/src/core/doc_fragment.c
  libdom/src/core/document.c
  libdom/src/core/tokenlist.c
  libdom/src/events/event.c
  libdom/src/events/dispatch.c
  libdom/src/events/event_target.c
  libdom/src/events/document_event.c
  libdom/src/events/custom_event.c
  libdom/src/events/keyboard_event.c
  libdom/src/events/mouse_wheel_event.c
  libdom/src/events/text_event.c
  libdom/src/events/event_listener.c
  libdom/src/events/mouse_event.c
  libdom/src/events/mutation_event.c
  libdom/src/events/ui_event.c
  libdom/src/events/mouse_multi_wheel_event.c
  libdom/src/events/mutation_name_event.c
  libdom/src/html/html_applet_element.c
  libdom/src/html/html_area_element.c
  libdom/src/html/html_anchor_element.c
  libdom/src/html/html_basefont_element.c
  libdom/src/html/html_base_element.c
  libdom/src/html/html_body_element.c
  libdom/src/html/html_button_element.c
  libdom/src/html/html_canvas_element.c
  libdom/src/html/html_collection.c
  libdom/src/html/html_document.c
  libdom/src/html/html_element.c
  libdom/src/html/html_dlist_element.c
  libdom/src/html/html_directory_element.c
  libdom/src/html/html_options_collection.c
  libdom/src/html/html_html_element.c
  libdom/src/html/html_head_element.c
  libdom/src/html/html_link_element.c
  libdom/src/html/html_title_element.c
  libdom/src/html/html_meta_element.c
  libdom/src/html/html_style_element.c
  libdom/src/html/html_form_element.c
  libdom/src/html/html_select_element.c
  libdom/src/html/html_input_element.c
  libdom/src/html/html_text_area_element.c
  libdom/src/html/html_opt_group_element.c
  libdom/src/html/html_option_element.c
  libdom/src/html/html_hr_element.c
  libdom/src/html/html_menu_element.c
  libdom/src/html/html_fieldset_element.c
  libdom/src/html/html_legend_element.c
  libdom/src/html/html_div_element.c
  libdom/src/html/html_paragraph_element.c
  libdom/src/html/html_heading_element.c
  libdom/src/html/html_quote_element.c
  libdom/src/html/html_pre_element.c
  libdom/src/html/html_br_element.c
  libdom/src/html/html_label_element.c
  libdom/src/html/html_ulist_element.c
  libdom/src/html/html_olist_element.c
  libdom/src/html/html_li_element.c
  libdom/src/html/html_font_element.c
  libdom/src/html/html_mod_element.c
  libdom/src/html/html_image_element.c
  libdom/src/html/html_object_element.c
  libdom/src/html/html_param_element.c
  libdom/src/html/html_map_element.c
  libdom/src/html/html_script_element.c
  libdom/src/html/html_tablecaption_element.c
  libdom/src/html/html_tablecell_element.c
  libdom/src/html/html_tablecol_element.c
  libdom/src/html/html_tablesection_element.c
  libdom/src/html/html_table_element.c
  libdom/src/html/html_tablerow_element.c
  libdom/src/html/html_frameset_element.c
  libdom/src/html/html_frame_element.c
  libdom/src/html/html_iframe_element.c
  libdom/src/html/html_isindex_element.c
  libdom/src/utils/namespace.c
  libdom/src/utils/hashtable.c
  libdom/src/utils/character_valid.c
  libdom/src/utils/validate.c
  libdom/src/utils/walk.c
  DEFINES ${BSD_DEFINES}
  PRIVATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/libdom/bindings/xml
  LINK libwapcaplet libparserutils libhubbub EXPAT::EXPAT)
target_include_directories(libdom PUBLIC ${NSLIBS_GEN}/libdom)

set(CSS_PROPERTIES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libcss/src/parse/properties)
set(CSS_GEN ${NSLIBS_GEN}/libcss)
file(MAKE_DIRECTORY ${CSS_GEN})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CSS_PROPERTIES_DIR}/properties.gen)
file(STRINGS ${CSS_PROPERTIES_DIR}/properties.gen css_property_lines
  REGEX "^[^#][^:]*:")
set(CSS_GENERATED)
foreach(line IN LISTS css_property_lines)
  string(REGEX MATCH "^[^:]+" property "${line}")
  add_custom_command(
    OUTPUT ${CSS_GEN}/autogenerated_${property}.c
    COMMAND ${NETSURFY_CSS_GEN_PARSER} -o ${CSS_GEN}/autogenerated_${property}.c "${line}"
    DEPENDS ${CSS_PROPERTIES_DIR}/properties.gen ${NETSURFY_CSS_GEN_PARSER}
    VERBATIM)
  list(APPEND CSS_GENERATED ${CSS_GEN}/autogenerated_${property}.c)
endforeach()

nslib(libcss
  SOURCES
  libcss/src/stylesheet.c
  libcss/src/charset/detect.c
  libcss/src/lex/lex.c
  libcss/src/parse/parse.c
  libcss/src/parse/language.c
  libcss/src/parse/important.c
  libcss/src/parse/propstrings.c
  libcss/src/parse/font_face.c
  libcss/src/parse/mq.c
  libcss/src/parse/properties/azimuth.c
  libcss/src/parse/properties/background.c
  libcss/src/parse/properties/background_position.c
  libcss/src/parse/properties/border.c
  libcss/src/parse/properties/border_color.c
  libcss/src/parse/properties/border_spacing.c
  libcss/src/parse/properties/border_style.c
  libcss/src/parse/properties/border_width.c
  libcss/src/parse/properties/clip.c
  libcss/src/parse/properties/columns.c
  libcss/src/parse/properties/column_rule.c
  libcss/src/parse/properties/content.c
  libcss/src/parse/properties/cue.c
  libcss/src/parse/properties/cursor.c
  libcss/src/parse/properties/elevation.c
  libcss/src/parse/properties/fill_opacity.c
  libcss/src/parse/properties/flex.c
  libcss/src/parse/properties/flex_flow.c
  libcss/src/parse/properties/font.c
  libcss/src/parse/properties/font_family.c
  libcss/src/parse/properties/font_weight.c
  libcss/src/parse/properties/gap.c
  libcss/src/parse/properties/list_style.c
  libcss/src/parse/properties/list_style_type.c
  libcss/src/parse/properties/margin.c
  libcss/src/parse/properties/opacity.c
  libcss/src/parse/properties/outline.c
  libcss/src/parse/properties/overflow.c
  libcss/src/parse/properties/padding.c
  libcss/src/parse/properties/pause.c
  libcss/src/parse/properties/play_during.c
  libcss/src/parse/properties/properties.c
  libcss/src/parse/properties/quotes.c
  libcss/src/parse/properties/stroke_opacity.c
  libcss/src/parse/properties/text_decoration.c
  libcss/src/parse/properties/utils.c
  libcss/src/parse/properties/voice_family.c
  libcss/src/select/arena.c
  libcss/src/select/calc.c
  libcss/src/select/computed.c
  libcss/src/select/dispatch.c
  libcss/src/select/hash.c
  libcss/src/select/select.c
  libcss/src/select/strings.c
  libcss/src/select/font_face.c
  libcss/src/select/format_list_style.c
  libcss/src/select/unit.c
  libcss/src/select/properties/helpers.c
  libcss/src/select/properties/align_content.c
  libcss/src/select/properties/align_items.c
  libcss/src/select/properties/align_self.c
  libcss/src/select/properties/azimuth.c
  libcss/src/select/properties/background_attachment.c
  libcss/src/select/properties/background_color.c
  libcss/src/select/properties/background_image.c
  libcss/src/select/properties/background_position.c
  libcss/src/select/properties/background_repeat.c
  libcss/src/select/properties/border_bottom_color.c
  libcss/src/select/properties/border_bottom_style.c
  libcss/src/select/properties/border_bottom_width.c
  libcss/src/select/properties/border_collapse.c
  libcss/src/select/properties/border_left_color.c
  libcss/src/select/properties/border_left_style.c
  libcss/src/select/properties/border_left_width.c
  libcss/src/select/properties/border_right_color.c
  libcss/src/select/properties/border_right_style.c
  libcss/src/select/properties/border_right_width.c
  libcss/src/select/properties/border_spacing.c
  libcss/src/select/properties/border_top_color.c
  libcss/src/select/properties/border_top_style.c
  libcss/src/select/properties/border_top_width.c
  libcss/src/select/properties/bottom.c
  libcss/src/select/properties/box_sizing.c
  libcss/src/select/properties/break_after.c
  libcss/src/select/properties/break_before.c
  libcss/src/select/properties/break_inside.c
  libcss/src/select/properties/caption_side.c
  libcss/src/select/properties/clear.c
  libcss/src/select/properties/clip.c
  libcss/src/select/properties/color.c
  libcss/src/select/properties/column_count.c
  libcss/src/select/properties/column_fill.c
  libcss/src/select/properties/column_gap.c
  libcss/src/select/properties/column_rule_color.c
  libcss/src/select/properties/column_rule_style.c
  libcss/src/select/properties/column_rule_width.c
  libcss/src/select/properties/column_span.c
  libcss/src/select/properties/column_width.c
  libcss/src/select/properties/content.c
  libcss/src/select/properties/counter_increment.c
  libcss/src/select/properties/counter_reset.c
  libcss/src/select/properties/cue_after.c
  libcss/src/select/properties/cue_before.c
  libcss/src/select/properties/cursor.c
  libcss/src/select/properties/direction.c
  libcss/src/select/properties/display.c
  libcss/src/select/properties/elevation.c
  libcss/src/select/properties/empty_cells.c
  libcss/src/select/properties/fill_opacity.c
  libcss/src/select/properties/flex_basis.c
  libcss/src/select/properties/flex_direction.c
  libcss/src/select/properties/flex_grow.c
  libcss/src/select/properties/flex_shrink.c
  libcss/src/select/properties/flex_wrap.c
  libcss/src/select/properties/float.c
  libcss/src/select/properties/font_family.c
  libcss/src/select/properties/font_size.c
  libcss/src/select/properties/font_style.c
  libcss/src/select/properties/font_variant.c
  libcss/src/select/properties/font_weight.c
  libcss/src/select/properties/height.c
  libcss/src/select/properties/justify_content.c
  libcss/src/select/properties/left.c
  libcss/src/select/properties/letter_spacing.c
  libcss/src/select/properties/line_height.c
  libcss/src/select/properties/list_style_image.c
  libcss/src/select/properties/list_style_position.c
  libcss/src/select/properties/list_style_type.c
  libcss/src/select/properties/margin_bottom.c
  libcss/src/select/properties/margin_left.c
  libcss/src/select/properties/margin_right.c
  libcss/src/select/properties/margin_top.c
  libcss/src/select/properties/max_height.c
  libcss/src/select/properties/max_width.c
  libcss/src/select/properties/min_height.c
  libcss/src/select/properties/min_width.c
  libcss/src/select/properties/opacity.c
  libcss/src/select/properties/order.c
  libcss/src/select/properties/orphans.c
  libcss/src/select/properties/outline_color.c
  libcss/src/select/properties/outline_style.c
  libcss/src/select/properties/outline_width.c
  libcss/src/select/properties/overflow_x.c
  libcss/src/select/properties/overflow_y.c
  libcss/src/select/properties/padding_bottom.c
  libcss/src/select/properties/padding_left.c
  libcss/src/select/properties/padding_right.c
  libcss/src/select/properties/padding_top.c
  libcss/src/select/properties/page_break_after.c
  libcss/src/select/properties/page_break_before.c
  libcss/src/select/properties/page_break_inside.c
  libcss/src/select/properties/pause_after.c
  libcss/src/select/properties/pause_before.c
  libcss/src/select/properties/pitch.c
  libcss/src/select/properties/pitch_range.c
  libcss/src/select/properties/play_during.c
  libcss/src/select/properties/position.c
  libcss/src/select/properties/quotes.c
  libcss/src/select/properties/richness.c
  libcss/src/select/properties/right.c
  libcss/src/select/properties/row_gap.c
  libcss/src/select/properties/speech_rate.c
  libcss/src/select/properties/speak.c
  libcss/src/select/properties/speak_header.c
  libcss/src/select/properties/speak_numeral.c
  libcss/src/select/properties/speak_punctuation.c
  libcss/src/select/properties/stress.c
  libcss/src/select/properties/stroke_opacity.c
  libcss/src/select/properties/table_layout.c
  libcss/src/select/properties/text_align.c
  libcss/src/select/properties/text_decoration.c
  libcss/src/select/properties/text_indent.c
  libcss/src/select/properties/text_transform.c
  libcss/src/select/properties/top.c
  libcss/src/select/properties/unicode_bidi.c
  libcss/src/select/properties/vertical_align.c
  libcss/src/select/properties/visibility.c
  libcss/src/select/properties/voice_family.c
  libcss/src/select/properties/volume.c
  libcss/src/select/properties/white_space.c
  libcss/src/select/properties/widows.c
  libcss/src/select/properties/width.c
  libcss/src/select/properties/word_spacing.c
  libcss/src/select/properties/writing_mode.c
  libcss/src/select/properties/z_index.c
  libcss/src/utils/errors.c
  libcss/src/utils/utils.c
  ${CSS_GENERATED}
  DEFINES ${BSD_DEFINES} "_ALIGNED=__attribute__((aligned))"
  LINK libwapcaplet libparserutils)

nslib(libnsutils
  SOURCES
  libnsutils/src/base64.c
  libnsutils/src/time.c
  libnsutils/src/unistd.c
  DEFINES ${GNU_DEFINES})

nslib(libnsbmp
  SOURCES
  libnsbmp/src/libnsbmp.c
  DEFINES ${BSD_DEFINES})

nslib(libnsgif
  SOURCES
  libnsgif/src/gif.c
  libnsgif/src/lzw.c)

nslib(libsvgtiny
  SOURCES
  libsvgtiny/src/svgtiny.c
  libsvgtiny/src/svgtiny_gradient.c
  libsvgtiny/src/svgtiny_list.c
  libsvgtiny/src/svgtiny_parse.c
  libsvgtiny/src/svgtiny_path.c
  DEFINES ${BSD_DEFINES}
  LINK libdom libwapcaplet m)

nslib(libnspsl
  SOURCES
  libnspsl/src/nspsl.c
  DEFINES ${GNU_DEFINES})

set(NSLOG_GEN ${NSLIBS_GEN}/libnslog)
file(MAKE_DIRECTORY ${NSLOG_GEN})
bison_target(nslog_filter_parser libnslog/src/filter-parser.y
  ${NSLOG_GEN}/filter-parser.c
  DEFINES_FILE ${NSLOG_GEN}/filter-parser.h
  COMPILE_FLAGS "-t --define=api.prefix={filter_}")
flex_target(nslog_filter_lexer libnslog/src/filter-lexer.l
  ${NSLOG_GEN}/filter-lexer.c
  DEFINES_FILE ${NSLOG_GEN}/filter-lexer.h)
add_flex_bison_dependency(nslog_filter_lexer nslog_filter_parser)

nslib(libnslog
  SOURCES
  libnslog/src/core.c
  libnslog/src/filter.c
  ${BISON_nslog_filter_parser_OUTPUTS}
  ${FLEX_nslog_filter_lexer_OUTPUTS}
  DEFINES ${GNU_DEFINES}
  PRIVATE_INCLUDES ${NSLOG_GEN})

nslib(librosprite
  SOURCES
  librosprite/src/librosprite.c)

nslib(libutf8proc
  SOURCES
  libutf8proc/src/utf8proc.c
  DEFINES ${GNU_DEFINES})
target_compile_definitions(libutf8proc PUBLIC UTF8PROC_STATIC)

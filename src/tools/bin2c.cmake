file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR length "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " bytes "${content}")
string(REGEX REPLACE "(0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], 0x[0-9a-f][0-9a-f], )" "\\1\n  " bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp"
  "unsigned char ${NAME}[] = {\n  ${bytes}\n};\nunsigned int ${NAME}_len = ${length};\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")

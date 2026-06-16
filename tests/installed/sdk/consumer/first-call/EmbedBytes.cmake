if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "INPUT and OUTPUT are required")
endif()

file(READ "${INPUT}" bytes HEX)
if(bytes STREQUAL "")
    message(FATAL_ERROR "Cannot embed an empty module: ${INPUT}")
endif()
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUTPUT}"
    "/* Generated complete BEAM module. Include beam.h before this file. */\n"
    "static const unsigned char answer_beam_data[] = {\n${bytes}\n};\n"
    "static const beam_bytes answer_beam = {\n"
    "    answer_beam_data, sizeof answer_beam_data\n"
    "};\n")

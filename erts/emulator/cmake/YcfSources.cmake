if(NOT DEFINED YCF_SOURCE_DIR)
    message(FATAL_ERROR "YcfSources.cmake requires YCF_SOURCE_DIR")
endif()

set(YCF_TOOL_SOURCES
    "${YCF_SOURCE_DIR}/ycf_lexer.c"
    "${YCF_SOURCE_DIR}/ycf_main.c"
    "${YCF_SOURCE_DIR}/ycf_node.c"
    "${YCF_SOURCE_DIR}/ycf_parser.c"
    "${YCF_SOURCE_DIR}/ycf_printers.c"
    "${YCF_SOURCE_DIR}/ycf_string.c"
    "${YCF_SOURCE_DIR}/ycf_symbol.c"
    "${YCF_SOURCE_DIR}/ycf_utils.c"
    "${YCF_SOURCE_DIR}/ycf_yield_fun.c"
)

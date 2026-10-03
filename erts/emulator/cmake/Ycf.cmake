# Yielding C Fun host tool and the headers it writes for the emulator.

function(erts_ensure_ycf_host_tool)
    if(TARGET yielding_c_fun_host)
        return()
    endif()

    get_filename_component(YCF_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../lib_src/yielding_c_fun"
        REALPATH
    )
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/YcfSources.cmake)

    add_executable(yielding_c_fun_host ${YCF_TOOL_SOURCES})
    target_include_directories(yielding_c_fun_host PRIVATE "${YCF_SOURCE_DIR}")
endfunction()

function(erts_configure_ycf_headers target)
    if(NOT DEFINED EMULATOR_GENERATED_DIR)
        message(FATAL_ERROR "erts_configure_ycf_headers requires EMULATOR_GENERATED_DIR")
    endif()

    erts_ensure_ycf_host_tool()
    file(MAKE_DIRECTORY ${EMULATOR_GENERATED_DIR})

    set(UTILS_YCF_H ${EMULATOR_GENERATED_DIR}/utils.ycf.h)
    set(MAPS_YCF_H  ${EMULATOR_GENERATED_DIR}/erl_map.ycf.h)
    set(DB_YCF_H    ${EMULATOR_GENERATED_DIR}/erl_db_insert_list.ycf.h)

    add_custom_command(
        OUTPUT ${UTILS_YCF_H}
        COMMAND $<TARGET_FILE:yielding_c_fun_host>
            -yield -only_yielding_funs
            -f erts_qsort -f erts_qsort_helper -f erts_qsort_partion_array
            -output_file_name ${UTILS_YCF_H} ${CMAKE_CURRENT_SOURCE_DIR}/beam/utils.c
        DEPENDS yielding_c_fun_host ${CMAKE_CURRENT_SOURCE_DIR}/beam/utils.c
        COMMENT "Generating utils.ycf.h"
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${MAPS_YCF_H}
        COMMAND $<TARGET_FILE:yielding_c_fun_host>
            -yield -static_aux_funs -only_yielding_funs
            -fnoauto maps_keys_1_helper -fnoauto maps_values_1_helper -fnoauto maps_from_keys_2_helper -fnoauto maps_from_list_1_helper -fnoauto hashmap_from_validated_list
            -f hashmap_keys -f hashmap_values -f hashmap_from_unsorted_array -f hashmap_from_sorted_unique_array -f hashmap_from_chunked_array
            -fexternal erts_qsort
            -output_file_name ${MAPS_YCF_H} ${CMAKE_CURRENT_SOURCE_DIR}/beam/erl_map.c
        DEPENDS yielding_c_fun_host ${CMAKE_CURRENT_SOURCE_DIR}/beam/erl_map.c
        COMMENT "Generating erl_map.ycf.h"
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${DB_YCF_H}
        COMMAND $<TARGET_FILE:yielding_c_fun_host>
            -yield -static_aux_funs -only_yielding_funs
            -f ets_insert_2_list_check -f ets_insert_new_2_list_has_member -f ets_insert_2_list_from_p_heap -f ets_insert_2_list_destroy_copied_dbterms -f ets_insert_2_list_copy_term_list -f ets_insert_new_2_dbterm_list_has_member -f ets_insert_2_list_insert_db_term_list -f ets_insert_2_list
            -fnoauto ets_insert_2_list_lock_tbl
            -output_file_name ${DB_YCF_H} ${CMAKE_CURRENT_SOURCE_DIR}/beam/erl_db.c
        DEPENDS yielding_c_fun_host ${CMAKE_CURRENT_SOURCE_DIR}/beam/erl_db.c
        COMMENT "Generating erl_db_insert_list.ycf.h"
        VERBATIM
    )
    add_custom_target(ycf_headers_${ERTS_BUILD_FLAVOR}
        DEPENDS ${UTILS_YCF_H} ${MAPS_YCF_H} ${DB_YCF_H})
    add_dependencies(${target} ycf_headers_${ERTS_BUILD_FLAVOR})

    set_source_files_properties(beam/utils.c PROPERTIES OBJECT_DEPENDS ${UTILS_YCF_H})
    set_source_files_properties(beam/erl_map.c PROPERTIES OBJECT_DEPENDS ${MAPS_YCF_H})
    set_source_files_properties(beam/erl_db.c PROPERTIES OBJECT_DEPENDS ${DB_YCF_H})
endfunction()

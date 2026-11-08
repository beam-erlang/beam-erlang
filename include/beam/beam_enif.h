/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_ENIF_H
#define BEAM_ENIF_H

#include "nif/erl_nif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime-linked access to selected NIF operations. The corresponding NIF
 * operations define the required environment, term ownership and execution
 * context for each function. */
typedef ErlNifEnv BeamEnv;
typedef ERL_NIF_TERM BeamTerm;
typedef ErlNifSInt64 BeamSInt64;
typedef ErlNifUInt64 BeamUInt64;
typedef ErlNifTermType BeamTermType;
typedef ErlNifHash BeamHash;
typedef ErlNifCharEncoding BeamCharEncoding;
typedef ErlNifUniqueInteger BeamUniqueInteger;
typedef ErlNifMapIterator BeamMapIterator;
typedef ErlNifMapIteratorEntry BeamMapIteratorEntry;
typedef ErlNifBinary BeamBinary;
typedef ErlNifPid BeamPid;
typedef ErlNifPort BeamPort;
typedef ErlNifSysInfo BeamSysInfo;
typedef ErlNifTime BeamTime;
typedef ErlNifTimeUnit BeamTimeUnit;

/* Allocate a process-independent environment after beam_vm_init succeeds.
 * Use on the VM owner thread outside VM callbacks. Host environments may
 * hold ordinary terms without NIF resource references. Clear and free
 * require a non-NULL owned environment and invalidate its terms. Clear
 * retains it for reuse. Free before VM termination. */
BeamEnv *beam_enif_alloc_env(void) ERL_NAPI_ATTR_WUR;
void beam_enif_clear_env(BeamEnv *env);
void beam_enif_free_env(BeamEnv *env);

/* Native memory. */
void beam_enif_free(void *ptr);
void *beam_enif_alloc(size_t size) ERL_NAPI_ATTR_MALLOC_USD(1, beam_enif_free, 1);
void *beam_enif_realloc(void *ptr, size_t size) ERL_NAPI_ATTR_ALLOC_SIZE(2);

/* Term predicates and comparison. */
int beam_enif_is_atom(BeamEnv *env, BeamTerm term);
int beam_enif_is_binary(BeamEnv *env, BeamTerm term);
int beam_enif_is_ref(BeamEnv *env, BeamTerm term);
int beam_enif_is_fun(BeamEnv *env, BeamTerm term);
int beam_enif_is_pid(BeamEnv *env, BeamTerm term);
int beam_enif_is_port(BeamEnv *env, BeamTerm term);
int beam_enif_is_tuple(BeamEnv *env, BeamTerm term);
int beam_enif_is_list(BeamEnv *env, BeamTerm term);
int beam_enif_is_empty_list(BeamEnv *env, BeamTerm term);
int beam_enif_is_number(BeamEnv *env, BeamTerm term);
int beam_enif_is_map(BeamEnv *env, BeamTerm term);
BeamTermType beam_enif_term_type(BeamEnv *env, BeamTerm term);
size_t beam_enif_term_size(BeamTerm term);
int beam_enif_is_identical(BeamTerm lhs, BeamTerm rhs);
int beam_enif_compare(BeamTerm lhs, BeamTerm rhs);
BeamUInt64 beam_enif_hash(BeamHash type, BeamTerm term, BeamUInt64 salt);

/* Numbers. */
int beam_enif_get_int(BeamEnv *env, BeamTerm term, int *value);
int beam_enif_get_uint(BeamEnv *env, BeamTerm term, unsigned *value);
int beam_enif_get_long(BeamEnv *env, BeamTerm term, long *value);
int beam_enif_get_ulong(BeamEnv *env, BeamTerm term, unsigned long *value);
int beam_enif_get_int64(BeamEnv *env, BeamTerm term, BeamSInt64 *value);
int beam_enif_get_uint64(BeamEnv *env, BeamTerm term, BeamUInt64 *value);
int beam_enif_get_double(BeamEnv *env, BeamTerm term, double *value);
BeamTerm beam_enif_make_int(BeamEnv *env, int value);
BeamTerm beam_enif_make_uint(BeamEnv *env, unsigned value);
BeamTerm beam_enif_make_long(BeamEnv *env, long value);
BeamTerm beam_enif_make_ulong(BeamEnv *env, unsigned long value);
BeamTerm beam_enif_make_int64(BeamEnv *env, BeamSInt64 value);
BeamTerm beam_enif_make_uint64(BeamEnv *env, BeamUInt64 value);
BeamTerm beam_enif_make_double(BeamEnv *env, double value);

/* Atoms and strings. */
int beam_enif_get_atom(BeamEnv *env, BeamTerm atom, char *buffer, unsigned len,
                       BeamCharEncoding encoding);
int beam_enif_get_atom_length(BeamEnv *env, BeamTerm atom, unsigned *len,
                              BeamCharEncoding encoding);
int beam_enif_get_string(BeamEnv *env, BeamTerm list, char *buffer, unsigned len,
                         BeamCharEncoding encoding);
int beam_enif_get_string_length(BeamEnv *env, BeamTerm list, unsigned *len,
                                BeamCharEncoding encoding);
BeamTerm beam_enif_make_atom(BeamEnv *env, const char *name);
BeamTerm beam_enif_make_atom_len(BeamEnv *env, const char *name, size_t len);
int beam_enif_make_new_atom(BeamEnv *env, const char *name, BeamTerm *atom,
                            BeamCharEncoding encoding);
int beam_enif_make_new_atom_len(BeamEnv *env, const char *name, size_t len, BeamTerm *atom,
                                BeamCharEncoding encoding);
int beam_enif_make_existing_atom(BeamEnv *env, const char *name, BeamTerm *atom,
                                 BeamCharEncoding encoding);
int beam_enif_make_existing_atom_len(BeamEnv *env, const char *name, size_t len,
                                     BeamTerm *atom, BeamCharEncoding encoding);
BeamTerm beam_enif_make_string(BeamEnv *env, const char *string, BeamCharEncoding encoding);
BeamTerm beam_enif_make_string_len(BeamEnv *env, const char *string, size_t len,
                                   BeamCharEncoding encoding);

/* Copies, references and unique integers. */
BeamTerm beam_enif_make_copy(BeamEnv *dst_env, BeamTerm src_term);
BeamTerm beam_enif_make_ref(BeamEnv *env);
BeamTerm beam_enif_make_unique_integer(BeamEnv *env, BeamUniqueInteger properties);

/* Lists and tuples. */
int beam_enif_get_list_cell(BeamEnv *env, BeamTerm term, BeamTerm *head, BeamTerm *tail);
int beam_enif_get_list_length(BeamEnv *env, BeamTerm term, unsigned *len);
int beam_enif_get_tuple(BeamEnv *env, BeamTerm tuple, int *arity, const BeamTerm **array);
BeamTerm beam_enif_make_list(BeamEnv *env, unsigned count, ...);
BeamTerm beam_enif_make_list_cell(BeamEnv *env, BeamTerm car, BeamTerm cdr);
BeamTerm beam_enif_make_list_from_array(BeamEnv *env, const BeamTerm terms[],
                                        unsigned count);
int beam_enif_make_reverse_list(BeamEnv *env, BeamTerm term, BeamTerm *list);
BeamTerm beam_enif_make_tuple(BeamEnv *env, unsigned count, ...);
BeamTerm beam_enif_make_tuple_from_array(BeamEnv *env, const BeamTerm terms[],
                                         unsigned count);

/* Maps. */
int beam_enif_get_map_size(BeamEnv *env, BeamTerm term, size_t *size);
int beam_enif_get_map_value(BeamEnv *env, BeamTerm map, BeamTerm key, BeamTerm *value);
BeamTerm beam_enif_make_new_map(BeamEnv *env);
int beam_enif_make_map_put(BeamEnv *env, BeamTerm map_in, BeamTerm key, BeamTerm value,
                           BeamTerm *map_out);
int beam_enif_make_map_update(BeamEnv *env, BeamTerm map_in, BeamTerm key, BeamTerm value,
                              BeamTerm *map_out);
int beam_enif_make_map_remove(BeamEnv *env, BeamTerm map_in, BeamTerm key,
                              BeamTerm *map_out);
int beam_enif_make_map_from_arrays(BeamEnv *env, BeamTerm keys[], BeamTerm values[],
                                   size_t count, BeamTerm *map_out);
int beam_enif_map_iterator_create(BeamEnv *env, BeamTerm map, BeamMapIterator *iter,
                                  BeamMapIteratorEntry entry);
void beam_enif_map_iterator_destroy(BeamEnv *env, BeamMapIterator *iter);
int beam_enif_map_iterator_is_head(BeamEnv *env, BeamMapIterator *iter);
int beam_enif_map_iterator_is_tail(BeamEnv *env, BeamMapIterator *iter);
int beam_enif_map_iterator_next(BeamEnv *env, BeamMapIterator *iter);
int beam_enif_map_iterator_prev(BeamEnv *env, BeamMapIterator *iter);
int beam_enif_map_iterator_get_pair(BeamEnv *env, BeamMapIterator *iter, BeamTerm *key,
                                    BeamTerm *value);

/* Binaries and external term format. */
int beam_enif_alloc_binary(size_t size, BeamBinary *bin);
int beam_enif_realloc_binary(BeamBinary *bin, size_t size);
void beam_enif_release_binary(BeamBinary *bin);
int beam_enif_inspect_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin);
int beam_enif_inspect_iolist_as_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin);
BeamTerm beam_enif_make_binary(BeamEnv *env, BeamBinary *bin);
unsigned char *beam_enif_make_new_binary(BeamEnv *env, size_t size, BeamTerm *term);
BeamTerm beam_enif_make_sub_binary(BeamEnv *env, BeamTerm term, size_t pos, size_t size);
int beam_enif_term_to_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin);
size_t beam_enif_binary_to_term(BeamEnv *env, const unsigned char *data, size_t size,
                                BeamTerm *term, unsigned int opts);

/* Process and port operations. */
int beam_enif_get_local_pid(BeamEnv *env, BeamTerm term, BeamPid *pid);
BeamTerm beam_enif_make_pid(BeamEnv *env, const BeamPid *pid);
int beam_enif_compare_pids(const BeamPid *lhs, const BeamPid *rhs);
int beam_enif_get_local_port(BeamEnv *env, BeamTerm term, BeamPort *port_id);
void beam_enif_set_pid_undefined(BeamPid *pid);
int beam_enif_is_pid_undefined(const BeamPid *pid);
int beam_enif_whereis_pid(BeamEnv *env, BeamTerm name, BeamPid *pid);
int beam_enif_whereis_port(BeamEnv *env, BeamTerm name, BeamPort *port);
int beam_enif_is_process_alive(BeamEnv *env, BeamPid *pid);
int beam_enif_is_port_alive(BeamEnv *env, BeamPort *port_id);
int beam_enif_send(BeamEnv *env, const BeamPid *to_pid, BeamEnv *msg_env, BeamTerm msg);

/* System queries and formatting. */
int beam_enif_getenv(const char *key, char *value, size_t *value_size);
void beam_enif_system_info(BeamSysInfo *info, size_t size);
BeamTime beam_enif_convert_time_unit(BeamTime value, BeamTimeUnit from, BeamTimeUnit to);
int beam_enif_fprintf(FILE *file, const char *format, ...);
int beam_enif_snprintf(char *buffer, size_t size, const char *format, ...);
int beam_enif_vfprintf(FILE *file, const char *format, va_list args);
int beam_enif_vsnprintf(char *buffer, size_t size, const char *format, va_list args);

#ifdef __cplusplus
}
#endif

#endif

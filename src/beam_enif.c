/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam_enif.h>
#include "beam_enif_internal.h"

/* Environment lifetime. */
BeamEnv *beam_enif_alloc_env(void)
{
    return enif_alloc_env();
}

void beam_enif_clear_env(BeamEnv *env)
{
    enif_clear_env(env);
}

void beam_enif_free_env(BeamEnv *env)
{
    enif_free_env(env);
}

/* Native memory. */
void beam_enif_free(void *ptr)
{
    enif_free(ptr);
}

void *beam_enif_alloc(size_t size)
{
    return enif_alloc(size);
}

void *beam_enif_realloc(void *ptr, size_t size)
{
    return enif_realloc(ptr, size);
}

/* Term predicates and comparison. */
int beam_enif_is_atom(BeamEnv *env, BeamTerm term)
{
    return enif_is_atom(env, term);
}

int beam_enif_is_binary(BeamEnv *env, BeamTerm term)
{
    return enif_is_binary(env, term);
}

int beam_enif_is_ref(BeamEnv *env, BeamTerm term)
{
    return enif_is_ref(env, term);
}

int beam_enif_is_fun(BeamEnv *env, BeamTerm term)
{
    return enif_is_fun(env, term);
}

int beam_enif_is_pid(BeamEnv *env, BeamTerm term)
{
    return enif_is_pid(env, term);
}

int beam_enif_is_port(BeamEnv *env, BeamTerm term)
{
    return enif_is_port(env, term);
}

int beam_enif_is_tuple(BeamEnv *env, BeamTerm term)
{
    return enif_is_tuple(env, term);
}

int beam_enif_is_list(BeamEnv *env, BeamTerm term)
{
    return enif_is_list(env, term);
}

int beam_enif_is_empty_list(BeamEnv *env, BeamTerm term)
{
    return enif_is_empty_list(env, term);
}

int beam_enif_is_number(BeamEnv *env, BeamTerm term)
{
    return enif_is_number(env, term);
}

int beam_enif_is_map(BeamEnv *env, BeamTerm term)
{
    return enif_is_map(env, term);
}

BeamTermType beam_enif_term_type(BeamEnv *env, BeamTerm term)
{
    return enif_term_type(env, term);
}

size_t beam_enif_term_size(BeamTerm term)
{
    return enif_term_size(term);
}

int beam_enif_is_identical(BeamTerm lhs, BeamTerm rhs)
{
    return enif_is_identical(lhs, rhs);
}

int beam_enif_compare(BeamTerm lhs, BeamTerm rhs)
{
    return enif_compare(lhs, rhs);
}

BeamUInt64 beam_enif_hash(BeamHash type, BeamTerm term, BeamUInt64 salt)
{
    return enif_hash(type, term, salt);
}

/* Numbers. */
int beam_enif_get_int(BeamEnv *env, BeamTerm term, int *value)
{
    return enif_get_int(env, term, value);
}

int beam_enif_get_uint(BeamEnv *env, BeamTerm term, unsigned *value)
{
    return enif_get_uint(env, term, value);
}

int beam_enif_get_long(BeamEnv *env, BeamTerm term, long *value)
{
    return enif_get_long(env, term, value);
}

int beam_enif_get_ulong(BeamEnv *env, BeamTerm term, unsigned long *value)
{
    return enif_get_ulong(env, term, value);
}

int beam_enif_get_int64(BeamEnv *env, BeamTerm term, BeamSInt64 *value)
{
    return enif_get_int64(env, term, value);
}

int beam_enif_get_uint64(BeamEnv *env, BeamTerm term, BeamUInt64 *value)
{
    return enif_get_uint64(env, term, value);
}

int beam_enif_get_double(BeamEnv *env, BeamTerm term, double *value)
{
    return enif_get_double(env, term, value);
}

BeamTerm beam_enif_make_int(BeamEnv *env, int value)
{
    return enif_make_int(env, value);
}

BeamTerm beam_enif_make_uint(BeamEnv *env, unsigned value)
{
    return enif_make_uint(env, value);
}

BeamTerm beam_enif_make_long(BeamEnv *env, long value)
{
    return enif_make_long(env, value);
}

BeamTerm beam_enif_make_ulong(BeamEnv *env, unsigned long value)
{
    return enif_make_ulong(env, value);
}

BeamTerm beam_enif_make_int64(BeamEnv *env, BeamSInt64 value)
{
    return enif_make_int64(env, value);
}

BeamTerm beam_enif_make_uint64(BeamEnv *env, BeamUInt64 value)
{
    return enif_make_uint64(env, value);
}

BeamTerm beam_enif_make_double(BeamEnv *env, double value)
{
    return enif_make_double(env, value);
}

/* Atoms and strings. */
int beam_enif_get_atom(BeamEnv *env, BeamTerm atom, char *buffer, unsigned len,
                       BeamCharEncoding encoding)
{
    return enif_get_atom(env, atom, buffer, len, encoding);
}

int beam_enif_get_atom_length(BeamEnv *env, BeamTerm atom, unsigned *len,
                              BeamCharEncoding encoding)
{
    return enif_get_atom_length(env, atom, len, encoding);
}

int beam_enif_get_string(BeamEnv *env, BeamTerm list, char *buffer, unsigned len,
                         BeamCharEncoding encoding)
{
    return enif_get_string(env, list, buffer, len, encoding);
}

int beam_enif_get_string_length(BeamEnv *env, BeamTerm list, unsigned *len,
                                BeamCharEncoding encoding)
{
    return enif_get_string_length(env, list, len, encoding);
}

BeamTerm beam_enif_make_atom(BeamEnv *env, const char *name)
{
    return enif_make_atom(env, name);
}

BeamTerm beam_enif_make_atom_len(BeamEnv *env, const char *name, size_t len)
{
    return enif_make_atom_len(env, name, len);
}

int beam_enif_make_new_atom(BeamEnv *env, const char *name, BeamTerm *atom,
                            BeamCharEncoding encoding)
{
    return enif_make_new_atom(env, name, atom, encoding);
}

int beam_enif_make_new_atom_len(BeamEnv *env, const char *name, size_t len, BeamTerm *atom,
                                BeamCharEncoding encoding)
{
    return enif_make_new_atom_len(env, name, len, atom, encoding);
}

int beam_enif_make_existing_atom(BeamEnv *env, const char *name, BeamTerm *atom,
                                 BeamCharEncoding encoding)
{
    return enif_make_existing_atom(env, name, atom, encoding);
}

int beam_enif_make_existing_atom_len(BeamEnv *env, const char *name, size_t len,
                                     BeamTerm *atom, BeamCharEncoding encoding)
{
    return enif_make_existing_atom_len(env, name, len, atom, encoding);
}

BeamTerm beam_enif_make_string(BeamEnv *env, const char *string, BeamCharEncoding encoding)
{
    return enif_make_string(env, string, encoding);
}

BeamTerm beam_enif_make_string_len(BeamEnv *env, const char *string, size_t len,
                                   BeamCharEncoding encoding)
{
    return enif_make_string_len(env, string, len, encoding);
}

/* Copies, references and unique integers. */
BeamTerm beam_enif_make_copy(BeamEnv *dst_env, BeamTerm src_term)
{
    return enif_make_copy(dst_env, src_term);
}

BeamTerm beam_enif_make_ref(BeamEnv *env)
{
    return enif_make_ref(env);
}

BeamTerm beam_enif_make_unique_integer(BeamEnv *env, BeamUniqueInteger properties)
{
    return enif_make_unique_integer(env, properties);
}

/* Lists and tuples. */
int beam_enif_get_list_cell(BeamEnv *env, BeamTerm term, BeamTerm *head, BeamTerm *tail)
{
    return enif_get_list_cell(env, term, head, tail);
}

int beam_enif_get_list_length(BeamEnv *env, BeamTerm term, unsigned *len)
{
    return enif_get_list_length(env, term, len);
}

int beam_enif_get_tuple(BeamEnv *env, BeamTerm tuple, int *arity, const BeamTerm **array)
{
    return enif_get_tuple(env, tuple, arity, array);
}

BeamTerm beam_enif_make_list(BeamEnv *env, unsigned count, ...)
{
    va_list args;
    va_start(args, count);
    BeamTerm result = erts_nif_vmake_list(env, count, args);
    va_end(args);
    return result;
}

BeamTerm beam_enif_make_list_cell(BeamEnv *env, BeamTerm car, BeamTerm cdr)
{
    return enif_make_list_cell(env, car, cdr);
}

BeamTerm beam_enif_make_list_from_array(BeamEnv *env, const BeamTerm terms[],
                                        unsigned count)
{
    return enif_make_list_from_array(env, terms, count);
}

int beam_enif_make_reverse_list(BeamEnv *env, BeamTerm term, BeamTerm *list)
{
    return enif_make_reverse_list(env, term, list);
}

BeamTerm beam_enif_make_tuple(BeamEnv *env, unsigned count, ...)
{
    va_list args;
    va_start(args, count);
    BeamTerm result = erts_nif_vmake_tuple(env, count, args);
    va_end(args);
    return result;
}

BeamTerm beam_enif_make_tuple_from_array(BeamEnv *env, const BeamTerm terms[],
                                         unsigned count)
{
    return enif_make_tuple_from_array(env, terms, count);
}

/* Maps. */
int beam_enif_get_map_size(BeamEnv *env, BeamTerm term, size_t *size)
{
    return enif_get_map_size(env, term, size);
}

int beam_enif_get_map_value(BeamEnv *env, BeamTerm map, BeamTerm key, BeamTerm *value)
{
    return enif_get_map_value(env, map, key, value);
}

BeamTerm beam_enif_make_new_map(BeamEnv *env)
{
    return enif_make_new_map(env);
}

int beam_enif_make_map_put(BeamEnv *env, BeamTerm map_in, BeamTerm key, BeamTerm value,
                           BeamTerm *map_out)
{
    return enif_make_map_put(env, map_in, key, value, map_out);
}

int beam_enif_make_map_update(BeamEnv *env, BeamTerm map_in, BeamTerm key, BeamTerm value,
                              BeamTerm *map_out)
{
    return enif_make_map_update(env, map_in, key, value, map_out);
}

int beam_enif_make_map_remove(BeamEnv *env, BeamTerm map_in, BeamTerm key,
                              BeamTerm *map_out)
{
    return enif_make_map_remove(env, map_in, key, map_out);
}

int beam_enif_make_map_from_arrays(BeamEnv *env, BeamTerm keys[], BeamTerm values[],
                                   size_t count, BeamTerm *map_out)
{
    return enif_make_map_from_arrays(env, keys, values, count, map_out);
}

int beam_enif_map_iterator_create(BeamEnv *env, BeamTerm map, BeamMapIterator *iter,
                                  BeamMapIteratorEntry entry)
{
    return enif_map_iterator_create(env, map, iter, entry);
}

void beam_enif_map_iterator_destroy(BeamEnv *env, BeamMapIterator *iter)
{
    enif_map_iterator_destroy(env, iter);
}

int beam_enif_map_iterator_is_head(BeamEnv *env, BeamMapIterator *iter)
{
    return enif_map_iterator_is_head(env, iter);
}

int beam_enif_map_iterator_is_tail(BeamEnv *env, BeamMapIterator *iter)
{
    return enif_map_iterator_is_tail(env, iter);
}

int beam_enif_map_iterator_next(BeamEnv *env, BeamMapIterator *iter)
{
    return enif_map_iterator_next(env, iter);
}

int beam_enif_map_iterator_prev(BeamEnv *env, BeamMapIterator *iter)
{
    return enif_map_iterator_prev(env, iter);
}

int beam_enif_map_iterator_get_pair(BeamEnv *env, BeamMapIterator *iter, BeamTerm *key,
                                    BeamTerm *value)
{
    return enif_map_iterator_get_pair(env, iter, key, value);
}

/* Binaries and external term format. */
int beam_enif_alloc_binary(size_t size, BeamBinary *bin)
{
    return enif_alloc_binary(size, bin);
}

int beam_enif_realloc_binary(BeamBinary *bin, size_t size)
{
    return enif_realloc_binary(bin, size);
}

void beam_enif_release_binary(BeamBinary *bin)
{
    enif_release_binary(bin);
}

int beam_enif_inspect_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin)
{
    return enif_inspect_binary(env, term, bin);
}

int beam_enif_inspect_iolist_as_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin)
{
    return enif_inspect_iolist_as_binary(env, term, bin);
}

BeamTerm beam_enif_make_binary(BeamEnv *env, BeamBinary *bin)
{
    return enif_make_binary(env, bin);
}

unsigned char *beam_enif_make_new_binary(BeamEnv *env, size_t size, BeamTerm *term)
{
    return enif_make_new_binary(env, size, term);
}

BeamTerm beam_enif_make_sub_binary(BeamEnv *env, BeamTerm term, size_t pos, size_t size)
{
    return enif_make_sub_binary(env, term, pos, size);
}

int beam_enif_term_to_binary(BeamEnv *env, BeamTerm term, BeamBinary *bin)
{
    return enif_term_to_binary(env, term, bin);
}

size_t beam_enif_binary_to_term(BeamEnv *env, const unsigned char *data, size_t size,
                                BeamTerm *term, unsigned int opts)
{
    return enif_binary_to_term(env, data, size, term, opts);
}

/* Process and port operations. */
int beam_enif_get_local_pid(BeamEnv *env, BeamTerm term, BeamPid *pid)
{
    return enif_get_local_pid(env, term, pid);
}

BeamTerm beam_enif_make_pid(BeamEnv *env, const BeamPid *pid)
{
    return enif_make_pid(env, pid);
}

int beam_enif_compare_pids(const BeamPid *lhs, const BeamPid *rhs)
{
    return enif_compare_pids(lhs, rhs);
}

int beam_enif_get_local_port(BeamEnv *env, BeamTerm term, BeamPort *port_id)
{
    return enif_get_local_port(env, term, port_id);
}

void beam_enif_set_pid_undefined(BeamPid *pid)
{
    enif_set_pid_undefined(pid);
}

int beam_enif_is_pid_undefined(const BeamPid *pid)
{
    return enif_is_pid_undefined(pid);
}

int beam_enif_whereis_pid(BeamEnv *env, BeamTerm name, BeamPid *pid)
{
    return enif_whereis_pid(env, name, pid);
}

int beam_enif_whereis_port(BeamEnv *env, BeamTerm name, BeamPort *port)
{
    return enif_whereis_port(env, name, port);
}

int beam_enif_is_process_alive(BeamEnv *env, BeamPid *pid)
{
    return enif_is_process_alive(env, pid);
}

int beam_enif_is_port_alive(BeamEnv *env, BeamPort *port_id)
{
    return enif_is_port_alive(env, port_id);
}

int beam_enif_send(BeamEnv *env, const BeamPid *to_pid, BeamEnv *msg_env, BeamTerm msg)
{
    return enif_send(env, to_pid, msg_env, msg);
}

/* System queries and formatting. */
int beam_enif_getenv(const char *key, char *value, size_t *value_size)
{
    return enif_getenv(key, value, value_size);
}

void beam_enif_system_info(BeamSysInfo *info, size_t size)
{
    enif_system_info(info, size);
}

BeamTime beam_enif_convert_time_unit(BeamTime value, BeamTimeUnit from, BeamTimeUnit to)
{
    return enif_convert_time_unit(value, from, to);
}

int beam_enif_fprintf(FILE *file, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = enif_vfprintf(file, format, args);
    va_end(args);
    return result;
}

int beam_enif_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = enif_vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int beam_enif_vfprintf(FILE *file, const char *format, va_list args)
{
    return enif_vfprintf(file, format, args);
}

int beam_enif_vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    return enif_vsnprintf(buffer, size, format, args);
}

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_INTERNAL_H
#define BEAM_INTERNAL_H

/* Private interface between the C API implementation and ERTS. Not installed. */
#include "beam.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int erts_bare_mode;
extern int erts_bare_destroying;
void erts_bare_init(const beam_vm_options *options);
beam_status erts_bare_start(const beam_entry *entry);
int erts_vm_process_owned(void);
int erts_vm_native_admitted(unsigned permission);
int erts_vm_process_policy(unsigned policy);
beam_break_policy erts_vm_break_policy(void);
const char *erts_vm_program_name(void);
int erts_vm_filename_utf8(void);
/* Immutable VM-owned composition inventory; no acquisition or host callbacks. */
size_t erts_vm_preloads(const beam_module_spec **modules);
size_t erts_vm_builtin_nifs(const beam_builtin_nif **entries);
size_t erts_vm_builtin_drivers(const beam_builtin_driver **entries);
void erts_vm_native_init_failed(const char *kind, const char *name);
void erts_sys_process_start(void);
void erts_sys_process_ready(void);
beam_status erts_bare_load_initial(beam_bytes module);
/* Running module helpers require code-load permission on a scheduler. Initial
 * helpers run on the owner with initial code staging still open. */
beam_status erts_bare_get_module(const char *name, int initial,
                                beam_module_info *out);
beam_status erts_bare_list_modules(int initial, beam_module_list *out);
beam_status erts_bare_unload_module(const char *name, int initial);
void erts_bare_get_schedulers(beam_scheduler_info *out);
beam_status erts_bare_set_schedulers_online(unsigned normal, unsigned dirty,
                                           void (*complete)(void *, beam_status),
                                           void *argument);
void erts_bare_cancel_scheduler_change(void);
void erts_bare_set_creation(unsigned creation);
void erts_bare_destroy_runtime(int started);
void erts_bare_destroy_term_roots(void);
void erts_bare_destroy_code(void);
void erts_bare_destroy_nif_registry(void);
void erts_bare_destroy_builtin_drivers(void);
void erts_bare_destroy_trace(void);
void erts_bare_destroy_async(int started);
void erts_bare_destroy_timers(void);
void erts_bare_destroy_scheduling(int started);
void erts_bare_destroy_cpu_topology(void);
void erts_bare_reset_init(void);
void erts_bare_reset_errno(void);
void erts_bare_destroy_msacc(void);
void erts_bare_begin_allocator_reclamation(void);
void erts_bare_reset_allocators(void);
void erts_bare_reset_mseg(void);
void erts_bare_reset_mmap(void);
void erts_bare_reset_node_tables(void);
void erts_sys_vm_prepare(void);
int erts_sys_system_cleanup(void);
int erts_bare_check_signal_handlers(void);
void erts_bare_sys_cleanup(void);
void erts_bare_request_stop(void);
void erts_bare_stop_runtime(void);
int erts_bare_stop_requested(void);
int erts_bare_is_stopped(void);
int erts_bare_threads_stopping(void);
void erts_bare_thread_rendezvous(void);
void erts_bare_trace_wakeup(void);
void erts_bare_trace_join(void);

/* Shared process-wide start admission, including the booting profile. */
int beam_start_claimed(void);
int beam_claim_start(void);
void beam_release_start(void);

/* Called only from ERTS after sys.h/global.h define Eterm and Process. */
void erts_bare_process_return(struct process *p, Eterm value);
int erts_bare_process_exception(struct process *p, Eterm exception_class,
                              Eterm reason, Eterm trace);
void erts_bare_process_exiting(struct process *p, Eterm reason);
void erts_bare_process_deleted(struct process *p);
void erts_bare_halt(int status);
int erts_bare_host_replies_pending(void);

/* Selected native admission. Configuration is retained across initialization;
 * explicit start creates the program helper before workers. Counters are
 * intentionally excluded from begin/end. */
void erts_components_begin(unsigned services, const char *program_path);
const char *erts_components_program_path(void);
void erts_components_end(void);
int erts_components_active(void);
int erts_components_program_enabled(void);
int erts_components_has_service(unsigned service);
void erts_components_set_cleanup(const char *bytes, size_t size);
void erts_components_zlib_adjust(int delta);
void erts_components_file_adjust(int delta);
void erts_components_socket_adjust(int delta);

#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_PROCESS_MANAGEMENT_H
#define BEAM_PROCESS_MANAGEMENT_H

#include <beam/process.h>

/* Private boundary, included after the ERTS types. Atom identities are
 * immediate and remain valid until owner-thread VM termination. No process or
 * term-heap pointer is retained by an observation. */
typedef struct beam_process_observation {
    Eterm pid;
    int is_system_process;
    unsigned fields;
    beam_process_state state;
    uint64_t reductions;
    size_t memory_bytes;
    size_t message_queue_len;
    Eterm module;
    Eterm function;
    unsigned arity;
    int has_registered_name;
    Eterm registered_name;
} beam_process_observation;

#define BEAM_PROCESS_FIELDS_ALL \
    (BEAM_PROCESS_FIELD_STATE | BEAM_PROCESS_FIELD_REDUCTIONS | \
     BEAM_PROCESS_FIELD_MEMORY | BEAM_PROCESS_FIELD_MESSAGE_QUEUE | \
     BEAM_PROCESS_FIELD_INITIAL_CALL | BEAM_PROCESS_FIELD_REGISTERED_NAME)

/* Owner allocates signal storage before dispatch and discards it after all
 * native references retire (or before submission). Send takes one native
 * context reference; result and release each run exactly once, including failed
 * enqueue. The sending command holds an independent reference until it returns.
 * The target never frees owner-allocated storage or accesses it after release. */
beam_status erts_bare_process_inspection_prepare(unsigned fields, void *context,
                                                 void **out_signal);
void erts_bare_process_inspection_discard(void *signal);
void erts_bare_process_inspection_send(Eterm target, void *signal);
void erts_bare_process_inspection_retain(void *context);
void erts_bare_process_inspection_result(void *context, beam_status status,
                                        const beam_process_observation *result);
void erts_bare_process_inspection_release(void *context);

int erts_bare_process_inspection_flags(unsigned fields);
beam_status erts_bare_process_observe(Process *process, unsigned fields,
                                      beam_process_observation *out, Uint *reds);

/* One exclusive-lock membership observation. Required count is returned on
 * success, even if it exceeds capacity; in that case no PID is written. */
beam_status erts_bare_process_list_capture(BeamPid *pids, size_t capacity,
                                          size_t *count);

#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_PROCESS_H
#define BEAM_PROCESS_H

#include <stdint.h>
#include "runtime.h"
#include "beam_enif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental host-controlled management API. Calls use the VM owner thread
 * outside callbacks. Queries are available while an asynchronous entry is
 * outstanding; spawn returns BEAM_BUSY in that condition. A running VM is
 * required. Stop/halt returns BEAM_STOPPED without implicitly joining workers.
 * A synchronous inspection can require target cooperation and has no bounded
 * latency guarantee. No operation implicitly services host callbacks. */
typedef struct beam_process_list {
    size_t count;
    const BeamPid *pids;
} beam_process_list;

typedef enum beam_process_state {
    BEAM_PROCESS_RUNNABLE = 1,
    BEAM_PROCESS_RUNNING,
    BEAM_PROCESS_WAITING,
    BEAM_PROCESS_GARBAGE_COLLECTING,
    BEAM_PROCESS_SUSPENDED,
    BEAM_PROCESS_EXITING
} beam_process_state;

typedef enum beam_process_fields {
    BEAM_PROCESS_FIELD_STATE = 1u << 0,
    BEAM_PROCESS_FIELD_REDUCTIONS = 1u << 1,
    BEAM_PROCESS_FIELD_MEMORY = 1u << 2,
    BEAM_PROCESS_FIELD_MESSAGE_QUEUE = 1u << 3,
    BEAM_PROCESS_FIELD_INITIAL_CALL = 1u << 4,
    BEAM_PROCESS_FIELD_REGISTERED_NAME = 1u << 5
} beam_process_fields;

typedef struct beam_process_query {
    size_t struct_size;
    unsigned fields;
} beam_process_query;

typedef struct beam_process_atom_name {
    const char *data;
    size_t size;                 /* UTF-8 byte length; embedded NUL is valid. */
} beam_process_atom_name;

typedef struct beam_process_mfa {
    beam_process_atom_name module;
    beam_process_atom_name function;
    unsigned arity;
} beam_process_mfa;

typedef struct beam_process_snapshot {
    BeamPid pid;
    int is_system_process;
    unsigned fields;
    beam_process_state state;
    uint64_t reductions;         /* Existing runtime work accounting. */
    size_t memory_bytes;        /* Process accounting, not RSS. */
    size_t message_queue_len;   /* Process-info message count. */
    beam_process_mfa initial_call;
    int has_registered_name;
    beam_process_atom_name registered_name;
} beam_process_snapshot;

/* Copied results are zero-initialized by the caller. Release an earlier result
 * before reuse. Failure leaves outputs unchanged. NULL/empty release is safe.
 * Lists capture table membership at one instant; later inspection may return
 * BEAM_NOT_FOUND. Runtime processes are included. Results may outlive their VM
 * and system while libbeam remains loaded; their PIDs are then historical and
 * must not be submitted to a later VM. Copying a PID does not retain a process. */
beam_status beam_process_list_pids(beam_vm *vm, beam_process_list *out);
void beam_process_list_release(beam_process_list *list);
/* Exact structure size and fields=0 (identity and system-process flag only). */
void beam_process_query_init(beam_process_query *query);
/* Unknown field bits/incorrect size are invalid. Success supplies every
 * requested field and zeroes unrequested groups. Absent targets return
 * BEAM_NOT_FOUND. An empty registered atom is distinguished from absence by
 * has_registered_name. */
beam_status beam_process_inspect(beam_vm *vm, const BeamPid *target,
                                 const beam_process_query *query,
                                 beam_process_snapshot *out);
void beam_process_snapshot_release(beam_process_snapshot *snapshot);

typedef struct beam_process_spawn_options {
    size_t struct_size;
    BeamPid group_leader;
} beam_process_spawn_options;

/* Exact structure size and undefined group_leader (VM-root default). */
void beam_process_spawn_options_init(beam_process_spawn_options *options);
/* Copies a proper native argument list without consuming its valid host
 * environment. Names are nonempty UTF-8 C strings. An explicit group leader
 * must be a live local process at admission; it is not retained alive.
 * Success returns the PID at publication, even if the child exits immediately.
 * Missing exports follow ordinary child undef behavior. No code or services
 * are acquired. Native terms/environments retain their VM-bound lifetime. */
beam_status beam_process_spawn(beam_vm *vm, const char *module,
                               const char *function, BeamEnv *arguments_env,
                               BeamTerm arguments,
                               const beam_process_spawn_options *options,
                               BeamPid *out, beam_error *error);

#ifdef __cplusplus
}
#endif

#endif

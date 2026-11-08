/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_MODULE_MANAGEMENT_H
#define BEAM_MODULE_MANAGEMENT_H

/* Private scheduler-side management boundary. Include after the ERTS types. */
beam_status erts_bare_replace_prepared(Binary *prepared, Eterm module);
beam_status erts_bare_delete_module(beam_module_name name);
beam_status erts_bare_module_name_atom(beam_module_name name, Eterm *out);
int erts_bare_module_purge_request_deleted(Process *process);
void erts_bare_module_purge_reset_after_stop(void);
int erts_bare_is_module_purge_request(Process *process);
void erts_bare_module_purge_retired(Eterm requester);

#endif

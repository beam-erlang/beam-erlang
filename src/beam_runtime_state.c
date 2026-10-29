/* SPDX-License-Identifier: Apache-2.0 */
/* Owner-thread configuration, held by the retained VM composition. */
static const char *program_path;
void erts_vm_native_begin(const char *helper) { program_path = helper; }
void erts_vm_native_end(void) { program_path = 0; }
const char *erts_vm_program_path(void) { return program_path; }

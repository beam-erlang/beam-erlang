/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_RUNTIME_IMAGE_H
#define BEAM_RUNTIME_IMAGE_H
/* Private final-link contract. These symbols must never interpose across VMs. */
__attribute__((visibility("hidden"))) int beam_runtime_is_static(void);
__attribute__((visibility("hidden"))) int beam_runtime_mapping_matches(void *handle);
#define BEAM_RUNTIME_IMAGE_ERROR "native library depends on another BEAM runtime image"
#endif

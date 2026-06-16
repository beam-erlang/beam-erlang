/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_RUNTIME_IMAGE_H
#define BEAM_RUNTIME_IMAGE_H
#include <stddef.h>
/* Private final-link contract. These symbols must never interpose across VMs. */
#ifdef _MSC_VER
#define BEAM_IMAGE_PRIVATE
#else
#define BEAM_IMAGE_PRIVATE __attribute__((visibility("hidden")))
#endif
BEAM_IMAGE_PRIVATE int beam_runtime_is_static(void);
BEAM_IMAGE_PRIVATE int beam_runtime_mapping_matches(void *handle);
BEAM_IMAGE_PRIVATE int beam_runtime_preflight(const char *path, char *error, size_t error_size);
/* Allocated canonical file or Android archive-member location; errno on failure. */
BEAM_IMAGE_PRIVATE char *beam_runtime_image_location(const char *location);
#define BEAM_RUNTIME_IMAGE_ERROR "native library depends on another BEAM runtime image"
#endif

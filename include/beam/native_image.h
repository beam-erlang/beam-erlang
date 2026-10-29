/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_NATIVE_IMAGE_H
#define BEAM_NATIVE_IMAGE_H
#include "runtime.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Generic native-code admission. Images are trusted code: platform constructors
 * may run when mapped. No VM, Erlang code or NIF load callback is started.
 * Locations and their dependency/search configuration must remain immutable.
 * An image resolving another runtime is rejected using the ordinary VM loader's
 * inspection rules. This interface interprets no library/container descriptor. */
typedef struct beam_native_image beam_native_image;
beam_status beam_native_image_open(const char *location, beam_native_image **out,
                                   beam_error *error);
void beam_native_image_retain(beam_native_image *image);
void beam_native_image_release(beam_native_image **image);
const char *beam_native_image_path(const beam_native_image *image);
beam_status beam_native_image_symbol(const beam_native_image *image,
                                     const char *name, void **out);
/* Borrow an owner record. Composition preparation retains it independently;
 * release the caller's image only after that preparation has succeeded. */
beam_status beam_native_image_owner(beam_native_image *image, beam_native_owner *out);
/* Constant for this runtime image; valid without an initialized system. */
int beam_runtime_static(void);

#ifdef __cplusplus
}
#endif
#endif

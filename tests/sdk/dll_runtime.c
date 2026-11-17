/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>

/* Reference the entry so the consumer also works with archive extraction
 * policies other than WHOLE_ARCHIVE. The entry itself is the public export. */
int (*beam_sdk_entry)(int, char **) = beam_main;

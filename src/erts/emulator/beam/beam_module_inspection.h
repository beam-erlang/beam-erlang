/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_MODULE_INSPECTION_H
#define BEAM_MODULE_INSPECTION_H

#include <beam/runtime.h>

/* Running callers own code loading permission. Initial callers own the open
 * staging index before workers start. NULL name selects the complete scope. */
beam_status erts_bare_inspect_modules(const beam_module_name *name,
                                      const beam_module_query *query,
                                      int initial,
                                      beam_module_snapshot *out);

#endif

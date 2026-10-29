/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <beam/native.h>

beam_native_trace_backend beam_native_trace_backend_get(void)
{
#if defined(USE_DTRACE)
    return BEAM_NATIVE_TRACE_DTRACE;
#elif defined(USE_SYSTEMTAP)
    return BEAM_NATIVE_TRACE_SYSTEMTAP;
#elif defined(USE_LTTNG)
    return BEAM_NATIVE_TRACE_LTTNG;
#else
    return BEAM_NATIVE_TRACE_NONE;
#endif
}

/* The stop bit stays set while quiescent NIF destructors execute. */
int beam_native_stop_requested(void)
{
    extern int erts_bare_stop_requested(void);
    extern int erts_bare_destroying;
    return erts_bare_stop_requested() || erts_bare_destroying;
}

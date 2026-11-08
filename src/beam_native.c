/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "global.h"
#include "erl_process.h"
#include <beam/native.h>
#include <stdint.h>

int beam_native_stack_has_space(size_t reserve_bytes)
{
    char current;
    const char *limit;
    uintptr_t here = (uintptr_t)&current;
    uintptr_t bound;

    /* The scheduler TLS key does not exist before VM initialization. */
    if (!erts_initialized || !erts_get_current_process())
        return -1;
    limit = erts_get_stacklimit();
    if (!limit)
        return -1;
    bound = (uintptr_t)limit;
    if (erts_check_if_stack_grows_downwards(&current))
        return here >= bound && reserve_bytes <= here - bound;
    return here <= bound && reserve_bytes <= bound - here;
}

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

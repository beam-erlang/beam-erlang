/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "beam_runtime_image.h"
#include <dlfcn.h>

int beam_runtime_is_static(void)
{
#ifdef BEAM_STATIC_RUNTIME
    return 1;
#else
    return 0;
#endif
}

int beam_runtime_mapping_matches(void *handle)
{
    Dl_info current, selected;
    void *symbol;
    dlerror();
    symbol = dlsym(handle, "beam_build_id");
    /* Runtime-neutral extensions import native APIs from the calling host. */
    if (!symbol) return 1;
    return dladdr((void *)&beam_runtime_is_static, &current) &&
        dladdr(symbol, &selected) && current.dli_fbase == selected.dli_fbase;
}

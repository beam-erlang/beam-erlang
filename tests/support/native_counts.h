/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_TEST_NATIVE_COUNTS_H
#define BEAM_TEST_NATIVE_COUNTS_H
#ifdef BEAM_TEST_STATIC_LIBRARIES
int beam_file_descriptor_count(void);
int beam_socket_count(void);
int beam_zlib_stream_count(void);
#else
/* Counters belong to their native images. Observe an already mapped image
 * without acquiring an unselected library. OS resource checks remain separate. */
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static int beam_test_native_count(const char *image, const char *symbol)
{
    HMODULE handle=GetModuleHandleA(image);
    if(!handle)return 0;
    FARPROC address=GetProcAddress(handle,symbol);
    int (*count)(void)=NULL;
    memcpy(&count,&address,sizeof(count));
    return count?count():-1;
}
#define BEAM_TEST_COUNT_PREFIX ""
#define BEAM_TEST_COUNT_SUFFIX ".dll"
#else
#include <dlfcn.h>
static int beam_test_native_count(const char *image, const char *symbol)
{
    void *handle=dlopen(image,RTLD_NOW|RTLD_NOLOAD);
    if(!handle)return 0;
    void *address=dlsym(handle,symbol);
    int (*count)(void)=NULL;
    memcpy(&count,&address,sizeof(count));
    int value=count?count():-1;
    dlclose(handle);return value;
}
#define BEAM_TEST_COUNT_PREFIX "lib"
#ifdef __APPLE__
#define BEAM_TEST_COUNT_SUFFIX ".dylib"
#else
#define BEAM_TEST_COUNT_SUFFIX ".so"
#endif
#endif
static int beam_file_descriptor_count(void)
{
    return beam_test_native_count(BEAM_TEST_COUNT_PREFIX "beam-file" BEAM_TEST_COUNT_SUFFIX,"beam_file_descriptor_count")+
        beam_test_native_count(BEAM_TEST_COUNT_PREFIX "beam-file-contract-test" BEAM_TEST_COUNT_SUFFIX,"beam_file_descriptor_count");
}
static int beam_socket_count(void)
{
    return beam_test_native_count(BEAM_TEST_COUNT_PREFIX "beam-network" BEAM_TEST_COUNT_SUFFIX,"beam_socket_count");
}
static int beam_zlib_stream_count(void)
{
    return beam_test_native_count(BEAM_TEST_COUNT_PREFIX "beam-zlib" BEAM_TEST_COUNT_SUFFIX,"beam_zlib_stream_count");
}
#endif
#endif

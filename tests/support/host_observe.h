/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_TEST_HOST_OBSERVE_H
#define BEAM_TEST_HOST_OBSERVE_H
#include <stddef.h>
#include <sys/types.h>
#include <string.h>

/* Test-only OS observations. Errors return -1, never an empty observation. */
int beam_test_host_fds(pid_t pid, int **values, size_t *count);
int beam_test_host_children(pid_t pid, int **values, size_t *count);
int beam_test_host_threads(pid_t pid);
int beam_test_host_executable_regions(void);
int beam_test_host_entries(const char *path);
int beam_test_host_copy_fds(int *values, size_t capacity);
int beam_test_host_child_text(char *text, size_t capacity);
#ifdef __APPLE__
int beam_test_host_runtime_images(void);
int beam_test_host_mapped_files(pid_t pid, char ***paths, size_t *count);
void beam_test_host_free_paths(char **paths, size_t count);
#endif
int beam_test_host_executable(pid_t pid, char *path, size_t capacity);
int beam_test_host_image(const void *address, char *path, size_t capacity);
int beam_test_host_state(pid_t pid, int *alive, int *zombie);
#ifdef BEAM_TEST_FILE_RELATIVE
static inline const char *beam_test_component(const char *name) {
    return !strcmp(name,"file") ? BEAM_TEST_FILE_RELATIVE :
        !strcmp(name,"network") ? BEAM_TEST_NETWORK_RELATIVE : BEAM_TEST_ZLIB_RELATIVE;
}
#endif
#endif

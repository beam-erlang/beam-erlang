/* SPDX-License-Identifier: Apache-2.0 */
/* Filesystem isolation must reject real inputs in children and descendants. */
#ifdef BEAM_ISOLATION_LIBRARY
__attribute__((visibility("default"))) int beam_isolation_value(void) { return 73; }
#else
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int denied(int error) { return error == EPERM || error == EACCES; }

static int observe(char **paths, int restricted)
{
    int fd = open(paths[0], O_RDONLY);
    int error = errno;
    if (fd >= 0) close(fd);
    if (restricted ? fd >= 0 || !denied(error) : fd < 0) return 1;
    struct stat info;
    int result = stat(paths[0], &info);
    error = errno;
    if (restricted ? result == 0 || !denied(error) : result != 0) return 2;
    void *library = dlopen(paths[2], RTLD_NOW | RTLD_LOCAL);
    if (restricted) {
        if (library) { dlclose(library); return 3; }
    } else {
        if (!library) { fprintf(stderr, "%s\n", dlerror()); return 4; }
        int (*value)(void) = (int (*)(void))dlsym(library, "beam_isolation_value");
        if (!value || value() != 73) { dlclose(library); return 5; }
        dlclose(library);
    }
    pid_t child = fork();
    if (child < 0) return 6;
    if (!child) {
        execl(paths[1], paths[1], "entry", (char *)NULL);
        _exit(restricted && denied(errno) ? 0 : 7);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 8;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "entry")) return 0;
    if (argc != 5 || (strcmp(argv[1], "allow") && strcmp(argv[1], "deny"))) return 64;
    int restricted = !strcmp(argv[1], "deny");
    int result = observe(argv + 2, restricted);
    if (result) { fprintf(stderr, "initial probe failed: %d\n", result); return result; }
    pid_t child = fork();
    if (child < 0) return 9;
    if (!child) _exit(observe(argv + 2, restricted));
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 10;
    puts(restricted ? "DENIED_IN_CHILD_AND_DESCENDANT" : "INPUTS_USABLE");
    return 0;
}
#endif

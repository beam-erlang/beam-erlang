/* Test-only macOS process ownership and post-detach VM exec receipts. */
#define _DARWIN_C_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <arpa/inet.h>
#include <libproc.h>
#include <sys/proc_info.h>

struct identity {
    struct proc_bsdinfo bsd;
    char path[PROC_PIDPATHINFO_MAXSIZE];
};

static void json_string(FILE *out, const char *value)
{
    const unsigned char *p = (const unsigned char *) value;
    fputc('"', out);
    for (; *p; ++p) {
        switch (*p) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b", out); break;
        case '\f': fputs("\\f", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*p < 0x20) fprintf(out, "\\u%04x", *p);
            else fputc(*p, out);
        }
    }
    fputc('"', out);
}

static int error(const char *operation)
{
    int saved_errno = errno;
    fputs("{\"error\":", stderr);
    json_string(stderr, operation);
    fprintf(stderr, ",\"errno\":%d,\"detail\":", saved_errno);
    json_string(stderr, strerror(saved_errno));
    fputs("}\n", stderr);
    return 1;
}

static int number(const char *text, uint64_t maximum, uint64_t *value)
{
    char *end;
    unsigned long long result;
    if (!text || !*text || *text < '0' || *text > '9') return 0;
    errno = 0;
    result = strtoull(text, &end, 10);
    if (errno || *end || result > maximum) return 0;
    *value = (uint64_t) result;
    return 1;
}

static int read_identity(pid_t pid, struct identity *id)
{
    int size;
    memset(id, 0, sizeof(*id));
    size = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &id->bsd, sizeof(id->bsd));
    if (size != (int) sizeof(id->bsd)) {
        if (size >= 0) errno = ESRCH;
        return 0;
    }
    if (id->bsd.pbi_pid != (uint32_t) pid || id->bsd.pbi_uid != getuid()) {
        errno = EPERM;
        return 0;
    }
    if (proc_pidpath(pid, id->path, sizeof(id->path)) <= 0) return 0;
    return 1;
}

static void print_identity(FILE *out, const struct identity *id, const char *path)
{
    fprintf(out, "\"pid\":%u,\"uid\":%u,\"birth_sec\":%" PRIu64
            ",\"birth_usec\":%" PRIu64 ",\"ppid\":%u,\"pgid\":%u,\"path\":",
            id->bsd.pbi_pid, id->bsd.pbi_uid,
            id->bsd.pbi_start_tvsec, id->bsd.pbi_start_tvusec,
            id->bsd.pbi_ppid, id->bsd.pbi_pgid);
    json_string(out, path);
}

static int inspect_process(const char *pid_text)
{
    uint64_t pid;
    struct identity id;
    if (!number(pid_text, INT_MAX, &pid) || pid <= 1) {
        errno = EINVAL;
        return error("invalid inspect PID");
    }
    if (!read_identity((pid_t) pid, &id)) return error("inspect process identity");
    fputc('{', stdout);
    print_identity(stdout, &id, id.path);
    fputs("}\n", stdout);
    return 0;
}

static int signal_process(int argc, char **argv)
{
    uint64_t pid, birth_sec, birth_usec, signal_no;
    char expected[PATH_MAX];
    struct identity id;
    if (argc != 7 || !number(argv[2], INT_MAX, &pid) || pid <= 1 ||
        !number(argv[3], UINT64_MAX, &birth_sec) ||
        !number(argv[4], 999999, &birth_usec) ||
        !number(argv[6], NSIG - 1, &signal_no) || signal_no == 0 ||
        (signal_no != SIGTERM && signal_no != SIGKILL) ||
        argv[5][0] != '/' || !realpath(argv[5], expected)) {
        errno = EINVAL;
        return error("invalid verified signal arguments (TERM/KILL only)");
    }
    if (!read_identity((pid_t) pid, &id)) return error("inspect signal target");
    if (id.bsd.pbi_start_tvsec != birth_sec ||
        id.bsd.pbi_start_tvusec != birth_usec || strcmp(id.path, expected)) {
        errno = EPERM;
        return error("signal target identity mismatch");
    }
    if (kill((pid_t) pid, (int) signal_no)) return error("signal owned process");
    fputs("{\"signaled\":true,", stdout);
    print_identity(stdout, &id, id.path);
    fprintf(stdout, ",\"signal\":%" PRIu64 "}\n", signal_no);
    return 0;
}

static int owns_listener(const char *pid_text, const char *port_text)
{
    uint64_t pid, port;
    struct identity before, after;
    struct proc_fdinfo *fds;
    int bytes, got, i, owned = 0;
    if (!number(pid_text, INT_MAX, &pid) || pid <= 1 ||
        !number(port_text, 65535, &port) || port == 0) {
        errno = EINVAL;
        return error("invalid listener arguments");
    }
    if (!read_identity((pid_t) pid, &before)) return error("inspect listener owner");
    bytes = proc_pidinfo((pid_t) pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (bytes <= 0) return 1;
    bytes += 32 * (int) sizeof(*fds);
    fds = malloc((size_t) bytes);
    if (!fds) return error("allocate listener descriptors");
    got = proc_pidinfo((pid_t) pid, PROC_PIDLISTFDS, 0, fds, bytes);
    if (got < 0) {
        free(fds);
        return error("read listener descriptors");
    }
    for (i = 0; i < got / (int) sizeof(*fds); ++i) {
        struct socket_fdinfo socket;
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
        memset(&socket, 0, sizeof(socket));
        if (proc_pidfdinfo((pid_t) pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO,
                          &socket, sizeof(socket)) != (int) sizeof(socket)) continue;
        if (socket.psi.soi_kind == SOCKINFO_TCP &&
            (socket.psi.soi_family == AF_INET || socket.psi.soi_family == AF_INET6) &&
            socket.psi.soi_proto.pri_tcp.tcpsi_state == TSI_S_LISTEN &&
            ntohs((uint16_t) socket.psi.soi_proto.pri_tcp.tcpsi_ini.insi_lport) == port) {
            owned = 1;
            break;
        }
    }
    free(fds);
    if (!owned || !read_identity((pid_t) pid, &after) ||
        before.bsd.pbi_start_tvsec != after.bsd.pbi_start_tvsec ||
        before.bsd.pbi_start_tvusec != after.bsd.pbi_start_tvusec ||
        strcmp(before.path, after.path)) return 1;
    fputs("{\"owns_listener\":true,", stdout);
    print_identity(stdout, &after, after.path);
    fprintf(stdout, ",\"port\":%" PRIu64 "}\n", port);
    return 0;
}

static int exec_candidate(char **argv)
{
    const char *actual = getenv("BEAM_TEST_ACTUAL_VM");
    const char *nonce = getenv("BEAM_TEST_RUN_NONCE");
    const char *journal = getenv("BEAM_TEST_PROCESS_JOURNAL");
    char resolved[PATH_MAX];
    struct identity id;
    struct stat st;
    FILE *stream;
    int fd, ok;
    if (!actual || actual[0] != '/' || !nonce || !*nonce ||
        !journal || journal[0] != '/' || !realpath(actual, resolved)) {
        errno = EINVAL;
        return error("missing absolute candidate/journal path or run nonce");
    }
    if (!read_identity(getpid(), &id)) return error("inspect post-detach VM identity");
    if (!strcmp(id.path, resolved) || access(resolved, X_OK)) {
        errno = EINVAL;
        return error("invalid actual candidate executable");
    }
    fd = open(journal, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return error("open candidate process journal");
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        close(fd);
        errno = EPERM;
        return error("unsafe candidate process journal");
    }
    if (flock(fd, LOCK_EX)) {
        close(fd);
        return error("lock candidate process journal");
    }
    stream = fdopen(fd, "a");
    if (!stream) {
        close(fd);
        return error("open candidate journal stream");
    }
    fputs("{\"event\":\"exec\",\"nonce\":", stream);
    json_string(stream, nonce);
    fputc(',', stream);
    print_identity(stream, &id, resolved);
    fputs(",\"launcher_path\":", stream);
    json_string(stream, id.path);
    fputs("}\n", stream);
    ok = fflush(stream) == 0 && !ferror(stream);
    if (flock(fd, LOCK_UN)) ok = 0;
    if (fclose(stream)) ok = 0;
    if (!ok) return error("write candidate process receipt");
    execv(resolved, argv);
    return error("exec actual candidate VM");
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "inspect")) {
        if (argc == 3) return inspect_process(argv[2]);
    } else if (argc > 1 && !strcmp(argv[1], "signal")) {
        return signal_process(argc, argv);
    } else if (argc > 1 && !strcmp(argv[1], "owns-listener")) {
        if (argc == 4) return owns_listener(argv[2], argv[3]);
    } else {
        return exec_candidate(argv);
    }
    errno = EINVAL;
    return error("usage: inspect PID | signal PID BIRTH_SEC BIRTH_USEC PATH SIGNAL | owns-listener PID PORT");
}
#else
int main(void)
{
    fputs("emulator_process currently supports the local macOS test profile only\n", stderr);
    return 1;
}
#endif

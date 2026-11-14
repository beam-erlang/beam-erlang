/* Test-only native process ownership and post-detach VM exec receipts. */
#ifdef __linux__
#define _GNU_SOURCE 1
#endif
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
#elif defined(__linux__)
#include <dirent.h>
#include <sys/syscall.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
struct identity {
    uint32_t pid, uid, ppid, pgid;
    uint64_t birth_sec, birth_usec;
#ifdef __APPLE__
    char path[PROC_PIDPATHINFO_MAXSIZE];
#else
    char path[PATH_MAX];
#endif
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
#ifdef __APPLE__
    struct proc_bsdinfo bsd;
    int size;
    memset(id, 0, sizeof(*id));
    size = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd));
    if (size != (int) sizeof(bsd)) {
        if (size >= 0) errno = ESRCH;
        return 0;
    }
    if (bsd.pbi_pid != (uint32_t) pid || bsd.pbi_uid != getuid()) {
        errno = EPERM;
        return 0;
    }
    if (proc_pidpath(pid, id->path, sizeof(id->path)) <= 0) return 0;
    id->pid = bsd.pbi_pid;
    id->uid = bsd.pbi_uid;
    id->ppid = bsd.pbi_ppid;
    id->pgid = bsd.pbi_pgid;
    id->birth_sec = bsd.pbi_start_tvsec;
    id->birth_usec = bsd.pbi_start_tvusec;
    return 1;
#else
    char directory[64], buffer[8192], *end, *field, *save;
    struct stat owner;
    uint64_t value, start = 0;
    long ticks = sysconf(_SC_CLK_TCK);
    ssize_t length;
    int proc, fd, index, saved_errno;
    memset(id, 0, sizeof(*id));
    if (ticks <= 0 || ticks > 1000000) {
        errno = EINVAL;
        return 0;
    }
    snprintf(directory, sizeof(directory), "/proc/%ld", (long) pid);
    proc = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (proc < 0) return 0;
    if (fstat(proc, &owner)) goto failed;
    if (owner.st_uid != getuid()) {
        errno = EPERM;
        goto failed;
    }
    fd = openat(proc, "stat", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) goto failed;
    length = read(fd, buffer, sizeof(buffer) - 1);
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    if (length <= 0) goto failed;
    if (length == (ssize_t) sizeof(buffer) - 1) {
        errno = EOVERFLOW;
        goto failed;
    }
    buffer[length] = '\0';
    /* comm is parenthesized and may itself contain spaces, ')' and newlines. */
    end = strrchr(buffer, ')');
    field = strchr(buffer, ' ');
    if (!end || !field || field >= end || end[1] != ' ') goto malformed;
    *field = '\0';
    if (!number(buffer, INT_MAX, &value) || value != (uint64_t) pid) goto malformed;
    id->pid = (uint32_t) value;
    id->uid = (uint32_t) owner.st_uid;
    for (index = 3, field = strtok_r(end + 2, " \n", &save);
         field; ++index, field = strtok_r(NULL, " \n", &save)) {
        if (index == 3 && (field[0] == 'Z' || field[0] == 'X' || field[0] == 'x')) {
            errno = ESRCH;
            goto failed;
        }
        if (index == 4 || index == 5 || index == 22) {
            if (!number(field, index == 22 ? UINT64_MAX : INT_MAX, &value)) goto malformed;
            if (index == 4) id->ppid = (uint32_t) value;
            else if (index == 5) id->pgid = (uint32_t) value;
            else { start = value; break; }
        }
    }
    if (index != 22 || !field) goto malformed;
    /* Same-boot birth token: retain every kernel start-time tick in the
     * existing seconds/microseconds CLI without inventing wall-clock time. */
    id->birth_sec = start / (uint64_t) ticks;
    id->birth_usec = (start % (uint64_t) ticks) * 1000000 / (uint64_t) ticks;
    length = readlinkat(proc, "exe", id->path, sizeof(id->path) - 1);
    if (length < 0) goto failed;
    if (length == (ssize_t) sizeof(id->path) - 1) {
        errno = ENAMETOOLONG;
        goto failed;
    }
    id->path[length] = '\0';
    close(proc);
    return 1;
malformed:
    errno = EINVAL;
failed:
    saved_errno = errno;
    close(proc);
    errno = saved_errno;
    return 0;
#endif
}

static void print_identity(FILE *out, const struct identity *id, const char *path)
{
    fprintf(out, "\"pid\":%u,\"uid\":%u,\"birth_sec\":%" PRIu64
            ",\"birth_usec\":%" PRIu64 ",\"ppid\":%u,\"pgid\":%u,\"path\":",
            id->pid, id->uid, id->birth_sec, id->birth_usec,
            id->ppid, id->pgid);
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
#ifdef __linux__
    int pidfd, result, saved_errno;
#endif
    if (argc != 7 || !number(argv[2], INT_MAX, &pid) || pid <= 1 ||
        !number(argv[3], UINT64_MAX, &birth_sec) ||
        !number(argv[4], 999999, &birth_usec) ||
        !number(argv[6], NSIG - 1, &signal_no) || signal_no == 0 ||
        (signal_no != SIGTERM && signal_no != SIGKILL) ||
        argv[5][0] != '/' || !realpath(argv[5], expected)) {
        errno = EINVAL;
        return error("invalid verified signal arguments (TERM/KILL only)");
    }
#ifdef __linux__
    /* Pin the task before checking /proc. There is deliberately no kill(PID)
     * fallback: an exit/reuse race must never signal a replacement process. */
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    pidfd = (int) syscall(SYS_pidfd_open, (pid_t) pid, 0);
    if (pidfd < 0) return error("open signal target pidfd");
#else
    errno = ENOSYS;
    return error("pidfd process signaling is required");
#endif
#endif
    if (!read_identity((pid_t) pid, &id)) {
#ifdef __linux__
        saved_errno = errno;
        close(pidfd);
        errno = saved_errno;
#endif
        return error("inspect signal target");
    }
    if (id.birth_sec != birth_sec ||
        id.birth_usec != birth_usec || strcmp(id.path, expected)) {
#ifdef __linux__
        close(pidfd);
#endif
        errno = EPERM;
        return error("signal target identity mismatch");
    }
#ifdef __linux__
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    result = (int) syscall(SYS_pidfd_send_signal, pidfd, (int) signal_no, NULL, 0);
#else
    result = -1;
    errno = ENOSYS;
#endif
    saved_errno = errno;
    close(pidfd);
    errno = saved_errno;
    if (result) return error("signal owned process through pidfd");
#else
    if (kill((pid_t) pid, (int) signal_no)) return error("signal owned process");
#endif
    fputs("{\"signaled\":true,", stdout);
    print_identity(stdout, &id, id.path);
    fprintf(stdout, ",\"signal\":%" PRIu64 "}\n", signal_no);
    return 0;
}

#ifdef __linux__
static int socket_inodes(int proc, uint64_t **inodes, size_t *count)
{
    DIR *directory;
    struct dirent *entry;
    int fd = openat(proc, "fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    directory = fdopendir(fd);
    if (!directory) { close(fd); return 0; }
    *inodes = NULL;
    *count = 0;
    errno = 0;
    while ((entry = readdir(directory))) {
        char link[128];
        ssize_t length = readlinkat(fd, entry->d_name, link, sizeof(link) - 1);
        uint64_t inode, *larger;
        if (length < 0) {
            if (errno == ENOENT || errno == EINVAL) { errno = 0; continue; }
            goto failed;
        }
        link[length] = '\0';
        if (length < 10 || strncmp(link, "socket:[", 8) || link[length - 1] != ']') continue;
        link[length - 1] = '\0';
        if (!number(link + 8, UINT64_MAX, &inode)) { errno = EINVAL; goto failed; }
        larger = realloc(*inodes, (*count + 1) * sizeof(**inodes));
        if (!larger) goto failed;
        *inodes = larger;
        (*inodes)[(*count)++] = inode;
        errno = 0;
    }
    if (errno) goto failed;
    closedir(directory);
    return 1;
failed:
    {
        int saved_errno = errno;
        closedir(directory);
        free(*inodes);
        *inodes = NULL;
        errno = saved_errno;
        return 0;
    }
}

static int table_has_listener(int proc, const char *table, uint64_t port,
                              const uint64_t *inodes, size_t count)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    int fd = openat(proc, table, O_RDONLY | O_CLOEXEC), owned = 0;
    FILE *stream;
    if (fd < 0) return errno == ENOENT && !strcmp(table, "net/tcp6") ? 0 : -1;
    stream = fdopen(fd, "r");
    if (!stream) { close(fd); return -1; }
    /* The first row is a header; subsequent rows have inode in column ten. */
    length = getline(&line, &capacity, stream);
    if (length < 0) { owned = -1; goto done; }
    while (getline(&line, &capacity, stream) >= 0) {
        char *field, *save, *local = NULL, *state = NULL, *inode_text = NULL, *end;
        uint64_t inode;
        unsigned long local_port;
        size_t index;
        int column;
        for (column = 0, field = strtok_r(line, " \t\n", &save);
             field; ++column, field = strtok_r(NULL, " \t\n", &save)) {
            if (column == 1) local = field;
            else if (column == 3) state = field;
            else if (column == 9) { inode_text = field; break; }
        }
        if (!local || !state || !inode_text) { errno = EINVAL; owned = -1; break; }
        if (strcmp(state, "0A")) continue;
        local = strrchr(local, ':');
        if (!local || !local[1]) { errno = EINVAL; owned = -1; break; }
        errno = 0;
        local_port = strtoul(local + 1, &end, 16);
        if (errno || *end || local_port > 65535 ||
            !number(inode_text, UINT64_MAX, &inode)) { errno = EINVAL; owned = -1; break; }
        if (local_port != port) continue;
        for (index = 0; index < count; ++index)
            if (inode == inodes[index]) { owned = 1; goto done; }
    }
    if (ferror(stream)) owned = -1;
done:
    free(line);
    {
        int saved_errno = errno;
        fclose(stream);
        errno = saved_errno;
    }
    return owned;
}
#endif

static int owns_listener(const char *pid_text, const char *port_text)
{
    uint64_t pid, port;
    struct identity before, after;
#ifdef __APPLE__
    struct proc_fdinfo *fds;
    int bytes, got, i, owned = 0;
#else
    char directory[64];
    uint64_t *inodes = NULL;
    size_t count = 0;
    int proc, owned, saved_errno;
#endif
    if (!number(pid_text, INT_MAX, &pid) || pid <= 1 ||
        !number(port_text, 65535, &port) || port == 0) {
        errno = EINVAL;
        return error("invalid listener arguments");
    }
    if (!read_identity((pid_t) pid, &before)) return error("inspect listener owner");
#ifdef __APPLE__
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
#else
    snprintf(directory, sizeof(directory), "/proc/%" PRIu64, pid);
    proc = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (proc < 0) return error("open listener owner proc directory");
    if (!socket_inodes(proc, &inodes, &count)) {
        saved_errno = errno;
        close(proc);
        errno = saved_errno;
        return error("read listener owner descriptors");
    }
    owned = table_has_listener(proc, "net/tcp", port, inodes, count);
    if (owned == 0) owned = table_has_listener(proc, "net/tcp6", port, inodes, count);
    saved_errno = errno;
    free(inodes);
    close(proc);
    errno = saved_errno;
    if (owned < 0) return error("read listener socket table");
#endif
    if (!owned || !read_identity((pid_t) pid, &after) ||
        before.birth_sec != after.birth_sec ||
        before.birth_usec != after.birth_usec ||
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
    fputs("emulator_process requires native macOS or Linux\n", stderr);
    return 1;
}
#endif

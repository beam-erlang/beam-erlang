/* SPDX-License-Identifier: Apache-2.0 */
/* Compiler acquisition and effects belong to the command's native host. */
#define _POSIX_C_SOURCE 200809L
#include "beam_compiler_host.h"
#include "beam_compiler_inventory.h"

/* Only the build executables select the private bootstrap command policy. */
#ifndef BEAM_COMPILER_COMMAND_FUNCTION
#define BEAM_COMPILER_COMMAND_FUNCTION "main"
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define COMPILER_MAX_FILE ((size_t)256 * 1024 * 1024)
#define ETF_VERSION 131
#define ETF_SMALL_INTEGER 97
#define ETF_INTEGER 98
#define ETF_ATOM 100
#define ETF_SMALL_TUPLE 104
#define ETF_LARGE_TUPLE 105
#define ETF_NIL 106
#define ETF_STRING 107
#define ETF_LIST 108
#define ETF_BINARY 109
#define ETF_SMALL_BIG 110
#define ETF_SMALL_ATOM 115
#define ETF_MAP 116
#define ETF_ATOM_UTF8 118
#define ETF_SMALL_ATOM_UTF8 119

typedef struct buffer {
    unsigned char *data;
    size_t size, capacity;
} buffer;

typedef struct cursor {
    const unsigned char *p, *end;
} cursor;

typedef struct writer {
    uint64_t token;
    int fd;
    struct writer *next;
} writer;

typedef struct compiler_context {
    writer *writers;
    uint64_t next_token;
} compiler_context;

static int append(buffer *b, const void *data, size_t size)
{
    size_t capacity;
    unsigned char *grown;
    if (size > SIZE_MAX - b->size)
        return 0;
    if (b->size + size > b->capacity) {
        capacity = b->capacity ? b->capacity : 256;
        while (capacity < b->size + size) {
            if (capacity > SIZE_MAX / 2)
                return 0;
            capacity *= 2;
        }
        grown = realloc(b->data, capacity);
        if (!grown)
            return 0;
        b->data = grown;
        b->capacity = capacity;
    }
    if (size)
        memcpy(b->data + b->size, data, size);
    b->size += size;
    return 1;
}

static int byte(buffer *b, unsigned n)
{
    unsigned char value = (unsigned char)n;
    return append(b, &value, 1);
}

static int u32(buffer *b, uint32_t n)
{
    unsigned char bytes[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16),
                              (unsigned char)(n >> 8), (unsigned char)n};
    return append(b, bytes, sizeof bytes);
}

static int atom(buffer *b, const char *text)
{
    size_t size = strlen(text);
    return size < 256 && byte(b, ETF_SMALL_ATOM_UTF8) && byte(b, (unsigned)size) &&
           append(b, text, size);
}

static int binary(buffer *b, const void *data, size_t size)
{
    return size <= UINT32_MAX && byte(b, ETF_BINARY) && u32(b, (uint32_t)size) &&
           append(b, data, size);
}

static int text_binary(buffer *b, const char *text)
{
    return binary(b, text, strlen(text));
}

static int integer(buffer *b, int64_t value)
{
    unsigned char digits[8];
    unsigned count = 0;
    uint64_t magnitude;
    if (value >= 0 && value < 256)
        return byte(b, ETF_SMALL_INTEGER) && byte(b, (unsigned)value);
    if (value >= INT32_MIN && value <= INT32_MAX)
        return byte(b, ETF_INTEGER) && u32(b, (uint32_t)value);
    magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    do {
        digits[count++] = (unsigned char)magnitude;
        magnitude >>= 8;
    } while (magnitude);
    return byte(b, ETF_SMALL_BIG) && byte(b, count) && byte(b, value < 0) &&
           append(b, digits, count);
}

static int unsigned_integer(buffer *b, uint64_t value)
{
    unsigned char digits[8];
    unsigned index;
    if (value <= INT64_MAX)
        return integer(b, (int64_t)value);
    for (index = 0; index < sizeof digits; ++index) {
        digits[index] = (unsigned char)value;
        value >>= 8;
    }
    return byte(b, ETF_SMALL_BIG) && byte(b, sizeof digits) && byte(b, 0) &&
           append(b, digits, sizeof digits);
}

static int tuple(buffer *b, unsigned n)
{
    return n < 256 && byte(b, ETF_SMALL_TUPLE) && byte(b, n);
}

static int list(buffer *b, size_t n)
{
    return n <= UINT32_MAX && byte(b, ETF_LIST) && u32(b, (uint32_t)n);
}

static int take(cursor *c, size_t size, const unsigned char **out)
{
    if (size > (size_t)(c->end - c->p))
        return 0;
    *out = c->p;
    c->p += size;
    return 1;
}

static int get_byte(cursor *c, unsigned *value)
{
    const unsigned char *p;
    if (!take(c, 1, &p))
        return 0;
    *value = p[0];
    return 1;
}

static int get_u32(cursor *c, uint32_t *value)
{
    const unsigned char *p;
    if (!take(c, 4, &p))
        return 0;
    *value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
             ((uint32_t)p[2] << 8) | p[3];
    return 1;
}

static int get_integer(cursor *c, int64_t *value)
{
    unsigned tag, small, sign, index;
    uint32_t word;
    uint64_t magnitude = 0;
    const unsigned char *digits;
    if (!get_byte(c, &tag))
        return 0;
    if (tag == ETF_SMALL_INTEGER && get_byte(c, &small)) {
        *value = small;
        return 1;
    }
    if (tag == ETF_INTEGER && get_u32(c, &word)) {
        *value = (int32_t)word;
        return 1;
    }
    if (tag == ETF_SMALL_BIG && get_byte(c, &small) && small <= 8 &&
        get_byte(c, &sign) && sign <= 1 && take(c, small, &digits)) {
        for (index = 0; index < small; ++index)
            magnitude |= (uint64_t)digits[index] << (index * 8);
        if (magnitude <= INT64_MAX) {
            *value = sign ? -(int64_t)magnitude : (int64_t)magnitude;
            return 1;
        }
        if (sign && magnitude == (UINT64_C(1) << 63)) {
            *value = INT64_MIN;
            return 1;
        }
    }
    return 0;
}

static int get_atom(cursor *c, char *out, size_t capacity)
{
    unsigned tag, length, low;
    size_t index, used = 0;
    const unsigned char *p;
    if (!get_byte(c, &tag) || !get_byte(c, &length))
        return 0;
    if (tag == ETF_ATOM || tag == ETF_ATOM_UTF8) {
        if (!get_byte(c, &low))
            return 0;
        length = length * 256 + low;
    } else if (tag != ETF_SMALL_ATOM_UTF8 && tag != ETF_SMALL_ATOM) {
        return 0;
    }
    if (length >= capacity || !take(c, length, &p) || memchr(p, 0, length))
        return 0;
    if (tag == ETF_ATOM || tag == ETF_SMALL_ATOM) {
        for (index = 0; index < length; ++index) {
            if (used + (p[index] < 128 ? 1u : 2u) >= capacity)
                return 0;
            if (p[index] >= 128)
                out[used++] = (char)(0xc0 | (p[index] >> 6));
            out[used++] = (char)(p[index] < 128 ? p[index] : 0x80 | (p[index] & 63));
        }
    } else {
        memcpy(out, p, length);
        used = length;
    }
    out[used] = 0;
    return 1;
}

static int get_binary(cursor *c, beam_bytes *out)
{
    unsigned tag;
    uint32_t size;
    return get_byte(c, &tag) && tag == ETF_BINARY && get_u32(c, &size) &&
           take(c, size, &out->data) && (out->size = size, 1);
}

static int get_tuple(cursor *c, unsigned wanted)
{
    unsigned tag, arity;
    return get_byte(c, &tag) && tag == ETF_SMALL_TUPLE &&
           get_byte(c, &arity) && arity == wanted;
}

static int get_list(cursor *c, uint32_t *size)
{
    unsigned tag;
    if (!get_byte(c, &tag))
        return 0;
    if (tag == ETF_NIL) {
        *size = 0;
        return 1;
    }
    if (tag != ETF_LIST || !get_u32(c, size) || *size > 1048576)
        return 0;
    if (!*size)
        return get_byte(c, &tag) && tag == ETF_NIL;
    return 1;
}

static int nil(cursor *c)
{
    unsigned tag;
    return get_byte(c, &tag) && tag == ETF_NIL;
}

static int finished(cursor c, uint32_t argc)
{
    return (!argc || nil(&c)) && c.p == c.end;
}

/* ETF packs a list such as [Handle] into STRING_EXT when every value fits
 * in one byte. Normalize those arguments before decoding the operation. */
static int get_arguments(cursor *c, uint32_t *argc, buffer *expanded)
{
    unsigned tag, high, low;
    const unsigned char *values;
    uint32_t index;
    if (c->p == c->end || *c->p != ETF_STRING)
        return get_list(c, argc);
    if (!get_byte(c, &tag) || !get_byte(c, &high) || !get_byte(c, &low))
        return 0;
    *argc = high * 256 + low;
    if (*argc > 3 || !take(c, *argc, &values) || c->p != c->end)
        return 0;
    if (!*argc)
        return 1;
    for (index = 0; index < *argc; ++index)
        if (!integer(expanded, values[index]))
            return 0;
    if (!byte(expanded, ETF_NIL))
        return 0;
    *c = (cursor){expanded->data, expanded->data + expanded->size};
    return 1;
}

static int utf8(buffer *b, uint32_t cp)
{
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        return 0;
    if (cp < 0x80)
        return byte(b, cp);
    if (cp < 0x800)
        return byte(b, 0xc0 | (cp >> 6)) && byte(b, 0x80 | (cp & 63));
    if (cp < 0x10000)
        return byte(b, 0xe0 | (cp >> 12)) && byte(b, 0x80 | ((cp >> 6) & 63)) &&
               byte(b, 0x80 | (cp & 63));
    return byte(b, 0xf0 | (cp >> 18)) && byte(b, 0x80 | ((cp >> 12) & 63)) &&
           byte(b, 0x80 | ((cp >> 6) & 63)) && byte(b, 0x80 | (cp & 63));
}

/* Paths are UTF-8 binaries or Unicode charlists, never arbitrary ETF terms. */
static int get_text(cursor *c, char **out)
{
    buffer b = {0};
    unsigned tag, high, low;
    uint32_t count, index;
    const unsigned char *p;
    int64_t cp;
    if (!get_byte(c, &tag))
        return 0;
    if (tag == ETF_BINARY) {
        if (!get_u32(c, &count) || count >= PATH_MAX || !take(c, count, &p) ||
            memchr(p, 0, count) || !append(&b, p, count))
            goto failed;
    } else if (tag == ETF_STRING) {
        if (!get_byte(c, &high) || !get_byte(c, &low))
            goto failed;
        count = high * 256 + low;
        if (!take(c, count, &p))
            goto failed;
        for (index = 0; index < count; ++index)
            if (!utf8(&b, p[index]))
                goto failed;
    } else if (tag == ETF_LIST) {
        if (!get_u32(c, &count) || count >= PATH_MAX)
            goto failed;
        for (index = 0; index < count; ++index)
            if (!get_integer(c, &cp) || cp < 0 || cp > 0x10ffff || !utf8(&b, (uint32_t)cp))
                goto failed;
        if (!nil(c))
            goto failed;
    } else if (tag != ETF_NIL) {
        goto failed;
    }
    if (b.size >= PATH_MAX || !byte(&b, 0))
        goto failed;
    *out = (char *)b.data;
    return 1;
failed:
    free(b.data);
    return 0;
}

/* Encode native UTF-8 paths as ordinary Erlang character lists. */
static int charlist(buffer *b, const char *text)
{
    buffer chars = {0};
    const unsigned char *p = (const unsigned char *)text;
    size_t count = 0;
    int ok = 0;
    while (*p) {
        uint32_t cp = *p++;
        unsigned needed = 0, minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; needed = 1; minimum = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; needed = 2; minimum = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; needed = 3; minimum = 0x10000; }
        else if (cp >= 0x80) goto done;
        while (needed--) {
            if ((*p & 0xc0) != 0x80)
                goto done;
            cp = (cp << 6) | (*p++ & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            !integer(&chars, cp))
            goto done;
        ++count;
    }
    ok = list(b, count) && append(b, chars.data, chars.size) && byte(b, ETF_NIL);
done:
    free(chars.data);
    return ok;
}

static const char *posix_error(int error)
{
    switch (error) {
    case EACCES: return "eacces"; case EPERM: return "eperm";
    case ENOENT: return "enoent"; case ENOTDIR: return "enotdir";
    case EISDIR: return "eisdir"; case EEXIST: return "eexist";
    case EINVAL: return "einval"; case EBADF: return "ebadf";
    case ELOOP: return "eloop"; case ENAMETOOLONG: return "enametoolong";
    case ENOSPC: return "enospc"; case EROFS: return "erofs";
    case ENOMEM: return "enomem"; case EMFILE: return "emfile";
    case ENFILE: return "enfile"; case EFBIG: return "efbig";
    case EXDEV: return "exdev"; case ENOTEMPTY: return "enotempty";
    default: return "eio";
    }
}

static int error_term(buffer *reply, int error)
{
    return tuple(reply, 2) && atom(reply, "error") && atom(reply, posix_error(error));
}

static int read_file(const char *path, buffer *bytes)
{
    struct stat st;
    unsigned char block[16384];
    int fd, error = 0;
    ssize_t count;
    do { fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK); } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return errno;
    if (fstat(fd, &st) != 0)
        error = errno;
    else if (!S_ISREG(st.st_mode))
        error = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
    else if (st.st_size < 0 || (uintmax_t)st.st_size > COMPILER_MAX_FILE)
        error = EFBIG;
    while (!error) {
        count = read(fd, block, sizeof block);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) { error = errno; break; }
        if (!count)
            break;
        if (bytes->size > COMPILER_MAX_FILE - (size_t)count) { error = EFBIG; break; }
        if (!append(bytes, block, (size_t)count)) { error = ENOMEM; break; }
    }
    if (close(fd) != 0 && !error)
        error = errno;
    return error;
}

static int write_descriptor(int fd, beam_bytes bytes)
{
    int error = 0;
    size_t offset = 0;
    while (offset < bytes.size) {
        ssize_t written = write(fd, bytes.data + offset, bytes.size - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) { error = written < 0 ? errno : EIO; break; }
        offset += (size_t)written;
    }
    return error;
}

static int write_file(const char *path, beam_bytes bytes, int flags)
{
    int fd, error;
    do { fd = open(path, flags | O_CLOEXEC, 0666); } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return errno;
    error = write_descriptor(fd, bytes);
    if (close(fd) != 0 && !error)
        error = errno;
    return error;
}

static int write_modes(cursor *c, int *flags, int *error)
{
    char mode[32];
    uint32_t count, index;
    *flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (!get_list(c, &count))
        return 0;
    for (index = 0; index < count; ++index) {
        if (!get_atom(c, mode, sizeof mode))
            return 0;
        if (!strcmp(mode, "append")) *flags = (*flags & ~O_TRUNC) | O_APPEND;
        else if (!strcmp(mode, "exclusive")) *flags |= O_EXCL;
        else if (strcmp(mode, "write") && strcmp(mode, "binary") && strcmp(mode, "raw"))
            *error = EINVAL;
    }
    return !count || nil(c);
}

static int open_writer(compiler_context *context, const char *path, int flags, uint64_t *token)
{
    writer *owned;
    int error;
    if (context->next_token == INT64_MAX)
        return EMFILE;
    owned = malloc(sizeof(*owned));
    if (!owned)
        return ENOMEM;
    do { owned->fd = open(path, flags | O_CLOEXEC, 0666); }
    while (owned->fd < 0 && errno == EINTR);
    if (owned->fd < 0) {
        error = errno;
        free(owned);
        return error;
    }
    owned->token = ++context->next_token;
    owned->next = context->writers;
    context->writers = owned;
    *token = owned->token;
    return 0;
}

static writer **find_writer(compiler_context *context, uint64_t token)
{
    writer **entry = &context->writers;
    while (*entry && (*entry)->token != token)
        entry = &(*entry)->next;
    return entry;
}

static int close_writer(writer **entry)
{
    writer *owned = *entry;
    int error;
    if (!owned)
        return EBADF;
    *entry = owned->next;
    error = close(owned->fd) == 0 ? 0 : errno;
    free(owned);
    return error;
}

static int ensure_parent(char *path)
{
    char *p;
    struct stat st;
    for (p = path + (path[0] == '/'); *p; ++p) {
        if (*p != '/')
            continue;
        *p = 0;
        if (path[0] && mkdir(path, 0777) != 0) {
            int error = errno;
            if (error != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                *p = '/';
                return error == EEXIST ? ENOTDIR : error;
            }
        }
        *p = '/';
    }
    return 0;
}

static int timestamp(buffer *b, time_t value)
{
    struct tm time;
    if (!localtime_r(&value, &time))
        return 0;
    return tuple(b, 2) && tuple(b, 3) && integer(b, time.tm_year + 1900) &&
           integer(b, time.tm_mon + 1) && integer(b, time.tm_mday) &&
           tuple(b, 3) && integer(b, time.tm_hour) && integer(b, time.tm_min) &&
           integer(b, time.tm_sec);
}

static int file_info(buffer *b, const char *path, const struct stat *st)
{
    int read = path && access(path, R_OK) == 0;
    int write = !path || access(path, W_OK) == 0;
    const char *type = S_ISREG(st->st_mode) ? "regular" : S_ISDIR(st->st_mode) ? "directory" :
                       S_ISLNK(st->st_mode) ? "symlink" : S_ISCHR(st->st_mode) ||
                       S_ISBLK(st->st_mode) ? "device" : "other";
    return tuple(b, 2) && atom(b, "ok") && tuple(b, 14) && atom(b, "file_info") &&
           integer(b, st->st_size) && atom(b, type) &&
           atom(b, read ? (write ? "read_write" : "read") : (write ? "write" : "none")) &&
           timestamp(b, st->st_atime) && timestamp(b, st->st_mtime) && timestamp(b, st->st_ctime) &&
           unsigned_integer(b, st->st_mode) && unsigned_integer(b, st->st_nlink) &&
           unsigned_integer(b, st->st_dev) && unsigned_integer(b, st->st_rdev) &&
           unsigned_integer(b, st->st_ino) && unsigned_integer(b, st->st_uid) &&
           unsigned_integer(b, st->st_gid);
}

static int directory_list(buffer *reply, const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    buffer entries = {0};
    size_t count = 0;
    int error = 0, ok;
    if (!directory)
        return error_term(reply, errno);
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { error = errno; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (count == 1048576 || !charlist(&entries, entry->d_name)) { error = EINVAL; break; }
        ++count;
    }
    if (closedir(directory) != 0 && !error)
        error = errno;
    ok = error ? error_term(reply, error) : tuple(reply, 2) && atom(reply, "ok") &&
         list(reply, count) && append(reply, entries.data, entries.size) && byte(reply, ETF_NIL);
    free(entries.data);
    return ok;
}

static beam_status compiler_request(void *context, beam_bytes request, beam_host_reply *target)
{
    compiler_context *host = context;
    cursor c;
    buffer reply = {0}, contents = {0}, expanded = {0};
    char label[64], op[64], *path = NULL, *other = NULL;
    unsigned version;
    uint32_t argc;
    int error = 0, ok = 0, parsed = 0;
    beam_bytes data;
    struct stat st;
    beam_status status = BEAM_INVALID_TERM;
    if (!request.data || !request.size)
        return BEAM_INVALID_TERM;
    c = (cursor){request.data, request.data + request.size};
    if (!get_byte(&c, &version) || version != ETF_VERSION || !get_tuple(&c, 3) ||
        !get_atom(&c, label, sizeof label) || strcmp(label, "compiler") ||
        !get_atom(&c, op, sizeof op) || !get_arguments(&c, &argc, &expanded) || argc > 3 ||
        !byte(&reply, ETF_VERSION))
        goto done;
    if (!argc && !finished(c, argc))
        goto done;

    if (argc == 0 && !strcmp(op, "get_cwd")) {
        path = getcwd(NULL, 0);
        ok = path ? tuple(&reply, 2) && atom(&reply, "ok") && charlist(&reply, path) :
                    error_term(&reply, errno);
        parsed = 1;
    } else if (argc == 0 && !strcmp(op, "getpid")) {
        char pid[32];
        snprintf(pid, sizeof pid, "%ld", (long)getpid());
        ok = charlist(&reply, pid);
        parsed = 1;
    } else if (argc == 2 && !strcmp(op, "output")) {
        FILE *out;
        if (!get_atom(&c, label, sizeof label) || !get_binary(&c, &data) || !finished(c, argc))
            goto done;
        if (!strcmp(label, "standard_output")) out = stdout;
        else if (!strcmp(label, "standard_error")) out = stderr;
        else goto done;
        if ((data.size && fwrite(data.data, 1, data.size, out) != data.size) || fflush(out) != 0)
            error = EIO;
        ok = error ? error_term(&reply, error) : atom(&reply, "ok");
        parsed = 1;
    } else if ((argc == 2 && !strcmp(op, "write_handle")) ||
               (argc == 1 && (!strcmp(op, "close_handle") || !strcmp(op, "stat_handle")))) {
        int64_t token;
        writer **entry;
        if (!get_integer(&c, &token) || token <= 0 ||
            (argc == 2 && !get_binary(&c, &data)) || !finished(c, argc))
            goto done;
        entry = find_writer(host, (uint64_t)token);
        if (!*entry)
            error = EBADF;
        else if (!strcmp(op, "write_handle"))
            error = write_descriptor((*entry)->fd, data);
        else if (!strcmp(op, "close_handle"))
            error = close_writer(entry);
        else if (fstat((*entry)->fd, &st) != 0)
            error = errno;
        ok = error ? error_term(&reply, error) : !strcmp(op, "stat_handle") ?
             file_info(&reply, NULL, &st) : atom(&reply, "ok");
        parsed = 1;
    } else if (argc > 0 && get_text(&c, &path)) {
        if (argc == 1 && !finished(c, argc))
            goto done;
        if (argc == 1 && !strcmp(op, "read_file")) {
            error = read_file(path, &contents);
            ok = error ? error_term(&reply, error) : tuple(&reply, 2) && atom(&reply, "ok") &&
                 binary(&reply, contents.data, contents.size);
            parsed = 1;
        } else if (argc == 3 && !strcmp(op, "write_file")) {
            int flags;
            if (!get_binary(&c, &data) || !write_modes(&c, &flags, &error) || !finished(c, argc))
                goto done;
            if (!error)
                error = write_file(path, data, flags);
            ok = error ? error_term(&reply, error) : atom(&reply, "ok");
            parsed = 1;
        } else if (argc == 2 && !strcmp(op, "open_write")) {
            int flags;
            uint64_t token;
            if (!write_modes(&c, &flags, &error) || !finished(c, argc))
                goto done;
            if (!error)
                error = open_writer(host, path, flags, &token);
            ok = error ? error_term(&reply, error) : tuple(&reply, 2) && atom(&reply, "ok") &&
                 unsigned_integer(&reply, token);
            parsed = 1;
        } else if (argc == 1 && !strcmp(op, "read_link")) {
            char link[PATH_MAX];
            ssize_t size = readlink(path, link, sizeof link - 1);
            if (size < 0) ok = error_term(&reply, errno);
            else if ((size_t)size == sizeof link - 1) ok = error_term(&reply, ENAMETOOLONG);
            else {
                link[size] = 0;
                ok = tuple(&reply, 2) && atom(&reply, "ok") && charlist(&reply, link);
            }
            parsed = 1;
        } else if (argc == 1 && (!strcmp(op, "read_file_info") || !strcmp(op, "read_link_info"))) {
            int result = !strcmp(op, "read_file_info") ? stat(path, &st) : lstat(path, &st);
            ok = result ? error_term(&reply, errno) : file_info(&reply, path, &st);
            parsed = 1;
        } else if (argc == 1 && !strcmp(op, "list_dir")) {
            ok = directory_list(&reply, path);
            parsed = 1;
        } else if (argc == 1 && !strcmp(op, "getenv")) {
            const char *value = getenv(path);
            ok = value ? charlist(&reply, value) : atom(&reply, "false");
            parsed = 1;
        } else if (argc == 1 && !strcmp(op, "ensure_dir")) {
            error = ensure_parent(path);
            ok = error ? error_term(&reply, error) : atom(&reply, "ok");
            parsed = 1;
        } else if (argc == 1 && (!strcmp(op, "delete") || !strcmp(op, "make_dir"))) {
            int result = !strcmp(op, "delete") ? unlink(path) : mkdir(path, 0777);
            ok = result ? error_term(&reply, errno) : atom(&reply, "ok");
            parsed = 1;
        } else if (argc == 2 && !strcmp(op, "rename") && get_text(&c, &other)) {
            if (!finished(c, argc))
                goto done;
            ok = rename(path, other) ? error_term(&reply, errno) : atom(&reply, "ok");
            parsed = 1;
        }
    }
    if (!parsed || (argc && !nil(&c)) || c.p != c.end)
        goto done;
    status = ok ? beam_host_reply_copy(target, (beam_bytes){reply.data, reply.size}) : BEAM_OUT_OF_MEMORY;
done:
    free(path);
    free(other);
    free(reply.data);
    free(contents.data);
    free(expanded.data);
    return status;
}

static int module_path(char *out, size_t capacity, const char *root, const char *module)
{
    int size = snprintf(out, capacity, "%s/lib/beam/tools/compiler/ebin/%s.beam", root, module);
    return size >= 0 && (size_t)size < capacity;
}

static int arguments(buffer *b, const char *root, const char *cwd, const char *source,
                     int argc, char **argv)
{
    size_t index;
    char path[PATH_MAX];
    if (argc < 0 || !byte(b, ETF_VERSION) || !list(b, 1) || !byte(b, ETF_MAP) ||
        !u32(b, source ? 5 : 4) || !atom(b, "root") || !text_binary(b, root) ||
        !atom(b, "cwd") || !text_binary(b, cwd) || !atom(b, "arguments") || !list(b, (size_t)argc))
        return 0;
    for (index = 0; index < (size_t)argc; ++index)
        if (!text_binary(b, argv[index]))
            return 0;
    if (!byte(b, ETF_NIL) || !atom(b, "origins") || !list(b, BEAM_COMPILER_MODULE_COUNT))
        return 0;
    for (index = 0; index < BEAM_COMPILER_MODULE_COUNT; ++index) {
        if (!module_path(path, sizeof path, root, beam_compiler_modules[index]) ||
            !tuple(b, 2) || !atom(b, beam_compiler_modules[index]) || !text_binary(b, path))
            return 0;
    }
    if (!byte(b, ETF_NIL))
        return 0;
    if (source && (!atom(b, "source") || !text_binary(b, source)))
        return 0;
    return byte(b, ETF_NIL);
}

/* Error rendering is deliberately bounded and independent of the tool modules. */
static void print_term(cursor *c, unsigned depth)
{
    cursor saved = *c;
    unsigned tag, n;
    uint32_t count, index;
    int64_t value;
    char name[1024];
    beam_bytes bytes;
    if (!depth || c->p == c->end) { fputs("...", stderr); c->p = c->end; return; }
    tag = *c->p;
    if (tag == ETF_SMALL_ATOM_UTF8 || tag == ETF_ATOM || tag == ETF_ATOM_UTF8 || tag == ETF_SMALL_ATOM) {
        if (get_atom(c, name, sizeof name)) { fputs(name, stderr); return; }
    } else if (tag == ETF_SMALL_INTEGER || tag == ETF_INTEGER) {
        if (get_integer(c, &value)) { fprintf(stderr, "%lld", (long long)value); return; }
    } else if (tag == ETF_BINARY) {
        if (get_binary(c, &bytes)) { fprintf(stderr, "<<%zu bytes>>", bytes.size); return; }
    } else if (tag == ETF_SMALL_TUPLE || tag == ETF_LARGE_TUPLE) {
        get_byte(c, &n);
        if (tag == ETF_SMALL_TUPLE && get_byte(c, &n)) count = n;
        else if (tag == ETF_LARGE_TUPLE && get_u32(c, &count)) {}
        else goto invalid;
        fputc('{', stderr);
        for (index = 0; index < count && index < 24 && c->p != c->end; ++index) {
            if (index) fputc(',', stderr);
            print_term(c, depth - 1);
        }
        if (index != count) { fputs("...", stderr); c->p = c->end; }
        fputc('}', stderr);
        return;
    } else if (tag == ETF_LIST || tag == ETF_NIL) {
        if (!get_list(c, &count)) goto invalid;
        fputc('[', stderr);
        for (index = 0; index < count && index < 12 && c->p != c->end; ++index) {
            if (index) fputc(',', stderr);
            print_term(c, depth - 1);
        }
        if (index != count) { fputs("...", stderr); c->p = c->end; }
        else if (count) (void)nil(c);
        fputc(']', stderr);
        return;
    } else if (tag == ETF_STRING) {
        char *text = NULL;
        if (get_text(c, &text)) { fprintf(stderr, "\"%.160s\"", text); free(text); return; }
    }
invalid:
    *c = saved;
    fputs("<term>", stderr);
    c->p = c->end;
}

static int result_status(beam_result *result, beam_compiled_source *compiled)
{
    cursor c;
    int64_t status;
    unsigned version;
    char tag[16], module[1024];
    beam_bytes bytes;
    if (result->kind == BEAM_HALT)
        return result->halt_status ? result->halt_status : 1;
    if (!result->value.data || !result->value.size)
        goto invalid;
    c = (cursor){result->value.data, result->value.data + result->value.size};
    if (!get_byte(&c, &version) || version != ETF_VERSION)
        goto invalid;
    if (result->kind == BEAM_RETURN) {
        if (!compiled && get_integer(&c, &status) && c.p == c.end && status >= 0 && status <= 255)
            return (int)status;
        c.p = result->value.data + 1;
        if (compiled && get_tuple(&c, 3) && get_atom(&c, tag, sizeof tag) && !strcmp(tag, "ok") &&
            get_atom(&c, module, sizeof module) && get_binary(&c, &bytes) && c.p == c.end) {
            compiled->module = strdup(module);
            compiled->bytes.data = malloc(bytes.size ? bytes.size : 1);
            if (!compiled->module || !compiled->bytes.data) {
                beam_compiled_source_release(compiled);
                fputs("beam: could not retain compiled source\n", stderr);
                return 1;
            }
            memcpy(compiled->bytes.data, bytes.data, bytes.size);
            compiled->bytes.size = bytes.size;
            return 0;
        }
        c.p = result->value.data + 1;
        if (compiled && get_tuple(&c, 2) && get_atom(&c, tag, sizeof tag) && !strcmp(tag, "error") &&
            get_integer(&c, &status) && c.p == c.end && status > 0 && status <= 255)
            return (int)status;
    }
invalid:
    fputs("beam: compiler execution failed", stderr);
    if (result->value.data && result->value.size > 1) {
        c = (cursor){result->value.data + 1, result->value.data + result->value.size};
        fputs(": ", stderr);
        print_term(&c, 7);
    }
    fputc('\n', stderr);
    return 1;
}

int beam_compiler_execute(beam_system *system, const char *root,
                          const char *source, int argc, char **argv,
                          beam_compiled_source *compiled)
{
    beam_vm_options options;
    beam_vm *vm = NULL;
    beam_result result = {0};
    beam_error error = {0};
    beam_status status;
    compiler_context context = {0};
    buffer call = {0};
    char path[PATH_MAX], *cwd = NULL;
    size_t index;
    int exit_status = 1, halt_status = 0;
    if (!system || !root || argc < 0 || (argc && !argv) ||
        (!!source != !!compiled) ||
        (compiled && (compiled->module || compiled->bytes.data || compiled->bytes.size)))
        return 1;
    for (index = 0; index < (size_t)argc; ++index)
        if (!argv[index])
            return 1;
    cwd = getcwd(NULL, 0);
    if (!cwd || !arguments(&call, root, cwd, source, argc, argv)) {
        fputs("beam: could not prepare compiler inputs\n", stderr);
        goto done;
    }
    beam_vm_options_init(&options);

    status = beam_vm_init(system, &options, &vm, &error);
    if (status != BEAM_OK)
        goto failed;
    for (index = 0; index < BEAM_COMPILER_MODULE_COUNT; ++index) {
        buffer module = {0};
        beam_module_info info = {.struct_size = sizeof info};
        int native_error;
        if (!module_path(path, sizeof path, root, beam_compiler_modules[index])) {
            fputs("beam: compiler module path is too long\n", stderr);
            goto done;
        }
        native_error = read_file(path, &module);
        if (native_error) {
            fprintf(stderr, "beam: cannot read compiler module %s: %s\n", path, strerror(native_error));
            free(module.data);
            goto done;
        }
        status = beam_vm_load(vm, (beam_bytes){module.data, module.size});
        free(module.data);
        if (status != BEAM_OK) {
            fprintf(stderr, "beam: cannot load compiler module %s: %s\n", path, beam_status_message(status));
            goto done;
        }
        status = beam_vm_get_module(vm, beam_compiler_modules[index], &info);
        if (status != BEAM_OK) {
            fprintf(stderr, "beam: cannot inspect compiler module %s: %s\n", path, beam_status_message(status));
            goto done;
        }
        if (info.flags != BEAM_MODULE_CURRENT) {
            fprintf(stderr, "beam: compiler module identity mismatch: %s does not contain %s\n",
                    path, beam_compiler_modules[index]);
            goto done;
        }
    }
    status = beam_vm_set_host_handler(vm, compiler_request, &context);
    if (status == BEAM_OK)
        status = beam_vm_start(vm, &error);
    if (status == BEAM_OK)
        status = beam_vm_call(vm, "beam_compiler_cli", source ? "run" : BEAM_COMPILER_COMMAND_FUNCTION,
                              (beam_bytes){call.data, call.size}, &result);
    if (status != BEAM_OK)
        goto failed;
    exit_status = result_status(&result, compiled);
    goto done;
failed:
    fprintf(stderr, "beam: compiler execution failed: %s\n",
            error.message[0] ? error.message : beam_status_message(status));
done:
    beam_result_release(&result);
    if (vm && beam_vm_terminate(&vm, &halt_status) != BEAM_OK) {
        fputs("beam: could not terminate compiler VM\n", stderr);
        exit_status = 1;
    }
    while (context.writers) {
        if (close_writer(&context.writers)) {
            fputs("beam: could not close compiler file\n", stderr);
            exit_status = 1;
        }
    }
    free(cwd);
    free(call.data);
    if (exit_status && compiled)
        beam_compiled_source_release(compiled);
    return exit_status;
}

void beam_compiled_source_release(beam_compiled_source *compiled)
{
    if (compiled) {
        free(compiled->module);
        free(compiled->bytes.data);
        memset(compiled, 0, sizeof(*compiled));
    }
}

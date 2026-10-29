/* SPDX-License-Identifier: Apache-2.0 */
/* Host-owned source execution and terminal I/O for the BEAM boot. */
#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "beam_cli_recipe.h"
#include "../compiler/beam_compiler_host.h"
#include "../compiler/beam_compiler_files.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <wchar.h>

#include "../cli/beam_cli_etf.h"
static char *cli_current_directory(char *buffer, size_t capacity)
{
    char *path = beam_host_cwd();
    if (!path) return NULL;
    if (strlen(path) >= capacity) { free(path); errno = ENAMETOOLONG; return NULL; }
    strcpy(buffer, path); free(path); return buffer;
}

#ifdef _WIN32
static int cli_environment(const char *name, const char *value)
{
    wchar_t *wide_name = beam_windows_text(name), *wide_value = beam_windows_text(value ? value : "");
    if (!wide_name || !wide_value) { free(wide_name); free(wide_value); return -1; }
    int result = _wputenv_s(wide_name, wide_value);
    free(wide_name); free(wide_value); return result ? -1 : 0;
}
#define setenv(name,value,overwrite) cli_environment(name,value)
#define unsetenv(name) cli_environment(name,NULL)
#define CLI_HOST_PLATFORM "windows"
#define CLI_EXECUTABLE "beam.exe"
#else
#define CLI_HOST_PLATFORM "posix"
#define CLI_EXECUTABLE "beam"
#endif
#if !defined(_WIN32) && !defined(_NSIG)
#define _NSIG NSIG
#endif

typedef struct cli_input_state {
    byte_buffer line;
    size_t cursor;
    int delivered;
    int eof;
    int error;
    int tty;
    int saved_terminal;
    int skip_lf;
#ifdef _WIN32
    DWORD input_mode, output_mode;
    UINT input_page, output_page;
    unsigned char pending[16];
    size_t pending_offset, pending_size;
    wchar_t surrogate;
#else
    struct termios terminal;
    locale_t locale;
#endif
    char escape[16];
    size_t escape_size;
    char *history[100];
    size_t history_count;
    size_t history_position;
    char *draft;
} cli_input_state;

#ifdef _WIN32
#include "beam_cli_windows.h"
#else
static int cli_read_byte(cli_input_state *input, unsigned char *character, int timeout)
{
    (void)input;
    struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
    int ready = poll(&descriptor, 1, timeout);
    if (ready < 0 && errno == EINTR) return -2;
    if (ready < 0 || (descriptor.revents & (POLLERR | POLLNVAL))) return -1;
    if (!ready) return -2;
    ssize_t count = read(STDIN_FILENO, character, 1);
    return count < 0 && (errno == EINTR || errno == EAGAIN) ? -2 : (int)count;
}
static int cli_window_size(unsigned *columns, unsigned *rows)
{
    struct winsize geometry;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &geometry)) return 0;
    if (geometry.ws_col) *columns = geometry.ws_col;
    if (geometry.ws_row) *rows = geometry.ws_row;
    return 1;
}
#endif

static beam_output_status cli_output(void *context, beam_bytes destination, beam_bytes bytes)
{
    char name[64];
    FILE *out;
    (void)context;
    if (!destination.data || !destination.size || destination.size >= sizeof(name))
        return BEAM_OUTPUT_UNKNOWN;
    memcpy(name, destination.data, destination.size);
    name[destination.size] = '\0';
    if (strcmp(name, "standard_output") == 0)
        out = stdout;
    else if (strcmp(name, "standard_error") == 0)
        out = stderr;
    else
        return BEAM_OUTPUT_UNKNOWN;
    if (bytes.size && !bytes.data) return BEAM_OUTPUT_FAILED;
    /* Keep failed output out of stdio's buffer: some implementations retry a
     * failed flush at process exit, after the caller restores SIGPIPE policy. */
    flockfile(out);
    int failed = fflush(out) != 0;
    size_t offset = 0;
    while (!failed && offset < bytes.size) {
        ssize_t written = beam_host_write(fileno(out), bytes.data + offset, bytes.size - offset);
        if (written > 0) offset += (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else failed = 1;
    }
    funlockfile(out);
    return failed ? BEAM_OUTPUT_FAILED : BEAM_OUTPUT_OK;
}

/* The source runner accepts only explicit UTF-8 file identities. This is a
 * host read, never a code-path search or a call back into the running VM. */
static int utf8_path(beam_bytes source)
{
    size_t i = 0;
    if (!source.data || source.size < 6 || source.size >= PATH_MAX ||
        !(source.data[0] == '/'
#ifdef _WIN32
          || (source.size >= 3 && source.data[1] == ':' && (source.data[2] == '/' || source.data[2] == '\\'))
          || (source.data[0] == '\\' && source.data[1] == '\\')
#endif
         ) ||
        memcmp(source.data + source.size - 5, ".beam", 5) != 0)
        return 0;
    while (i < source.size) {
        unsigned c = source.data[i++], value, count, minimum;
        if (c == 0)
            return 0;
        if (c < 0x80)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            value = c & 0x1f; count = 1; minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            value = c & 0x0f; count = 2; minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            value = c & 7; count = 3; minimum = 0x10000;
        } else {
            return 0;
        }
        while (count--) {
            if (i == source.size || (source.data[i] & 0xc0) != 0x80)
                return 0;
            value = (value << 6) | (source.data[i++] & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return 0;
    }
    return 1;
}

static beam_code_status cli_code(void *context, beam_bytes source, beam_bytes module,
                                 const unsigned char **bytes, size_t *size)
{
    byte_buffer *buffer = context;
    char path[PATH_MAX];
    beam_host_stat st;
    int fd;
    unsigned char trailing;
    ssize_t got;
    beam_code_status status = BEAM_CODE_UNAVAILABLE;
    (void)module; /* Module identity is validated by beam_code:load/3. */
    *bytes = NULL;
    *size = 0;
    buffer->size = 0;
    if (!utf8_path(source))
        return BEAM_CODE_INVALID;
    memcpy(path, source.data, source.size);
    path[source.size] = '\0';
    do {
        fd = beam_host_open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return errno == ENOENT || errno == ENOTDIR ? BEAM_CODE_NOT_FOUND : BEAM_CODE_UNAVAILABLE;
    if (beam_host_fstat(fd, &st) != 0)
        goto done;
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || (uintmax_t)st.st_size > UINT32_MAX) {
        status = BEAM_CODE_INVALID;
        goto done;
    }
    if (!buffer_reserve(buffer, (size_t)st.st_size))
        goto done;
    while (buffer->size < (size_t)st.st_size) {
        size_t remaining = (size_t)st.st_size - buffer->size;
        if (remaining > SSIZE_MAX)
            remaining = SSIZE_MAX;
        got = beam_host_read(fd, buffer->data + buffer->size, remaining);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            goto done;
        buffer->size += (size_t)got;
    }
    do {
        got = beam_host_read(fd, &trailing, 1);
    } while (got < 0 && errno == EINTR);
    if (got != 0)
        goto done;
    status = BEAM_CODE_OK;
done:
    if (beam_host_close(fd) != 0)
        status = BEAM_CODE_UNAVAILABLE;
    if (status == BEAM_CODE_OK) {
        *bytes = buffer->data;
        *size = buffer->size;
    }
    return status;
}

static int terminal_write(cli_input_state *input, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        ssize_t written = beam_host_write(STDOUT_FILENO, bytes, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            input->error = 1;
            return 0;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return 1;
}

static size_t previous_character(const byte_buffer *line, size_t cursor)
{
    if (cursor) {
        --cursor;
        while (cursor && (line->data[cursor] & 0xc0) == 0x80)
            --cursor;
    }
    return cursor;
}

static size_t next_character(const byte_buffer *line, size_t cursor)
{
    if (cursor < line->size) {
        ++cursor;
        while (cursor < line->size && (line->data[cursor] & 0xc0) == 0x80)
            ++cursor;
    }
    return cursor;
}

static size_t terminal_columns(cli_input_state *input, size_t begin, size_t end)
{
#ifdef _WIN32
    size_t columns = 0;
    while (begin < end) {
        const unsigned char *p = input->line.data + begin;
        size_t count = p[0] < 0x80 ? 1 : p[0] < 0xe0 ? 2 : p[0] < 0xf0 ? 3 : 4;
        if (count > end - begin) break;
        wchar_t wide[2]; WORD kind[2] = {0};
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)p, (int)count, wide, 2);
        if (!length) { ++begin; ++columns; continue; }
        if (!GetStringTypeW(CT_CTYPE3, wide, length, kind)) { input->error = 1; break; }
        unsigned point = length == 2 ? 0x10000 + ((wide[0]-0xd800)<<10) + wide[1]-0xdc00 : wide[0];
        if (!(kind[0] & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)))
            columns += (kind[0] & (C3_FULLWIDTH | C3_IDEOGRAPH)) || (point >= 0x1f300 && point <= 0x1faff) ? 2 : 1;
        begin += count;
    }
    return columns;
#else
    mbstate_t state = {0};
    locale_t previous = input->locale ? uselocale(input->locale) : (locale_t)0;
    size_t columns = 0;
    while (begin < end) {
        wchar_t character;
        size_t count = mbrtowc(&character, (const char *)input->line.data + begin,
                              end - begin, &state);
        if (count == (size_t)-2)
            break;
        if (count == (size_t)-1 || count == 0) {
            memset(&state, 0, sizeof(state));
            count = 1;
            columns++;
        } else {
            int width = wcwidth(character);
            columns += width < 0 ? 1 : (size_t)width;
        }
        begin += count;
    }
    if (input->locale)
        uselocale(previous);
    return columns;
#endif
}

static void terminal_move(cli_input_state *input, size_t columns, char direction)
{
    char sequence[64];
    if (columns) {
        int size = snprintf(sequence, sizeof(sequence), "\033[%zu%c", columns, direction);
        (void)terminal_write(input, sequence, (size_t)size);
    }
}

static void input_move(cli_input_state *input, size_t cursor)
{
    if (cursor < input->cursor)
        terminal_move(input, terminal_columns(input, cursor, input->cursor), 'D');
    else
        terminal_move(input, terminal_columns(input, input->cursor, cursor), 'C');
    input->cursor = cursor;
}

static void input_redraw_tail(cli_input_state *input)
{
    if (input->line.size != input->cursor)
        (void)terminal_write(input, input->line.data + input->cursor,
                             input->line.size - input->cursor);
    (void)terminal_write(input, "\033[K", 3);
    terminal_move(input, terminal_columns(input, input->cursor, input->line.size), 'D');
}

static void input_delete(cli_input_state *input, size_t begin, size_t end)
{
    if (begin == end)
        return;
    input_move(input, begin);
    memmove(input->line.data + begin, input->line.data + end, input->line.size - end);
    input->line.size -= end - begin;
    input_redraw_tail(input);
}

static int input_insert(cli_input_state *input, unsigned char character)
{
    if (!buffer_reserve(&input->line, 1)) {
        input->error = 1;
        return 0;
    }
    memmove(input->line.data + input->cursor + 1, input->line.data + input->cursor,
            input->line.size - input->cursor);
    input->line.data[input->cursor++] = character;
    input->line.size++;
    if (input->tty) {
        size_t begin = previous_character(&input->line, input->cursor);
        size_t length = input->cursor - begin;
        unsigned char first = input->line.data[begin];
        size_t expected = first >= 0xc2 && first <= 0xdf ? 2 :
                          first >= 0xe0 && first <= 0xef ? 3 :
                          first >= 0xf0 && first <= 0xf4 ? 4 : 1;
        /* Tail redraws contain terminal controls. Keep them outside a UTF-8
         * character even when its bytes arrive in separate callbacks. */
        if (length < expected)
            return 1;
        (void)terminal_write(input, input->line.data + begin, length);
        if (input->cursor != input->line.size)
            input_redraw_tail(input);
    }
    return 1;
}

static void input_history(cli_input_state *input, int direction)
{
    size_t position = input->history_position;
    const char *text;
    if (direction < 0) {
        if (!position)
            return;
        if (position == input->history_count) {
            free(input->draft);
            input->draft = malloc(input->line.size + 1);
            if (!input->draft) {
                input->error = 1;
                return;
            }
            if (input->line.size)
                memcpy(input->draft, input->line.data, input->line.size);
            input->draft[input->line.size] = '\0';
        }
        --position;
    } else {
        if (position == input->history_count)
            return;
        ++position;
    }
    text = position == input->history_count ? input->draft : input->history[position];
    input_move(input, 0);
    input->line.size = 0;
    if (!buffer_bytes(&input->line, text ? text : "", text ? strlen(text) : 0)) {
        input->error = 1;
        return;
    }
    input_redraw_tail(input);
    input_move(input, input->line.size);
    input->history_position = position;
}

static void input_escape(cli_input_state *input, unsigned char character)
{
    if (input->escape_size == sizeof(input->escape) - 1) {
        input->escape_size = 0;
        return;
    }
    input->escape[input->escape_size++] = (char)character;
    if (input->escape_size == 1 && (character == '[' || character == 'O'))
        return;
    if (character >= '0' && character <= '9')
        return;
    if (character == ';')
        return;
    input->escape[input->escape_size] = '\0';
    if (strcmp(input->escape, "[A") == 0)
        input_history(input, -1);
    else if (strcmp(input->escape, "[B") == 0)
        input_history(input, 1);
    else if (strcmp(input->escape, "[C") == 0)
        input_move(input, next_character(&input->line, input->cursor));
    else if (strcmp(input->escape, "[D") == 0)
        input_move(input, previous_character(&input->line, input->cursor));
    else if (strcmp(input->escape, "[H") == 0 || strcmp(input->escape, "OH") == 0 ||
             strcmp(input->escape, "[1~") == 0 || strcmp(input->escape, "[7~") == 0)
        input_move(input, 0);
    else if (strcmp(input->escape, "[F") == 0 || strcmp(input->escape, "OF") == 0 ||
             strcmp(input->escape, "[4~") == 0 || strcmp(input->escape, "[8~") == 0)
        input_move(input, input->line.size);
    else if (strcmp(input->escape, "[3~") == 0)
        input_delete(input, input->cursor, next_character(&input->line, input->cursor));
    input->escape_size = 0;
}

static beam_input_status input_deliver(cli_input_state *input, int newline,
                                       const unsigned char **bytes, size_t *size)
{
    if (input->tty && input->line.size) {
        char *history = malloc(input->line.size + 1);
        if (!history) {
            input->error = 1;
            return BEAM_INPUT_EOF;
        }
        memcpy(history, input->line.data, input->line.size);
        history[input->line.size] = '\0';
        if (input->history_count == sizeof(input->history) / sizeof(input->history[0])) {
            free(input->history[0]);
            memmove(input->history, input->history + 1,
                    (--input->history_count) * sizeof(input->history[0]));
        }
        input->history[input->history_count++] = history;
    }
    if (newline && !buffer_u8(&input->line, '\n')) {
        input->error = 1;
        return BEAM_INPUT_EOF;
    }
    input->delivered = 1;
    *bytes = input->line.data;
    *size = input->line.size;
    return BEAM_INPUT_LINE;
}

static beam_input_status cli_input(void *context, const unsigned char **bytes, size_t *size)
{
    cli_input_state *input = context;
    unsigned processed;
    *bytes = NULL;
    *size = 0;
    if (input->delivered) {
        input->line.size = input->cursor = 0;
        input->delivered = 0;
        input->history_position = input->history_count;
        free(input->draft);
        input->draft = NULL;
    }
    if (input->error || input->eof)
        return BEAM_INPUT_EOF;
    /* Bound each owner-thread callback so output and stop can make progress. */
    for (processed = 0; processed < 256; ++processed) {
        unsigned char character;
        ssize_t count = cli_read_byte(input, &character, processed ? 0 : 10);
        if (count == -2) return input->line.size && !input->tty
            ? input_deliver(input, 0, bytes, size) : BEAM_INPUT_PENDING;
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            return BEAM_INPUT_PENDING;
        if (count <= 0) {
            input->eof = 1;
            if (count < 0)
                input->error = 1;
            return input->line.size ? input_deliver(input, 0, bytes, size) : BEAM_INPUT_EOF;
        }
        if (input->skip_lf) {
            input->skip_lf = 0;
            if (character == '\n')
                continue;
        }
        if (input->tty && input->escape_size) {
            if (input->escape_size == (size_t)-1)
                input->escape_size = 0;
            input_escape(input, character);
            continue;
        }
        if (character == '\n' || character == '\r') {
            input->skip_lf = character == '\r';
            if (input->tty)
                (void)terminal_write(input, "\n", 1);
            return input_deliver(input, 1, bytes, size);
        }
        if (input->tty) {
            switch (character) {
            case 27: input->escape_size = (size_t)-1; continue;
            case 1: input_move(input, 0); continue;
            case 5: input_move(input, input->line.size); continue;
            case 2: input_move(input, previous_character(&input->line, input->cursor)); continue;
            case 6: input_move(input, next_character(&input->line, input->cursor)); continue;
            case 16: input_history(input, -1); continue;
            case 14: input_history(input, 1); continue;
            case 8: case 127:
                input_delete(input, previous_character(&input->line, input->cursor), input->cursor);
                continue;
            case 21: input_delete(input, 0, input->cursor); continue;
            case 23: {
                size_t begin = input->cursor;
                while (begin && isspace(input->line.data[begin - 1]))
                    begin = previous_character(&input->line, begin);
                while (begin && !isspace(input->line.data[begin - 1]))
                    begin = previous_character(&input->line, begin);
                input_delete(input, begin, input->cursor);
                continue;
            }
            case 11: input_delete(input, input->cursor, input->line.size); continue;
            case 4:
                if (!input->line.size) {
                    input->eof = 1;
                    return BEAM_INPUT_EOF;
                }
                input_delete(input, input->cursor, next_character(&input->line, input->cursor));
                continue;
            case 3:
                input->line.size = input->cursor = 0;
                if (!buffer_u8(&input->line, 3)) {
                    input->error = 1;
                    return BEAM_INPUT_EOF;
                }
                (void)terminal_write(input, "^C\n", 3);
                input->delivered = 1;
                *bytes = input->line.data;
                *size = 1;
                return BEAM_INPUT_LINE;
            default: break;
            }
        }
        if (!input_insert(input, character))
            return BEAM_INPUT_EOF;
    }
    return BEAM_INPUT_PENDING;
}

static beam_input_status cli_run_input(void *context, const unsigned char **bytes,
                                       size_t *size)
{
    cli_input_state *input = context;
    unsigned processed;
    *bytes = NULL;
    *size = 0;
    if (input->delivered) {
        input->line.size = input->cursor = 0;
        input->delivered = 0;
    }
    if (input->error || input->eof)
        return BEAM_INPUT_EOF;

    /* Source I/O collects characters and lines in Erlang. Deliver available
     * bytes without waiting for a newline; keep each owner-thread turn bounded.
     * UTF-8 sequences may span deliveries and are decoded by the run reader. */
    for (processed = 0; processed < 256; ++processed) {
        unsigned char character;
        ssize_t count = cli_read_byte(input, &character, processed ? 0 : 10);
        if (count == -2) return input->line.size && !input->tty
            ? input_deliver(input, 0, bytes, size) : BEAM_INPUT_PENDING;
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            break;
        if (count <= 0) {
            input->eof = 1;
            if (count < 0)
                input->error = 1;
            break;
        }
        if (input->skip_lf) {
            input->skip_lf = 0;
            if (character == '\n')
                continue;
        }
        if (character == '\n' || character == '\r') {
            input->skip_lf = character == '\r';
            return input_deliver(input, 1, bytes, size);
        }
        if (!buffer_u8(&input->line, character)) {
            input->error = 1;
            break;
        }
    }
    if (input->line.size)
        return input_deliver(input, 0, bytes, size);
    return input->error || input->eof ? BEAM_INPUT_EOF : BEAM_INPUT_PENDING;
}

static int input_initialize(cli_input_state *input, int shell)
{
#ifdef _WIN32
    return cli_console_initialize(input, shell);
#else
    struct termios raw;
    input->tty = shell && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (!input->tty)
        return 1;
    if (tcgetattr(STDIN_FILENO, &input->terminal) != 0)
        return 0;
    raw = input->terminal;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN | ISIG);
    raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return 0;
    input->saved_terminal = 1;
    input->locale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    if (!input->locale)
        input->locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
    return 1;
#endif
}

static int input_destroy(cli_input_state *input)
{
    size_t index;
    int result = 1;
#ifdef _WIN32
    result = cli_console_restore(input);
#else
    if (input->saved_terminal) {
        int restored;
        do {
            restored = tcsetattr(STDIN_FILENO, TCSANOW, &input->terminal);
        } while (restored != 0 && errno == EINTR);
        result = restored == 0;
    }
#endif
    for (index = 0; index < input->history_count; ++index)
        free(input->history[index]);
    free(input->draft);
    free(input->line.data);
#ifndef _WIN32
    if (input->locale)
        freelocale(input->locale);
#endif
    return result;
}

static int discover_root(char *root, beam_error *error)
{
    char library[4096];
    char *slash;
    size_t required;
    if (beam_library_path(library, sizeof(library), &required, error) != BEAM_OK)
        return 0;
    slash = strrchr(library, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    slash = strrchr(library, '/');
    if (!slash)
        return 0;
    if (slash == library)
        slash[1] = '\0';
    else
        *slash = '\0';
    if (strlen(library) >= 4096)
        return 0;
    memcpy(root, library, strlen(library) + 1);
    return 1;
}

static int encode_arguments(byte_buffer *buffer, const char *root, int argc, char **argv,
                            const char *command, const char *value,
                            unsigned columns, unsigned rows, int terminal,
                            const beam_compiled_source *compiled)
{
    int index;
    if (!buffer_u8(buffer, ETF_VERSION) || !buffer_u8(buffer, ETF_LIST) ||
        !buffer_u32(buffer, 1) || !buffer_u8(buffer, ETF_SMALL_TUPLE) ||
        !buffer_u8(buffer, compiled ? 4 : 5) || !buffer_binary(buffer, root))
        return 0;
    if (!argc) {
        if (!buffer_u8(buffer, ETF_NIL))
            return 0;
    } else {
        if (!buffer_u8(buffer, ETF_LIST) || !buffer_u32(buffer, (unsigned)argc))
            return 0;
        for (index = 0; index < argc; ++index)
            if (!buffer_charlist(buffer, argv[index] ? argv[index] : ""))
                return 0;
        if (!buffer_u8(buffer, ETF_NIL))
            return 0;
    }
    if (strcmp(command, "shell") == 0) {
        if (!buffer_atom(buffer, "shell"))
            return 0;
    } else if (compiled) {
        if (compiled->bytes.size > UINT32_MAX ||
            !buffer_u8(buffer, ETF_SMALL_TUPLE) || !buffer_u8(buffer, 4) ||
            !buffer_atom(buffer, "run") || !buffer_binary(buffer, value) ||
            !buffer_atom(buffer, compiled->module) || !buffer_u8(buffer, ETF_BINARY) ||
            !buffer_u32(buffer, (unsigned)compiled->bytes.size) ||
            !buffer_bytes(buffer, compiled->bytes.data, compiled->bytes.size))
            return 0;
    } else if (!buffer_u8(buffer, ETF_SMALL_TUPLE) || !buffer_u8(buffer, 2) ||
               !buffer_atom(buffer, command) || !buffer_binary(buffer, value ? value : "")) {
        return 0;
    }
    return buffer_u8(buffer, ETF_SMALL_TUPLE) && buffer_u8(buffer, 2) &&
           buffer_u8(buffer, ETF_INTEGER) && buffer_u32(buffer, columns) &&
           buffer_u8(buffer, ETF_INTEGER) && buffer_u32(buffer, rows) &&
           (compiled || buffer_atom(buffer, terminal ? "true" : "false")) &&
           buffer_u8(buffer, ETF_NIL);
}

static int outcome_status(const beam_outcome *outcome)
{
    if (outcome->kind == BEAM_OUTCOME_HALT || outcome->kind == BEAM_OUTCOME_FINISH)
        return outcome->halt_status < 0 ? 1 : outcome->halt_status;
    if (outcome->kind == BEAM_OUTCOME_RETURN)
        return 0;
    if (outcome->has_cleanup_error)
        fprintf(stderr, "beam: %s\n", outcome->cleanup_error);
    return 1;
}

int beam_boot_main(const char *command, const char *value, int plain_count, char **plain)
{
    char root[4096];
    char *saved_erl_libs = NULL;
    int had_erl_libs = 0, changed_environment = 0;
    unsigned columns = 80, rows = 24;
    cli_input_state input = {0};
    byte_buffer arguments = {0};
    byte_buffer code = {0};
    char *source_origin = NULL;
    beam_system *system = NULL;
    beam_compiled_source compiled = {0};
    beam_return_boot *boot = NULL;
    beam_cli_recipe_return_options options = {0};
    beam_outcome outcome = {0};
    beam_error error = {0};
    beam_status status;
    int exit_status = 1;

    if (!discover_root(root, &error)) {
        fprintf(stderr, "beam: %s\n", error.message[0] ? error.message : "could not locate runtime");
        return 1;
    }
    status = beam_init(&system, &error);
    if (status != BEAM_OK) {
        fprintf(stderr, "beam: %s\n", error.message[0] ? error.message : beam_status_message(status));
        goto cleanup;
    }
    if (strcmp(command, "compile") == 0) {
        exit_status = beam_compiler_execute(system, root, NULL, plain_count, plain, NULL);
        goto cleanup;
    }
    if (strcmp(command, "run") == 0) {
        exit_status = beam_compiler_execute(system, root, value, 0, NULL, &compiled);
        if (exit_status)
            goto cleanup;
        exit_status = 1;
        if (beam_path_absolute(value)) {
            source_origin = strdup(value);
        } else {
            char *cwd = beam_host_cwd();
            if (cwd) {
                size_t length = strlen(cwd) + strlen(value) + 2;
                source_origin = malloc(length);
                if (source_origin)
                    snprintf(source_origin, length, "%s/%s", cwd, value);
                free(cwd);
            }
        }
        if (!source_origin) {
            fputs("beam: could not retain source identity\n", stderr);
            goto cleanup;
        }
    }
    (void)cli_window_size(&columns, &rows);
    if (!encode_arguments(&arguments, root, plain_count, plain, command,
                          source_origin ? source_origin : value,
                          columns, rows, strcmp(command, "shell") == 0 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO),
                          compiled.module ? &compiled : NULL)) {
        fprintf(stderr, "beam: out of memory\n");
        goto cleanup;
    }
    outcome.struct_size = sizeof(outcome);
    options.struct_size = sizeof(options);
    options.services = BEAM_CLI_OUTPUT | BEAM_CLI_CODE | BEAM_CLI_FILE |
                       BEAM_CLI_PROGRAM | BEAM_CLI_SOCKET | BEAM_CLI_ZLIB;
    options.runtime_root = root;
    options.profile = compiled.module ? BEAM_CLI_RUN : BEAM_CLI_INTERACTIVE;
    options.entry_module = compiled.module ? "beam_source_run" : "beam_interactive";
    options.entry_function = "main";
    options.arguments = (beam_bytes){arguments.data, arguments.size};
    options.output = cli_output;
    options.input = strcmp(command, "shell") == 0 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)
                  ? cli_input : cli_run_input;
    options.input_context = &input;
    options.code = cli_code;
    options.code_context = &code;
    {
        const char *current = getenv("ERL_LIBS");
        if (current) {
            had_erl_libs = 1;
            saved_erl_libs = beam_host_environment("ERL_LIBS");
            if (!saved_erl_libs) {
                fputs("beam: could not save ERL_LIBS\n", stderr);
                goto cleanup;
            }
        }
    }
    if (unsetenv("ERL_LIBS") != 0) {
        fputs("beam: could not clear ERL_LIBS\n", stderr);
        goto cleanup;
    }
    changed_environment = 1;
    if (!input_initialize(&input, strcmp(command, "shell") == 0)) {
        fprintf(stderr, "beam: could not initialize terminal: %s\n", strerror(errno));
        goto cleanup;
    }
    status = beam_cli_recipe_return_prepare(&options, &boot, &error);
    if (status == BEAM_OK)
        status = beam_return_run(system, &boot, &outcome, &error);
    if (status != BEAM_OK) {
        fprintf(stderr, "beam: %s\n", error.message[0] ? error.message : beam_status_message(status));
    } else {
        if (outcome.kind != BEAM_OUTCOME_RETURN && outcome.kind != BEAM_OUTCOME_HALT &&
            outcome.kind != BEAM_OUTCOME_FINISH) {
            fprintf(stderr, "beam: application failed (outcome %d)\n", (int)outcome.kind);
            if (outcome.term.data)
                fwrite(outcome.term.data, 1, outcome.term.size, stderr);
            fputc('\n', stderr);
        }
        exit_status = outcome_status(&outcome);
    }
cleanup:
    beam_compiled_source_release(&compiled);
    beam_outcome_release(&outcome);
    if (boot)
        (void)beam_return_discard(&boot);
    if (system)
        (void)beam_terminate(&system);
    if (!input_destroy(&input)) {
        fputs("beam: could not restore terminal state\n", stderr);
        exit_status = 1;
    }
    if (input.error) {
        fputs("beam: terminal input failed\n", stderr);
        exit_status = 1;
    }
    if (changed_environment &&
        (had_erl_libs ? setenv("ERL_LIBS", saved_erl_libs, 1) : unsetenv("ERL_LIBS")) != 0) {
        fputs("beam: could not restore ERL_LIBS\n", stderr);
        exit_status = 1;
    }
    free(saved_erl_libs);
    free(source_origin);
    free(code.data);
    free(arguments.data);
    return exit_status;
}

/* General installed application execution. No package formats or policy. */
#include "beam_bytecode_internal.h"
#define APP_LIMIT (256u * 1024u * 1024u)
#define APP_MODULE_LIMIT 4096u

typedef struct application {
    char *root, *module, *function;
    char contract[24];
    unsigned services;
    beam_return_module *modules;
    size_t count;
    beam_cli_nif *nifs;
    size_t nif_count;
    char **required;
    size_t required_count;
    beam_bytes descriptor;
} application;

static int etf_number(etf_cursor *c, size_t *n)
{
    if (c->p < c->end && *c->p == 97) {
        ++c->p; if (c->p == c->end) return 0; *n = *c->p++; return 1;
    }
    return etf_tag(c, ETF_INTEGER) && etf_u32(c, n) && *n <= 0x7fffffff;
}
static int etf_named(etf_cursor *c, const char *name)
{ char atom[64]; return etf_atom(c, atom, sizeof(atom)) && !strcmp(atom, name); }
static int etf_end_list(etf_cursor *c, size_t count)
{ return !count || etf_tag(c, ETF_NIL); }
static void application_release(application *app)
{
    for (size_t i = 0; app->modules && i < app->count; ++i) {
        free((void *)app->modules[i].name); free((void *)app->modules[i].origin);
        free((void *)app->modules[i].bytes.data);
    }
    for (size_t i = 0; app->nifs && i < app->nif_count; ++i) {
        free((void *)app->nifs[i].module); free((void *)app->nifs[i].path);
    }
    for (size_t i = 0; app->required && i < app->required_count; ++i) free(app->required[i]);
    free(app->modules); free(app->nifs); free(app->required);
    free(app->root); free(app->module); free(app->function);
    free((void *)app->descriptor.data); memset(app, 0, sizeof(*app));
}
static int application_file(const char *path, size_t limit, beam_bytes *bytes)
{
    beam_host_stat st; FILE *f; unsigned char *data; int valid;
    if (beam_host_stat_path(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uintmax_t)st.st_size > limit) return 0;
    data = malloc((size_t)st.st_size + 1); if (!data) return 0;
    f = beam_host_fopen(path, "rb"); if (!f) { free(data); return 0; }
    valid = fread(data, 1, (size_t)st.st_size, f) == (size_t)st.st_size && fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) valid = 0;
    if (!valid) { free(data); return 0; }
    *bytes = (beam_bytes){data, (size_t)st.st_size}; return 1;
}
static char *application_path(const char *root, const char *relative)
{
    char path[PATH_MAX], resolved[PATH_MAX];
    size_t n = strlen(root);
    if (!relative || !*relative || relative[0] == '/' || strchr(relative, '\\') || strchr(relative, ':') ||
        snprintf(path, sizeof(path), "%s/%s", root, relative) >= sizeof(path) ||
        !beam_host_realpath(path, resolved) || strncmp(root, resolved, n) ||
        (n > 1 && resolved[n] && resolved[n] != '/')) return NULL;
    return strdup(resolved);
}
static int application_parse(etf_cursor *c, const char *base, int continuation, application *app)
{
    const unsigned char *start = c->p;
    size_t version, count, total = 0; char *id = NULL, *release = NULL, *root = NULL;
    char canonical[PATH_MAX]; int ok = 0;
    if (!etf_tuple(c, 12) || !etf_named(c, "beam_application") || !etf_number(c, &version) || version != 1 ||
        !(id = etf_string(c)) || !(release = etf_string(c)) || !(root = etf_string(c))) goto done;
    if (continuation && beam_path_absolute(root)) {
        if (!beam_host_realpath(root, canonical)) goto done;
        app->root = strdup(canonical);
    } else app->root = application_path(base, root);
    if (!app->root || !etf_tuple(c, 3) || !etf_atom(c, app->contract, sizeof(app->contract)) ||
        (strcmp(app->contract,"main") && strcmp(app->contract,"command") && strcmp(app->contract,"shell")) ||
        !(app->module = etf_string(c)) || !(app->function = etf_string(c))) goto done;
    if (!etf_list(c, &count) || count > 16) goto done;
    for (size_t i = 0; i < count; ++i) {
        char feature[32];
        if (!etf_atom(c, feature, sizeof(feature)) ||
            (strcmp(feature,"application_v1") && strcmp(feature,"command_v1") && strcmp(feature,"native_v1"))) goto done;
    }
    if (!etf_end_list(c,count) || !etf_list(c, &count) || count > 6) goto done;
    for (size_t i = 0; i < count; ++i) {
        char service[24]; unsigned bit = 0;
        if (!etf_atom(c, service, sizeof(service))) goto done;
        if (!strcmp(service,"output")) bit = BEAM_CLI_OUTPUT;
        else if (!strcmp(service,"code")) bit = BEAM_CLI_CODE;
        else if (!strcmp(service,"file")) bit = BEAM_CLI_FILE;
        else if (!strcmp(service,"network")) bit = BEAM_CLI_SOCKET;
        else if (!strcmp(service,"zlib")) bit = BEAM_CLI_ZLIB;
        else if (!strcmp(service,"program")) bit = BEAM_CLI_PROGRAM;
        if (!bit || (app->services & bit)) goto done;
        app->services |= bit;
    }
    if (!etf_end_list(c,count) || !etf_list(c, &app->count) || app->count > APP_MODULE_LIMIT) goto done;
    app->modules = calloc(app->count ? app->count : 1, sizeof(*app->modules));
    if (!app->modules) { app->count = 0; goto done; }
    for (size_t i = 0; i < app->count; ++i) {
        size_t arity; char kind[16]; beam_return_module *m = &app->modules[i];
        if (!etf_tag(c, ETF_SMALL_TUPLE) || c->p == c->end) goto done;
        arity = *c->p++;
        if (!etf_atom(c, kind, sizeof(kind)) || !(m->name = etf_string(c))) goto done;
        for (size_t j = 0; j < i; ++j) if (!strcmp(m->name, app->modules[j].name)) goto done;
        if (arity == 5 && !strcmp(kind,"file")) {
            char *relative = etf_string(c); size_t size; beam_bytes digest; unsigned char actual[32];
            if (!relative) goto done;
            m->origin = application_path(app->root, relative); free(relative);
            if (!m->origin || !etf_number(c, &size) || size > APP_LIMIT - total ||
                !etf_binary(c, &digest) || digest.size != 32 ||
                !application_file(m->origin, size, &m->bytes) || m->bytes.size != size) goto done;
            beam_bytecode_sha256(m->bytes, actual);
            if (memcmp(actual, digest.data, 32)) goto done;
        } else if (continuation && arity == 4 && !strcmp(kind,"memory")) {
            beam_bytes bytes;
            if (!etf_binary(c, &bytes) || bytes.size > APP_LIMIT - total || !(m->origin = etf_string(c))) goto done;
            unsigned char *copy = malloc(bytes.size ? bytes.size : 1); if (!copy) goto done;
            memcpy(copy, bytes.data, bytes.size); m->bytes = (beam_bytes){copy, bytes.size};
        } else goto done;
        total += m->bytes.size;
    }
    if (!etf_end_list(c,app->count) || !etf_list(c, &app->nif_count) || app->nif_count > 1024) goto done;
    app->nifs = calloc(app->nif_count ? app->nif_count : 1, sizeof(*app->nifs));
    if (!app->nifs) { app->nif_count = 0; goto done; }
    for (size_t i = 0; i < app->nif_count; ++i) {
        char *relative;
        app->nifs[i].struct_size = sizeof(beam_cli_nif);
        if (!etf_tuple(c,2) || !(app->nifs[i].module = etf_string(c)) || !(relative = etf_string(c))) goto done;
        app->nifs[i].path = application_path(app->root, relative); free(relative);
        if (!app->nifs[i].path) goto done;
    }
    if (!etf_end_list(c,app->nif_count) || !etf_list(c,&count) || count > 256) goto done;
    int compatible = count == 0;
    for (size_t i = 0; i < count; ++i) {
        char *identity = etf_string(c); if (!identity) goto done;
        if (!strcmp(identity, beam_build_id())) compatible = 1;
        free(identity);
    }
    if (!compatible || !etf_end_list(c,count) || !etf_list(c,&app->required_count) || app->required_count > 4096) goto done;
    app->required = calloc(app->required_count ? app->required_count : 1, sizeof(char *));
    if (!app->required) { app->required_count = 0; goto done; }
    for (size_t i = 0; i < app->required_count; ++i) if (!(app->required[i] = etf_string(c))) goto done;
    if (!etf_end_list(c,app->required_count)) goto done;
    size_t length = (size_t)(c->p - start);
    unsigned char *copy = malloc(length + 1); if (!copy) goto done;
    copy[0] = ETF_VERSION; memcpy(copy + 1, start, length);
    app->descriptor = (beam_bytes){copy, length + 1}; ok = 1;
done:
    free(id); free(release); free(root);
    return ok;
}
static int application_unicode(byte_buffer *b, const char *text)
{
    byte_buffer points = {0}; size_t count = 0; const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned ch = *p++, needed = 0, minimum = 0;
        if (ch >= 0xc2 && ch <= 0xdf) { ch &= 31; needed=1; minimum=0x80; }
        else if (ch >= 0xe0 && ch <= 0xef) { ch &= 15; needed=2; minimum=0x800; }
        else if (ch >= 0xf0 && ch <= 0xf4) { ch &= 7; needed=3; minimum=0x10000; }
        else if (ch >= 0x80) goto invalid;
        for (unsigned i=0; i<needed; ++i) { if ((*p & 0xc0) != 0x80) goto invalid; ch=(ch<<6)|(*p++ & 63); }
        if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff) ||
            !buffer_u8(&points, ETF_INTEGER) || !buffer_u32(&points,ch)) goto invalid;
        ++count;
    }
    int ok = count ? buffer_u8(b,ETF_LIST) && buffer_u32(b,(unsigned)count) &&
        buffer_bytes(b,points.data,points.size) && buffer_u8(b,ETF_NIL) : buffer_u8(b,ETF_NIL);
    free(points.data); return ok;
invalid: free(points.data); return 0;
}
static int application_argv(byte_buffer *b, int argc, char **argv)
{
    if (!argc) return buffer_u8(b,ETF_NIL);
    if (!buffer_u8(b,ETF_LIST) || !buffer_u32(b,(unsigned)argc)) return 0;
    for (int i=0; i<argc; ++i) if (!application_unicode(b,argv[i])) return 0;
    return buffer_u8(b,ETF_NIL);
}
/* Continuations carry one bounded, proper list of Unicode character lists.
 * Do not allow command applications to smuggle arbitrary entry arguments. */
static int application_arguments(beam_bytes bytes)
{
    size_t count, length, point;
    if (!bytes.data || bytes.size < 2 || bytes.size > 1024*1024) return 0;
    etf_cursor c={bytes.data,bytes.data+bytes.size};
    if (!etf_tag(&c,ETF_VERSION) || !etf_list(&c,&count) || count > 4096) return 0;
    for (size_t i=0; i<count; ++i) {
        if (c.p<c.end && *c.p==ETF_STRING) {
            ++c.p;
            if(c.end-c.p<2) return 0;
            length=((size_t)c.p[0]<<8)|c.p[1]; c.p+=2;
            if(length>(size_t)(c.end-c.p) || memchr(c.p,0,length)) return 0;
            c.p+=length;
        } else {
            if(!etf_list(&c,&length) || length>65536) return 0;
            for(size_t j=0;j<length;++j)
                if(!etf_number(&c,&point) || !point || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) return 0;
            if(!etf_end_list(&c,length)) return 0;
        }
    }
    return etf_end_list(&c,count) && c.p==c.end;
}
#ifdef _WIN32
#include "beam_cli_windows_process.h"
#else
extern char **environ;
static int application_exec(char **argv, int *exit_status)
{
    (void)exit_status;
    if (strchr(argv[0],'/')) { execve(argv[0],argv,environ); return 0; }
    const char *path = getenv("PATH"); if (!path) { errno=ENOENT; return 0; }
    int denied=0; char candidate[PATH_MAX];
    for (;;) {
        const char *end=strchr(path,':'); size_t length=end ? (size_t)(end-path) : strlen(path);
        if (length+strlen(argv[0])+2 < sizeof(candidate)) {
            if (length) snprintf(candidate,sizeof(candidate),"%.*s/%s",(int)length,path,argv[0]);
            else snprintf(candidate,sizeof(candidate),"%s",argv[0]);
            execve(candidate,argv,environ);
            if (errno==EACCES) denied=1;
            else if (errno!=ENOENT && errno!=ENOTDIR) return 0;
        }
        if (!end) break;
        path=end+1;
    }
    errno=denied ? EACCES : ENOENT;
    return 0;
}
#endif

int beam_application_main(const char *descriptor, int argc, char **argv)
{
    char path[PATH_MAX], base[PATH_MAX], root[PATH_MAX], cwd[PATH_MAX], executable[PATH_MAX];
    beam_bytes bytes={0}; application app={0}; beam_error error={0};
    beam_system *system=NULL; int result=1;
    char *search = NULL;
#ifndef _WIN32
    sigset_t saved_mask; struct sigaction saved_signals[_NSIG]; unsigned char saved_valid[_NSIG]={0};
    if (sigprocmask(SIG_SETMASK,NULL,&saved_mask)) return 1;
    for (int sig=1; sig<_NSIG; ++sig) saved_valid[sig]=sig!=SIGKILL && sig!=SIGSTOP && !sigaction(sig,NULL,&saved_signals[sig]);
    struct sigaction ignored_pipe = {0};
    ignored_pipe.sa_handler=SIG_IGN; sigemptyset(&ignored_pipe.sa_mask);
    if (!saved_valid[SIGPIPE] || sigaction(SIGPIPE,&ignored_pipe,NULL)) return 1;
#endif
    search = beam_host_environment("PATH");
    if (!beam_host_realpath(descriptor,path) || !cli_current_directory(cwd,sizeof(cwd)) || !discover_root(root,&error) ||
        snprintf(executable,sizeof(executable),"%s/bin/" CLI_EXECUTABLE,root) >= sizeof(executable) ||
        !application_file(path,2*1024*1024,&bytes)) goto invalid;
    strcpy(base,path); char *slash=strrchr(base,'/'); if (!slash) goto invalid; *slash=0;
    etf_cursor c={bytes.data,bytes.data+bytes.size};
    if (!etf_tag(&c,ETF_VERSION) || !application_parse(&c,base,0,&app) || c.p!=c.end) goto invalid;
    free((void *)bytes.data); bytes=(beam_bytes){0};
    byte_buffer next_args={0};
    if (!application_argv(&next_args,argc,argv)) { free(next_args.data); goto invalid; }
    for (unsigned iteration=0; iteration<64; ++iteration) {
        beam_cli_recipe_return_options options={.struct_size=sizeof(options)};
        beam_return_boot *boot=NULL; beam_outcome outcome={.struct_size=sizeof(outcome)};
        byte_buffer args={0}, code={0}; cli_input_state input={0}; beam_status status=BEAM_INVALID_ARGUMENT;
        int command=!strcmp(app.contract,"command"), shell=!strcmp(app.contract,"shell");
        if (shell) {
            unsigned columns=80,rows=24;
            (void)cli_window_size(&columns, &rows);
            if (!input_initialize(&input,1) || !encode_arguments(&args,root,0,NULL,"shell",NULL,columns,rows,input.tty,NULL)) goto failed;
            options.profile=BEAM_CLI_INTERACTIVE; options.entry_module="beam_interactive"; options.entry_function="main";
        } else {
            if (!buffer_u8(&args,ETF_VERSION) || !buffer_u8(&args,ETF_LIST) || !buffer_u32(&args,command ? 2 : 1)) goto failed;
            if (command) {
                if (!buffer_u8(&args,ETF_SMALL_TUPLE) || !buffer_u8(&args,11) || !buffer_atom(&args,"beam_context") ||
                    !buffer_u8(&args,97) || !buffer_u8(&args,1) || !buffer_binary(&args,executable) ||
                    !buffer_binary(&args,root) || !buffer_binary(&args,beam_version()) || !buffer_binary(&args,beam_build_id()) ||
                    !buffer_binary(&args,app.root) || !buffer_binary(&args,cwd) || !buffer_binary(&args,search ? search : "") ||
                    !buffer_binary(&args,CLI_HOST_PLATFORM) || !buffer_u8(&args,ETF_BINARY) || !buffer_u32(&args,(unsigned)app.descriptor.size) ||
                    !buffer_bytes(&args,app.descriptor.data,app.descriptor.size)) goto failed;
                options.profile=BEAM_CLI_NONE; options.entry_module=app.module; options.entry_function=app.function;
            } else {
                if (!buffer_u8(&args,ETF_SMALL_TUPLE) || !buffer_u8(&args,3) || !buffer_binary(&args,app.module) || !buffer_binary(&args,app.function)) goto failed;
                options.profile=BEAM_CLI_APPLICATION; options.entry_module="beam_application_run"; options.entry_function="main";
            }
            if (!buffer_bytes(&args,next_args.data,next_args.size) || !buffer_u8(&args,ETF_NIL)) goto failed;
        }
        options.services=app.services; options.modules=app.modules; options.module_count=app.count;
        options.nifs=app.nifs; options.nif_count=app.nif_count;
        options.required_modules=(const char *const *)app.required; options.required_count=app.required_count;
        options.arguments=(beam_bytes){args.data,args.size}; options.output=cli_output;
        options.input=shell ? cli_input : cli_run_input; options.input_context=&input;
        options.code=cli_code; options.code_context=&code;
        status=beam_cli_recipe_return_prepare(&options,&boot,&error);
        if(status==BEAM_OK && !system) status=beam_init(&system,&error);
        if(status==BEAM_OK) status=beam_return_run(system,&boot,&outcome,&error);
failed:
        free(args.data);
        free(code.data);
        if(boot && beam_return_discard(&boot)!=BEAM_OK) status=BEAM_SYSTEM_ERROR;
        if(!input_destroy(&input) || input.error || outcome.has_cleanup_error) status=BEAM_SYSTEM_ERROR;
        if(status!=BEAM_OK) {
            fprintf(stderr,"beam: application failed: %s\n",error.message[0] ? error.message : beam_status_message(status));
            beam_outcome_release(&outcome); break;
        }
        if(!command || outcome.kind!=BEAM_OUTCOME_RETURN) {
            if(outcome.kind!=BEAM_OUTCOME_RETURN && outcome.kind!=BEAM_OUTCOME_HALT && outcome.kind!=BEAM_OUTCOME_FINISH)
                fprintf(stderr,"beam: application execution failed (outcome %d, service %s)\n",
                        (int)outcome.kind,outcome.origin[0] ? outcome.origin : "none");
            result=outcome_status(&outcome); beam_outcome_release(&outcome); break;
        }
        etf_cursor action={0};
        char tag[16]; size_t version,arity;
        if(!outcome.term.data || outcome.term.size>APP_LIMIT+2*1024*1024) goto bad_action;
        action=(etf_cursor){outcome.term.data,outcome.term.data+outcome.term.size};
        if(!etf_tag(&action,ETF_VERSION) ||
           !etf_tuple(&action,3) || !etf_named(&action,"beam_command") || !etf_number(&action,&version) || version!=1 ||
           !etf_tag(&action,ETF_SMALL_TUPLE) || action.p==action.end) goto bad_action;
        arity=*action.p++;
        if(!etf_atom(&action,tag,sizeof(tag))) goto bad_action;
        if(arity==2 && !strcmp(tag,"exit")) {
            size_t code; if(!etf_number(&action,&code) || code>255 || action.p!=action.end) goto bad_action;
            result=(int)code; beam_outcome_release(&outcome); break;
        }
        if(arity==3 && !strcmp(tag,"run")) {
            application next={0}; beam_bytes arguments;
            if(!application_parse(&action,app.root,1,&next) || !etf_binary(&action,&arguments) ||
                !application_arguments(arguments) || action.p!=action.end) {
                application_release(&next); goto bad_action;
            }
            free(next_args.data); next_args=(byte_buffer){0};
            if(!buffer_bytes(&next_args,arguments.data+1,arguments.size-1)) { application_release(&next); goto bad_action; }
            application_release(&app); app=next; beam_outcome_release(&outcome); continue;
        }
        if(arity==3 && !strcmp(tag,"exec")) {
            char *directory=etf_string(&action); char **command_args=NULL; size_t count=0,filled=0;
            int valid=directory && etf_list(&action,&count) && count>0 && count<=1024 && outcome.term.size<=1100000;
            if(valid) command_args=calloc(count+1,sizeof(char *));
            if(!command_args) valid=0;
            while(valid && filled<count) {
                beam_bytes arg;
                valid=etf_binary(&action,&arg) && arg.size<=16384 && (filled || arg.size) && !memchr(arg.data,0,arg.size);
                if(!valid) break;
                command_args[filled]=malloc(arg.size+1); if(!command_args[filled]) { valid=0; break; }
                memcpy(command_args[filled],arg.data,arg.size); command_args[filled++][arg.size]=0;
            }
            valid=valid && etf_tag(&action,ETF_NIL) && action.p==action.end;
            if(system && beam_terminate(&system)!=BEAM_OK) valid=0;
            beam_outcome_release(&outcome);
            if(fflush(stdout)) valid=0;
            if(fflush(stderr)) valid=0;
            if(valid) {
#ifndef _WIN32
                for(int sig=1;sig<_NSIG;++sig) if(saved_valid[sig] && sigaction(sig,&saved_signals[sig],NULL)) valid=0;
                if(sigprocmask(SIG_SETMASK,&saved_mask,NULL)) valid=0;
#endif
                if(valid && !beam_host_chdir(directory)) {
                    if (!application_exec(command_args,&result))
                        fprintf(stderr,"beam: cannot execute application command: %s\n",strerror(errno));
                } else fprintf(stderr,"beam: cannot change command directory: %s\n",strerror(errno));
            } else fputs("beam: invalid command action or cleanup failure\n",stderr);
            for(size_t i=0;i<filled;++i) free(command_args[i]); free(command_args); free(directory); break;
        }
bad_action:
        fputs("beam: invalid or unsupported command action\n",stderr); beam_outcome_release(&outcome); break;
    }
    free(next_args.data); goto done;
invalid:
    fputs("beam: invalid application descriptor, incompatible runtime, or missing/corrupt application inputs\n",stderr);
done:
    if(system && beam_terminate(&system)!=BEAM_OK) result=1;
    if(fflush(stdout)) result=1;
    if(fflush(stderr)) result=1;
#ifndef _WIN32
    if(sigaction(SIGPIPE,&saved_signals[SIGPIPE],NULL)) result=1;
#endif
    application_release(&app); free((void *)bytes.data); free(search); return result;
}

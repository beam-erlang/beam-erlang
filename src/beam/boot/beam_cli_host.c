/* SPDX-License-Identifier: Apache-2.0 */
/* Host-owned source execution and terminal I/O for the BEAM boot. */
#define _XOPEN_SOURCE 700
#include "beam_cli_recipe.h"
#include "../compiler/beam_compiler_host.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#define ETF_VERSION 131
#define ETF_SMALL_TUPLE 104
#define ETF_INTEGER 98
#define ETF_NIL 106
#define ETF_STRING 107
#define ETF_LIST 108
#define ETF_BINARY 109
#define ETF_ATOM_UTF8 118
#define ETF_SMALL_ATOM_UTF8 119

typedef struct byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

typedef struct cli_input_state {
    byte_buffer line;
    size_t cursor;
    int delivered;
    int eof;
    int error;
    int tty;
    int saved_terminal;
    int skip_lf;
    struct termios terminal;
    char escape[16];
    size_t escape_size;
    char *history[100];
    size_t history_count;
    size_t history_position;
    char *draft;
    locale_t locale;
} cli_input_state;

static int buffer_reserve(byte_buffer *buffer, size_t extra)
{
    unsigned char *grown;
    size_t needed = buffer->size + extra;
    size_t capacity = buffer->capacity;
    if (extra > (size_t)-1 - buffer->size)
        return 0;
    if (needed <= buffer->capacity)
        return 1;
    if (!capacity)
        capacity = 64;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2)
            return 0;
        capacity *= 2;
    }
    grown = realloc(buffer->data, capacity);
    if (!grown)
        return 0;
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_bytes(byte_buffer *buffer, const void *bytes, size_t size)
{
    if (!buffer_reserve(buffer, size))
        return 0;
    if (size)
        memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
    return 1;
}

static int buffer_u8(byte_buffer *buffer, unsigned value)
{
    unsigned char byte = (unsigned char)value;
    return buffer_bytes(buffer, &byte, 1);
}

static int buffer_u16(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[2] = {(unsigned char)(value >> 8), (unsigned char)value};
    return buffer_bytes(buffer, bytes, 2);
}

static int buffer_u32(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value
    };
    return buffer_bytes(buffer, bytes, 4);
}

static int buffer_atom(byte_buffer *buffer, const char *name)
{
    size_t length = strlen(name);
    if (length < 256)
        return buffer_u8(buffer, ETF_SMALL_ATOM_UTF8) &&
               buffer_u8(buffer, (unsigned)length) && buffer_bytes(buffer, name, length);
    return length <= 1020 && buffer_u8(buffer, ETF_ATOM_UTF8) &&
           buffer_u16(buffer, (unsigned)length) && buffer_bytes(buffer, name, length);
}

static int buffer_binary(byte_buffer *buffer, const char *text)
{
    size_t length = strlen(text);
    return buffer_u8(buffer, ETF_BINARY) && buffer_u32(buffer, (unsigned)length) &&
           buffer_bytes(buffer, text, length);
}

static int buffer_charlist(byte_buffer *buffer, const char *text)
{
    size_t length = strlen(text);
    if (length > 65535)
        return 0;
    return buffer_u8(buffer, ETF_STRING) && buffer_u16(buffer, (unsigned)length) &&
           buffer_bytes(buffer, text, length);
}

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
    if (bytes.size && (!bytes.data || fwrite(bytes.data, 1, bytes.size, out) != bytes.size))
        return BEAM_OUTPUT_FAILED;
    if (fflush(out) != 0)
        return BEAM_OUTPUT_FAILED;
    return BEAM_OUTPUT_OK;
}

/* The source runner accepts only explicit UTF-8 file identities. This is a
 * host read, never a code-path search or a call back into the running VM. */
static int utf8_path(beam_bytes source)
{
    size_t i = 0;
    if (!source.data || source.size < 6 || source.size >= PATH_MAX ||
        source.data[0] != '/' ||
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
    struct stat st;
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
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return errno == ENOENT || errno == ENOTDIR ? BEAM_CODE_NOT_FOUND : BEAM_CODE_UNAVAILABLE;
    if (fstat(fd, &st) != 0)
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
        got = read(fd, buffer->data + buffer->size, remaining);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            goto done;
        buffer->size += (size_t)got;
    }
    do {
        got = read(fd, &trailing, 1);
    } while (got < 0 && errno == EINTR);
    if (got != 0)
        goto done;
    status = BEAM_CODE_OK;
done:
    if (close(fd) != 0)
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
        ssize_t written = write(STDOUT_FILENO, bytes, size);
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
        struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
        unsigned char character;
        ssize_t count;
        int ready = poll(&descriptor, 1, processed ? 0 : 10);
        if (ready < 0 && errno == EINTR)
            return BEAM_INPUT_PENDING;
        if (ready < 0 || (descriptor.revents & (POLLERR | POLLNVAL))) {
            input->error = 1;
            return BEAM_INPUT_EOF;
        }
        if (!ready)
            return BEAM_INPUT_PENDING;
        count = read(STDIN_FILENO, &character, 1);
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
        struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
        unsigned char character;
        ssize_t count;
        int ready = poll(&descriptor, 1, processed ? 0 : 10);
        if (ready < 0 && errno == EINTR)
            break;
        if (ready < 0 || (descriptor.revents & (POLLERR | POLLNVAL))) {
            input->error = 1;
            break;
        }
        if (!ready)
            break;
        count = read(STDIN_FILENO, &character, 1);
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
    return 1;
}

static int input_destroy(cli_input_state *input)
{
    size_t index;
    int result = 1;
    if (input->saved_terminal) {
        int restored;
        do {
            restored = tcsetattr(STDIN_FILENO, TCSANOW, &input->terminal);
        } while (restored != 0 && errno == EINTR);
        result = restored == 0;
    }
    for (index = 0; index < input->history_count; ++index)
        free(input->history[index]);
    free(input->draft);
    free(input->line.data);
    if (input->locale)
        freelocale(input->locale);
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
    struct winsize geometry;
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
        if (value[0] == '/') {
            source_origin = strdup(value);
        } else {
            char *cwd = getcwd(NULL, 0);
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &geometry) == 0) {
        if (geometry.ws_col)
            columns = geometry.ws_col;
        if (geometry.ws_row)
            rows = geometry.ws_row;
    }
    if (!encode_arguments(&arguments, root, plain_count, plain, command,
                          source_origin ? source_origin : value,
                          columns, rows, strcmp(command, "shell") == 0 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO),
                          compiled.module ? &compiled : NULL)) {
        fprintf(stderr, "beam: out of memory\n");
        goto cleanup;
    }
    outcome.struct_size = sizeof(outcome);
    options.struct_size = sizeof(options);
    options.services = BEAM_SERVICE_OUTPUT | BEAM_SERVICE_CODE | BEAM_SERVICE_FILE |
                       BEAM_SERVICE_PROGRAM | BEAM_SERVICE_SOCKET | BEAM_SERVICE_ZLIB;
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
            saved_erl_libs = strdup(current);
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

/* Package policy stays in Erlang; build selects explicit host capabilities. */
static int package_main(int argc, char **argv, const char *entry)
{
    byte_buffer arguments = {0};
    beam_system *system = NULL;
    beam_return_boot *boot = NULL;
    beam_cli_recipe_return_options options = {0};
    beam_outcome outcome = {.struct_size = sizeof(outcome)};
    beam_error error = {0};
    beam_status status;
    int index, result = 1;

    char root[PATH_MAX], cwd[PATH_MAX];
    const char *search_path = getenv("PATH");
    if (!getcwd(cwd, sizeof(cwd)) || !discover_root(root, &error)) {
        fputs("beam-pkg: cannot discover working directory or installed prefix\n", stderr);
        goto cleanup;
    }
    /* Private context and literal arguments; paths never select package policy. */
    if (!buffer_u8(&arguments, ETF_VERSION) || !buffer_u8(&arguments, ETF_LIST) ||
        !buffer_u32(&arguments, 1) || !buffer_u8(&arguments, ETF_SMALL_TUPLE) ||
        !buffer_u8(&arguments, 2) ||
        !buffer_u8(&arguments, ETF_SMALL_TUPLE) || !buffer_u8(&arguments, 4) ||
        !buffer_binary(&arguments, beam_version()) || !buffer_binary(&arguments, root) ||
        !buffer_binary(&arguments, cwd) || !buffer_binary(&arguments, search_path ? search_path : "") ||
        !buffer_u8(&arguments, ETF_LIST) || !buffer_u32(&arguments, (unsigned)argc))
        goto memory_error;
    for (index = 0; index < argc; ++index)
        if (!buffer_binary(&arguments, argv[index]))
            goto memory_error;
    if (!buffer_u8(&arguments, ETF_NIL) || !buffer_u8(&arguments, ETF_NIL))
        goto memory_error;
    options.struct_size = sizeof(options);
    options.services = BEAM_SERVICE_OUTPUT;
    if (argc > 0 && (!strcmp(argv[0], "build") || !strcmp(argv[0], "install")))
        options.services |= BEAM_SERVICE_FILE | BEAM_SERVICE_PROGRAM | BEAM_SERVICE_ZLIB;
    options.profile = BEAM_CLI_PKG;
    options.entry_module = "beam_pkg";
    options.entry_function = entry;
    options.arguments = (beam_bytes){arguments.data, arguments.size};
    options.output = cli_output;
    status = beam_cli_recipe_return_prepare(&options, &boot, &error);
    if (status == BEAM_OK)
        status = beam_init(&system, &error);
    if (status == BEAM_OK)
        status = beam_return_run(system, &boot, &outcome, &error);
    if (status != BEAM_OK) {
        fprintf(stderr, "beam-pkg: %s\n", error.message[0] ? error.message : beam_status_message(status));
    } else if (outcome.kind == BEAM_OUTCOME_FINISH && !outcome.has_cleanup_error) {
        result = outcome_status(&outcome);
    } else {
        fputs("beam-pkg: package tool failed\n", stderr);
    }
    goto cleanup;
memory_error:
    fputs("beam-pkg: out of memory\n", stderr);
cleanup:
    beam_outcome_release(&outcome);
    if (boot)
        (void)beam_return_discard(&boot);
    if (system)
        (void)beam_terminate(&system);
    free(arguments.data);
    return result;
}

/* A bounded private result protocol, not a public package/plugin API. */
typedef struct project_cursor { const unsigned char *p, *end; } project_cursor;
static int project_tag(project_cursor *c, unsigned tag)
{ return c->p < c->end && *c->p++ == tag; }
static int project_u32(project_cursor *c, size_t *n)
{
    if (c->end - c->p < 4) return 0;
    *n = ((size_t)c->p[0] << 24) | ((size_t)c->p[1] << 16) | ((size_t)c->p[2] << 8) | c->p[3];
    c->p += 4; return 1;
}
static int project_tuple(project_cursor *c, unsigned n)
{ return project_tag(c, ETF_SMALL_TUPLE) && project_tag(c, n); }
static int project_list(project_cursor *c, size_t *n)
{
    if (c->p < c->end && *c->p == ETF_NIL) { ++c->p; *n = 0; return 1; }
    return project_tag(c, ETF_LIST) && project_u32(c, n);
}
static int project_binary(project_cursor *c, beam_bytes *b)
{
    if (!project_tag(c, ETF_BINARY) || !project_u32(c, &b->size) || b->size > (size_t)(c->end-c->p)) return 0;
    b->data = c->p; c->p += b->size; return 1;
}
static char *project_string(project_cursor *c)
{
    beam_bytes b; char *s;
    if (!project_binary(c, &b) || !b.size || b.size > 16384 || memchr(b.data, 0, b.size)) return NULL;
    s = malloc(b.size + 1);
    if (s) { memcpy(s, b.data, b.size); s[b.size] = 0; }
    return s;
}
static int project_atom(project_cursor *c, char *s, size_t capacity)
{
    size_t n;
    if (c->p == c->end) return 0;
    if (*c->p++ == ETF_SMALL_ATOM_UTF8) {
        if (c->p == c->end) return 0;
        n = *c->p++;
    } else {
        --c->p;
        if (!project_tag(c, ETF_ATOM_UTF8) || c->end-c->p < 2) return 0;
        n = ((size_t)c->p[0] << 8) | c->p[1]; c->p += 2;
    }
    if (n >= capacity || n > (size_t)(c->end-c->p)) return 0;
    memcpy(s, c->p, n); s[n] = 0; c->p += n; return 1;
}
static beam_status project_io(void *context, beam_bytes argument, beam_host_reply *reply)
{
    project_cursor c = {argument.data, argument.data + argument.size};
    byte_buffer out = {0}; char operation[32], destination[32];
    beam_bytes bytes; beam_status status = BEAM_INVALID_TERM;
    if (!project_tag(&c, ETF_VERSION) || !buffer_u8(&out, ETF_VERSION)) goto done;
    if (c.p < c.end && *c.p == ETF_SMALL_TUPLE) {
        if (!project_tuple(&c, 3) || !project_atom(&c, operation, sizeof(operation)) ||
            strcmp(operation, "output") || !project_atom(&c, destination, sizeof(destination)) ||
            !project_binary(&c, &bytes) || c.p != c.end) goto done;
        if (cli_output(NULL, (beam_bytes){(const unsigned char *)destination, strlen(destination)}, bytes) == BEAM_OUTPUT_OK) {
            if (!buffer_atom(&out, "ok")) goto done;
        } else {
            ((cli_input_state *)context)->error = 1;
            if (!buffer_u8(&out, ETF_SMALL_TUPLE) || !buffer_u8(&out, 2) ||
                !buffer_atom(&out, "error") || !buffer_atom(&out, "io")) goto done;
        }
    } else {
        const unsigned char *line = NULL; size_t size = 0;
        if (!project_atom(&c, operation, sizeof(operation)) || strcmp(operation, "read_line") || c.p != c.end) goto done;
        beam_input_status input = ((cli_input_state *)context)->tty
            ? cli_input(context, &line, &size) : cli_run_input(context, &line, &size);
        if (input == BEAM_INPUT_LINE) {
            if (!buffer_u8(&out, ETF_SMALL_TUPLE) || !buffer_u8(&out, 2) || !buffer_atom(&out, "ok") ||
                !buffer_u8(&out, ETF_BINARY) || !buffer_u32(&out, (unsigned)size) || !buffer_bytes(&out, line, size)) goto done;
        } else if (!buffer_atom(&out, input == BEAM_INPUT_PENDING ? "pending" : "eof")) goto done;
    }
    status = beam_host_reply_copy(reply, (beam_bytes){out.data, out.size});
done:
    free(out.data); return status;
}

int beam_project_main(const char *directory, int argc, char **argv, int shell)
{
    char root[PATH_MAX], cwd[PATH_MAX], tag[32];
    struct sigaction saved_pipe, ignored_pipe = {0};
    ignored_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignored_pipe.sa_mask);
    if (sigaction(SIGPIPE, &ignored_pipe, &saved_pipe) != 0) {
        fputs("beam: cannot configure project output handling\n", stderr); return 1;
    }
    const char *search = getenv("PATH");
    size_t reserved_count, host_count = 0, module_count = 0, nif_count = 0;
    const char *const *reserved = beam_cli_project_reserved(&reserved_count);
    beam_return_module *host = NULL;
    beam_module_spec *modules = NULL;
    beam_nif_spec *nifs = NULL;
    beam_system *system = NULL; beam_vm *vm = NULL;
    beam_composition *composition = NULL;
    beam_return_boot *tool = NULL;
    beam_cli_recipe_return_options preparation = {0};
    beam_outcome selection = {.struct_size = sizeof(selection)};
    beam_result outcome = {0}; beam_error error = {0};
    beam_status status = BEAM_INVALID_TERM;
    byte_buffer arguments = {0}, call = {0}; cli_input_state input = {0};
    char *entry = NULL; int result = 1;
    if (!getcwd(cwd, sizeof(cwd)) || !discover_root(root, &error)) goto done;
    status = beam_cli_project_acquire(root, shell, &host, &host_count, &error);
    if (status != BEAM_OK) goto done;
    if (!buffer_u8(&arguments, ETF_VERSION) || !buffer_u8(&arguments, ETF_LIST) || !buffer_u32(&arguments, 1) ||
        !buffer_u8(&arguments, ETF_SMALL_TUPLE) || !buffer_u8(&arguments, 4) ||
        !buffer_u8(&arguments, ETF_SMALL_TUPLE) || !buffer_u8(&arguments, 4) ||
        !buffer_binary(&arguments, beam_version()) || !buffer_binary(&arguments, root) ||
        !buffer_binary(&arguments, cwd) || !buffer_binary(&arguments, search ? search : "") ||
        !buffer_binary(&arguments, directory) || !buffer_u8(&arguments, ETF_LIST) ||
        !buffer_u32(&arguments, (unsigned)(reserved_count + host_count))) goto malformed;
    for (size_t i = 0; i < reserved_count; ++i)
        if (!buffer_binary(&arguments, reserved[i])) goto malformed;
    for (size_t i = 0; i < host_count; ++i)
        if (!buffer_binary(&arguments, host[i].name)) goto malformed;
    if (!buffer_u8(&arguments, ETF_NIL) || !buffer_atom(&arguments, shell ? "shell" : "run") ||
        !buffer_u8(&arguments, ETF_NIL)) goto malformed;
    preparation.struct_size = sizeof(preparation);
    preparation.profile = BEAM_CLI_PKG;
    preparation.services = BEAM_SERVICE_OUTPUT | BEAM_SERVICE_FILE;
    preparation.entry_module = "beam_pkg_environment"; preparation.entry_function = "prepare";
    preparation.arguments = (beam_bytes){arguments.data, arguments.size}; preparation.output = cli_output;
    status = beam_cli_recipe_return_prepare(&preparation, &tool, &error);
    if (status == BEAM_OK) status = beam_init(&system, &error);
    if (status == BEAM_OK) status = beam_return_run(system, &tool, &selection, &error);
    if (status != BEAM_OK) goto done;
    if (selection.kind != BEAM_OUTCOME_RETURN || selection.has_cleanup_error ||
        !selection.term.data || selection.term.size > 270000000) goto malformed;
    project_cursor c = {selection.term.data, selection.term.data + selection.term.size};
    if (!project_tag(&c, ETF_VERSION) || !project_tuple(&c, 2) || !project_atom(&c, tag, sizeof(tag))) goto malformed;
    if (!strcmp(tag, "error")) {
        beam_bytes message;
        if (!project_binary(&c, &message) || c.p != c.end) goto malformed;
        fputs("beam: ", stderr); fwrite(message.data, 1, message.size, stderr); fputc('\n', stderr);
        goto done;
    }
    if (strcmp(tag, "ok") || !project_tuple(&c, 3) || !(entry = project_string(&c)) ||
        !project_list(&c, &module_count) || module_count > 4096) goto malformed;
    modules = calloc(host_count + module_count, sizeof(*modules));
    if (!modules) goto malformed;
    for (size_t i = 0; i < host_count; ++i)
        modules[i] = (beam_module_spec){host[i].name, host[i].bytes, host[i].origin};
    for (size_t i = 0; i < module_count; ++i) {
        beam_module_spec *m = &modules[host_count+i];
        if (!project_tuple(&c, 3) || !(m->name = project_string(&c)) ||
            !project_binary(&c, &m->bytes) || !(m->origin = project_string(&c))) goto malformed;
    }
    if (module_count && !project_tag(&c, ETF_NIL)) goto malformed;
    if (!project_list(&c, &nif_count) || nif_count > 1024) goto malformed;
    nifs = calloc(nif_count ? nif_count : 1, sizeof(*nifs));
    if (!nifs) goto malformed;
    for (size_t i = 0; i < nif_count; ++i) {
        nifs[i].struct_size = sizeof(*nifs);
        if (!project_tuple(&c, 2) || !(nifs[i].module = project_string(&c)) || !(nifs[i].path = project_string(&c))) goto malformed;
    }
    if ((nif_count && !project_tag(&c, ETF_NIL)) || c.p != c.end) goto malformed;
    beam_composition_options options = {.struct_size = sizeof(options),
        .modules = modules, .module_count = host_count + module_count, .nifs = nifs, .nif_count = nif_count};
    status = beam_composition_prepare(&options, &composition, &error);
    if (status != BEAM_OK) goto done;
    beam_vm_options vm_options; beam_vm_options_init(&vm_options);
    vm_options.composition = composition;
    status = beam_vm_init(system, &vm_options, &vm, &error);
    if (status == BEAM_OK) status = beam_vm_set_host_handler(vm, project_io, &input);
    if (status == BEAM_OK) status = beam_vm_start(vm, &error);
    if (status == BEAM_OK) status = beam_vm_activate(vm, &error);
    if (status != BEAM_OK) goto done;
    if (shell) {
        unsigned columns = 80, rows = 24;
        struct winsize geometry;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &geometry) == 0) {
            if (geometry.ws_col) columns = geometry.ws_col;
            if (geometry.ws_row) rows = geometry.ws_row;
        }
        if (!input_initialize(&input, 1)) { status = BEAM_INVALID_ARGUMENT; goto done; }
        if (!encode_arguments(&call, root, 0, NULL, "shell", NULL, columns, rows, input.tty, NULL)) goto malformed;
    } else {
        if (!buffer_u8(&call, ETF_VERSION) || !buffer_u8(&call, ETF_LIST) || !buffer_u32(&call, 1) ||
            !buffer_u8(&call, ETF_SMALL_TUPLE) || !buffer_u8(&call, 2) || !buffer_binary(&call, entry) ||
            !buffer_u8(&call, ETF_LIST) || !buffer_u32(&call, (unsigned)argc)) goto malformed;
        for (int i = 0; i < argc; ++i) if (!buffer_binary(&call, argv[i])) goto malformed;
        if (!buffer_u8(&call, ETF_NIL) || !buffer_u8(&call, ETF_NIL)) goto malformed;
    }
    status = beam_vm_call(vm, shell ? "beam_interactive" : "beam_project_run", shell ? "project" : "main", (beam_bytes){call.data, call.size}, &outcome);
    if (status == BEAM_OK) {
        if (outcome.kind == BEAM_RETURN) result = 0;
        else if (outcome.kind == BEAM_HALT) result = outcome.halt_status;
        else fputs("beam: project entry failed\n", stderr);
    }
    goto done;
malformed:
    status = BEAM_INVALID_TERM;
    snprintf(error.message, sizeof(error.message), "invalid or oversized project preparation result");
done:
    if (status != BEAM_OK) fprintf(stderr, "beam: %s\n", error.message[0] ? error.message : beam_status_message(status));
    beam_result_release(&outcome);
    if (vm && beam_vm_terminate(&vm, NULL) != BEAM_OK) result = 1;
    beam_composition_release(&composition);
    if (tool) beam_return_discard(&tool);
    if (system) beam_terminate(&system);
    if (modules) for (size_t i = 0; i < module_count; ++i) {
        free((void *)modules[host_count+i].name); free((void *)modules[host_count+i].origin);
    }
    if (nifs) for (size_t i = 0; i < nif_count; ++i) { free((void *)nifs[i].module); free((void *)nifs[i].path); }
    free(modules); free(nifs); free(entry);
    beam_cli_project_release(host, host_count);
    beam_outcome_release(&selection);
    free(arguments.data); free(call.data);
    if (!input_destroy(&input)) { fputs("beam: cannot restore terminal\n", stderr); result = 1; }
    if (input.error) result = 1;
    /* Complete buffered writes before restoring the caller's disposition. */
    if (fflush(stdout) != 0) result = 1;
    if (fflush(stderr) != 0) result = 1;
    if (sigaction(SIGPIPE, &saved_pipe, NULL) != 0) result = 1;
    return result;
}

int beam_pkg_main(int argc, char **argv) { return package_main(argc, argv, "main"); }
int beam_build_main(int argc, char **argv) { return package_main(argc, argv, "beam_main"); }

/* execve deliberately has no ENOEXEC shell fallback. The VM has already gone. */
extern char **environ;
static void task_exec(const char *root, char **argv)
{
    char candidate[PATH_MAX];
    const char *name = argv[0];
    if (!strcmp(name, "beam") || !strcmp(name, "beam-pkg")) {
        if (snprintf(candidate, sizeof(candidate), "%s/bin/%s", root, name) >= sizeof(candidate)) {
            errno = ENAMETOOLONG; return;
        }
        execve(candidate, argv, environ); return;
    }
    if (strchr(name, '/')) { execve(name, argv, environ); return; }
    const char *path = getenv("PATH");
    if (!path) path = "/bin:/usr/bin";
    int denied = 0;
    for (;;) {
        const char *end = strchr(path, ':');
        size_t size = end ? (size_t)(end - path) : strlen(path);
        if (size + strlen(name) + 2 < sizeof(candidate)) {
            if (size) snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)size, path, name);
            else snprintf(candidate, sizeof(candidate), "%s", name);
            execve(candidate, argv, environ);
            if (errno == EACCES) denied = 1;
            else if (errno != ENOENT && errno != ENOTDIR) return;
        }
        if (!end) break;
        path = end + 1;
    }
    errno = denied ? EACCES : ENOENT;
}

int beam_task_main(int argc, char **argv)
{
    char root[PATH_MAX], cwd[PATH_MAX], tag[32];
    sigset_t saved_mask;
    struct sigaction saved_signals[_NSIG];
    unsigned char saved_valid[_NSIG] = {0};
    if (sigprocmask(SIG_SETMASK, NULL, &saved_mask) != 0) return 1;
    for (int sig = 1; sig < _NSIG; ++sig)
        saved_valid[sig] = sig != SIGKILL && sig != SIGSTOP && sigaction(sig, NULL, &saved_signals[sig]) == 0;
    byte_buffer arguments = {0};
    beam_system *system = NULL;
    beam_return_boot *tool = NULL;
    beam_outcome selection = {.struct_size = sizeof(selection)};
    beam_cli_recipe_return_options options = {0};
    beam_error error = {0}; beam_status status = BEAM_INVALID_ARGUMENT;
    char *directory = NULL, **command = NULL;
    size_t count = 0; int result = 1, execute = 0, cleanup_failed;
    if (!getcwd(cwd, sizeof(cwd)) || !discover_root(root, &error)) goto done;
    if (!buffer_u8(&arguments, ETF_VERSION) || !buffer_u8(&arguments, ETF_LIST) ||
        !buffer_u32(&arguments, 1) || !buffer_u8(&arguments, ETF_SMALL_TUPLE) ||
        !buffer_u8(&arguments, 2) || !buffer_u8(&arguments, ETF_SMALL_TUPLE) || !buffer_u8(&arguments, 4) ||
        !buffer_binary(&arguments, beam_version()) || !buffer_binary(&arguments, root) ||
        !buffer_binary(&arguments, cwd) || !buffer_binary(&arguments, getenv("PATH") ? getenv("PATH") : "") ||
        !buffer_u8(&arguments, ETF_LIST) || !buffer_u32(&arguments, argc)) goto malformed;
    for (int i = 0; i < argc; ++i) if (!buffer_binary(&arguments, argv[i])) goto malformed;
    if (!buffer_u8(&arguments, ETF_NIL) || !buffer_u8(&arguments, ETF_NIL)) goto malformed;
    options.struct_size = sizeof(options); options.profile = BEAM_CLI_PKG;
    options.services = BEAM_SERVICE_OUTPUT | (argc == 1 && !strcmp(argv[0], "--help") ? 0 : BEAM_SERVICE_FILE);
    options.entry_module = "beam_pkg_task"; options.entry_function = "prepare";
    options.arguments = (beam_bytes){arguments.data, arguments.size}; options.output = cli_output;
    status = beam_cli_recipe_return_prepare(&options, &tool, &error);
    if (status == BEAM_OK) status = beam_init(&system, &error);
    if (status == BEAM_OK) status = beam_return_run(system, &tool, &selection, &error);
    if (status != BEAM_OK) goto done;
    if (selection.kind != BEAM_OUTCOME_RETURN || selection.has_cleanup_error ||
        !selection.term.data || selection.term.size > 1100000) goto malformed;
    project_cursor c = {selection.term.data, selection.term.data + selection.term.size};
    if (!project_tag(&c, ETF_VERSION) || !project_tuple(&c, 2) || !project_atom(&c, tag, sizeof(tag))) goto malformed;
    if (!strcmp(tag, "list") || !strcmp(tag, "error")) {
        beam_bytes message;
        if (!project_binary(&c, &message) || c.p != c.end) goto malformed;
        FILE *out = !strcmp(tag, "list") ? stdout : stderr;
        if (out == stderr) fputs("beam: ", out);
        if (fwrite(message.data, 1, message.size, out) != message.size) goto done;
        if (out == stderr) fputc('\n', out);
        result = out == stdout ? 0 : 1; goto done;
    }
    if (strcmp(tag, "exec") || !project_tuple(&c, 2) || !(directory = project_string(&c)) ||
        !project_list(&c, &count) || !count || count > 1024) goto malformed;
    command = calloc(count + 1, sizeof(*command));
    if (!command) goto malformed;
    for (size_t i = 0; i < count; ++i) {
        beam_bytes b;
        if (!project_binary(&c, &b) || b.size > 16384 || (!i && !b.size) || memchr(b.data, 0, b.size)) goto malformed;
        command[i] = malloc(b.size + 1);
        if (!command[i]) goto malformed;
        memcpy(command[i], b.data, b.size); command[i][b.size] = 0;
    }
    if (!project_tag(&c, ETF_NIL) || c.p != c.end) goto malformed;
    execute = 1; goto done;
malformed:
    status = BEAM_INVALID_TERM;
    snprintf(error.message, sizeof(error.message), "invalid or oversized task preparation result");
done:
    cleanup_failed = tool && beam_return_discard(&tool) != BEAM_OK;
    if (system && beam_terminate(&system) != BEAM_OK) cleanup_failed = 1;
    if (cleanup_failed) {
        fputs("beam: task preparation cleanup failed\n", stderr);
        execute = 0; result = 1;
    }
    beam_outcome_release(&selection); free(arguments.data);
    if (status != BEAM_OK) fprintf(stderr, "beam: %s\n", error.message[0] ? error.message : beam_status_message(status));
    if (execute) {
        for (int sig = 1; sig < _NSIG; ++sig)
            if (saved_valid[sig] && sigaction(sig, &saved_signals[sig], NULL) != 0) {
                fputs("beam: cannot restore task signal disposition\n", stderr); execute = 0; break;
            }
        if (sigprocmask(SIG_SETMASK, &saved_mask, NULL) != 0) {
            fputs("beam: cannot restore task signal mask\n", stderr); execute = 0;
        }
    }
    if (execute) {
        if (chdir(directory) == 0) task_exec(root, command);
        fprintf(stderr, "beam: cannot execute task '%s': %s\n", command[0], strerror(errno));
    }
    if (command) { for (size_t i = 0; i < count; ++i) free(command[i]); free(command); }
    free(directory); return result;
}

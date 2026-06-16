/* SPDX-License-Identifier: Apache-2.0 */
/* Private native console adapter; state belongs to the command invocation. */
#ifndef BEAM_CLI_WINDOWS_H
#define BEAM_CLI_WINDOWS_H
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define fileno _fileno
#define flockfile _lock_file
#define funlockfile _unlock_file
#define isatty _isatty

static int cli_window_size(unsigned *columns, unsigned *rows)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return 0;
    *columns = (unsigned)(info.srWindow.Right - info.srWindow.Left + 1);
    *rows = (unsigned)(info.srWindow.Bottom - info.srWindow.Top + 1);
    return 1;
}

static int cli_read_byte(cli_input_state *input, unsigned char *byte, DWORD timeout)
{
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode, count;
    if (input->pending_offset < input->pending_size) {
        *byte = input->pending[input->pending_offset++]; return 1;
    }
    input->pending_offset = input->pending_size = 0;
    if (GetConsoleMode(handle, &mode)) {
        DWORD waited = WaitForSingleObject(handle, timeout);
        if (waited == WAIT_TIMEOUT) return -2;
        if (waited != WAIT_OBJECT_0) { errno = EIO; return -1; }
        INPUT_RECORD record;
        if (!ReadConsoleInputW(handle, &record, 1, &count) || count != 1) { errno = EIO; return -1; }
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) return -2;
        KEY_EVENT_RECORD *key = &record.Event.KeyEvent;
        wchar_t character = key->uChar.UnicodeChar;
        if (character) {
            wchar_t pair[2]; int size = 1;
            if (character >= 0xd800 && character <= 0xdbff) {
                input->surrogate = character; return -2;
            }
            if (character >= 0xdc00 && character <= 0xdfff && input->surrogate) {
                pair[0] = input->surrogate; pair[1] = character; size = 2;
            } else pair[0] = character;
            input->surrogate = 0;
            int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pair, size,
                (char *)input->pending, sizeof(input->pending), NULL, NULL);
            if (!bytes) { errno = EILSEQ; return -1; }
            input->pending_size = (size_t)bytes;
        } else {
            const char *sequence = NULL;
            switch (key->wVirtualKeyCode) {
            case VK_UP: sequence = "\033[A"; break;
            case VK_DOWN: sequence = "\033[B"; break;
            case VK_RIGHT: sequence = "\033[C"; break;
            case VK_LEFT: sequence = "\033[D"; break;
            case VK_HOME: sequence = "\033[H"; break;
            case VK_END: sequence = "\033[F"; break;
            case VK_DELETE: sequence = "\033[3~"; break;
            default: return -2;
            }
            input->pending_size = strlen(sequence);
            memcpy(input->pending, sequence, input->pending_size);
        }
        *byte = input->pending[input->pending_offset++]; return 1;
    }
    DWORD type = GetFileType(handle);
    if (type == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &count, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return 0;
            errno = EIO; return -1;
        }
        if (!count) { if (timeout) Sleep(timeout); return -2; }
    } else if (type != FILE_TYPE_DISK && type != FILE_TYPE_CHAR) { errno = EBADF; return -1; }
    if (!ReadFile(handle, byte, 1, &count, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF) return 0;
        errno = EIO; return -1;
    }
    return count ? 1 : 0;
}

static int cli_console_initialize(cli_input_state *input, int shell)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    input->tty = shell && GetConsoleMode(in, &input->input_mode) && GetConsoleMode(out, &input->output_mode);
    if (!input->tty) return 1;
    input->input_page = GetConsoleCP(); input->output_page = GetConsoleOutputCP();
    /* Keyboard records supply Unicode directly. Disable cooked echo and signals
     * so the existing editor can handle control characters and restore state. */
    DWORD mode = input->input_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
        ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT | ENABLE_QUICK_EDIT_MODE);
    mode |= ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(in, mode)) return 0;
    input->saved_terminal = 1;
    if (!SetConsoleMode(out, input->output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ||
        !SetConsoleOutputCP(CP_UTF8)) return 0;
    return 1;
}

static int cli_console_restore(cli_input_state *input)
{
    int ok = 1;
    if (input->saved_terminal) {
        if (!SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input->input_mode)) ok = 0;
        if (!SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), input->output_mode)) ok = 0;
        if (!SetConsoleOutputCP(input->output_page)) ok = 0;
    }
    return ok;
}
#endif

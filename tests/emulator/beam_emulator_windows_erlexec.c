/* Test-only adapter: keep upstream erlexec's argument parser, but launch the
 * project's standalone executable rather than upstream's beam.dll lifecycle. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "beam_emulator_windows_process.h"

int win_erlexec(int argc, char **argv, HANDLE module);

/* Upstream's Windows parser expects its platform entry to supply this hook. */
void error(char *format, ...)
{
    va_list arguments;
    fputs("private erl: ", stderr);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    exit(1);
}

#ifndef BEAM_TEST_EXPECTED_FLAVOR
#error "Private Windows erl requires its selected emulator flavor"
#endif
#ifndef BEAM_TEST_EXPECTED_BUILD_TYPE
#error "Private Windows erl requires its selected emulator build type"
#endif

static int same_windows_path(const char *left, const char *right)
{
    for (; *left && *right; ++left, ++right) {
        unsigned char a = (unsigned char) *left, b = (unsigned char) *right;
        if (a == '/') a = '\\';
        if (b == '/') b = '\\';
        if (a >= 'A' && a <= 'Z') a = (unsigned char) (a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char) (b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return !*left && !*right;
}

static const char *parsed_bindir(char **argv)
{
    const char *bindir = NULL;
    int index, metadata = 0;
    /* erlexec inserts -root/-bindir/-progname between the first two --
     * boundaries, before user init arguments. Do not infer it from argv[0]. */
    for (index = 1; argv[index]; ++index) {
        if (!strcmp(argv[index], "--")) {
            if (metadata) break;
            metadata = 1;
        } else if (metadata && !strcmp(argv[index], "-bindir")) {
            if (bindir || !argv[index + 1]) return NULL;
            bindir = argv[++index];
        }
    }
    return bindir;
}

static int selected_emulator(const char *emulator, char **argv)
{
    const char *bindir = parsed_bindir(argv);
    const char *type = BEAM_TEST_EXPECTED_BUILD_TYPE;
    const char *flavor = BEAM_TEST_EXPECTED_FLAVOR;
    const char *suffix;
    char name[64], *expected;
    size_t capacity;
    int matches;
    if (!bindir || (strcmp(type, "opt") && strcmp(type, "debug")) ||
        (strcmp(flavor, "emu") && strcmp(flavor, "jit"))) return 0;
    suffix = !strcmp(type, "debug") ? ".debug" : "";
    capacity = strlen(bindir) + sizeof(name) + 2;
    expected = malloc(capacity);
    if (!expected) return 0;
    snprintf(name, sizeof(name), "beam%s.smp.dll", suffix);
    snprintf(expected, capacity, "%s\\%s", bindir, name);
    matches = same_windows_path(emulator, expected);
    if (!matches) {
        snprintf(name, sizeof(name), "beam%s.%s.dll", suffix, flavor);
        snprintf(expected, capacity, "%s\\%s", bindir, name);
        matches = same_windows_path(emulator, expected);
    }
    free(expected);
    return matches;
}

int start_emulator(char *emulator, char *start_program, char **argv, int detached)
{
    const char *actual = getenv("BEAM_TEST_ACTUAL_VM");
    (void) start_program;
    if (!actual || !*actual) {
        fputs("Private Windows erl requires BEAM_TEST_ACTUAL_VM\n", stderr);
        return 1;
    }
    if (!selected_emulator(emulator, argv)) {
        fprintf(stderr, "Private Windows erl cannot select %s; prepared candidate is %s/%s\n",
                emulator, BEAM_TEST_EXPECTED_FLAVOR, BEAM_TEST_EXPECTED_BUILD_TYPE);
        return 1;
    }
    return beam_test_windows_start(actual, argv, detached ? 1 : 0, NULL);
}

/* The retained parser uses UTF-8 internally. Convert the native Unicode command
 * line instead of depending on the active ANSI code page used by main(). */
int wmain(int argc, wchar_t **wide_argv)
{
    char **argv = calloc((size_t) argc + 1, sizeof(*argv));
    int i, result;
    if (!argv) return 1;
    for (i = 0; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       wide_argv[i], -1, NULL, 0, NULL, NULL);
        if (!bytes || !(argv[i] = malloc((size_t) bytes)) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                 wide_argv[i], -1, argv[i], bytes, NULL, NULL))
            return 1;
    }
    if (beam_test_windows_contain_launcher()) return 1;
    result = win_erlexec(argc, argv, GetModuleHandleW(NULL));
    /* erlexec may rearrange argv; the process is about to exit. */
    return result;
}

/* SPDX-License-Identifier: Apache-2.0 */

#include "beam_cli_recipe.h"

#include <stdio.h>
#include <string.h>

int beam_pkg_main(int argc, char **argv);
int beam_build_main(int argc, char **argv);
int beam_project_main(const char *directory, int argc, char **argv, int shell);
int beam_task_main(int argc, char **argv);

int beam_boot_main(const char *command, const char *value, int argc, char **argv);

static void
usage(FILE *out)
{
    fputs(
        "usage: beam [command] [args]\n"
        "\n"
        "Omit command to open the modern BEAM shell.\n"
        "\n"
        "commands:\n"
        "  help [command]              Show commands\n"
        "  version                     Print the runtime identity\n"
        "  task [--project dir] [name [-- args...]] Run a development task\n"
        "  shell [--project dir]       Open the interactive Erlang shell\n"
        "  eval <expression>           Evaluate one Erlang expression and halt\n"
        "  run <file.erl> [-- args...] Compile and run main/1\n"
        "  build [directory]              Build local packages\n"
        "  run --project <dir> [-- args...] Run an installed BEAM project\n"
        "  compile [options] <files...> Compile Erlang sources to bytecode\n",
        out);
}

static int
help_command(const char *command)
{
    if (command == NULL) {
        usage(stdout);
        return 0;
    }
    if (strcmp(command, "help") == 0) {
        fputs("usage: beam help [command]\n", stdout);
        return 0;
    }
    if (strcmp(command, "version") == 0) {
        fputs("usage: beam version\n", stdout);
        return 0;
    }
    if (strcmp(command, "shell") == 0) {
        fputs("usage: beam shell [--project <directory>]\n\n", stdout);
        return 0;
    }
    if (strcmp(command, "eval") == 0) {
        fputs("usage: beam eval <expression>\n", stdout);
        return 0;
    }
    if (strcmp(command, "run") == 0) {
        fputs("usage: beam run <file.erl> [-- args...]\n"
              "       beam run --project <directory> [-- args...]\n"
              "\n", stdout);
        return 0;
    }
    if (strcmp(command, "build") == 0) {
        char *help[] = {"help", "build"}; return beam_build_main(2, help);
    }
    if (strcmp(command, "task") == 0) {
        char *help[] = {"--help"}; return beam_task_main(1, help);
    }
    if (strcmp(command, "compile") == 0) {
        fputs("usage: beam compile [options] [--] file.erl...\n"
              "  -o DIR                   Output directory (default: current directory)\n"
              "  -I DIR, -IDIR            Add an include directory\n"
              "  -D NAME[=TERM], -DNAME[=TERM] Define a macro (default value: true)\n"
              "  --warnings-as-errors     Fail on compiler warnings\n"
              "  --deterministic          Enable deterministic compiler output\n"
              "  --no-transforms          Reject compiler transforms\n"
              "  --depfile FILE           Write dependencies for one source\n"
              "  --                       Treat remaining arguments as filenames\n",
              stdout);
        return 0;
    }
    fprintf(stderr, "beam: unknown command '%s'\n", command);
    usage(stderr);
    return 1;
}

static int
boot_command(const char *function, int argc, char **argv)
{
    if (strcmp(function, "run") == 0)
        return beam_boot_main(function, argv[0], argc - 1, argv + 1);
    if (strcmp(function, "compile") == 0)
        return beam_boot_main(function, NULL, argc, argv);
    return beam_boot_main(function, argc ? argv[0] : NULL, 0, NULL);
}

static int
reject_runtime_flags(int argc, char **argv)
{
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], "--project") && i + 1 < argc) { ++i; continue; }
        if (!strncmp(argv[i], "--runtime", 9)) {
            fputs("beam: runtime selection is obsolete; beam runs modern BEAM\n", stderr);
            return 1;
        }
    }
    return 0;
}

static int
cmd_run(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "beam: run requires a source file\n");
        return 1;
    }
    if (strcmp(argv[0], "--project") == 0) {
        if (argc < 2 || !argv[1][0] || argv[1][0] == '-' ||
            (argc > 2 && strcmp(argv[2], "--") != 0)) {
            fputs("usage: beam run --project <directory> [-- args...]\n", stderr);
            return 1;
        }
        return beam_project_main(argv[1], argc > 2 ? argc - 3 : 0, argc > 2 ? argv + 3 : NULL, 0);
    }
    if (strncmp(argv[0], "--runtime", 9) == 0) {
        fprintf(stderr, "beam: runtime selection is obsolete; beam runs modern BEAM\n");
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "--") != 0) {
        fprintf(stderr, "beam: arguments follow --\n");
        return 1;
    }
    /* Remove only the CLI separator; subsequent strings are application data. */
    if (argc > 1) {
        argv[1] = argv[0];
        return boot_command("run", argc - 1, &argv[1]);
    }
    return boot_command("run", 1, argv);
}

static int
cmd_compile(int argc, char **argv)
{
    int i, literal = 0, sources = 0, output = 0, depfile = 0;

    /* Validate the command shape without starting a VM. Values and filenames
     * remain data, including selector-looking strings following -- or an option. */
    for (i = 0; i < argc; ++i) {
        const char *argument = argv[i];
        if (literal) {
            ++sources;
        } else if (strcmp(argument, "--") == 0) {
            literal = 1;
        } else if (strcmp(argument, "-o") == 0 || strcmp(argument, "-I") == 0 ||
                   strcmp(argument, "-D") == 0 || strcmp(argument, "--depfile") == 0) {
            if ((strcmp(argument, "-o") == 0 && output++) ||
                (strcmp(argument, "--depfile") == 0 && depfile++)) {
                fprintf(stderr, "beam: compile option '%s' may appear only once\n", argument);
                return 1;
            }
            if (++i == argc || argv[i][0] == '\0') {
                fprintf(stderr, "beam: compile option '%s' requires a value\n", argument);
                return 1;
            }
        } else if ((strncmp(argument, "-I", 2) == 0 ||
                    strncmp(argument, "-D", 2) == 0) && argument[2]) {
            continue;
        } else if (strcmp(argument, "--warnings-as-errors") == 0 ||
                   strcmp(argument, "--deterministic") == 0 ||
                   strcmp(argument, "--no-transforms") == 0) {
            continue;
        } else if (strncmp(argument, "--runtime", 9) == 0) {
            fputs("beam: runtime selection is obsolete; beam runs modern BEAM\n", stderr);
            return 1;
        } else if (argument[0] == '-') {
            fprintf(stderr, "beam: unknown compile option '%s'\n", argument);
            return 1;
        } else {
            ++sources;
        }
    }
    if (!sources || (depfile && sources != 1)) {
        fputs(!sources ? "beam: compile requires at least one source file\n" :
                         "beam: --depfile requires exactly one source file\n", stderr);
        return 1;
    }
    return boot_command("compile", argc, argv);
}

int
main(int argc, char **argv)
{
    const char *command;
    int i;

    const char *executable = strrchr(argv[0], '/');
    executable = executable ? executable + 1 : argv[0];
    if (strcmp(executable, "beam-pkg") == 0)
        return beam_pkg_main(argc - 1, argv + 1);

    --argc;
    ++argv;
    if (!argc)
        return boot_command("shell", 0, NULL);
    command = *argv++;
    --argc;
    if (strncmp(command, "--runtime", 9) == 0) {
        fprintf(stderr, "beam: runtime selection is obsolete; beam runs modern BEAM\n");
        return 1;
    }
    if ((!strcmp(command, "run") || !strcmp(command, "task") || !strcmp(command, "build")) &&
        reject_runtime_flags(argc, argv)) return 1;
    if (strcmp(command, "run") == 0)
        return cmd_run(argc, argv);
    if (strcmp(command, "task") == 0) {
        return beam_task_main(argc, argv);
    }
    if (strcmp(command, "build") == 0) {
        return beam_build_main(argc + 1, argv - 1);
    }
    if (strcmp(command, "help") == 0 && argc == 1 && strcmp(argv[0], "build") == 0) {
        char *help[] = {"help", "build"}; return beam_build_main(2, help);
    }
    if (strcmp(command, "compile") == 0)
        return cmd_compile(argc, argv);
    for (i = 0; i < argc; ++i) {
        if (strncmp(argv[i], "--runtime", 9) == 0) {
            fprintf(stderr, "beam: runtime selection is obsolete; beam runs modern BEAM\n");
            return 1;
        }
    }
    if (strcmp(command, "help") == 0) {
        if (argc > 1) {
            usage(stderr);
            return 1;
        }
        return help_command(argc ? argv[0] : NULL);
    }
    if (strcmp(command, "version") == 0) {
        if (argc) {
            fputs("usage: beam version\n", stderr);
            return 1;
        }
        printf("%s\n", beam_version());
        return 0;
    }
    if (strcmp(command, "eval") == 0) {
        if (argc != 1) {
            fputs("usage: beam eval <expression>\n", stderr);
            return 1;
        }
        return boot_command("eval", 1, argv);
    }
    if (strcmp(command, "shell") == 0) {
        if (argc == 2 && !strcmp(argv[0], "--project") && argv[1][0] && argv[1][0] != '-') {
            return beam_project_main(argv[1], 0, NULL, 1);
        }
        if (argc) {
            fputs("usage: beam shell\n", stderr);
            return 1;
        }
        return boot_command("shell", 0, NULL);
    }
    fprintf(stderr, "beam: unknown command '%s'\n", command);
    usage(stderr);
    return 1;
}

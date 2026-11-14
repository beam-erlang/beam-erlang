/* Project-owned private Windows test process support. */
#ifndef BEAM_EMULATOR_WINDOWS_PROCESS_H
#define BEAM_EMULATOR_WINDOWS_PROCESS_H

/* argv includes argv[0]. The exact executable is selected independently.
 * Every child is suspended until its job membership and receipt are durable. */
int beam_test_windows_start(const char *executable, char **argv, int detached,
                            const char *record_path);

/* Contain the parser entry itself before it may start native helpers. */
int beam_test_windows_contain_launcher(void);

#endif

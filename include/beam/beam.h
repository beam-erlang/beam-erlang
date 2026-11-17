/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_BEAM_H
#define BEAM_BEAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start the process-owning BEAM emulator with its ordinary raw arguments.
 * Call once on the original process main thread. The runtime owns signals,
 * standard descriptors and the process lifetime; halt and fatal errors
 * terminate the process. The integer signature does not promise a return to
 * the caller or provide a returning embedding lifecycle. Use the matching
 * runtime and native headers from one installed preview SDK. On Windows argv
 * must contain UTF-8 strings; hosts using wmain convert their UTF-16 arguments
 * before calling this entrypoint.
 */
#if defined(_WIN32)
__declspec(dllexport)
#elif defined(__GNUC__)
__attribute__((visibility("default")))
#endif
int beam_main(int argc, char **argv);

#if defined(_WIN32)
/* Read a key through the initialized VM console input owner. Windows launchers
 * use this from their exit handler for -keep_window. Call only after entering
 * beam_main, on its original thread; this is not a pre-start console API.
 */
__declspec(dllexport)
int beam_windows_wait_for_console_key(void);
#endif

#ifdef __cplusplus
}
#endif
#endif

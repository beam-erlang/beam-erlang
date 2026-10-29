/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_NATIVE_H
#define BEAM_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental shared mechanisms for native SDK consumers. */
/* Valid from native callbacks throughout the current VM lifetime, including
 * quiescent destruction. Querying never pumps or reenters the VM. */
int beam_native_stop_requested(void);
/* Copy the first cleanup diagnostic without replacing the terminal outcome.
 * Thread-safe within native callbacks; message must be NUL-terminated and is
 * truncated to the report capacity. This does not request shutdown. */
void beam_native_report_cleanup(int native_code, const char *message);
/* Request coordinated VM shutdown from a native worker or monitor. Copies
 * bounded diagnostics; first terminal cause wins. Does not invoke or wait for
 * an owner-thread callback. Valid only while the calling native code belongs
 * to the active VM, including its quiescent destruction. */
void beam_native_request_failure(const char *origin, const char *reason);

typedef enum beam_native_trace_backend {
    BEAM_NATIVE_TRACE_NONE = 0,
    BEAM_NATIVE_TRACE_DTRACE = 1,
    BEAM_NATIVE_TRACE_SYSTEMTAP = 2,
    BEAM_NATIVE_TRACE_LTTNG = 3
} beam_native_trace_backend;

/* The VM's compiled tracing backend. This query allocates nothing, starts no
 * system or VM, and is valid before initialization and after termination. Its
 * result is constant for the lifetime of the loaded runtime library. */
beam_native_trace_backend beam_native_trace_backend_get(void);

typedef enum beam_native_failure_mode {
    BEAM_NATIVE_FAILURE_ERROR = 0,
    BEAM_NATIVE_FAILURE_ABORT = 1,
    BEAM_NATIVE_FAILURE_DUMP = 2
} beam_native_failure_mode;

/* Unrecoverable failure during an active VM's native execution. Never returns.
 * ERROR emits diagnostics, attempts an Erlang dump unless suppressed, and
 * aborts. ABORT emits diagnostics and aborts without an Erlang dump. DUMP emits
 * diagnostics, attempts an Erlang dump, and exits with failure status. Existing
 * VM dump limits and output policy apply. Unknown modes use ERROR semantics.
 * The format accepts the VM's %T conversion with public native term values.
 * This terminates the process; normal host shutdown must use the lifecycle API. */
#if defined(__cplusplus)
[[noreturn]]
#elif defined(_MSC_VER)
__declspec(noreturn)
#elif defined(__GNUC__)
__attribute__((noreturn))
#else
_Noreturn
#endif
void beam_native_fail(beam_native_failure_mode mode, const char *format, ...);

/* Packet framing shared by native consumers and the VM's decode_packet BIF.
 * Calls require native execution in an active VM. No call starts a VM or
 * acquires code/services. All spans and callback records are borrowed for the
 * duration of a call; callbacks must copy anything retained afterwards. */
typedef enum beam_packet_format {
    BEAM_PACKET_RAW, BEAM_PACKET_1, BEAM_PACKET_2, BEAM_PACKET_4,
    BEAM_PACKET_ASN1, BEAM_PACKET_RM, BEAM_PACKET_CDR, BEAM_PACKET_FCGI,
    BEAM_PACKET_LINE, BEAM_PACKET_TPKT, BEAM_PACKET_HTTP, BEAM_PACKET_HTTPH,
    BEAM_PACKET_TLS, BEAM_PACKET_HTTP_BIN, BEAM_PACKET_HTTPH_BIN
} beam_packet_format;

typedef struct beam_packet_name {
    const char *name;             /* Canonical, NUL-terminated known name. */
    int length;
    int index;                    /* Zero-based index; not an atom value. */
} beam_packet_name;

typedef enum beam_packet_uri_kind {
    BEAM_PACKET_URI_STAR, BEAM_PACKET_URI_STRING, BEAM_PACKET_URI_PATH,
    BEAM_PACKET_URI_SCHEME, BEAM_PACKET_URI_HTTP, BEAM_PACKET_URI_HTTPS
} beam_packet_uri_kind;

typedef struct beam_packet_uri {
    beam_packet_uri_kind kind;
    const char *first;
    int first_length;
    const char *second;
    int second_length;
    int port;                     /* Zero means unspecified. */
} beam_packet_uri;

typedef struct beam_packet_callbacks {
    int (*http_response)(void *, int major, int minor, int status,
                         const char *phrase, int length);
    int (*http_request)(void *, const beam_packet_name *known,
                        const char *method, int length,
                        const beam_packet_uri *, int major, int minor);
    int (*http_eoh)(void *);
    int (*http_header)(void *, const beam_packet_name *known,
                       const char *name, int name_length,
                       const char *original, int original_length,
                       const char *value, int value_length);
    int (*http_error)(void *, const char *, int length);
    int (*tls)(void *, unsigned type, unsigned major, unsigned minor,
               const char *data, int length, const char *prefix, int prefix_length);
} beam_packet_callbacks;

/* Return the total frame length (>0, possibly larger than available input),
 * zero when more header/line data is needed, or -1 for invalid input/format.
 * Input lengths must fit int. Limits of zero mean unlimited. State must point
 * to 0 (HTTP start line) or 1 (headers); HTTPH formats set it to 1. */
int beam_packet_length(beam_packet_format format, const char *bytes,
                       unsigned length, unsigned maximum, unsigned truncate,
                       char delimiter, int *state);

/* Select the body of a complete frame previously identified by length().
 * Adjust the borrowed pointer/length in place. Strip 1/2/4-byte length headers
 * or FastCGI padding; other formats remain unchanged. Return 0 or -1 for
 * invalid arguments/structural header length, leaving outputs unchanged on -1. */
int beam_packet_body(beam_packet_format format, const char **bytes, int *length);

/* Parse a complete frame identified by length(). State is updated for HTTP.
 * All five HTTP callbacks are required for HTTP formats, tls for TLS; others
 * need no callbacks. Known method/header records are NULL for unknown names.
 * Return 1 for handled HTTP (malformed lines invoke http_error), the TLS
 * callback's result for TLS, 0 for other formats, or -1 for invalid arguments.
 * A negative HTTP callback result invokes http_error, preserving VM semantics.
 * TLS callbacks receive a synthetic prefix for SSLv2 ClientHello only. */
int beam_packet_parse(beam_packet_format format, const char *bytes, int length,
                      int *state, const beam_packet_callbacks *callbacks,
                      void *context);

#ifdef __cplusplus
}
#endif
#endif

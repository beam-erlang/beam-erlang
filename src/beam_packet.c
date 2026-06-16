/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "packet_parser.h"
#include <beam/native.h>
#include <limits.h>

static int packet_format(beam_packet_format format, enum PacketParseType *type)
{
    switch (format) {
#define FORMAT(public_name, private_name) \
    case BEAM_PACKET_##public_name: *type = TCP_PB_##private_name; return 1
    FORMAT(RAW, RAW); FORMAT(1, 1); FORMAT(2, 2); FORMAT(4, 4);
    FORMAT(ASN1, ASN1); FORMAT(RM, RM); FORMAT(CDR, CDR); FORMAT(FCGI, FCGI);
    FORMAT(LINE, LINE_LF); FORMAT(TPKT, TPKT); FORMAT(HTTP, HTTP);
    FORMAT(HTTPH, HTTPH); FORMAT(TLS, SSL_TLS);
    FORMAT(HTTP_BIN, HTTP_BIN); FORMAT(HTTPH_BIN, HTTPH_BIN);
#undef FORMAT
    default: return 0;
    }
}

int beam_packet_length(beam_packet_format format, const char *bytes,
                       unsigned length, unsigned maximum, unsigned truncate,
                       char delimiter, int *state)
{
    enum PacketParseType type;
    if (!packet_format(format, &type) || !bytes || !state ||
        (*state != 0 && *state != 1) || length > INT_MAX)
        return -1;
    return packet_get_length(type, bytes, length, maximum, truncate, delimiter, state);
}

int beam_packet_body(beam_packet_format format, const char **bytes, int *length)
{
    enum PacketParseType type;
    int minimum = 0;
    if (!packet_format(format, &type) || !bytes || !*bytes || !length || *length < 0)
        return -1;
    switch (type) {
    case TCP_PB_1: minimum = 1; break;
    case TCP_PB_2: minimum = 2; break;
    case TCP_PB_4: minimum = 4; break;
    case TCP_PB_FCGI:
        if (*length < 8 || (unsigned char)(*bytes)[6] > *length - 8)
            return -1;
        break;
    default: break;
    }
    if (*length < minimum)
        return -1;
    packet_get_body(type, bytes, length);
    return 0;
}

typedef struct {
    const beam_packet_callbacks *callbacks;
    void *context;
} packet_context;

static beam_packet_name packet_name(const http_atom_t *name)
{
    return (beam_packet_name){name->name, name->len, name->index};
}

static int packet_response(void *opaque, int major, int minor, int status,
                           const char *phrase, int length)
{
    packet_context *p = opaque;
    return p->callbacks->http_response(p->context, major, minor, status, phrase, length);
}

static int packet_request(void *opaque, const http_atom_t *method,
                          const char *text, int length, const PacketHttpURI *uri,
                          int major, int minor)
{
    packet_context *p = opaque;
    beam_packet_name name;
    beam_packet_uri value = {0};
    if (method)
        name = packet_name(method);
    /* Private URI fields not belonging to its variant are uninitialized. */
    switch (uri->type) {
    case URI_STAR: value.kind = BEAM_PACKET_URI_STAR; break;
    case URI_STRING: value.kind = BEAM_PACKET_URI_STRING; break;
    case URI_ABS_PATH: value.kind = BEAM_PACKET_URI_PATH; break;
    case URI_SCHEME: value.kind = BEAM_PACKET_URI_SCHEME; break;
    case URI_HTTP: value.kind = BEAM_PACKET_URI_HTTP; break;
    case URI_HTTPS: value.kind = BEAM_PACKET_URI_HTTPS; break;
    default: return -1;
    }
    if (uri->type != URI_STAR) {
        value.first = uri->s1_ptr;
        value.first_length = uri->s1_len;
    }
    if (uri->type == URI_SCHEME || uri->type == URI_HTTP || uri->type == URI_HTTPS) {
        value.second = uri->s2_ptr;
        value.second_length = uri->s2_len;
    }
    if (uri->type == URI_HTTP || uri->type == URI_HTTPS)
        value.port = uri->port;
    return p->callbacks->http_request(p->context, method ? &name : NULL,
                                      text, length, &value, major, minor);
}

static int packet_eoh(void *opaque)
{
    packet_context *p = opaque;
    return p->callbacks->http_eoh(p->context);
}

static int packet_header(void *opaque, const http_atom_t *header,
                         const char *text, int length, const char *original,
                         int original_length, const char *value, int value_length)
{
    packet_context *p = opaque;
    beam_packet_name name;
    if (header)
        name = packet_name(header);
    return p->callbacks->http_header(p->context, header ? &name : NULL,
                                     text, length, original, original_length,
                                     value, value_length);
}

static int packet_error(void *opaque, const char *bytes, int length)
{
    packet_context *p = opaque;
    return p->callbacks->http_error(p->context, bytes, length);
}

static int packet_tls(void *opaque, unsigned type, unsigned major, unsigned minor,
                      const char *data, int length, const char *prefix, int prefix_length)
{
    packet_context *p = opaque;
    return p->callbacks->tls(p->context, type, major, minor, data, length,
                             prefix, prefix_length);
}

int beam_packet_parse(beam_packet_format format, const char *bytes, int length,
                      int *state, const beam_packet_callbacks *callbacks,
                      void *context)
{
    enum PacketParseType type;
    packet_context adapter = {callbacks, context};
    PacketCallbacks dispatch = {packet_response, packet_request, packet_eoh,
                                packet_header, packet_error, packet_tls};
    if (!packet_format(format, &type) || !bytes || length < 0 || !state ||
        (*state != 0 && *state != 1))
        return -1;
    switch (type) {
    case TCP_PB_HTTP: case TCP_PB_HTTP_BIN:
    case TCP_PB_HTTPH: case TCP_PB_HTTPH_BIN:
        if (!callbacks || !callbacks->http_response || !callbacks->http_request ||
            !callbacks->http_eoh || !callbacks->http_header || !callbacks->http_error)
            return -1;
        break;
    case TCP_PB_SSL_TLS:
        if (length < 5 || !callbacks || !callbacks->tls)
            return -1;
        break;
    default: return 0;
    }
    return packet_parse(type, bytes, length, state, &dispatch, &adapter);
}

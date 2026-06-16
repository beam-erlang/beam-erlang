/* Public SDK consumer: turn callback records into decode_packet-shaped terms. */
#include <limits.h>
#include <string.h>
typedef struct {
    ErlNifEnv *env;
    ERL_NIF_TERM result;
    int binary;
    int callback_status;
} packet_probe_context;
static ERL_NIF_TERM pa(ErlNifEnv *env, const char *name) { return enif_make_atom(env, name); }
static ERL_NIF_TERM ps(packet_probe_context *p, const char *data, int length) {
    if (!p->binary) return enif_make_string_len(p->env, data, length, ERL_NIF_LATIN1);
    ERL_NIF_TERM term;
    unsigned char *bytes = enif_make_new_binary(p->env, length, &term);
    if (length) memcpy(bytes, data, length);
    return term;
}
static ERL_NIF_TERM pv(packet_probe_context *p, int major, int minor) {
    return enif_make_tuple2(p->env, enif_make_int(p->env, major), enif_make_int(p->env, minor));
}
static int pr_response(void *opaque, int major, int minor, int status, const char *text, int length) {
    packet_probe_context *p = opaque;
    p->result = enif_make_tuple4(p->env, pa(p->env,"http_response"), pv(p,major,minor),
                                enif_make_int(p->env,status), ps(p,text,length));
    return p->callback_status;
}
static int pr_request(void *opaque, const beam_packet_name *known, const char *text,
                       int length, const beam_packet_uri *uri, int major, int minor) {
    packet_probe_context *p = opaque;
    ERL_NIF_TERM value;
    if (uri->kind == BEAM_PACKET_URI_STAR) {
        if (uri->first || uri->second || uri->first_length || uri->second_length || uri->port) abort();
        value = pa(p->env,"*");
    } else {
        ERL_NIF_TERM first = ps(p,uri->first,uri->first_length);
        switch (uri->kind) {
        case BEAM_PACKET_URI_STRING: value = first; break;
        case BEAM_PACKET_URI_PATH: value = enif_make_tuple2(p->env,pa(p->env,"abs_path"),first); break;
        case BEAM_PACKET_URI_SCHEME:
            value = enif_make_tuple3(p->env,pa(p->env,"scheme"),first,ps(p,uri->second,uri->second_length)); break;
        case BEAM_PACKET_URI_HTTP: case BEAM_PACKET_URI_HTTPS:
            value = enif_make_tuple5(p->env,pa(p->env,"absoluteURI"),
                pa(p->env,uri->kind==BEAM_PACKET_URI_HTTP ? "http" : "https"),first,
                uri->port ? enif_make_int(p->env,uri->port) : pa(p->env,"undefined"),
                ps(p,uri->second,uri->second_length)); break;
        default: abort();
        }
    }
    if (known && (known->length != (int)strlen(known->name) || known->index < 0)) abort();
    p->result = enif_make_tuple4(p->env,pa(p->env,"http_request"),
        known ? pa(p->env,known->name) : ps(p,text,length),value,pv(p,major,minor));
    return p->callback_status;
}
static int pr_header(void *opaque, const beam_packet_name *known, const char *name, int length,
                     const char *original, int original_length, const char *value, int value_length) {
    packet_probe_context *p = opaque;
    if (known && (known->length != (int)strlen(known->name) || known->index < 0)) abort();
    p->result = enif_make_tuple5(p->env,pa(p->env,"http_header"),
        enif_make_int(p->env,known ? known->index+1 : 0),
        known ? pa(p->env,known->name) : ps(p,name,length),
        ps(p,original,original_length),ps(p,value,value_length));
    return p->callback_status;
}
static int pr_eoh(void *opaque) {
    packet_probe_context *p = opaque; p->result = pa(p->env,"http_eoh"); return p->callback_status;
}
static int pr_error(void *opaque, const char *bytes, int length) {
    packet_probe_context *p = opaque;
    p->result = enif_make_tuple2(p->env,pa(p->env,"http_error"),ps(p,bytes,length)); return 1;
}
static int pr_tls(void *opaque, unsigned type, unsigned major, unsigned minor,
                  const char *data, int length, const char *prefix, int prefix_length) {
    packet_probe_context *p = opaque;
    ERL_NIF_TERM value;
    unsigned char *bytes = enif_make_new_binary(p->env,length+prefix_length,&value);
    if (prefix_length) memcpy(bytes,prefix,prefix_length);
    if (length) memcpy(bytes+prefix_length,data,length);
    p->result = enif_make_tuple5(p->env,pa(p->env,"ssl_tls"),enif_make_list(p->env,0),
                                 enif_make_uint(p->env,type),pv(p,major,minor),value);
    return p->callback_status;
}
static ERL_NIF_TERM packet(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    int format, state; unsigned maximum, truncate, delimiter; ErlNifBinary data;
    if (argc != 6 || !enif_get_int(env,argv[0],&format) ||
        !enif_inspect_binary(env,argv[1],&data) || data.size > INT_MAX ||
        !enif_get_uint(env,argv[2],&maximum) || !enif_get_uint(env,argv[3],&truncate) ||
        !enif_get_uint(env,argv[4],&delimiter) || delimiter > 255 || !enif_get_int(env,argv[5],&state))
        return enif_make_badarg(env);
    const char *bytes = data.size ? (const char *)data.data : "";
    int size = beam_packet_length(format,bytes,data.size,maximum,truncate,delimiter,&state);
    ERL_NIF_TERM result;
    if (size < 0) result = enif_make_tuple2(env,pa(env,"error"),pa(env,"invalid"));
    else if (!size || size > (int)data.size)
        result = enif_make_tuple2(env,pa(env,"more"),size ? enif_make_int(env,size) : pa(env,"undefined"));
    else {
        beam_packet_callbacks callbacks = {pr_response,pr_request,pr_eoh,pr_header,pr_error,pr_tls};
        packet_probe_context p = {env,pa(env,"no_callback"),
            format==BEAM_PACKET_HTTP_BIN || format==BEAM_PACKET_HTTPH_BIN,1};
        int parsed = beam_packet_parse(format,bytes,size,&state,&callbacks,&p);
        if (parsed < 0) result = enif_make_tuple2(env,pa(env,"error"),pa(env,"invalid"));
        else {
            if (!parsed) {
                const char *body = bytes; int length = size;
                if (beam_packet_body(format,&body,&length)) abort();
                p.result = enif_make_sub_binary(env,argv[1],body-bytes,length);
            }
            result = enif_make_tuple3(env,pa(env,"ok"),p.result,enif_make_sub_binary(env,argv[1],size,data.size-size));
        }
    }
    return enif_make_tuple2(env,result,enif_make_int(env,state));
}
static ERL_NIF_TERM packet_invalid(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    (void)argc; (void)argv;
    int state=0, length=0; const char *bytes="";
#define PCHECK(test) do { if (!(test)) return enif_make_badarg(env); } while (0)
    PCHECK(beam_packet_length((beam_packet_format)-1,bytes,0,0,0,'\n',&state)==-1);
    PCHECK(beam_packet_length(BEAM_PACKET_RAW,NULL,0,0,0,'\n',&state)==-1);
    PCHECK(beam_packet_length(BEAM_PACKET_RAW,bytes,UINT_MAX,0,0,'\n',&state)==-1);
    PCHECK(beam_packet_parse(BEAM_PACKET_HTTP,bytes,0,&state,NULL,NULL)==-1);
    PCHECK(beam_packet_parse(BEAM_PACKET_TLS,bytes,0,&state,NULL,NULL)==-1);
    PCHECK(beam_packet_parse(BEAM_PACKET_RAW,bytes,0,&state,NULL,NULL)==0);
    PCHECK(beam_packet_body(BEAM_PACKET_4,&bytes,&length)==-1 && length==0);
    PCHECK(beam_packet_body(BEAM_PACKET_FCGI,&bytes,&length)==-1 && length==0);
    packet_probe_context p = {env,pa(env,"unset"),0,-7};
    beam_packet_callbacks callbacks = {pr_response,pr_request,pr_eoh,pr_header,pr_error,pr_tls};
    const char response[] = "HTTP/1.1 200 OK\r\n";
    PCHECK(beam_packet_parse(BEAM_PACKET_HTTP,response,sizeof(response)-1,&state,&callbacks,&p)==1);
    const ERL_NIF_TERM *tuple; int arity;
    PCHECK(enif_get_tuple(env,p.result,&arity,&tuple) && arity==2 &&
           enif_is_identical(tuple[0],pa(env,"http_error")));
    const char tls[] = {23,3,3,0,0};
    PCHECK(beam_packet_parse(BEAM_PACKET_TLS,tls,sizeof(tls),&state,&callbacks,&p)==-7);
    state=2;
    PCHECK(beam_packet_length(BEAM_PACKET_HTTP,bytes,0,0,0,'\n',&state)==-1 && state==2);
#undef PCHECK
    return pa(env,"ok");
}

/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C"
#endif
int codec_probe(void);
#ifndef CODEC_ONLY
#include <beam/beam.h>
#include <beam/beam_etf.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n",__LINE__,#expr); return 1; } } while (0)
static int lifetime(const char *module)
{
    BeamEtfBuffer arguments;
    BeamEtfFun fun = {0}, retained_fun = {0};
    char wire_fun[2048], atom[32];
    int i = 0, arity, version;
    long number;
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_error error = {0};
    beam_result result;
    memset(&result,0,sizeof result);
    CHECK(beam_etf_x_new_with_version(&arguments) == 0);
    CHECK(beam_etf_x_encode_list_header(&arguments,1) == 0);
    CHECK(beam_etf_x_encode_tuple_header(&arguments,2) == 0);
    CHECK(beam_etf_x_encode_atom(&arguments,"\xc3\xa9") == 0);
    CHECK(beam_etf_x_encode_long(&arguments,42) == 0);
    CHECK(beam_etf_x_encode_empty_list(&arguments) == 0);
#ifdef __cplusplus
    fun.type = BeamEtfFun::BEAM_ETF_FUN_EXPORT;
#else
    fun.type = BEAM_ETF_FUN_EXPORT;
#endif
    /* Force the exported function name into owned heap storage. */
    for (int j=0; j<255; ++j) memcpy(fun.module+4*j,"\xf0\x9f\x90\xa7",4);
    fun.module[1020] = 0;
    fun.u.exprt.func = (char *)"length"; fun.arity = 1;
    CHECK(beam_etf_encode_fun(wire_fun,&i,&fun) == 0);
    i = 0; CHECK(beam_etf_decode_fun(wire_fun,&i,&retained_fun) == 0 && retained_fun.u.exprt.func_allocated);
    FILE *input = fopen(module,"rb"); CHECK(input != NULL);
    CHECK(fseek(input,0,SEEK_END) == 0);
    long length = ftell(input); CHECK(length > 0 && fseek(input,0,SEEK_SET) == 0);
    unsigned char *bytes = (unsigned char *)malloc((size_t)length); CHECK(bytes != NULL);
    CHECK(fread(bytes,1,(size_t)length,input) == (size_t)length && fclose(input) == 0);
    beam_bytes code = {bytes,(size_t)length};
    beam_bytes args = {(const unsigned char *)arguments.buff,(size_t)arguments.index};
    CHECK(beam_init(&system,&error) == BEAM_OK);
    CHECK(beam_vm_init(system,NULL,&vm,&error) == BEAM_OK);
    CHECK(beam_module_load(vm,code) == BEAM_OK); free(bytes);
    CHECK(beam_vm_start(vm,&error) == BEAM_OK);
    CHECK(beam_vm_call(vm,"etf_echo","echo",args,&result) == BEAM_OK && result.kind == BEAM_RETURN);
    CHECK(beam_vm_terminate(&vm,NULL) == BEAM_OK);
    CHECK(beam_terminate(&system) == BEAM_OK);
    /* Codec and copied VM results remain usable after both lifetimes end. */
    i = 0;
    const char *returned = (const char *)result.value.data;
    CHECK(beam_etf_decode_version(returned,&i,&version) == 0 && version == 131);
    CHECK(beam_etf_decode_tuple_header(returned,&i,&arity) == 0 && arity == 2);
    CHECK(beam_etf_decode_atom(returned,&i,atom,sizeof atom) == 0 && !strcmp(atom,"\xc3\xa9"));
    CHECK(beam_etf_decode_long(returned,&i,&number) == 0 && number == 42 && (size_t)i == result.value.size);
    CHECK(!strcmp(retained_fun.u.exprt.func,"length"));
    beam_etf_free_fun(&retained_fun);
    CHECK(beam_etf_x_free(&arguments) == 0);
    beam_result_release(&result);
    return 0;
}
#endif
int main(int argc, char **argv)
{
    if (codec_probe()) return 1;
#ifndef CODEC_ONLY
    if (argc != 2 || lifetime(argv[1])) return 1;
#endif
    puts("ETF SDK OK");
    return 0;
}

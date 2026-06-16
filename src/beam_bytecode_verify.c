/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_bytecode_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rotate(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_block(uint32_t state[8], const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64], a=state[0], b=state[1], c=state[2], d=state[3];
    uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
    for (unsigned i=0; i<16; ++i)
        w[i] = ((uint32_t)block[4*i]<<24) | ((uint32_t)block[4*i+1]<<16) |
               ((uint32_t)block[4*i+2]<<8) | block[4*i+3];
    for (unsigned i=16; i<64; ++i) {
        uint32_t x=w[i-15], y=w[i-2];
        w[i] = w[i-16] + (rotate(x,7)^rotate(x,18)^(x>>3)) + w[i-7] +
               (rotate(y,17)^rotate(y,19)^(y>>10));
    }
    for (unsigned i=0; i<64; ++i) {
        uint32_t t1 = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) +
                      ((e&f)^((~e)&g)) + k[i] + w[i];
        uint32_t t2 = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void beam_bytecode_sha256(beam_bytes bytes, unsigned char digest[32])
{
    uint32_t state[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                         0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    unsigned char tail[64] = {0};
    size_t offset=0, remaining=bytes.size;
    uint64_t bits=(uint64_t)bytes.size*8;
    while (remaining>=64) {
        sha256_block(state, bytes.data+offset);
        offset+=64; remaining-=64;
    }
    if (remaining) memcpy(tail, bytes.data+offset, remaining);
    tail[remaining]=0x80;
    if (remaining>=56) {
        sha256_block(state, tail);
        memset(tail, 0, sizeof(tail));
    }
    for (unsigned i=0; i<8; ++i) tail[63-i]=(unsigned char)(bits>>(8*i));
    sha256_block(state, tail);
    for (unsigned i=0; i<8; ++i)
        for (unsigned j=0; j<4; ++j)
            digest[4*i+j]=(unsigned char)(state[i]>>(24-8*j));
}

static beam_status fail(beam_error *error, beam_status status,
                        const char *message, const char *module)
{
    if (error)
        snprintf(error->message, sizeof(error->message), "%s%s%s",
                 message, module ? ": " : "", module ? module : "");
    return status;
}

beam_status beam_bytecode_verify(const beam_bytecode_inventory *inventory,
                                const beam_module_spec *modules, size_t count,
                                beam_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!inventory || !inventory->build_id || !*inventory->build_id ||
        !inventory->count || inventory->count>4096 ||
        !inventory->records || !modules || count!=inventory->count)
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid bytecode inventory", NULL);
    if (strcmp(inventory->build_id, beam_build_id()))
        return fail(error, BEAM_INVALID_ARGUMENT, "bytecode inventory does not match the loaded runtime build", NULL);
    for (size_t i=0; i<count; ++i) {
        const beam_bytecode_record *record=&inventory->records[i];
        const beam_module_spec *module=NULL;
        unsigned char digest[32];
        if (!record->name || !*record->name || !record->relative_path || !*record->relative_path)
            return fail(error, BEAM_INVALID_ARGUMENT, "invalid bytecode expectation", NULL);
        for (size_t j=0; j<i; ++j)
            if (!strcmp(record->name, inventory->records[j].name))
                return fail(error, BEAM_INVALID_ARGUMENT, "duplicate bytecode expectation", record->name);
        for (size_t j=0; j<count; ++j) {
            if (!modules[j].name || !*modules[j].name)
                return fail(error, BEAM_INVALID_ARGUMENT, "invalid supplied module name", NULL);
            if (!strcmp(record->name, modules[j].name)) {
                if (module)
                    return fail(error, BEAM_INVALID_ARGUMENT, "duplicate supplied module", record->name);
                module=&modules[j];
            }
        }
        if (!module)
            return fail(error, BEAM_INVALID_ARGUMENT, "required bytecode module is missing", record->name);
        if ((module->bytes.size && !module->bytes.data) || module->bytes.size>UINT64_MAX/8)
            return fail(error, BEAM_INVALID_ARGUMENT, "invalid supplied module bytes", record->name);
        if (module->bytes.size!=record->size)
            return fail(error, BEAM_INVALID_BEAM, "bytecode size differs from the selected build", record->name);
        beam_bytecode_sha256(module->bytes, digest);
        if (memcmp(digest, record->sha256, sizeof(digest)))
            return fail(error, BEAM_INVALID_BEAM, "bytecode differs from the selected build", record->name);
    }
    return BEAM_OK;
}

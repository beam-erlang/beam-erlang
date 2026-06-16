/* SPDX-License-Identifier: Apache-2.0 */
#include <beam_bytecode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "bytecode verifier:%d: %s (%s)\n", __LINE__, #expression, error.message); \
    return 1; } } while (0)

static void digest(const char *hex, unsigned char bytes[32])
{
    for (size_t i=0; i<32; ++i) {
        unsigned value;
        if (sscanf(hex+2*i, "%2x", &value)!=1) abort();
        bytes[i]=(unsigned char)value;
    }
}

int main(void)
{
    beam_error error={0};
    const char *build_id=beam_build_id();
    CHECK(build_id && *build_id && build_id==beam_build_id());
    struct { const char *text, *sha256; } vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}
    };
    beam_bytecode_record records[2]={{"one", "ebin/one.beam", 0, {0}},
                                     {"two", "ebin/two.beam", 0, {0}}};
    beam_bytecode_inventory inventory={build_id, 1, records};
    beam_module_spec modules[2]={{"one", {0}, "memory:one"}, {"two", {0}, "memory:two"}};
    for (size_t i=0; i<sizeof(vectors)/sizeof(vectors[0]); ++i) {
        records[0].size=strlen(vectors[i].text);
        digest(vectors[i].sha256, records[0].sha256);
        modules[0].bytes=(beam_bytes){(const unsigned char *)vectors[i].text, records[0].size};
        CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_OK);
        records[0].sha256[0]^=1;
        CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_INVALID_BEAM);
        CHECK(strstr(error.message, "one"));
    }
    unsigned char *million=malloc(1000000);
    CHECK(million);
    memset(million, 'a', 1000000);
    records[0].size=1000000;
    digest("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", records[0].sha256);
    modules[0].bytes=(beam_bytes){million, 1000000};
    CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_OK);
    million[999999]='b';
    CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_INVALID_BEAM);
    million[999999]='a';
    inventory.build_id="another-build";
    CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_INVALID_ARGUMENT);
    inventory.build_id=build_id;
    CHECK(beam_bytecode_verify(&inventory, modules, 0, &error)==BEAM_INVALID_ARGUMENT);
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_INVALID_ARGUMENT);
    CHECK(beam_bytecode_verify(NULL, modules, 1, &error)==BEAM_INVALID_ARGUMENT);
    CHECK(beam_bytecode_verify(&inventory, NULL, 1, &error)==BEAM_INVALID_ARGUMENT);
    modules[0].bytes.size--;
    CHECK(beam_bytecode_verify(&inventory, modules, 1, &error)==BEAM_INVALID_BEAM);
    modules[0].bytes.size++;
    records[1]=records[0]; records[1].name="two";
    modules[1]=modules[0]; modules[1].name="two";
    inventory.count=2;
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_OK);
    beam_module_spec reversed[]={modules[1], modules[0]};
    CHECK(beam_bytecode_verify(&inventory, reversed, 2, &error)==BEAM_OK);
    modules[1].name="one";
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_INVALID_ARGUMENT);
    modules[1].name="extra";
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_INVALID_ARGUMENT);
    modules[1].name="two"; records[1].name="one";
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_INVALID_ARGUMENT);
    records[1].name="two"; modules[1].bytes.data=NULL;
    CHECK(beam_bytecode_verify(&inventory, modules, 2, &error)==BEAM_INVALID_ARGUMENT);
    free(million);
    return 0;
}

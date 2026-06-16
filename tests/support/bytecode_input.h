/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BYTECODE_INPUT_H
#define BYTECODE_INPUT_H
#include <beam/sdk/inventories.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { INPUT_BASE = 1, INPUT_BOOT = 2, INPUT_SERVICES = 4,
       INPUT_RETURNING = INPUT_BASE | INPUT_BOOT | INPUT_SERVICES };

/* Test-owned acquisition. Composition receives exactly the verified buffers. */
typedef struct bytecode_input {
    beam_module_spec modules[13];
    char paths[13][4096];
    size_t count;
} bytecode_input;

static void bytecode_input_release(bytecode_input *input)
{
    for (size_t i = 0; i < input->count; ++i) free((void *)input->modules[i].bytes.data);
    memset(input, 0, sizeof(*input));
}

static beam_status bytecode_input_read(bytecode_input *input, const char *prefix,
                                      unsigned selection, beam_error *error)
{
    const beam_sdk_inventory *inventories[] = {
        beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE),
        beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BOOT),
        beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)};
    char path[4096];
    unsigned char manifest[512];
    FILE *file = NULL;
    beam_status status = BEAM_RUNTIME_NOT_FOUND;
    memset(input, 0, sizeof(*input));
    if (!selection || (selection & ~INPUT_RETURNING)) return BEAM_INVALID_ARGUMENT;
    for (size_t inventory = 0; inventory < 3; ++inventory) {
        if (!(selection & (1u << inventory))) continue;
        if (snprintf(path, sizeof(path), "%s/%s", prefix, inventories[inventory]->manifest_path) >= (int)sizeof(path)) goto failed;
        file = fopen(path, "rb");
        if (!file) goto failed;
        size_t size = fread(manifest, 1, sizeof(manifest), file);
        int valid = !ferror(file) && size == inventories[inventory]->manifest.size &&
            !memcmp(manifest, inventories[inventory]->manifest.data, size);
        int closed = fclose(file); file = NULL;
        if (!valid || closed) { status = BEAM_INVALID_ARGUMENT; goto failed; }
        size_t start = input->count;
        for (size_t i = 0; i < inventories[inventory]->bytecode->count; ++i) {
            const beam_bytecode_record *record = &inventories[inventory]->bytecode->records[i];
            size_t index = input->count++;
            long length;
            if (snprintf(input->paths[index], sizeof(input->paths[index]), "%s/%s", prefix,
                         record->relative_path) >= (int)sizeof(input->paths[index])) goto failed;
            file = fopen(input->paths[index], "rb");
            if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
                fseek(file, 0, SEEK_SET)) goto failed;
            unsigned char *bytes = (unsigned char *)malloc((size_t)length);
            if (!bytes) { status = BEAM_OUT_OF_MEMORY; goto failed; }
            input->modules[index].name = record->name;
            input->modules[index].origin = input->paths[index];
            input->modules[index].bytes = (beam_bytes){bytes, (size_t)length};
            if (fread(bytes, 1, (size_t)length, file) != (size_t)length || fgetc(file) != EOF || ferror(file)) goto failed;
            closed = fclose(file); file = NULL;
            if (closed) goto failed;
        }
        status = beam_bytecode_verify(inventories[inventory]->bytecode, input->modules + start,
                                      input->count - start, error);
        if (status != BEAM_OK) { bytecode_input_release(input); return status; }
        status = BEAM_RUNTIME_NOT_FOUND;
    }
    return BEAM_OK;
failed:
    if (file) fclose(file);
    bytecode_input_release(input);
    if (error) snprintf(error->message, sizeof(error->message), "selected bytecode inventory acquisition failed");
    return status;
}
#endif

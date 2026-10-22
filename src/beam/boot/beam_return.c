/* SPDX-License-Identifier: Apache-2.0 */
/* Returning boot layered on explicit ordinary code and selected native units. */
#include "beam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ETF_VERSION 131
#define ETF_SMALL_INTEGER 97
#define ETF_INTEGER 98
#define ETF_ATOM 100
#define ETF_SMALL_TUPLE 104
#define ETF_NIL 106
#define ETF_STRING 107
#define ETF_LIST 108
#define ETF_BINARY 109
#define ETF_SMALL_ATOM 115
#define ETF_ATOM_UTF8 118
#define ETF_SMALL_ATOM_UTF8 119

static const char *const service_atoms[] = {
    "code", "file", "output", "program", "socket", "zlib"
};
static const unsigned service_bits[] = {
    BEAM_SERVICE_CODE, BEAM_SERVICE_FILE, BEAM_SERVICE_OUTPUT,
    BEAM_SERVICE_PROGRAM, BEAM_SERVICE_SOCKET, BEAM_SERVICE_ZLIB
};

struct beam_return_boot {
    unsigned services;
    int seed_origins;
    char *entry_module;
    char *entry_function;
    unsigned char *arguments;
    size_t argument_size;
    beam_composition *composition;
    const beam_module_spec **initial_modules;
    size_t initial_count;
    beam_vm *pending_vm;
    beam_output_handler output;
    void *output_context;
    beam_code_handler code;
    void *code_context;
    beam_input_handler input;
    void *input_context;
    beam_vm_options vm;
    int has_vm;
};

typedef struct byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

typedef struct etf_cursor {
    const unsigned char *cursor;
    const unsigned char *end;
} etf_cursor;

static void clear_error(beam_error *error)
{
    if (error)
        memset(error, 0, sizeof(*error));
}

static beam_status fail(beam_error *error, beam_status status, const char *message)
{
    if (error)
        snprintf(error->message, sizeof(error->message), "%s", message);
    return status;
}

static int buffer_reserve(byte_buffer *buffer, size_t extra)
{
    size_t needed;
    unsigned char *grown;
    if (extra > (size_t)-1 - buffer->size)
        return 0;
    needed = buffer->size + extra;
    if (needed <= buffer->capacity)
        return 1;
    if (buffer->capacity > (size_t)-1 / 2)
        return 0;
    while (buffer->capacity < needed) {
        size_t next = buffer->capacity ? buffer->capacity * 2 : 64;
        if (next < buffer->capacity)
            return 0;
        buffer->capacity = next;
    }
    grown = realloc(buffer->data, buffer->capacity);
    if (!grown)
        return 0;
    buffer->data = grown;
    return 1;
}

static int buffer_bytes(byte_buffer *buffer, const void *bytes, size_t size)
{
    if (!buffer_reserve(buffer, size))
        return 0;
    if (size)
        memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
    return 1;
}

static int buffer_u8(byte_buffer *buffer, unsigned value)
{
    unsigned char byte = (unsigned char)value;
    return buffer_bytes(buffer, &byte, 1);
}

static int buffer_u16(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[2] = {(unsigned char)(value >> 8), (unsigned char)value};
    return buffer_bytes(buffer, bytes, 2);
}

static int buffer_u32(byte_buffer *buffer, unsigned value)
{
    unsigned char bytes[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value
    };
    return buffer_bytes(buffer, bytes, 4);
}

static int buffer_atom(byte_buffer *buffer, const char *name)
{
    size_t length = strlen(name);
    if (length < 256)
        return buffer_u8(buffer, ETF_SMALL_ATOM_UTF8) && buffer_u8(buffer, (unsigned)length) &&
               buffer_bytes(buffer, name, length);
    if (length > 65535)
        return 0;
    return buffer_u8(buffer, ETF_ATOM_UTF8) && buffer_u16(buffer, (unsigned)length) &&
           buffer_bytes(buffer, name, length);
}

static int buffer_binary(byte_buffer *buffer, const unsigned char *bytes, size_t size)
{
    if (size > 0xffffffffu)
        return 0;
    return buffer_u8(buffer, ETF_BINARY) && buffer_u32(buffer, (unsigned)size) &&
           buffer_bytes(buffer, bytes, size);
}

static int etf_need(etf_cursor *cursor, size_t size)
{
    return (size_t)(cursor->end - cursor->cursor) >= size;
}

static int etf_u8(etf_cursor *cursor, unsigned *value)
{
    if (!etf_need(cursor, 1))
        return 0;
    *value = *cursor->cursor++;
    return 1;
}

static int etf_u16(etf_cursor *cursor, unsigned *value)
{
    if (!etf_need(cursor, 2))
        return 0;
    *value = ((unsigned)cursor->cursor[0] << 8) | cursor->cursor[1];
    cursor->cursor += 2;
    return 1;
}

static int etf_u32(etf_cursor *cursor, unsigned *value)
{
    if (!etf_need(cursor, 4))
        return 0;
    *value = ((unsigned)cursor->cursor[0] << 24) | ((unsigned)cursor->cursor[1] << 16) |
             ((unsigned)cursor->cursor[2] << 8) | cursor->cursor[3];
    cursor->cursor += 4;
    return 1;
}

static int etf_atom(etf_cursor *cursor, char *name, size_t capacity)
{
    unsigned tag, length;
    if (!etf_u8(cursor, &tag))
        return 0;
    if (tag == ETF_SMALL_ATOM_UTF8 || tag == ETF_SMALL_ATOM) {
        if (!etf_u8(cursor, &length))
            return 0;
    } else if (tag == ETF_ATOM_UTF8 || tag == ETF_ATOM) {
        if (!etf_u16(cursor, &length))
            return 0;
    } else {
        return 0;
    }
    if (!etf_need(cursor, length) || length >= capacity)
        return 0;
    memcpy(name, cursor->cursor, length);
    name[length] = '\0';
    cursor->cursor += length;
    return 1;
}

static int etf_integer(etf_cursor *cursor, int *value)
{
    unsigned tag, small;
    unsigned raw;
    if (!etf_u8(cursor, &tag))
        return 0;
    if (tag == ETF_SMALL_INTEGER) {
        if (!etf_u8(cursor, &small))
            return 0;
        *value = (int)small;
        return 1;
    }
    if (tag != ETF_INTEGER || !etf_u32(cursor, &raw))
        return 0;
    *value = (int)raw;
    return 1;
}

static int etf_bytes(etf_cursor *cursor, beam_bytes *bytes)
{
    unsigned tag, length;
    if (!etf_need(cursor, 1))
        return 0;
    tag = *cursor->cursor;
    if (tag == ETF_BINARY) {
        cursor->cursor++;
        if (!etf_u32(cursor, &length) || !etf_need(cursor, length))
            return 0;
        bytes->data = cursor->cursor;
        bytes->size = length;
        cursor->cursor += length;
        return 1;
    }
    if (tag != ETF_STRING)
        return 0;
    cursor->cursor++;
    if (!etf_u16(cursor, &length) || !etf_need(cursor, length))
        return 0;
    bytes->data = cursor->cursor;
    bytes->size = length;
    cursor->cursor += length;
    return 1;
}

static int etf_name_or_bytes(etf_cursor *cursor, char *storage, size_t capacity,
                             beam_bytes *bytes)
{
    if (etf_need(cursor, 1) &&
        (*cursor->cursor == ETF_BINARY || *cursor->cursor == ETF_STRING))
        return etf_bytes(cursor, bytes);
    if (!etf_atom(cursor, storage, capacity))
        return 0;
    bytes->data = (const unsigned char *)storage;
    bytes->size = strlen(storage);
    return 1;
}

static beam_status reply_bytes(beam_host_reply *reply, const unsigned char *data, size_t size)
{
    return beam_host_reply_copy(reply, (beam_bytes){data, size});
}

static beam_status reply_atom(beam_host_reply *reply, const char *name)
{
    byte_buffer buffer = {0};
    beam_status status = BEAM_OUT_OF_MEMORY;
    if (buffer_u8(&buffer, ETF_VERSION) && buffer_atom(&buffer, name))
        status = reply_bytes(reply, buffer.data, buffer.size);
    free(buffer.data);
    return status;
}

static beam_status reply_error_atom(beam_host_reply *reply, const char *kind)
{
    byte_buffer buffer = {0};
    beam_status status = BEAM_OUT_OF_MEMORY;
    if (buffer_u8(&buffer, ETF_VERSION) && buffer_u8(&buffer, ETF_SMALL_TUPLE) &&
        buffer_u8(&buffer, 2) && buffer_atom(&buffer, "error") && buffer_atom(&buffer, kind))
        status = reply_bytes(reply, buffer.data, buffer.size);
    free(buffer.data);
    return status;
}

static beam_status reply_ok_binary(beam_host_reply *reply, const unsigned char *data, size_t size)
{
    byte_buffer buffer = {0};
    beam_status status = BEAM_OUT_OF_MEMORY;
    if (buffer_u8(&buffer, ETF_VERSION) && buffer_u8(&buffer, ETF_SMALL_TUPLE) &&
        buffer_u8(&buffer, 2) && buffer_atom(&buffer, "ok") && buffer_binary(&buffer, data, size))
        status = reply_bytes(reply, buffer.data, buffer.size);
    free(buffer.data);
    return status;
}

static const char *output_kind(beam_output_status status)
{
    switch (status) {
    case BEAM_OUTPUT_UNKNOWN: return "not_found";
    case BEAM_OUTPUT_NOT_READY: return "unavailable";
    case BEAM_OUTPUT_BACKPRESSURE: return "busy";
    case BEAM_OUTPUT_FAILED: return "io";
    case BEAM_OUTPUT_OK: return NULL;
    }
    return "io";
}

static const char *code_kind(beam_code_status status)
{
    switch (status) {
    case BEAM_CODE_NOT_FOUND: return "not_found";
    case BEAM_CODE_INVALID: return "invalid_data";
    case BEAM_CODE_UNAVAILABLE: return "unavailable";
    case BEAM_CODE_OK: return NULL;
    }
    return "io";
}

static beam_status host_request(void *context, beam_bytes request, beam_host_reply *reply)
{
    struct beam_return_boot *boot = context;
    etf_cursor cursor;
    unsigned arity;
    char name[64];
    if (!request.data || request.size < 2 || request.data[0] != ETF_VERSION)
        return BEAM_INVALID_TERM;
    cursor.cursor = request.data + 1;
    cursor.end = request.data + request.size;
    if (*cursor.cursor != ETF_SMALL_TUPLE) {
        if (!etf_atom(&cursor, name, sizeof(name)))
            return BEAM_INVALID_TERM;
        if (strcmp(name, "read_line") != 0)
            return BEAM_INVALID_TERM;
        if (!boot->input)
            return reply_atom(reply, "eof");
        {
            const unsigned char *line = NULL;
            size_t size = 0;
            beam_input_status input = boot->input(boot->input_context, &line, &size);
            if (input == BEAM_INPUT_EOF)
                return reply_atom(reply, "eof");
            if (input == BEAM_INPUT_PENDING)
                return reply_atom(reply, "pending");
            if (input != BEAM_INPUT_LINE || (size && !line))
                return reply_error_atom(reply, "io");
            return reply_ok_binary(reply, line ? line : (const unsigned char *)"", size);
        }
    }
    cursor.cursor++;
    if (!etf_u8(&cursor, &arity) || !etf_atom(&cursor, name, sizeof(name)))
        return BEAM_INVALID_TERM;
    if (strcmp(name, "finish") == 0 && arity == 2) {
        int status;
        if (!etf_integer(&cursor, &status) || status < 0 || cursor.cursor != cursor.end)
            return BEAM_INVALID_TERM;
        const unsigned char finish[] = {ETF_VERSION, ETF_SMALL_ATOM_UTF8, 6, 'f', 'i', 'n', 'i', 's', 'h'};
        return beam_host_reply_complete(reply, (beam_bytes){finish, sizeof(finish)}, status);
    }
    if (strcmp(name, "output") == 0 && arity == 3) {
        char destination_name[256];
        beam_bytes destination, bytes;
        const char *kind;
        if (!etf_name_or_bytes(&cursor, destination_name, sizeof(destination_name),
                               &destination) ||
            !etf_bytes(&cursor, &bytes))
            return BEAM_INVALID_TERM;
        if (!boot->output)
            return reply_error_atom(reply, "unavailable");
        kind = output_kind(boot->output(boot->output_context, destination, bytes));
        return kind ? reply_error_atom(reply, kind) : reply_atom(reply, "ok");
    }
    if (strcmp(name, "code") == 0 && arity == 3) {
        char source_name[256], module_name[256];
        beam_bytes source, module;
        const unsigned char *bytes = NULL;
        size_t size = 0;
        const char *kind;
        beam_code_status code_status;
        if (!etf_name_or_bytes(&cursor, source_name, sizeof(source_name), &source) ||
            !etf_name_or_bytes(&cursor, module_name, sizeof(module_name), &module))
            return BEAM_INVALID_TERM;
        if (!boot->code)
            return reply_error_atom(reply, "unavailable");
        code_status = boot->code(boot->code_context, source, module, &bytes, &size);
        kind = code_kind(code_status);
        return kind ? reply_error_atom(reply, kind) :
               reply_ok_binary(reply, bytes ? bytes : (const unsigned char *)"", size);
    }
    return BEAM_INVALID_TERM;
}

static int copy_text(char **out, const char *text)
{
    size_t length;
    if (!text)
        return 1;
    length = strlen(text);
    *out = malloc(length + 1);
    if (!*out)
        return 0;
    memcpy(*out, text, length + 1);
    return 1;
}

static void free_boot(struct beam_return_boot *boot)
{
    if (!boot)
        return;
    free(boot->entry_module);
    free(boot->entry_function);
    free(boot->arguments);
    free(boot->initial_modules);
    free((void *)boot->vm.startup_context.data);
    free((void *)boot->vm.program_name);
    beam_composition_release(&boot->composition);
    free(boot);
}

static int known_services(unsigned services)
{
    unsigned known = BEAM_SERVICE_OUTPUT | BEAM_SERVICE_CODE | BEAM_SERVICE_ZLIB |
                     BEAM_SERVICE_FILE | BEAM_SERVICE_SOCKET | BEAM_SERVICE_PROGRAM;
    return (services & ~known) == 0;
}

beam_status beam_return_prepare(const beam_return_options *options,
                                beam_return_boot **out, beam_error *error)
{
    struct beam_return_boot *boot;
    size_t index;
    clear_error(error);
    if (!out || *out)
        return fail(error, BEAM_INVALID_ARGUMENT, "return boot output is invalid");
    if (!options || options->struct_size != sizeof(*options) || !known_services(options->services))
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid returning boot options");
    if (!options->entry_module || !*options->entry_module ||
        !options->entry_function || !*options->entry_function)
        return fail(error, BEAM_INVALID_ARGUMENT, "returning entry is required");
    if (options->arguments.size &&
        (!options->arguments.data || options->arguments.data[0] != ETF_VERSION))
        return fail(error, BEAM_INVALID_TERM, "returning arguments must be an ETF list");
    if (options->module_count && !options->modules)
        return fail(error, BEAM_INVALID_ARGUMENT, "returning modules are missing");
    if (options->vm && (options->vm->struct_size != sizeof(*options->vm) ||
                        options->vm->execution != BEAM_HOST_CONTROLLED || options->vm->initial_root ||
                        options->vm->composition || options->vm->process_policy ||
                        (options->vm->startup_context.size && !options->vm->startup_context.data)))
        return fail(error, BEAM_INVALID_ARGUMENT, "returning boot requires host-controlled VM options");
    boot = calloc(1, sizeof(*boot));
    if (!boot)
        return fail(error, BEAM_OUT_OF_MEMORY, "could not allocate returning boot");
    boot->services = options->services;
    boot->output = options->output;
    boot->output_context = options->output_context;
    boot->code = options->code;
    boot->code_context = options->code_context;
    boot->input = options->input;
    boot->input_context = options->input_context;
    if (options->vm) {
        boot->vm = *options->vm;
        boot->has_vm = 1;
        boot->vm.startup_context.data = NULL;
        boot->vm.program_name = NULL;
        if (options->vm->startup_context.size) {
            unsigned char *copy = malloc(options->vm->startup_context.size);
            if (!copy) {
                free_boot(boot);
                return fail(error, BEAM_OUT_OF_MEMORY, "could not copy startup context");
            }
            memcpy(copy, options->vm->startup_context.data, options->vm->startup_context.size);
            boot->vm.startup_context.data = copy;
        }
        if (options->vm->program_name) {
            boot->vm.program_name = strdup(options->vm->program_name);
            if (!boot->vm.program_name) {
                free_boot(boot);
                return fail(error, BEAM_OUT_OF_MEMORY, "could not copy diagnostic identity");
            }
        }
    }
    if (!copy_text(&boot->entry_module, options->entry_module) ||
        !copy_text(&boot->entry_function, options->entry_function)) {
        free_boot(boot);
        return fail(error, BEAM_OUT_OF_MEMORY, "could not copy returning boot");
    }
    if (options->arguments.size) {
        boot->arguments = malloc(options->arguments.size);
        if (!boot->arguments) {
            free_boot(boot);
            return fail(error, BEAM_OUT_OF_MEMORY, "could not copy returning arguments");
        }
        memcpy(boot->arguments, options->arguments.data, options->arguments.size);
        boot->argument_size = options->arguments.size;
    }
    {
        unsigned selected = 0;
        unsigned required =
            ((options->services & BEAM_SERVICE_FILE) ? 1u << BEAM_COMPONENT_FILE : 0) |
            ((options->services & BEAM_SERVICE_SOCKET) ? 1u << BEAM_COMPONENT_NETWORK : 0) |
            ((options->services & BEAM_SERVICE_ZLIB) ? 1u << BEAM_COMPONENT_ZLIB : 0);
        beam_composition_options composition = {.struct_size = sizeof(composition)};
        beam_module_spec *ordinary = NULL, *activation = NULL;
        char *program_helper = NULL;
        beam_status status;
        if ((options->component_count && !options->components) || options->component_count > 3 ||
            options->module_count > 4096) {
            free_boot(boot);
            return fail(error, BEAM_INVALID_ARGUMENT, "returning component selection is missing");
        }
        for (index = 0; index < options->component_count; ++index) {
            const beam_component_spec *spec = &options->components[index];
            int valid_kind = spec->kind == BEAM_COMPONENT_FILE ||
                spec->kind == BEAM_COMPONENT_NETWORK ||
                spec->kind == BEAM_COMPONENT_ZLIB;
            if (spec->struct_size != sizeof(*spec) || !valid_kind || (selected & (1u << spec->kind))) {
                free_boot(boot);
                return fail(error, BEAM_INVALID_ARGUMENT, "invalid or duplicate returning component");
            }
            selected |= 1u << spec->kind;
        }
        if (selected != required) {
            free_boot(boot);
            return fail(error, BEAM_INVALID_ARGUMENT, "runtime components do not match selected services");
        }
        ordinary = calloc(options->module_count ? options->module_count : 1, sizeof(*ordinary));
        activation = calloc(options->module_count ? options->module_count : 1, sizeof(*activation));
        if (!ordinary || !activation) { status = BEAM_OUT_OF_MEMORY; goto prepared; }
        for (index = 0; index < options->module_count; ++index) {
            const beam_return_module *module = &options->modules[index];
            beam_module_spec spec = {module->name, module->bytes, module->origin};
            if (module->service) activation[composition.activation_count++] = spec;
            else ordinary[composition.module_count++] = spec;
        }
        static const char *const required_modules[] = {
            "beam", "beam_list", "beam_map", "beam_bytes", "beam_text", "beam_format", "beam_time",
            "beam_return_boot", "beam_boot",
            "beam_component_services", "beam_host", "beam_output", "beam_code"
        };
        for (size_t i = 0; i < sizeof(required_modules) / sizeof(required_modules[0]); ++i) {
            int found = 0;
            for (size_t j = 0; j < composition.module_count; ++j)
                if (ordinary[j].name && !strcmp(ordinary[j].name, required_modules[i])) found = 1;
            if (!found) {
                char message[256];
                snprintf(message, sizeof(message), "required ordinary module is missing: %s", required_modules[i]);
                status = fail(error, BEAM_INVALID_ARGUMENT, message);
                goto prepared;
            }
        }
        if (options->services & BEAM_SERVICE_PROGRAM) {
            if (!options->bindir || options->bindir[0] != '/' || strlen(options->bindir) > 4096) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "program service requires an explicit helper directory");
                goto prepared;
            }
            size_t size = strlen(options->bindir) + sizeof("/erl_child_setup");
            program_helper = malloc(size);
            if (!program_helper) { status = BEAM_OUT_OF_MEMORY; goto prepared; }
            snprintf(program_helper, size, "%s/erl_child_setup", options->bindir);
        }
        composition.component_count = options->component_count;
        composition.components = options->components;
        composition.nif_count = options->nif_count;
        composition.nifs = options->nifs;
        composition.modules = ordinary;
        composition.defer_modules = 1;
        composition.activation_modules = activation;
        composition.services = options->services;
        composition.program_helper = program_helper;
        boot->seed_origins = !composition.services && !composition.activation_count;
        status = beam_composition_prepare(&composition, &boot->composition, error);
        if (status == BEAM_OK && composition.module_count) {
            const beam_module_spec *inventory;
            size_t count;
            status = beam_composition_get_modules(boot->composition, &inventory, &count);
            boot->initial_modules = calloc(composition.module_count, sizeof(*boot->initial_modules));
            if (!boot->initial_modules) status = BEAM_OUT_OF_MEMORY;
            for (size_t i = 0; status == BEAM_OK && i < composition.module_count; ++i) {
                for (size_t j = 0; j < count; ++j) {
                    if (!strcmp(ordinary[i].name, inventory[j].name)) {
                        boot->initial_modules[boot->initial_count++] = &inventory[j];
                        break;
                    }
                }
            }
        }
prepared:
        free(ordinary);
        free(activation);
        free(program_helper);
        if (status != BEAM_OK) {
            free_boot(boot);
            if (error && !error->message[0]) fail(error, status, beam_status_message(status));
            return status;
        }
    }
    *out = boot;
    return BEAM_OK;
}

beam_status beam_return_discard(beam_return_boot **boot)
{
    if (!boot)
        return BEAM_INVALID_ARGUMENT;
    if (*boot && (*boot)->pending_vm) {
        beam_status status = beam_vm_terminate(&(*boot)->pending_vm, NULL);
        if (status != BEAM_OK) return status;
    }
    free_boot(*boot);
    *boot = NULL;
    return BEAM_OK;
}

void beam_outcome_release(beam_outcome *outcome)
{
    if (!outcome)
        return;
    free(outcome->term.data);
    memset(outcome, 0, sizeof(*outcome));
}

static int encode_services(byte_buffer *buffer, unsigned services)
{
    unsigned count = 0, index;
    for (index = 0; index < sizeof(service_bits) / sizeof(service_bits[0]); ++index)
        if (services & service_bits[index])
            count++;
    if (!count)
        return buffer_u8(buffer, ETF_NIL);
    if (!buffer_u8(buffer, ETF_LIST) || !buffer_u32(buffer, count))
        return 0;
    for (index = 0; index < sizeof(service_bits) / sizeof(service_bits[0]); ++index)
        if ((services & service_bits[index]) && !buffer_atom(buffer, service_atoms[index]))
            return 0;
    return buffer_u8(buffer, ETF_NIL);
}

static int encode_call(struct beam_return_boot *boot, byte_buffer *buffer)
{
    if (!buffer_u8(buffer, ETF_VERSION) || !buffer_u8(buffer, ETF_LIST) ||
        !buffer_u32(buffer, 4) || !encode_services(buffer, boot->services) ||
        !buffer_u8(buffer, ETF_SMALL_TUPLE) || !buffer_u8(buffer, 2) ||
        !buffer_atom(buffer, boot->entry_module) || !buffer_atom(buffer, boot->entry_function)) return 0;
    if (boot->argument_size) {
        if (!buffer_bytes(buffer, boot->arguments + 1, boot->argument_size - 1)) return 0;
    } else if (!buffer_u8(buffer, ETF_NIL)) return 0;
    if (boot->seed_origins) {
        const beam_module_spec *modules;
        size_t count;
        if (beam_composition_get_modules(boot->composition, &modules, &count) != BEAM_OK ||
            !buffer_u8(buffer, ETF_LIST) || !buffer_u32(buffer, (unsigned)count)) return 0;
        for (size_t i = 0; i < count; ++i)
            if (!buffer_u8(buffer, ETF_SMALL_TUPLE) || !buffer_u8(buffer, 2) ||
                !buffer_atom(buffer, modules[i].name) ||
                !buffer_binary(buffer, (const unsigned char *)modules[i].origin, strlen(modules[i].origin))) return 0;
        if (!buffer_u8(buffer, ETF_NIL)) return 0;
    } else if (!buffer_atom(buffer, "undefined")) return 0;
    return buffer_u8(buffer, ETF_NIL);
}

static beam_status decode_outcome(beam_result *result, beam_outcome *out)
{
    out->halt_status = result->halt_status;
    if (result->kind == BEAM_HALT) { out->kind = BEAM_OUTCOME_HALT; return BEAM_OK; }
    if (result->kind == BEAM_EXCEPTION) {
        out->kind = BEAM_OUTCOME_EXCEPTION;
        out->term = result->value;
        result->value = (beam_buffer){0};
        return BEAM_OK;
    }
    if (result->kind == BEAM_COMPLETE) {
        const unsigned char finish[] = {ETF_VERSION, ETF_SMALL_ATOM_UTF8, 6, 'f', 'i', 'n', 'i', 's', 'h'};
        if (result->value.size != sizeof(finish) || memcmp(result->value.data, finish, sizeof(finish)))
            return BEAM_INVALID_TERM;
        out->kind = BEAM_OUTCOME_FINISH;
        return BEAM_OK;
    }
    if (result->kind != BEAM_RETURN) return BEAM_INVALID_TERM;
    out->kind = BEAM_OUTCOME_RETURN;
    out->term = result->value;
    result->value = (beam_buffer){0};
    return BEAM_OK;
}


beam_status beam_return_run(beam_system *system, beam_return_boot **boot_out,
                            beam_outcome *out, beam_error *error)
{
    beam_vm_options options;
    beam_vm *vm = NULL;
    beam_result result = {0};
    beam_vm_report report = {.struct_size = sizeof(report)};
    beam_outcome outcome = {.struct_size = sizeof(outcome)};
    byte_buffer call = {0};
    beam_status status, cleanup;
    int have_outcome = 0;
    clear_error(error);
    if (!boot_out || !*boot_out || !out || out->struct_size != sizeof(*out))
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid returning run");
    struct beam_return_boot *boot = *boot_out;
    if (boot->pending_vm) return fail(error, BEAM_INVALID_STATE, "discard the preparation to retry pending VM cleanup");
    if (boot->has_vm) options = boot->vm;
    else { beam_vm_options_init(&options); }
    options.composition = boot->composition;
    status = beam_vm_init(system, &options, &vm, error);
    if (status != BEAM_OK) return status;
    status = beam_vm_set_host_handler(vm, host_request, boot);
    for (size_t i = 0; status == BEAM_OK && i < boot->initial_count; ++i)
        status = beam_vm_load(vm, boot->initial_modules[i]->bytes);
    if (status == BEAM_OK) status = beam_vm_start(vm, error);
    if (status == BEAM_OK) status = beam_vm_activate(vm, error);
    if (status == BEAM_OK && !encode_call(boot, &call)) status = BEAM_OUT_OF_MEMORY;
    if (status == BEAM_OK) status = beam_vm_call(vm, "beam_return_boot", "start",
                                               (beam_bytes){call.data, call.size}, &result);
    if (status == BEAM_OK) {
        status = decode_outcome(&result, &outcome);
        have_outcome = status == BEAM_OK;
    }
    (void)beam_vm_get_report(vm, &report);
    if (((!have_outcome && (status == BEAM_SYSTEM_ERROR || status == BEAM_STOPPED)) ||
         (have_outcome && outcome.kind == BEAM_OUTCOME_HALT)) &&
        report.cause == BEAM_TERMINAL_COMPONENT_FAILURE) {
        outcome.kind = BEAM_OUTCOME_SERVICE_FAILURE;
        outcome.halt_status = report.halt_status;
        snprintf(outcome.service, sizeof(outcome.service), "%s", report.component);
        have_outcome = 1;
        status = BEAM_OK;
        clear_error(error);
    }
    cleanup = beam_vm_terminate_report(&vm, NULL, &report);
    if (report.has_cleanup_error) {
        outcome.has_cleanup_error = 1;
        snprintf(outcome.cleanup_error, sizeof(outcome.cleanup_error), "%s", report.cleanup.message);
    }
    if (cleanup != BEAM_OK) {
        boot->pending_vm = vm;
        outcome.has_cleanup_error = 1;
        if (!outcome.cleanup_error[0]) snprintf(outcome.cleanup_error, sizeof(outcome.cleanup_error), "%s", beam_status_message(cleanup));
        if (status == BEAM_OK) status = cleanup;
    } else {
        free_boot(boot);
        *boot_out = NULL;
    }
    if (have_outcome || outcome.has_cleanup_error) *out = outcome;
    else beam_outcome_release(&outcome);
    beam_result_release(&result);
    free(call.data);
    if (status != BEAM_OK && error && !error->message[0]) fail(error, status, beam_status_message(status));
    return status;
}

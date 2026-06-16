/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"
#include "bytecode_input.h"

beam_system *acceptance_system;

void fail(const char *message)
{
    fprintf(stderr, "returning acceptance: %s\n", message);
    exit(1);
}

void check(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static int buffer_reserve(byte_buffer *buffer, size_t extra)
{
    unsigned char *grown;
    if (extra > (size_t)-1 - buffer->size)
        return 0;
    if (buffer->size + extra <= buffer->capacity)
        return 1;
    if (!buffer->capacity)
        buffer->capacity = 64;
    while (buffer->capacity < buffer->size + extra) {
        if (buffer->capacity > (size_t)-1 / 2)
            return 0;
        buffer->capacity *= 2;
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
    return buffer_u8(buffer, ETF_SMALL_ATOM_UTF8) && buffer_u8(buffer, (unsigned)length) &&
           buffer_bytes(buffer, name, length);
}

static int buffer_binary(byte_buffer *buffer, const unsigned char *bytes, size_t size)
{
    return buffer_u8(buffer, ETF_BINARY) && buffer_u32(buffer, (unsigned)size) &&
           buffer_bytes(buffer, bytes, size);
}

beam_bytes list_of_binaries(const unsigned char *const *items, const size_t *sizes, size_t count)
{
    byte_buffer buffer = {0};
    size_t index;
    check(buffer_u8(&buffer, ETF_VERSION), "encode");
    if (!count)
        check(buffer_u8(&buffer, ETF_NIL), "encode");
    else {
        check(buffer_u8(&buffer, ETF_LIST) && buffer_u32(&buffer, (unsigned)count), "encode");
        for (index = 0; index < count; ++index)
            check(buffer_binary(&buffer, items[index], sizes[index]), "encode");
        check(buffer_u8(&buffer, ETF_NIL), "encode");
    }
    return (beam_bytes){buffer.data, buffer.size};
}

unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    unsigned char *data;
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

beam_output_status on_output(void *context, beam_bytes destination, beam_bytes bytes)
{
    sink *output = context;
    char name[64];
    if (!destination.size || destination.size >= sizeof(name))
        return BEAM_OUTPUT_UNKNOWN;
    memcpy(name, destination.data, destination.size);
    name[destination.size] = '\0';
    if (strcmp(name, "missing_destination") == 0)
        return BEAM_OUTPUT_UNKNOWN;
    if (strcmp(name, "standard_output") != 0 && strcmp(name, "standard_error") != 0)
        return BEAM_OUTPUT_UNKNOWN;
    if (bytes.size == 4 && memcmp(bytes.data, "wait", 4) == 0)
        return BEAM_OUTPUT_NOT_READY;
    if (bytes.size == 4 && memcmp(bytes.data, "full", 4) == 0)
        return BEAM_OUTPUT_BACKPRESSURE;
    if (bytes.size == 4 && memcmp(bytes.data, "fail", 4) == 0)
        return BEAM_OUTPUT_FAILED;
    if (!buffer_reserve((byte_buffer *)output, bytes.size))
        return BEAM_OUTPUT_FAILED;
    if (bytes.size)
        memcpy(output->data + output->size, bytes.data, bytes.size);
    output->size += bytes.size;
    return BEAM_OUTPUT_OK;
}

beam_code_status on_code(void *context, beam_bytes source, beam_bytes module,
                                const unsigned char **bytes, size_t *size)
{
    sink *output = context;
    (void)source;
    if (module.size == strlen("uppercase_transform") &&
        memcmp(module.data, "uppercase_transform", module.size) == 0) {
        *bytes = output->code;
        *size = output->code_size;
        return output->code ? BEAM_CODE_OK : BEAM_CODE_NOT_FOUND;
    }
    if (module.size == strlen("not_a_module") && memcmp(module.data, "not_a_module", module.size) == 0) {
        *bytes = output->bad;
        *size = output->bad_size;
        return BEAM_CODE_OK;
    }
    return BEAM_CODE_NOT_FOUND;
}

beam_return_module module_from_file(const char *path, const char *name, int service)
{
    beam_return_module module = {0};
    module.name = name;
    module.bytes.data = read_file(path, &module.bytes.size);
    module.service = service;
    check(module.bytes.data != NULL, path);
    return module;
}

void start_system(void)
{
    beam_error error = {0};
    check(beam_init(&acceptance_system, &error) == BEAM_OK, error.message);
}

void stop_system(void)
{
    if (acceptance_system)
        check(beam_terminate(&acceptance_system) == BEAM_OK, "terminate");
}

static int cmp_fd(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

void snapshot_fds(int *fds, int capacity, int *count)
{
#ifdef __APPLE__
    *count=beam_test_host_copy_fds(fds,(size_t)capacity);
    check(*count>=0,"process descriptors");
#else
    DIR *directory = opendir("/proc/self/fd");
    struct dirent *entry;
    int self;
    check(directory != NULL, "process descriptors");
    self = dirfd(directory);
    *count = 0;
    while ((entry = readdir(directory)) != NULL) {
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (!end || *end || value < 0 || value == self)
            continue;
        if (*count < capacity)
            fds[(*count)++] = (int)value;
    }
    closedir(directory);
    qsort(fds, (size_t)*count, sizeof(int), cmp_fd);
#endif
}

static int fd_sets_equal(const int *before, int before_count, const int *after, int after_count)
{
    int index;
    if (before_count != after_count)
        return 0;
    for (index = 0; index < before_count; ++index)
        if (before[index] != after[index])
            return 0;
    return 1;
}

void check_fds(const int *before, int before_count)
{
    int after[1024];
    int after_count = 0;
    int delta;
    snapshot_fds(after, 1024, &after_count);
    delta = after_count - before_count;
    /* /proc/self/fd, not beam_file_descriptor_count or beam_socket_count. */
    printf("proc-fd-before %d proc-fd-after %d proc-fd-delta %d\n",
           before_count, after_count, delta);
    if (!fd_sets_equal(before, before_count, after, after_count)) {
        fprintf(stderr, "process descriptor set changed by %d\n", delta);
        fail("descriptor leak");
    }
}

void run_entry(const char *root, const char *function, beam_bytes arguments,
                      unsigned services, beam_return_module *modules, size_t count,
                      sink *output, beam_outcome *outcome)
{
    beam_return_boot *boot = NULL;
    beam_return_options options = {0};
    beam_error error = {0};
    beam_status status;
    int descriptors[1024];
    int descriptor_count = 0;
    snapshot_fds(descriptors, 1024, &descriptor_count);
    outcome->struct_size = sizeof(*outcome);
    options.struct_size = sizeof(options);
    options.services = services;
    beam_vm_options parallel;
    unsigned char startup[] = {131,97,42};
    char identity[] = "copied-host";
    if (strstr(function, "then_halt")) {
        beam_vm_options_init(&parallel);

        parallel.schedulers = parallel.schedulers_online = 4;
        options.vm = &parallel;
    }
    options.entry_module = "returning_programs";
    options.entry_function = function;
    if (!strcmp(function, "ret")) {
        beam_vm_options_init(&parallel);

        parallel.startup_context = (beam_bytes){startup, sizeof(startup)};
        parallel.program_name = identity;
        options.vm = &parallel;
        options.entry_function = "ret_context";
    }
    options.arguments = arguments;
    bytecode_input base = {0};
    check(bytecode_input_read(&base, root, INPUT_RETURNING, &error) == BEAM_OK, "read verified base inventory");
    beam_return_module *selected = calloc(count + base.count, sizeof(*selected));
    check(selected != NULL, "allocate selected modules");
    for (size_t i = 0; i < base.count; ++i) {
        selected[i].name = base.modules[i].name;
        selected[i].bytes = base.modules[i].bytes;
        selected[i].origin = base.modules[i].origin;
    }
    memcpy(selected + base.count, modules, count * sizeof(*modules));
    options.module_count = count + base.count;
    options.modules = selected;
    options.output = on_output;
    options.output_context = output;
    options.code = on_code;
    options.code_context = output;
    beam_component_spec components[5] = {{0}};
    char paths[5][4096], bindir[4096];
    const char *names[] = {"file", "network", "zlib"};
    const beam_component_kind kinds[] = {BEAM_COMPONENT_FILE, BEAM_COMPONENT_NETWORK, BEAM_COMPONENT_ZLIB};
    const unsigned bits[] = {BEAM_SERVICE_FILE, BEAM_SERVICE_SOCKET, BEAM_SERVICE_ZLIB};
    for (size_t i = 0; i < 3; ++i) {
        if (bits[i] && !(services & bits[i])) continue;
        size_t n = options.component_count++;
        check(snprintf(paths[n], sizeof paths[n], "%s/%s", root, beam_test_component(names[i])) < (int)sizeof paths[n], "component path");
        components[n] = (beam_component_spec){sizeof(beam_component_spec), kinds[i], paths[n]};
    }
    options.components = components;
    check(snprintf(bindir, sizeof bindir, "%s/libexec/beam", root) < (int)sizeof bindir, "helper path");
    options.bindir = bindir;
    status = beam_return_prepare(&options, &boot, &error);
    bytecode_input_release(&base);
    free(selected);
    if (status != BEAM_OK) {
        fprintf(stderr, "prepare: %s\n", error.message);
        fail("prepare failed");
    }
    memset(startup, 0, sizeof(startup));
    memset(identity, 0, sizeof(identity));
    status = beam_return_run(acceptance_system, &boot, outcome, &error);
    if (status != BEAM_OK) {
        fprintf(stderr, "run %s: %s\n", function, error.message);
        fail("run failed");
    }
    check_fds(descriptors, descriptor_count);
}

etf_reader read_term(beam_buffer term, unsigned tuple_arity)
{
    etf_reader reader;
    check(etf_start(&reader, term.data, term.size), "ETF version");
    if (tuple_arity) check(etf_tuple(&reader, tuple_arity), "ETF tuple arity");
    return reader;
}

void expect_one_binary(const beam_outcome *outcome, const char *label, const char *expected)
{
    size_t text_size = 0;
    etf_reader cursor = read_term(outcome->term, 0);
    const unsigned char *text = NULL;
    check(outcome->kind == BEAM_OUTCOME_RETURN, label);
    check(etf_binary(&cursor, &text, &text_size), label);
    check(text_size == strlen(expected) && memcmp(text, expected, text_size) == 0, label);
    check(etf_done(&cursor), "binary trailing bytes");
    printf("%s %.*s\n", label, (int)text_size, text);
}

void expect_return_text(const beam_outcome *outcome, const char *text)
{
    const unsigned char *bytes;
    size_t size;
    check(outcome->kind == BEAM_OUTCOME_RETURN, "binary return kind");
    etf_reader reader = read_term(outcome->term, 0);
    check(etf_binary(&reader, &bytes, &size) && size == strlen(text) &&
          memcmp(bytes, text, size) == 0 && etf_done(&reader), text);
}

void add_program(beam_return_module *modules, size_t *count, const char *fixture)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/returning_programs.beam", fixture);
    modules[(*count)++] = module_from_file(path, "returning_programs", 0);
}

void expect_exception_tag(const beam_outcome *outcome, const char *tag, const char *detail)
{
    check(outcome->kind == BEAM_OUTCOME_EXCEPTION, "expected application exception");
    check(!outcome->has_cleanup_error, "exception cleanup");
    etf_reader reader = read_term(outcome->term, 3);
    check(etf_atom(&reader, "error"), "exception class");
    if (detail) check(etf_tuple(&reader, 2), "exception reason tuple");
    check(etf_atom(&reader, tag), "exception reason tag");
    if (detail) check(etf_atom(&reader, detail), "exception reason detail");
    check(etf_stacktrace(&reader), "exception stacktrace shape");
    check(etf_done(&reader), "exception trailing bytes");
}

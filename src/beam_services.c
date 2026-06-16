/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <stdatomic.h>

static int components_enabled;
static atomic_int components_stopping;
static unsigned component_services;
static const char *program_path;
static atomic_int zlib_streams;
static atomic_int file_descriptors;
static atomic_int socket_count;

static void adjust(atomic_int *counter, int delta)
{
    atomic_fetch_add_explicit(counter, delta, memory_order_relaxed);
}

static int load_counter(atomic_int *counter)
{
    return atomic_load_explicit(counter, memory_order_relaxed);
}

void erts_components_begin(unsigned services, const char *helper)
{
    components_enabled = 1;
    atomic_store_explicit(&components_stopping, 0, memory_order_release);
    program_path = helper;
    component_services = services;
}

void erts_components_end(void)
{
    components_enabled = 0;
    program_path = NULL;
    component_services = 0;
}

int erts_components_active(void)
{
    return components_enabled;
}

int erts_components_program_enabled(void)
{
    return components_enabled && (component_services & BEAM_SERVICE_PROGRAM) != 0;
}

int erts_components_has_service(unsigned service)
{
    return components_enabled && (component_services & service) != 0;
}

const char *erts_components_program_path(void)
{
    return components_enabled ? program_path : NULL;
}

void erts_components_zlib_adjust(int delta)
{
    adjust(&zlib_streams, delta);
}

void erts_components_file_adjust(int delta)
{
    adjust(&file_descriptors, delta);
}

void erts_components_socket_adjust(int delta)
{
    adjust(&socket_count, delta);
}

int beam_zlib_stream_count(void)
{
    return load_counter(&zlib_streams);
}

int beam_file_descriptor_count(void)
{
    return load_counter(&file_descriptors);
}

int beam_socket_count(void)
{
    return load_counter(&socket_count);
}


void erts_components_request_stop(void)
{
    atomic_store_explicit(&components_stopping, 1, memory_order_release);
}
int erts_components_stopping(void)
{
    return atomic_load_explicit(&components_stopping, memory_order_acquire);
}

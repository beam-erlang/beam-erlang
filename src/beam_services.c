/* SPDX-License-Identifier: Apache-2.0 */
#include "beam.h"

static int components_enabled;
static int components_stopping;
static unsigned component_services;
static const char *program_path;
static int zlib_streams;
static int file_descriptors;
static int socket_count;

static void adjust(int *counter, int delta)
{
    __atomic_fetch_add(counter, delta, __ATOMIC_RELAXED);
}

static int load_counter(int *counter)
{
    return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

void erts_components_begin(unsigned services, const char *helper)
{
    components_enabled = 1;
    __atomic_store_n(&components_stopping, 0, __ATOMIC_RELEASE);
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
    return components_enabled && program_path != NULL;
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
    __atomic_store_n(&components_stopping, 1, __ATOMIC_RELEASE);
}
int erts_components_stopping(void)
{
    return __atomic_load_n(&components_stopping, __ATOMIC_ACQUIRE);
}

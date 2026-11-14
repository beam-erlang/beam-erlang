/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_driver.h>

static ErlDrvData start(ErlDrvPort port, char *command)
{
    (void)command;
    return (ErlDrvData)port;
}

static void output(ErlDrvData handle, char *buffer, ErlDrvSizeT length)
{
    driver_output((ErlDrvPort)handle, buffer, length);
}

static ErlDrvEntry entry = {
    .start = start,
    .output = output,
    .driver_name = "beam_sdk_driver",
    .extended_marker = ERL_DRV_EXTENDED_MARKER,
    .major_version = ERL_DRV_EXTENDED_MAJOR_VERSION,
    .minor_version = ERL_DRV_EXTENDED_MINOR_VERSION,
    .driver_flags = ERL_DRV_FLAG_USE_PORT_LOCKING
};

DRIVER_INIT(beam_sdk_driver)
{
    return &entry;
}

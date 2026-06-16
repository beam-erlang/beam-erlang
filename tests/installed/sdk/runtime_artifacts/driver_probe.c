/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_driver.h>

static ErlDrvData start(ErlDrvPort port, char *command)
{
    (void)command;
    return (ErlDrvData)port;
}
static void output(ErlDrvData data, char *bytes, ErlDrvSizeT size)
{
    driver_output((ErlDrvPort)data, bytes, size);
}
static ErlDrvEntry entry = {
    .start = start,
    .output = output,
    .driver_name = "sdk_driver_probe",
    .extended_marker = ERL_DRV_EXTENDED_MARKER,
    .major_version = ERL_DRV_EXTENDED_MAJOR_VERSION,
    .minor_version = ERL_DRV_EXTENDED_MINOR_VERSION
};
DRIVER_INIT(sdk_driver_probe)
{
    return &entry;
}

/* Exercise the public data symbol as well as ordinary driver function imports. */
ErlDrvTermData sdk_probe_nil(void) { return driver_term_nil; }

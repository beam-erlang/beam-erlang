%% SPDX-License-Identifier: Apache-2.0
-module(driver_probe).
-export([main/1]).
main([Directory]) ->
    ok = erl_ddll:load_driver(Directory, "sdk_driver_probe"),
    Port = open_port({spawn_driver, "sdk_driver_probe"}, [binary]),
    Data = <<0, 1, 127, 255, "public driver">>,
    true = port_command(Port, Data),
    receive {Port, {data, Data}} -> ok after 5000 -> error(driver_timeout) end,
    true = port_close(Port),
    ok = erl_ddll:unload_driver("sdk_driver_probe").

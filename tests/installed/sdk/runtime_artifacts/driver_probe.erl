%% SPDX-License-Identifier: Apache-2.0
-module(driver_probe).
-export([main/1, start/1]).

start(Directory) ->
    ok = main([beam_unicode:characters_to_list(Directory)]),
    beam:halt(0, []).

main([Directory]) ->
    ok = base_apis(),
    {error,{already_started,undefined}} = beam_ddll:start(),
    ok = beam_ddll:stop(),
    ok = beam_ddll:load_driver(Directory, "sdk_driver_probe"),
    {ok, Drivers} = beam_ddll:loaded_drivers(),
    true = beam_lists:member("sdk_driver_probe", Drivers),
    true = beam_lists:keymember("sdk_driver_probe", 1, beam_ddll:info()),
    Port = open_port({spawn_driver, "sdk_driver_probe"}, [binary]),
    Data = <<0, 1, 127, 255, "public driver">>,
    true = port_command(Port, Data),
    receive {Port, {data, Data}} -> ok after 5000 -> error(driver_timeout) end,
    true = port_close(Port),
    ok = beam_ddll:unload_driver("sdk_driver_probe").

base_apis() ->
    false = beam:module_loaded(math),
    false = beam:is_builtin(math, sqrt, 1),
    false = beam:is_builtin(erl_ddll, try_load, 3),
    false = beam:is_builtin(erl_debugger, supported, 0),
    true = beam:is_builtin(beam_math, sqrt, 1),
    true = beam:is_builtin(beam_ddll, try_load, 3),
    true = beam:is_builtin(beam_debugger, supported, 0),
    3.0 = beam_math:sqrt(9),
    1.0 = beam_math:cos(0),
    8.0 = beam_math:pow(2, 3),
    -2.0 = beam_math:floor(-1.5),
    -1.0 = beam_math:ceil(-1.5),
    Pi = beam_math:pi(),
    true = Pi > 3.14 andalso Pi < 3.15,
    true = beam_math:tau() =:= 2 * Pi,
    badarith = try beam_math:sqrt(-1) catch error:badarith -> badarith end,
    badarg = try beam_math:sqrt(invalid) catch error:badarg -> badarg end,
    false = beam_debugger:supported(),
    Allowed = [{module_info,0}, {module_info,1}, {supported,0}],
    [undef = try apply(beam_debugger, Function, beam_lists:duplicate(Arity, dummy))
             catch error:undef -> undef end
     || {Function, Arity} <- beam_debugger:module_info(exports),
        not beam_lists:member({Function, Arity}, Allowed)],
    ok.

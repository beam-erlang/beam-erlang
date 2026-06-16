%% SPDX-License-Identifier: Apache-2.0
-module(pkg_acceptance).
-export([load/1,value/0,orphan/1]).
load(Path) -> erlang:load_nif(Path,0).
value() -> erlang:nif_error(unloaded).

orphan(Python) ->
    Program = "import subprocess,sys; p=subprocess.Popen([sys.executable,'-c',"
              "'import time;time.sleep(60)'],stdin=subprocess.DEVNULL,"
              "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,"
              "creationflags=subprocess.DETACHED_PROCESS);sys.stdout.write(str(p.pid))",
    Port = open_port({spawn_executable,Python},[binary,exit_status,{args,["-c",Program]}]),
    collect(Port,<<>>).
collect(Port,Output) ->
    receive
        {Port,{data,Bytes}} -> collect(Port,<<Output/binary,Bytes/binary>>);
        {Port,{exit_status,0}} -> binary_to_integer(Output)
    end.

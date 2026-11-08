%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 2024-2025. All Rights Reserved.
%%
%% Licensed under the Apache License, Version 2.0 (the "License");
%% you may not use this file except in compliance with the License.
%% You may obtain a copy of the License at
%%
%%     http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing, software
%% distributed under the License is distributed on an "AS IS" BASIS,
%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%% See the License for the specific language governing permissions and
%% limitations under the License.
%%
%% %CopyrightEnd%
%%
-module(beam_internal).
-moduledoc false.
-compile({beam_native_bindings,
          [{vm_erts_trace_cleaner_check_0,beam_internal,trace_cleaner_check,0},
           {vm_erts_trace_cleaner_send_trace_clean_signal_1,beam_internal,trace_cleaner_send_signal,1}]}).


-export([trace_cleaner_start/0,
         trace_cleaner_check/0,
         trace_cleaner_send_signal/1,
         module_soft_purge/1]).

%% Only the VM-created management requester can reserve this protocol. The
%% native gate owns identities and completion; mailbox wakeups carry no native
%% pointers and cannot fabricate a successful purge result.
module_soft_purge(Mod) ->
    case erts_internal:purge_module(Mod, beam_host_admit) of
        true ->
            erts_code_purger ! beam_host_purge,
            module_soft_purge_wait(Mod);
        Status ->
            Status
    end.

module_soft_purge_wait(Mod) ->
    receive
        beam_host_purge ->
            case erts_internal:purge_module(Mod, beam_host_result) of
                undefined -> module_soft_purge_wait(Mod);
                Result -> Result
            end
    end.

%%
%% The erts_trace_cleaner is started at
%% VM boot by the VM. It is a spawned as a system
%% process, i.e, the whole VM will terminate if
%% this process terminates.
%%
trace_cleaner_start() ->
    process_flag(trap_exit, true),
    loop(passive, undefined, 0, []).

%%
%% The VM will send us a clean message
%% when it has a new trace session that needs to
%% be cleaned from all processes and ports.
%%
loop(passive, _, 0, []) ->
    %% Wait for things to do
    receive
	notify ->
            call_check();

        _Garbage ->
            erlang:display({?MODULE, unexpected, _Garbage}),
	    loop(passive, undefined, 0, [])

    after 60_000 ->
            %% We hibernate in order to clear the heap completely.
            erlang:hibernate(?MODULE, trace_cleaner_start, [])
    end;
loop(processes, OReqLim, 0, []) ->
    %% Done with processes, continue with ports
    loop(ports, OReqLim, 0, erlang:ports());
loop(ports, _, 0, []) ->
    %% All done
    call_check();
loop(State, OReqLim, OReq, [P|Rest]) when OReq < OReqLim ->
    %% Send another request
    Sent = send_clean_req(P),
    loop(State, OReqLim, OReq+Sent, Rest);
loop(State, OReqLim, OReq, Pids) ->
    %% Wait for acks
    receive
	notify -> % ignore
            loop(State, OReqLim, OReq, Pids);

        Ack when is_pid(Ack), OReq > 0 ->
	    loop(State, OReqLim, OReq-1, Pids);

        _Garbage ->
            erlang:display({?MODULE, unexpected, _Garbage}),
	    loop(State, OReqLim, OReq, Pids)
    end.


call_check() ->
    %% Try pick new sessions from wait list
    case beam_internal:trace_cleaner_check() of
        true ->
            Pids = erlang:processes(),
            OReqLim = erlang:system_info(outstanding_system_requests_limit),
            loop(processes, OReqLim, 0, Pids);

        false ->
            loop(passive, undefined, 0, [])
    end.


send_clean_req(P) ->
    case beam_internal:trace_cleaner_send_signal(P) of
        true -> 1; % signal sent
        false -> 0 % no signal sent (process already dead)
    end.

-spec trace_cleaner_check() -> boolean().

trace_cleaner_check() ->
    erlang:nif_error(undef). %% Implemented in erl_bif_trace.c

-spec trace_cleaner_send_signal(To) -> boolean() when
      To :: pid() | port().

trace_cleaner_send_signal(_To) ->
    erlang:nif_error(undef). %% Implemented in erl_bif_trace.c


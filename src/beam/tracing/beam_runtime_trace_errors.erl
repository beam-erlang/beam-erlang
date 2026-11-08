%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 2020-2025. All Rights Reserved.
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

-module(beam_runtime_trace_errors).
-moduledoc false.
-export([format_error/2]).

format_error(_Reason, [{M,F,As,Info}|_]) ->
    ErrorInfoMap = error_info(Info),
    Cause = case ErrorInfoMap of #{cause := Value} -> Value; #{} -> none end,
    Res = case M of
              beam_trace -> format_trace_error(F, As, Cause);
              _ -> []
          end,
    format_error_map(Res, 1, #{}).

error_info([{error_info, Value}|_]) -> Value;
error_info([error_info|_]) -> true;
error_info([_|Rest]) -> error_info(Rest);
error_info([]) -> #{}.

format_trace_error(process, [_Session,Proc,How,Options], Cause) ->
    PidError = case Proc of
                   _ when is_pid(Proc), node(Proc) =/= node() ->
                       not_local_pid;
                   _ ->
                       []
               end,
    HowError = must_be_boolean(How),
    case Cause of
        session ->
            [bad_session];

        badopt ->
            [[], PidError, HowError, must_be_option_list(Options)];
        _ ->
            case {HowError, PidError} of
                {[], []} ->
                    [[], <<"invalid process spec">>];
                _ ->
                    [[], PidError, HowError, []]
            end
    end;
format_trace_error(port, [_Session,Port,How,Options], Cause) ->
    PrtError = case Port of
                   _ when is_port(Port), node(Port) =/= node() ->
                       not_local_port;
                   _ ->
                       []
                end,
    HowError = must_be_boolean(How),
    case Cause of
        session ->
            [bad_session];

        badopt ->
            [[], PrtError, HowError, must_be_option_list(Options)];
        _ ->
            case {HowError, PrtError} of
                {[], []} ->
                    [[], <<"invalid port spec">>];
                _ ->
                    [[], PrtError, HowError, []]
            end
    end;
format_trace_error(function, [_Session,_MFA,_MatchSpec,Options], Cause) ->
    case Cause of
        session ->
            [bad_session];
        badopt ->
            [[], [], [], must_be_option_list(Options)];
        match_spec ->
            [[], [], bad_match_spec, must_be_list(Options)];
        call_count ->
            [[], [], [], <<"a match spec is not allowed in combination with these options">>];
        _ ->
            [[], <<"invalid MFA specification">>, [], []]
    end;
format_trace_error(SendRecv, [_Session,_MatchSpec,Options], Cause)
  when SendRecv =:= send; SendRecv =:= recv ->
    case Cause of
        session ->
            [bad_session];
        badopt ->
            [[], [], must_be_option_list(Options)];
        match_spec ->
            [[], bad_match_spec, must_be_list(Options)];
        _ ->
            []
    end;
format_trace_error(info, [_Session,Tracee,_Item], Cause) ->
    case Cause of
        session ->
            [bad_session];
        badopt ->
            if
                is_pid(Tracee), node(Tracee) =/= node() ->
                    [[], not_local_pid];
                is_port(Tracee), node(Tracee) =/= node() ->
                    [[], not_local_port];
                true ->
                    [[], <<"not a valid tracee specification">>]
            end;
        none ->
            [[], [], <<"invalid trace item">>]
    end;
format_trace_error(session_create, [Name,Tracer,Options], _) ->
    NameError = if
                    is_atom(Name) -> [];
                    true -> not_atom
                end,
    TracerError = case Tracer of
                      _ when is_pid(Tracer), node(Tracer) =:= node() -> [];
                      _ when is_port(Tracer), node(Tracer) =:= node() -> [];
                      {Mod,_} when is_atom(Mod) -> [];
                      _ -> bad_tracer
                  end,
    OptError = case Options of
                   [] -> [];
                   [_|_] -> bad_option;
                   _ -> not_list
               end,
    [NameError, TracerError, OptError];
format_trace_error(session_destroy, [_Session], _) ->
    [bad_session];
format_trace_error(session_info, [_PidPortFuncEvent], _) ->
    [<<"not a valid tracee specification">>];
format_trace_error(_, _, _) ->
    [].


must_be_boolean(B) when is_boolean(B) -> [];
must_be_boolean(_) -> bad_boolean.

must_be_list(List) ->
    must_be_list(List, []).

must_be_list(List, Error) when is_list(List) ->
    case proper_list(List) of
        true -> Error;
        false -> not_proper_list
    end;
must_be_list(_, _) ->
    not_list.

must_be_option_list(Options) ->
    case must_be_list(Options) of
        [] -> bad_option;
        Error -> Error
    end.

format_error_map([""|Es], ArgNum, Map) ->
    format_error_map(Es, ArgNum + 1, Map);
format_error_map([{general, E}|Es], ArgNum, Map) ->
    format_error_map(Es, ArgNum, Map#{ general => expand_error(E)});
format_error_map([E|Es], ArgNum, Map) ->
    format_error_map(Es, ArgNum + 1, Map#{ArgNum => expand_error(E)});
format_error_map([], _, Map) ->
    Map.

proper_list([]) -> true;
proper_list([_|Tail]) -> proper_list(Tail);
proper_list(_) -> false.

expand_error(bad_boolean) ->
    <<"not a boolean ('true' or 'false')">>;
expand_error(bad_match_spec) ->
    <<"invalid match specification">>;
expand_error(bad_option) ->
    <<"invalid option in list">>;
expand_error(bad_session) ->
    <<"invalid trace session">>;
expand_error(bad_tracer) ->
    <<"invalid tracer">>;
expand_error(not_atom) ->
    <<"not an atom">>;
expand_error(not_list) ->
    <<"not a list">>;
expand_error(not_local_pid) ->
    <<"not a local pid">>;
expand_error(not_local_port) ->
    <<"not a local port">>;
expand_error(not_proper_list) ->
    <<"not a proper list">>;
expand_error(Other) ->
    Other.

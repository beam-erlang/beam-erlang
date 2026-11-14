%% Observation-only Common Test event journal for the full emulator corpus.
-module(beam_emulator_events).
-behaviour(gen_event).
-export([init/1, handle_event/2, handle_call/2, handle_info/2,
         terminate/2, code_change/3, timezone_facts/0]).
-include_lib("common_test/include/ct_event.hrl").

init([LogDir]) ->
    Path = filename:join(LogDir, "events.jsonl"),
    ok = filelib:ensure_dir(Path),
    case file:open(Path, [write, raw, binary, exclusive]) of
        {ok, Device} ->
            State = #{device => Device, sequence => 0, path => Path},
            {ok, write_event(#{<<"event">> => <<"handler_started">>,
                               <<"node">> => text(node())}, State)};
        Error -> {error, {cannot_open_emulator_event_journal, Path, Error}}
    end;
init(Args) ->
    {error, {invalid_emulator_event_arguments, Args}}.

handle_event(Event = #event{name = Name, node = Node, data = Data}, State) ->
    Base = #{<<"event">> => text(Name), <<"node">> => text(Node),
             <<"raw">> => representation(Event)},
    Normalized = normalize(Name, Data),
    Record = maps:merge(maps:merge(Base, Normalized), resource_fields(Normalized)),
    {ok, write_event(Record, State)};
handle_event(Event, State) ->
    {ok, write_event(#{<<"event">> => <<"unrecognized_event">>,
                       <<"raw">> => representation(Event)}, State)}.

handle_call(snapshot, State) ->
    ok = file:sync(maps:get(device, State)),
    {ok, #{sequence => maps:get(sequence, State), path => maps:get(path, State)}, State};
handle_call(Request, State) ->
    {ok, {error, {unsupported_request, Request}}, State}.

handle_info(Info, State) ->
    {ok, write_event(#{<<"event">> => <<"handler_info">>,
                       <<"raw">> => representation(Info)}, State)}.

terminate(Reason, State) ->
    Final = write_event(#{<<"event">> => <<"handler_stopped">>,
                          <<"reasontext">> => reason_text(Reason),
                          <<"reason_term">> => representation(Reason)}, State),
    ok = file:sync(maps:get(device, Final)),
    ok = file:close(maps:get(device, Final)),
    ok.

code_change(_OldVersion, State, _Extra) -> {ok, State}.

normalize(tc_start, {Suite, Function}) ->
    function_fields(Suite, Function);
normalize(tc_done, {Suite, Function, Result}) ->
    maps:merge(function_fields(Suite, Function), outcome_fields(Result));
normalize(Name, {Suite, Function, Reason})
  when Name =:= tc_user_skip; Name =:= tc_auto_skip ->
    Kind = case Name of tc_user_skip -> <<"user">>; tc_auto_skip -> <<"auto">> end,
    maps:merge(function_fields(Suite, Function),
               #{<<"outcome">> => case Kind of <<"user">> -> <<"skipped">>;
                                                   _ -> <<"auto_skipped">> end,
                 <<"skipkind">> => Kind, <<"reasontext">> => reason_text(Reason),
                 <<"reason_term">> => representation(Reason)});
normalize(tc_logfile, {{Suite, Function}, Logfile}) ->
    (function_fields(Suite, Function))#{<<"logfile">> => text(Logfile)};
normalize(test_stats, {Passed, Failed, {User, Auto}})
  when is_integer(Passed), is_integer(Failed), is_integer(User), is_integer(Auto) ->
    #{<<"passed">> => Passed, <<"failed">> => Failed,
      <<"user_skipped">> => User, <<"auto_skipped">> => Auto};
normalize(start_info, {Tests, Suites, Cases}) ->
    #{<<"tests">> => json_number(Tests), <<"suites">> => json_number(Suites),
      <<"cases">> => json_number(Cases)};
normalize(test_start, {Time, Logdir}) ->
    #{<<"time">> => representation(Time), <<"logdir">> => text(Logdir)};
normalize(test_done, Time) ->
    #{<<"time">> => representation(Time)};
normalize(Name, Data) when Name =:= tc_start; Name =:= tc_done;
                          Name =:= tc_user_skip; Name =:= tc_auto_skip;
                          Name =:= tc_logfile; Name =:= test_stats;
                          Name =:= start_info ->
    #{<<"malformed">> => true, <<"data">> => representation(Data)};
normalize(_Name, _Data) -> #{}.

function_fields(Suite, {Callback, Group, Properties})
  when Callback =:= init_per_group; Callback =:= end_per_group ->
    #{<<"suite">> => text(Suite), <<"case">> => text(Callback),
      <<"callback">> => text(Callback), <<"group">> => text(Group),
      <<"properties">> => representation(Properties)};
function_fields(Suite, {Function, Group}) ->
    (function_fields(Suite, Function))#{<<"group">> => text(Group)};
function_fields(Suite, Function) when is_atom(Function) ->
    Base = #{<<"suite">> => text(Suite), <<"case">> => text(Function)},
    case lists:member(Function, [init_per_suite, end_per_suite, init_per_group,
                                 end_per_group, init_per_testcase, end_per_testcase]) of
        true -> Base#{<<"callback">> => text(Function)};
        false -> Base
    end;
function_fields(Suite, Function) ->
    #{<<"suite">> => text(Suite), <<"case">> => representation(Function),
      <<"malformed">> => true}.

outcome_fields(ok) -> #{<<"outcome">> => <<"passed">>};
outcome_fields({failed, Reason}) ->
    reason_fields(<<"failed">>, Reason);
outcome_fields({skipped, Reason}) ->
    Kind = case Reason of
               {failed, {_, init_per_testcase, _}} -> <<"auto">>;
               {require_failed, _} -> <<"auto">>;
               {timetrap_error, _} -> <<"auto">>;
               {invalid_time_format, _} -> <<"auto">>;
               _ -> <<"user">>
           end,
    (reason_fields(case Kind of <<"auto">> -> <<"auto_skipped">>;
                               _ -> <<"skipped">> end, Reason))#{<<"skipkind">> => Kind};
outcome_fields({auto_skipped, Reason}) ->
    (reason_fields(<<"auto_skipped">>, Reason))#{<<"skipkind">> => <<"auto">>};
outcome_fields({framework_error, Reason}) ->
    reason_fields(<<"framework_error">>, Reason);
outcome_fields(Result) ->
    #{<<"outcome">> => <<"unrecognized">>, <<"result">> => representation(Result)}.

reason_fields(Outcome, Reason) ->
    #{<<"outcome">> => Outcome, <<"reasontext">> => reason_text(Reason),
      <<"reason_term">> => representation(Reason)}.

%% The suites catch absent memory discovery and may then report an apparent
%% resource skip. Record a fresh measurement without loading a missing service,
%% so the outer classifier can distinguish that gap from measured low memory.
resource_fields(#{<<"skipkind">> := <<"user">>, <<"suite">> := Suite}) ->
    Base = #{<<"resource_wordsize">> => erlang:system_info(wordsize),
             <<"resource_build_type">> => text(erlang:system_info(build_type)),
             <<"guard_facts">> => guard_facts(Suite)},
    case code:is_loaded(memsup) of
        false -> Base#{<<"resource_memory_error">> => <<"memsup_not_loaded">>};
        _ ->
            try memsup:get_system_memory_data() of
                Rows when is_list(Rows), Rows =/= [] ->
                    Base#{<<"resource_memory">> =>
                              maps:from_list([{text(Key), Value} ||
                                                {Key, Value} <- Rows,
                                                is_integer(Value)])};
                Other -> Base#{<<"resource_memory_error">> => representation(Other)}
            catch Class:Reason ->
                Base#{<<"resource_memory_error">> => representation({Class, Reason})}
            end
    end;
resource_fields(_) -> #{}.

guard_facts(Suite) ->
    {Family, OS} = os:type(),
    Fields = [flavor, build_type, wordsize, schedulers_online,
              dirty_cpu_schedulers_online, dirty_io_schedulers,
              logical_processors, logical_processors_available, cpu_quota,
              debug_compiled, lock_checking, lock_counting, multi_scheduling],
    Facts = lists:foldl(
      fun(Field, Acc) ->
          Query = case Field of flavor -> emu_flavor; _ -> Field end,
          try erlang:system_info(Query) of
              Value -> Acc#{text(Field) => fact_value(Value)}
          catch _:_ -> Acc
          end
      end, #{<<"suite">> => Suite, <<"os_family">> => text(Family),
             <<"os_name">> => text(OS),
             <<"extreme_testing">> => text(os:getenv("ERL_EXTREME_TESTING")),
             <<"native_name_encoding">> => text(file:native_name_encoding())}, Fields),
    Topology = try erlang:system_info({cpu_topology, detected}) of
                   Value -> representation(Value)
               catch _:_ -> <<"unavailable">>
               end,
    WithTopology = Facts#{<<"cpu_topology_detected">> => Topology},
    case Suite of
        <<"alloc_SUITE">> ->
            maps:merge(WithTopology, mseg_allocator_facts());
        <<"driver_SUITE">> ->
            maps:merge(WithTopology, mseg_allocator_facts());
        <<"time_SUITE">> ->
            maps:merge(WithTopology, timezone_facts());
        <<"dump_SUITE">> ->
            WithTopology#{<<"scheduler_dump">> => text(scheduler_dump_supported())};
        <<"scheduler_SUITE">> ->
            WithTopology#{<<"scheduler_bind">> => text(scheduler_bind_supported())};
        <<"efile_SUITE">> ->
            %% Keep the candidate's exact filelib probes beside the skip event;
            %% the later audit must not substitute another process's filesystem.
            Paths = ["/proc/cpuinfo", "/proc/meminfo", "/proc/partitions",
                     "/proc/swaps", "/proc/version", "/proc/uptime",
                     "/proc/curproc/cmdline"],
            WithTopology#{<<"known_proc_files">> =>
                              [text(P) || P <- Paths, filelib:is_file(P)]};
        _ -> WithTopology
    end.

%% Measure the candidate allocator used by the pinned native HAVE_MSEG() guard.
%% Errors must remain distinct from an actual unavailable allocator.
mseg_allocator_facts() ->
    try erlang:system_info({allocator, mseg_alloc}) of
        false -> #{<<"mseg_alloc_probe">> => <<"ok">>,
                   <<"mseg_alloc">> => <<"false">>};
        Info when is_list(Info) ->
            #{<<"mseg_alloc_probe">> => <<"ok">>,
              <<"mseg_alloc">> => <<"true">>};
        Other -> #{<<"mseg_alloc_probe">> => <<"error">>,
                   <<"mseg_alloc_error">> => representation({unexpected_allocator_info, Other})}
    catch Class:Reason ->
        #{<<"mseg_alloc_probe">> => <<"error">>,
          <<"mseg_alloc_error">> => representation({Class, Reason})}
    end.

%% Observe the exact platform branch used by time_SUITE:is_stockholm_time/0.
%% Win32 reads the host timezone through tzutil; a private TZ variable does not
%% change that guard. A failed native probe must never justify a timezone skip.
timezone_facts() ->
    case os:type() of
        {win32, _} ->
            case os:find_executable("tzutil") of
                false ->
                    #{<<"timezone_probe">> => <<"native_win32_tzutil">>,
                      <<"timezone_probe_succeeded">> => <<"false">>,
                      <<"timezone_probe_error">> => <<"tzutil_not_found">>};
                Executable ->
                    case timezone_probe(Executable) of
                        {ok, Output} ->
                            #{<<"timezone_probe">> => <<"native_win32_tzutil">>,
                              <<"timezone_probe_succeeded">> => <<"true">>,
                              <<"timezone_id">> => text(string:trim(Output)),
                              <<"stockholm_timezone">> =>
                                  text(lists:prefix("W. Europe Standard Time", Output))};
                        {error, Reason} ->
                            #{<<"timezone_probe">> => <<"native_win32_tzutil">>,
                              <<"timezone_probe_succeeded">> => <<"false">>,
                              <<"timezone_probe_error">> => representation(Reason)}
                    end
            end;
        {unix, _} ->
            Output = os:cmd("date '+%Z'"),
            #{<<"timezone_probe">> => <<"unix_date">>,
              <<"stockholm_timezone">> =>
                  text(lists:prefix("ME", Output) orelse lists:prefix("CE", Output)),
              <<"timezone_abbreviation">> => text(string:trim(Output))}
    end.

timezone_probe(Executable) ->
    try open_port({spawn_executable, Executable},
                  [{args, ["/g"]}, binary, exit_status, stderr_to_stdout,
                   use_stdio, hide]) of
        Port -> timezone_probe_result(Port, [])
    catch Class:Reason -> {error, {Class, Reason}}
    end.

timezone_probe_result(Port, Parts) ->
    receive
        {Port, {data, Data}} -> timezone_probe_result(Port, [Data | Parts]);
        {Port, {exit_status, 0}} ->
            Output = binary_to_list(iolist_to_binary(lists:reverse(Parts))),
            case string:trim(Output) of
                [] -> {error, empty_timezone_id};
                _ -> {ok, Output}
            end;
        {Port, {exit_status, Status}} -> {error, {exit_status, Status}}
    after 5000 ->
        try port_close(Port) catch _:_ -> ok end,
        {error, timezone_probe_timeout}
    end.

scheduler_dump_supported() ->
    Previous = try erts_debug:get_internal_state(available_internal_state)
               catch _:_ -> false end,
    try
        erts_debug:set_internal_state(available_internal_state, true),
        erts_debug:get_internal_state(scheduler_dump)
    catch
        _:_ -> false
    after
        catch erts_debug:set_internal_state(available_internal_state, Previous)
    end.

scheduler_bind_supported() ->
    try
        Old = erlang:system_flag(scheduler_bind_type, default_bind),
        erlang:system_flag(scheduler_bind_type, Old),
        true
    catch
        error:notsup -> false;
        _:_ -> true
    end.

fact_value(Value) when is_integer(Value) -> Value;
fact_value(Value) when is_atom(Value) -> text(Value);
fact_value(Value) -> representation(Value).

write_event(Event, State = #{device := Device, sequence := Previous}) ->
    Sequence = Previous + 1,
    Record = Event#{<<"seq">> => Sequence,
                    <<"monotonic_us">> => erlang:monotonic_time(microsecond)},
    ok = file:write(Device, [json:encode(Record), $\n]),
    case maps:get(<<"event">>, Event) of
        <<"test_done">> -> ok = file:sync(Device);
        _ -> ok
    end,
    State#{sequence => Sequence}.

json_number(Value) when is_integer(Value) -> Value;
json_number(Value) -> representation(Value).

text(Value) when is_binary(Value) -> Value;
text(Value) when is_atom(Value) -> atom_to_binary(Value, utf8);
text(Value) when is_list(Value) -> reason_text(Value);
text(Value) -> representation(Value).

reason_text(Value) when is_list(Value); is_binary(Value) ->
    try unicode:characters_to_binary(Value) of
        Binary when is_binary(Binary) -> Binary;
        _ -> representation(Value)
    catch _:_ -> representation(Value) end;
reason_text(Value) -> representation(Value).

representation(Value) ->
    unicode:characters_to_binary(io_lib:format("~tp", [Value])).

%% SPDX-License-Identifier: Apache-2.0
-module(tracing_contract).
-export([missing_backend/0, activate/0, custom_backend/0, run/0, target/1, worker/1, collector/1,
         cleaner_start/0, core_default/0, core_override/0, core_binary/1]).

cleaner_start() -> beam_internal:trace_cleaner_start().
core_default() -> core(beam_internal, trace_cleaner_start).
core_override() -> core(?MODULE, cleaner_start).

core_binary(Binary) ->
    {error,sticky_directory} = erts_internal:prepare_loading(beam_internal, Binary),
    {false,false} = erts_code_purger:purge(beam_internal),
    true = erts_code_purger:soft_purge(beam_internal),
    true = beam:module_loaded(beam_internal),
    true = beam:function_exported(beam_internal, trace_cleaner_start, 0),
    ok.

core(Module, Function) ->
    [_] = [P || P <- beam:processes(),
                beam:process_info(P, initial_call) =:= {initial_call,{Module,Function,0}}],
    false = beam:delete_module(beam_internal),
    false = beam:function_exported(beam_internal, trace_session_create, 3),
    false = beam:function_exported(beam_internal, trace_cleaner_check, 0),
    true = beam:function_exported(erts_internal, system_monitor, 3),
    false = beam:function_exported(beam_trace, system_monitor, 3),
    false = beam:function_exported(erlang, trace, 3),
    false = beam:function_exported(erlang, trace_pattern, 2),
    false = beam:function_exported(erlang, trace_pattern, 3),
    false = beam:function_exported(beam, trace_info, 2),
    ok.

missing_backend() ->
    try beam_trace:session_create(missing_backend, self(), []) of
        _ -> error(expected_missing_backend)
    catch error:badarg -> ok end.

activate() ->
    Processes = beam:processes(),
    ok = beam_trace:load_nif(),
    Processes = beam:processes(),
    ok.

custom_backend() ->
    ok = missing_backend(),
    S = beam_trace:session_create(custom, {beam_trace,self()}, []),
    true = beam_trace:session_destroy(S),
    ok.

run() ->
    Exports = beam_trace:module_info(exports),
    33 = count([FA || FA={F,_} <- Exports, F =/= module_info]),
    false = member({on_load,0}, Exports),
    true = member({load_nif,0}, Exports),
    Parent = self(),
    P = beam:spawn(?MODULE, worker, [Parent]),
    S = beam_trace:session_create(contract, Parent, []),
    [_|_] = beam_trace:session_info(all),
    1 = beam_trace:process(S, P, true, [call,send,'receive']),
    1 = beam_trace:function(S, {?MODULE,target,1}, true, []),
    1 = beam_trace:send(S, true, []),
    1 = beam_trace:recv(S, true, []),
    {tracer,Parent} = beam_trace:info(S, P, tracer),
    [_] = beam_trace:session_info(P),
    N = beam_trace:port(S, all, false, []),
    true = is_integer(N),
    ok = beam_trace:system(S, long_gc, 100),
    {system,[_|_]} = beam_trace:info(S, system, all),
    ok = beam_trace:system(S, long_gc, false),
    P ! ping,
    receive {pong,P} -> ok after 3000 -> error(missing_reply) end,
    receive {trace,P,'receive',ping} -> ok after 3000 -> error(missing_receive_event) end,
    receive {trace,P,call,{?MODULE,target,[ping]}} -> ok after 3000 -> error(missing_call_event) end,
    receive {trace,P,send,{pong,P},Parent} -> ok after 3000 -> error(missing_send_event) end,
    Ref = beam_trace:delivered(ignored_session, P),
    receive {trace_delivered,P,Ref} -> ok after 3000 -> error(missing_delivery_barrier) end,
    1 = beam_trace:process(S, P, false, [all]),
    true = beam_trace:session_destroy(S),
    false = beam_trace:session_destroy(S),
    P ! stop,
    receive {stopped,P} -> ok after 3000 -> error(worker_not_stopped) end,
    undefined = erts_internal:system_monitor(legacy, Parent, [{long_gc,100}]),
    {Parent,[{long_gc,100}]} = erts_internal:system_monitor(legacy, undefined, []),
    ok = sequence(),
    try beam_trace:process(invalid_session, self(), true, []) of
        _ -> error(expected_invalid_session)
    catch error:badarg:Stack ->
        [{beam_trace,process,[invalid_session,_,true,[]],_}|_] = Stack,
        #{1 := <<"invalid trace session">>} = beam_runtime_trace_errors:format_error(badarg, Stack)
    end,
    ok.

worker(Parent) ->
    receive
        ping -> ?MODULE:target(ping), Parent ! {pong,self()}, worker(Parent);
        token_probe -> Parent ! {token_seen,beam_trace:get_token(label)}, worker(Parent);
        stop -> Parent ! {stopped,self()}
    end.
target(Value) -> Value.

sequence() ->
    [] = beam_trace:get_token(),
    [] = beam_trace:set_token([]),
    Self = self(),
    Collector = beam:spawn(?MODULE, collector, [Self]),
    TokenWorker = beam:spawn(?MODULE, worker, [Self]),
    false = beam_trace:set_system_tracer(Collector),
    Collector = beam_trace:get_system_tracer(),
    0 = beam_trace:set_token(label, 7),
    false = beam_trace:set_token(print, true),
    {label,7} = beam_trace:get_token(label),
    Token = beam_trace:get_token(),
    true = is_tuple(Token),
    ok = beam_trace:set_token(Token),
    ok = beam_trace:print(first_marker),
    ok = beam_trace:print(7, second_marker),
    receive {seq_trace,7,{print,_,Self,[],first_marker}} -> ok after 3000 -> error(missing_seq_print) end,
    receive {seq_trace,7,{print,_,Self,[],second_marker}} -> ok after 3000 -> error(missing_seq_label_print) end,
    %% Receiving ordinary collector messages clears the receiver's token.
    ok = beam_trace:set_token(Token),
    false = beam_trace:set_token(send, true),
    TokenWorker ! token_probe,
    receive {token_seen,{label,7}} -> ok after 3000 -> error(missing_seq_token_propagation) end,
    Collector = beam_trace:set_system_tracer(false),
    false = beam_trace:get_system_tracer(),
    true = beam_trace:reset_trace(),
    [] = beam_trace:get_token(),
    Collector ! stop,
    receive {stopped,Collector} -> ok after 3000 -> error(collector_not_stopped) end,
    TokenWorker ! stop,
    receive {stopped,TokenWorker} -> ok after 3000 -> error(token_worker_not_stopped) end,
    ok.

collector(Parent) ->
    receive
        stop -> Parent ! {stopped,self()};
        Event -> Parent ! Event, collector(Parent)
    end.

count([]) -> 0;
count([_|Rest]) -> 1 + count(Rest).
member(X,[X|_]) -> true;
member(X,[_|Rest]) -> member(X,Rest);
member(_,[]) -> false.

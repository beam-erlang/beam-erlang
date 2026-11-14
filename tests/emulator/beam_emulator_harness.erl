%% Candidate-owned Common Test framework and bounded emulator CT execution.
%% All compilation runs inside the candidate VM with its retained bootstrap
%% compiler. No installed Erlang commands or sibling distribution participate.
-module(beam_emulator_harness).
-export([prepare_framework/1, prepare/1, run/1]).

prepare([RuntimeRoot0, SuiteSourceRoot0, EvidenceDir0, Flavor, BuildType]) ->
    RuntimeRoot = filename:absname(RuntimeRoot0),
    SuiteSourceRoot = filename:absname(SuiteSourceRoot0),
    EvidenceDir = filename:absname(EvidenceDir0),
    ensure_directory(EvidenceDir),
    try
        {ok, FrameworkRecords} = prepare_framework([RuntimeRoot, EvidenceDir, Flavor, BuildType]),
        TestEbin = filename:join(RuntimeRoot, "tests"),
        ensure_directory(TestEbin),
        add_path(TestEbin),
        Include = include_options(RuntimeRoot),
        SuiteRecords = lists:map(
          fun(Suite) ->
              Name = atom_to_list(Suite) ++ ".erl",
              Source = filename:join(SuiteSourceRoot, Name),
              Staged = filename:join(TestEbin, Name),
              copy_source(Source, Staged),
              (compile_source(Staged, TestEbin, Include))#{source_origin => Source}
          end, selected_suites()),
        Evidence = #{status => passed, runtime => runtime_identity(),
                     suite_source_root => SuiteSourceRoot,
                     suites => selected_suites(),
                     compiled => FrameworkRecords ++ SuiteRecords},
        write_evidence(EvidenceDir, "prepare-result.term", Evidence),
        io:format("beam_emulator_prepare_ok~n"),
        ok
    catch
        Class:Reason:Stack ->
            write_evidence(EvidenceDir, "prepare-result.term",
                           #{status => failed, class => Class, reason => Reason,
                             stack => Stack, runtime => runtime_identity()}),
            erlang:raise(Class, Reason, Stack)
    end;
prepare(Args) ->
    erlang:error({invalid_prepare_arguments,
                  #{expected => [runtime_root, suite_source_root, evidence_dir, flavor, build_type],
                    received => Args}}).

prepare_framework([RuntimeRoot0, EvidenceDir0, Flavor, BuildType]) ->
    RuntimeRoot = filename:absname(RuntimeRoot0),
    EvidenceDir = filename:absname(EvidenceDir0),
    ensure_directory(EvidenceDir),
    try
        check_runtime(RuntimeRoot, Flavor, BuildType),
        Syntax = filename:join([RuntimeRoot, "lib", "syntax_tools"]),
        CT = filename:join([RuntimeRoot, "lib", "common_test"]),
        SyntaxEbin = filename:join(Syntax, "ebin"),
        CTEbin = filename:join(CT, "ebin"),
        SupportEbin = filename:join(RuntimeRoot, "support"),
        SeedEbin = filename:join(RuntimeRoot, "merl-seed"),
        lists:foreach(fun ensure_directory/1,
                      [SyntaxEbin, CTEbin, SupportEbin, SeedEbin]),
        prepare_app(Syntax, "syntax_tools", "SYNTAX_TOOLS_VSN"),
        prepare_app(CT, "common_test", "COMMON_TEST_VSN"),
        lists:foreach(fun add_path/1, [SyntaxEbin, CTEbin, SupportEbin]),
        Include = include_options(RuntimeRoot),
        SyntaxRecords = compile_modules(syntax_modules(), filename:join(Syntax, "src"),
                                        SyntaxEbin, Include),
        TransformSource = filename:join([Syntax, "src", "merl_transform.erl"]),
        SeedRecord = compile_source(TransformSource, SeedEbin,
                                    [{d, 'MERL_NO_TRANSFORM'} | Include]),
        TransformRecord = compile_source(TransformSource, SyntaxEbin, Include),
        CTRecords = compile_modules(ct_modules(), filename:join(CT, "src"),
                                    CTEbin, Include),
        HookRecord = compile_source(
                       filename:join([CT, "test_server", "ts_install_cth.erl"]),
                       SupportEbin, Include),
        FrameworkRecords = SyntaxRecords ++ [SeedRecord, TransformRecord] ++
                           CTRecords ++ [HookRecord],
        Evidence = #{status => passed, runtime => runtime_identity(),
                     compiled => FrameworkRecords},
        write_evidence(EvidenceDir, "framework-prepare-result.term", Evidence),
        {ok, FrameworkRecords}
    catch
        Class:Reason:Stack ->
            write_evidence(EvidenceDir, "framework-prepare-result.term",
                           #{status => failed, class => Class, reason => Reason,
                             stack => Stack, runtime => runtime_identity()}),
            erlang:raise(Class, Reason, Stack)
    end;
prepare_framework(Args) ->
    erlang:error({invalid_framework_prepare_arguments,
                  #{expected => [runtime_root, evidence_dir, flavor, build_type],
                    received => Args}}).

run([RuntimeRoot0, SuiteName, ExpectedCount0, LogDir0, Flavor, BuildType]) ->
    RuntimeRoot = filename:absname(RuntimeRoot0),
    LogDir = filename:absname(LogDir0),
    ensure_directory(LogDir),
    try
        check_runtime(RuntimeRoot, Flavor, BuildType),
        Suite = suite_atom(SuiteName),
        ExpectedCount = expected_count(ExpectedCount0),
        TestEbin = filename:join(RuntimeRoot, "tests"),
        lists:foreach(fun add_path/1,
                      [filename:join([RuntimeRoot, "lib", "syntax_tools", "ebin"]),
                       filename:join([RuntimeRoot, "lib", "common_test", "ebin"]),
                       filename:join(RuntimeRoot, "support"), TestEbin]),
        Beam = filename:join(TestEbin, SuiteName ++ ".beam"),
        require_file(Beam),
        load_module(Suite, Beam),
        Result = ct:run_test([{dir, TestEbin}, {suite, Suite},
                              {auto_compile, false},
                              {abort_if_missing_suites, true},
                              {logdir, LogDir}]),
        Passed = Result =:= {ExpectedCount, 0, {0, 0}},
        Evidence = #{status => case Passed of true -> passed; false -> failed end,
                     suite => Suite, expected_cases => ExpectedCount,
                     result => Result, runtime => runtime_identity()},
        write_evidence(LogDir, "result.term", Evidence),
        case Passed of
            true ->
                io:format("beam_emulator_suite_ok,~s,~B~n", [SuiteName, ExpectedCount]),
                ok;
            false ->
                erlang:error({emulator_suite_nonpassing,
                              #{suite => Suite, expected => {ExpectedCount, 0, {0, 0}},
                                actual => Result, logs => LogDir}})
        end
    catch
        Class:Reason:Stack ->
            write_evidence(LogDir, "failure.term",
                           #{status => failed, suite => SuiteName, class => Class,
                             reason => Reason, stack => Stack,
                             runtime => runtime_identity()}),
            erlang:raise(Class, Reason, Stack)
    end;
run(Args) ->
    erlang:error({invalid_run_arguments,
                  #{expected => [runtime_root, suite_name, expected_count, log_dir, flavor, build_type],
                    received => Args}}).

selected_suites() ->
    [prim_eval_SUITE, atomics_SUITE, counters_SUITE, num_bif_SUITE,
     process_max_heap_size_SUITE].

%% Keep the pinned upstream Makefile order, with the transform bootstrapped
%% separately using its original MERL_NO_TRANSFORM recipe.
syntax_modules() ->
    [erl_syntax, erl_prettypr, erl_syntax_lib, erl_comment_scan, erl_recomment,
     epp_dodger, prettypr, merl].

%% Behavior modules precede their users, as in common_test/src/Makefile.
ct_modules() ->
    [ct_suite, ct_hooks,
     ct, ct_logs, ct_framework, ct_ftp, ct_ssh, ct_snmp, ct_gen_conn, ct_rpc,
     ct_run, ct_master, ct_telnet, ct_util, ct_cover, ct_doctest, ct_testspec,
     ct_event, ct_master_event, ct_master_logs, ct_master_status, ct_repeat,
     ct_telnet_client, ct_make, unix_telnet, ct_config, ct_config_plain,
     ct_config_xml, ct_slave, ct_hooks_lock, cte_track, cth_log_redirect,
     cth_surefire, ct_netconfc, ct_conn_log_h, cth_conn_log, ct_groups,
     ct_property_test, ct_release_test, ct_default_gl, erl2html2,
     test_server_ctrl, test_server_gl, test_server_io, test_server_node,
     test_server, test_server_sup].

include_options(RuntimeRoot) ->
    [{i, filename:join(RuntimeRoot, "lib")} |
     [{i, filename:join([RuntimeRoot, "lib", App, Dir])}
      || {App, Dir} <- [{"kernel", "include"}, {"stdlib", "include"},
                        {"syntax_tools", "include"}, {"common_test", "include"},
                        {"snmp", "include"}, {"xmerl", "include"},
                        {"xmerl", "inc"}]]].

compile_modules(Modules, SourceDir, Ebin, Include) ->
    [compile_source(filename:join(SourceDir, atom_to_list(Module) ++ ".erl"),
                    Ebin, Include) || Module <- Modules].

compile_source(Source, Ebin, Include) ->
    require_file(Source),
    Options = [debug_info, return_errors, return_warnings, {outdir, Ebin} | Include],
    case compile:file(Source, Options) of
        {ok, Module, Warnings} ->
            Beam = filename:join(Ebin, atom_to_list(Module) ++ ".beam"),
            load_module(Module, Beam),
            #{module => Module, source => Source, beam => Beam, warnings => Warnings};
        {error, Errors, Warnings} ->
            erlang:error({candidate_compile_failed,
                          #{source => Source, errors => Errors, warnings => Warnings}});
        Other ->
            erlang:error({candidate_compile_failed, #{source => Source, result => Other}})
    end.

load_module(Module, Beam) ->
    _ = code:purge(Module),
    _ = code:delete(Module),
    case code:load_abs(filename:rootname(Beam)) of
        {module, Module} -> ok;
        Other -> erlang:error({candidate_module_load_failed, Beam, Other})
    end.

prepare_app(AppRoot, Name, VersionVariable) ->
    VsnFile = filename:join(AppRoot, "vsn.mk"),
    VsnBytes = read_file(VsnFile),
    Pattern = "(?m)^" ++ VersionVariable ++ "[ \\t]*=[ \\t]*([^ \\t\\r\\n]+)",
    Version = case re:run(VsnBytes, Pattern, [{capture, [1], binary}]) of
                  {match, [Value]} -> Value;
                  _ -> erlang:error({missing_application_version, VsnFile, VersionVariable})
              end,
    lists:foreach(
      fun(Suffix) ->
          Source = filename:join([AppRoot, "src", Name ++ Suffix ++ ".src"]),
          Output = filename:join([AppRoot, "ebin", Name ++ Suffix]),
          Bytes = binary:replace(read_file(Source), <<"%VSN%">>, Version, [global]),
          case binary:match(Bytes, <<"%VSN%">>) of
              nomatch -> ok;
              _ -> erlang:error({unresolved_application_version, Source})
          end,
          write_file(Output, Bytes)
      end, [".app", ".appup"]).

check_runtime(RuntimeRoot, FlavorText, BuildTypeText) ->
    Flavor = case FlavorText of
                 "emu" -> emu;
                 "jit" -> jit;
                 _ -> erlang:error({invalid_expected_flavor, FlavorText})
             end,
    ExpectedDiagnostics = case BuildTypeText of
                             "debug" -> {debug, true, true};
                             "opt" -> {opt, false, false};
                             _ -> erlang:error({invalid_expected_build_type, BuildTypeText})
                         end,
    case {erlang:system_info(build_type), erlang:system_info(debug_compiled),
          erlang:system_info(lock_checking)} of
        ExpectedDiagnostics -> ok;
        Diagnostics -> erlang:error({unexpected_candidate_build,
                                     #{expected => ExpectedDiagnostics, actual => Diagnostics}})
    end,
    case {erlang:system_info(emu_flavor), erlang:system_info(schedulers_online),
          node(), filename:absname(code:root_dir())} of
        {Flavor, Schedulers, nonode@nohost, RuntimeRoot} when Schedulers >= 2 -> ok;
        Actual -> erlang:error({unexpected_candidate_runtime,
                               #{expected => #{flavor => Flavor, min_schedulers => 2,
                                               node => nonode@nohost, root => RuntimeRoot},
                                 actual => Actual}})
    end,
    Compiler = filename:absname(code:which(compile)),
    ExpectedCompiler = filename:join([RuntimeRoot, "lib", "compiler", "ebin", "compile.beam"]),
    case Compiler of
        ExpectedCompiler -> ok;
        _ -> erlang:error({unexpected_candidate_compiler,
                           #{expected => ExpectedCompiler, actual => Compiler}})
    end.

runtime_identity() ->
    #{erts_version => erlang:system_info(version),
      otp_release => erlang:system_info(otp_release),
      flavor => erlang:system_info(emu_flavor),
      build_type => erlang:system_info(build_type),
      debug_compiled => erlang:system_info(debug_compiled),
      lock_checking => erlang:system_info(lock_checking),
      architecture => erlang:system_info(system_architecture),
      schedulers_online => erlang:system_info(schedulers_online),
      node => node(), root => code:root_dir(), bindir => os:getenv("BINDIR"),
      compiler => #{path => code:which(compile), md5 => compile:module_info(md5)}}.

suite_atom(Name) when is_list(Name), Name =/= [] ->
    case lists:all(fun(C) -> (C >= $a andalso C =< $z) orelse
                              (C >= $A andalso C =< $Z) orelse
                              (C >= $0 andalso C =< $9) orelse C =:= $_ end, Name) of
        true -> list_to_atom(Name);
        false -> erlang:error({invalid_suite_name, Name})
    end;
suite_atom(Name) -> erlang:error({invalid_suite_name, Name}).

expected_count(Text) when is_list(Text) ->
    try list_to_integer(Text) of
        Count when Count > 0 -> Count;
        _ -> erlang:error({invalid_expected_case_count, Text})
    catch error:badarg -> erlang:error({invalid_expected_case_count, Text}) end;
expected_count(Count) when is_integer(Count), Count > 0 -> Count;
expected_count(Value) -> erlang:error({invalid_expected_case_count, Value}).

add_path(Path) ->
    case code:add_patha(Path) of
        true -> ok;
        Other -> erlang:error({cannot_add_candidate_code_path, Path, Other})
    end.

ensure_directory(Path) ->
    case filelib:ensure_dir(filename:join(Path, ".directory")) of
        ok -> ok;
        Error -> erlang:error({cannot_create_test_directory, Path, Error})
    end.

require_file(Path) ->
    case filelib:is_regular(Path) of
        true -> ok;
        false -> erlang:error({missing_test_input, Path})
    end.

read_file(Path) ->
    case file:read_file(Path) of
        {ok, Bytes} -> Bytes;
        Error -> erlang:error({cannot_read_test_input, Path, Error})
    end.

copy_source(Source, Destination) ->
    write_file(Destination, read_file(Source)).

write_file(Path, Bytes) ->
    case file:write_file(Path, Bytes) of
        ok -> ok;
        Error -> erlang:error({cannot_write_test_output, Path, Error})
    end.

write_evidence(Directory, Name, Evidence) ->
    write_file(filename:join(Directory, Name), io_lib:format("~tp.~n", [Evidence])).

%% Prepare the unchanged emulator corpus using the candidate VM and compiler.
%% Native targets and their scheduling belong to CMake. This module compiles
%% Erlang inputs and creates inspection-only preload files in the test root.
-module(beam_emulator_full_prepare).
-export([prepare/1]).

prepare([RuntimeRoot0, CorpusSourceRoot0, EvidenceDir0, Flavor, BuildType]) ->
    RuntimeRoot = filename:absname(RuntimeRoot0),
    CorpusSourceRoot = filename:absname(CorpusSourceRoot0),
    EvidenceDir = filename:absname(EvidenceDir0),
    ensure_directory(EvidenceDir),
    try
        check_runtime(RuntimeRoot, Flavor, BuildType),
        RepositoryRoot = filename:dirname(filename:dirname(
                                           filename:dirname(CorpusSourceRoot))),
        Config = corpus_config(),
        Pin = string:trim(read_file(filename:join(RepositoryRoot, "UPSTREAM_COMMIT"))),
        Include = include_options(RuntimeRoot),
        CorpusRoot = filename:join(RuntimeRoot, "emulator_test"),
        ensure_directory(CorpusRoot),
        AppRecords = prepare_apps(RuntimeRoot, Include),
        SupportRecords = prepare_support(RuntimeRoot, CorpusRoot, Include),
        PreloadRecords = prepare_preloads(RuntimeRoot, RepositoryRoot, Config, Include),
        OrdinaryBifRecords = prepare_ordinary_bif_inspection(RuntimeRoot, Config, Include),
        BifMetadataRecords = preflight_bif_metadata(RuntimeRoot, Config),
        CorpusRecords = stage_tree(CorpusSourceRoot, CorpusRoot),
        SpecsRoot = filename:join(RuntimeRoot, "specs"),
        ensure_directory(SpecsRoot),
        lists:foreach(fun(Name0) ->
                              Name = text(Name0),
                              copy_file(filename:join(CorpusRoot, Name),
                                        filename:join(SpecsRoot, Name))
                      end, maps:get(specs, Config)),
        Modules = [text(Name) || Name <- maps:get(modules, Config)],
        Suites = [Module || Module <- Modules, lists:suffix("_SUITE", Module)],
        NormalOptions = maps:get(normal_options, Config),
        ModuleRecords = compile_modules(Modules, CorpusRoot, CorpusRoot,
                                        NormalOptions ++ Include),
        VariantRecords = prepare_variants(CorpusRoot, Config, NormalOptions ++ Include),
        ok = ensure_suite_data_directories(CorpusRoot),
        DataRecords = compile_data_jobs(CorpusRoot, Include),
        add_path(CorpusRoot),
        Selection = maps:map(fun(_, Names) -> [list_to_binary(Name) || Name <- Names] end,
                             #{suites => Suites, no_opt => maps:get(no_opt, Config),
                               stripped_types => maps:get(stripped_types, Config)}),
        write_file(filename:join(EvidenceDir, "corpus-selection.json"), json:encode(Selection)),
        Evidence = #{status => passed, upstream_commit => Pin,
                     runtime => runtime_identity(),
                     corpus_configuration => Config,
                     staged_files => CorpusRecords,
                     applications => AppRecords, support => SupportRecords,
                     preloads => PreloadRecords,
                     ordinary_bif_inspection => OrdinaryBifRecords,
                     bif_metadata_preflight => BifMetadataRecords,
                     compiled => ModuleRecords ++ VariantRecords ++ DataRecords,
                     ordinary_modules => length(ModuleRecords),
                     ordinary_suites => length(Suites),
                     generated_variants => length(VariantRecords),
                     specs => maps:get(specs, Config)},
        write_evidence(EvidenceDir, "full-prepare-result.term", Evidence),
        io:format("beam_emulator_full_prepare_ok,modules=~B,variants=~B,preloads=~B~n",
                  [length(ModuleRecords), length(VariantRecords), length(PreloadRecords)]),
        ok
    catch
        Class:Reason:Stack ->
            write_evidence(EvidenceDir, "full-prepare-result.term",
                           #{status => failed, class => Class, reason => Reason,
                             stack => Stack, runtime => runtime_identity()}),
            erlang:raise(Class, Reason, Stack)
    end;
prepare(Args) ->
    erlang:error({invalid_full_prepare_arguments,
                  #{expected => [runtime_root, corpus_source_root, evidence_dir,
                                  flavor, build_type],
                    received => Args}}).

%% Module order and variant recipes follow upstream Erlang/OTP.
%% Keep the preparation settings here beside the code that consumes them.
corpus_config() ->
    #{modules =>
        ["a_SUITE", "after_SUITE", "alloc_SUITE", "async_ports_SUITE", "atomics_SUITE",
         "beam_SUITE", "beam_literals_SUITE", "bif_SUITE", "big_SUITE", "binary_SUITE",
         "bs_bincomp_SUITE", "bs_bit_binaries_SUITE", "bs_construct_SUITE",
         "bs_match_bin_SUITE", "bs_match_int_SUITE", "bs_match_tail_SUITE",
         "bs_match_misc_SUITE", "bs_utf_SUITE", "busy_port_SUITE", "call_trace_SUITE",
         "code_SUITE", "code_parallel_load_SUITE", "counters_SUITE", "crypto_SUITE",
         "ddll_SUITE", "decode_packet_SUITE", "dirty_bif_SUITE", "dirty_nif_SUITE",
         "distribution_SUITE", "driver_SUITE", "dummy", "dump_SUITE", "efile_SUITE",
         "erl_debugger_SUITE", "erts_debug_SUITE", "estone_SUITE", "erl_link_SUITE",
         "erl_drv_thread_SUITE", "evil_SUITE", "exception_SUITE", "float_SUITE", "fun_SUITE",
         "gc_SUITE", "guard_SUITE", "hash_SUITE", "hash_property_test_SUITE", "hello_SUITE",
         "hibernate_SUITE", "iovec_SUITE", "jit_SUITE", "list_bif_SUITE", "lttng_SUITE",
         "lcnt_SUITE", "map_SUITE", "match_spec_SUITE", "module_info_SUITE", "monitor_SUITE",
         "multi_load_SUITE", "native_record_SUITE", "nested_SUITE", "nif_SUITE",
         "node_container_SUITE", "nofrag_SUITE", "num_bif_SUITE", "message_queue_data_SUITE",
         "op_SUITE", "os_signal_SUITE", "port_SUITE", "port_bif_SUITE", "prim_eval_SUITE",
         "persistent_term_SUITE", "process_SUITE", "process_max_heap_size_SUITE",
         "pseudoknot_SUITE", "receive_SUITE", "ref_SUITE", "register_SUITE", "mtx_SUITE",
         "save_calls_SUITE", "send_term_SUITE", "sensitive_SUITE", "signal_SUITE",
         "small_SUITE", "smoke_test_SUITE", "statistics_SUITE", "system_info_SUITE",
         "system_profile_SUITE", "time_SUITE", "timer_bif_SUITE", "trace_SUITE",
         "trace_bif_SUITE", "trace_nif_SUITE", "trace_port_SUITE", "tuple_SUITE",
         "trace_local_SUITE", "trace_meta_SUITE", "trace_call_count_SUITE",
         "trace_call_time_SUITE", "trace_call_memory_SUITE", "tracer_SUITE",
         "trace_session_SUITE", "trace_sessions", "tracer_test", "scheduler_SUITE",
         "port_trace_SUITE", "unique_SUITE", "z_SUITE", "long_timers_test", "ignore_cores",
         "dgawd_handler", "random_iolist", "erts_test_utils", "erts_test_destructor",
         "crypto_reference", "literal_area_collector_test", "ext_records"],
      specs =>
        ["emulator.spec", "emulator.spec.win", "emulator_bench.spec", "emulator_gh.spec",
         "emulator_smoke.spec", "emulator_trace.spec", "emulator_node_container_SUITE.spec"],
      normal_options => [compressed, nowarn_missing_spec_documented],
      no_opt =>
        ["bs_bincomp", "bs_bit_binaries", "bs_construct", "bs_match_bin", "bs_match_int",
         "bs_match_tail", "bs_match_misc", "bs_utf", "guard", "map"],
      no_opt_options => [no_copt, no_postopt, no_ssa_opt, no_bsm_opt],
      stripped_types =>
        ["bs_bincomp", "bs_construct", "bs_match_bin", "bs_match_int", "bs_match_tail",
         "bs_match_misc", "bs_utf"],
      stripped_types_options => [strip_types],
      preloads =>
          [#{module => Name,
             source => filename:join(["erts", "preloaded", "src", Name ++ ".erl"]),
             seed => filename:join(["erts", "preloaded", "ebin", Name ++ ".beam"])}
           || Name <- preload_modules()],
      ordinary_bif_modules =>
          [#{application => App, module => Name,
             source => filename:join(["lib", App, "src", Name ++ ".erl"]),
             seed => filename:join(["lib", App, "ebin", Name ++ ".beam"])}
           || {App, Name} <- ordinary_bif_modules()]}.

preload_modules() ->
    ["atomics", "counters", "erl_init", "erl_prim_loader", "erl_tracer", "erlang",
     "erts_code_purger", "erts_dirty_process_signal_handler", "erts_internal",
     "erts_literal_area_collector", "erts_trace_cleaner", "init", "persistent_term",
     "prim_buffer", "prim_eval", "prim_file", "prim_inet", "prim_net", "prim_socket",
     "prim_zip", "socket_registry", "zlib"].

ordinary_bif_modules() ->
    [{"kernel", "code"}, {"kernel", "erl_ddll"}, {"kernel", "erl_debugger"},
     {"kernel", "error_logger"}, {"kernel", "erts_debug"}, {"kernel", "file"},
     {"kernel", "net_kernel"}, {"kernel", "os"},
     {"stdlib", "binary"}, {"stdlib", "ets"}, {"stdlib", "io"}, {"stdlib", "lists"},
     {"stdlib", "maps"}, {"stdlib", "math"}, {"stdlib", "re"}, {"stdlib", "records"},
     {"stdlib", "string"}, {"stdlib", "unicode"}].

prepare_apps(RuntimeRoot, Include) ->
    [prepare_app(RuntimeRoot, "sasl", "SASL_VSN", sasl_modules(), Include),
     prepare_app(RuntimeRoot, "os_mon", "OS_MON_VSN", os_mon_modules(), Include)].

prepare_app(RuntimeRoot, Name, VersionVariable, Modules, Include) ->
    AppRoot = filename:join([RuntimeRoot, "lib", Name]),
    Ebin = filename:join(AppRoot, "ebin"),
    ensure_directory(Ebin),
    add_path(Ebin),
    VsnFile = filename:join(AppRoot, "vsn.mk"),
    Pattern = "(?m)^" ++ VersionVariable ++ "[ \\t]*=[ \\t]*([^ \\t\\r\\n]+)",
    Version = case re:run(read_file(VsnFile), Pattern, [{capture, [1], binary}]) of
                  {match, [Value]} -> Value;
                  _ -> erlang:error({missing_application_version, VsnFile})
              end,
    lists:foreach(
      fun(Suffix) ->
          Template = filename:join([AppRoot, "src", Name ++ Suffix ++ ".src"]),
          Bytes = binary:replace(read_file(Template), <<"%VSN%">>, Version, [global]),
          nomatch = binary:match(Bytes, <<"%VSN%">>),
          write_file(filename:join(Ebin, Name ++ Suffix), Bytes)
      end, [".app", ".appup"]),
    Records = compile_modules(Modules, filename:join(AppRoot, "src"), Ebin,
                              [nowarn_deprecated_catch | Include]),
    #{application => Name, version => Version, compiled => Records}.

sasl_modules() ->
    ["alarm_handler", "sasl", "sasl_report", "sasl_report_file_h",
     "sasl_report_tty_h", "format_lib_supp", "misc_supp", "rb",
     "rb_format_supp", "release_handler", "release_handler_1", "systools",
     "systools_make", "systools_rc", "systools_relup", "systools_lib", "erlsrv"].

os_mon_modules() ->
    ["disksup", "memsup", "cpu_sup", "os_mon", "os_mon_mib", "os_sup",
     "os_mon_sysinfo", "nteventlog"].

prepare_support(RuntimeRoot, CorpusRoot, Include) ->
    EUnitRoot = filename:join([RuntimeRoot, "lib", "eunit"]),
    EUnitEbin = filename:join(EUnitRoot, "ebin"),
    RuntimeToolsRoot = filename:join([RuntimeRoot, "lib", "runtime_tools"]),
    RuntimeToolsEbin = filename:join(RuntimeToolsRoot, "ebin"),
    ensure_directory(EUnitEbin),
    ensure_directory(RuntimeToolsEbin),
    add_path(EUnitEbin),
    add_path(RuntimeToolsEbin),
    TransformRecords = compile_modules(["eunit_autoexport", "eunit_striptests"],
                                        filename:join(EUnitRoot, "src"), EUnitEbin, Include),
    lists:foreach(fun(Record) -> load_transform(maps:get(module, Record), maps:get(beam, Record)) end,
                  TransformRecords),
    RuntimeToolRecords = compile_modules(["dbg", "instrument"],
                                         filename:join(RuntimeToolsRoot, "src"),
                                         RuntimeToolsEbin, Include),
    %% The upstream release recipe puts shared helpers alongside the suites.
    %% Peers inherit emulator_test; controller-only support paths are not enough.
    Paths = ["lib/dialyzer/src/erl_types.erl", "lib/dialyzer/src/erl_bif_types.erl",
             "lib/stdlib/test/error_info_lib.erl",
             "lib/kernel/examples/gen_tcp_dist/src/gen_tcp_dist.erl"],
    SharedRecords = [begin
                         Source = filename:join(RuntimeRoot, Path),
                         Staged = filename:join(CorpusRoot, filename:basename(Source)),
                         copy_file(Source, Staged),
                         (compile_source(Staged, CorpusRoot, Include))#{source_origin => Source}
                     end || Path <- Paths],
    TransformRecords ++ RuntimeToolRecords ++ SharedRecords.

prepare_preloads(RuntimeRoot, RepositoryRoot, Config, Include) ->
    Version = erlang:system_info(version),
    "17.0" = Version,
    ErtsRoot = filename:join([RuntimeRoot, "lib", "erts-" ++ Version]),
    Ebin = filename:join(ErtsRoot, "ebin"),
    SourceDir = filename:join(ErtsRoot, "src"),
    ensure_directory(Ebin),
    ensure_directory(SourceDir),
    add_path(Ebin),
    PreloadSourceRoot = filename:join([RepositoryRoot, "erts", "preloaded", "src"]),
    copy_file(filename:join(PreloadSourceRoot, "zip_internal.hrl"),
              filename:join(SourceDir, "zip_internal.hrl")),
    Records = lists:map(
      fun(Entry) ->
          ModuleName = text(maps:get(module, Entry)),
          Module = list_to_atom(ModuleName),
          SourceRelative = safe_relative(maps:get(source, Entry)),
          SeedRelative = safe_relative(maps:get(seed, Entry)),
          Source = filename:join(RepositoryRoot, SourceRelative),
          Seed = filename:join(RepositoryRoot, SeedRelative),
          StagedSource = filename:join(SourceDir, ModuleName ++ ".erl"),
          copy_file(Source, StagedSource),
          {Module, Metadata, Warnings} = compile_binary(StagedSource, Include),
          SeedBytes = read_file(Seed),
          {ok, Module, SeedChunks} = beam_lib:all_chunks(SeedBytes),
          {ok, Module, MetadataChunks} = beam_lib:all_chunks(Metadata),
          DebugChunk = required_chunk("Dbgi", MetadataChunks),
          DocsChunk = required_chunk("Docs", MetadataChunks),
          Preserved = [Chunk || {Id, _} = Chunk <- SeedChunks,
                                Id =/= "Dbgi", Id =/= "Docs"],
          {ok, Augmented} = beam_lib:build_module(Preserved ++ [DebugChunk, DocsChunk]),
          {ok, Module, AugmentedChunks} = beam_lib:all_chunks(Augmented),
          lists:foreach(fun({Id, Bytes}) -> {Id, Bytes} = required_chunk(Id, AugmentedChunks) end,
                        Preserved),
          {ok, {Module, [{abstract_code, {raw_abstract_v1, _}}]}} =
              beam_lib:chunks(Augmented, [abstract_code]),
          Output = filename:join(Ebin, ModuleName ++ ".beam"),
          write_file(Output, Augmented),
          #{module => Module, source => SourceRelative, seed => SeedRelative,
            seed_md5 => md5(SeedBytes), output => Output, output_md5 => md5(Augmented),
            augmented_chunks => ["Dbgi", "Docs"],
            preserved_chunks => [Id || {Id, _} <- Preserved], warnings => Warnings}
      end, maps:get(preloads, Config)),
    Template = read_file(filename:join(PreloadSourceRoot, "erts.app.src")),
    App0 = binary:replace(Template, <<"%VSN%">>, list_to_binary(Version), [global]),
    App = binary:replace(App0, <<"%ESOCK_MODS%">>,
                         <<"prim_net, prim_socket, socket_registry,">>, [global]),
    write_file(filename:join(Ebin, "erts.app"), App),
    ErtsRoot = code:lib_dir(erts),
    Records.

prepare_ordinary_bif_inspection(RuntimeRoot, Config, Include) ->
    Entries = maps:get(ordinary_bif_modules, Config),
    DeclaredModules = lists:sort([list_to_atom(text(maps:get(module, Entry)))
                                  || Entry <- Entries]),
    ActualModules = lists:sort([Module || Module <- bif_modules(),
                                        code:which(Module) =/= preloaded]),
    case DeclaredModules =:= ActualModules of
        true -> ok;
        false -> erlang:error({ordinary_bif_module_inventory_changed,
                               #{declared => DeclaredModules, actual => ActualModules}})
    end,
    lists:map(
      fun(Entry) ->
          App = text(maps:get(application, Entry)),
          Name = text(maps:get(module, Entry)),
          Module = list_to_atom(Name),
          SourceRelative = safe_relative(maps:get(source, Entry)),
          SeedRelative = safe_relative(maps:get(seed, Entry)),
          SourceRelative = filename:join(["lib", App, "src", Name ++ ".erl"]),
          SeedRelative = filename:join(["lib", App, "ebin", Name ++ ".beam"]),
          Source = filename:join(RuntimeRoot, SourceRelative),
          Seed = filename:join(RuntimeRoot, SeedRelative),
          Seed = filename:absname(code:which(Module)),
          SeedBytes = read_file(Seed),
          {Module, Metadata, Warnings} = compile_binary(Source, Include),
          {ok, {Module, [{exports, SeedExports0}]}} = beam_lib:chunks(SeedBytes, [exports]),
          {ok, {Module, [{exports, SourceExports0}]}} = beam_lib:chunks(Metadata, [exports]),
          SeedExports = lists:sort(SeedExports0),
          SourceExports = lists:sort(SourceExports0),
          case SeedExports =:= SourceExports of
              true -> ok;
              false -> erlang:error({bif_inspection_source_exports_differ,
                                     #{module => Module, seed => SeedRelative,
                                       source => SourceRelative,
                                       seed_only => SeedExports -- SourceExports,
                                       source_only => SourceExports -- SeedExports}})
          end,
          {ok, Module, SeedChunks} = beam_lib:all_chunks(SeedBytes),
          {ok, Module, MetadataChunks} = beam_lib:all_chunks(Metadata),
          Preserved = [Chunk || {Id, _} = Chunk <- SeedChunks,
                                Id =/= "Dbgi", Id =/= "Docs"],
          NewMetadata = [required_chunk("Dbgi", MetadataChunks),
                         required_chunk("Docs", MetadataChunks)],
          {ok, Augmented} = beam_lib:build_module(Preserved ++ NewMetadata),
          {ok, Module, AugmentedChunks} = beam_lib:all_chunks(Augmented),
          lists:foreach(fun({Id, Bytes}) -> {Id, Bytes} = required_chunk(Id, AugmentedChunks) end,
                        Preserved),
          {ok, {Module, [{abstract_code, {raw_abstract_v1, _}}]}} =
              beam_lib:chunks(Augmented, [abstract_code]),
          %% This file remains the bootstrap execution seed. Do not load the
          %% newly compiled source stubs or replace any executable seed chunk.
          write_file(Seed, Augmented),
          #{module => Module, application => App, source => SourceRelative,
            seed => SeedRelative, seed_md5 => md5(SeedBytes),
            output_md5 => md5(Augmented), exports_match => true,
            augmented_chunks => ["Dbgi", "Docs"],
            preserved_chunks => [Id || {Id, _} <- Preserved], warnings => Warnings}
      end, Entries).

preflight_bif_metadata(RuntimeRoot, Config) ->
    Preloads = [list_to_atom(text(maps:get(module, Entry)))
                || Entry <- maps:get(preloads, Config)],
    lists:map(
      fun(Module) ->
          Beam = case code:which(Module) of
                     preloaded ->
                         true = lists:member(Module, Preloads),
                         filename:join([RuntimeRoot, "lib", "erts-17.0", "ebin",
                                        atom_to_list(Module) ++ ".beam"]);
                     Path when is_list(Path) -> filename:absname(Path)
                 end,
          case beam_lib:chunks(Beam, [abstract_code]) of
              {ok, {Module, [{abstract_code, {raw_abstract_v1, _}}]}} ->
                  #{module => Module, beam => Beam, raw_abstract_code => true};
              Other -> erlang:error({bif_module_missing_inspection_metadata, Module, Beam, Other})
          end
      end, bif_modules()).

bif_modules() ->
    lists:usort([Module || {Module, _, _} <- erlang:system_info(snifs)]).

required_chunk(Id, Chunks) ->
    case lists:keyfind(Id, 1, Chunks) of
        false -> erlang:error({missing_preload_chunk, Id});
        Chunk -> Chunk
    end.

stage_tree(SourceRoot, DestinationRoot) ->
    Sources = lists:sort(filelib:fold_files(SourceRoot, ".*", true,
                                           fun(Path, Acc) -> [Path | Acc] end, [])),
    PrefixLength = length(filename:split(SourceRoot)),
    [begin
         Relative = safe_relative(filename:join(lists:nthtail(PrefixLength,
                                                             filename:split(Source)))),
         Target = filename:join(DestinationRoot, Relative),
         copy_file(Source, Target),
         #{source => Source, staged_path => Target, staged_md5 => md5_file(Target)}
     end || Source <- Sources].

prepare_variants(CorpusRoot, Config, Options) ->
    lists:append(
      [begin
           VariantOptions = maps:get(OptionsKey, Config),
           [begin
                Stem = text(Stem0),
                Original = Stem ++ "_SUITE",
                Variant = Stem ++ Suffix ++ "_SUITE",
                Source = filename:join(CorpusRoot, Original ++ ".erl"),
                Target = filename:join(CorpusRoot, Variant ++ ".erl"),
                Before = list_to_binary("-module(" ++ Original ++ ")."),
                After = list_to_binary("-module(" ++ Variant ++ ")."),
                Bytes = read_file(Source),
                [_] = binary:matches(Bytes, Before),
                write_file(Target, binary:replace(Bytes, Before, After)),
                (compile_source(Target, CorpusRoot, VariantOptions ++ Options))#{
                  generated_from => Source, variant_options => VariantOptions}
            end || Stem0 <- maps:get(NamesKey, Config)]
       end || {NamesKey, Suffix, OptionsKey} <-
                  [{no_opt, "_no_opt", no_opt_options},
                   {stripped_types, "_stripped_types", stripped_types_options}]]).

compile_data_jobs(CorpusRoot, Include) ->
    [begin
         Source = filename:join([CorpusRoot, Directory, Module ++ ".erl"]),
         compile_source(Source, filename:dirname(Source), Include)
     end || {Directory, Module} <-
                [{"distribution_SUITE_data", "run"},
                 {"port_SUITE_data", "port_test"},
                 {"float_SUITE_data", "has_fpe_bug"}]].

compile_modules(Modules, SourceDir, Ebin, Options) ->
    [compile_source(filename:join(SourceDir, Module ++ ".erl"), Ebin, Options)
     || Module <- Modules].

compile_source(Source, Ebin, Options) ->
    ensure_directory(Ebin),
    {Module, Binary, Warnings} = compile_binary(Source, Options),
    Beam = filename:join(Ebin, atom_to_list(Module) ++ ".beam"),
    write_file(Beam, Binary),
    #{module => Module, source => Source, source_md5 => md5_file(Source),
      beam => Beam, beam_md5 => md5(Binary), options => Options, warnings => Warnings}.

compile_binary(Source, Options) ->
    %% Upstream suites assert basenames in stack frames and compile metadata.
    %% Preserve the upstream make convention instead of compiling absolute paths.
    {ok, OriginalCwd} = file:get_cwd(),
    ok = file:set_cwd(filename:dirname(Source)),
    try
        CompileOptions = [binary, debug_info, return_errors, return_warnings | Options],
        case compile:file(filename:basename(Source), CompileOptions) of
            {ok, Module, Binary, Warnings} -> {Module, Binary, Warnings};
            {error, Errors, Warnings} ->
                erlang:error({full_candidate_compile_failed,
                              #{source => Source, errors => Errors, warnings => Warnings}});
            Other -> erlang:error({full_candidate_compile_failed,
                                   #{source => Source, result => Other}})
        end
    after
        ok = file:set_cwd(OriginalCwd)
    end.

include_options(RuntimeRoot) ->
    [{i, filename:join(RuntimeRoot, "lib")} |
     [{i, filename:join([RuntimeRoot, "lib", App, Dir])}
      || {App, Dir} <- [{"kernel", "src"}, {"kernel", "include"},
                        {"stdlib", "include"}, {"stdlib", "src"},
                        {"syntax_tools", "include"},
                        {"common_test", "include"}, {"snmp", "include"},
                        {"xmerl", "include"}, {"xmerl", "inc"},
                        {"eunit", "include"}, {"os_mon", "include"}]]].

check_runtime(RuntimeRoot, FlavorText, BuildTypeText) ->
    Flavor = case FlavorText of "emu" -> emu; "jit" -> jit end,
    Flavor = erlang:system_info(emu_flavor),
    Expected = case BuildTypeText of
                   "opt" -> {opt, false, false};
                   "debug" -> {debug, true, true}
               end,
    Expected = {erlang:system_info(build_type), erlang:system_info(debug_compiled),
                erlang:system_info(lock_checking)},
    nonode@nohost = node(),
    true = erlang:system_info(schedulers_online) >= 2,
    RuntimeRoot = filename:absname(code:root_dir()),
    Compiler = filename:join([RuntimeRoot, "lib", "compiler", "ebin", "compile.beam"]),
    Compiler = filename:absname(code:which(compile)),
    ok.

runtime_identity() ->
    #{version => erlang:system_info(version), otp_release => erlang:system_info(otp_release),
      flavor => erlang:system_info(emu_flavor), architecture => erlang:system_info(system_architecture),
      build_type => erlang:system_info(build_type),
      debug_compiled => erlang:system_info(debug_compiled),
      lock_checking => erlang:system_info(lock_checking),
      schedulers_online => erlang:system_info(schedulers_online), node => node(),
      root => code:root_dir(), bindir => os:getenv("BINDIR"),
      compiler => code:which(compile), compiler_md5 => compile:module_info(md5)}.

load_transform(Module, Beam) ->
    _ = code:purge(Module),
    _ = code:delete(Module),
    {module, Module} = code:load_abs(filename:rootname(Beam)),
    ok.

safe_relative(Value) ->
    Path = text(Value),
    relative = filename:pathtype(Path),
    [_ | _] = filename:split(Path),
    false = lists:any(fun(Segment) -> Segment =:= "." orelse Segment =:= ".." end,
                      filename:split(Path)),
    Path.

text(Binary) when is_binary(Binary) -> binary_to_list(Binary);
text(List) when is_list(List) -> List.

add_path(Path) ->
    true = code:add_patha(Path),
    ok.

ensure_suite_data_directories(CorpusRoot) ->
    filelib:fold_files(
      CorpusRoot, "_SUITE\\.erl$", false,
      fun(Path, ok) ->
              ensure_directory(filename:rootname(Path) ++ "_data"),
              ok
      end, ok).

ensure_directory(Path) ->
    case filelib:ensure_dir(filename:join(Path, ".directory")) of
        ok -> ok;
        Error -> erlang:error({cannot_create_full_test_directory, Path, Error})
    end.

read_file(Path) ->
    case file:read_file(Path) of
        {ok, Bytes} -> Bytes;
        Error -> erlang:error({cannot_read_full_test_input, Path, Error})
    end.

copy_file(Source, Destination) ->
    ensure_directory(filename:dirname(Destination)),
    write_file(Destination, read_file(Source)).

write_file(Path, Bytes) ->
    case file:write_file(Path, Bytes) of
        ok -> ok;
        Error -> erlang:error({cannot_write_full_test_output, Path, Error})
    end.

md5_file(Path) -> md5(read_file(Path)).
md5(Bytes) -> binary:encode_hex(erlang:md5(Bytes)).

write_evidence(Directory, Name, Evidence) ->
    write_file(filename:join(Directory, Name), io_lib:format("~tp.~n", [Evidence])).

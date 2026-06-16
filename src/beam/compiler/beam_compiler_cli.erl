%% SPDX-License-Identifier: Apache-2.0
%% Command policy for the isolated BEAM compiler host.
-module(beam_compiler_cli).
-export([main/1, run/1, bootstrap/1]).

main(Context) when is_map(Context) ->
    initialize(Context),
    command([unicode:characters_to_list(A) || A <- maps:get(arguments, Context)]).

%% Private build entry. Inspection artifacts need debug information; this
%% does not extend the installed compile command's option surface.
bootstrap(Context) when is_map(Context) ->
    initialize(Context),
    command([unicode:characters_to_list(A) || A <- maps:get(arguments, Context)], true).

run(Context) when is_map(Context) ->
    initialize(Context),
    Source = unicode:characters_to_list(maps:get(source, Context)),
    Result = beam_compiler_engine:source(Source, [], engine_context()),
    case Result of
        {ok, #{module:=Module,bytecode:=Bytes,warnings:=Warnings}} ->
            diagnostics(standard_io, Warnings, "warning: "), {ok, Module, Bytes};
        {error, Errors, Warnings} ->
            diagnostics(standard_io, Errors, ""), diagnostics(standard_io, Warnings, "warning: "), {error, 1};
        _ -> {error, 1}
    end.

initialize(Context) ->
    persistent_term:put(beam_compiler_context, Context),
    persistent_term:put(beam_compiler_origins, maps:get(origins, Context, [])),
    file:initialize(),
    ok.

engine_context() ->
    Context = persistent_term:get(beam_compiler_context),
    #{cwd=>unicode:characters_to_list(maps:get(cwd,Context))}.

command(Arguments) ->
    command(Arguments, false).

command(Arguments, Build) ->
    try
        Settings = parse(Arguments, #{sources => [], includes => [], macros => [],
                                      outdir => undefined, depfile => undefined,
                                      deterministic => false, no_transforms => false, werror => false,
                                      build => Build, debug_info => false}),
        Jobs = prepare(Settings),
        lists:foreach(fun(Job) -> compile_one(Job, Settings) end, Jobs),
        0
    catch
        throw:{compile_error, Message} ->
            io:format(standard_error, "beam compile: ~ts~n", [Message]),
            1;
        throw:compilation_failed ->
            1;
        Class:Reason ->
            io:format(standard_error, "beam compile: ~p:~tp~n", [Class, Reason]),
            1
    end.

parse([], Settings) ->
    Settings#{sources := lists:reverse(maps:get(sources, Settings)),
              includes := lists:reverse(maps:get(includes, Settings)),
              macros := lists:reverse(maps:get(macros, Settings))};
parse(["--" | Files], Settings) ->
    parse([], Settings#{sources := lists:reverse(Files, maps:get(sources, Settings))});
parse(["-o", Directory | Rest], Settings) ->
    parse(Rest, single_option(outdir, Directory, "-o", Settings));
parse(["--depfile", File | Rest], Settings) ->
    parse(Rest, single_option(depfile, File, "--depfile", Settings));
parse(["-I", Directory | Rest], Settings) ->
    parse(Rest, include_directory(Directory, Settings));
parse(["-I" ++ Directory | Rest], Settings) when Directory =/= [] ->
    parse(Rest, include_directory(Directory, Settings));
parse(["-D", Definition | Rest], Settings) ->
    parse(Rest, define(Definition, Settings));
parse(["-D" ++ Definition | Rest], Settings) when Definition =/= [] ->
    parse(Rest, define(Definition, Settings));
parse(["--warnings-as-errors" | Rest], Settings) ->
    parse(Rest, Settings#{werror := true});
parse(["--no-transforms" | Rest], Settings) ->
    parse(Rest, Settings#{no_transforms := true});
parse(["--deterministic" | Rest], Settings) ->
    parse(Rest, Settings#{deterministic := true});
parse(["--debug-info" | Rest], #{build := true} = Settings) ->
    parse(Rest, Settings#{debug_info := true});
parse([Option], _Settings)
  when Option =:= "-o"; Option =:= "-I"; Option =:= "-D"; Option =:= "--depfile" ->
    fail("~ts requires a value", [Option]);
parse([[ $- | _ ] = Option | _], _Settings) ->
    fail("unknown option ~ts", [Option]);
parse([File | Rest], Settings) ->
    parse(Rest, Settings#{sources := [File | maps:get(sources, Settings)]}).

single_option(Key, Value, Option, Settings) ->
    nonempty(Value, Option),
    case maps:get(Key, Settings) of
        undefined -> Settings#{Key := Value};
        _ -> fail("~ts may only be specified once", [Option])
    end.

include_directory(Directory, Settings) ->
    nonempty(Directory, "-I"),
    Settings#{includes := [Directory | maps:get(includes, Settings)]}.

define(Definition, Settings) ->
    {NameText, Value} = case lists:splitwith(fun(C) -> C =/= $= end, Definition) of
                          {Label, []} -> {Label, true};
                          {Label, [$= | Term]} -> {Label, literal(Term)}
                      end,
    Name = macro_name(NameText),
    Macros = maps:get(macros, Settings),
    case lists:keymember(Name, 1, Macros) of
        true -> fail("duplicate macro definition ~ts", [NameText]);
        false -> Settings#{macros := [{Name, Value} | Macros]}
    end.

macro_name(Text) ->
    case erl_scan:string(Text ++ ".") of
        {ok, [{Kind, _, Name}, {dot, _}], _} when Kind =:= atom; Kind =:= var -> Name;
        _ -> fail("invalid macro name ~ts", [Text])
    end.

literal(Text) ->
    case erl_scan:string(Text ++ ".") of
        {ok, Tokens, _} ->
            case erl_parse:parse_term(Tokens) of
                {ok, Term} -> Term;
                _ -> fail("macro value must be an Erlang literal term: ~ts", [Text])
            end;
        _ -> fail("macro value must be an Erlang literal term: ~ts", [Text])
    end.

nonempty([], Option) -> fail("~ts requires a nonempty value", [Option]);
nonempty(_, _) -> ok.

prepare(#{sources := []}) ->
    fail("requires at least one Erlang source file", []);
prepare(#{sources := Sources, outdir := Outdir0, depfile := Depfile}) ->
    case Depfile =/= undefined andalso length(Sources) =/= 1 of
        true -> fail("--depfile requires exactly one source file", []);
        false -> ok
    end,
    Outdir = case Outdir0 of undefined -> "."; _ -> Outdir0 end,
    Jobs = [job(Source, Outdir) || Source <- Sources],
    Outputs = [Output || {_, _, Output} <- Jobs],
    unique_outputs(Outputs, #{}),
    case Depfile of
        undefined -> ok;
        _ ->
            no_alias(Depfile, Sources ++ Outputs),
            no_newline(Depfile)
    end,
    Jobs.

job(Source, Directory) ->
    case filename:extension(Source) of
        ".erl" -> ok;
        _ -> fail("expected an explicit .erl source file: ~ts", [Source])
    end,
    Stem = filename:basename(Source, ".erl"),
    case Stem of
        [] -> fail("invalid source filename ~ts", [Source]);
        _ -> ok
    end,
    {Source, Stem, filename:join(Directory, Stem ++ ".beam")}.

unique_outputs([Output | Rest], Seen) ->
    Identity = canonical_path(Output),
    case maps:is_key(Identity, Seen) of
        true -> fail("conflicting output destination ~ts", [Output]);
        false -> unique_outputs(Rest, Seen#{Identity => true})
    end;
unique_outputs([], _) -> ok.

no_alias(Path, Others) ->
    Identity = canonical_path(Path),
    case lists:any(fun(Other) -> canonical_path(Other) =:= Identity end, Others) of
        true -> fail("dependency file conflicts with an input or output: ~ts", [Path]);
        false -> ok
    end.

%% Resolve symlinked ancestors even when the final output does not yet exist.
canonical_path(Path) ->
    resolve_path(filename:split(filename:absname(Path)), [], 0).

resolve_path(_, _, Links) when Links > 40 ->
    fail("too many symbolic links in an output path", []);
resolve_path([], Parts, _) -> filename:join(lists:reverse(Parts));
resolve_path(["." | Rest], Parts, Links) -> resolve_path(Rest, Parts, Links);
resolve_path([".." | Rest], [_ | [_ | _] = Parent], Links) ->
    resolve_path(Rest, Parent, Links);
resolve_path([".." | Rest], Parts, Links) -> resolve_path(Rest, Parts, Links);
resolve_path([Part | Rest], Parts, Links) ->
    Current = filename:join(lists:reverse([Part | Parts])),
    case file:read_link(Current) of
        {ok, Target} ->
            Absolute = filename:absname(Target, filename:dirname(Current)),
            resolve_path(filename:split(Absolute) ++ Rest, [], Links + 1);
        {error, _} -> resolve_path(Rest, [Part | Parts], Links)
    end.

compile_one({Source, Stem, Output}, Settings) ->
    Options = compiler_options(Settings),
    Context = persistent_term:get(beam_compiler_context),
    persistent_term:put(beam_compiler_context,Context#{source=>unicode:characters_to_binary(Source)}),
    {Binary, Dependencies} = case beam_compiler_engine:source(Source,Options,engine_context()) of
        {ok, #{module:=Module,bytecode:=Bytes,warnings:=Warnings,dependencies:=Inputs}} ->
            diagnostics(Warnings, "warning: "),
            check_module(Module,Stem,Source),
            {Bytes,Inputs};
        {error,Errors,Warnings} ->
            diagnostics(Errors,""), diagnostics(Warnings,"warning: "),
            throw(compilation_failed)
    end,
    Depfile = maps:get(depfile, Settings),
    DepBytes = case Depfile of
                   undefined -> undefined;
                   _ ->
                       no_alias(Depfile, Dependencies ++ [Output]),
                       depfile(Output, Dependencies)
               end,
    publish(Output, Binary, Depfile, DepBytes).

check_module(Module, Stem, Source) ->
    case atom_to_list(Module) of
        Stem -> ok;
        Actual -> fail("~ts declares module ~ts; expected ~ts", [Source, Actual, Stem])
    end.

compiler_options(Settings) ->
    [binary, return_errors, return_warnings] ++
        [{i, Directory} || Directory <- maps:get(includes, Settings)] ++
        [{d, Name, Value} || {Name, Value} <- maps:get(macros, Settings)] ++
        [deterministic || maps:get(deterministic, Settings)] ++
        [debug_info || maps:get(debug_info, Settings)] ++
        [warnings_as_errors || maps:get(werror, Settings)] ++
        [beam_no_transforms || maps:get(no_transforms, Settings)].

diagnostics(Groups, Prefix) ->
    diagnostics(standard_error, Groups, Prefix).

diagnostics(Device, Groups, Prefix) ->
    lists:foreach(
      fun({{File, _}, Messages}) -> diagnostic_group(Device, File, Messages, Prefix);
         ({File, Messages}) -> diagnostic_group(Device, File, Messages, Prefix)
      end, Groups).

diagnostic_group(Device, File, Messages, Prefix) ->
    lists:foreach(
      fun({_, Text}) -> io:put_chars(Device, Text) end,
      sys_messages:format_messages(File, Prefix, Messages, [])).

depfile(Output, Dependencies) ->
    %% A final literal backslash is quoted by the following word separator,
    %% not by the newline. The required .erl source supplies a final ordinary
    %% prerequisite, even when an included filename ends in a backslash.
    {TrailingBackslashes, Ordinary} =
        lists:partition(fun(Path) -> lists:last(Path) =:= $\\ end, Dependencies),
    unicode:characters_to_binary(
      [make_path(filename:absname(Output), target), ":",
       [[" ", make_path(Path, prerequisite)] || Path <- TrailingBackslashes ++ Ordinary], "\n"]).

make_path(Path, Kind) ->
    no_newline(Path),
    case lists:member($=, Path) of
        true -> fail("dependency rule paths cannot contain '=': ~ts; rename the path or omit --depfile",
                     [Path]);
        false -> ok
    end,
    make_characters(Path, Kind).

make_characters([$\\ | Rest], Kind) -> make_backslashes(Rest, 1, Kind);
make_characters([$$ | Rest], Kind) -> ["$$" | make_characters(Rest, Kind)];
make_characters([$; | Rest], Kind) ->
    [lists:duplicate(3, $\\), $; | make_characters(Rest, Kind)];
make_characters([C | Rest], Kind) ->
    case make_quoted(C, Kind) of
        true -> [$\\, C | make_characters(Rest, Kind)];
        false -> [C | make_characters(Rest, Kind)]
    end;
make_characters([], _) -> [].

make_quoted($%, target) -> true;
make_quoted($|, prerequisite) -> true;
make_quoted(C, _) -> lists:member(C, " \t#:*?[]").

%% Make keeps backslashes before ordinary characters verbatim. Before an
%% escaped separator each literal backslash needs another quoting backslash.
make_backslashes([$\\ | Rest], Count, Kind) -> make_backslashes(Rest, Count + 1, Kind);
%% Semicolons pass through both Make's inline-recipe and rule-word scanners.
make_backslashes([$; | Rest], Count, Kind) ->
    [lists:duplicate(4 * Count + 3, $\\), $; | make_characters(Rest, Kind)];
make_backslashes([C | Rest] = Tail, Count, Kind) ->
    case make_quoted(C, Kind) of
        true -> [lists:duplicate(2 * Count + 1, $\\), C | make_characters(Rest, Kind)];
        false -> [lists:duplicate(Count, $\\) | make_characters(Tail, Kind)]
    end;
make_backslashes([], Count, _) -> lists:duplicate(2 * Count, $\\).

no_newline(Path) ->
    case lists:any(fun(C) -> C =:= $\n orelse C =:= $\r end, Path) of
        true -> fail("dependency paths cannot contain newline characters", []);
        false -> ok
    end.

publish(Output, Binary, undefined, undefined) ->
    Temporary = stage(Output, Binary),
    try replace(Temporary, Output)
    after file:delete(Temporary)
    end;
publish(Output, Binary, Depfile, DepBytes) ->
    Temporary = stage(Output, Binary),
    try
        DepTemporary = stage(Depfile, DepBytes),
        try
            replace(DepTemporary, Depfile),
            replace(Temporary, Output)
        after file:delete(DepTemporary)
        end
    after file:delete(Temporary)
    end.

stage(Destination, Bytes) ->
    case beam_compiler_host:request(ensure_dir, [Destination]) of
        ok -> ok;
        {error, Reason} -> file_error("create output directory for", Destination, Reason)
    end,
    stage_unique(Destination, Bytes).

stage_unique(Destination, Bytes) ->
    Suffix = ".tmp-" ++ os:getpid() ++ "-" ++
        integer_to_list(erlang:unique_integer([positive, monotonic])),
    Temporary = Destination ++ Suffix,
    case file:open(Temporary, [write, binary, raw, exclusive]) of
        {ok, File} ->
            Written = file:write(File, Bytes),
            Closed = file:close(File),
            case {Written, Closed} of
                {ok, ok} -> Temporary;
                {{error, Reason}, _} ->
                    file:delete(Temporary),
                    file_error("write", Temporary, Reason);
                {_, {error, Reason}} ->
                    file:delete(Temporary),
                    file_error("close", Temporary, Reason)
            end;
        {error, eexist} -> stage_unique(Destination, Bytes);
        {error, Reason} -> file_error("create", Temporary, Reason)
    end.

replace(Temporary, Destination) ->
    case file:rename(Temporary, Destination) of
        ok -> ok;
        {error, Reason} -> file_error("publish", Destination, Reason)
    end.

file_error(Operation, Path, Reason) ->
    fail("cannot ~s ~ts: ~ts", [Operation, Path, file:format_error(Reason)]).

fail(Format, Arguments) ->
    throw({compile_error, io_lib:format(Format, Arguments)}).

%% SPDX-License-Identifier: Apache-2.0
%% Only declared compiler modules and explicit client paths can supply code.
-module(code).
-export([root_dir/0,lib_dir/0,lib_dir/1,lib_dir/2,priv_dir/1,
         which/1,is_loaded/1,all_loaded/0,get_path/0,ensure_loaded/1,
         ensure_modules_loaded/1,load_binary/3,load_file/1,get_object_code/1,
         is_sticky/1,delete/1,purge/1]).
root_dir() -> unicode:characters_to_list(beam_compiler_host:context(root)).
lib_dir() -> filename:join([root_dir(),"lib","beam","apps"]).
lib_dir(App) when is_atom(App) -> lib_dir(atom_to_list(App));
lib_dir(App) when is_list(App) ->
    Path=filename:join(lib_dir(),App),
    case file:read_file_info(Path) of
        {ok,Info} when element(3,Info)=:=directory -> Path;
        _ -> {error,bad_name}
    end.
lib_dir(App,Part) ->
    case lib_dir(App) of {error,_}=Error->Error; Path->filename:join(Path,atom_to_list(Part)) end.
priv_dir(App) -> lib_dir(App,priv).
origins() -> persistent_term:get(beam_compiler_origins,[]).
which(Module) ->
    case lists:member(Module,erlang:pre_loaded()) of
        true -> preloaded;
        false ->
            case lists:keyfind(Module,1,origins()) of
                {Module,Path}->unicode:characters_to_list(Path);
                false->non_existing
            end
    end.
is_loaded(Module) ->
    case erlang:module_loaded(Module) of
        false->false;
        true->{file,which(Module)}
    end.
all_loaded() -> [{M,which(M)} || M <- erlang:loaded(),erlang:module_loaded(M)].
get_path() ->
    Context=persistent_term:get(beam_compiler_context),
    Cwd=unicode:characters_to_list(maps:get(cwd,Context)),
    Source=case maps:get(source,Context,undefined) of
               undefined -> Cwd;
               Name -> filename:dirname(unicode:characters_to_list(Name))
           end,
    lists:uniq([Cwd,filename:absname(Source,Cwd),filename:join([root_dir(),"lib","beam","ebin"])]).
ensure_loaded(Module) ->
    case erlang:module_loaded(Module) of true->{module,Module}; false->load_file(Module) end.
ensure_modules_loaded(Modules) ->
    Errors=[{M,E} || M<-Modules,{error,E}<-[ensure_loaded(M)]],
    case Errors of []->ok; _->{error,Errors} end.
load_file(Module) ->
    case get_object_code(Module) of
        {Module,Bytes,Path}->load_binary(Module,Path,Bytes);
        error->{error,nofile}
    end.
get_object_code(Module) ->
    Paths=case which(Module) of
              Path when is_list(Path)->[Path];
              _->[filename:join(Dir,atom_to_list(Module)++".beam") || Dir<-get_path()]
          end,
    object_code(Module,Paths).
object_code(_,[]) -> error;
object_code(Module,[Path|Rest]) ->
    case file:read_file(Path) of
        {ok,Bytes}->{Module,Bytes,Path};
        _->object_code(Module,Rest)
    end.
load_binary(Module,Path,Bytes) ->
    case publish(Module,Bytes) of
        {module,Module}=Result ->
            Others=lists:keydelete(Module,1,origins()),
            persistent_term:put(beam_compiler_origins,[{Module,unicode:characters_to_binary(Path)}|Others]),
            Result;
        Error->Error
    end.
is_sticky(Module) -> lists:member(Module,erlang:pre_loaded()).
delete(Module) -> erlang:delete_module(Module).
purge(Module) -> erlang:purge_module(Module).

publish(Module,Bytes) ->
    case erlang:prepare_loading(Module,Bytes) of
        {error,_}=Error -> Error;
        Prepared ->
            case erlang:has_prepared_code_on_load(Prepared) of
                true -> {error,on_load};
                false ->
                    case erlang:finish_loading([Prepared]) of
                        ok -> {module,Module};
                        {Reason,_} -> {error,Reason}
                    end
            end
    end.

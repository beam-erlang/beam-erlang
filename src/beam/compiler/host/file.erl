%% SPDX-License-Identifier: Apache-2.0
%% Source handles are compiler-owned byte buffers; publication is host-owned.
-module(file).
-export([initialize/0,open/2,close/1,read/2,write/2,position/2,
         read_file/1,write_file/2,write_file/3,path_open/3,
         get_cwd/0,get_cwd/1,read_link/1,read_file_info/1,read_file_info/2,
         read_link_info/1,read_link_info/2,rename/2,delete/1,make_dir/1,list_dir/1,
         format_error/1,consult/1,handle/1,update/2,read_chars/1,consume_chars/2,
         native_name_encoding/0]).
initialize() ->
    case ets:whereis(beam_compiler_files) of
        undefined -> ets:new(beam_compiler_files, [named_table, public, set]);
        _ -> ok
    end, ok.
open(Path, Modes) ->
    Writing = lists:member(write, Modes) orelse lists:member(append, Modes),
    case Writing of
        false ->
            case read_file(Path) of
                {ok, Bytes} -> new_handle(Path, Modes, Bytes, false);
                Error -> Error
            end;
        true ->
            case beam_compiler_host:request(open_write,[Path,Modes]) of
                {ok,Token} ->
                    {ok,Fd}=new_handle(Path,Modes,<<>>,true),
                    update(Fd,(handle(Fd))#{token=>Token}),
                    {ok,Fd};
                Error -> Error
            end
    end.
new_handle(Path, Modes, Bytes, Writing) ->
    Ref = make_ref(),
    State = #{path=>Path,data=>Bytes,position=>0,writing=>Writing,
              binary=>lists:member(binary, Modes),encoding=>latin1},
    ets:insert(beam_compiler_files, {Ref, State}),
    {ok, {beam_compiler_file,Ref}}.
handle({beam_compiler_file,Ref}) ->
    case ets:lookup(beam_compiler_files, Ref) of
        [{_, State}] -> State;
        [] -> erlang:error(badarg)
    end.
update({beam_compiler_file,Ref}, State) -> ets:insert(beam_compiler_files,{Ref,State}), ok.
close(Fd) ->
    State = handle(Fd),
    Result = case State of
                 #{writing:=true,token:=Token} -> beam_compiler_host:request(close_handle,[Token]);
                 _ -> ok
             end,
    {beam_compiler_file,Ref}=Fd,
    ets:delete(beam_compiler_files,Ref), Result.
read(Fd, Count) when is_integer(Count), Count >= 0 ->
    #{data:=Bytes,position:=Position,binary:=Binary}=State=handle(Fd),
    Size = min(Count, max(0,byte_size(Bytes)-Position)),
    case {Size,Count} of
        {0,N} when N > 0 -> eof;
        _ ->
            Part = binary:part(Bytes,Position,Size),
            update(Fd,maps:remove(decoded,State#{position:=Position+Size})),
            {ok,case Binary of true -> Part; false -> binary_to_list(Part) end}
    end.
write(Fd, Data) ->
    #{writing:=true,token:=Token}=handle(Fd),
    beam_compiler_host:request(write_handle,[Token,iolist_to_binary(Data)]).
position(Fd, At) ->
    case handle(Fd) of
        #{writing:=true} -> {error,enotsup};
        State -> read_position(Fd,At,State)
    end.
read_position(Fd, At, State) ->
    #{data:=Bytes,position:=Old}=State,
    New=case At of
            cur -> Old; bof -> 0; eof -> byte_size(Bytes);
            {cur,N} -> Old+N; {bof,N} -> N; {eof,N} -> byte_size(Bytes)+N;
            N when is_integer(N) -> N
        end,
    case New >= 0 andalso New =< byte_size(Bytes) of
        true -> update(Fd,maps:remove(decoded,State#{position:=New})), {ok,New};
        false -> {error,einval}
    end.
read_chars(Fd) ->
    case handle(Fd) of
        #{decoded:=Chars} -> Chars;
        #{data:=Bytes,position:=Position,encoding:=Encoding}=State ->
            Chars=unicode:characters_to_list(binary:part(Bytes,Position,byte_size(Bytes)-Position),Encoding),
            update(Fd,State#{decoded=>Chars}),
            Chars
    end.
consume_chars(Fd, Chars) ->
    #{position:=Position,encoding:=Encoding,decoded:=Remaining}=State=handle(Fd),
    Bytes=unicode:characters_to_binary(Chars,unicode,Encoding),
    Rest=lists:nthtail(length(Chars),Remaining),
    update(Fd,State#{position:=Position+byte_size(Bytes),decoded:=Rest}).
read_file(Path) -> beam_compiler_host:request(read_file,[Path]).
write_file(Path,Bytes) -> write_file(Path,Bytes,[]).
write_file(Path,Bytes,Modes) -> beam_compiler_host:request(write_file,[Path,iolist_to_binary(Bytes),Modes]).
path_open(Paths,Name,Modes) ->
    case filename:pathtype(Name) of
        absolute -> case open(Name,Modes) of {ok,Fd}->{ok,Fd,Name}; Error->Error end;
        _ -> search(Paths,Name,Modes,{error,enoent})
    end.
search([],_,_,Error) -> Error;
search([Dir|Rest],Name,Modes,_) ->
    Path=filename:join(Dir,Name),
    case open(Path,Modes) of
        {ok,Fd}->{ok,Fd,Path};
        Error->search(Rest,Name,Modes,Error)
    end.
get_cwd() -> {ok,unicode:characters_to_list(beam_compiler_host:context(cwd))}.
get_cwd(_) -> get_cwd().
read_link(Path) -> beam_compiler_host:request(read_link,[Path]).
read_file_info({beam_compiler_file,_}=Fd) ->
    case handle(Fd) of
        #{writing:=true,token:=Token} -> beam_compiler_host:request(stat_handle,[Token]);
        #{path:=Path} -> read_file_info(Path)
    end;
read_file_info(Path) -> beam_compiler_host:request(read_file_info,[Path]).
read_file_info(Path,_) -> read_file_info(Path).
read_link_info(Path) -> beam_compiler_host:request(read_link_info,[Path]).
read_link_info(Path,_) -> read_link_info(Path).
rename(From,To) -> beam_compiler_host:request(rename,[From,To]).
delete(Path) -> beam_compiler_host:request(delete,[Path]).
make_dir(Path) -> beam_compiler_host:request(make_dir,[Path]).
list_dir(Path) -> beam_compiler_host:request(list_dir,[Path]).
format_error(Reason) -> erl_posix_msg:message(Reason).
native_name_encoding() -> erlang:system_info(filename_encoding).
consult(Path) ->
    case read_file(Path) of
        {ok,Bytes} ->
            case erl_scan:string(unicode:characters_to_list(Bytes)) of
                {ok,Tokens,_} -> consult_tokens(Tokens,[],[]);
                {error,Error,_} -> {error,Error}
            end;
        Error -> Error
    end.
consult_tokens([],[],Terms) -> {ok,lists:reverse(Terms)};
consult_tokens([],_,_) -> {error,{0,erl_parse,"unexpected end of file"}};
consult_tokens([{dot,_}=Dot|Rest],Current,Terms) ->
    case erl_parse:parse_term(lists:reverse([Dot|Current])) of
        {ok,Term}->consult_tokens(Rest,[],[Term|Terms]);
        Error->Error
    end;
consult_tokens([Token|Rest],Current,Terms) -> consult_tokens(Rest,[Token|Current],Terms).

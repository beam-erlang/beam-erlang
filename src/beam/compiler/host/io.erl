%% SPDX-License-Identifier: Apache-2.0
%% Compiler diagnostics and source scanning use explicit host and buffer APIs.
-module(io).
-export([format/1,format/2,format/3,fwrite/1,fwrite/2,fwrite/3,
         put_chars/1,put_chars/2,put_chars/3,getopts/0,getopts/1,setopts/1,setopts/2,
         scan_erl_form/2,scan_erl_form/3,scan_erl_form/4,columns/0,columns/1,
         rows/0,rows/1]).
format(Format) -> format(Format,[]).
format(Format,Args) -> format(standard_io,Format,Args).
format(Device,Format,Args) -> put_chars(Device,io_lib:format(Format,Args)).
fwrite(Format) -> format(Format).
fwrite(Format,Args) -> format(Format,Args).
fwrite(Device,Format,Args) -> format(Device,Format,Args).
put_chars(Chars) -> put_chars(standard_io,Chars).
put_chars(Device,Chars) -> put_chars(Device,unicode,Chars).
put_chars({beam_compiler_file,_}=Fd,Encoding,Chars) ->
    file:write(Fd,unicode:characters_to_binary(Chars,Encoding,utf8));
put_chars(Device,Encoding,Chars) ->
    Destination=case Device of standard_error->standard_error; _->standard_output end,
    beam_compiler_host:request(output,[Destination,unicode:characters_to_binary(Chars,Encoding,utf8)]).
getopts() -> getopts(standard_io).
getopts({beam_compiler_file,_}=Fd) ->
    State=file:handle(Fd),[{binary,maps:get(binary,State)},{encoding,maps:get(encoding,State)}];
getopts(_) -> [{binary,false},{encoding,unicode}].
setopts(Options) -> setopts(standard_io,Options).
setopts({beam_compiler_file,_}=Fd,Options) ->
    State=lists:foldl(fun(binary,S)->S#{binary:=true};
                        (list,S)->S#{binary:=false};
                        ({binary,B},S)->S#{binary:=B};
                        ({encoding,E},S)->S#{encoding:=E}
                     end,file:handle(Fd),Options),
    file:update(Fd,maps:remove(decoded,State));
setopts(_,_) -> ok.
scan_erl_form(Fd,Prompt) -> scan_erl_form(Fd,Prompt,1,[]).
scan_erl_form(Fd,Prompt,Location) -> scan_erl_form(Fd,Prompt,Location,[]).
scan_erl_form(Fd,_Prompt,Location,Options) ->
    scan_form(Fd,[],Location,Options).

scan_form(Fd,Continuation,Location,Options) ->
    case file:read_chars(Fd) of
        [] ->
            {done,Result,eof}=erl_scan:tokens(Continuation,eof,Location,Options),
            Result;
        Chars when is_list(Chars) ->
            Chunk=lists:sublist(Chars,4096),
            case erl_scan:tokens(Continuation,Chunk,Location,Options) of
                {done,Result,Rest} ->
                    Used=lists:sublist(Chunk,length(Chunk)-length(Rest)),
                    file:consume_chars(Fd,Used), Result;
                {more,Next} ->
                    file:consume_chars(Fd,Chunk),
                    scan_form(Fd,Next,Location,Options)
            end;
        _ ->
            #{data:=Bytes}=State=file:handle(Fd),
            file:update(Fd,State#{position:=byte_size(Bytes),decoded=>[]}),
            {error,{Location,file,invalid_unicode},Location}
    end.

columns() -> {error,enotsup}.
columns(_) -> columns().
rows() -> {error,enotsup}.
rows(_) -> rows().

%%
%% %CopyrightBegin%
%%
%% SPDX-License-Identifier: Apache-2.0
%%
%% Copyright Ericsson AB 1996-2025. All Rights Reserved.
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
-module(io).
%% Local Unicode I/O protocol client for private BEAM CLI compositions.
-export([put_chars/1,put_chars/2,nl/0,nl/1,write/1,write/2,
         format/1,format/2,format/3,fwrite/1,fwrite/2,fwrite/3,
         get_chars/2,get_chars/3,get_line/1,get_line/2,
         setopts/1,setopts/2,getopts/0,getopts/1,
         columns/0,columns/1,rows/0,rows/1,
         request/1,request/2,requests/1,requests/2,printable_range/0]).
-export([read/1,read/2,read/3,read/4,fread/2,fread/3,
         scan_erl_exprs/1,scan_erl_exprs/2,scan_erl_exprs/3,scan_erl_exprs/4,
         scan_erl_form/1,scan_erl_form/2,scan_erl_form/3,scan_erl_form/4,
         parse_erl_exprs/1,parse_erl_exprs/2,parse_erl_exprs/3,parse_erl_exprs/4,
         parse_erl_form/1,parse_erl_form/2,parse_erl_form/3,parse_erl_form/4]).
-compile({inline,[o_request/2]}).

o_request(Function, OrigArgs) ->
    {Io, Request} =
        if
            Function =:= format; Function =:= fwrite ->
                case OrigArgs of
                    [Format] ->
                        {default_output(), {format, Format, []}};
                    [Format, Args] ->
                        {default_output(), {format, Format, Args}};
                    [D, Format, Args] ->
                        {D, {format, Format, Args}}
                end;
            Function =:= put_chars ->
                case OrigArgs of
                    [Chars] ->
                        {default_output(), {put_chars, unicode, Chars}};
                    [D, Chars] ->
                        {D, {put_chars, unicode, Chars}};
                    [D, Encoding, Chars] ->
                        {D, {put_chars, Encoding, Chars}}
                end;
            Function =:= nl ->
                case OrigArgs of
                    [] ->
                        {default_output(), nl};
                    [D] ->
                        {D, nl}
                end;
            Function =:= write ->
                case OrigArgs of
                    [Term] ->
                        {default_output(), {write, Term}};
                    [D, Term] ->
                        {D, {write, Term}}
                end
        end,
    ErrorRef = make_ref(),
    case request(Io, Request, ErrorRef) of
        {ErrorRef, Reason} ->
            %% We differentiate between errors that are created by this module
            erlang:error(conv_reason(Reason), OrigArgs,
                         [{error_info,#{cause => {?MODULE, Reason},
                                        module => erl_stdlib_errors}}]);
        {error, Reason} ->
            %% and the errors we get from the Device
            erlang:error(conv_reason(Reason), OrigArgs,
                         [{error_info,#{cause => {device, Reason},
                                        module => erl_stdlib_errors}}]);
        Other ->
            Other
    end.


%% Implemented by the VM's ordinary io BIF.
printable_range() -> erlang:nif_error(undefined).
put_chars(Chars) -> o_request(?FUNCTION_NAME, [Chars]).
put_chars(Device, Chars) -> o_request(?FUNCTION_NAME, [Device, Chars]).
nl() -> o_request(?FUNCTION_NAME, []).
nl(Device) -> o_request(?FUNCTION_NAME, [Device]).
write(Term) -> o_request(?FUNCTION_NAME, [Term]).
write(Device, Term) -> o_request(?FUNCTION_NAME, [Device, Term]).
format(Format) -> o_request(?FUNCTION_NAME, [Format]).
format(Format, Args) -> o_request(?FUNCTION_NAME, [Format, Args]).
format(Device, Format, Args) -> o_request(?FUNCTION_NAME, [Device, Format, Args]).
fwrite(Format) -> o_request(?FUNCTION_NAME, [Format]).
fwrite(Format, Args) -> o_request(?FUNCTION_NAME, [Format, Args]).
fwrite(Device, Format, Args) -> o_request(?FUNCTION_NAME, [Device, Format, Args]).
get_chars(Prompt, Count) -> get_chars(group_leader(), Prompt, Count).
get_chars(Device, Prompt, Count) when is_integer(Count), Count >= 0 ->
    request(Device, {get_chars,unicode,Prompt,Count}).
get_line(Prompt) -> get_line(group_leader(), Prompt).
get_line(Device, Prompt) -> request(Device, {get_line,unicode,Prompt}).
getopts() -> getopts(group_leader()).
getopts(Device) -> request(Device, getopts).
setopts(Options) -> setopts(group_leader(), Options).
setopts(Device, Options) -> request(Device, {setopts,Options}).
columns() -> columns(group_leader()).
columns(Device) -> geometry(Device, columns).
rows() -> rows(group_leader()).
rows(Device) -> geometry(Device, rows).
geometry(Device, Dimension) ->
    case request(Device, {get_geometry,Dimension}) of
        Value when is_integer(Value), Value > 0 -> {ok,Value};
        _ -> {error,enotsup}
    end.

conv_reason(arguments) -> badarg;
conv_reason(terminated) -> terminated;
conv_reason(calling_self) -> calling_self;
conv_reason({no_translation,_,_}) -> no_translation;
conv_reason(_) -> badarg.

request(Request) ->
    request(default_output(), Request).

-doc false.
request(Name, Request) ->
    request(Name, Request, error).
request(standard_io, Request, ErrorTag) ->
    request(group_leader(), Request, ErrorTag);
request(Pid, Request, ErrorTag) when is_pid(Pid), node(Pid) =:= node() ->
    execute_request(Pid, io_request(Pid, Request), ErrorTag);
request(Pid, _Request, ErrorTag) when is_pid(Pid) ->
    {ErrorTag,enotsup};
request({Name, Node}, _Request, ErrorTag) when is_atom(Name), is_atom(Node) ->
    {ErrorTag,enotsup};
request(Name, Request, ErrorTag) when is_atom(Name) ->
    case whereis(Name) of
	undefined ->
	    {ErrorTag, arguments};
	Pid ->
	    request(Pid, Request, ErrorTag)
    end;
request(_, _, ErrorTag) ->
    {ErrorTag, arguments}.

execute_request(Pid, _Tuple, ErrorTag) when Pid =:= self() ->
    {ErrorTag, calling_self};
execute_request(Pid, {Convert,Converted}, ErrorTag) ->
    Mref = erlang:monitor(process, Pid),
    Pid ! {io_request,self(),Mref,Converted},

    receive
	{io_reply, Mref, Reply} ->
	    erlang:demonitor(Mref, [flush]),
	    if
		Convert ->
		    convert_binaries(Reply);
		true ->
		    Reply
	    end;
	{'DOWN', Mref, _, _, _} ->
	    receive
		{'EXIT', Pid, _What} -> true
	    after 0 -> true
	    end,
	    {ErrorTag,terminated}
    end.

-doc false.
requests(Requests) ->				%Requests as atomic action
    requests(default_output(), Requests).

-doc false.
requests(standard_io, Requests) ->              %Requests as atomic action
    requests(group_leader(), Requests);
requests(Pid, Requests) when is_pid(Pid), node(Pid) =:= node() ->
    {Convert, Converted} = io_requests(Pid, Requests),
    execute_request(Pid,{Convert,{requests,Converted}},error);
requests(Pid, _Requests) when is_pid(Pid) ->
    {error,enotsup};
requests({Name, Node}, _Requests) when is_atom(Name), is_atom(Node) ->
    {error,enotsup};
requests(Name, Requests) when is_atom(Name) ->
    case whereis(Name) of
	undefined ->
	    {error, arguments};
	Pid ->
	    requests(Pid, Requests)
    end;
requests(_, _) ->
    {error, arguments}.


default_output() ->
    group_leader().

%% io_requests(Requests)
%%  Transform requests into correct i/o server messages. Only handle the
%%  one we KNOW must be changed, others, including incorrect ones, are
%%  passed straight through. Perform a flatten on the request list.

io_requests(Pid, Rs) ->
    io_requests(Pid, Rs, [], []).

io_requests(Pid, [{requests,Rs1}|Rs], Cont, Tail) ->
    io_requests(Pid, Rs1, [Rs|Cont], Tail);
io_requests(Pid, [R], [], _Tail) ->
    {Conv,Request} = io_request(Pid, R),
    {Conv,[Request]};
io_requests(Pid, [R|Rs], Cont, Tail) ->
    {_,Request} = io_request(Pid, R),
    {Conv,Requests} = io_requests(Pid, Rs, Cont, Tail),
    {Conv,[Request|Requests]};
io_requests(Pid, [], [Rs|Cont], Tail) ->
    io_requests(Pid, Rs, Cont, Tail);
io_requests(_Pid, [], [], _Tail) ->
    {false,[]}.

%% Every supported device speaks the Unicode protocol. There is no
%% distributed-node capability negotiation or legacy binary translation.
io_request(_Pid, {write,Term}) ->
    {false,{put_chars,unicode,io_lib,write,[Term]}};
io_request(_Pid, {format,Format,Args}) ->
    {false,{put_chars,unicode,io_lib,format,[Format,Args]}};
io_request(_Pid, {fwrite,Format,Args}) ->
    {false,{put_chars,unicode,io_lib,fwrite,[Format,Args]}};
io_request(_Pid, nl) ->
    {false,{put_chars,unicode,io_lib:nl()}};
io_request(_Pid, Request) -> {false,Request}.

convert_binaries(Reply) -> Reply.

%% The device decides whether term collection belongs to its composition.
%% Parser calls happen only after a successful collector reply; source run
%% rejects these requests without acquiring the interactive parser inventory.
read(Prompt) -> read(group_leader(), Prompt).
read(Device, Prompt) ->
    case read(Device, Prompt, 1, []) of
        {ok, Term, _} -> {ok, Term};
        {error, Error, _} -> {error, Error};
        {eof, _} -> eof;
        Other -> Other
    end.
read(Device, Prompt, Pos) -> read(Device, Prompt, Pos, []).
read(Device, Prompt, Pos, Options) ->
    parse_input(parse_term, Device, Prompt, Pos, Options).
fread(Prompt, Format) -> fread(group_leader(), Prompt, Format).
fread(Device, Prompt, Format) -> request(Device, {fread, Prompt, Format}).
scan_erl_exprs(Prompt) -> scan_erl_exprs(group_leader(), Prompt).
scan_erl_exprs(Device, Prompt) -> scan_erl_exprs(Device, Prompt, 1).
scan_erl_exprs(Device, Prompt, Pos) -> scan_erl_exprs(Device, Prompt, Pos, []).
scan_erl_exprs(Device, Prompt, Pos, Options) ->
    request(Device, {get_until, unicode, Prompt, erl_scan, tokens, [Pos, Options]}).
scan_erl_form(Prompt) -> scan_erl_form(group_leader(), Prompt).
scan_erl_form(Device, Prompt) -> scan_erl_form(Device, Prompt, 1).
scan_erl_form(Device, Prompt, Pos) -> scan_erl_form(Device, Prompt, Pos, []).
scan_erl_form(Device, Prompt, Pos, Options) ->
    request(Device, {get_until, unicode, Prompt, erl_scan, tokens, [Pos, Options]}).
parse_erl_exprs(Prompt) -> parse_erl_exprs(group_leader(), Prompt).
parse_erl_exprs(Device, Prompt) -> parse_erl_exprs(Device, Prompt, 1).
parse_erl_exprs(Device, Prompt, Pos) -> parse_erl_exprs(Device, Prompt, Pos, []).
parse_erl_exprs(Device, Prompt, Pos, Options) ->
    parse_input(parse_exprs, Device, Prompt, Pos, Options).
parse_erl_form(Prompt) -> parse_erl_form(group_leader(), Prompt).
parse_erl_form(Device, Prompt) -> parse_erl_form(Device, Prompt, 1).
parse_erl_form(Device, Prompt, Pos) -> parse_erl_form(Device, Prompt, Pos, []).
parse_erl_form(Device, Prompt, Pos, Options) ->
    parse_input(parse_form, Device, Prompt, Pos, Options).
parse_input(Function, Device, Prompt, Pos, Options) ->
    case scan_erl_exprs(Device, Prompt, Pos, Options) of
        {ok, Tokens, End} ->
            case apply(erl_parse, Function, [Tokens]) of
                {ok, Value} -> {ok, Value, End};
                {error, Reason} -> {error, Reason, End}
            end;
        Other -> Other
    end.

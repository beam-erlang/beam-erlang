%% SPDX-License-Identifier: Apache-2.0
-module(beam_host).
-export([request/1]).

%% Optional support for a bare VM's registered C host handler.
%% The request waits until an owner-thread beam_vm_call services it.
-spec request(term()) -> {ok, term()} | {error, term()}.
request(Request) ->
    erts_internal:host_request(Request).

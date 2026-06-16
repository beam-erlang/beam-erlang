%% SPDX-License-Identifier: Apache-2.0
%% The BEAM shell's explicit helpers. Language/history helpers stay in shell.
-module(shell_default).
-export([help/0, q/0, '$handle_undefined_function'/2]).
help() -> shell:help().
q() -> beam_interactive:finish(0).
'$handle_undefined_function'(Function, Arguments) ->
    beam_interactive:undefined_function(?MODULE, Function, Arguments).

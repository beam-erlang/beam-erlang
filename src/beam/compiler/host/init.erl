%% SPDX-License-Identifier: Apache-2.0
%% Compiler configuration is supplied with each explicit host invocation.
-module(init).
-export([get_argument/1,get_arguments/0,get_plain_arguments/0,
         objfile_extension/0,archive_extension/0]).
get_argument(root) -> {ok, [[unicode:characters_to_list(beam_compiler_host:context(root))]]};
get_argument(_) -> error.
get_arguments() -> [].
get_plain_arguments() -> [unicode:characters_to_list(A) || A <- beam_compiler_host:context(arguments)].
objfile_extension() -> ".beam".
archive_extension() -> ".ez".

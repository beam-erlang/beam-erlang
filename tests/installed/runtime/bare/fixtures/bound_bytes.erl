%% SPDX-License-Identifier: Apache-2.0
-module('bound.bytes').
-export([repeat/2,referenced_size/1,cut/2, flatten/1, version/0, seek/2, seek/3, ranges/2,has/2,back/2,lookup/2,insert/3,valid/1,utf8/2,chars/2]).
-export([a_new/2,a_get/2,a_put/3,a_add/3,a_add_get/3,a_exchange/3,a_compare_exchange/4,a_info/1,c_new/2,c_get/2,c_put/3,c_add/3,c_info/1,p_put/2,p_put_new/2,p_get/0,p_get/1,p_get/2,p_erase/1,p_info/0]).

-ifndef(OMIT_THREE).
-export([cut/3]).
cut(_, _, _) -> erlang:nif_error(undef).
-endif.
cut(_, _) -> erlang:nif_error(undef).
flatten(_) -> erlang:nif_error(undef).
repeat(_,_) -> erlang:nif_error(undef).
referenced_size(_) -> erlang:nif_error(undef).
version() -> ?GENERATION.
seek(_, _) -> erlang:nif_error(undef).
seek(_, _, _) -> erlang:nif_error(undef).
ranges(_, _) -> erlang:nif_error(undef).

has(_,_) -> erlang:nif_error(undef).
back(_,_) -> erlang:nif_error(undef).
lookup(_,_) -> erlang:nif_error(undef).
insert(_,_,_) -> erlang:nif_error(undef).
valid(_) -> erlang:nif_error(undef).
utf8(_,_) -> erlang:nif_error(undef).
chars(_,_) -> erlang:nif_error(undef).

a_new(_,_) -> erlang:nif_error(undef).
a_get(_,_) -> erlang:nif_error(undef).
a_put(_,_,_) -> erlang:nif_error(undef).
a_add(_,_,_) -> erlang:nif_error(undef).
a_add_get(_,_,_) -> erlang:nif_error(undef).
a_exchange(_,_,_) -> erlang:nif_error(undef).
a_compare_exchange(_,_,_,_) -> erlang:nif_error(undef).
a_info(_) -> erlang:nif_error(undef).
c_new(_,_) -> erlang:nif_error(undef).
c_get(_,_) -> erlang:nif_error(undef).
c_put(_,_,_) -> erlang:nif_error(undef).
c_add(_,_,_) -> erlang:nif_error(undef).
c_info(_) -> erlang:nif_error(undef).
p_put(_,_) -> erlang:nif_error(undef).
p_put_new(_,_) -> erlang:nif_error(undef).
p_get() -> erlang:nif_error(undef).
p_get(_) -> erlang:nif_error(undef).
p_get(_,_) -> erlang:nif_error(undef).
p_erase(_) -> erlang:nif_error(undef).
p_info() -> erlang:nif_error(undef).

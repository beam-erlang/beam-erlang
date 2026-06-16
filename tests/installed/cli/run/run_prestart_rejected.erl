%% SPDX-License-Identifier: Apache-2.0
-module(run_prestart_rejected).
-on_load(activate/0).

activate() -> erlang:error(prestart_on_load_must_not_execute).

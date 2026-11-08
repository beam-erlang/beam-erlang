%% SPDX-License-Identifier: Apache-2.0
%% Copyright Ericsson AB 1996-2026. All Rights Reserved.
%% Shared arithmetic; the including module owns error identity.
?BEAM_TIME_CONVERT(Time, FromUnit, ToUnit) ->
    try
	FU = case FromUnit of
		 native -> erts_internal:time_unit();
                 perf_counter -> erts_internal:perf_counter_unit();
		 nanosecond -> 1000*1000*1000;
		 microsecond -> 1000*1000;
		 millisecond -> 1000;
		 second -> 1;

		 %% Deprecated symbolic units...
		 nano_seconds -> 1000*1000*1000;
		 micro_seconds -> 1000*1000;
		 milli_seconds -> 1000;
		 seconds -> 1;

		 _ when FromUnit > 0 -> FromUnit
	     end,
	TU = case ToUnit of
		 native -> erts_internal:time_unit();
                 perf_counter -> erts_internal:perf_counter_unit();
		 nanosecond -> 1000*1000*1000;
		 microsecond -> 1000*1000;
		 millisecond -> 1000;
		 second -> 1;

		 %% Deprecated symbolic units...
		 nano_seconds -> 1000*1000*1000;
		 micro_seconds -> 1000*1000;
		 milli_seconds -> 1000;
		 seconds -> 1;

		 _ when ToUnit > 0 -> ToUnit
	     end,
	case Time < 0 of
	    true -> TU*Time - (FU - 1);
	    false -> TU*Time
	end div FU
    catch
	_ : _ ->
	    ?BEAM_TIME_CONVERT_ERROR([Time, FromUnit, ToUnit])
    end.
-undef(BEAM_TIME_CONVERT).
-undef(BEAM_TIME_CONVERT_ERROR).

"""Reviewed complete suite and grouped-occurrence inventories for VM integration."""

import json
from pathlib import Path

_native = json.loads((Path(__file__).with_name("native-selections.json")).read_text())
NATIVE_SUITES = _native["suites"]

CASES = {
    "atomics_SUITE": ["signed", "unsigned", "bad", "signed_limits", "unsigned_limits", "error_info"],
    "counters_SUITE": ["basic", "bad", "limits", "indep", "write_concurrency", "error_info"],
    "bs_bincomp_SUITE": ["byte_aligned", "bit_aligned", "extended_byte_aligned",
                         "extended_bit_aligned", "mixed", "tracing"],
    "bs_match_tail_SUITE": ["aligned", "unaligned", "zero_tail", "huge_tail"],
    "persistent_term_SUITE": ["basic", "purging", "sharing", "get_trapping", "info", "info_trapping",
        "destruction", "get_all_race", "killed_while_trapping", "off_heap_values", "keys", "collisions",
        "init_restart", "put_erase_trapping", "killed_while_trapping_put", "killed_while_trapping_erase",
        "error_info", "whole_message", "shared_magic_ref", "non_message_signal",
        "get_put_colliding_bucket", "gc_binary_orig"],
    "guard_SUITE": ["bad_arith", "bad_tuple", "test_heap_guards", "guard_bifs",
                    "type_tests", "guard_bif_binary_part"],
}

MATCH_SPEC_TRACE_CASES = ['test_1', 'test_2', 'test_3', 'test_4a', 'test_4b', 'test_5a', 'test_5b', 'test_6', 'caller_and_return_to', 'trace_control_word', 'silent', 'silent_no_ms', 'silent_test', 'ms_trace2', 'ms_trace3', 'ms_trace_dead', 'otp_9422']
MATCH_SPEC_FLAT_CASES = ['bad_match_spec_bin', 'fpe', 'boxed_and_small', 'destructive_in_test_bif', 'guard_exceptions', 'unary_plus', 'unary_minus', 'moving_labels', 'faulty_seq_trace', 'empty_list', 'maps', 'guard_bifs']
MATCH_SPEC_GROUPS = ("legacy_pre_post", "dynamic_pre_post")
CASES["match_spec_SUITE"] = MATCH_SPEC_TRACE_CASES * 2 + MATCH_SPEC_FLAT_CASES

CASES["time_SUITE"] = ['univ_to_local', 'local_to_univ', 'local_to_univ_utc', 'bad_univ_to_local', 'bad_local_to_univ', 'univ_to_seconds', 'seconds_to_univ', 'consistency', 'now_unique', 'now_update', 'timestamp', 'time_warp_modes', 'monotonic_time_monotonicity', 'monotonic_time_monotonicity_parallel', 'time_unit_conversion', 'signed_time_unit_conversion', 'erlang_timestamp', 'native_time_unit_gh6165']
CASES["timer_bif_SUITE"] = ['start_timer_1', 'send_after_1', 'send_after_2', 'cancel_timer_1', 'cancel_timer_sync', 'start_timer_e', 'send_after_e', 'cancel_timer_e', 'start_timer_big', 'send_after_big', 'read_timer_trivial', 'read_timer', 'read_timer_async', 'cleanup', 'evil_timers', 'registered_process', 'same_time_yielding', 'same_time_yielding_with_cancel', 'same_time_yielding_with_cancel_other', 'auto_cancel_yielding', 'suspended_scheduler_timeout', 'multizero_timeout_in_timeout', 'huge_timeout']

CASES.update({suite: [case["name"] for case in selection["occurrences"]]
              for suite, selection in NATIVE_SUITES.items()})

INPUTS = {suite: ["emulator/" + suite + ".erl", "support/ts_install_cth.erl",
                  "common_test/beam_vm_ct_event.erl"] for suite in CASES}
INPUTS["persistent_term_SUITE"].extend([
    "emulator/erts_test_destructor.erl", "emulator/persistent_term_SUITE_data/erts_test_destructor.c"])

INPUTS["match_spec_SUITE"].extend(["emulator/trace_sessions.erl", "emulator/erts_test_utils.erl"])

INPUTS["guard_SUITE"].append("common_test/beam_vm_bytes.erl")

# Public composition keeps these six VM-owned modules in both products. OTP's
# installed SDK recipe also consumes native headers, static link metadata and
# the child helper. This inventory must not shrink with a malformed manifest.
CORE_MODULES = {"erlang", "erts_internal", "erts_code_purger", "erts_literal_area_collector",
                "erts_dirty_process_signal_handler", "beam_internal"}
SDK_ROOTS = ({"etf.objects", "api.etf", "api.beam_etf", "api.zlib", "runtime.static", "helper.child_setup", "link.static", "link.static.configuration",
              "link.static.exports", "header.beam_native", "header.erl_nif", "header.erl_driver",
              "header.erl_drv_nif", "header.erl_nif_api_funcs", "header.erl_int_sizes_config"}
             | {prefix + module for prefix in ("core.", "core-source.") for module in CORE_MODULES})

for suite in ("time_SUITE", "timer_bif_SUITE"):
    INPUTS[suite].append("emulator/erts_test_utils.erl")

for suite, sources in _native["inputs"].items():
    INPUTS[suite] = list(dict.fromkeys(INPUTS[suite] + sources +
                                      ["common_test/native-selections.json"]))

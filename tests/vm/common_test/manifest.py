"""Reviewed complete flat-suite inventories for the initial VM integration batch."""

CASES = {
    "atomics_SUITE": ["signed", "unsigned", "bad", "signed_limits", "unsigned_limits", "error_info"],
    "counters_SUITE": ["basic", "bad", "limits", "indep", "write_concurrency", "error_info"],
    "bs_bincomp_SUITE": ["byte_aligned", "bit_aligned", "extended_byte_aligned",
                         "extended_bit_aligned", "mixed", "tracing"],
    "bs_match_tail_SUITE": ["aligned", "unaligned", "zero_tail", "huge_tail"],
}

INPUTS = {suite: ["emulator/" + suite + ".erl", "support/ts_install_cth.erl",
                  "common_test/beam_vm_ct_event.erl"] for suite in CASES}

# Public composition keeps these nine VM-owned modules in both products. OTP's
# installed SDK recipe also consumes native headers, static link metadata and
# the child helper. This inventory must not shrink with a malformed manifest.
CORE_MODULES = {"erlang", "erts_internal", "erts_code_purger", "erts_literal_area_collector",
                "erts_dirty_process_signal_handler", "erts_trace_cleaner", "atomics",
                "counters", "persistent_term"}
SDK_ROOTS = ({"runtime.static", "helper.child_setup", "link.static", "link.static.configuration",
              "link.static.exports", "header.beam_native", "header.erl_nif", "header.erl_driver",
              "header.erl_drv_nif", "header.erl_nif_api_funcs", "header.erl_int_sizes_config"}
             | {prefix + module for prefix in ("core.", "core-source.") for module in CORE_MODULES})

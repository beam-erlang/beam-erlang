# Core BIFs share one implementation across both boot families.
add_library(beam_core_shared_data_native OBJECT
    erl_bif_atomics.c
    erl_bif_counters.c
    erl_bif_persistent.c
)
beam_configure_native_component(beam_core_shared_data_native)

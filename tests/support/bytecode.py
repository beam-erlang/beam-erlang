"""Explicit ordinary bytecode assets used by installed acceptance hosts."""
from pathlib import Path
import shutil

TEST_SUPPORT_DIR = Path(__file__).resolve().parent

BASE_MODULES = ("beam", "beam_format", "beam_time", "beam_atomics", "beam_counters", "beam_persistent_term", "beam_binary", "beam_lists", "beam_maps", "beam_records", "beam_ets", "beam_string", "beam_unicode", "beam_runtime_unicode_util", "beam_math", "beam_ddll", "beam_debugger")
BASE_ASSETS = ("lib/beam/library/modules.txt",) + tuple(
    f"lib/beam/library/ebin/{name}.beam" for name in BASE_MODULES)


BOOT_MODULES = ("beam_return_boot", "beam_boot")
SERVICES_MODULES = ("beam_startup", "beam_host", "beam_output", "beam_code")
BOOT_ASSETS = ("lib/beam/boot/modules.txt",) + tuple(f"lib/beam/boot/ebin/{name}.beam" for name in BOOT_MODULES)
SERVICES_ASSETS = ("lib/beam/services/modules.txt",) + tuple(f"lib/beam/services/ebin/{name}.beam" for name in SERVICES_MODULES)
RETURNING_ASSETS = BASE_ASSETS + BOOT_ASSETS + SERVICES_ASSETS

STDLIB_MODULES = ("array", "dict", "graph", "gb_sets", "gb_trees", "orddict", "ordsets",
                  "proplists", "queue", "sets", "sofs", "digraph", "digraph_utils")
STDLIB_ASSETS = ("lib/beam/stdlib/modules.txt",) + tuple(
    f"lib/beam/stdlib/ebin/{name}.beam" for name in STDLIB_MODULES)


def copy_bytecode(source, destination, assets=RETURNING_ASSETS):
    for relative in assets:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / relative, target)


def copy_base(source, destination):
    copy_bytecode(source, destination, BASE_ASSETS)

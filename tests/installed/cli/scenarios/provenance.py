"""Reusable installed provenance scenarios; profiles belong to the runners."""

from pathlib import Path
import sys
from support.host_observe import images
from support.process import isolated_environment
from support.artifacts import artifact

from support.process import check, run as capture


def run(command, cwd, env, input_data=None, status=0):
    result = capture(command, cwd=cwd, env=env, input_data=input_data, status=status, timeout=10)
    return result.stdout, result.stderr


def check_loaded_library(pid, prefix):
    expected=artifact(prefix,'runtime').resolve()
    if sys.platform=='darwin':
        observed=images(pid,cwd=prefix.parent,env=isolated_environment(prefix.parent))
        mapped={Path(p).resolve() for p in observed['files'] if Path(p).name==expected.name}
        check(mapped=={expected},f'VM mapped {mapped}, expected installed library {expected}')
        check(Path(observed['executable']).resolve()==(prefix/'bin/beam').resolve(),'VM executable escaped selected installation')
        return
    mapped = set()
    for line in Path(f"/proc/{pid}/maps").read_text().splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) == 6 and "libbeam.so" in fields[5]:
            mapped.add(Path(fields[5]).resolve())
    check(mapped == {expected}, f"VM mapped {mapped}, expected installed library {expected}")


def check_interpreted_receive(shell, prefix):
    # The shell interpreter uses prim_eval's mailbox instructions. A compiled
    # receive would not exercise that independently supplied module.
    path = str(prefix / "lib/beam/runtime/beam/interactive/ebin/prim_eval.beam")
    path = path.replace("\\", "\\\\").replace('"', '\\"')
    setup = ('(fun() -> {prim_eval, P} = lists:keyfind(prim_eval, 1, '
             'persistent_term:get(beam_boot_origins)), '
             f'P = unicode:characters_to_binary("{path}") end)(), ')
    shell.send_and_expect(
        setup +
        'false = lists:member(prim_eval, erlang:pre_loaded()), '
        'receive_module_ready.\n',
        r"\nreceive_module_ready\n\d+> ")
    shell.send_and_expect(
        'self() ! preserved_message, self() ! selected_message, '
        'matched = receive selected_message -> matched '
        'after 1000 -> missing_selected_message end, '
        'preserved = receive preserved_message -> preserved '
        'after 0 -> missing_preserved_message end, '
        'timeout = receive unexpected_message -> unexpected '
        'after 5 -> timeout end, selective_receive_passed.\n',
        r"\nselective_receive_passed\n\d+> ")


def beam_provenance_expression(prefix):
    root = prefix / "lib/beam/runtime/beam"
    expected = {path.stem: str(path) for directory in (root / "cli/ebin", root / "interactive/ebin")
                for path in directory.glob("*.beam")}
    base = {"beam", "beam_list", "beam_map", "beam_bytes", "beam_text", "beam_format", "beam_time"}
    expected.update({module: f"{prefix}/lib/beam/library/ebin/{module}.beam" for module in base})
    units = {
        "file": ["beam_file", "prim_file", "prim_buffer"],
        "network": ["beam_socket", "socket", "socket_registry", "prim_socket", "maps", "lists"],
    }
    ordinary = {
        "boot": ["beam_return_boot", "beam_boot"],
        "services": ["beam_component_services", "beam_host", "beam_output", "beam_code"],
    }
    for inventory, names in ordinary.items():
        expected.update({name: f"{prefix}/lib/beam/{inventory}/ebin/{name}.beam" for name in names})
    for unit, modules in units.items():
        expected.update({module: f"{artifact(prefix,'component.'+unit)}#{module}" for module in modules})
    expected["zlib"] = f"{artifact(prefix,'component.zlib')}#zlib"

    def binary(text):
        escaped = text.replace("\\", "\\\\").replace('"', '\\"')
        return f'<<"{escaped}"/utf8>>'

    origins = ",".join("{" + module + "," + binary(path) + "}"
                       for module, path in sorted(expected.items()))
    return (
        'Core = lists:sort([atomics,counters,persistent_term,erlang,erts_internal,erts_code_purger,'
        'erts_literal_area_collector,erts_trace_cleaner,erts_dirty_process_signal_handler]), '
        'Core = lists:sort(erlang:pre_loaded()), '
        'lists:foreach(fun(M) -> false = erlang:module_loaded(M) end, '
        '[compile,beam_compiler_cli,beam_source_run,beam_cli,beam_cli_io,init,erl_init,'
        'erl_prim_loader,code,code_server,file,file_server,os,inet,inet_db,inet_gethost_native,'
        'application,application_controller]), '
        'lists:foreach(fun(N) -> undefined = whereis(N) end, '
        '[code_server,file_server_2,application_controller,inet_db,erl_prim_loader]), '
        'undefined = ets:whereis(ac_tab), '
        f'Expected = [{origins}], Origins = persistent_term:get(beam_boot_origins), '
        'lists:foreach(fun(M) -> case lists:member(M,Core) of true -> ok; false -> '
        '{M,P} = lists:keyfind(M,1,Origins), {M,P} = lists:keyfind(M,1,Expected) end end, erlang:loaded()), '
        'true = erlang:module_loaded(beam_interactive), true = erlang:module_loaded(prim_eval), '
        'false = lists:member(prim_eval,Core).'
    )

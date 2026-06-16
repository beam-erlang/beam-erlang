"""Reusable installed terminal scenarios; profiles belong to the runners."""

import os

from support.process import check
from support.terminal import TerminalShell

from .provenance import (
    check_loaded_library, check_interpreted_receive,
)


def check_terminal(beam, arguments, work, env, prefix):
    shell = TerminalShell([str(beam), *arguments], work, env)
    try:
        shell.expect(r"(?:^|\n)1> ")
        check_loaded_library(shell.pid, prefix)
        shell.send_and_expect("X = 6 * 7.\n", r"\n42\n2> ")
        # Backspace edits the expression before evaluation; X is from the
        # preceding command. This checks real terminal editing and bindings.
        shell.send_and_expect("X + 2\x7f1.\n", r"\n43\n3> ")
        shell.send_and_expect("erlang:error(contract_failure).\n",
                              r"exception error: contract_failure[\s\S]*\n4> ")
        shell.send_and_expect("X + 2.\n", r"\n44\n5> ")
        edit_start = len(shell.output)
        shell.send_and_expect('length("x").\x01' + "\x1b[C" * 8 + "λ\n",
                              r"\n2\n\d+> ")
        edited_output = bytes(shell.output[edit_start:])
        try:
            edited_output.decode("utf-8")
        except UnicodeDecodeError as error:
            raise AssertionError(f"terminal redraw split a UTF-8 character: {edited_output!r}") from error
        check("λ".encode("utf-8") in edited_output, "terminal did not echo the inserted character")
        check_interpreted_receive(shell, prefix)
        shell.send_and_expect("[1,\n2,\n3].\n", r"\n\[1,2,3\]\n\d+> ")
        start = len(shell.output)
        shell.send_and_expect(
            'spawn(fun() -> receive after 20 -> io:format("ASYNC_OUTPUT~n") end end), ok.\n',
            r"\nok\n\d+> ")
        # An idle shell may redraw or retain its prompt before concurrent output.
        # The command's echoed string contains "~n", so only delivered output
        # can match the marker followed by an actual newline.
        shell.expect(r"ASYNC_OUTPUT\n", start)
        check_beam_shell_history(shell)
        os.write(shell.fd, b"q( ). % parsed shell command\n")
        shell.wait_exit(0)
    finally:
        shell.close()


def check_beam_shell_history(shell):
    shell.send_and_expect("v(1).\n", r"\n42\n\d+> ")
    shell.send_and_expect("e(2).\n", r"\n43\n\d+> ")
    shell.send_and_expect("200 + 5.\n", r"\n205\n\d+> ")
    shell.send_and_expect("\x1b[A\x7f\x7f7.\n", r"\n207\n\d+> ")
    shell.send_and_expect("\x1b[A\x1b[B201 + 8.\n", r"\n209\n\d+> ")
    shell.send_and_expect("twice(N) -> N * 2.\n", r"\nok\n\d+> ")
    shell.send_and_expect("twice(21).\n", r"\n42\n\d+> ")
    shell.send_and_expect("factorial(0) -> 1; factorial(N) when N > 0 -> N * factorial(N - 1).\n",
                          r"\nok\n\d+> ")
    shell.send_and_expect("factorial(5).\n", r"\n120\n\d+> ")
    shell.send_and_expect("-record(shell_row, {value = 0}).\n", r"\nok\n\d+> ")
    shell.send_and_expect("Row = #shell_row{value = twice(21)}, Row#shell_row.value.\n",
                          r"\n42\n\d+> ")
    shell.send_and_expect("(Row#shell_row{value = 43})#shell_row.value.\n", r"\n43\n\d+> ")
    shell.send_and_expect("1 + .\n", r"syntax error[\s\S]*\n\d+> ")
    shell.send_and_expect('c("unrequested.erl").\n', r"unsupported_shell_helper[\s\S]*\n\d+> ")
    start = len(shell.output)
    os.write(shell.fd, b"\xff\n")
    shell.expect(r"Input error: invalid_unicode[\s\S]*\n\d+> ", start)
    shell.send_and_expect("{X, twice(21), Row#shell_row.value}.\n", r"\n\{42,42,42\}\n\d+> ")

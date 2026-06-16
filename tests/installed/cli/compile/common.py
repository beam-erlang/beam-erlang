"""Common compiler fixture construction, invocation and dependency decoding."""


from pathlib import Path
import json

from support.process import run as capture, isolated_environment, check


def run(command, cwd, env, status=0, timeout=30):
    result = capture(command, cwd=cwd, env=env, timeout=timeout, status=status, text=True)
    return result.stdout, result.stderr


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def erl_string(value):
    return json.dumps(str(value), ensure_ascii=False)


def module(path, value="ok", attributes=""):
    return write(path, f"-module('{path.stem}').\n{attributes}"
                 f"-export([value/0]).\nvalue() -> {value}.\n")


def selectors(runtime):
    return []


class Compiler:
    def __init__(self, prefix, work, runtime):
        self.prefix, self.work, self.runtime = prefix, work, runtime
        work.mkdir(parents=True)
        self.env = isolated_environment(work)
        self.command = [prefix / "bin/beam", *selectors(runtime)]

    def compile(self, *args, status=0, warning=False, env=None):
        stdout, stderr = run([*self.command, "compile", *args], self.work,
                             env or self.env, status)
        check(not stdout, f"compiler wrote to stdout: {stdout!r}")
        if status or warning:
            check(stderr, "compiler omitted its diagnostic")
        else:
            check(not stderr, f"successful compiler diagnostic: {stderr!r}")
        return stderr

    def eval(self, expression):
        stdout, stderr = run([*self.command, "eval", expression], self.work, self.env)
        check(not stdout and not stderr,
              f"assertion evaluation produced output: {stdout!r} {stderr!r}")

    def compiler_eval(self, expression):
        if self.runtime != "beam":
            return self.eval(expression)
        # Compiler-private EPP executes in the compiler VM, not the independent
        # interactive profile. A transform exercises the same assertions there.
        transform = write(self.work / "epp_assertion_transform.erl",
            '-module(epp_assertion_transform).\n-export([parse_transform/2]).\n'
            + 'parse_transform(InputForms, _) -> ' + expression.rstrip().rstrip('.')
            + ', InputForms.\n')
        self.compile("--deterministic", transform)
        consumer = module(self.work / "epp_assertion_consumer.erl", "ok",
                          '-compile({parse_transform,epp_assertion_transform}).\n')
        self.compile("--deterministic", consumer)

    def load(self, beam):
        name = "'" + beam.stem.replace("'", "\\'") + "'"
        if self.runtime == "beam":
            return (f"Origin = <<{erl_string(beam)}/utf8>>, "
                    f"{{ok,Bytes}} = beam_code:fetch(Origin,{name},infinity), "
                    f"ok = beam_code:load({name},Bytes,Origin), ")
        return f"{{module,{name}}} = code:load_abs({erl_string(beam.with_suffix(''))}), "

    def value(self, beam, expected):
        name = "'" + beam.stem.replace("'", "\\'") + "'"
        self.eval(self.load(beam) + f"{expected} = {name}:value().")


def dep_words(text, target=False):
    """Decode the Make escaping used by a single dependency rule."""
    words, word, index = [], [], 0
    while index < len(text):
        char = text[index]
        if char == "\\":
            end = index
            while end < len(text) and text[end] == "\\":
                end += 1
            count = end - index
            if end == len(text):
                word.extend("\\" * (count // 2))
                break
            following = text[end]
            if following == ";":
                check(count % 4 == 3, "incorrect inline-recipe separator escaping")
                word.extend("\\" * ((count - 3) // 4))
                word.append(following)
                index = end + 1
            elif (following in " \t#:\n*?[]" or (target and following == "%") or
                  (not target and following == "|")):
                word.extend("\\" * (count // 2))
                if count % 2:
                    if following != "\n":
                        word.append(following)
                    index = end + 1
                else:
                    index = end
            else:
                word.extend("\\" * count)
                index = end
            continue
        elif char == "$" and text[index:index + 2] == "$$":
            word.append("$")
            index += 1
        elif char.isspace():
            if word:
                words.append("".join(word))
                word = []
        else:
            word.append(char)
        index += 1
    if word:
        words.append("".join(word))
    return words


def dependencies(depfile, target):
    text = depfile.read_text(encoding="utf-8")
    # A target colon may itself be escaped, so locate the first unescaped one.
    index, escaped = 0, False
    for index, char in enumerate(text):
        if char == ":" and not escaped:
            break
        escaped = char == "\\" and not escaped
    else:
        raise AssertionError(f"depfile has no rule: {text!r}")
    check(dep_words(text[:index], target=True) == [str(target)], f"incorrect depfile target: {text!r}")
    words = dep_words(text[index + 1:])
    check(len(words) == len(set(words)), f"duplicate dependency: {words}")
    return {Path(path).resolve() for path in words}

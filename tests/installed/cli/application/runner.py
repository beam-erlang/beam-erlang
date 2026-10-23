"""General installed application execution, independent inventories and handoffs."""
import argparse
import importlib.util
from pathlib import Path
from support.scratch import scratch
import shutil
import sys

from support.installation import add_installation_arguments, resolve_installation, stage_installation
from support.files import hidden
from support.process import check, isolated_environment, run as capture


def run(*args, **kwargs):
    kwargs.setdefault('timeout', 30)
    return capture(*args, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--test-dir', type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    root = args.test_dir.resolve()
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    with scratch(root):
        sdk = root / 'SDK λ'
        stage_installation(args, sdk)
        env = isolated_environment(root)
        spec = importlib.util.spec_from_file_location('application_format', sdk / 'share/beam/tools/beam-application.py')
        codec = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(codec)
        atom = codec.Atom
        app = root / 'unrelated application 日本語'
        (app / 'ebin').mkdir(parents=True)
        (app / 'ordinary.erl').write_text('''-module(ordinary).
-export([main/1]).
main(["halt"]) -> erlang:halt(23);
main(["throw"]) -> erlang:error(application_fixture);
main(["action"]) -> {beam_command,1,{exit,97}};
main(Args) ->
    undefined = persistent_term:get(application_fixture,undefined),
    false = erlang:module_loaded(compile),
    false = erlang:module_loaded(beam_pkg),
    io:format("ENTRY ~tp~n",[Args]).
''')
        (app / 'dispatcher.erl').write_text('''-module(dispatcher).
-export([command/2]).
command({beam_context,1,_,_,_,_,Root,Cwd,_,_,Bytes},["forward"|Rest]) ->
    persistent_term:put(application_fixture,dirty),
    D0 = binary_to_term(Bytes),
    Inventory = [M || M={file,<<"ordinary">>,_,_,_} <- element(9,D0)],
    D1 = setelement(9,setelement(6,setelement(5,D0,Root),{main,<<"ordinary">>,<<"main">>}),Inventory),
    {beam_command,1,{run,D1,term_to_binary(Rest)}};
command(_, ["invalid"]) -> {beam_command,1,{run,invalid,<<>>}};
command({beam_context,1,_,_,_,_,_,Cwd,_,_,_}, ["exec",Executable|Args]) ->
    {beam_command,1,{exec,Cwd,[<< <<C/utf8>> || C <- A >> || A <- [Executable|Args]]}};
command(_, _) -> {beam_command,1,{exit,19}}.
'''.replace('Root,Cwd,_,_,Bytes','Root,_Cwd,_,_,Bytes'))
        run([sdk/'bin/beam','compile','--deterministic','-o',app/'ebin','--',app/'ordinary.erl',app/'dispatcher.erl'], cwd=root, env=env)
        (app/'modules.txt').write_text('ordinary\ndispatcher\n')
        descriptor = app/'application.etf'
        run([sys.executable,sdk/'share/beam/tools/beam-application.py','create','--root',app,
             '--inventory',app/'modules.txt','--entry','ordinary:main','--name','unrelated',
             '--version','1','--service','output','--output',descriptor], cwd=root, env=env)
        original=codec.decode(descriptor.read_bytes())

        def save(d):
            descriptor.write_bytes(b'\x83'+codec.encode(d))

        def invoke(argv=(), status=0):
            return run([sdk/'bin/beam','run','--application',descriptor,'--',*argv],cwd=root,env=env,status=status,text=True,timeout=30)

        out=invoke(['日本語','','--runtime=literal','$(touch unwanted)']).stdout
        check('26085' in out and '--runtime=literal' in out and '$(touch unwanted)' in out, 'Unicode/literal argv changed')
        check(not (root/'unwanted').exists(), 'literal argument executed')
        invoke(['halt'],23)
        invoke(['throw'],1)
        invoke(['action'])  # Ordinary return values are never command actions.
        command=list(original)
        command[5]=(atom('command'),b'dispatcher',b'command')
        command[6]=[atom('application_v1'),atom('command_v1')]
        save(tuple(command))
        invoke(status=19)
        check('ENTRY' in invoke(['forward','fresh']).stdout,'fresh composition was not entered')
        invoke(['invalid'],1)
        result=invoke(['exec',sys.executable,'-c','import sys;print(repr(sys.argv[1:]));raise SystemExit(17)','日本語','','--literal'],17)
        check("['日本語', '', '--literal']" in result.stdout,'native handoff changed arguments')
        # Change the external module inventory without rebuilding the executable.
        reduced=list(original);reduced[8]=[original[8][0]];save(tuple(reduced));invoke()
        saved=descriptor.read_bytes()
        bad=[]
        for index,value in ((1,2),(6,[atom('unknown_feature')]),(7,[atom('output'),atom('output')]),
                             (10,[b'0'*64]),(11,[b'absent_host_module'])):
            d=list(original);d[index]=value;bad.append(tuple(d))
        d=list(original);d[8]=original[8]*2;bad.append(tuple(d))
        d=list(original);record=list(d[8][0]);record[4]=b'0'*32;d[8]=[tuple(record)];bad.append(tuple(d))
        for d in bad:
            save(d);check('ENTRY' not in invoke(status=1).stdout,'invalid input entered application')
        for raw in (saved[:-1],saved+b'j',b'\x83P',b'\x83l\xff\xff\xff\xff'):
            descriptor.write_bytes(raw);invoke(status=1)
        descriptor.write_bytes(saved)
        with hidden(app/'ebin/ordinary.beam'):
            invoke(status=1)
        invoke()
        moved=root/'relocated application λ';app.rename(moved);app=moved;descriptor=app/'application.etf'
        invoke(['relocated'])
        # A generic launcher is an ordinary native command, with no VM dependency.
        launcher=root/'entry λ'
        shutil.copy2(sdk/'bin/beam-launch',launcher)
        config=Path(str(launcher)+'.beam-launcher')
        config.write_text(f'beam-launcher-v1\nabsolute\n{sdk}/bin/beam\n{app.name}/application.etf\n')
        run([launcher,'literal'],cwd=root,env=env,timeout=30)
        with hidden(sdk/'bin/beam'):
            run([launcher],cwd=root,env=env,status=1,timeout=30)
        run([launcher],cwd=root,env=env,timeout=30)
        print('Generic applications: inventories, validation, arguments, lifecycle, actions, launcher and relocation passed.')


if __name__=='__main__':
    main()

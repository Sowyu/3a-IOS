#!/usr/bin/env python3
"""One reviewed offline TFTP request probe, with no network-profile mutation."""
from pathlib import Path
import importlib.util,json,os,re,subprocess,sys,time
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('reviewed_runner',Path(__file__).with_name('run-data-usb.py'))
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

def validate(m):
    if m.get('approved') is not True:raise ValueError('exact-image Sol GO missing')
    if not re.fullmatch('[a-f0-9]{32}',m['nonce']):raise ValueError('bad nonce')
    if m['test']!='modem-tftp' or m['note']!='modem-tftp-2270':raise ValueError('wrong probe')
    for name,want in m['hashes'].items():
        if d.digest(d.local(name))!=want:raise ValueError('frozen artifact changed: '+name)
    for key in ['image','args','script','root_script','original','audit']:
        if m[key] not in m['hashes']:raise ValueError('unpinned '+key)

def main():
    if len(sys.argv)!=2:raise ValueError('usage: run-tftp-probe.py REVIEWED-MANIFEST')
    manifest=d.local(sys.argv[1]);m=json.loads(manifest.read_text());validate(m)
    spec=importlib.util.spec_from_file_location('probe_devloop',d.DEV)
    dev=importlib.util.module_from_spec(spec);spec.loader.exec_module(dev)
    args=dev.bootargs_of(d.local(m['image']))
    if args!=d.local(m['args']).read_text().strip() or dev.check_loop_args(args)!=(None,600):raise ValueError('image/guard mismatch')
    words=args.split();pairs=[w.split('=',1)[0] for w in words]
    if len(set(pairs))!=len(pairs):raise ValueError('duplicate argument')
    required=['sweetmtftpprobe=1','sweetmpss=1','sweetmglink=1','sweetmrmtfs=1','sweetmpdm=1','sweetmstop=1','sweetmho=1','sweetfbarm=1','sweetselftest=1','sweetthermal=3','sweetfetchsync=1','sweetusb=4','cpus=8','sweetpanel=0','sweetfb_noconsole=1']
    forbidden=['sweetmsession','sweetdata','sweetdataonline','sweetqmiraw','sweetipa','sweetipafw','sweetipamap','sweetipaprobe','sweetmprobe','sweetmbcm','sweetapn','sweetapnauth']
    if any(w not in words for w in required) or any(w in pairs for w in forbidden):raise ValueError('offline argument scope violated')
    if d.run([sys.executable,str(d.DEV),'--print-script',m['test']]).stdout!=d.local(m['script']).read_bytes():raise ValueError('hook changed')
    attempt=manifest.with_suffix('.attempt')
    if attempt.exists():raise ValueError('attempt consumed')
    d.HOLD.touch()
    while True:
        for pid in d.busy():
            while True:
                try:os.kill(pid,0)
                except ProcessLookupError:break
                time.sleep(2)
        serial=d.recovery()
        if serial and not d.busy():
            try:d.LOCK.mkdir();break
            except FileExistsError:pass
        time.sleep(5)
    launched=False;safe=False;out=d.ROOT/'plans/modem/out'/('tftp-run-'+m['nonce'])
    try:
        if d.busy() or d.recovery()!=serial or not d.clean(serial):raise ValueError('requires idle same-device clean recovery')
        out.mkdir();before=d.pull_profile(serial,out/'before',hook=True)
        if before.read_bytes()!=d.local(m['original']).read_bytes():raise ValueError('original profile changed')
        body=(d.ROOT/'plans/devloop/selftest/scripts/modem-tftp.sh').read_text()
        base=(out/'before/renegade-firstboot.sh').read_text()
        if dev.compose_script(body,base).encode()!=d.local(m['root_script']).read_bytes():raise ValueError('live root hook differs')
        validate(m)
        with attempt.open('x') as f:f.write('one attempt consumed\n')
        launched=True
        command=[sys.executable,str(d.DEV),str(d.local(m['image'])),'--no-replay','--boot-tries','1','--button-wait','0','--test',m['test'],'--note',m['note'],'--key','SWEET MODEM|SWEET IPA|SWEET DATA','--pull','/private/var/tmp/modem-tftp-complete.txt']
        with (out/'devloop.log').open('wb') as log:
            process=subprocess.Popen(command,cwd=d.ROOT,stdout=log,stderr=subprocess.STDOUT)
            print('Actual dev.py PID',process.pid,flush=True)
            rc=process.wait()
        return rc
    finally:
        if launched:
            if d.busy() or d.recovery()!=serial or not d.clean(serial):raise RuntimeError('ownership retained: clean recovery not proven')
            after=d.pull_profile(serial,out/'after')
            if after.read_bytes()!=d.local(m['original']).read_bytes() or not d.clean(serial):raise RuntimeError('ownership retained: root/profile changed')
        safe=True
        if safe:
            d.run(['/usr/bin/trash',str(d.LOCK)],30)
            d.run(['/usr/bin/trash',str(d.HOLD)],30)
if __name__=='__main__':
    try:sys.exit(main())
    except Exception as error:print('Stopped:',error,file=sys.stderr);sys.exit(2)

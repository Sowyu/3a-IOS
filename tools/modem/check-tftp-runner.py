#!/usr/bin/env python3
"""Exercise the actual runner with fake phone operations and recoverable host fixtures."""
from pathlib import Path
import hashlib,importlib.util,json,sys,tempfile
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('tftp_runner',Path(__file__).with_name('run-tftp-probe.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
required='sweetmtftpprobe=1 sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1 sweetmstop=1 sweetmho=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetthermal=3 sweetfetchsync=1 sweetusb=4 cpus=8 sweetpanel=0 sweetfb_noconsole=1'
real_popen=m.subprocess.Popen
for case in ['success','unapproved','changed-pin','consumed','post-profile-conflict','dirty-after']:
    root=Path(tempfile.mkdtemp(prefix='tftp-runner-',dir='/tmp')).resolve()
    d=m.d;d.ROOT=root;d.HOLD=root/'hold';d.LOCK=root/'lock';d.DEV=root/'dev.py'
    d.DEV.write_text('def bootargs_of(p): return p.read_text()\ndef check_loop_args(s): return (None,600)\ndef compose_script(body,base): return "root hook"\n')
    nonce='1'*32
    manifest={'approved':True,'nonce':nonce,'test':'modem-tftp','note':'modem-tftp-2270','image':'image','args':'args','script':'script','root_script':'root_script','original':'original','audit':'audit','hashes':{}}
    values={'image':required,'args':required,'script':'script','root_script':'root hook','original':'original','audit':'GO'}
    for name,value in values.items():
        (root/name).write_text(value);manifest['hashes'][name]=hashlib.sha256(value.encode()).hexdigest()
    folder=root/'plans/devloop/selftest/scripts';folder.mkdir(parents=True);(folder/'modem-tftp.sh').write_text('body')
    (root/'plans/modem/out').mkdir(parents=True)
    if case=='unapproved':manifest['approved']=False
    if case=='changed-pin':(root/'image').write_text('wrong')
    if case=='consumed':(root/'manifest.attempt').write_text('consumed')
    (root/'manifest.json').write_text(json.dumps(manifest))
    launched=[];trashed=[]
    def run(argv,timeout=300):
        if argv[0]=='/usr/bin/trash':trashed.append(argv[1]);return type('R',(),{'stdout':b''})()
        assert argv[-2:]==['--print-script','modem-tftp'];return type('R',(),{'stdout':b'script'})()
    def pull(serial,out,hook=False):
        assert serial=='fake-recovery';out.mkdir();p=out/'preferences.plist'
        p.write_text('conflict' if case=='post-profile-conflict' and out.name=='after' else 'original')
        if hook:(out/'renegade-firstboot.sh').write_text('base')
        return p
    class Process:
        pid=123
        def __init__(self,argv,**kwargs):launched.append(argv)
        def wait(self):return 0
    d.run=run;d.busy=lambda:[];d.recovery=lambda:'fake-recovery';d.clean=lambda serial:not(case=='dirty-after' and launched);d.pull_profile=pull
    m.subprocess.Popen=Process;sys.argv=['run-tftp-probe.py','manifest.json']
    try:
        result=m.main();assert case=='success' and result==0
    except (ValueError,RuntimeError):assert case!='success'
    if case in ['unapproved','changed-pin','consumed']:
        assert not launched and not d.HOLD.exists() and not trashed
    elif case in ['post-profile-conflict','dirty-after']:
        assert len(launched)==1 and d.HOLD.exists() and d.LOCK.exists() and not trashed
    else:
        assert len(launched)==1 and len(trashed)==2
        a=launched[0];assert '--no-replay' in a and a[a.index('--boot-tries')+1]=='1' and not any('preferences.plist=' in x for x in a)
        assert (root/'manifest.attempt').exists()
m.subprocess.Popen=real_popen
print('PASS actual runner: approval/pins/one attempt, no profile PUT, post-run conflicts retain ownership')

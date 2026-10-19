"""Run the real CLI against bad bindings; all cases must fail before export."""
import json,shutil,subprocess,sys
from pathlib import Path
from backend import HERE

root=HERE.parents[1];run=Path(sys.argv[1]).resolve();dest=run/'failure-tests';dest.mkdir(exist_ok=False)
source_work=run/'fresh-analysis';results=[]
def check(name,work,out,needle):
    p=subprocess.run([sys.executable,str(HERE/'runner.py'),'render','--work',str(work),'--output',str(out)],capture_output=True,text=True)
    assert p.returncode==2 and needle in p.stderr,(name,p.stdout,p.stderr)
    results.append(dict(test=name,exit=p.returncode,error=p.stderr.strip()))
    assert not list(dest.glob('*.partial-*'))

check('existing-output',source_work,run/'fresh-cut-Metal-2K.mov','already exists')
for case in ['source-changed','masks-changed']:
    work=dest/case;shutil.copytree(source_work,work,ignore=shutil.ignore_patterns('mpl-cache'))
    if case=='source-changed':
        m=json.loads((work/'analysis.json').read_text());m['sourceSha256']='0'*64;(work/'analysis.json').write_text(json.dumps(m))
        expected='Source/cut changed'
    else:
        with (work/'tracking.npz').open('ab') as f:f.write(b'changed')
        expected='Analysis dependency changed'
    output=dest/(case+'.mov');check(case,work,output,expected);assert not output.exists()
(dest/'results.json').write_text(json.dumps(results,indent=2));print('CLI failure paths passed:',len(results))

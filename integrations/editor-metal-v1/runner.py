#!/usr/bin/env python3
"""Offline F5 Metal video runner for editor. No Adobe, jobs or approval writes."""
import argparse,ctypes as C,hashlib,importlib.metadata,json,os,platform,re,shutil,subprocess,sys,time,uuid
from fractions import Fraction
from pathlib import Path
import cv2,numpy as np
from backend import Engine,cpu_reference,ptr,BYTE,FLOAT,HERE

ROOT=HERE.parents[1]
FF=str(ROOT/'.local/bin/ffmpeg')
LIB=HERE/'build/libRostoMetalF5.dylib'
VERSION='editor-metal-F5-v1'

def provenance():
    return dict(python=platform.python_version(),platform=platform.platform(),
                packages={k:importlib.metadata.version(k) for k in ['numpy','opencv-contrib-python','mediapipe']},
                files={str(p.relative_to(ROOT)):sha(p) for p in [HERE/'runner.py',HERE/'backend.py',HERE/'F5.metal',LIB,Path(FF)]})

def require(ok,message):
    if not ok:raise ValueError(message)

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()

def dump(path,data):Path(path).write_text(json.dumps(data,ensure_ascii=False,indent=2))
def load(path):return json.loads(Path(path).read_text())
def command(args):return subprocess.run(args,check=True,capture_output=True).stdout

def frames_info(path,decoded=False):
    cmd=[FF,'-v','error','-i',str(path),'-map','0:v:0']
    if not decoded:cmd+=['-c','copy']
    text=command(cmd+['-f','framemd5','-']).decode()
    shape=re.search(r'#dimensions 0: (\d+)x(\d+)',text)
    tb=Fraction(re.search(r'#tb 0: (\d+/\d+)',text)[1])
    lines=[line.split(',') for line in text.splitlines() if line and not line.startswith('#')]
    require(shape and lines,'Missing video')
    # Containers may round 1/60 s to microsecond ticks. Admit at most one tick,
    # capped at 1/6000 s; never accept a missing/duplicated frame or VFR drift.
    tolerance=min(tb,Fraction(1,6000))
    require(int(lines[0][2])==0 and all(abs(Fraction(int(a[2]))*tb-Fraction(i,60))<=tolerance and abs(Fraction(int(a[3]))*tb-Fraction(1,60))<=tolerance for i,a in enumerate(lines)),'Input must have consecutive 60 fps timestamps from zero; no automatic time remapping')
    return dict(width=int(shape[1]),height=int(shape[2]),frames=len(lines),fps=60),text

def media_info(path):
    text=subprocess.run([FF,'-hide_banner','-i',str(path)],capture_output=True).stderr.decode()
    video=next((line for line in text.splitlines() if 'Video:' in line),'')
    require(re.search(r'Video: prores',video) and re.search(r'yuv422p10le\((?:tv, )?bt709, progressive\)',video),'v1 input requires progressive ProRes 422 with uniform Rec.709 tags')
    audio=re.search(r'Audio: (pcm_(?:f32le|s16le|s24le))[^\n]*, 48000 Hz, stereo',text)
    require(audio,'v1 requires PCM stereo 48 kHz, copied without processing')
    require(not re.search(r'rotation of (?!-?0\.00)[-\d.]+',text),'Bake rotation before analysis')
    sar=re.search(r'SAR (\d+:\d+)',text)
    require(not sar or sar[1]=='1:1','Only square pixels supported')
    return dict(audioCodec=audio[1],raw=text)

def validate_review(c,s,source_hash):
    require(c['sourceSha256']==source_hash,'Review belongs to a different source/cut')
    require(c['width']==s['width'] and c['height']==s['height'] and c['frames']==s['frames'] and c['fps']==60,'Review geometry/time base mismatch')
    require(c.get('reviewed') is True and bool(c.get('reviewNotes')),'Supply visual review of cuts and occlusions; Hands does not detect pompoms')
    n=s['frames'];cuts=c['cuts']
    require(cuts and cuts[0]==0 and cuts==sorted(set(cuts)) and all(type(i)is int and 0<=i<n for i in cuts),'Invalid cuts')
    require(type(c['fadeFrames'])is int and 1<=c['fadeFrames']<=120 and 0<c['spatialFeatherPx']<=200,'Invalid transition settings')
    def interval(p):return len(p)==2 and all(type(i)is int for i in p) and 0<=p[0]<p[1]<=n
    for e in c['rectangles']:
        require(interval(e['frames']),'Invalid rectangle interval');x,y,x1,y1=e['box']
        require(all(type(i)is int for i in e['box']) and 0<=x<x1<=s['width'] and 0<=y<y1<=s['height'],'Invalid rectangle bounds')
    for p in c.get('fullBypassIntervals',[]):require(interval(p),'Invalid full bypass')
    for p in c.get('reviewedHandOverrides',[]):
        require(interval(p) and any(e['frames'][0]<=p[0] and e['frames'][1]>=p[1] for e in c['rectangles']),'Hand override needs a reviewed spatial exclusion for the entire interval')

def stream(path,w,h,analysis=False):
    vf=('scale=432:768:flags=lanczos:in_color_matrix=bt709:out_color_matrix=bt709:out_range=full' if analysis else 'scale=in_color_matrix=bt709:out_color_matrix=bt709:out_range=full:flags=accurate_rnd+full_chroma_int')
    if analysis:
        ah=768;aw=round(ah*w/h);vf=f'scale={aw}:{ah}:flags=lanczos:in_color_matrix=bt709:out_color_matrix=bt709:out_range=full';w,h=aw,ah
    p=subprocess.Popen([FF,'-v','error','-i',str(path),'-an','-vf',vf,'-pix_fmt','rgb24','-fps_mode','passthrough','-f','rawvideo','-'],stdout=subprocess.PIPE)
    try:
        while True:
            b=p.stdout.read(w*h*3)
            if not b:break
            require(len(b)==w*h*3,'Truncated decoded frame')
            yield np.frombuffer(b,np.uint8).reshape(h,w,3).copy()
        require(p.wait()==0,'Decoder failed')
    finally:
        p.stdout.close()
        if p.poll() is None:p.terminate();p.wait()

def analyze(source,s,c,work):
    os.environ['MPLCONFIGDIR']=str(work/'mpl-cache');Path(os.environ['MPLCONFIGDIR']).mkdir()
    for f in (ROOT/'local-dev/v001/mpl-cache').glob('fontlist-*.json'):shutil.copy2(f,Path(os.environ['MPLCONFIGDIR'])/f.name)
    import mediapipe as mp
    require(mp.__version__=='0.10.21','Use the validated MediaPipe 0.10.21 environment')
    raw=[];valid=[];reasons=[];face=hands=None
    try:
        for i,im in enumerate(stream(source,s['width'],s['height'],True)):
            if i in c['cuts']:
                if face:face.close();hands.close()
                face=mp.solutions.face_mesh.FaceMesh(static_image_mode=False,max_num_faces=1,refine_landmarks=True,min_detection_confidence=.6,min_tracking_confidence=.6)
                hands=mp.solutions.hands.Hands(static_image_mode=False,max_num_hands=2,min_detection_confidence=.6,min_tracking_confidence=.6)
            f=face.process(im);hh=hands.process(im);why=[];xy=np.zeros((478,2),np.float32)
            if not f.multi_face_landmarks:why.append('face-missing')
            else:
                xy=np.array([(q.x,q.y) for q in f.multi_face_landmarks[0].landmark],np.float32)
                if not np.isfinite(xy).all() or np.any(xy<0) or np.any(xy>1):why.append('out-of-frame')
                else:
                    size=[im.shape[1],im.shape[0]];hull=cv2.convexHull((xy[:468]*size).astype(np.float32))
                    for hand in hh.multi_hand_landmarks or []:
                        hp=np.array([(q.x*size[0],q.y*size[1]) for q in hand.landmark],np.float32)
                        if cv2.intersectConvexConvex(hull,cv2.convexHull(hp))[0]>0:why.append('hand-over-face-bypass')
            raw.append(xy);valid.append(not why);reasons.append(why)
            if i%300==0:print(f'Analysis {i}/{s["frames"]}',flush=True)
    finally:
        if face:face.close();hands.close()
    require(len(raw)==s['frames'],'Analysis frame count mismatch')
    return np.array(raw,np.float32),np.array(valid,np.float32),reasons

def temporal(raw,valid,reasons,s,c):
    lib=C.CDLL(str(LIB));lib.expression_meshes.argtypes=[FLOAT,FLOAT,BYTE,C.c_int,C.c_int,C.c_int,C.c_float,FLOAT];lib.envelope.argtypes=[BYTE,BYTE,C.c_int,C.c_int,FLOAT]
    valid=valid.copy();n=s['frames'];cuts=np.zeros(n,np.uint8);cuts[c['cuts']]=1
    for b,e in c.get('reviewedHandOverrides',[]):
        require(all(set(reasons[i])<={'hand-over-face-bypass'} for i in range(b,e)),'Hand override cannot override missing/out-of-frame faces')
        valid[b:e]=1
    # Expand unknown failures, never keeping stale treatment on unsafe frames.
    valid[np.convolve(1-valid,np.ones(7),mode='full')[3:3+n]>0]=0
    for b,e in c.get('fullBypassIntervals',[]):valid[b:e]=0
    out=np.zeros_like(raw);lib.expression_meshes(ptr(raw),ptr(valid),ptr(cuts,BYTE),n,s['width'],s['height'],1.,ptr(out))
    def env(a):
        result=np.zeros(n,np.float32);a=np.ascontiguousarray(a,np.uint8);lib.envelope(ptr(a,BYTE),ptr(cuts,BYTE),n,c['fadeFrames'],ptr(result));return result
    regions=[]
    for e in c['rectangles']:
        a=np.ones(n,np.uint8);b,end=e['frames'];a[b:end]=0;regions.append(env(a))
    return dict(expression=out,available=valid,cuts=cuts,alpha=env(valid),regionAlpha=np.array(regions,np.float32).reshape(len(regions),n))

def prepare(a):
    source=Path(a.input).resolve();work=Path(a.work).resolve();require(not work.exists(),'Work directory already exists')
    source_hash=sha(source);s,fh=frames_info(source);info=media_info(source);c=load(a.review);validate_review(c,s,source_hash)
    require(16<=s['width']<=4096 and 16<=s['height']<=4096,'Unsupported source dimensions')
    work.mkdir(parents=True);dump(work/'review.json',c);shutil.copy2(HERE/'preset-F5-v004.json',work/'preset.json');(work/'source-framemd5.txt').write_text(fh);started=time.perf_counter()
    if a.reuse_v005:
        old=Path(a.reuse_v005).resolve();sample=load(old/'sample.json');manifest=load(old/'manifest.json')['files']
        require(sample['sourceSha256']==source_hash and sample['n']==s['frames'] and sample['w']==s['width'] and sample['h']==s['height'],'Cached tracking source mismatch')
        for name in ['tracking-full.npz','tracking-full.json']:require(sha(old/name)==manifest[name]['sha256'],'Cached tracking hash mismatch')
        tr=np.load(old/'tracking-full.npz',allow_pickle=False);raw=tr['raw'];valid=tr['detectorValid'];reasons=load(old/'tracking-full.json')['reasons'];mode='verified-v005-cache'
    else:
        raw,valid,reasons=analyze(source,s,c,work);mode='mediapipe-0.10.21-cut-reset'
    require(raw.shape==(s['frames'],478,2) and valid.shape==(s['frames'],) and np.isfinite(raw).all(),'Malformed tracking')
    t=temporal(raw,valid,reasons,s,c);np.savez_compressed(work/'tracking.npz',raw=raw,detectorValid=valid,**t);dump(work/'reasons.json',reasons)
    meta=dict(version=VERSION,input=str(source),sourceSha256=source_hash,**s,audioCodec=info['audioCodec'],analysisMode=mode,seconds=time.perf_counter()-started,files={k:sha(work/k) for k in ['review.json','preset.json','tracking.npz','reasons.json']},librarySha256=sha(LIB))
    meta['provenance']=provenance()
    dump(work/'analysis.json',meta);print(json.dumps(dict(prepared=str(work),**{k:meta[k] for k in ['frames','analysisMode','seconds']})),flush=True)

def visibility(i,t,c,w,h,fields):
    weight=np.full((h,w),t['alpha'][i],np.float32)
    for k,e in enumerate(c['rectangles']):
        a=t['regionAlpha'][k,i]
        if a>=1:continue
        if k not in fields:
            yy,xx=np.ogrid[:h,:w];x0,y0,x1,y1=e['box'];dist=np.maximum.reduce(np.broadcast_arrays(x0-xx,xx-x1,y0-yy,yy-y1));f=np.clip(dist/c['spatialFeatherPx'],0,1).astype(np.float32);fields[k]=f*f*(3-2*f)
        f=fields[k];weight=np.minimum(weight,f+(1-f)*a)
    return weight

def pcm(path):
    p=subprocess.Popen([FF,'-v','error','-i',str(path),'-map','0:a:0','-c:a','pcm_f32le','-f','f32le','-'],stdout=subprocess.PIPE);h=hashlib.sha256();size=0
    for b in iter(lambda:p.stdout.read(8*1024*1024),b''):h.update(b);size+=len(b)
    p.stdout.close();require(p.wait()==0,'Audio decode failed');return dict(sha256=h.hexdigest(),bytes=size,samplesPerChannel=size//8)

def render(a):
    work=Path(a.work).resolve();m=load(work/'analysis.json');c=load(work/'review.json');preset=load(work/'preset.json');source=Path(m['input']);out=Path(a.output).resolve();report=out.with_suffix('.report.json')
    require(m.get('version')==VERSION,'Unsupported analysis version')
    require(not out.exists() and not report.exists() and not out.with_suffix('.framemd5.txt').exists(),'Output/report already exists; choose a new name')
    require(out.suffix.lower()=='.mov' and out.parent.exists(),'Output must be a .mov in an existing directory')
    require(sha(source)==m['sourceSha256'],'Source/cut changed after analysis')
    require(m['librarySha256']==sha(LIB),'Library changed after analysis: prepare a new work directory')
    for name,hsh in m['files'].items():require(sha(work/name)==hsh,'Analysis dependency changed: '+name)
    validate_review(c,m,m['sourceSha256']);w,h=m['width'],m['height'];ow,oh=map(int,a.size.lower().split('x'))
    require(16<=ow<=4096 and 16<=oh<=4096 and ow%2==0 and oh%2==0 and ow*h==oh*w,'Output must preserve aspect ratio with even dimensions 16..4096')
    tr=np.load(work/'tracking.npz',allow_pickle=False);cpu=C.CDLL(str(LIB));engine=Engine(preset=preset) if a.backend=='metal' else None
    temp=out.parent/(out.stem+'.partial-'+uuid.uuid4().hex+'.mov')
    cmd=[FF,'-n','-v','error','-f','rawvideo','-pix_fmt','rgb24','-s',f'{w}x{h}','-r','60','-i','-','-i',str(source),'-map','0:v:0','-map','1:a:0','-vf',f'scale={ow}:{oh}:flags=lanczos+accurate_rnd+full_chroma_int:in_range=full:out_range=tv:out_color_matrix=bt709,setsar=1,setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709','-c:v','prores_ks','-profile:v','2','-pix_fmt','yuv422p10le','-color_range','tv','-colorspace','bt709','-color_trc','bt709','-color_primaries','bt709','-movflags','+write_colr','-c:a','copy','-threads','2',str(temp)]
    writer=subprocess.Popen(cmd,stdin=subprocess.PIPE);started=time.perf_counter();fields={};rows=[];gpu_time=0.;processing=0.;rawhash=hashlib.sha256()
    try:
        for i,raw in enumerate(stream(source,w,h)):
            require(i<m['frames'],'Unexpected extra source frame');weight=visibility(i,tr,c,w,h,fields);mesh=np.ascontiguousarray(tr['expression'][i]);tick=time.perf_counter()
            if weight.max()==0:processed=raw.copy()
            elif engine:processed=engine.process(raw,mesh,weight);gpu_time+=engine.gpu_seconds
            else:processed,_,_=cpu_reference(cpu,raw,mesh,preset,weight)
            processing+=time.perf_counter()-tick;row=dict(frame=i,unsafeChanged=int(np.count_nonzero(processed[weight==0]!=raw[weight==0])))
            require(row['unsafeChanged']==0,'Unsafe pixels changed')
            if a.compare_cpu and weight.max()>0:
                expected,box,masks=cpu_reference(cpu,raw,mesh,preset,weight);diff=np.abs(processed.astype(np.int16)-expected);row.update(maxDelta=int(diff.max()),meanDelta=float(diff.mean()))
                x,y,rw,rh=box;roi=processed[y:y+rh,x:x+rw];original=raw[y:y+rh,x:x+rw];protect=(masks[:,:,2]>=1)|(masks[:,:,3]>=1);high=(masks[:,:,4]>=1)&((original@np.array([.2126,.7152,.0722]))>=.86*255)
                row.update(protectedChanged=int(np.count_nonzero(roi[protect]!=original[protect])),highlightsChanged=int(np.count_nonzero(roi[high]!=original[high])))
                require(row['maxDelta']<=1 and row['protectedChanged']==0 and row['highlightsChanged']==0,'CPU parity/protection failed')
            rows.append(row);rawhash.update(processed.tobytes());writer.stdin.write(processed.tobytes())
            if i%180==0:print(f'Render {i}/{m["frames"]}',flush=True)
        require(len(rows)==m['frames'],'Render frame count mismatch');writer.stdin.close();require(writer.wait()==0,'Encoder failed');render_s=time.perf_counter()-started
        encoded,decoded=frames_info(temp,True);require(encoded==dict(width=ow,height=oh,frames=m['frames'],fps=60),'Output frames/geometry mismatch')
        audio=pcm(source);require(pcm(temp)==audio,'PCM audio mismatch');output_info=media_info(temp);require(output_info['audioCodec']==m['audioCodec'],'Audio codec changed')
        report_data=dict(version=VERSION,backend=a.backend,device=engine.device if engine else 'CPU',input=str(source),sourceSha256=m['sourceSha256'],output=str(out),outputSha256=sha(temp),presetSha256=sha(work/'preset.json'),librarySha256=sha(LIB),shaderSha256=sha(HERE/'F5.metal'),analysisSha256=sha(work/'analysis.json'),reviewSha256=sha(work/'review.json'),**encoded,audioCodec=m['audioCodec'],pcm=audio,pcmExact=True,renderSeconds=render_s,processingSeconds=processing,gpuCommandSeconds=gpu_time,compareCPU=a.compare_cpu,framesChecks=rows,processedRGBSha256=rawhash.hexdigest(),allOutputFramesDecoded=True,color='Rec709 RGB8 encoded full -> single Lanczos scale -> ProRes422 10bit limited; original PCM copy',command=cmd,approved=False)
        # Atomic no-overwrite publication only after every check succeeds.
        report_data['provenance']=provenance()
        os.link(temp,out);temp.unlink();dump(report,report_data);out.with_suffix('.framemd5.txt').write_text(decoded)
        print(json.dumps({k:report_data[k] for k in ['output','backend','device','frames','renderSeconds','processingSeconds','pcmExact']}),flush=True)
    finally:
        if not writer.stdin.closed:writer.stdin.close()
        if writer.poll() is None:writer.terminate();writer.wait()
        if temp.exists():temp.unlink()
        if engine:engine.close()

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    sub.add_parser('doctor')
    a=sub.add_parser('prepare');a.add_argument('--input',required=True);a.add_argument('--review',required=True);a.add_argument('--work',required=True);a.add_argument('--reuse-v005',help='Verified v005 source/tracking manifest, for the unchanged 176 control only')
    a=sub.add_parser('render');a.add_argument('--work',required=True);a.add_argument('--output',required=True);a.add_argument('--size',default='1440x2560');a.add_argument('--backend',choices=['metal','cpu'],default='metal');a.add_argument('--compare-cpu',action='store_true')
    a=p.parse_args()
    if a.cmd=='doctor':
        with Engine() as e:print(json.dumps(dict(version=VERSION,device=e.device,metal=True,librarySha256=sha(LIB),shaderSha256=sha(HERE/'F5.metal'))))
    elif a.cmd=='prepare':prepare(a)
    else:render(a)

if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,subprocess.CalledProcessError,OSError,KeyError) as e:
        print('ERROR:',e,file=sys.stderr);sys.exit(2)

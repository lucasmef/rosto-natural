import ctypes,json,sys,time
from pathlib import Path
import cv2,numpy as np
from backend import Engine,cpu_reference,HERE

ROOT=HERE.parents[1]
CONTROL=ROOT/'local-dev/runs/2026-10-13-160047-176-full-F5-2k'
DEST=Path(sys.argv[1]);DEST.mkdir(parents=True,exist_ok=False)
t=np.load(CONTROL/'temporal-full.npz');cfg=json.loads((CONTROL/'visibility.json').read_text());preset=json.loads((HERE/'preset-F5-v004.json').read_text())
cpu=ctypes.CDLL(str(CONTROL/'libfinish_v004.dylib'));rows=[]
def weight(i,w=1080,h=1920):
 yy,xx=np.ogrid[:h,:w];a=np.full((h,w),t['alpha'][i],np.float32)
 for k,entry in enumerate(cfg['rectangles']):
  if t['regionAlpha'][k,i]>=1:continue
  x0,y0,x1,y1=entry['box'];dist=np.maximum.reduce(np.broadcast_arrays(x0-xx,xx-x1,y0-yy,yy-y1));f=np.clip(dist/cfg['spatialFeatherPx'],0,1).astype(np.float32);f=f*f*(3-2*f);a=np.minimum(a,f+(1-f)*t['regionAlpha'][k,i])
 return a
with Engine() as gpu:
 for f in sorted((CONTROL/'frames').glob('raw-*.png')):
  i=int(f.stem[-4:]);raw=cv2.cvtColor(cv2.imread(str(f)),cv2.COLOR_BGR2RGB);mesh=t['expression'][i];a=weight(i)
  start=time.perf_counter();expected,box,m=cpu_reference(cpu,raw,mesh,preset,a);cpu_s=time.perf_counter()-start
  start=time.perf_counter();actual=gpu.process(raw,mesh,a);metal_s=time.perf_counter()-start
  diff=np.abs(actual.astype(np.int16)-expected);x,y,w,h=box;delta=np.abs(actual.astype(np.int16)-raw)
  protect=(m[:,:,2]>=1)|(m[:,:,3]>=1);high=(m[:,:,4]>=1)&((raw[y:y+h,x:x+w]@np.array([.2126,.7152,.0722]))>=.86*255)
  row=dict(frame=i,maxDelta=int(diff.max()),meanDelta=float(diff.mean()),channelsAbove1=int((diff>1).sum()),protectedChanged=int(np.count_nonzero(delta[y:y+h,x:x+w][protect])),highlightsChanged=int(np.count_nonzero(delta[y:y+h,x:x+w][high])),unsafeChanged=int(np.count_nonzero(delta[a==0])),cpuSeconds=cpu_s,metalSeconds=metal_s,gpuSeconds=gpu.gpu_seconds)
  rows.append(row);print(json.dumps(row),flush=True)
  cv2.imwrite(str(DEST/f'metal-{i:04}.png'),cv2.cvtColor(actual,cv2.COLOR_RGB2BGR))
  assert row['maxDelta']<=1 and row['protectedChanged']==0 and row['highlightsChanged']==0 and row['unsafeChanged']==0
 # All-zero spatial weight and invalid input must not silently become treatment.
 assert np.array_equal(gpu.process(raw,mesh,0),raw)
 errors=[]
 for tag,badmesh,badweight in [('nan-mesh',np.full((478,2),np.nan,np.float32),1),('degenerate',np.zeros((478,2),np.float32),1),('nan-visibility',mesh,np.nan),('negative-visibility',mesh,-1)]:
  try:gpu.process(raw,badmesh,badweight)
  except RuntimeError:errors.append(tag)
  else:raise AssertionError(tag+' was accepted')
 # Both dimensions/blur radii and allocation reuse at 2K.
 raw2=cv2.resize(raw,(1440,2560));expected,_,_=cpu_reference(cpu,raw2,mesh,preset);actual=gpu.process(raw2,mesh);d=np.abs(actual.astype(np.int16)-expected);assert d.max()<=1
 result=dict(device=gpu.device,frames=rows,zeroWeightExact=True,rejected=errors,parity2KMaxDelta=int(d.max()),tolerance='max 1 RGB8 level; protected/unsafe/specular pixels exactly source',approved=False)
 (DEST/'parity.json').write_text(json.dumps(result,indent=2))
 print('PARITY PASS',len(rows),'native frames and 2K fixture',flush=True)

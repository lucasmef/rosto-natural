"""Local Metal F5 C ABI wrapper. No silent fallback, no network calls."""
import ctypes as C
import json
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
BYTE = C.POINTER(C.c_uint8)
FLOAT = C.POINTER(C.c_float)
INT = C.POINTER(C.c_int)
BASE = ['master','skin','preserve_texture','under_eye','tone','shine','eyes','blush','lips']
DETAIL = ['detail_reduction','chin_reduction','blush_diffusion','blush_chroma_reduction','blush_lift']
LOCAL = ['concealer','lip_chroma','cheek_lift']


def ptr(a, kind=FLOAT):
    return a.ctypes.data_as(kind)


def parameters(preset):
    if preset['baseline'].get('flyaways', 0) != 0:
        raise ValueError('Hair treatment is not part of this F5 backend')
    return np.array([preset['baseline'][k] for k in BASE] +
                    [preset['complexion'][k] for k in DETAIL] +
                    [preset['local'][k] for k in LOCAL], dtype=np.float32)


class Engine:
    def __init__(self, library=None, shader=None, preset=None):
        self.lib = C.CDLL(str(library or HERE/'build/libRostoMetalF5.dylib'))
        l = self.lib
        l.rn_metal_create.argtypes = [C.c_char_p,C.c_char_p,C.c_int]
        l.rn_metal_create.restype = C.c_void_p
        l.rn_metal_device.argtypes = [C.c_void_p]; l.rn_metal_device.restype = C.c_char_p
        l.rn_metal_process.argtypes = [C.c_void_p,BYTE,BYTE,C.c_int,C.c_int,FLOAT,FLOAT,FLOAT,C.c_char_p,C.c_int]
        l.rn_metal_gpu_seconds.argtypes = [C.c_void_p]; l.rn_metal_gpu_seconds.restype = C.c_double
        l.rn_metal_destroy.argtypes = [C.c_void_p]
        self.error = C.create_string_buffer(4096)
        self.context = l.rn_metal_create(str(shader or HERE/'F5.metal').encode(),self.error,len(self.error))
        if not self.context:
            raise RuntimeError(self.error.value.decode())
        self.device = l.rn_metal_device(self.context).decode()
        self.preset = preset or json.loads((HERE/'preset-F5-v004.json').read_text())
        self.params = parameters(self.preset)
        self.gpu_seconds = 0.

    def process(self, raw, mesh, visibility=1.):
        if raw.dtype != np.uint8 or raw.ndim != 3 or raw.shape[2] != 3:
            raise ValueError('Expected uint8 HxWx3 RGB')
        raw = np.ascontiguousarray(raw); h,w = raw.shape[:2]
        mesh = np.ascontiguousarray(mesh, dtype=np.float32)
        if mesh.shape != (478,2):
            raise ValueError('Expected 478 x 2 normalized landmarks')
        weight = np.ascontiguousarray(np.broadcast_to(visibility,(h,w)),dtype=np.float32)
        out = np.empty_like(raw)
        result = self.lib.rn_metal_process(self.context,ptr(raw,BYTE),ptr(out,BYTE),w,h,
                                          ptr(mesh),ptr(self.params),ptr(weight),self.error,len(self.error))
        if result:
            raise RuntimeError(self.error.value.decode())
        self.gpu_seconds = self.lib.rn_metal_gpu_seconds(self.context)
        return out

    def close(self):
        if self.context:
            self.lib.rn_metal_destroy(self.context);self.context=None

    def __enter__(self): return self
    def __exit__(self,*_): self.close()


def cpu_reference(lib,raw,mesh,preset,visibility=1.):
    """Frozen-order CPU stages, also used for numerical equivalence checks."""
    lib.kernel.argtypes=[BYTE,C.c_int,C.c_int,FLOAT,FLOAT,FLOAT]
    lib.refined_masks.argtypes=[FLOAT,C.c_int,C.c_int,C.c_int,INT];lib.refined_masks.restype=C.c_void_p
    lib.export_refined.argtypes=[C.c_void_p,FLOAT]
    lib.refined.argtypes=[BYTE,BYTE,C.c_int,C.c_int,C.c_void_p,C.c_float,C.c_float,C.c_float]
    lib.free_refined.argtypes=[C.c_void_p]
    h,w=raw.shape[:2];mesh=np.ascontiguousarray(mesh,np.float32);p=parameters(preset);out=raw.copy()
    lib.kernel(ptr(out,BYTE),w,h,ptr(mesh),ptr(p),ptr(p[9:]))
    box=np.zeros(4,np.int32);mask=lib.refined_masks(ptr(mesh),w,h,0,ptr(box,INT))
    try:
        masks=np.empty((box[3],box[2],6),np.float32);lib.export_refined(mask,ptr(masks))
        lib.refined(ptr(out,BYTE),ptr(raw,BYTE),w,h,mask,*map(float,p[14:17]))
    finally: lib.free_refined(mask)
    weight=np.broadcast_to(visibility,(h,w))[:,:,None]
    out=np.floor(raw.astype(np.float32)+(out.astype(np.float32)-raw)*weight+.5).clip(0,255).astype(np.uint8)
    return out,box,masks

#include <metal_stdlib>
using namespace metal;
// Encoded Rec.709 RGB, not linear-light. Match CPU operation/quantization order.
float sat(float x) { return clamp(x,0.0f,1.0f); }
float ss(float a,float b,float x) {float t=sat((x-a)/(b-a));return t*t*(3.0f-2.0f*t);}
float lum(float3 c) {return .2126f*c.r+.7152f*c.g+.0722f*c.b;}
float3 blend(float3 a,float3 b,float t) {return a+(b-a)*t;}
float ell(float2 q,float2 center,float rx,float ry,constant float* p) {
 float2 d=q-center;
 float x=(p[19]*d.x-p[20]*d.y)/max(rx,1e-5f);
 float y=(p[20]*d.x+p[19]*d.y)/max(ry,1e-5f);
 return sqrt(x*x+y*y);
}
kernel void unpack(device const uchar* rgb [[buffer(0)]],device float4* dst [[buffer(1)]],constant int* d [[buffer(2)]],uint i [[thread_position_in_grid]]) {
 if(i>=uint(d[0]*d[1]))return;
 dst[i]=float4(float(rgb[3*i])/255.0f,float(rgb[3*i+1])/255.0f,float(rgb[3*i+2])/255.0f,1.0f);
}
// One thread per row/column preserves the CPU running-sum order, including
// clamped borders. It avoids a different blur approximation or second skin pass.
kernel void blur_h(device const float4* src [[buffer(0)]],device float4* dst [[buffer(1)]],constant int& radius [[buffer(2)]],constant int* d [[buffer(3)]],uint y [[thread_position_in_grid]]) {
 int w=d[0],h=d[1];if(y>=uint(h))return;
 float4 sum=float4(0,0,0,1);float inv=1.0f/float(radius*2+1);
 for(int k=-radius;k<=radius;++k)sum+=src[int(y)*w+clamp(k,0,w-1)];
 for(int x=0;x<w;++x){dst[int(y)*w+x]=sum*inv;sum+=src[int(y)*w+clamp(x+radius+1,0,w-1)]-src[int(y)*w+clamp(x-radius,0,w-1)];}
}
kernel void blur_v(device const float4* src [[buffer(0)]],device float4* dst [[buffer(1)]],constant int& radius [[buffer(2)]],constant int* d [[buffer(3)]],uint x [[thread_position_in_grid]]) {
 int w=d[0],h=d[1];if(x>=uint(w))return;
 float4 sum=float4(0,0,0,1);float inv=1.0f/float(radius*2+1);
 for(int k=-radius;k<=radius;++k)sum+=src[clamp(k,0,h-1)*w+int(x)];
 for(int y=0;y<h;++y){dst[y*w+int(x)]=sum*inv;sum+=src[clamp(y+radius+1,0,h-1)*w+int(x)]-src[clamp(y-radius,0,h-1)*w+int(x)];}
}
kernel void retouch(device const float4* source [[buffer(0)]],device const float4* fine [[buffer(1)]],device const float4* coarse [[buffer(2)]],device const float* masks [[buffer(3)]],constant float* p [[buffer(4)]],constant int* d [[buffer(5)]],device uchar* dst [[buffer(6)]],uint i [[thread_position_in_grid]]) {
 int w=d[0],h=d[1],mw=d[2],mh=d[3];if(i>=uint(w*h))return;
 float2 q=float2(float(i%w)+.5f,float(i/w)+.5f)/float2(w,h);
 float2 f=q*float2(mw-1,mh-1);int2 a=int2(f),b=min(a+1,int2(mw-1,mh-1));float2 t=f-float2(a);
 float m[7];
 for(int k=0;k<7;k++){float aa=masks[(a.y*mw+a.x)*7+k],bb=masks[(a.y*mw+b.x)*7+k],cc=masks[(b.y*mw+a.x)*7+k],dd=masks[(b.y*mw+b.x)*7+k];float top=aa+(bb-aa)*t.x,bottom=cc+(dd-cc)*t.x;m[k]=top+(bottom-top)*t.y;}
 float3 src=source[i].rgb,mid=fine[i].rgb,low=coarse[i].rgb,out=src;
 if(m[0]>0 && p[0]>0){
 float master=p[0],preserve=p[2],fw=p[17],fh=p[18];
 float3 fd=abs(src-mid),md=abs(mid-low);
 float texture=(.075f+preserve*.45f)+((.45f+preserve*.45f)-(.075f+preserve*.45f))*ss(.006f,.035f,max(fd.r,max(fd.g,fd.b)));
 float mp=(.085f+preserve*.35f)+((.45f+preserve*.45f)-(.085f+preserve*.35f))*ss(.004f,.030f,max(md.r,max(md.g,md.b)));
 float chin=1-ss(.35f,1.0f,ell(q,float2(p[21],p[22]),fw*.42f,fh*.20f,p));
 float reduce=sat(p[9]+p[10]*chin);texture*=1-reduce*.85f;mp*=1-reduce*.92f;
 float cheek=max(1-ss(.20f,1.0f,ell(q,float2(p[23],p[24]),fw*.22f,fh*.13f,p)),1-ss(.20f,1.0f,ell(q,float2(p[25],p[26]),fw*.22f,fh*.13f,p)))*m[1];
 float inner=max(1-ss(.20f,1.0f,ell(q,float2(p[27],p[28]),fw*.095f,fh*.07f,p)),1-ss(.20f,1.0f,ell(q,float2(p[29],p[30]),fw*.095f,fh*.07f,p)));cheek*=1-inner;
 float3 smooth=low+(mid-low)*mp+(src-mid)*texture;
 float3 ed=abs(low-src);float contour=1-ss(.025f,.090f,max(ed.r,max(ed.g,ed.b)));
 float interior=1-ss(.080f,.250f,abs(lum(low)-lum(src)));float band=1-ss(.90f,.999f,m[0]);float guard=interior+(contour-interior)*band;
 out=blend(src,smooth,sat(master*p[1]*m[1]*2.40f*guard));
 if(cheek>0){float y=lum(out),ly=lum(low),gate=(1-ss(.72f,.91f,y))*cheek*master;
 out=clamp(out+((low-ly)-(out-y))*(p[11]*gate),0.0f,1.0f);
 out.r=sat(out.r-p[12]*gate*.12f);out.g=sat(out.g+p[12]*gate*.12f*(.2126f/.7152f));out=clamp(out+p[13]*gate*(8.0f/255.0f),0.0f,1.0f);}
 float y=lum(out);out=blend(out,float3(y),master*p[5]*ss(.62f,.95f,y)*m[1]*.22f);
 float delta=(.58f-y)*.04f*master*p[4]*m[1],tone=master*p[4]*m[1];out=clamp(out+delta+tone*float3(.010f,.012f,.013f),0.0f,1.0f);
 out=clamp(out-master*p[3]*m[2]*.055f,0.0f,1.0f);
 // F5 uses zero legacy blush/lips. Host rejects nonzero values explicitly.
 float eg=master*p[6]*m[3];out=blend(out,float3(lum(out)),eg*1.82f);
 out=clamp(out+(src-mid)*eg*.70f-eg*float3(.014f,.014f,.010f),0.0f,1.0f);
 float bg=master*p[6]*m[4];out=clamp(out+(src-mid)*bg*.55f-bg*.018f,0.0f,1.0f);
 }
 for(int k=0;k<3;k++)dst[i*3+k]=uchar(sat(out[k])*255.0f);
}
kernel void finish(device const uchar* base [[buffer(0)]],device const uchar* raw [[buffer(1)]],device const float* masks [[buffer(2)]],device const float* visibility [[buffer(3)]],constant float* p [[buffer(4)]],constant int* d [[buffer(5)]],device uchar* dst [[buffer(6)]],uint i [[thread_position_in_grid]]) {
 int w=d[0],h=d[1];if(i>=uint(w*h))return;int x=i%w,y=i/w;
 float3 src=float3(raw[i*3],raw[i*3+1],raw[i*3+2]);float3 out=float3(base[i*3],base[i*3+1],base[i*3+2]);
 if(x>=d[4] && y>=d[5] && x<d[4]+d[6] && y<d[5]+d[7]){
 int j=((y-d[5])*d[6]+x-d[4])*6;float protect=max(masks[j+2],max(masks[j+3],masks[j+4]));
 out=out+(src-out)*protect;float l=lum(src)/255.0f;
 float add=12.0f*p[14]*masks[j]*(1-ss(.70f,.90f,l))+6.0f*p[16]*masks[j+5]*(1-ss(.60f,.90f,l));
 float tint=p[15]*masks[j+1]*ss(.10f,.22f,l)*(1-ss(.58f,.86f,l));
 out=floor(clamp(out+add+tint*float3(3,-2,2),0.0f,255.0f)+.5f);
 }
 float a=visibility[i];out=floor(clamp(src+(out-src)*a,0.0f,255.0f)+.5f);
 for(int k=0;k<3;k++)dst[i*3+k]=uchar(out[k]);
}

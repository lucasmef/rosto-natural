#include "rosto/finishing.hpp"
#include "rosto/retouch.hpp"
#include <cstring>
extern "C" {
void expression_meshes(const float* xy,const float* available,const unsigned char* cuts,int n,int w,int h,float cap,float* result){
 std::vector<rosto::TimedMesh> seq(n);std::vector<uint8_t> cc(cuts,cuts+n);
 for(int i=0;i<n;i++){seq[i].seconds=i/60.;if(available[i]>0){rosto::FaceMesh m;m.confidence=1;for(int j=0;j<478;j++)m.points[j]={xy[(i*478+j)*2],xy[(i*478+j)*2+1]};seq[i].mesh=m;}}
 auto out=rosto::stabilize_expression_meshes(seq,cc,w,h,cap);
 for(int i=0;i<n;i++)for(int j=0;j<478;j++){result[(i*478+j)*2]=out[i].mesh?out[i].mesh->points[j].x:0;result[(i*478+j)*2+1]=out[i].mesh?out[i].mesh->points[j].y:0;}
}
void envelope(const unsigned char* safe,const unsigned char* cuts,int n,int fade,float* out){auto a=rosto::treatment_envelope({safe,safe+n},{cuts,cuts+n},fade);std::copy(a.begin(),a.end(),out);}
void* masks(const float* xy,int w,int h,int reference,int* bounds){rosto::FaceMesh m;m.confidence=1;for(int j=0;j<478;j++)m.points[j]={xy[j*2],xy[j*2+1]};auto* map=new rosto::LocalMaskMap(rosto::local_masks(m,w,h,reference!=0));auto r=map->region;bounds[0]=r.x;bounds[1]=r.y;bounds[2]=r.width;bounds[3]=r.height;return map;}
void free_masks(void* p){delete static_cast<rosto::LocalMaskMap*>(p);}
void export_masks(void* p,float* out){auto& map=*static_cast<rosto::LocalMaskMap*>(p);for(size_t i=0;i<map.pixels.size();++i){auto a=map.pixels[i];out[i*4]=a.concealer;out[i*4+1]=a.lips;out[i*4+2]=a.eye_protection;out[i*4+3]=a.mouth_protection;}}
void apply(unsigned char* rgb,int w,int h,void* p,float concealer,float lips){rosto::process_local_rgb(rgb,w,h,w*3,*static_cast<rosto::LocalMaskMap*>(p),{concealer,lips});}
void blend(const unsigned char* raw,const unsigned char* processed,unsigned char* out,int w,int h,const float* xy,float alpha){
 std::memcpy(out,raw,size_t(w)*h*3);if(alpha<=0)return;
 float minx=1,miny=1,maxx=0,maxy=0;for(int j=0;j<478;j++){minx=std::min(minx,xy[j*2]);maxx=std::max(maxx,xy[j*2]);miny=std::min(miny,xy[j*2+1]);maxy=std::max(maxy,xy[j*2+1]);}
 int x0=std::max(0,int(minx*w)-16),y0=std::max(0,int(miny*h)-16),x1=std::min(w,int(maxx*w)+17),y1=std::min(h,int(maxy*h)+17);
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)for(int c=0;c<3;c++){int i=(y*w+x)*3+c;out[i]=uint8_t(std::lround(raw[i]+(float(processed[i])-raw[i])*alpha));}
}
}

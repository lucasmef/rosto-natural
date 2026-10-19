#include "rosto/finishing.hpp"
#include "rosto/retouch.hpp"
#include "rosto/complexion.hpp"
extern "C" {
void kernel(unsigned char* rgb,int w,int h,const float* xy,const float* values,const float* detail) {
 rosto::OwnedImage image(w,h);for(int i=0;i<w*h;++i){for(int c=0;c<3;++c)image.pixels[i*4+c]=rgb[i*3+c]/255.F;image.pixels[i*4+3]=1;}
 rosto::FaceMesh m;m.confidence=1;for(int i=0;i<478;++i)m.points[i]={xy[i*2],xy[i*2+1]};
 rosto::RetouchParams p;p.master=values[0];p.skin=values[1];p.preserve_texture=values[2];p.under_eye=values[3];p.tone=values[4];p.shine=values[5];p.eyes=values[6];p.blush=values[7];p.lips=values[8];p.flyaways=0;
 rosto::process_complexion(image.view(),m,p,{detail[0],detail[1],detail[2],detail[3],detail[4]});
 for(int i=0;i<w*h;++i)for(int c=0;c<3;++c)rgb[i*3+c]=static_cast<unsigned char>(rosto::clamp01(image.pixels[i*4+c])*255.F);
}
void* refined_masks(const float* xy,int w,int h,int reference,int* box) {
 rosto::FaceMesh m;m.confidence=1;for(int i=0;i<478;++i)m.points[i]={xy[i*2],xy[i*2+1]};
 auto* p=new rosto::RefinedMaskMap(rosto::refined_local_masks(m,w,h,reference));auto r=p->region;box[0]=r.x;box[1]=r.y;box[2]=r.width;box[3]=r.height;return p;
}
void export_refined(void* p,float* out){auto& m=*static_cast<rosto::RefinedMaskMap*>(p);for(size_t i=0;i<m.pixels.size();++i){auto a=m.pixels[i];out[i*6]=a.concealer;out[i*6+1]=a.lips;out[i*6+2]=a.eye_protection;out[i*6+3]=a.mouth_protection;out[i*6+4]=a.lip_protection;out[i*6+5]=a.cheeks;}}
void refined(unsigned char* rgb,const unsigned char* raw,int w,int h,void* p,float c,float l,float cheeks){rosto::process_refined_rgb(rgb,raw,w,h,w*3,*static_cast<rosto::RefinedMaskMap*>(p),{c,l,cheeks});}
void free_refined(void* p){delete static_cast<rosto::RefinedMaskMap*>(p);}
}

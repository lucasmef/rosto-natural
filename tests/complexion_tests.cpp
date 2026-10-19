#include "rosto/complexion.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace rosto;
namespace {
void require(bool condition,const char* why) { if(!condition) throw std::runtime_error(why); }
FaceMesh mesh() {
 FaceMesh m; m.confidence=1; for(auto& p:m.points) p={.5F,.5F};
 constexpr std::array<int,36> ids{10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,400,377,152,148,176,149,150,136,172,58,132,93,234,127,162,21,54,103,67,109};
 for(size_t i=0;i<ids.size();++i) { float t=-1.5707963F+6.2831853F*i/ids.size();m.points[ids[i]]={.5F+.4F*std::cos(t),.5F+.45F*std::sin(t)}; }
 m.points[234]={.1F,.5F};m.points[454]={.9F,.5F};m.points[200]={.5F,.81F};m.points[50]={.28F,.62F};m.points[280]={.72F,.62F};return m;
}
OwnedImage source() {
 OwnedImage im(240,320);
 for(int y=0;y<im.height;++y) for(int x=0;x<im.width;++x) {
  const float v=.03F*std::sin(x*1.71F)*std::cos(y*1.41F);
  im.view().set(x,y,{.65F+v,.43F+v,.36F+v,1});
 }
 return im;
}
void defaults_and_bypass() {
 auto m=mesh();RetouchParams p;auto raw=source(),old=raw,zero=raw,bad=raw;
 process(old.view(),m,p);process_complexion(zero.view(),m,p,{});require(old.pixels==zero.pixels,"zero controls changed legacy pixels");
 process_complexion(bad.view(),m,p,{NAN,INFINITY,NAN,INFINITY,NAN});require(old.pixels==bad.pixels,"nonfinite controls not sanitized");
 for(int test=0;test<4;++test) { auto q=p;auto mm=m;auto a=raw;if(test==0)q.master=0;if(test==1)q.before_after=true;if(test==2)mm.confidence=0;if(test==3)mm.points[50].x=NAN;process_complexion(a.view(),mm,q,{1,1,1,1,1});require(a.pixels==raw.pixels,"unsafe/bypass changed source"); }
}
void bounds_and_locality() {
 auto raw=source(),a=raw,b=raw;auto m=mesh();RetouchParams p;
 process_complexion(a.view(),m,p,{1,1,1,1,1});process_complexion(b.view(),m,p,{4,5,6,7,8});require(a.pixels==b.pixels,"controls exceed clamps");
 int changed=0;for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x)for(int c=0;c<4;++c){int i=(y*a.width+x)*4+c;require(std::isfinite(a.pixels[i])&&a.pixels[i]>=0&&a.pixels[i]<=1,"invalid output");if(c==3||x<12||x>228)require(a.pixels[i]==raw.pixels[i],"alpha/outside changed");changed+=a.pixels[i]!=raw.pixels[i];}require(changed>100,"treatment absent");
}
void targeted_texture() {
 auto a=source(),b=a;auto m=mesh();RetouchParams p;p.eyes=p.lips=p.under_eye=p.tone=p.shine=0;
 process_complexion(a.view(),m,p,{});process_complexion(b.view(),m,p,{0,.4F,0,0,0});
 double ea=0,eb=0;for(int y=250;y<275;++y)for(int x=100;x<140;++x){ea+=std::abs(a.view().get(x,y).r-a.view().get(x-1,y).r);eb+=std::abs(b.view().get(x,y).r-b.view().get(x-1,y).r);}require(eb<ea*.95,"chin detail did not decrease");
 for(int y=50;y<80;++y)for(int x=80;x<160;++x)require(a.view().get(x,y).r==b.view().get(x,y).r,"chin correction affected forehead");
}
void chroma_and_lift() {
 auto raw=source(),a=raw,b=raw;auto m=mesh();RetouchParams p;p.master=1;p.skin=p.eyes=p.lips=p.under_eye=p.tone=p.shine=0;
 process_complexion(a.view(),m,p,{0,0,.6F,.1F,0});process_complexion(b.view(),m,p,{0,0,0,0,1});
 int changes=0;for(size_t i=0;i<a.pixels.size();i+=4) {
  float dy=(a.pixels[i]-raw.pixels[i])*.2126F+(a.pixels[i+1]-raw.pixels[i+1])*.7152F+(a.pixels[i+2]-raw.pixels[i+2])*.0722F;
  require(std::abs(dy)<1e-6F,"chroma-only altered luminance");changes+=a.pixels[i]!=raw.pixels[i];
  for(int c=0;c<3;++c)require(b.pixels[i+c]>=raw.pixels[i+c]&&b.pixels[i+c]-raw.pixels[i+c]<=8.F/255.F+1e-6F,"lift exceeded bound");
 }require(changes>100,"chroma absent");
}
}
int main(){try{defaults_and_bypass();bounds_and_locality();targeted_texture();chroma_and_lift();std::cout<<"complexion: 4 test groups passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

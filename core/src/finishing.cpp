#include "rosto/finishing.hpp"
#include <cmath>
#include <limits>

namespace rosto {
namespace {
constexpr std::array<int,20> outer{61,185,40,39,37,0,267,269,270,409,291,375,321,405,314,17,84,181,91,146};
constexpr std::array<int,20> inner{78,191,80,81,82,13,312,311,310,415,308,324,318,402,317,14,87,178,88,95};
constexpr std::array<int,16> left{33,7,163,144,145,153,154,155,133,173,157,158,159,160,161,246};
constexpr std::array<int,16> right{362,382,381,380,374,373,390,249,263,466,388,387,386,385,384,398};
constexpr std::array<int,36> oval{10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,400,377,152,148,176,149,150,136,172,58,132,93,234,127,162,21,54,103,67,109};
float smooth(float a,float b,float x) {const float t=clamp01((x-a)/(b-a));return t*t*(3-2*t);}
template<size_t N> float distance(Point p,const FaceMesh& m,const std::array<int,N>& ids) {
  bool inside=false;float result=std::numeric_limits<float>::infinity();
  for(size_t i=0,j=N-1;i<N;j=i++) {
    const auto a=m.points[ids[j]],b=m.points[ids[i]];
    if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    const float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
    const float t=len>0?clamp01(((p.x-a.x)*dx+(p.y-a.y)*dy)/len):0;
    result=std::min(result,std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy));
  }
  return inside?result:-result;
}
struct Eye {Point center;float width,c,s;};
Eye eye(const FaceMesh& m,int a,int b) {
  const auto p=m.points[a],q=m.points[b];const float w=std::hypot(q.x-p.x,q.y-p.y);
  if(w<2) return {{},0,1,0};
  const float c=(q.x-p.x)/w,s=(q.y-p.y)/w;
  return {{(p.x+q.x)*.5F-s*w*.34F,(p.y+q.y)*.5F+c*w*.34F},w,c,s};
}
float under(Point p,Eye e,bool refined=false) {
  if(e.width<2)return 0;
  const float dx=p.x-e.center.x,dy=p.y-e.center.y;
  const float x=(e.c*dx+e.s*dy)/(e.width*.56F),y=(-e.s*dx+e.c*dy)/(e.width*.23F);
  return 1-smooth(refined?0.F:.55F,1.F,std::hypot(x,y));
}
float cheek(Point p,const FaceMesh& m,int id) {
 const float fw=std::hypot(m.points[454].x-m.points[234].x,m.points[454].y-m.points[234].y);
 const float fh=std::hypot(m.points[152].x-m.points[10].x,m.points[152].y-m.points[10].y);
 const float c=(m.points[454].x-m.points[234].x)/fw,s=(m.points[454].y-m.points[234].y)/fw;
 const float dx=p.x-m.points[id].x,dy=p.y-m.points[id].y;
 return 1-smooth(0,1,std::hypot((c*dx+s*dy)/(fw*.20F),(-s*dx+c*dy)/std::max(1.F,fh*.14F)));
}
RefinedMask mask(Point p,const FaceMesh& m,Eye a,Eye b,float feather,float margin,bool refined=false) {
  const float eye_d=std::max(distance(p,m,left),distance(p,m,right));
  const float mouth_d=distance(p,m,inner);
  const float eye_allow=1-smooth(-margin-feather,-margin,eye_d);
  const float mouth_allow=1-smooth(-margin-feather,-margin,mouth_d);
  float c=std::max(under(p,a,refined),under(p,b,refined))*eye_allow;
  // Inward-only outer feather. Nothing is authorized outside lip contour.
  const float outer_d=distance(p,m,outer);
  float l=smooth(0,feather,outer_d)*mouth_allow;
  const float face=smooth(0,feather,distance(p,m,oval));
  return {c*face,l*face,1-eye_allow,1-mouth_allow,
          refined?smooth(-feather,0,outer_d):0.F,
          refined?std::max(cheek(p,m,205),cheek(p,m,425))*face*eye_allow*mouth_allow*(1-smooth(-feather,0,outer_d)):0.F};
}
Color finish(Color c,LocalMask m,LocalFinishParams p) {
  const float lum=.2126F*c.r+.7152F*c.g+.0722F*c.b;
  const float gate=1-smooth(.70F,.90F,lum);
  const float add=12.F/255.F*clamp01(p.concealer)*m.concealer*gate;
  c.r=clamp01(c.r+add);c.g=clamp01(c.g+add);c.b=clamp01(c.b+add);
  // Small saturation change, no fixed color overlay; preserve specular shine.
  const float y=.2126F*c.r+.7152F*c.g+.0722F*c.b;
  const float amount=clamp01(p.lip_coverage)*m.lips*gate;
  c.r=clamp01(c.r+amount*((c.r-y)*.15F-c.r*.03F));
  c.g=clamp01(c.g+amount*((c.g-y)*.15F-c.g*.03F));
  c.b=clamp01(c.b+amount*((c.b-y)*.15F-c.b*.03F));
  return c;
}
template<class Map> void check_map(const Map& m,int w,int h) {
 if(m.region.x<0 || m.region.y<0 || m.region.width<0 || m.region.height<0 ||
    m.region.x+m.region.width>w || m.region.y+m.region.height>h ||
    m.pixels.size()!=size_t(m.region.width)*m.region.height)throw std::invalid_argument("invalid local mask map");
}
void append_mask(LocalMaskMap& map,RefinedMask m) {
 map.pixels.push_back({m.concealer,m.lips,m.eye_protection,m.mouth_protection});
}
void append_mask(RefinedMaskMap& map,RefinedMask m) {map.pixels.push_back(m);}
}  // namespace
template<class Map> static Map make_local_masks(const FaceMesh& input,int w,int h,bool reference,bool refined) {
 if(w<=0||h<=0)throw std::invalid_argument("invalid mask dimensions");
 if(!input.valid())return {};
 FaceMesh m=input;
 for(auto& p:m.points) {
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.x>1||p.y<0||p.y>1)return {};
  p.x*=w;p.y*=h;
 }
 const float fw=std::hypot(m.points[454].x-m.points[234].x,m.points[454].y-m.points[234].y);
 if(fw<8)return {};
 const float feather=std::max(1.F,fw*.006F),margin=std::max(1.5F,fw*.004F);
 const auto a=eye(m,33,133),b=eye(m,362,263);
 float minx=float(w),miny=float(h),maxx=0,maxy=0;
 auto extend=[&](Point p,float r){minx=std::min(minx,p.x-r);miny=std::min(miny,p.y-r);maxx=std::max(maxx,p.x+r);maxy=std::max(maxy,p.y+r);};
 for(int id:outer)extend(m.points[id],feather+2);
  // The refined path restores source detail on the whole eye, including the
  // upper eyelid margin. Legacy ROI only needed the under-eye support.
  if(refined) {
    for(int id:left)extend(m.points[id],margin+feather+2);
    for(int id:right)extend(m.points[id],margin+feather+2);
    const float fh=std::hypot(m.points[152].x-m.points[10].x,m.points[152].y-m.points[10].y);
    const float radius=std::max(fw*.20F,fh*.14F)+2;
    extend(m.points[205],radius);extend(m.points[425],radius);
  }
 // Circle encloses every rotation of the under-eye ellipse, including feather.
 extend(a.center,a.width*.57F+feather+2);extend(b.center,b.width*.57F+feather+2);
 PixelRegion roi{std::clamp(int(std::floor(minx)),0,w),std::clamp(int(std::floor(miny)),0,h),0,0};
 roi.width=std::clamp(int(std::ceil(maxx)),0,w)-roi.x;roi.height=std::clamp(int(std::ceil(maxy)),0,h)-roi.y;
 if(reference)roi={0,0,w,h};
 Map out{roi,{}};out.pixels.reserve(size_t(roi.width)*roi.height);
 for(int y=roi.y;y<roi.y+roi.height;++y)for(int x=roi.x;x<roi.x+roi.width;++x)
  append_mask(out,mask({x+.5F,y+.5F},m,a,b,feather,margin,refined));
 return out;
}
LocalMaskMap local_masks(const FaceMesh& input,int w,int h,bool reference) {
 return make_local_masks<LocalMaskMap>(input,w,h,reference,false);
}
RefinedMaskMap refined_local_masks(const FaceMesh& input,int w,int h,bool reference) {
 return make_local_masks<RefinedMaskMap>(input,w,h,reference,true);
}
void process_refined_rgb(uint8_t* rgb,const uint8_t* original,int w,int h,int stride,
                         const RefinedMaskMap& map,const RefinedFinishParams& p) {
 if(!rgb||!original||stride<w*3)throw std::invalid_argument("invalid RGB buffer");
 check_map(map,w,h);
 const float concealer=std::isfinite(p.concealer)?clamp01(p.concealer):0;
 const float lips=std::isfinite(p.lip_chroma)?clamp01(p.lip_chroma):0;
 const float cheeks=std::isfinite(p.cheek_lift)?clamp01(p.cheek_lift):0;
 size_t i=0;
 for(int y=map.region.y;y<map.region.y+map.region.height;++y)
 for(int x=map.region.x;x<map.region.x+map.region.width;++x,++i) {
  const auto m=map.pixels[i];auto dst=rgb+y*stride+x*3;const auto src=original+y*stride+x*3;
  const float protect=std::max({m.eye_protection,m.mouth_protection,m.lip_protection});
  float c[3];for(int k=0;k<3;++k)c[k]=dst[k]+(float(src[k])-dst[k])*protect;
  const float lum=(.2126F*src[0]+.7152F*src[1]+.0722F*src[2])/255.F;
  const float add=12.F*concealer*m.concealer*(1-smooth(.70F,.90F,lum))+
                  6.F*cheeks*m.cheeks*(1-smooth(.60F,.90F,lum));
  // Bounded rose chroma, replacing the old lower-lip darkening + saturation
  // stack. Additive color retains local detail, and rolls off on reflections.
  const float tint=lips*m.lips*smooth(.10F,.22F,lum)*(1-smooth(.58F,.86F,lum));
  constexpr float shift[3]{3.F,-2.F,2.F};
  for(int k=0;k<3;++k)dst[k]=uint8_t(std::lround(std::clamp(c[k]+add+tint*shift[k],0.F,255.F)));
 }
}
void process_local(ImageView im,const LocalMaskMap& map,const LocalFinishParams& p) {
 check_map(map,im.width,im.height);
 if(!(p.concealer>0)||!std::isfinite(p.concealer)) {if(!(p.lip_coverage>0)||!std::isfinite(p.lip_coverage))return;}
 auto safe=p;if(!std::isfinite(safe.concealer))safe.concealer=0;if(!std::isfinite(safe.lip_coverage))safe.lip_coverage=0;
 size_t i=0;for(int y=map.region.y;y<map.region.y+map.region.height;++y)for(int x=map.region.x;x<map.region.x+map.region.width;++x,++i) {
  const auto m=map.pixels[i];if(m.concealer==0&&m.lips==0)continue;
  im.set(x,y,finish(im.get(x,y),m,safe));
 }
}
void process_local_rgb(uint8_t* rgb,int w,int h,int stride,const LocalMaskMap& map,const LocalFinishParams& p) {
 if(!rgb||stride<w*3)throw std::invalid_argument("invalid RGB buffer");check_map(map,w,h);
 auto safe=p;if(!std::isfinite(safe.concealer))safe.concealer=0;if(!std::isfinite(safe.lip_coverage))safe.lip_coverage=0;
 if(safe.concealer<=0&&safe.lip_coverage<=0)return;
 size_t i=0;for(int y=map.region.y;y<map.region.y+map.region.height;++y)for(int x=map.region.x;x<map.region.x+map.region.width;++x,++i) {
  const auto m=map.pixels[i];if(m.concealer==0&&m.lips==0)continue;
  auto b=rgb+y*stride+x*3;auto c=finish({b[0]/255.F,b[1]/255.F,b[2]/255.F,1},m,safe);
  b[0]=uint8_t(std::lround(c.r*255));b[1]=uint8_t(std::lround(c.g*255));b[2]=uint8_t(std::lround(c.b*255));
 }
}
std::vector<float> treatment_envelope(const std::vector<uint8_t>& safe,const std::vector<uint8_t>& cuts,int fade) {
 if(cuts.size()!=safe.size()||fade<1)throw std::invalid_argument("invalid envelope options");
 std::vector<float> out(safe.size(),0);
 for(size_t first=0;first<safe.size();) {
  size_t end=first+1;while(end<safe.size()&&!cuts[end])++end;
  std::vector<int> dist(end-first,fade);int nearest=-fade;
  for(size_t i=first;i<end;++i){if(!safe[i])nearest=int(i-first);dist[i-first]=std::min(fade,int(i-first)-nearest);}
  nearest=int(end-first)+fade;
  for(size_t i=end;i-->first;){if(!safe[i])nearest=int(i-first);int d=std::min(dist[i-first],nearest-int(i-first));out[i]=smooth(0,float(fade),float(d));}
  first=end;
 }
 return out;
}
std::vector<TimedMesh> stabilize_expression_meshes(const std::vector<TimedMesh>& input,const std::vector<uint8_t>& cuts,int w,int h,float cap) {
 if(input.size()!=cuts.size()||w<=0||h<=0||!std::isfinite(cap)||cap<0)throw std::invalid_argument("invalid expression options");
 std::vector<TimedMesh> out;out.reserve(input.size());
 TemporalOptions opts;opts.max_gap_frames=0;
 for(size_t first=0;first<input.size();) {
  size_t end=first+1;while(end<input.size()&&!cuts[end])++end;
  // Split at unavailable meshes too: median never samples across an occlusion.
  for(size_t b=first;b<end;) {
   if(!input[b].mesh){out.push_back(input[b++]);continue;}
   size_t e=b+1;while(e<end&&input[e].mesh)++e;
   std::vector<TimedMesh> part(input.begin()+b,input.begin()+e);auto stable=stabilize_meshes(part,opts);
   auto limit=[&](size_t i,int id){auto raw=input[b+i].mesh->points[id];auto& p=stable[i].mesh->points[id];float d=std::hypot((p.x-raw.x)*w,(p.y-raw.y)*h);if(d>cap){float t=cap/d;p={raw.x+(p.x-raw.x)*t,raw.y+(p.y-raw.y)*t};}};
   for(size_t i=0;i<stable.size();++i){for(int id:left)limit(i,id);for(int id:right)limit(i,id);for(int id:outer)limit(i,id);for(int id:inner)limit(i,id);}
   out.insert(out.end(),stable.begin(),stable.end());b=e;
  }
  first=end;
 }
 return out;
}
}  // namespace rosto

#include "rosto/finishing.hpp"
#include "rosto/retouch.hpp"
#include <cmath>
#include <iostream>
#include <limits>
using namespace rosto;
namespace {
void require(bool x,const char* s){if(!x)throw std::runtime_error(s);}
constexpr std::array<int,20> outer{61,185,40,39,37,0,267,269,270,409,291,375,321,405,314,17,84,181,91,146};
constexpr std::array<int,20> inner{78,191,80,81,82,13,312,311,310,415,308,324,318,402,317,14,87,178,88,95};
constexpr std::array<int,16> left{33,7,163,144,145,153,154,155,133,173,157,158,159,160,161,246};
constexpr std::array<int,16> right{362,382,381,380,374,373,390,249,263,466,388,387,386,385,384,398};
constexpr std::array<int,36> oval{10,338,297,332,284,251,389,356,454,323,361,288,397,365,379,378,400,377,152,148,176,149,150,136,172,58,132,93,234,127,162,21,54,103,67,109};
template<size_t N>void ellipse(FaceMesh& m,std::array<int,N> ids,float x,float y,float rx,float ry) {
 for(size_t i=0;i<N;++i){float a=3.14159265F+6.2831853F*i/N;m.points[ids[i]]={x+rx*std::cos(a),y+ry*std::sin(a)};}
}
FaceMesh mesh(){FaceMesh m;m.confidence=1;for(auto& p:m.points)p={.5F,.5F};ellipse(m,oval,.5F,.5F,.40F,.45F);m.points[234]={.1F,.5F};m.points[454]={.9F,.5F};ellipse(m,left,.33F,.35F,.08F,.02F);ellipse(m,right,.67F,.35F,.08F,.02F);ellipse(m,outer,.5F,.67F,.13F,.05F);ellipse(m,inner,.5F,.67F,.085F,.015F);return m;}
std::vector<uint8_t> rgb(int w=320,int h=480){std::vector<uint8_t> p(w*h*3);for(size_t i=0;i<p.size();i+=3){p[i]=uint8_t(150+(i/3)%11);p[i+1]=100;p[i+2]=90;}return p;}
void zero_and_invalid(){auto m=mesh();auto map=local_masks(m,320,480);auto p=rgb(),old=p;process_local_rgb(p.data(),320,480,960,map,{});require(p==old,"zero must be exact");m.points[1].x=std::numeric_limits<float>::quiet_NaN();require(local_masks(m,320,480).pixels.empty(),"invalid mesh not bypassed");m=mesh();m.confidence=0;require(local_masks(m,320,480).pixels.empty(),"missing mesh not bypassed");}
void roi_equivalence(){
 for(bool shifted:{false,true}){auto m=mesh();if(shifted)for(auto& p:m.points){p.x=.12F+p.x*.70F;p.y=.08F+p.y*.80F;}
  auto a=rgb(),b=a;auto roi=local_masks(m,320,480),ref=local_masks(m,320,480,true);process_local_rgb(a.data(),320,480,960,roi,{.67F,.55F});process_local_rgb(b.data(),320,480,960,ref,{.67F,.55F});require(a==b,"ROI/reference differ or seam exists");require(roi.pixels.size()<ref.pixels.size()/2,"ROI did not reduce memory");
 }
}
void safety_and_coverage(){auto p=rgb(),old=p;auto map=local_masks(mesh(),320,480);process_local_rgb(p.data(),320,480,960,map,{1,1});int upper=0,lower=0,conceal=0;
 for(int y=0;y<480;++y)for(int x=0;x<320;++x){size_t i=(y*320+x)*3;bool changed=p[i]!=old[i]||p[i+1]!=old[i+1]||p[i+2]!=old[i+2];
  if(x>140&&x<180&&y>290&&y<317&&changed)++upper;
  if(x>140&&x<180&&y>326&&y<347&&changed)++lower;
  if(x>145&&x<175&&y>318&&y<325)require(!changed,"inner mouth/teeth changed");
  if((x-106)*(x-106)/400.F+(y-168)*(y-168)/36.F<.5F)require(!changed,"eye changed");
  if(x<20||x>300)require(!changed,"outside face changed");
  if(x<135&&y>182&&y<210&&changed)++conceal;
 }
 require(upper>0&&lower>0&&conceal>0,"upper/lower lips or under eyes not covered");
}
void cap_and_texture(){auto p=rgb(),old=p;auto map=local_masks(mesh(),320,480);process_local_rgb(p.data(),320,480,960,map,{9,0});int count=0;for(size_t i=0;i<p.size();++i){require(p[i]>=old[i]&&p[i]-old[i]<=12,"concealer wrong sign or cap");if(p[i]>old[i])++count;}require(count>0,"concealer no effect");
 auto highlights=std::vector<uint8_t>(320*480*3,245),before=highlights;process_local_rgb(highlights.data(),320,480,960,map,{1,1});require(highlights==before,"highlights not preserved");
}
void stride(){auto map=local_masks(mesh(),320,480);std::vector<uint8_t> p(1000*480,150);process_local_rgb(p.data(),320,480,1000,map,{1,1});for(int y=0;y<480;++y)for(int x=960;x<1000;++x)require(p[y*1000+x]==150,"stride padding overwritten");}
void envelope(){std::vector<uint8_t>s(80,1),cuts(80,0);for(int i=20;i<27;++i)s[i]=0;cuts[0]=1;auto a=treatment_envelope(s,cuts);require(a[20]==0&&a[26]==0,"unsafe treatment");for(size_t i=1;i<a.size();++i)require(std::abs(a[i]-a[i-1])<=.1251F,"excessive temporal alpha step");require(a[8]==1&&a[39]==1,"fade duration wrong");std::fill(s.begin(),s.end(),0);a=treatment_envelope(s,cuts);for(auto x:a)require(x==0,"all-unsafe must bypass");}
void cuts(){std::vector<uint8_t>s(40,1),cuts(40,0);cuts[20]=1;s[19]=0;auto a=treatment_envelope(s,cuts);require(a[20]==1,"fade leaked across cut");
 std::vector<TimedMesh> seq;for(int i=0;i<40;++i){auto m=mesh();for(auto& p:m.points)p.x+=i<20?-.04F:.04F;seq.push_back({i/60.,m});}auto out=stabilize_expression_meshes(seq,cuts,320,480);require(std::abs(out[20].mesh->points[1].x-seq[20].mesh->points[1].x)<1e-6F,"mesh state leaked across cut");}
void expression(){std::vector<TimedMesh> seq;std::vector<uint8_t> cuts(15,0);for(int i=0;i<15;++i){auto m=mesh();m.points[13].y+=i==7?.025F:0;seq.push_back({i/60.,m});}seq[4].mesh.reset();auto out=stabilize_expression_meshes(seq,cuts,320,480);require(!out[4].mesh,"failure interpolated");require(std::abs(out[7].mesh->points[13].y-seq[7].mesh->points[13].y)*480<=1.0001F,"expression delay cap exceeded");}
void small_jitter(){
 std::vector<TimedMesh> seq;std::vector<uint8_t> cuts(31,0);auto truth=mesh();
 for(int i=0;i<31;++i){auto m=truth;m.points[13].x+=(i%2?1:-1)*.15F/320;seq.push_back({i/60.,m});}
 auto out=stabilize_expression_meshes(seq,cuts,320,480);float sum=0;
 for(int i=6;i<25;++i)sum+=std::abs(out[i].mesh->points[13].x-truth.points[13].x)*320;
 require(sum/19<.10F,"subpixel jitter was not stabilized");
}
void refined_protection_and_roi(){
 for(float opening:{.002F,.015F,.035F})for(float shift:{-.06F,0.F,.09F}) {
  auto m=mesh();ellipse(m,inner,.5F,.67F,.085F,opening);
  for(auto& p:m.points){p.x+=shift;p.y+=shift*.3F;}
  auto raw=rgb();auto a=raw;for(auto& c:a)c=uint8_t(c+8);auto b=a;
  const auto roi=refined_local_masks(m,320,480),ref=refined_local_masks(m,320,480,true);
  require(!roi.pixels.empty()&&ref.pixels.size()==320*480,"invalid refined test mesh");
  process_refined_rgb(a.data(),raw.data(),320,480,960,roi,{.18F,.80F,.60F});
  process_refined_rgb(b.data(),raw.data(),320,480,960,ref,{.18F,.80F,.60F});
  require(a==b,"refined ROI/reference mismatch");int protected_count=0,upper=0,lower=0;
  for(int y=0;y<480;++y)for(int x=0;x<320;++x){const auto mm=ref.pixels[y*320+x];int j=(y*320+x)*3;
   if(mm.eye_protection>=1||mm.mouth_protection>=1){++protected_count;for(int c=0;c<3;++c)require(a[j+c]==raw[j+c],"refined source eyes/mouth not restored");}
   if(mm.lips>.9F){if(y<(.67F+shift*.3F)*480)++upper;else ++lower;}
  }
  require(protected_count>100&&upper>0&&lower>0,"refined feature coverage incomplete");
 }
}
void refined_color_and_highlights(){
 auto map=refined_local_masks(mesh(),320,480);auto raw=rgb(),a=raw;
 process_refined_rgb(a.data(),raw.data(),320,480,960,map,{});require(a==raw,"refined zero/source identity");
 process_refined_rgb(a.data(),raw.data(),320,480,960,map,{0,1});int tinted=0;
 for(size_t j=0;j<a.size();j+=3){if(a[j]!=raw[j]){++tinted;require(a[j]>=raw[j]&&a[j]-raw[j]<=4,"rose red bound");require(a[j+1]<=raw[j+1]&&raw[j+1]-a[j+1]<=2,"rose green bound");require(a[j+2]>=raw[j+2]&&a[j+2]-raw[j+2]<=4,"rose blue bound");}}
 require(tinted>0,"rose tint missing");
 std::vector<uint8_t> highlight(320*480*3,245);a=highlight;process_refined_rgb(a.data(),highlight.data(),320,480,960,map,{1,1,1});require(a==highlight,"refined specular shine changed");
 a=raw;process_refined_rgb(a.data(),raw.data(),320,480,960,map,{NAN,INFINITY,NAN});require(a==raw,"nonfinite refined params changed source");
 std::vector<uint8_t> padded(1000*480,150),source=padded;process_refined_rgb(padded.data(),source.data(),320,480,1000,map,{1,1});
 for(int y=0;y<480;++y)for(int x=960;x<1000;++x)require(padded[y*1000+x]==150,"refined stride padding overwritten");
}
}
int main(){try{zero_and_invalid();roi_equivalence();safety_and_coverage();cap_and_texture();stride();envelope();cuts();expression();small_jitter();refined_protection_and_roi();refined_color_and_highlights();std::cout<<"finishing: 11 test groups passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

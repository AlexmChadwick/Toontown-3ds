#include "game/golf_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tt::golf {
namespace {
constexpr float kStep = 1.0f / 90.0f;
constexpr float kRadius = 0.25f;
constexpr float kGravity = -25.0f;
struct FileHeader { char magic[4]; uint16_t version, reserved; uint32_t triangles; };
struct FileTriangle { float p[9]; uint8_t surface; uint8_t reserved[3]; };
static_assert(sizeof(FileTriangle) == 40, "gph triangle layout");
float dot(float ax, float ay, float az, float bx, float by, float bz) { return ax * bx + ay * by + az * bz; }
void cross(float ax, float ay, float az, float bx, float by, float bz, float& x, float& y, float& z) { x=ay*bz-az*by; y=az*bx-ax*bz; z=ax*by-ay*bx; }
void closest(const Triangle& t, float px, float py, float pz, float& x, float& y, float& z) {
    const float* a=t.p; const float* b=t.p+3; const float* c=t.p+6;
    float abx=b[0]-a[0],aby=b[1]-a[1],abz=b[2]-a[2],acx=c[0]-a[0],acy=c[1]-a[1],acz=c[2]-a[2],apx=px-a[0],apy=py-a[1],apz=pz-a[2];
    float d1=dot(abx,aby,abz,apx,apy,apz),d2=dot(acx,acy,acz,apx,apy,apz); if(d1<=0&&d2<=0){x=a[0];y=a[1];z=a[2];return;}
    float bpx=px-b[0],bpy=py-b[1],bpz=pz-b[2],d3=dot(abx,aby,abz,bpx,bpy,bpz),d4=dot(acx,acy,acz,bpx,bpy,bpz); if(d3>=0&&d4<=d3){x=b[0];y=b[1];z=b[2];return;}
    float vc=d1*d4-d3*d2; if(vc<=0&&d1>=0&&d3<=0){float v=d1/(d1-d3);x=a[0]+v*abx;y=a[1]+v*aby;z=a[2]+v*abz;return;}
    float cpx=px-c[0],cpy=py-c[1],cpz=pz-c[2],d5=dot(abx,aby,abz,cpx,cpy,cpz),d6=dot(acx,acy,acz,cpx,cpy,cpz); if(d6>=0&&d5<=d6){x=c[0];y=c[1];z=c[2];return;}
    float vb=d5*d2-d1*d6; if(vb<=0&&d2>=0&&d6<=0){float w=d2/(d2-d6);x=a[0]+w*acx;y=a[1]+w*acy;z=a[2]+w*acz;return;}
    float va=d3*d6-d5*d4; if(va<=0&&d4-d3>=0&&d5-d6>=0){float w=(d4-d3)/((d4-d3)+(d5-d6));x=b[0]+w*(c[0]-b[0]);y=b[1]+w*(c[1]-b[1]);z=b[2]+w*(c[2]-b[2]);return;}
    float inv=1.0f/(va+vb+vc),v=vb*inv,w=vc*inv;x=a[0]+abx*v+acx*w;y=a[1]+aby*v+acy*w;z=a[2]+abz*v+acz*w;
}
}
Physics::~Physics(){clear();}
void Physics::clear(){std::free(triangles_);triangles_=nullptr;count_=capacity_=0;}
bool Physics::addTriangle(const Triangle& t){if(count_==capacity_){uint32_t n=capacity_?capacity_*2:64;Triangle* p=static_cast<Triangle*>(std::realloc(triangles_,n*sizeof(Triangle)));if(!p)return false;triangles_=p;capacity_=n;}triangles_[count_++]=t;return true;}
bool Physics::load(const char* path){clear();FILE* f=std::fopen(path,"rb");if(!f)return false;FileHeader h={};if(std::fread(&h,sizeof(h),1,f)!=1||std::memcmp(h.magic,"GPH1",4)||h.version!=1||h.triangles>20000){std::fclose(f);return false;}for(uint32_t i=0;i<h.triangles;i++){FileTriangle in={};if(std::fread(&in,sizeof(in),1,f)!=1||in.surface>uint8_t(Surface::Slick)){clear();std::fclose(f);return false;}Triangle t={};std::memcpy(t.p,in.p,sizeof(t.p));t.surface=static_cast<Surface>(in.surface);if(!addTriangle(t)){clear();std::fclose(f);return false;}}std::fclose(f);return true;}
void swing(Ball& b,float power,float x,float y){float l=std::sqrt(x*x+y*y);if(l<.001f)return;x/=l;y/=l;b.vx=x*power/3;b.vy=y*power/3;b.vz=0;b.resting=false;}
void Physics::simulate(Ball& b,uint16_t* touchedCup){if(b.resting)return;b.vz+=kGravity*kStep;b.x+=b.vx*kStep;b.y+=b.vy*kStep;b.z+=b.vz*kStep;bool ground=false,slick=false;for(uint32_t i=0;i<count_;i++){const Triangle&t=triangles_[i];float qx,qy,qz;closest(t,b.x,b.y,b.z,qx,qy,qz);float dx=b.x-qx,dy=b.y-qy,dz=b.z-qz,ds=dx*dx+dy*dy+dz*dz;if(ds>=kRadius*kRadius)continue;if(t.surface==Surface::Cup&&touchedCup)*touchedCup=1;float nx,ny,nz;if(ds>.000001f){float il=1/std::sqrt(ds);nx=dx*il;ny=dy*il;nz=dz*il;}else{cross(t.p[3]-t.p[0],t.p[4]-t.p[1],t.p[5]-t.p[2],t.p[6]-t.p[0],t.p[7]-t.p[1],t.p[8]-t.p[2],nx,ny,nz);float il=1/std::sqrt(dot(nx,ny,nz,nx,ny,nz));nx*=il;ny*=il;nz*=il;}float pen=kRadius-std::sqrt(ds),vn=dot(b.vx,b.vy,b.vz,nx,ny,nz);b.x+=nx*pen;b.y+=ny*pen;b.z+=nz*pen;if(vn<0){float bounce=t.surface==Surface::Hard?.9f:.15f;b.vx-=(1+bounce)*vn*nx;b.vy-=(1+bounce)*vn*ny;b.vz-=(1+bounce)*vn*nz;}if(nz>.35f){ground=true;slick|=t.surface==Surface::Slick;}}if(ground){float f=slick?.99917f:.986f;b.vx*=f;b.vy*=f;}if(std::fabs(b.vx)+std::fabs(b.vy)+std::fabs(b.vz)<.03f&&ground){b.vx=b.vy=b.vz=0;b.resting=true;}}
void step(Ball& b,float dt){if(b.resting)return;b.vz+=kGravity*dt;b.x+=b.vx*dt;b.y+=b.vy*dt;b.z+=b.vz*dt;if(b.z<=kRadius){b.z=kRadius;if(std::fabs(b.vz)>.25f)b.vz=-b.vz*.15f;else b.vz=0;b.vx*=.986f;b.vy*=.986f;if(std::fabs(b.vx)+std::fabs(b.vy)+std::fabs(b.vz)<.03f){b.vx=b.vy=b.vz=0;b.resting=true;}}}
bool inCup(const Ball&b,float x,float y,float z){float dx=b.x-x,dy=b.y-y,dz=b.z-z;return dx*dx+dy*dy+dz*dz<=.75f*.75f;}
void recordSwing(Movie&m,Physics&p,const Ball&start,float power,float x,float y,float cupX,float cupY,float cupZ){m=Movie{};Ball b=start;swing(b,power,x,y);uint16_t cup=0;for(int frame=0;frame<kMaxMovieFrames;frame++){m.frames[m.count++]=b;p.simulate(b,&cup);if(cup&&inCup(b,cupX,cupY,cupZ)){m.ballInFrame=uint16_t(frame);break;}if(b.resting){m.frames[m.count++]=b;break;}}}
} // namespace tt::golf

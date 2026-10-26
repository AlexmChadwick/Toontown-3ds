#include "game/golf_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tt::golf {
namespace {
constexpr float kStep = 1.0f / 90.0f;
constexpr float kRadius = 0.25f;
constexpr float kGravity = -25.0f;
constexpr float kBallMass = 0.5f;
constexpr float kInvMass = 1.0f / kBallMass;
constexpr float kInvInertia = 1.0f / (0.4f * kBallMass * kRadius * kRadius);
constexpr float kContactErp = 0.9f;
constexpr float kContactCfm = 1.0e-5f;
constexpr int kQuickStepIterations = 8;
// dWorldQuickStep's default SOR factor.  Golf never changes it after world
// creation, so this is the ODE 0.x default rather than a tuning parameter.
constexpr float kQuickStepSor = 1.3f;
constexpr int kMaxContacts = 64;
constexpr float kRestMotionSq = 0.002f;
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
void Physics::clear(){std::free(triangles_);triangles_=nullptr;count_=capacity_=0;lastRayX_=0;lastRayY_=0;lastRayZ_=kRadius;missingRaySteps_=0;haveRay_=false;rayHit_=false;rayHitGrass_=false;rayOnSlick_=false;outOfBoundsReset_=false;}
bool Physics::addTriangle(const Triangle& t){if(count_==capacity_){uint32_t n=capacity_?capacity_*2:64;Triangle* p=static_cast<Triangle*>(std::realloc(triangles_,n*sizeof(Triangle)));if(!p)return false;triangles_=p;capacity_=n;}triangles_[count_++]=t;return true;}
bool Physics::load(const char* path){clear();FILE* f=std::fopen(path,"rb");if(!f)return false;FileHeader h={};if(std::fread(&h,sizeof(h),1,f)!=1||std::memcmp(h.magic,"GPH1",4)||h.version!=1||h.triangles>20000){std::fclose(f);return false;}for(uint32_t i=0;i<h.triangles;i++){FileTriangle in={};if(std::fread(&in,sizeof(in),1,f)!=1||in.surface>uint8_t(Surface::Slick)){clear();std::fclose(f);return false;}Triangle t={};std::memcpy(t.p,in.p,sizeof(t.p));t.surface=static_cast<Surface>(in.surface);if(!addTriangle(t)){clear();std::fclose(f);return false;}}std::fclose(f);return true;}
void swing(Ball& b,float power,float x,float y){float l=std::sqrt(x*x+y*y);if(l<.001f)return;x/=l;y/=l;b.vx=x*power/3;b.vy=y*power/3;b.vz=0;b.wx=b.wy=b.wz=0;b.resting=false;}
namespace {

struct Contact {
    float nx, ny, nz;
    float x, y, z;
    float depth;
    Surface surface;
    float normalImpulse;
    float tangent1Impulse;
    float tangent2Impulse;
};

struct SurfaceParams { float mu, bounce, bounceVelocity, dampen; };

// PhysicsWorldBase.setupSimulation's ball (surface 1) entries.  soft_erp,
// soft_cfm and slip are common to each entry and are represented above.
SurfaceParams surfaceParams(Surface surface) {
    switch (surface) {
    case Surface::Grass: return {80.0f, 0.15f, 0.1f, 0.35f / 1.2f};
    case Surface::Hard:  return {1500.0f, 0.9f, 0.01f, 0.001f / 1.2f};
    case Surface::Cup:   return {150.0f, 0.0f, 99.1f, 1.0f / 1.2f};
    case Surface::Slick: return {150.0f, 0.0f, 99.1f, 0.001f / 1.2f};
    }
    return {0, 0, 0, 0};
}

void normalize(float& x, float& y, float& z) {
    const float lengthSq = dot(x, y, z, x, y, z);
    if (lengthSq <= 1.0e-12f) { x = 1; y = 0; z = 0; return; }
    const float inverse = 1.0f / std::sqrt(lengthSq);
    x *= inverse; y *= inverse; z *= inverse;
}

void tangentBasis(float nx, float ny, float nz, float& tx, float& ty, float& tz,
                  float& ux, float& uy, float& uz) {
    // dPlaneSpace, used by ODE's contact-joint setup when no fdir1 is set.
    // Its axes matter because QuickStep clamps each friction row separately.
    if (std::fabs(nz) > 0.70710678f) {
        const float a = ny * ny + nz * nz;
        const float k = 1.0f / std::sqrt(a);
        tx = 0.0f; ty = -nz * k; tz = ny * k;
        ux = a * k; uy = -nx * tz; uz = nx * ty;
    } else {
        const float a = nx * nx + ny * ny;
        const float k = 1.0f / std::sqrt(a);
        tx = -ny * k; ty = nx * k; tz = 0.0f;
        ux = -nz * ty; uy = nz * tx; uz = a * k;
    }
}

float contactVelocity(const Ball& ball, const Contact& contact, float tx, float ty, float tz) {
    // dJointCreateContact uses dContactGeom::pos as its anchor.  For an edge
    // contact this is the closest mesh point, not the point at -radius*n.
    const float rx = contact.x - ball.x, ry = contact.y - ball.y, rz = contact.z - ball.z;
    float ax, ay, az;
    cross(ball.wx, ball.wy, ball.wz, rx, ry, rz, ax, ay, az);
    return dot(ball.vx + ax, ball.vy + ay, ball.vz + az, tx, ty, tz);
}

float effectiveMass(const Ball& ball, const Contact& contact, float tx, float ty, float tz) {
    const float rx = contact.x - ball.x, ry = contact.y - ball.y, rz = contact.z - ball.z;
    float cx, cy, cz;
    cross(rx, ry, rz, tx, ty, tz, cx, cy, cz);
    return kInvMass + kInvInertia * dot(cx, cy, cz, cx, cy, cz) + kContactCfm / kStep;
}

void applyImpulse(Ball& ball, const Contact& contact, float tx, float ty, float tz, float impulse) {
    ball.vx += tx * impulse * kInvMass;
    ball.vy += ty * impulse * kInvMass;
    ball.vz += tz * impulse * kInvMass;
    const float rx = contact.x - ball.x, ry = contact.y - ball.y, rz = contact.z - ball.z;
    float ax, ay, az;
    cross(rx, ry, rz, tx * impulse, ty * impulse, tz * impulse, ax, ay, az);
    ball.wx += ax * kInvInertia;
    ball.wy += ay * kInvInertia;
    ball.wz += az * kInvInertia;
}

// The ball ray in GolfHoleBase points straight down for 50 units.  We only need
// its grass/hard hit used by postStep's small height correction, not ODE ray
// contact joints.  The test is deliberately independent of triangle winding.
bool rayFloor(const Triangle& triangle, float x, float y, float z, float& hitZ) {
    const float* a = triangle.p;
    const float abx = triangle.p[3] - a[0], aby = triangle.p[4] - a[1], abz = triangle.p[5] - a[2];
    const float acx = triangle.p[6] - a[0], acy = triangle.p[7] - a[1], acz = triangle.p[8] - a[2];
    const float determinant = abx * acy - aby * acx;
    if (std::fabs(determinant) < 1.0e-7f) return false;
    const float px = x - a[0], py = y - a[1];
    const float u = (px * acy - py * acx) / determinant;
    const float v = (abx * py - aby * px) / determinant;
    if (u < 0.0f || v < 0.0f || u + v > 1.0f) return false;
    const float planeZ = a[2] + u * abz + v * acz;
    if (planeZ > z || planeZ < z - 50.0f) return false;
    hitZ = planeZ;
    return true;
}

} // namespace

void Physics::simulate(Ball& ball, uint16_t* touchedCup) {
    if (ball.resting) return;

    rayHit_ = false;
    rayHitGrass_ = false;
    rayOnSlick_ = false;

    Contact contacts[kMaxContacts] = {};
    int contactCount = 0;
    float strongestDampen = 0.0f;
    for (uint32_t i = 0; i < count_ && contactCount < kMaxContacts; ++i) {
        const Triangle& triangle = triangles_[i];
        float qx, qy, qz;
        closest(triangle, ball.x, ball.y, ball.z, qx, qy, qz);
        float dx = ball.x - qx, dy = ball.y - qy, dz = ball.z - qz;
        const float distanceSq = dot(dx, dy, dz, dx, dy, dz);
        if (distanceSq >= kRadius * kRadius) continue;
        const float distance = std::sqrt(distanceSq);
        // dCollideSTL keeps the triangle's face normal for every contact,
        // including a closest point on an edge or vertex.  It does *not* use
        // the radial point-to-sphere normal there.  That distinction is
        // material at the three-contact hard-wall corner on hole 6: ODE
        // reports three +X normals with distinct depths.
        float nx, ny, nz;
        cross(triangle.p[3] - triangle.p[0], triangle.p[4] - triangle.p[1], triangle.p[5] - triangle.p[2],
              triangle.p[6] - triangle.p[0], triangle.p[7] - triangle.p[1], triangle.p[8] - triangle.p[2],
              nx, ny, nz);
        normalize(nx, ny, nz);
        Contact& contact = contacts[contactCount++];
        contact.nx = nx; contact.ny = ny; contact.nz = nz;
        contact.x = qx; contact.y = qy; contact.z = qz;
        contact.depth = kRadius - distance;
        contact.surface = triangle.surface;
        const SurfaceParams params = surfaceParams(triangle.surface);
        if (params.dampen > strongestDampen) strongestDampen = params.dampen;
    }

    // OdeSimpleSpace reports the later-created hard trimesh before the grass
    // trimeshes.  QuickStep preserves that contact-joint row order, which is
    // observable when a ball hits a wall while still touching the green.
    // GPH stores surfaces in bake order (grass first), so restore the ODE
    // ordering before building the PGS rows.
    for (int i = 1; i < contactCount; ++i) {
        Contact value = contacts[i];
        const int valueOrder = value.surface == Surface::Hard ? 0 : 1;
        int j = i;
        while (j > 0 && (contacts[j - 1].surface == Surface::Hard ? 0 : 1) > valueOrder) {
            contacts[j] = contacts[j - 1];
            --j;
        }
        contacts[j] = value;
    }

    // dWorldQuickStep first applies gravity/forces, solves its contact rows eight
    // times, then advances the body.  The recorder's swing force is represented
    // by swing()'s one-step velocity change.
    ball.vz += kGravity * kStep;
    for (int iteration = 0; iteration < kQuickStepIterations; ++iteration) {
        for (int i = 0; i < contactCount; ++i) {
            Contact& contact = contacts[i];
            const SurfaceParams params = surfaceParams(contact.surface);
            const float normalVelocity = dot(ball.vx, ball.vy, ball.vz,
                                             contact.nx, contact.ny, contact.nz);
            float targetVelocity = kContactErp * contact.depth / kStep;
            if (normalVelocity < -params.bounceVelocity)
                targetVelocity = std::max(targetVelocity, -params.bounce * normalVelocity);
            const float mass = effectiveMass(ball, contact, contact.nx, contact.ny, contact.nz);
            float delta = kQuickStepSor * (targetVelocity - normalVelocity -
                                           contact.normalImpulse * kContactCfm / kStep) / mass;
            const float oldNormal = contact.normalImpulse;
            contact.normalImpulse = std::max(0.0f, oldNormal + delta);
            applyImpulse(ball, contact, contact.nx, contact.ny, contact.nz,
                         contact.normalImpulse - oldNormal);

            float tx, ty, tz, ux, uy, uz;
            tangentBasis(contact.nx, contact.ny, contact.nz, tx, ty, tz, ux, uy, uz);
            const float maxFriction = params.mu * contact.normalImpulse;
            const float tangentMass = effectiveMass(ball, contact, tx, ty, tz);
            const float vt1 = contactVelocity(ball, contact, tx, ty, tz);
            const float oldTangent1 = contact.tangent1Impulse;
            contact.tangent1Impulse = std::max(-maxFriction, std::min(maxFriction,
                oldTangent1 - kQuickStepSor * vt1 / tangentMass));
            applyImpulse(ball, contact, tx, ty, tz, contact.tangent1Impulse - oldTangent1);
            const float vt2 = contactVelocity(ball, contact, ux, uy, uz);
            const float oldTangent2 = contact.tangent2Impulse;
            contact.tangent2Impulse = std::max(-maxFriction, std::min(maxFriction,
                oldTangent2 - kQuickStepSor * vt2 / tangentMass));
            applyImpulse(ball, contact, ux, uy, uz, contact.tangent2Impulse - oldTangent2);
        }
    }
    ball.x += ball.vx * kStep;
    ball.y += ball.vy * kStep;
    ball.z += ball.vz * kStep;

    // GolfHoleBase only records a cup touch when the mesh contact is below the
    // ball underside.  A rim/side contact is not a sunk putt.
    if (touchedCup) {
        for (int i = 0; i < contactCount; ++i) {
            if (contacts[i].surface == Surface::Cup && contacts[i].z < ball.z - 0.05f) {
                *touchedCup = 1;
                break;
            }
        }
    }

    // GolfHoleBase.postStep pins the ball ray's grass/hard contact just above
    // the surface whenever the sphere has numerically sunk beneath it.
    float rayZ = -1.0e30f;
    for (uint32_t i = 0; i < count_; ++i) {
        float hitZ;
        if (rayFloor(triangles_[i], ball.x, ball.y, ball.z, hitZ)) {
            rayHit_ = true;
            // postStep walks every ray contact.  greenIn and onSlick are not
            // properties of just the nearest triangle: overlapping cup/grass
            // and hard/grass meshes report both contacts to ODE.
            if (triangles_[i].surface == Surface::Grass) rayHitGrass_ = true;
            if (triangles_[i].surface == Surface::Slick || triangles_[i].surface == Surface::Hard)
                rayOnSlick_ = true;
            if (hitZ > rayZ) rayZ = hitZ;
        }
    }
    if (rayZ > -1.0e20f) {
        lastRayX_ = ball.x;
        lastRayY_ = ball.y;
        lastRayZ_ = rayZ + 0.245f;
        haveRay_ = true;
        missingRaySteps_ = 0;
        if (ball.z < rayZ + 0.2f) ball.z = lastRayZ_;
    } else if (haveRay_) {
        ++missingRaySteps_;
        // GolfHoleBase lets the OOB ray miss for 270 ticks, then restores its
        // earlier recording[greenIn] checkpoint.  The recorder holds that
        // fixed-size history and consumes this signal below.
        if (missingRaySteps_ > 270) {
            outOfBoundsReset_ = true;
            missingRaySteps_ = 0;
        }
    }

    // OdeWorld.applyDampening is a per-second coefficient, applied after each
    // QuickStep.  Only the ball's current surface pair contributes here.
    if (strongestDampen > 0.0f) {
        const float factor = std::max(0.0f, 1.0f - strongestDampen * kStep);
        ball.vx *= factor; ball.vy *= factor; ball.vz *= factor;
        ball.wx *= factor; ball.wy *= factor; ball.wz *= factor;
    }
}
bool Physics::takeOutOfBoundsReset() {
    const bool reset = outOfBoundsReset_;
    outOfBoundsReset_ = false;
    return reset;
}
void step(Ball& b,float dt){if(b.resting)return;b.vz+=kGravity*dt;b.x+=b.vx*dt;b.y+=b.vy*dt;b.z+=b.vz*dt;if(b.z<=kRadius){b.z=kRadius;if(std::fabs(b.vz)>.25f)b.vz=-b.vz*.15f;else b.vz=0;b.vx*=.986f;b.vy*=.986f;if(std::fabs(b.vx)+std::fabs(b.vy)+std::fabs(b.vz)<.03f){b.vx=b.vy=b.vz=0;b.resting=true;}}}
bool inCup(const Ball&b,float x,float y,float z){float dx=b.x-x,dy=b.y-y,dz=b.z-z;return dx*dx+dy*dy+dz*dz<=.375f*.375f;}
bool predictable(const Movie& movie, uint16_t index, float error) {
    if (!index || index + 1 >= movie.count) return false;
    const uint16_t first = movie.frameIds[index - 1];
    const uint16_t last = movie.frameIds[index + 1];
    if (last == first) return false;
    const float t = float(movie.frameIds[index] - first) / float(last - first);
    const Ball& a = movie.frames[index - 1]; const Ball& b = movie.frames[index]; const Ball& c = movie.frames[index + 1];
    return std::fabs((a.x + (c.x - a.x) * t) - b.x) <= error &&
           std::fabs((a.y + (c.y - a.y) * t) - b.y) <= error &&
           std::fabs((a.z + (c.z - a.z) * t) - b.z) <= error;
}
void reduceMovie(Movie& movie) {
    float error = .05f;
    for (;;) {
        uint16_t removed = 0;
        for (uint16_t i = 1; i + 1 < movie.count;) {
            if (!predictable(movie, i, error)) { ++i; continue; }
            for (uint16_t j = i; j + 1 < movie.count; ++j) {
                movie.frames[j] = movie.frames[j + 1]; movie.frameIds[j] = movie.frameIds[j + 1];
            }
            --movie.count; ++removed;
        }
        if (removed <= 5 || movie.count <= 120) break;
        error *= 1.25f;
    }
}
void recordRaw(Movie&m,Physics&p,const Ball&start,float power,float x,float y,float cupX,float cupY,float cupZ){
    m=Movie{}; Ball b=start; b.resting=false; uint16_t cup=0;
    bool didHoleBreak = false;
    // GolfHoleBase settles the positioned sphere once before applying the club
    // force, then records that settled state as frame zero.
    p.simulate(b,&cup);
    swing(b,power,x,y);
    // This is the bounded equivalent of GolfHoleBase.recording.  greenIn is
    // deliberately an index into the recording rather than a saved last ray
    // point: OOB recovery rewinds to the last grass sample (minus two frames),
    // drops newer samples, and can then retry the same path.
    uint16_t frame = 0;
    int greenIn = 0;
    int resetAt = 100000;
    uint8_t resetCount = 0;
    uint8_t ballRocket = 0;
    uint16_t inCount = 0;
    bool haveGreenIn = false;
    Ball lastGrassVelocity{};
    while (frame < kMaxMovieFrames) {
        if (m.count >= kMaxMovieFrames) break;
        m.frames[m.count]=b; m.frameIds[m.count++]=frame;
        // GolfHoleBase.record, not ODE auto-disable (which stock explicitly
        // turns off), decides that a stationary grass ball is done.
        if (frame > 50 && frame % 13 == 0) {
            const Ball& past5 = m.frames[m.count - 12];
            const Ball& past10 = m.frames[m.count - 35];
            const float d5x = b.x - past5.x, d5y = b.y - past5.y, d5z = b.z - past5.z;
            const float d10x = b.x - past10.x, d10y = b.y - past10.y, d10z = b.z - past10.z;
            if (d5x*d5x + d5y*d5y + d5z*d5z < kRestMotionSq &&
                d10x*d10x + d10y*d10y + d10z*d10z < kRestMotionSq && !p.rayOnSlick()) b.resting = true;
        }
        if (b.resting) break;
        p.simulate(b,&cup);
        ++frame;
        if (p.rayHit()) ++inCount;
        else inCount = 0;
        if (ballRocket && inCount > 1) {
            --ballRocket;
            b.vx *= 2.0f;
            b.vy *= 2.0f;
            b.vz *= 2.0f;
        }
        if (p.rayHitGrass()) {
            // postStep sees self.frame after record() advanced it.  m.count is
            // therefore the exact slot that the following record will occupy.
            greenIn = int(m.count);
            haveGreenIn = true;
            lastGrassVelocity = b;
        }
        if (p.takeOutOfBoundsReset() && haveGreenIn) {
            if (greenIn > 2) greenIn -= 2;
            if (greenIn > resetAt) greenIn = resetAt - 10;
            if (resetCount > 3) greenIn = 0;
            if (greenIn < 0) greenIn = 0;
            ++resetCount;
            b = m.frames[uint16_t(greenIn)];
            b.z += 0.27f;
            b.wx = b.wy = b.wz = 0.0f;
            if (resetCount < 3) {
                b.vx = 0.5f * lastGrassVelocity.vx;
                b.vy = 0.5f * lastGrassVelocity.vy;
                b.vz = 0.5f * lastGrassVelocity.vz;
                ++ballRocket;
            } else {
                b.vx = b.vy = b.vz = 0.0f;
                b.resting = true;
            }
            // GolfHoleBase slices both recordings and resets self.frame here.
            m.count = uint16_t(greenIn);
            frame = uint16_t(greenIn);
            resetAt = greenIn;
            cup = 0;
            continue;
        }
        if (cup && inCup(b, cupX, cupY, cupZ) && !didHoleBreak) {
            // Stock keeps simulating after this, but halves the body velocity
            // once.  A later grass contact can still revoke ballInHoleFrame.
            m.ballInFrame = frame;
            b.vx *= 0.5f; b.vy *= 0.5f; b.vz *= 0.5f;
            b.wx *= 0.5f; b.wy *= 0.5f; b.wz *= 0.5f;
            didHoleBreak = true;
        }
        if (p.rayHitGrass()) m.ballInFrame = 0;
        if(b.resting){if(m.count<kMaxMovieFrames){m.frames[m.count]=b;m.frameIds[m.count++]=uint16_t(frame+1);}break;}
    }
}
void recordSwing(Movie&m,Physics&p,const Ball&start,float power,float x,float y,float cupX,float cupY,float cupZ){recordRaw(m,p,start,power,x,y,cupX,cupY,cupZ);reduceMovie(m);}
} // namespace tt::golf

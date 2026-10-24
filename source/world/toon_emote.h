// Stock TTEmote action selection; pure math so late joins/subranges are host tested.
#pragma once
#include "world/toon_anim.h"
#include "world/toon_playback.h"
namespace tt::world {
struct EmotePose { ToonAnim clip=kAnimNeutral; float time=0,headH=0,headP=0; bool loop=false,body=true; };
inline bool validEmote(int id) { return id>=0 && id<=24; }
inline bool bodyEmote(int id) { return validEmote(id) && id!=2 && id!=16 && id!=17 && id!=18 && id!=19; }
inline float emoteDuration(int id,bool skirt=false,const char* torso=nullptr) {
    switch (id) {
    case 0:return 78/24.0f; case 1:return 40/24.0f; case 2:return 3;
    case 3:case 5:case 23:return 50/24.0f; case 4:return 4; case 6:return 123/24.0f;
    case 7:return 94/24.0f; case 8:return 160/24.0f; case 9:return 51/24.0f;
    case 10:case 12:return 55/24.0f; case 11:return 71/24.0f;
    case 13:return toonDuration(skirt?kAnimCurtsy:kAnimBow,torso);
    case 14:return 67/24.0f; case 15:return 20/18.0f;
    case 17:case 18:return .95f; case 20:return 3; case 21:return 4; case 22:return 2.5f; case 24:return 2;
    default:return 0;
    }
}
inline float headGesture(float t,bool yes) {
    const float lengths[7]={.1f,.15f,.15f,.15f,.15f,.15f,.1f};
    const float pitch[7]={-30,20,-20,20,-20,20,0};
    const float heading[7]={40,-40,40,-40,20,-20,0};
    float previous=0;
    for (int i=0;i<7;i++) {
        const float target=yes?pitch[i]:heading[i];
        if (t<=lengths[i]) { const float k=t>0?t/lengths[i]:0; return previous+(target-previous)*k; }
        t-=lengths[i]; previous=target;
    }
    return 0;
}
inline EmotePose emotePose(int id,float t,bool skirt=false) {
    EmotePose p;
    switch(id) {
    case 0:p.clip=kAnimWave;break; case 1:p.clip=kAnimJump;break;
    case 3:case 23:p.clip=kAnimAngry;break;
    case 4:p.loop=true;p.time=t*.4f;return p;
    case 5:p.clip=kAnimShrug;break; case 6:p.clip=kAnimVictory;break;
    case 7:p.clip=kAnimThink;p.time=t<47/24.0f?t:46/24.0f-(t-47/24.0f);return p;
    case 8:p.clip=kAnimBored;break; case 9:p.clip=kAnimApplause;break;
    case 10:p.clip=kAnimCringe;break;case 11:p.clip=kAnimConfused;break;
    case 12:p.clip=kAnimSlipForward;break;
    case 13:p.clip=skirt?kAnimCurtsy:kAnimBow;break;
    case 14:p.clip=kAnimSlipBackward;break;
    case 15:p.clip=kAnimVictory;p.time=t<10/18.0f?t*.75f:9/24.0f-(t-10/18.0f)*.75f;return p;
    case 17:p.body=false;p.headP=headGesture(t,true);return p;
    case 18:p.body=false;p.headH=headGesture(t,false);return p;
    case 20:p.clip=kAnimConked;p.time=t<42/24.0f?9/24.0f+t:70/24.0f+t-42/24.0f;return p;
    case 21:p.clip=kAnimBadPutt;p.time=t<31/18.0f?59/24.0f-t*.75f:29/24.0f+(t-31/18.0f)*.75f;
        if(p.time<29/24.0f) p.time=29/24.0f;
        if(p.time>59/24.0f) p.time=59/24.0f;
        return p;
    case 22:p.clip=kAnimLeft;p.time=t<16/24.0f?t:t<16/24.0f+1?15/24.0f:15/24.0f-(t-16/24.0f-1);return p;
    case 24:p.loop=true;p.time=t*10;return p;
    default:p.body=false;return p;
    }
    p.time=t;return p;
}
} // namespace tt::world

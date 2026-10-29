#include "world/toon_playback.h"
#include "world/toon_emote.h"
#include "ui/speedchat_data.h"
#include <cassert>
#include <cstdio>
using namespace tt::world;
bool near(float a,float b) { return std::fabs(a-b)<.0001f; }
int main() {
    static_assert(kAnimCount==51 && kAnimWave==4,"IDs");
    ToonPlayback p;
    p.begin(ClipMode::Once,.45f,-1,1,.5f);
    assert(near(p.sample(40/24.0f),.95f) && !p.finished(40/24.0f));
    p.update(10);assert(p.finished(40/24.0f) && near(p.sample(40/24.0f),40/24.0f));
    auto s=clipSample(100,12,20,false);assert(s.a==19 && s.b==19 && s.blend==0);
    p.begin(ClipMode::Once,0,2,-2,.25f);assert(near(p.sample(5),1.5f));
    p.update(2);assert(near(p.sample(5),0) && p.finished(5));
    p.begin(ClipMode::Once,2,0,1,.5f);assert(near(p.sample(5),1.5f));
    p.begin(ClipMode::Loop,1,2,1,1.25f);assert(near(p.sample(5),1.25f));
    p.begin(ClipMode::Hold,3/24.0f,-1,0);p.update(99);assert(near(p.sample(5),.125f) && !p.finished(5));
    s=clipSample(.125f,12,10,false);assert(s.a==1 && s.b==2 && near(s.blend,.5f));
    s=clipSample(1e38f,240,1,true,.001f);assert(s.a==0 && s.b==0);
    s=clipSample(1e38f,240,10,true);assert(s.a<10 && s.b<10);
    s=clipSample(-.01f,12,10,true);assert(s.a==9 && s.b==0);
    s=clipSample(2.02f,12,25,true,49/24.0f);assert(s.a==24 && s.b==0 && s.blend>.4f);
    s=clipSample(49/24.0f,12,25,true,49/24.0f);assert(s.a==0);
    assert(near(clipElapsed(20,UINT32_MAX-979),1));
    assert(clipElapsed(100,200)==0);
    p.begin(ClipMode::Once,0,2,1,clipElapsed(5000,4000));assert(near(p.sample(2),1));
    const auto token=p.token;p.begin(ClipMode::Once,0,2,1);assert(p.token==token+1 && p.elapsed==0);
    for(int id=0;id<25;id++) {
        assert(validEmote(id));
        for(int i=0;i<100;i++) { const auto pose=emotePose(id,emoteDuration(id)*i/100);assert(pose.clip>=0 && pose.clip<51 && std::isfinite(pose.time)); }
    }
    assert(!validEmote(-1) && !validEmote(25));
    assert(emotePose(16,0).body==false && emotePose(19,0).body==false);
    assert(emotePose(13,0,true).clip==kAnimCurtsy && emotePose(13,0,false).clip==kAnimBow);
    assert(emotePose(24,1).loop && near(emotePose(24,1).time,10));
    assert(near(emoteDuration(7),94/24.0f));assert(near(emoteDuration(15),20/18.0f));
    assert(near(headGesture(.95f,true),0) && near(headGesture(.95f,false),0));
    int menu=-1,emotes=0;
    for(int i=0;i<tt::sc::kNumMenus;i++) if(!std::strcmp(tt::sc::kMenus[i].title,"EMOTIONS")) menu=i;
    assert(menu>=0);
    const auto& m=tt::sc::kMenus[menu];
    for(int i=0;i<m.numItems;i++) {const auto& item=tt::sc::kItems[m.firstItem+i];assert(item.value==65535 && item.emote>=0 && item.emote<25 && item.emote!=16);++emotes;}
    assert(emotes==24);
    std::puts("PASS playback: forward/reverse/subrange/hold/restart, source-period loops, timestamp wrap/seek, 25 emotes and menu IDs");
}

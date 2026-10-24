#include "world/toon_avatar.h"
#include <cstring>
namespace tt::world {
namespace {
bool skirt(const ToonLook* look) { return look && look->torso && std::strlen(look->torso->key)>7 && look->torso->key[7]=='d'; }
const char* torsoKey(const ToonLook* look) { return look && look->torso?look->torso->key:nullptr; }
}
void ToonAvatar::play(ToonAnim anim,float rate) {
    if (anim<0 || anim>=kAnimCount) anim=kAnimNeutral;
    if (anim_!=anim || playback_.mode!=ClipMode::Loop) {
        anim_=anim; playback_.begin(ClipMode::Loop,0,-1,rate);
    }
    rate_=rate; playback_.rate=rate;
}
void ToonAvatar::playAction(ToonAnim anim,ClipMode mode,float rate,float start,float end,bool restart,float seekTime) {
    if (anim<0 || anim>=kAnimCount) return;
    if(emote_>=0) emoteFinished_=true;
    emote_=-1;
    if (restart || anim_!=anim || playback_.mode!=mode || playback_.start!=start || playback_.end!=end || playback_.rate!=rate) {
        anim_=anim; playback_.begin(mode,start,end,rate,seekTime);
    }
    rate_=rate;
}
void ToonAvatar::cancel() {
    const ToonLook* l=look_&&look_->ready()?look_:fallback_;
    if (l && l->cache) l->cache->forget(this);
    if(emote_>=0) emoteFinished_=true;
    emote_=-1; headOnly_=false;bodyPitch_=bodyZ_=0;
    anim_=kAnimNeutral; playback_.begin(ClipMode::Loop,0,-1);
}
bool ToonAvatar::finished() const { return playback_.finished(toonDuration(anim_,torsoKey(look_))); }
void ToonAvatar::update(float dt) {
    playback_.update(dt);
    if (emote_>=0) {
        emoteAge_+=dt;
        if (emoteAge_>=emoteDuration(emote_,skirt(look_),torsoKey(look_))) { emote_=-1; emoteFinished_=true; }
    }
}
bool ToonAvatar::playEmote(int id,float elapsed) {
    if (!validEmote(id)) { if (id==-1) emote_=-1; return id==-1; }
    emoteAge_=elapsed>0?elapsed:0;
    emoteFinished_=false;
    emote_=emoteAge_<emoteDuration(id,skirt(look_),torsoKey(look_))?id:-1;
    return true;
}
void ToonAvatar::playClip(const ToonClipSet* set,int index,bool loop) {
    if (set && index>=0 && index<set->count) playAction(set->clips[index],loop?ClipMode::Loop:ClipMode::Once);
}
void ToonClipSet::unload() { count=0; look=nullptr; }
void ToonClipSet::load(const ToonLook& l,const char* const* names,int n) {
    unload();look=&l;count=n<kMax?n:kMax;
    for(int i=0;i<count;i++) clips[i]=toonAnimByName(names[i]);
}
void ToonAvatar::draw(render::Renderer& renderer,const C3D_Mtx& world,const render::Tints* tintOverride) const {
    const ToonLook* look=look_&&look_->ready()?look_:fallback_;
    if (!look || !look->ready()) return;
    const render::Tints* tints=tintOverride?tintOverride:&look->tints;
    const C3D_Tex* textures[render::kTexSlotCount];look->slotTextures(textures);
    C3D_Mtx actorWorld=world;
    Mtx_RotateX(&actorWorld,bodyPitch_*0.01745329252f,true);
    Mtx_Translate(&actorWorld,0,0,bodyZ_,true);
    C3D_Mtx partWorld=actorWorld;Mtx_Scale(&partWorld,look->scale,look->scale,look->scale);
    ToonAnim anim=anim_;
    float time=playback_.sample(toonDuration(anim,torsoKey(look)));
    bool loop=playback_.mode==ClipMode::Loop;
    EmotePose emote;
    if (emote_>=0) {
        emote=emotePose(emote_,emoteAge_,skirt(look));
        if (emote.body) { anim=emote.clip;time=emote.time;loop=emote.loop; }
    }
    if (time<0) time=0;
    ToonCache::Pose pose;
    const bool streamed=look->cache && look->cache->sample(this,*look,anim,time,loop,&pose);
    ToonPart* parts[3]={look->legs,look->torso,look->head};
    for (int p=0;p<3;p++) {
        if (p==2) {
            Mtx_RotateZ(&partWorld,emote.headH*0.01745329252f,true);
            Mtx_RotateX(&partWorld,emote.headP*0.01745329252f,true);
            if (headOnly_) { partWorld=world; Mtx_Scale(&partWorld,look->scale,look->scale,look->scale); Mtx_RotateX(&partWorld,headPitch_*0.01745329252f,true); }
        }
        const render::Anim* clip=streamed && pose.parts[p]?pose.parts[p]:&parts[p]->neutral;
        const float blend=streamed?pose.blend[p]:0;
        if (!headOnly_ || p==2) {
            if (clip && clip->loaded()) renderer.drawMorph(parts[p]->model,*clip,0,1,blend,partWorld,tints,textures);
            else renderer.drawModel(parts[p]->model,partWorld,tints,textures);
        }
        if (p<2 && clip && clip->hasAttach()) {
            C3D_Mtx attach,next;clip->attach(0,1,blend,&attach);Mtx_Multiply(&next,&partWorld,&attach);partWorld=next;
        }
        if(p==2 && look->lashes && look->lashes->state==SlotState::Ready) renderer.drawModel(look->lashes->model,partWorld,tints);
    }
}
} // namespace tt::world

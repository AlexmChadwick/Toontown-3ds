#include "world/mint_office.h"
#include <cstdio>
namespace tt::world {
void MintOffice::unload() { room_.unload(); prop_.unload(); roomId_ = 0xffff; }
void MintOffice::handleEvent(const net::Event& ev, const net::Client& c) {
    if (ev.type == net::EventType::MintUp || ev.type == net::EventType::MintRoomUp) mint_ = true;
    if (ev.type == net::EventType::StageUp || ev.type == net::EventType::StageRoomUp) mint_ = false;
    if (ev.type == net::EventType::MintGone || ev.type == net::EventType::StageGone) { active_ = false; unload(); }
    if (ev.type == net::EventType::MintRoomUp || ev.type == net::EventType::StageRoomUp) {
        const net::FacilityRoom* rp = nullptr;
        for (int n = 0; n < 32; ++n) {
            const net::FacilityRoom& candidate = mint_ ? c.mintRoomSlot(n) : c.stageRoomSlot(n);
            if (candidate.active && candidate.doId == ev.doId) { rp = &candidate; break; }
        }
        if (!rp) return;
        const net::FacilityRoom& r = *rp;
        roomId_ = r.roomId; active_ = true;
        char path[128]; snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/%s/room_%02u.ttm",
                                  mint_ ? "mint" : "stage", roomId_);
        room_.load(path);
        char collisionPath[128];
        snprintf(collisionPath, sizeof(collisionPath), "sdmc:/3ds/toontown3ds/data/%s/room_%02u.ttc",
                 mint_ ? "mint" : "stage", roomId_);
        collision_.unload();
        collision_.load(collisionPath);
        spawned_ = false;
        prop_.load(mint_ ? "sdmc:/3ds/toontown3ds/data/mint/gear.ttm" :
                          "sdmc:/3ds/toontown3ds/data/stage/camera.ttm");
    }
}
void MintOffice::update(net::Client& c, float x, float y, float z, uint32_t now) {
    if (!c.mint() && !c.stage()) { active_ = false; return; }
    if (now - lastInteract_ > 750) {
        for (int n = 0; n < 32; ++n) {
            const net::FacilityRoom& r = mint_ ? c.mintRoomSlot(n) : c.stageRoomSlot(n);
            if (r.active && x*x+y*y+z*z < 36.0f && c.sendRoomEntityInteract(r.doId)) {
                lastInteract_ = now;
                break;
            }
        }
    }
}
void MintOffice::draw(render::Renderer& renderer, const net::Client&) const {
    if (!active_ || !room_.loaded()) return;
    C3D_Mtx m; Mtx_Identity(&m); renderer.drawModel(const_cast<render::Model&>(room_), m);
    if (prop_.loaded()) renderer.drawModel(const_cast<render::Model&>(prop_), m);
}
bool MintOffice::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    *x = 0.0f; *y = -10.0f; *z = 0.0f; *h = 0.0f;
    return true;
}

bool MintOffice::battleTarget(const net::Client& client, uint32_t* doId, float* x, float* y,
                              float* z, float* h) const {
    if (!active_) return false;
    for (int n = 0; n < 32; ++n) {
        const net::FacilityRoom& r = mint_ ? client.mintRoomSlot(n) : client.stageRoomSlot(n);
        if (r.active && r.roomId == roomId_ && r.numSuits) {
            *doId = r.suits[0]; *x = 0.0f; *y = 4.0f; *z = 0.0f; *h = 180.0f;
            return true;
        }
    }
    return false;
}

void MintOffice::shutdown() { active_ = false; collision_.unload(); spawned_ = false; unload(); }
}

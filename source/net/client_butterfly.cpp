// Playground ambient objects: DistributedButterfly (docs/design/butterflies.md) and Daisy
// Gardens' DistributedDGFlower.
#include "net/client.h"

#include "core/log.h"
#include "net/msg_types.h"

#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace tt::net {

static_assert(dc::sigEq(dc::DistributedButterfly::sig_setArea, "i16,i16"), "setArea layout changed");
static_assert(dc::sigEq(dc::DistributedButterfly::sig_setState, "i8,u8,u8,u16/10,i16"), "setState layout changed");
static_assert(dc::sigEq(dc::DistributedDGFlower::sig_setHeight, "u8/10"), "setHeight layout changed");

bool Client::applyButterflyField(Butterfly& b, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedButterfly::k_setArea) {
        b.playground = r.i16();
        b.area = r.i16();
    } else if (fieldId == dc::DistributedButterfly::k_setState) {
        b.state = r.i8();
        b.cur = r.u8();
        b.dest = r.u8();
        b.seconds = r.u16() / 10.0f;
        const int16_t ts = r.i16();
        b.startMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else {
        return skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::handleButterflyEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                                  uint16_t reqCount, bool other) {
    if (classId == dc::DistributedDGFlower::kClassId) {
        DGFlower f;
        f.doId = doId;
        bool ok = true;
        auto apply = [&](uint16_t field) {
            if (field == dc::DistributedDGFlower::k_setHeight) {
                f.height = r.u8() / 10.0f;
                return r.ok();
            }
            return skipField(r, field);
        };
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = apply(req[i]);
        if (ok && other) {
            const uint16_t count = r.u16();
            for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = apply(r.u16());
        }
        if (ok && r.ok()) {
            f.active = true;
            dgFlower_ = f;
        }
        return true;
    }
    if (classId != dc::DistributedButterfly::kClassId) return false;
    Butterfly* dst = nullptr;
    for (Butterfly& b : butterflies_) if (b.active && b.doId == doId) dst = &b;
    for (Butterfly& b : butterflies_) if (!dst && !b.active) dst = &b;
    if (!dst) {
        TT_LOG("net: no free slot for butterfly %u", unsigned(doId));
        return true;
    }
    Butterfly tmp;
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyButterflyField(tmp, r, req[i]);
    if (ok && other) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = applyButterflyField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) {
        TT_LOG("net: butterfly %u failed to parse", unsigned(doId));
        return true;
    }
    tmp.active = true;
    *dst = tmp;
    return true;
}

bool Client::handleButterflyField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (fieldId == dc::DistributedDGFlower::k_setHeight) {
        if (!dgFlower_.active || dgFlower_.doId != doId) return false;
        dgFlower_.height = r.u8() / 10.0f;
        dgFlower_.heightMs = now_;
        return true;
    }
    if (fieldId != dc::DistributedButterfly::k_setState && fieldId != dc::DistributedButterfly::k_setArea) return false;
    for (Butterfly& b : butterflies_) {
        if (b.active && b.doId == doId) {
            applyButterflyField(b, r, fieldId);
            return true;
        }
    }
    return false;
}

bool Client::sendDGFlowerTouch(bool enter) {
    if (state_ != State::InZone || !dgFlower_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (enter) {
        TT_NET_UPDATE(w, dgFlower_.doId, DistributedDGFlower, avatarEnter);
    } else {
        TT_NET_UPDATE(w, dgFlower_.doId, DistributedDGFlower, avatarExit);
    }
    return finish(w);
}

bool Client::butterflyLeft(uint32_t doId) {
    if (dgFlower_.active && dgFlower_.doId == doId) {
        dgFlower_.active = false;
        return true;
    }
    for (Butterfly& b : butterflies_) {
        if (b.active && b.doId == doId) {
            b.active = false;
            return true;
        }
    }
    return false;
}

void Client::clearButterflies() {
    for (Butterfly& b : butterflies_) b.active = false;
    dgFlower_.active = false;
}

} // namespace tt::net

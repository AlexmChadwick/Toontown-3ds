// DistributedButterfly: the playground butterflies (docs/design/butterflies.md).
#include "net/client.h"

#include "core/log.h"

namespace tt::net {

static_assert(dc::sigEq(dc::DistributedButterfly::sig_setArea, "i16,i16"), "setArea layout changed");
static_assert(dc::sigEq(dc::DistributedButterfly::sig_setState, "i8,u8,u8,u16/10,i16"), "setState layout changed");

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
    if (fieldId != dc::DistributedButterfly::k_setState && fieldId != dc::DistributedButterfly::k_setArea) return false;
    for (Butterfly& b : butterflies_) {
        if (b.active && b.doId == doId) {
            applyButterflyField(b, r, fieldId);
            return true;
        }
    }
    return false;
}

bool Client::butterflyLeft(uint32_t doId) {
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
}

} // namespace tt::net

// Platform badges (tools/server/platform/README.md): the "platform" section of tt::net::Client.
//
// The global TTPlatformManager (doId 4721, platform.dc read after toon.dc) is an uberdog: it is
// never generated to us. Once our toon is in a zone we send setMyPlatform(own), then batch the
// avIds of toons we see into requestPlatforms (at most 50 every 250 ms, each id once per
// session). setPlatforms answers and later pushes (registration, VR enter/leave, logout = 0)
// fill a fixed PlatformBook. CLIENT_HELLO keeps the stock hash (dc::kHash = 547155168).
//
// A server without the manager ejects us (117 / 118 naming doId 4721) on the first send. That
// marks the feature unsupported for the rest of the run, and the normal reconnect rejoins
// without it: no badges, nothing else lost.
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"

namespace tt::net {

// Same guard as client.cpp: outgoing fields must be clsend (Astron ejects anything else).
#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

#define TT_FSIG(cls, field, expect) \
    static_assert(dc::sigEq(dc::cls::sig_##field, expect), #cls "." #field " layout changed")
TT_FSIG(TTPlatformManager, setMyPlatform, "u8");
TT_FSIG(TTPlatformManager, requestPlatforms, "u32[]");
TT_FSIG(TTPlatformManager, setPlatforms, "u32[],u8[]");
#undef TT_FSIG
static_assert(dc::kHash == 547155168u, "CLIENT_HELLO must keep the stock otp.dc + toon.dc hash");

constexpr uint16_t kEjectMissingObject = 117;  // CLIENT_DISCONNECT_MISSING_OBJECT
constexpr uint16_t kEjectForbiddenField = 118; // CLIENT_DISCONNECT_FORBIDDEN_FIELD

} // namespace

void Client::setOwnPlatform(uint8_t p) {
    p = clampPlatform(p);
    if (p == platform_.own) return;
    platform_.own = p;
    platform_.announced = false; // platformUpdate re-sends it (VR-style change)
}

uint8_t Client::badgePlatformFor(uint32_t avId) const {
    if (platform_.unsupported) return kPlatformUnknown;
    const uint8_t p = platform_.book.get(avId);
    return shouldShowPlatformBadge(platform_.own, p, avId == avatarId_) ? p : uint8_t(kPlatformUnknown);
}

void Client::platformReset() {
    platform_.book.reset();
    platform_.announced = false;
    platform_.sentThisSession = false;
    platform_.lastFlushMs = 0;
    platform_.answers = 0;
}

void Client::platformNoteToon(uint32_t avId) {
    if (platform_.unsupported) return;
    platform_.book.note(avId, avatarId_, now_);
}

void Client::platformUpdate() {
    if (platform_.unsupported || state_ != State::InZone || avatarId_ == 0) return;
    if (!platform_.announced) {
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, kPlatformManagerId, TTPlatformManager, setMyPlatform);
        w.u8(platform_.own);
        if (!finish(w)) return;
        platform_.announced = platform_.sentThisSession = true;
        platform_.lastFlushMs = now_;
        TT_LOG("platform: own %s (%u) sent", platformName(platform_.own), unsigned(platform_.own));
        return;
    }
    if (now_ - platform_.lastFlushMs < kPlatformFlushMs || platform_.book.queued() == 0) return;
    platform_.lastFlushMs = now_;
    uint32_t ids[kMaxPlatformIdsPerRequest];
    const int n = platform_.book.take(ids, kMaxPlatformIdsPerRequest);
    if (n == 0) return;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, kPlatformManagerId, TTPlatformManager, requestPlatforms);
    w.u16(uint16_t(n * 4));
    for (int i = 0; i < n; i++) w.u32(ids[i]);
    if (finish(w)) TT_LOG("platform: requested %d ids (first %u)", n, unsigned(ids[0]));
}

bool Client::platformHandleSetField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (doId != kPlatformManagerId) return false;
    if (fieldId != dc::TTPlatformManager::k_setPlatforms) {
        r.skip(r.remaining()); // nothing else is sent to clients
        return true;
    }
    const uint16_t idBytes = r.u16();
    if (!r.ok() || idBytes % 4 != 0 || r.remaining() < uint32_t(idBytes) + 2u) {
        r.fail();
        return true;
    }
    const int n = idBytes / 4;
    const uint8_t* ids = r.cursor();
    r.skip(idBytes);
    const uint16_t pBytes = r.u16();
    if (!r.ok() || pBytes != n || r.remaining() < pBytes) {
        r.fail();
        return true;
    }
    const uint8_t* ps = r.cursor();
    r.skip(pBytes);
    for (int i = 0; i < n; i++) {
        const uint32_t avId = uint32_t(ids[i * 4]) | uint32_t(ids[i * 4 + 1]) << 8 | uint32_t(ids[i * 4 + 2]) << 16 |
                              uint32_t(ids[i * 4 + 3]) << 24;
        const uint8_t p = clampPlatform(ps[i]);
        if (avId == 0 || avId == avatarId_) continue;
        if (platform_.book.get(avId) != p) {
            const RemoteToon* t = findToon(avId);
            TT_LOG("platform: toon %u \"%s\" is %s%s", unsigned(avId), t ? t->name : "?", platformName(p),
                   shouldShowPlatformBadge(platform_.own, p, false) ? " (badge)" : "");
        }
        platform_.book.set(avId, p, now_);
    }
    ++platform_.answers;
    return true;
}

void Client::platformHandleEject(uint16_t code, const char* reason) {
    if (!platform_.sentThisSession || platform_.answers != 0) return;
    if (code != kEjectMissingObject && code != kEjectForbiddenField) return;
    char id[16];
    snprintf(id, sizeof(id), " %u", unsigned(kPlatformManagerId));
    if (!strstr(reason, id)) return;
    platform_.unsupported = true;
    TT_LOG("platform: server has no TTPlatformManager (%u); badges off for this run", unsigned(code));
}

} // namespace tt::net

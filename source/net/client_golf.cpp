#include "net/client.h"

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "game/golf_logic.h"
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(w, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

// Every field this file parses: a DC layout change must fail the build, not the generate.
#define TT_GSIG(cls, field, expect) \
    static_assert(dc::sigEq(dc::cls::sig_##field, expect), #cls "." #field " layout changed")
TT_GSIG(DistributedGolfKart, setGolfCourse, "i8");
TT_GSIG(DistributedGolfKart, setPosHpr, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10");
TT_GSIG(DistributedGolfKart, setState, "string,i16");
TT_GSIG(DistributedGolfKart, fillSlot0, "u32");
TT_GSIG(DistributedGolfKart, fillSlot3, "u32");
TT_GSIG(DistributedGolfKart, emptySlot0, "u32,i16");
TT_GSIG(DistributedGolfKart, emptySlot3, "u32,i16");
TT_GSIG(DistributedGolfKart, setGolfZone, "u32,u16");
TT_GSIG(DistributedGolfCourse, setGolferIds, "u32[]");
TT_GSIG(DistributedGolfCourse, setCourseId, "i8");
TT_GSIG(DistributedGolfCourse, setCurHoleIndex, "i8");
TT_GSIG(DistributedGolfCourse, setCurHoleDoId, "u32");
TT_GSIG(DistributedGolfCourse, setCourseReady, "i8,i16[],i8");
TT_GSIG(DistributedGolfCourse, setScores, "i16[]");
TT_GSIG(DistributedGolfCourse, setCourseAbort, "u32");
TT_GSIG(DistributedGolfHole, setHoleId, "i8");
TT_GSIG(DistributedGolfHole, setTimingCycleLength, "u32/1000");
TT_GSIG(DistributedGolfHole, setGolfCourseDoId, "u32");
TT_GSIG(DistributedGolfHole, setGolferIds, "u32[]");
TT_GSIG(DistributedGolfHole, golfersTurn, "u32");
TT_GSIG(DistributedGolfHole, golferChooseTee, "u32");
#undef TT_GSIG

void readU32Array(DatagramReader& r, uint32_t* out, int cap, int& count) {
    const uint16_t bytes = r.u16();
    count = 0;
    const uint32_t end = r.pos() + bytes;
    while (r.ok() && r.pos() < end) {
        const uint32_t value = r.u32();
        if (count < cap) out[count++] = value;
    }
    if (r.pos() != end) r.fail();
}

void readI16Array(DatagramReader& r, int16_t* out, int cap, int& count) {
    const uint16_t bytes = r.u16();
    count = 0;
    const uint32_t end = r.pos() + bytes;
    while (r.ok() && r.pos() < end) {
        const int16_t value = r.i16();
        if (count < cap) out[count++] = value;
    }
    if (r.pos() != end) r.fail();
}

float fixed(DatagramReader& r, float scale) { return static_cast<float>(r.i32()) / scale; }

} // namespace

bool Client::applyGolfKartField(GolfKart& k, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace G = dc::DistributedGolfKart;
    if (fieldId == G::k_setGolfCourse) {
        k.courseId = r.i8();
    } else if (fieldId == G::k_setPosHpr) {
        k.x = r.i16() / 10.0f; k.y = r.i16() / 10.0f; k.z = r.i16() / 10.0f;
        k.h = r.i16() / 10.0f; k.p = r.i16() / 10.0f; k.r = r.i16() / 10.0f;
    } else if (fieldId == G::k_setState) {
        r.string(k.state, sizeof(k.state));
        r.i16(); // timestamp: the kart's countdown is drawn from our own clock
        k.stateMs = now_;
    } else if (fieldId >= G::k_fillSlot0 && fieldId <= G::k_fillSlot3) {
        k.slots[fieldId - G::k_fillSlot0] = r.u32();
    } else if (fieldId >= G::k_emptySlot0 && fieldId <= G::k_emptySlot3) {
        k.slots[fieldId - G::k_emptySlot0] = 0;
        r.u32(); r.i16();
    } else if (fieldId == G::k_setGolfZone) {
        k.golfZone = r.u32();
        const uint16_t course = r.u16();
        k.courseId = static_cast<int8_t>(course);
        if (announce) pushEvent(EventType::GolfZone, k.golfZone);
    } else {
        skipField(r, fieldId);
    }
    if (announce && r.ok() && fieldId != G::k_setGolfZone) pushEvent(EventType::GolfKartState, k.doId);
    return r.ok();
}

bool Client::applyGolfCourseField(GolfCourse& c, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace G = dc::DistributedGolfCourse;
    if (fieldId == G::k_setGolferIds) {
        readU32Array(r, c.golfers, kGolfMaxGolfers, c.numGolfers);
    } else if (fieldId == G::k_setCourseId) {
        c.courseId = r.i8();
    } else if (fieldId == G::k_setCurHoleIndex) {
        c.currentHole = r.i8();
    } else if (fieldId == G::k_setCurHoleDoId) {
        c.currentHoleDoId = r.u32();
    } else if (fieldId == G::k_setCourseReady) {
        c.numHoles = r.i8();
        int ignored = 0; int16_t holes[32] = {};
        readI16Array(r, holes, 32, ignored);
        c.par = r.i8();
    } else if (fieldId == G::k_setScores) {
        readI16Array(r, c.scores, kGolfMaxGolfers, c.numScores);
    } else if (fieldId == G::k_setReward) {
        skipField(r, fieldId);
        c.reward = true;
    } else if (fieldId == G::k_setCourseExit || fieldId == G::k_setCourseAbort) {
        if (fieldId == G::k_setCourseAbort) r.u32();
        c.exiting = true;
    } else {
        skipField(r, fieldId);
    }
    TT_LOG("golf: course %u field %u hole %d golfers %d scores %d reward %d exit %d", unsigned(c.doId),
           unsigned(fieldId), int(c.currentHole), c.numGolfers, c.numScores, int(c.reward), int(c.exiting));
    if (announce && r.ok()) pushEvent(EventType::GolfCourseState, c.doId);
    return r.ok();
}

bool Client::applyGolfHoleField(GolfHole& h, DatagramReader& r, uint16_t fieldId, bool announce) {
    namespace G = dc::DistributedGolfHole;
    if (fieldId == G::k_setHoleId) {
        h.holeId = r.i8();
    } else if (fieldId == G::k_setTimingCycleLength) {
        h.cycleLength = r.u32() / 1000.0f;
    } else if (fieldId == G::k_setGolfCourseDoId) {
        h.courseDoId = r.u32();
    } else if (fieldId == G::k_setGolferIds) {
        readU32Array(r, h.golfers, kGolfMaxGolfers, h.numGolfers);
    } else if (fieldId == G::k_golfersTurn || fieldId == G::k_golferChooseTee) {
        h.currentGolfer = r.u32();
    } else if (fieldId == G::k_ballMovie2Client) {
        const uint32_t cycle = r.u32();
        (void)cycle;
        r.u32();
        const uint16_t movieBytes = r.u16();
        const uint32_t end = r.pos() + movieBytes;
        while (r.ok() && r.pos() < end) {
            r.i16(); h.ballX = fixed(r, 100000.0f); h.ballY = fixed(r, 100000.0f); h.ballZ = fixed(r, 100000.0f);
        }
        if (r.pos() != end) r.fail();
        const uint16_t spinBytes = r.u16(); r.skip(spinBytes);
        r.u16(); r.u16(); r.u16();
        const uint16_t commonBytes = r.u16(); r.skip(commonBytes);
        h.swingSeq++;
        TT_LOG("golf: movie received hole %u frames %u", unsigned(h.doId), unsigned(movieBytes / 14));
    } else {
        skipField(r, fieldId);
    }
    if (announce && r.ok()) pushEvent(EventType::GolfHoleState, h.doId);
    return r.ok();
}

bool Client::handleGolfEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                             uint16_t reqCount, bool other) {
    if (classId == dc::DistributedPhysicsWorld::kClassId) {
        for (uint16_t i = 0; i < reqCount && r.ok(); ++i) skipField(r, req[i]);
        if (other) { const uint16_t n = r.u16(); for (uint16_t i = 0; i < n && r.ok(); ++i) skipField(r, r.u16()); }
        return r.ok();
    }
    if (classId == dc::DistributedGolfKart::kClassId) {
        GolfKart tmp{}; tmp.doId = doId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyGolfKartField(tmp, r, req[i], false);
        if (ok && other) { const uint16_t n = r.u16(); for (uint16_t i = 0; i < n && ok; ++i) ok = applyGolfKartField(tmp, r, r.u16(), false); }
        if (!ok) return false;
        tmp.active = true; golfKart_ = tmp; pushEvent(EventType::GolfKartUp, doId); return true;
    }
    if (classId == dc::DistributedGolfCourse::kClassId) {
        GolfCourse tmp{}; tmp.doId = doId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyGolfCourseField(tmp, r, req[i], false);
        if (ok && other) { const uint16_t n = r.u16(); for (uint16_t i = 0; i < n && ok; ++i) ok = applyGolfCourseField(tmp, r, r.u16(), false); }
        if (!ok) return false;
        tmp.active = true; golfCourse_ = tmp; TT_LOG("golf: course arrived %u golfers %d", unsigned(doId), tmp.numGolfers); pushEvent(EventType::GolfCourseUp, doId); return true;
    }
    GolfHole tmp{}; tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyGolfHoleField(tmp, r, req[i], false);
    if (ok && other) { const uint16_t n = r.u16(); for (uint16_t i = 0; i < n && ok; ++i) ok = applyGolfHoleField(tmp, r, r.u16(), false); }
    if (!ok) return false;
    tmp.active = true; golfHole_ = tmp; TT_LOG("golf: hole arrived %u id %d", unsigned(doId), int(tmp.holeId)); pushEvent(EventType::GolfHoleUp, doId); return true;
}

bool Client::handleGolfField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    if (golfKart_.active && golfKart_.doId == doId) return applyGolfKartField(golfKart_, r, fieldId, true);
    if (golfCourse_.active && golfCourse_.doId == doId) return applyGolfCourseField(golfCourse_, r, fieldId, true);
    if (golfHole_.active && golfHole_.doId == doId) return applyGolfHoleField(golfHole_, r, fieldId, true);
    return false;
}

bool Client::handleGolfLeaving(uint32_t doId) {
    if (golfKart_.active && golfKart_.doId == doId) { golfKart_ = GolfKart{}; pushEvent(EventType::GolfKartState, doId); return true; }
    if (golfCourse_.active && golfCourse_.doId == doId) { golfCourse_ = GolfCourse{}; pushEvent(EventType::GolfCourseState, doId); return true; }
    if (golfHole_.active && golfHole_.doId == doId) { golfHole_ = GolfHole{}; pushEvent(EventType::GolfHoleState, doId); return true; }
    return false;
}

void Client::clearGolfObjects() {
    golfKart_ = GolfKart{};
    golfCourse_ = GolfCourse{};
    golfHole_ = GolfHole{};
}

bool Client::sendGolfKartBoard() {
    if (!golfKart_.active || state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfKart_.doId, DistributedGolfKart, requestBoard);
    return finish(w);
}

bool Client::sendGolfKartExit() {
    if (!golfKart_.active || state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfKart_.doId, DistributedGolfKart, requestExit);
    return finish(w);
}

bool Client::sendGolfReadyCourse() {
    if (!golfCourse_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfCourse_.doId, DistributedGolfCourse, setAvatarReadyCourse);
    return finish(w);
}

bool Client::sendGolfJoined() {
    if (!golfCourse_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfCourse_.doId, DistributedGolfCourse, setAvatarJoined);
    TT_LOG("golf: course joined %u", unsigned(golfCourse_.doId));
    return finish(w);
}

bool Client::sendGolfReadyHole() {
    if (golfHole_.active) {
        DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
        TT_NET_UPDATE(w, golfHole_.doId, DistributedGolfHole, setAvatarReadyHole);
        return finish(w);
    }
    if (!golfCourse_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfCourse_.doId, DistributedGolfCourse, setAvatarReadyHole);
    return finish(w);
}

bool Client::sendGolfExitCourse() {
    if (!golfCourse_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfCourse_.doId, DistributedGolfCourse, setAvatarExited);
    return finish(w);
}

bool Client::sendGolfDoneReward() {
    if (!golfCourse_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfCourse_.doId, DistributedGolfCourse, setDoneReward);
    return finish(w);
}

bool Client::sendGolfTee(uint8_t tee) {
    if (!golfHole_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfHole_.doId, DistributedGolfHole, setAvatarTee);
    w.u8(tee);
    return finish(w);
}

bool Client::sendGolfSwing(float cycleTime, int32_t power, float x, float y, float z, float dirX, float dirY) {
    if (!golfHole_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfHole_.doId, DistributedGolfHole, postSwingState);
    w.u32(static_cast<uint32_t>(cycleTime * 1000.0f));
    w.i32(power);
    w.i32(static_cast<int32_t>(x * 1000.0f)); w.i32(static_cast<int32_t>(y * 1000.0f)); w.i32(static_cast<int32_t>(z * 1000.0f));
    w.i32(static_cast<int32_t>(dirX * 1000.0f)); w.i32(static_cast<int32_t>(dirY * 1000.0f));
    w.u16(0);
    w.u16(0);
    TT_LOG("golf: swing sent hole %u power %ld", unsigned(golfHole_.doId), long(power));
    return finish(w);
}

bool Client::sendGolfTurnDone() {
    if (!golfHole_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfHole_.doId, DistributedGolfHole, turnDone);
    return finish(w);
}

bool Client::sendGolfBallInHole() {
    if (!golfHole_.active) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, golfHole_.doId, DistributedGolfHole, ballInHole);
    return finish(w);
}

#undef TT_NET_UPDATE
} // namespace tt::net

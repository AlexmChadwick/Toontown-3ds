// Playground ambient objects: DistributedButterfly (docs/design/butterflies.md), Daisy
// Gardens' DistributedDGFlower and Goofy Speedway's DistributedLeaderBoard.
#include "net/client.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

#define TT_NET_UPDATE(w, doId, cls, field)                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace tt::net {

static_assert(dc::sigEq(dc::DistributedButterfly::sig_setArea, "i16,i16"), "setArea layout changed");
static_assert(dc::sigEq(dc::DistributedButterfly::sig_setState, "i8,u8,u8,u16/10,i16"), "setState layout changed");
static_assert(dc::sigEq(dc::DistributedDGFlower::sig_setHeight, "u8/10"), "setHeight layout changed");
static_assert(dc::sigEq(dc::DistributedLeaderBoard::sig_setPosHpr, "i16/10,i16/10,i16/10,i16/10,i16/10,i16/10"),
              "setPosHpr layout changed");
static_assert(dc::sigEq(dc::DistributedLeaderBoard::sig_setDisplay, "blob"), "setDisplay layout changed");

namespace {

// DistributedLeaderBoardAI.updateDisplay sends pickle.dumps((trackName, periodName, leaderList))
// with the AI's default protocol (4): strings, floats or ints, tuples, one list, memo refs.
// A small stack machine for exactly those opcodes; anything else fails the decode.
struct PkVal {
    enum Kind : uint8_t { Mark, Str, Num, Seq } kind = Mark;
    double num = 0.0;
    const uint8_t* str = nullptr;
    uint32_t len = 0;
    int16_t first = 0, count = 0; // Seq: children in PkMachine::kids
};

struct PkMachine {
    static constexpr int kMax = 96;
    PkVal stack[kMax];
    int top = 0;
    PkVal memo[kMax];
    int memoCount = 0;
    PkVal kids[kMax * 2];
    int kidCount = 0;

    bool push(const PkVal& v) {
        if (top >= kMax) return false;
        stack[top++] = v;
        return true;
    }
    // Pops the top n values into a new sequence.
    bool collect(int n, PkVal* out) {
        if (n > top || kidCount + n > kMax * 2) return false;
        out->kind = PkVal::Seq;
        out->first = static_cast<int16_t>(kidCount);
        out->count = static_cast<int16_t>(n);
        for (int i = top - n; i < top; ++i) kids[kidCount++] = stack[i];
        top -= n;
        return true;
    }
    int markDepth() const {
        for (int i = top - 1; i >= 0; --i) if (stack[i].kind == PkVal::Mark) return top - 1 - i;
        return -1;
    }
};

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool pickleRun(PkMachine& m, const uint8_t* d, uint32_t n) {
    uint32_t i = 0;
    auto need = [&](uint32_t k) { return i + k <= n; };
    while (i < n) {
        const uint8_t op = d[i++];
        PkVal v;
        switch (op) {
        case 0x80: if (!need(1)) return false; i += 1; break;            // PROTO
        case 0x95: if (!need(8)) return false; i += 8; break;            // FRAME
        case 0x94:                                                       // MEMOIZE
            if (m.top < 1 || m.memoCount >= PkMachine::kMax) return false;
            m.memo[m.memoCount++] = m.stack[m.top - 1];
            break;
        case 'q': case 'r': {                                            // BINPUT, LONG_BINPUT
            const uint32_t k = op == 'q' ? 1 : 4;
            if (!need(k) || m.top < 1) return false;
            const uint32_t idx = k == 1 ? d[i] : le32(d + i);
            i += k;
            if (idx >= PkMachine::kMax) return false;
            m.memo[idx] = m.stack[m.top - 1];
            if (static_cast<int>(idx) >= m.memoCount) m.memoCount = idx + 1;
            break;
        }
        case 'h': case 'j': {                                            // BINGET, LONG_BINGET
            const uint32_t k = op == 'h' ? 1 : 4;
            if (!need(k)) return false;
            const uint32_t idx = k == 1 ? d[i] : le32(d + i);
            i += k;
            if (static_cast<int>(idx) >= m.memoCount || !m.push(m.memo[idx])) return false;
            break;
        }
        case 0x8c: case 'X': {                                           // SHORT_BINUNICODE, BINUNICODE
            const uint32_t k = op == 0x8c ? 1 : 4;
            if (!need(k)) return false;
            v.len = k == 1 ? d[i] : le32(d + i);
            i += k;
            if (!need(v.len)) return false;
            v.kind = PkVal::Str;
            v.str = d + i;
            i += v.len;
            if (!m.push(v)) return false;
            break;
        }
        case 'G': {                                                      // BINFLOAT (big endian)
            if (!need(8)) return false;
            uint64_t u = 0;
            for (int b = 0; b < 8; ++b) u = (u << 8) | d[i + b];
            i += 8;
            v.kind = PkVal::Num;
            std::memcpy(&v.num, &u, 8);
            if (!m.push(v)) return false;
            break;
        }
        case 'K': case 'M': case 'J': {                                  // BININT1, BININT2, BININT
            const uint32_t k = op == 'K' ? 1 : op == 'M' ? 2 : 4;
            if (!need(k)) return false;
            v.kind = PkVal::Num;
            v.num = k == 1 ? d[i] : k == 2 ? (d[i] | (d[i + 1] << 8)) : static_cast<int32_t>(le32(d + i));
            i += k;
            if (!m.push(v)) return false;
            break;
        }
        case '(': if (!m.push(v)) return false; break;                   // MARK
        case ']': case ')': if (!m.collect(0, &v) || !m.push(v)) return false; break; // EMPTY_LIST/TUPLE
        case 0x85: case 0x86: case 0x87:                                 // TUPLE1..3
            if (!m.collect(op - 0x84, &v) || !m.push(v)) return false;
            break;
        case 't': {                                                      // TUPLE (to the mark)
            const int k = m.markDepth();
            if (k < 0 || !m.collect(k, &v)) return false;
            m.stack[m.top - 1] = v; // replaces the mark
            break;
        }
        case 'a': case 'e': {                                            // APPEND, APPENDS
            const int k = op == 'a' ? 1 : m.markDepth();
            const int listAt = m.top - k - (op == 'a' ? 1 : 2);
            if (k < 0 || listAt < 0 || m.stack[listAt].kind != PkVal::Seq) return false;
            PkVal& list = m.stack[listAt];
            // The list's children must end the kids array to grow in place (true for one list).
            if (list.first + list.count != m.kidCount && list.count != 0) return false;
            if (list.count == 0) list.first = static_cast<int16_t>(m.kidCount);
            PkVal tmp;
            if (!m.collect(k, &tmp)) return false;
            list.count = static_cast<int16_t>(list.count + k);
            if (op == 'e') --m.top; // the mark
            break;
        }
        case '.': return m.top == 1;                                     // STOP
        default: return false;
        }
    }
    return false;
}

void copyStr(char* dst, size_t cap, const PkVal& v, uint32_t maxChars) {
    // name[:22] counts characters, not UTF-8 bytes.
    uint32_t bytes = 0, chars = 0;
    while (bytes < v.len && chars < maxChars) {
        uint32_t step = 1;
        const uint8_t c = v.str[bytes];
        if (c >= 0xF0) step = 4; else if (c >= 0xE0) step = 3; else if (c >= 0xC0) step = 2;
        if (bytes + step > v.len || bytes + step >= cap) break;
        bytes += step;
        ++chars;
    }
    std::memcpy(dst, v.str, bytes);
    dst[bytes] = 0;
}

// DistributedLeaderBoard.display: title rows, then one row per record. Upstream's loop raises
// IndexError at row len(list), so shorter lists leave the later rows as they were.
bool decodeBoardDisplay(const uint8_t* data, uint32_t len, GsLeaderBoard& b) {
    static PkMachine m;
    m = PkMachine{};
    if (!pickleRun(m, data, len)) return false;
    const PkVal& root = m.stack[0];
    if (root.kind != PkVal::Seq || root.count != 3) return false;
    const PkVal& track = m.kids[root.first];
    const PkVal& period = m.kids[root.first + 1];
    const PkVal& list = m.kids[root.first + 2];
    if (track.kind != PkVal::Str || period.kind != PkVal::Str || list.kind != PkVal::Seq) return false;
    copyStr(b.period, sizeof(b.period), period, 64);
    copyStr(b.track, sizeof(b.track), track, 64);
    for (int r = 0; r < GsLeaderBoard::kRows && r < list.count; ++r) {
        const PkVal& rec = m.kids[list.first + r];
        if (rec.kind != PkVal::Seq || rec.count < 2) return false;
        const PkVal& time = m.kids[rec.first];
        const PkVal& name = m.kids[rec.first + 1];
        if (time.kind != PkVal::Num || name.kind != PkVal::Str) return false;
        copyStr(b.names[r], sizeof(b.names[r]), name, 22);
        // secs, hundredths = divmod(time, 1); min, sec = divmod(secs, 60); '%02d' truncates.
        const double secs = std::floor(time.num);
        const int hundredths = static_cast<int>((time.num - secs) * 100.0);
        const int total = static_cast<int>(secs);
        std::snprintf(b.times[r], sizeof(b.times[r]), "%02d:%02d:%02d", total / 60, total % 60, hundredths);
    }
    return true;
}

bool applyBoardField(GsLeaderBoard& b, DatagramReader& r, uint16_t fieldId) {
    if (fieldId == dc::DistributedLeaderBoard::k_setPosHpr) {
        for (float& f : b.pos) f = r.i16() / 10.0f;
        for (float& f : b.hpr) f = r.i16() / 10.0f;
        return r.ok();
    }
    if (fieldId == dc::DistributedLeaderBoard::k_setDisplay) {
        const Bytes blob = r.blob();
        if (!r.ok()) return false;
        if (decodeBoardDisplay(blob.data, blob.len, b)) {
            ++b.version;
        } else {
            TT_LOG("leaderboard %lu: setDisplay pickle not understood (%lu bytes)",
                   static_cast<unsigned long>(b.doId), static_cast<unsigned long>(blob.len));
        }
        return true;
    }
    return false;
}

} // namespace

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
    if (classId == dc::DistributedLeaderBoard::kClassId) {
        GsLeaderBoard b;
        b.doId = doId;
        std::strcpy(b.period, "Score Title"); // buildTitleRow's text until setDisplay
        for (int i = 0; i < GsLeaderBoard::kRows; ++i) {
            std::strcpy(b.names[i], "-");
            std::strcpy(b.times[i], "-");
        }
        bool ok = true;
        auto apply = [&](uint16_t field) {
            if (field == dc::DistributedLeaderBoard::k_setPosHpr || field == dc::DistributedLeaderBoard::k_setDisplay)
                return applyBoardField(b, r, field);
            return skipField(r, field);
        };
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = apply(req[i]);
        if (ok && other) {
            const uint16_t count = r.u16();
            for (uint16_t i = 0; i < count && ok && r.ok(); ++i) ok = apply(r.u16());
        }
        if (!ok || !r.ok()) return true;
        GsLeaderBoard* dst = nullptr;
        for (GsLeaderBoard& s : leaderBoards_) if (s.active && s.doId == doId) dst = &s;
        for (GsLeaderBoard& s : leaderBoards_) if (!dst && !s.active) dst = &s;
        if (!dst) {
            TT_LOG("leaderboard %lu: no free slot", static_cast<unsigned long>(doId));
            return true;
        }
        b.active = true;
        b.version = dst->version + 1;
        *dst = b;
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
    if (fieldId == dc::DistributedLeaderBoard::k_setPosHpr || fieldId == dc::DistributedLeaderBoard::k_setDisplay) {
        for (GsLeaderBoard& b : leaderBoards_) {
            if (b.active && b.doId == doId) return applyBoardField(b, r, fieldId) || true;
        }
        return false;
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
    for (GsLeaderBoard& b : leaderBoards_) {
        if (b.active && b.doId == doId) {
            b.active = false;
            return true;
        }
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
    for (GsLeaderBoard& b : leaderBoards_) b.active = false;
}

} // namespace tt::net

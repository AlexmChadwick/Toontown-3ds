#include "net/datagram.h"

namespace tt::net {

namespace {

constexpr int kMaxDepth = 8; // struct/array nesting in toon.dc is at most 3

uint16_t progU16(const uint8_t* pc) { return uint16_t(pc[0] | (pc[1] << 8)); }

// Runs a type program [pc, end) (or until kOpEnd) against the reader. v may be
// null (skip only).
bool exec(const uint8_t* pc, const uint8_t* end, DatagramReader& r, FieldVisitor* v, int depth) {
    if (depth > kMaxDepth) { r.fail(); return false; }
    while (pc < end && r.ok()) {
        uint8_t op = *pc++;
        switch (op) {
        case dc::kOpEnd: return r.ok();
        case dc::kOpI8: { int64_t x = r.i8(); if (v) v->onInt(x); break; }
        case dc::kOpI16: { int64_t x = r.i16(); if (v) v->onInt(x); break; }
        case dc::kOpI32: { int64_t x = r.i32(); if (v) v->onInt(x); break; }
        case dc::kOpI64: { int64_t x = r.i64(); if (v) v->onInt(x); break; }
        case dc::kOpU8:
        case dc::kOpChar: { uint64_t x = r.u8(); if (v) v->onUint(x); break; }
        case dc::kOpU16: { uint64_t x = r.u16(); if (v) v->onUint(x); break; }
        case dc::kOpU32: { uint64_t x = r.u32(); if (v) v->onUint(x); break; }
        case dc::kOpU64: { uint64_t x = r.u64(); if (v) v->onUint(x); break; }
        case dc::kOpF64: { double x = r.f64(); if (v) v->onFloat(x); break; }
        case dc::kOpString:
        case dc::kOpBlob: { Bytes b = r.blob(); if (v && r.ok()) v->onBytes(op, b); break; }
        case dc::kOpBlob32: { Bytes b = r.blob32(); if (v && r.ok()) v->onBytes(op, b); break; }
        case dc::kOpFixbytes: {
            Bytes b = r.fixed(progU16(pc));
            pc += 2;
            if (v && r.ok()) v->onBytes(op, b);
            break;
        }
        case dc::kOpVararray: {
            uint16_t subLen = progU16(pc);
            const uint8_t* sub = pc + 2;
            pc = sub + subLen;
            Bytes b = r.blob();
            if (!r.ok()) return false;
            if (!v) break; // skip by byte length
            DatagramReader inner(b.data, b.len);
            v->onArrayBegin();
            while (inner.ok() && inner.remaining() > 0) {
                uint32_t before = inner.pos();
                if (!exec(sub, sub + subLen, inner, v, depth + 1)) break;
                if (inner.pos() == before) inner.fail(); // zero-size element: malformed
            }
            v->onArrayEnd();
            if (!inner.ok()) { r.fail(); return false; }
            break;
        }
        case dc::kOpFixarray: {
            uint16_t count = progU16(pc);
            uint16_t subLen = progU16(pc + 2);
            const uint8_t* sub = pc + 4;
            pc = sub + subLen;
            if (v) v->onArrayBegin();
            for (uint16_t i = 0; i < count && r.ok(); ++i) exec(sub, sub + subLen, r, v, depth + 1);
            if (v) v->onArrayEnd();
            break;
        }
        default: r.fail(); return false;
        }
    }
    return r.ok();
}

} // namespace

const dc::FieldDesc* findField(uint16_t fieldId) {
    int lo = 0, hi = dc::kNumFields - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t id = dc::kFields[mid].id;
        if (id == fieldId) return &dc::kFields[mid];
        if (id < fieldId) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const char* fieldName(uint16_t fieldId) {
    const dc::FieldDesc* f = findField(fieldId);
    return f ? dc::kFieldNames[f - dc::kFields] : "?";
}

const dc::ClassDesc* findClass(uint16_t classId) {
    for (const dc::ClassDesc& c : dc::kClasses) {
        if (c.classId == classId) return &c;
    }
    return nullptr;
}

bool classHasField(const dc::ClassDesc& cls, uint16_t fieldId) {
    const uint16_t* m = dc::kFieldLists + cls.memberOffset;
    int lo = 0, hi = int(cls.memberCount) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (m[mid] == fieldId) return true;
        if (m[mid] < fieldId) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

bool skipField(DatagramReader& r, uint16_t fieldId) {
    const dc::FieldDesc* f = findField(fieldId);
    if (!f) { r.fail(); return false; }
    if (f->fixedSize != 0xffff) return r.skip(f->fixedSize);
    return exec(dc::kTypeCode + f->typeOffset, dc::kTypeCode + dc::kTypeCodeSize, r, nullptr, 0);
}

bool readField(DatagramReader& r, uint16_t fieldId, FieldVisitor& v) {
    const dc::FieldDesc* f = findField(fieldId);
    if (!f) { r.fail(); return false; }
    return exec(dc::kTypeCode + f->typeOffset, dc::kTypeCode + dc::kTypeCodeSize, r, &v, 0);
}

} // namespace tt::net

// Astron datagrams: little-endian writer/reader over caller-owned fixed buffers,
// plus generic field skip/read driven by the dcgen descriptor tables
// (dc_generated.h). No allocation, no exceptions: the writer and reader set a
// sticky error flag on overflow/underflow and return zeros from then on.
#pragma once

#include <cstdint>
#include <cstring>

#include "net/dc_generated.h"

namespace tt::net {

class DatagramWriter {
public:
    DatagramWriter(uint8_t* buf, uint32_t capacity) : buf_(buf), cap_(capacity) {}

    void reset() { size_ = 0; overflow_ = false; }
    uint32_t size() const { return size_; }
    const uint8_t* data() const { return buf_; }
    bool ok() const { return !overflow_; }

    void u8(uint8_t v) { put(&v, 1); }
    void u16(uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; put(b, 2); }
    void u32(uint32_t v) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        put(b, 4);
    }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void i8(int8_t v) { u8(uint8_t(v)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void f64(double v) { uint64_t u; memcpy(&u, &v, 8); u64(u); }
    // string / blob: u16 length + bytes.
    void string(const char* s) { blob(s, uint32_t(strlen(s))); }
    void blob(const void* p, uint32_t len) {
        if (len > 0xffff) { overflow_ = true; return; }
        u16(uint16_t(len));
        put(p, len);
    }
    void bytes(const void* p, uint32_t len) { put(p, len); }

    // Reserve a u16 now, fill it later (array byte lengths, frame lengths).
    uint32_t reserveU16() { uint32_t at = size_; u16(0); return at; }
    void patchU16(uint32_t at, uint16_t v) {
        if (at + 2 <= size_) { buf_[at] = uint8_t(v); buf_[at + 1] = uint8_t(v >> 8); }
    }

private:
    void put(const void* p, uint32_t n) {
        if (overflow_ || n > cap_ - size_) { overflow_ = true; return; }
        if (n) memcpy(buf_ + size_, p, n);
        size_ += n;
    }

    uint8_t* buf_;
    uint32_t cap_;
    uint32_t size_ = 0;
    bool overflow_ = false;
};

// A view into the datagram (not NUL terminated).
struct Bytes {
    const uint8_t* data = nullptr;
    uint32_t len = 0;
};

class DatagramReader {
public:
    DatagramReader() = default;
    DatagramReader(const uint8_t* p, uint32_t len) : p_(p), len_(len) {}

    bool ok() const { return !error_; }
    void fail() { error_ = true; pos_ = len_; }
    uint32_t pos() const { return pos_; }
    uint32_t remaining() const { return len_ - pos_; }
    const uint8_t* cursor() const { return p_ + pos_; }

    uint8_t u8() { return need(1) ? p_[pos_++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(p_[pos_] | (p_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = uint32_t(p_[pos_]) | (uint32_t(p_[pos_ + 1]) << 8) |
                     (uint32_t(p_[pos_ + 2]) << 16) | (uint32_t(p_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    uint64_t u64() { uint64_t lo = u32(); uint64_t hi = u32(); return lo | (hi << 32); }
    int8_t i8() { return int8_t(u8()); }
    int16_t i16() { return int16_t(u16()); }
    int32_t i32() { return int32_t(u32()); }
    int64_t i64() { return int64_t(u64()); }
    double f64() { uint64_t u = u64(); double d; memcpy(&d, &u, 8); return d; }

    Bytes fixed(uint32_t n) {
        Bytes b;
        if (!need(n)) return b;
        b.data = p_ + pos_;
        b.len = n;
        pos_ += n;
        return b;
    }
    Bytes blob() { uint16_t n = u16(); return fixed(n); }    // string or blob
    Bytes blob32() { uint32_t n = u32(); return fixed(n); }
    bool skip(uint32_t n) { if (!need(n)) return false; pos_ += n; return true; }

    // Copy a u16-length string into dst (always NUL terminated, truncated).
    void string(char* dst, uint32_t cap) {
        Bytes b = blob();
        uint32_t n = b.len < cap - 1 ? b.len : cap - 1;
        if (n) memcpy(dst, b.data, n);
        dst[n] = 0;
    }

private:
    bool need(uint32_t n) {
        if (error_ || n > len_ - pos_) { fail(); return false; }
        return true;
    }

    const uint8_t* p_ = nullptr;
    uint32_t len_ = 0;
    uint32_t pos_ = 0;
    bool error_ = false;
};

// ---- descriptor-table driven field access ----

const dc::FieldDesc* findField(uint16_t fieldId);   // nullptr if not in table
const char* fieldName(uint16_t fieldId);            // "?" if not in table
const dc::ClassDesc* findClass(uint16_t classId);   // nullptr if no table
bool classHasField(const dc::ClassDesc& cls, uint16_t fieldId);
// Required-field id lists in wire order (see dcgen.py).
inline const uint16_t* requiredOther(const dc::ClassDesc& c) { return dc::kFieldLists + c.otherOffset; }
inline const uint16_t* requiredOwner(const dc::ClassDesc& c) { return dc::kFieldLists + c.ownerOffset; }

// Skip one field's value. Returns false (and fails the reader) on unknown
// field or truncated data.
bool skipField(DatagramReader& r, uint16_t fieldId);

// Decodes a field value into a stream of typed callbacks. Variable arrays are
// walked element by element between arrayBegin/arrayEnd.
class FieldVisitor {
public:
    virtual ~FieldVisitor() = default;
    virtual void onInt(int64_t) {}
    virtual void onUint(uint64_t) {}
    virtual void onFloat(double) {}
    virtual void onBytes(uint8_t /*op: kOpString/kOpBlob/...*/, Bytes) {}
    virtual void onArrayBegin() {}
    virtual void onArrayEnd() {}
};
bool readField(DatagramReader& r, uint16_t fieldId, FieldVisitor& v);

} // namespace tt::net

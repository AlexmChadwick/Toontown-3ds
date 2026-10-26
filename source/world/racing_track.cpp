#include "world/racing_track.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace tt::racing {
void Track::unload() {
    std::free(points);
    std::free(floors_);
    points = nullptr;
    floors_ = nullptr;
    count = numStarts = numGags = 0;
    numFloors_ = 0;
}
bool Track::load(const char *path, unsigned track) {
    unload();
    struct Header {
        char magic[4];
        uint16_t version, track;
        uint32_t count;
        float start, middle;
        uint32_t starts, gags, floors;
    } header{};
    static_assert(sizeof(Header) == 32, "TRC1 header");
    FILE *file = std::fopen(path, "rb");
    if (!file)
        return false;
    bool valid = std::fread(&header, sizeof(header), 1, file) == 1 &&
                 !std::memcmp(header.magic, "TRC1", 4) && header.version == 1 &&
                 header.track == track && header.count == 4000 && header.starts >= 4 &&
                 header.starts <= 8 && header.gags <= 32 && header.floors > 0 &&
                 header.floors <= 32768 && std::isfinite(header.start) &&
                 std::isfinite(header.middle) && header.start >= 0 && header.start < 1 &&
                 header.middle >= 0 && header.middle < 1;
    if (valid) {
        points = static_cast<CurvePoint *>(std::malloc(header.count * sizeof(CurvePoint)));
        floors_ = static_cast<Floor *>(std::malloc(header.floors * sizeof(Floor)));
        valid = points && floors_ &&
                std::fread(points, sizeof(CurvePoint), header.count, file) == header.count &&
                std::fread(starts, sizeof(starts[0]), header.starts, file) == header.starts &&
                std::fread(gags, sizeof(Vector), header.gags, file) == header.gags &&
                std::fread(floors_, sizeof(Floor), header.floors, file) == header.floors &&
                std::fgetc(file) == EOF;
    }
    std::fclose(file);
    if (!valid) {
        unload();
        return false;
    }
    for (unsigned i = 0; i < header.count; ++i) {
        if (!std::isfinite(points[i].x + points[i].y + points[i].z + points[i].surface)) {
            unload();
            return false;
        }
    }
    for (unsigned i = 0; i < header.starts; ++i) {
        for (float value : starts[i])
            if (!std::isfinite(value)) {
                unload();
                return false;
            }
    }
    for (unsigned i = 0; i < header.gags; ++i) {
        if (!std::isfinite(gags[i].x) || !std::isfinite(gags[i].y) || !std::isfinite(gags[i].z)) {
            unload();
            return false;
        }
    }
    for (unsigned i = 0; i < header.floors; ++i) {
        const auto &floor = floors_[i];
        for (float value : floor.a)
            if (!std::isfinite(value)) {
                unload();
                return false;
            }
        for (float value : floor.b)
            if (!std::isfinite(value)) {
                unload();
                return false;
            }
        for (float value : floor.c)
            if (!std::isfinite(value)) {
                unload();
                return false;
            }
        if (!std::isfinite(floor.code) || floor.code < 0 || floor.code > 5 ||
            std::floor(floor.code) != floor.code) {
            unload();
            return false;
        }
    }
    count = int(header.count);
    numStarts = int(header.starts);
    numGags = int(header.gags);
    numFloors_ = header.floors;
    start = header.start;
    middle = header.middle;
    return true;
}
Surface Track::surface(float x, float y, float z) const {
    float best = 1e30f;
    Surface output = Surface::Unknown;
    for (unsigned i = 0; i < numFloors_; ++i) {
        const Floor &f = floors_[i];
        const float ux = f.b[0] - f.a[0], uy = f.b[1] - f.a[1];
        const float vx = f.c[0] - f.a[0], vy = f.c[1] - f.a[1];
        const float det = ux * vy - uy * vx;
        if (std::fabs(det) < .00001f)
            continue;
        const float px = x - f.a[0], py = y - f.a[1];
        const float a = (px * vy - py * vx) / det, b = (ux * py - uy * px) / det;
        if (a < -.00001f || b < -.00001f || a + b > 1.00001f)
            continue;
        const float height = f.a[2] + a * (f.b[2] - f.a[2]) + b * (f.c[2] - f.a[2]);
        const float difference = std::fabs(height - z);
        if (difference < best) {
            best = difference;
            output = Surface(int(f.code));
        }
    }
    return output;
}
} // namespace tt::racing

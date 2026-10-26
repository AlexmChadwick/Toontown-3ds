#pragma once
#include "world/racing_logic.h"
namespace tt::racing {
class Track {
  public:
    ~Track() { unload(); }
    bool load(const char *path, unsigned track);
    void unload();
    Surface surface(float x, float y, float z) const;
    bool loaded() const { return points != nullptr; }
    CurvePoint *points = nullptr;
    int count = 0, numStarts = 0, numGags = 0;
    float start = 0, middle = 0;
    float starts[8][6] = {};
    Vector gags[32];

  private:
    struct Floor {
        float a[3], b[3], c[3], code;
    };
    Floor *floors_ = nullptr;
    unsigned numFloors_ = 0;
};
} // namespace tt::racing

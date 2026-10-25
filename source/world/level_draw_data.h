#pragma once
#include "world/level_spec.h"
namespace tt::world {
// Only immutable TLV properties live here. Network state and moving transforms
// remain sampled every frame; a spec revision invalidates this derived data.
struct LevelDrawData {
    float matrix[16] = {};
    float scale = 1;
    float gridX = 0, gridY = 0;
    int modelType = 0;
    bool hasGridX = false, hasGridY = false;
};
inline LevelDrawData prepareLevelDraw(const LevelSpec &spec, const LevelEntity &entity) {
    LevelDrawData data;
    for (int row = 0; row < 3; ++row) {
        data.matrix[row * 4] = entity.world[9 + row];
        data.matrix[row * 4 + 1] = entity.world[6 + row];
        data.matrix[row * 4 + 2] = entity.world[3 + row];
        data.matrix[row * 4 + 3] = entity.world[row];
    }
    data.matrix[12] = 1;
    data.scale = spec.number(entity, "scale", 1);
    data.modelType = spec.integer(entity, "modelType", 0);
    data.hasGridX = spec.getNumber(entity, "gridScaleX", &data.gridX);
    data.hasGridY = spec.getNumber(entity, "gridScaleY", &data.gridY);
    return data;
}
} // namespace tt::world

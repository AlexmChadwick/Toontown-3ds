#pragma once
namespace tt {
// Stable bounded selection. Equal distances retain ascending source indices,
// matching repeated strict-less-than scans; the caller supplies the same cutoff.
template <int Limit, class Active, class Distance>
void nearestIndices(int count, Active active, Distance distance, float cutoff, int (&output)[Limit]) {
    float best[Limit];
    for (int slot = 0; slot < Limit; ++slot) {
        output[slot] = -1;
        best[slot] = cutoff;
    }
    for (int index = 0; index < count; ++index) {
        if (!active(index))
            continue;
        const float value = distance(index);
        for (int slot = 0; slot < Limit; ++slot) {
            if (!(value < best[slot]))
                continue;
            for (int move = Limit - 1; move > slot; --move) {
                best[move] = best[move - 1];
                output[move] = output[move - 1];
            }
            best[slot] = value;
            output[slot] = index;
            break;
        }
    }
}
} // namespace tt

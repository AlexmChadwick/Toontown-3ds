// Photo Fun tripod aim: swivel directions and limits (upstream __localPhotoMoveTask), and
// autoplay steering that must land photos on matching subjects in the TTC hood.
#include <cmath>
#include <cstdio>
#include "game/photo_logic.h"

using namespace tt::photogame;

int main() {
    int failures = 0;
    auto check = [&failures](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) failures++;
    };

    // Upstream: LEFT adds PHOTO_ROTATION_VEL (turns left), UP adds PHOTO_ANGLE_VEL, pitch clamps.
    Swivel left;
    stepSwivel(left, 1.0f, 0.0f, 0.1f);
    check(std::fabs(left.h - kPhotoRotationVel * 0.1f) < 1e-4f, "left turn adds rotation velocity");
    Swivel right;
    stepSwivel(right, -1.0f, 0.0f, 0.1f);
    check(right.h < 0.0f, "right turn subtracts rotation velocity");
    Swivel up;
    for (int i = 0; i < 400; ++i) stepSwivel(up, 0.0f, 1.0f, 0.05f);
    check(std::fabs(up.p - kPhotoAngleMax) < 1e-4f, "pitch clamps at PHOTO_ANGLE_MAX");
    Swivel down;
    for (int i = 0; i < 400; ++i) stepSwivel(down, 0.0f, -1.0f, 0.05f);
    check(std::fabs(down.p - kPhotoAngleMin) < 1e-4f, "pitch clamps at PHOTO_ANGLE_MIN");
    Swivel wrap{179.0f, 0.0f};
    stepSwivel(wrap, 1.0f, 0.0f, 0.2f);
    check(wrap.h > -180.0f && wrap.h <= 180.0f, "heading wraps into (-180, 180]");

    // Autoplay on TTC (hood 2000) with the upstream tripod eye.
    const auto* data = getHoodPhotoData(2000);
    check(data->hasRoot == 1, "TTC has an upstream photoRoot");
    const float ex = data->rootX + data->tripOffsetX;
    const float ey = data->rootY + data->tripOffsetY;
    const float ez = data->rootZ + data->tripOffsetZ + kTripodPivotZ;

    Simulation sim;
    sim.init(1, 2000, 3);
    Swivel sw{data->startH, data->startP};
    int shots = 0, hits = 0, scored = 0;
    float cooldown = 0.0f;
    const float dt = 0.1f;
    for (float t = 0.0f; t < 60.0f && sim.filmUsed[0] < sim.maxFilm; t += dt) {
        cooldown -= dt;
        const bool aligned = planAutoShot(sim, ex, ey, ez, sw, t, dt);
        if (!aligned || cooldown > 0.0f) continue;
        cooldown = 1.0f;
        const PhotoShotResult r = sim.evaluateShot(0, ex, ey, ez, sw.h, sw.p, t);
        shots++;
        if (r.hitSubject) hits++;
        if (r.newHighScore) scored++;
    }
    std::printf("autoaim shots=%d hits=%d scored=%d film=%d\n", shots, hits, scored, sim.filmUsed[0]);
    check(shots > 0 && hits == shots, "every autoplay shot lands on a subject");
    check(scored > 0, "autoplay scores at least one assignment");
    return failures == 0 ? 0 : 1;
}

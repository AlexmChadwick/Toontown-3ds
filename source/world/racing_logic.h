#pragma once
#include <cstdint>
namespace tt::racing {
struct Vector {
    float x = 0, y = 0, z = 0;
};
enum class Surface : uint8_t { Asphalt, Gravel, Dirt, Grass, Ice, Unknown };
struct Vehicle {
    Vector position, velocity;
    float heading = 0, pitch = 0, roll = 0, lean = 0;
    float accumulator = 0;
    Surface surface = Surface::Gravel;
    bool stopped = true, turbo = false;
    float hitMultiplier = 1;
    float speed() const;
    // DistributedVehicle.__watchControls, stock proRacer=0; fixed 60Hz Euler integration.
    void update(float dt, float horizontal, float vertical);
};
struct CurvePoint {
    float x, y, z, surface;
};
struct Progress {
    int pole = 0, laps = 0;
    float fraction = 0;
    bool initialized = false, wrongWay = false;
    void reset(const CurvePoint *points, int count, Vector position, float start);
    bool update(const CurvePoint *points, int count, Vector position, float heading, float start);
};
struct Pie {
    Vector position, velocity, direction;
    float lifetime = 3.2f;
    float age = 0, accumulator = 0, previousDistance = 0;
    bool active = false, targeted = false;
    void launch(Vector origin, Vector ownerVelocity, float heading, bool hasTarget);
    bool update(float dt, Vector target, float targetSpeed);
};
struct Controls {
    float horizontal, vertical;
};
// Diagnostic-only steering; never writes position, laps, ownership or results.
Controls diagnosticControls(const CurvePoint *points, int count, int pole, const Vehicle &vehicle);
int trackIndex(unsigned id);
const char *trackName(unsigned id);
} // namespace tt::racing

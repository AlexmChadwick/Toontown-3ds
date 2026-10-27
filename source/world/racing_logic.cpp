#include "world/racing_logic.h"
#include <algorithm>
#include <cmath>
namespace tt::racing {
namespace {
constexpr float kRadians = 3.14159265358979323846f / 180;
constexpr float kStep = 1.0f / 60; // DistributedVehicle.physicsDt
float length(Vector v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
float wrap(float value) { return value - std::floor(value); }
} // namespace
void Pie::launch(Vector origin, Vector ownerVelocity, float heading, bool hasTarget) {
    *this = Pie{};
    position = origin;
    const float h = heading * kRadians;
    velocity = {ownerVelocity.x - std::sin(h) * 50, ownerVelocity.y + std::cos(h) * 50,
                ownerVelocity.z + 50};
    direction = {-std::sin(h), std::cos(h), 0};
    active = true;
    targeted = hasTarget;
    lifetime = hasTarget ? 8 : 8 / 2.5f;
}
bool Pie::update(float dt, Vector target, float targetSpeed) {
    if (!active)
        return false;
    dt = std::clamp(dt, 0.0f, 1.0f);
    age += dt;
    if (age >= lifetime) {
        active = false;
        return false;
    }
    Vector aim{target.x - position.x, target.y - position.y, target.z - position.z};
    const float distance = length(aim);
    if (targeted && distance < 7) {
        active = false;
        return true;
    }
    if (targeted && distance > 0) {
        aim.x /= distance;
        aim.y /= distance;
        aim.z /= distance;
        direction = aim;
    }
    aim = direction;
    const float timeRatio = age / 8;
    float engine = 100;
    if (targeted) {
        // Piejectile computes a targetAim but actually looks at targetPos.
        const bool movingAway = distance - 10 * kStep > previousDistance;
        const float base = movingAway ? 150 : 10;
        const float multiplier = movingAway ? 1 : .5f;
        engine = base * (1 + timeRatio) + (targetSpeed + distance) * multiplier * (1 + timeRatio);
    }
    previousDistance = distance;
    const float friction = .2f + .8f * timeRatio;
    accumulator += dt;
    while (accumulator + .000001f >= kStep) {
        accumulator -= kStep;
        const float horizontal = std::sqrt(aim.x * aim.x + aim.y * aim.y);
        const float lift = targeted ? 12 : 3;
        const Vector up = horizontal > .00001f ? Vector{-aim.x * aim.z / horizontal,
                                                        -aim.y * aim.z / horizontal, horizontal}
                                               : Vector{0, 0, 1};
        const Vector force{aim.x * engine + up.x * lift - friction * velocity.x,
                           aim.y * engine + up.y * lift - friction * velocity.y,
                           aim.z * engine + up.z * lift - friction * velocity.z};
        position.x += velocity.x * kStep + .5f * force.x * kStep * kStep;
        position.y += velocity.y * kStep + .5f * force.y * kStep * kStep;
        position.z += velocity.z * kStep + .5f * force.z * kStep * kStep;
        velocity.x += force.x * kStep;
        velocity.y += force.y * kStep;
        velocity.z += force.z * kStep;
    }
    return false;
}
Controls diagnosticControls(const CurvePoint *points, int count, int pole, const Vehicle &vehicle) {
    if (!points || count < 2 || pole < 0 || pole >= count)
        return {0, 0};
    // 60 of the 4,000 curve samples ahead: nearer targets sit behind Corkscrew Coliseum's and
    // Airborne Acres' inner walls, and a kart steered into a wall stalls against it.
    const auto &point = points[(pole + 60) % count];
    const float heading =
        std::atan2(-(point.x - vehicle.position.x), point.y - vehicle.position.y) / kRadians;
    const float error = std::remainder(heading - vehicle.heading, 360.0f);
    // Upstream arrow-key driving is binary. The diagnostic only supplies steering;
    // speed shaping here silently reduced its throttle through every turn.
    return {std::clamp(-error / 30, -1.0f, 1.0f), 1.0f};
}
int trackIndex(unsigned id) {
    constexpr unsigned kIds[] = {0, 1, 20, 21, 40, 41, 60, 61, 62, 63, 64, 65}; // RaceGlobals
    for (int i = 0; i < 12; ++i)
        if (kIds[i] == id)
            return i;
    return -1;
}
const char *trackName(unsigned id) {
    constexpr const char *kNames[] = {
        "screwball_stadium",  "screwball_stadium_rev",  "rustic_raceway",
        "rustic_raceway_rev", "city_circuit",           "city_circuit_rev",
        "corkscrew_coliseum", "corkscrew_coliseum_rev", "airborne_acres",
        "airborne_acres_rev", "blizzard_boulevard",     "blizzard_boulevard_rev"};
    const int index = trackIndex(id);
    return index >= 0 ? kNames[index] : nullptr;
}
float Vehicle::speed() const { return length(velocity); }
void Vehicle::update(float dt, float horizontal, float vertical) {
    dt = std::clamp(dt, 0.0f, 1.0f); // maxPhysicsDt
    horizontal = std::clamp(horizontal, -1.0f, 1.0f);
    vertical = std::clamp(vertical, -1.0f, 1.0f);
    float currentSpeed = speed();
    const float oldHeading = heading;
    const float rotation = !stopped && currentSpeed > 1 ? horizontal * dt * 40 * -1.2f : 0;
    heading = wrap((heading + rotation) / 360) * 360;
    const float velocityHeading = surface == Surface::Ice ? oldHeading + rotation * .25f : heading;
    float acceleration = std::clamp(25 - pitch, 15.0f, 25.0f) * 2;
    if (surface == Surface::Ice)
        acceleration *= .4f;
    const float engine = stopped ? 0 : vertical * acceleration + (turbo ? acceleration * 1.5f : 0);
    const float h = heading * kRadians, vh = velocityHeading * kRadians;
    const Vector forward{-std::sin(vh), std::cos(vh), -std::clamp((3 - pitch) * .02f, -.2f, .2f)};
    const float friction = surface == Surface::Dirt    ? .3f
                           : surface == Surface::Grass ? .4f
                           : surface == Surface::Ice   ? .01f
                                                       : .2f;
    accumulator += dt;
    while (accumulator + .000001f >= kStep) {
        accumulator -= kStep;
        const Vector force{-std::sin(h) * engine - friction * velocity.x,
                           std::cos(h) * engine - friction * velocity.y, -friction * velocity.z};
        // Panda LinearEulerIntegrator advances position using mean old/new velocity.
        position.x += velocity.x * kStep + .5f * force.x * kStep * kStep;
        position.y += velocity.y * kStep + .5f * force.y * kStep * kStep;
        position.z += velocity.z * kStep + .5f * force.z * kStep * kStep;
        velocity.x += force.x * kStep;
        velocity.y += force.y * kStep;
        velocity.z += force.z * kStep;
        const float drift = std::fabs(lean) * 16 + 15;
        velocity = {(velocity.x * hitMultiplier * drift + forward.x * currentSpeed) / (drift + 1),
                    (velocity.y * hitMultiplier * drift + forward.y * currentSpeed) / (drift + 1),
                    (velocity.z * hitMultiplier * drift + forward.z * currentSpeed) / (drift + 1)};
        currentSpeed = speed();
        if (currentSpeed > 600) {
            const float factor = 600 / currentSpeed;
            velocity.x *= factor;
            velocity.y *= factor;
            velocity.z *= factor;
            currentSpeed = 600;
        }
        const float increment = stopped ? 0 : horizontal * kStep * 40;
        lean = std::clamp((lean + increment) * std::min(currentSpeed, 150.0f) / 162, -10.0f, 10.0f);
    }
}
void Vehicle::wallShove(float shoveX, float shoveY) {
    const float length = std::sqrt(shoveX * shoveX + shoveY * shoveY);
    const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    if (length < .001f || speed <= 0)
        return;
    const float nx = shoveX / length, ny = shoveY / length;
    const float into = velocity.x * nx + velocity.y * ny;
    // angle = -dot(normalize(vel), normalize(force)): only a kart moving into the wall is in
    // contact. The into-wall component goes, then friction = dynamic coef (.1) x angle, so a
    // grazing slide along a barrier keeps nearly all its speed and a head-on hit loses 10%.
    const float angle = -into / speed;
    if (angle <= 0)
        return;
    const float keep = 1 - .1f * angle;
    velocity.x = (velocity.x - nx * into) * keep;
    velocity.y = (velocity.y - ny * into) * keep;
    velocity.z *= keep;
}
void Progress::reset(const CurvePoint *points, int count, Vector position, float start) {
    *this = Progress{};
    float best = 1e30f;
    for (int i = 0; i < count; ++i) {
        const float dx = position.x - points[i].x, dy = position.y - points[i].y;
        const float distance = dx * dx + dy * dy;
        if (distance < best) {
            best = distance;
            pole = i;
        }
    }
    update(points, count, position, 0, start);
    // DistributedRace starts with currLapT=0, so a grid behind the line first yields lap -1.
    laps = fraction > .5f ? -1 : 0;
}
bool Progress::update(const CurvePoint *points, int count, Vector position, float heading,
                      float start) {
    if (count < 2 || !points)
        return false;
    int direction = 0;
    float ratio = 0, dx = 0, dy = 0;
    for (int visited = 0; visited < count; ++visited) {
        const auto &a = points[pole];
        const auto &b = points[(pole + 1) % count];
        dx = b.x - a.x;
        dy = b.y - a.y;
        const float squared = dx * dx + dy * dy;
        ratio = squared > 0 ? ((position.x - a.x) * dx + (position.y - a.y) * dy) / squared : 0;
        if (ratio < 0 && direction != 1) {
            pole = (pole + count - 1) % count;
            direction = -1;
        } else if (ratio > 1 && direction != -1) {
            pole = (pole + 1) % count;
            direction = 1;
        } else
            break;
    }
    // raceWatcher interpolates by squared projected length, including its legacy nonlinearity.
    const float next = wrap((pole + ratio * ratio) / count - start);
    const int oldLap = laps;
    if (initialized) {
        if (next - fraction < -.5f)
            ++laps;
        else if (next - fraction > .5f)
            --laps;
    }
    fraction = next;
    initialized = true;
    const float dot = -std::sin(heading * kRadians) * dx + std::cos(heading * kRadians) * dy;
    if (dot < 0)
        wrongWay = true;
    else if (dot > .1f)
        wrongWay = false;
    return oldLap != laps;
}
} // namespace tt::racing

#include "world/cogdo_maze.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
namespace tt::world {
namespace {
constexpr float kDegrees = .01745329252f;
Vec3 rotate(Vec3 point, float heading) {
    const float sine = std::sin(heading * kDegrees);
    const float cosine = std::cos(heading * kDegrees);
    return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine, point.z};
}
constexpr int kBarriers[12][2] = {{7, 34},  {24, 34}, {41, 34}, {7, 17},  {24, 17}, {41, 17},
                                  {17, 41}, {17, 24}, {17, 7},  {34, 41}, {34, 24}, {34, 7}};
constexpr int kMoveX[] = {0, 0, -1, 1};
constexpr int kMoveY[] = {1, -1, 0, 0};
constexpr int kOpposite[] = {1, 0, 3, 2};
constexpr float kHeading[] = {0, 180, 90, 270};
} // namespace
float cogdoDifficulty(int32_t overrideValue, uint32_t exterior, int32_t playground) {
    if (overrideValue >= 0 && overrideValue != kCogdoNoDifficultyOverride)
        return std::clamp(overrideValue / 65536.0f, 0.0f, 1.0f);
    const uint32_t hood = playground >= 0 ? playground : exterior / 1000 * 1000;
    switch (hood) {
    case 1000:
        return .2f;
    case 5000:
        return .4f;
    case 4000:
        return .6f;
    case 3000:
        return .8f;
    case 9000:
        return 1;
    default:
        return 0;
    }
}
Vec3 CogdoMaze::tile(int x, int y) {
    return {(x - 26) * 3.0f, (y - 26) * 3.0f, 0};
}
void CogdoMaze::cell(Vec3 point, int *x, int *y) {
    *x = static_cast<int>(point.x / 3 + 26);
    *y = static_cast<int>(point.y / 3 + 26);
}
bool CogdoMaze::accessible(int x, int y) const {
    return x >= 0 && y >= 0 && x < kCogdoMazeSize && y < kCogdoMazeSize && cells[y][x] != 1;
}
bool CogdoMaze::walkable(int x, int y) const {
    return x > 0 && y > 0 && x < kCogdoMazeSize && y < kCogdoMazeSize && !cells[y][x] && !cells[y - 1][x] &&
           !cells[y][x - 1] && !cells[y - 1][x - 1];
}
bool CogdoMaze::load(uint32_t seed, const uint8_t counts[3], float difficulty, const char *directory) {
    uint8_t tables[9][4][16][16];
    char path[192];
    std::snprintf(path, sizeof(path), "%smaze_cells.bin", directory);
    FILE *file = std::fopen(path, "rb");
    char magic[4];
    uint16_t version = 0;
    uint16_t count = 0;
    const bool valid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "CGM1", 4) &&
                       std::fread(&version, 2, 1, file) == 1 && version == 1 &&
                       std::fread(&count, 2, 1, file) == 1 && count == 9 &&
                       std::fread(tables, sizeof(tables), 1, file) == 1 && std::fgetc(file) == EOF;
    if (file)
        std::fclose(file);
    if (!valid)
        return false;
    for (auto &drop : drops)
        drop = CogdoMazeDrop{};
    nextDrop = 0;
    game::catchgame::RandomNumGen parent(seed);
    game::catchgame::RandomNumGen random(parent);
    for (int index = 0; index < 12; ++index)
        barriers[index] = index;
    random.shuffle(barriers, 12); // Upstream consumes these draws even though all openings are cleared below.
    for (int index = 0; index < 9; ++index)
        quadrant[index] = index;
    random.shuffle(quadrant, 9);
    for (uint8_t &rotation : angle)
        rotation = static_cast<uint8_t>(random.randint(0, 3));
    std::memset(cells, 1, sizeof(cells));
    for (int index = 0; index < 9; ++index) {
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const uint8_t value = tables[quadrant[index]][angle[index]][y][x];
                if (value > 1)
                    return false;
                cells[1 + (index / 3) * 17 + y][1 + (index % 3) * 17 + x] =
                    index == 1 && y < 6 && value == 0 ? 2 : value;
            }
    }
    for (const auto &barrier : kBarriers) {
        for (int offset = 0; offset < 4; ++offset) {
            const bool horizontal = barrier[0] != 17 && barrier[0] != 34;
            cells[barrier[1] + (horizontal ? 0 : offset)][barrier[0] + (horizontal ? offset : 0)] = 0;
        }
    }
    for (int index = 0; index < kCogdoMazeSize; ++index) {
        if (!cells[26][index])
            cells[26][index] = 2;
        if (!cells[index][26])
            cells[index][26] = 2;
    }
    difficulty_ = std::clamp(difficulty, 0.0f, 1.0f);
    if (!initializeSuits(parent, counts))
        return false;
    for (int index = 0; index < numSuits; ++index) {
        suits[index].random.~RandomNumGen();
        new (&suits[index].random) game::catchgame::RandomNumGen(seed + index * 10);
    }
    std::snprintf(path, sizeof(path), "%smaze_coolers.bin", directory);
    file = std::fopen(path, "rb");
    bool coolersValid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "CGW1", 4) &&
                        std::fread(&version, 2, 1, file) == 1 && version == 1 &&
                        std::fread(&count, 2, 1, file) == 1 && count <= 64;
    numCoolers = 0;
    for (unsigned record = 0; coolersValid && record < count; ++record) {
        uint32_t model = 0;
        float pose[4] = {};
        coolersValid =
            std::fread(&model, 4, 1, file) == 1 && model < 9 && std::fread(pose, sizeof(pose), 1, file) == 1;
        for (float value : pose)
            coolersValid &= std::isfinite(value) && std::fabs(value) < 3276;
        for (int index = 0; coolersValid && index < 9; ++index) {
            if (quadrant[index] != model || numCoolers >= 64)
                continue;
            const Vec3 local = rotate({pose[0], pose[1], pose[2]}, angle[index] * 90);
            CogdoMazeCooler &cooler = coolers[numCoolers++];
            cooler.position = {local.x + (index % 3 - 1) * 51, local.y + (index / 3 - 1) * 51, local.z};
            cooler.heading = pose[3] + angle[index] * 90;
            const Vec3 offset = rotate({0, -1.5f, 0}, cooler.heading);
            cooler.trigger = {cooler.position.x + offset.x, cooler.position.y + offset.y,
                              cooler.position.z + offset.z};
        }
    }
    if (file) {
        coolersValid &= std::fgetc(file) == EOF;
        std::fclose(file);
    }
    std::sort(coolers, coolers + numCoolers, [](const CogdoMazeCooler &first, const CogdoMazeCooler &second) {
        if (first.position.x != second.position.x)
            return first.position.x < second.position.x;
        if (first.position.y != second.position.y)
            return first.position.y < second.position.y;
        if (first.position.z != second.position.z)
            return first.position.z < second.position.z;
        return first.heading < second.heading;
    });
    return coolersValid;
}
bool CogdoMaze::initializeSuits(game::catchgame::RandomNumGen &parent, const uint8_t counts[3]) {
    game::catchgame::RandomNumGen random(parent);
    int quadrants[4][4] = {{0, 0, 25, 25}, {26, 0, 51, 25}, {0, 26, 25, 51}, {26, 26, 51, 51}};
    numSuits = 0;
    for (int type = 0; type < 3; ++type) {
        if (numSuits + counts[type] > kCogdoMazeSuits)
            return false;
        for (int index = 3; index > 0; --index) {
            const int selected = random.randrange(0, index + 1);
            for (int column = 0; column < 4; ++column)
                std::swap(quadrants[index][column], quadrants[selected][column]);
        }
        for (int spot = 0; spot < counts[type]; ++spot) {
            const int quarter =
                spot < 4 * (counts[type] / 4) ? spot / (counts[type] / 4) : random.randint(0, 3);
            int x = 0;
            int y = 0;
            bool found = false;
            for (int attempt = 0; attempt < 10000 && !found; ++attempt) {
                x = random.randint(quadrants[quarter][0], quadrants[quarter][2]);
                y = random.randint(quadrants[quarter][1], quadrants[quarter][3]);
                found = walkable(x, y);
                for (int previous = 0; found && previous < numSuits; ++previous)
                    if (suits[previous].x == x && suits[previous].y == y)
                        found = false;
            }
            if (!found)
                return false;
            CogdoMazeSuit &suit = suits[numSuits];
            suit.~CogdoMazeSuit();
            new (&suit) CogdoMazeSuit();
            suit.x = suit.nextX = x;
            suit.y = suit.nextY = y;
            suit.position = tile(x, y);
            suit.type = type;
            suit.period = type == 0 ? 192 : type == 1 ? 64 : 160;
            suit.nextTick = numSuits * 256 / 20;
            suit.alive = true;
            // Filled by load after deriving all spawn locations from the shared RNG.
            ++numSuits;
        }
    }
    // Each independent actor RNG is reseeded by load before simulation starts.
    return true;
}
bool CogdoMaze::occupied(int x, int y, int except) const {
    for (int index = 0; index < numSuits; ++index) {
        const CogdoMazeSuit &suit = suits[index];
        if (index == except || !suit.alive)
            continue;
        if (suit.nextX == x && suit.nextY == y)
            return true;
        if (suit.previousOccupied && suit.x == x && suit.y == y)
            return true;
    }
    return false;
}
void CogdoMaze::think(int index, int tick) {
    CogdoMazeSuit &suit = suits[index];
    suit.x = suit.nextX;
    suit.y = suit.nextY;
    const auto allowed = [&](int direction) {
        const int x = suit.x + kMoveX[direction];
        const int y = suit.y + kMoveY[direction];
        return walkable(x, y) && !occupied(x, y, index);
    };
    int direction = -1;
    if (suit.random.randrange(0, 1) == 0 && allowed(suit.direction))
        direction = suit.direction;
    if (direction < 0 && difficulty_ >= .5f && suit.random.randrange(0, 100) == 0 &&
        allowed(kOpposite[suit.direction]))
        direction = kOpposite[suit.direction];
    int candidates[3];
    int count = 0;
    for (int candidate = 0; candidate < 4; ++candidate)
        if (candidate != kOpposite[suit.direction])
            candidates[count++] = candidate;
    while (direction < 0 && count) {
        const int selected = suit.random.randrange(0, count);
        if (allowed(candidates[selected]))
            direction = candidates[selected];
        for (int move = selected; move < count - 1; ++move)
            candidates[move] = candidates[move + 1];
        --count;
    }
    if (direction < 0)
        direction = kOpposite[suit.direction];
    suit.direction = direction;
    suit.nextX = suit.x + kMoveX[direction];
    suit.nextY = suit.y + kMoveY[direction];
    // Preserve MazeSuit's unchecked reverse fallback, including reserved cells.
    // Bounds checks in walkable/occupied keep malformed maps memory-safe.
    if (suit.type == 0 && ++suit.dropTimer >= 3) {
        suit.dropTimer = 0;
        int16_t spots[100][2];
        int count = 0;
        // CogdoMazeBossSuit.pickRandomValidSpot: x outer, y inner, upper bound excluded.
        for (int x = suit.x - 5; x < suit.x + 5; ++x)
            for (int y = suit.y - 5; y < suit.y + 5; ++y)
                if (walkable(x, y)) {
                    spots[count][0] = x;
                    spots[count++][1] = y;
                }
        if (count) {
            const int chosen = suit.random.randrange(0, count);
            auto &drop = drops[nextDrop++ % 16];
            drop.position = tile(spots[chosen][0], spots[chosen][1]);
            drop.tick = tick;
        }
    }
    suit.previousOccupied = true;
    suit.lastTick = tick;
    suit.nextTick += suit.period;
}
void CogdoMaze::update(float seconds, const uint8_t hp[kCogdoMazeSuits]) {
    const int tick = static_cast<int>(std::max(0.0f, seconds) * 256);
    for (int index = 0; index < numSuits; ++index)
        suits[index].alive = hp[index] != 0;
    for (int update = 0; update < 1024; ++update) {
        int next = -1;
        for (int index = 0; index < numSuits; ++index) {
            if (suits[index].alive && suits[index].nextTick <= tick &&
                (next < 0 || suits[index].nextTick < suits[next].nextTick))
                next = index;
        }
        if (next < 0)
            break;
        const int when = suits[next].nextTick;
        for (int index = 0; index < numSuits; ++index)
            if (suits[index].nextTick == when)
                suits[index].previousOccupied = false;
        for (int index = 0; index < numSuits; ++index)
            if (suits[index].alive && suits[index].nextTick == when)
                think(index, when);
    }
    for (int index = 0; index < numSuits; ++index) {
        CogdoMazeSuit &suit = suits[index];
        const float fraction = std::clamp((tick - suit.lastTick) / float(suit.period), 0.0f, 1.0f);
        const Vec3 start = tile(suit.x, suit.y);
        const Vec3 end = tile(suit.nextX, suit.nextY);
        suit.position = {start.x + (end.x - start.x) * fraction, start.y + (end.y - start.y) * fraction, 0};
        suit.heading = kHeading[suit.direction];
    }
}
void CogdoMaze::slide(Vec3 previous, Vec3 *current) const {
    int x = 0;
    int y = 0;
    cell(previous, &x, &y);
    float dx = current->x - previous.x;
    float dy = current->y - previous.y;
    const float offsetX = dx < 0 ? -1.0f : 1.0f;
    const float offsetY = dy < 0 ? -1.0f : 1.0f;
    const auto flush = [](int oldCell, int newCell) {
        return newCell > oldCell ? (newCell - 26) * 3 - .01f - 1 : (oldCell - 26) * 3 + 1;
    };
    int nextX = 0;
    int nextY = 0;
    cell({previous.x + dx + offsetX, previous.y, 0}, &nextX, &nextY);
    if (nextX != x && !accessible(nextX, nextY))
        dx = flush(x, nextX) - previous.x;
    cell({previous.x, previous.y + dy + offsetY, 0}, &nextX, &nextY);
    if (nextY != y && !accessible(nextX, nextY))
        dy = flush(y, nextY) - previous.y;
    cell({previous.x + dx + offsetX, previous.y + dy + offsetY, 0}, &nextX, &nextY);
    if (!accessible(nextX, nextY)) {
        const float correctionX = flush(x, nextX) - previous.x;
        const float correctionY = flush(y, nextY) - previous.y;
        if (std::fabs(correctionX) < std::fabs(correctionY))
            dx = correctionX;
        else
            dy = correctionY;
    }
    current->x = previous.x + dx;
    current->y = previous.y + dy;
}
bool CogdoMaze::route(Vec3 from, Vec3 destination, Vec3 *next) const {
    int startX = 0, startY = 0, endX = 0, endY = 0;
    cell(from, &startX, &startY);
    cell(destination, &endX, &endY);
    if (!accessible(startX, startY))
        return false;
    int16_t parent[kCogdoMazeSize * kCogdoMazeSize];
    uint16_t queue[kCogdoMazeSize * kCogdoMazeSize];
    for (int16_t &value : parent)
        value = -1;
    const int start = startY * kCogdoMazeSize + startX;
    queue[0] = start;
    parent[start] = start;
    int count = 1;
    int closest = start;
    int minimum = 100000;
    for (int head = 0; head < count; ++head) {
        const int cellIndex = queue[head];
        const int x = cellIndex % kCogdoMazeSize;
        const int y = cellIndex / kCogdoMazeSize;
        const int separation = std::abs(x - endX) + std::abs(y - endY);
        if (separation < minimum) {
            minimum = separation;
            closest = cellIndex;
        }
        if (separation == 0)
            break;
        for (int direction = 0; direction < 4; ++direction) {
            const int nx = x + kMoveX[direction], ny = y + kMoveY[direction];
            if (!accessible(nx, ny))
                continue;
            const int target = ny * kCogdoMazeSize + nx;
            if (parent[target] >= 0)
                continue;
            parent[target] = cellIndex;
            queue[count++] = target;
        }
    }
    while (parent[closest] != start && closest != start)
        closest = parent[closest];
    *next = tile(closest % kCogdoMazeSize, closest / kCogdoMazeSize);
    if (closest == start) {
        // Cooler triggers can lie in the occupied furniture cell. Approach its
        // edge within the adjacent walkable cell rather than stop at its center.
        next->x = std::clamp(destination.x, next->x + 1.01f, next->x + 1.99f);
        next->y = std::clamp(destination.y, next->y + 1.01f, next->y + 1.99f);
    } else {
        next->x += 1.5f;
        next->y += 1.5f;
    }
    return true;
}
} // namespace tt::world

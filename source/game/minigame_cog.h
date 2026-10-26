// A Cog a trolley game wants drawn as a real suit actor; main renders it through world::Suits
// so minigames share the Cog models, animations and streaming of the rest of the game.
#pragma once

namespace tt::game {

struct MinigameCog {
    float x, y, z, h;
    char code[4]; // SuitDNA head code
    bool walking;
};
constexpr int kMaxMinigameCogs = 16; // DistributedMazeGame.__loadSuits: 4 per player

} // namespace tt::game

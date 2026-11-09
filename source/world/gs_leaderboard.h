// Goofy Speedway's race leader boards (DistributedLeaderBoard): the playground's three
// typ_leaderBoardSign2 boards are in the zone bake; this draws the text the AI cycles onto
// them every 10 s (track, period, ten "N. name time" rows), in the top-screen 2D pass like the
// HQ interior's leader board, projected at each row's place on the board's face.
#pragma once

#include <citro2d.h>
#include <cstdint>

namespace tt::net {
class Client;
}

namespace tt::world {

class GsLeaderBoards {
public:
    bool init();
    void shutdown();
    void setZone(uint32_t zoneId) { active_ = zoneId == 8000; }
    // After the 3D pass (Renderer::endWorld), before nametags and balloons, on the top target.
    void draw2D(const net::Client& client, const C3D_Mtx& proj, const C3D_Mtx& view, C3D_RenderTarget* top);

private:
    static constexpr int kBoards = 4, kRows = 10;
    struct BoardText {
        uint32_t doId = 0, version = 0;
        bool valid = false;
        C2D_Text track = {}, period = {};
        C2D_Text place[kRows] = {}, name[kRows] = {}, time[kRows] = {};
    };
    void rebuild(const net::Client& client);
    C2D_TextBuf buf_ = nullptr;
    BoardText text_[kBoards] = {};
    bool active_ = false;
};

} // namespace tt::world

// User settings read from sdmc:/3ds/toontown3ds/server.txt (key=value lines, '#' comments):
//
//   host=192.168.1.20   # Open Toontown server (astrond client agent)
//   port=7198
//   token=my-toon       # dev servers create an account per token
//   online=1            # 0 = offline (no network at all)
//   platform=mac        # debug: platform-badge platform we report (default n3ds)
//
// sdmc:/3ds/toontown3ds/platform.txt (one word: a name or enum number) overrides platform=.
#pragma once

namespace tt::config {

struct Settings {
    char host[64] = "127.0.0.1"; // Azahar maps 3DS sockets to host sockets
    int port = 7198;
    char token[64] = "3ds-player";
    bool online = true;
    char platform[16] = ""; // empty = N3DS (net/platform_badge.h names)
};

// Missing file or unknown keys are fine; defaults stay.
Settings load(const char* path = "sdmc:/3ds/toontown3ds/server.txt");

} // namespace tt::config

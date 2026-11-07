// User settings read from sdmc:/3ds/toontown3ds/server.txt (key=value lines, '#' comments):
//
//   host=192.168.1.20   # Open Toontown server (astrond client agent)
//   port=7198
//   token=my-toon       # dev servers create an account per token
//   online=1            # 0 = offline (no network at all)
//
// With no server.txt the build's baked-in server is used (TT_DEFAULT_HOST / _PORT / _TOKEN,
// set by the release build), so a fresh install needs nothing on the SD card.
#pragma once

#ifndef TT_DEFAULT_HOST
#define TT_DEFAULT_HOST "2.25.74.62"
#endif
#ifndef TT_DEFAULT_PORT
#define TT_DEFAULT_PORT 7198
#endif
#ifndef TT_DEFAULT_TOKEN
#define TT_DEFAULT_TOKEN "alex-3ds"
#endif

namespace tt::config {

struct Settings {
    char host[64] = TT_DEFAULT_HOST;
    int port = TT_DEFAULT_PORT;
    char token[64] = TT_DEFAULT_TOKEN;
    bool online = true;
};

// Missing file or unknown keys are fine; defaults stay.
Settings load(const char* path = "sdmc:/3ds/toontown3ds/server.txt");

} // namespace tt::config

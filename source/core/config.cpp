#include "core/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace tt::config {

namespace {

char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
    return s;
}

} // namespace

namespace {

Settings loadServer(const char* path);

// Platform-badge debug override: platform.txt beside server.txt (first word wins).
void loadPlatformOverride(const char* serverPath, Settings& s) {
    char path[128];
    snprintf(path, sizeof(path), "%s", serverPath);
    char* slash = strrchr(path, '/');
    if (!slash) return;
    snprintf(slash + 1, sizeof(path) - size_t(slash + 1 - path), "platform.txt");
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[32];
    if (fgets(line, sizeof(line), f)) {
        if (char* hash = strchr(line, '#')) *hash = '\0';
        const char* v = trim(line);
        if (*v) snprintf(s.platform, sizeof(s.platform), "%s", v);
    }
    fclose(f);
    TT_LOG("config: platform override '%s' (%s)", s.platform, path);
}

} // namespace

Settings load(const char* path) {
    Settings s = loadServer(path);
    loadPlatformOverride(path, s);
    return s;
}

namespace {

Settings loadServer(const char* path) {
    Settings s;
    FILE* f = fopen(path, "r");
    if (!f) {
        TT_LOG("config: %s not found, using defaults (host %s)", path, s.host);
        return s;
    }
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        if (char* hash = strchr(line, '#')) *hash = '\0';
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = trim(line);
        const char* value = trim(eq + 1);
        if (!strcmp(key, "host")) {
            snprintf(s.host, sizeof(s.host), "%s", value);
        } else if (!strcmp(key, "port")) {
            s.port = atoi(value);
        } else if (!strcmp(key, "token")) {
            snprintf(s.token, sizeof(s.token), "%s", value);
        } else if (!strcmp(key, "online")) {
            s.online = atoi(value) != 0;
        } else if (!strcmp(key, "platform")) {
            snprintf(s.platform, sizeof(s.platform), "%s", value);
        }
    }
    fclose(f);
    TT_LOG("config: server %s:%d token '%s'%s", s.host, s.port, s.token, s.online ? "" : " (offline)");
    return s;
}

} // namespace

} // namespace tt::config

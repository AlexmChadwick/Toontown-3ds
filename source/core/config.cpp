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

Settings load(const char* path) {
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
        }
    }
    fclose(f);
    TT_LOG("config: server %s:%d token '%s'%s", s.host, s.port, s.token, s.online ? "" : " (offline)");
    return s;
}

} // namespace tt::config

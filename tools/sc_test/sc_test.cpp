// SpeedChat ToonTask phrases (quest::scStrings / decodeScToontask) against upstream
// Quests.getSCStrings, from vectors.txt (tools/sc_test/gen_vectors.py).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "quest/quest_text.h"

using namespace tt::quest;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tools/sc_test/vectors.txt";
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    char line[512];
    int checked = 0, failed = 0;
    // One case = all phrases of (quest, npc, progress); compare lists, not just lines.
    long curQ = -1, curN = -1, curP = -1;
    char want[kMaxScStrings * 2][kScLen];
    int wantN = 0;
    auto flush = [&]() {
        if (curQ < 0) return;
        const QuestDef* q = findQuest(uint16_t(curQ));
        char got[kMaxScStrings][kScLen];
        const int n = q ? scStrings(*q, uint16_t(curN), uint32_t(curP), got, kMaxScStrings) : -1;
        bool ok = n == wantN;
        for (int i = 0; ok && i < n; i++) ok = !strcmp(got[i], want[i]);
        for (int i = 0; ok && i < n; i++) { // the receive path decodes the same text
            char one[kScLen];
            ok = decodeScToontask(uint32_t(curQ), uint32_t(curN), uint32_t(curP), i, one, sizeof one) &&
                 !strcmp(one, want[i]);
        }
        checked++;
        if (!ok) {
            if (++failed <= 15) {
                printf("FAIL quest %ld npc %ld progress %ld: got %d, want %d\n", curQ, curN, curP, n, wantN);
                for (int i = 0; i < wantN || i < n; i++)
                    printf("   want [%s]\n    got [%s]\n", i < wantN ? want[i] : "", i < n && n > 0 ? got[i] : "");
            }
        }
    };
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* fields[5];
        int nf = 0;
        for (char *p = line, *tab; nf < 5; p = tab + 1) {
            fields[nf++] = p;
            tab = strchr(p, '\t');
            if (!tab) break;
            *tab = 0;
        }
        if (nf < 5) continue;
        const long qid = strtol(fields[0], nullptr, 10), npc = strtol(fields[1], nullptr, 10),
                   prog = strtol(fields[2], nullptr, 10);
        if (qid != curQ || npc != curN || prog != curP) {
            flush();
            curQ = qid, curN = npc, curP = prog;
            wantN = 0;
        }
        if (fields[3][0] != '-' && wantN < kMaxScStrings * 2) snprintf(want[wantN++], kScLen, "%s", fields[4]);
    }
    flush();
    fclose(f);
    printf("sc_test: %d cases, %d failures\n", checked, failed);
    return failed || checked == 0;
}

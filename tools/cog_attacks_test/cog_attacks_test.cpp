#include "world/cog_attack_tables.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

void testStaticTables() {
    printf("[test] Checking kCogAttacks static table...\n");
    assert(tt::world::kNumCogAttacks == 67);

    for (int i = 0; i < tt::world::kNumCogAttacks; ++i) {
        const auto& atk = tt::world::kCogAttacks[i];
        assert(atk.id == i);
        assert(atk.name != nullptr && strlen(atk.name) > 0);
        assert(atk.animName != nullptr && strlen(atk.animName) > 0);
        assert(atk.toonHitAnim != nullptr && strlen(atk.toonHitAnim) > 0);
        assert(atk.toonMissAnim != nullptr && strlen(atk.toonMissAnim) > 0);
        assert(atk.durationMs[0] > 0);
        assert(atk.durationMs[1] > 0);
        assert(atk.durationMs[2] > 0);

        // Name lookup check
        int foundId = tt::world::findAttackIdByName(atk.name);
        assert(foundId == i);

        // Access via getter
        const auto* info = tt::world::getCogAttackInfo(i);
        assert(info != nullptr);
        assert(info->id == i);
        assert(strcmp(info->name, atk.name) == 0);
    }

    printf("[test] Checking kCogAttackTable (32 Cogs)...\n");
    static const char* const kCodes[32] = {
        "f", "p", "ym", "mm", "ds", "hh", "cr", "tbc",
        "bf", "b", "dt", "ac", "bs", "sd", "le", "bw",
        "sc", "pp", "tw", "bc", "nc", "mb", "ls", "rb",
        "cc", "tm", "nd", "gh", "ms", "tf", "m", "mh",
    };

    assert(tt::world::kNumCogs == 32);
    for (int cogIdx = 0; cogIdx < tt::world::kNumCogs; ++cogIdx) {
        const char* code = kCodes[cogIdx];
        assert(code != nullptr && strlen(code) > 0);
        assert(tt::world::findCogHeadTypeIndex(code) == cogIdx);

        bool hasAtLeastOne = false;
        for (int a = 0; a < 6; ++a) {
            int atkId = tt::world::getCogAttackId(code, a);
            if (atkId >= 0) {
                assert(atkId < tt::world::kNumCogAttacks);
                hasAtLeastOne = true;
            }
        }
        assert(hasAtLeastOne);
    }
}

// Simple JSON extraction verification against reference_vectors.json
void testAgainstReferenceJson(const char* jsonPath) {
    printf("[test] Verifying against reference vectors in %s...\n", jsonPath);
    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        fprintf(stderr, "Error: Could not open %s\n", jsonPath);
        exit(1);
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    // Verify all 67 attack names appear in the JSON
    for (int i = 0; i < tt::world::kNumCogAttacks; ++i) {
        const auto& atk = tt::world::kCogAttacks[i];
        std::string pattern = std::string("\"name\": \"") + atk.name + "\"";
        if (content.find(pattern) == std::string::npos) {
            fprintf(stderr, "Error: Attack %s not found in JSON\n", atk.name);
            exit(1);
        }
    }

    // Verify all 32 Cog codes appear in the JSON
    static const char* const kCodes[32] = {
        "f", "p", "ym", "mm", "ds", "hh", "cr", "tbc",
        "bf", "b", "dt", "ac", "bs", "sd", "le", "bw",
        "sc", "pp", "tw", "bc", "nc", "mb", "ls", "rb",
        "cc", "tm", "nd", "gh", "ms", "tf", "m", "mh",
    };
    for (int i = 0; i < 32; ++i) {
        std::string pattern = std::string("\"") + kCodes[i] + "\"";
        if (content.find(pattern) == std::string::npos) {
            fprintf(stderr, "Error: Cog code %s not found in JSON\n", kCodes[i]);
            exit(1);
        }
    }

    printf("[test] All attacks and Cog codes verified against reference JSON.\n");
}

} // namespace

int main(int argc, char** argv) {
    const char* jsonPath = (argc > 1) ? argv[1] : "tools/cog_attacks_test/reference_vectors.json";
    testStaticTables();
    testAgainstReferenceJson(jsonPath);
    printf("All cog_attacks_test assertions passed successfully!\n");
    return 0;
}

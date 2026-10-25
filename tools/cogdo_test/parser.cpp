#include "net/cogdo_state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tt::net;
int main() {
    FILE *file = std::fopen("sdmc-out/cogdo-test/vectors.bin", "rb");
    assert(file);
    uint16_t classId = 0;
    unsigned count = 0;
    while (std::fread(&classId, 2, 1, file) == 1) {
        uint16_t field = 0;
        uint32_t size = 0;
        uint8_t bytes[8192];
        assert(std::fread(&field, 2, 1, file) == 1);
        assert(std::fread(&size, 4, 1, file) == 1 && size <= sizeof(bytes));
        assert(std::fread(bytes, 1, size, file) == size);
        CogdoState state;
        state.classId = classId;
        state.kind = cogdoKind(classId);
        DatagramReader reader(bytes, size);
        assert(applyCogdoField(state, field, reader, 1000, 42));
        assert(reader.ok() && reader.remaining() == 0);
        for (uint32_t length = 0; length < size; ++length) {
            const CogdoState before = state;
            DatagramReader truncated(bytes, length);
            assert(!applyCogdoField(state, field, truncated, 2000, 42));
            assert(std::memcmp(&state, &before, sizeof(state)) == 0);
        }
        ++count;
    }
    std::fclose(file);
    std::printf("Cogdo Panda fields/truncation publication: %u PASS\n", count);
}

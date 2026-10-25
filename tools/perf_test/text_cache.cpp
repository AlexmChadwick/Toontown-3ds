#include "render/text_cache.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static unsigned parses = 0;
size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf buffer) {
    return buffer->glyphs;
}
void C2D_TextBufClear(C2D_TextBuf buffer) {
    buffer->glyphs = 0;
    ++buffer->generation;
}
void C2D_TextParse(C2D_Text* text, C2D_TextBuf buffer, const char* value) {
    ++parses;
    text->begin = buffer->glyphs;
    buffer->glyphs += std::strlen(value);
    text->end = buffer->glyphs;
    text->generation = buffer->generation;
}
void C2D_TextOptimize(C2D_Text*) {}
int main() {
    tt::render::TextCache cache;
    FakeTextBuffer buffer;
    C2D_Text text;
    for (int frame = 0; frame < 10000; ++frame)
        for (int label = 0; label < 64; ++label) {
            char value[40];
            std::snprintf(value, sizeof(value), "label %d", label);
            cache.parse(&text, &buffer, value, 1024);
            assert(text.generation == buffer.generation && text.end <= buffer.glyphs);
        }
    assert(parses == 64);
    for (int change = 0; change < 1000; ++change) {
        char value[40];
        std::snprintf(value, sizeof(value), "changed value %d", change);
        cache.parse(&text, &buffer, value, 1024);
        assert(text.generation == buffer.generation && text.end <= 1024);
    }
    cache.parse(&text, &buffer, "UTF-8: Doodlé", 1024);
    const unsigned before = parses;
    cache.parse(&text, &buffer, "UTF-8: Doodlé", 1024);
    assert(parses == before);
    char longText[300];
    std::memset(longText, 'x', sizeof(longText) - 1);
    longText[299] = 0;
    cache.parse(&text, &buffer, longText, 1024);
    cache.parse(&text, &buffer, longText, 1024);
    assert(text.end - text.begin == 299);
    std::puts("Actual TextCache wrapper: 640000 -> 64 parses; buffer-full generations, changing content, UTF-8 "
              "and uncached long text PASS (stub font backend)");
}

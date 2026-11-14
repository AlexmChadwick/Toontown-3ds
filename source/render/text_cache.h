#pragma once
#include "render/ui_font.h"
#include "core/text_keys.h"
#include <citro2d.h>
namespace tt::render {
class TextCache {
public:
    void clear() {
        keys_.clear();
    }
    void parse(C2D_Text* output, C2D_TextBuf buffer, const char* text, size_t capacity) {
        int entry = keys_.find(text);
        if (entry >= 0) {
            *output = text_[entry];
            return;
        }
        // UTF-8 byte length is an upper bound on glyph count. Flush both glyphs
        // and keys together; a cached text never references a reused buffer range.
        if (keys_.full() || C2D_TextBufGetNumGlyphs(buffer) + std::strlen(text) + 1 > capacity) {
            C2D_TextBufClear(buffer);
            keys_.clear();
        }
        tt::render::textParse(output, buffer, text);
        C2D_TextOptimize(output);
        entry = keys_.add(text);
        if (entry >= 0)
            text_[entry] = *output;
    }

private:
    TextKeys keys_;
    C2D_Text text_[TextKeys::kEntries] = {};
};
} // namespace tt::render

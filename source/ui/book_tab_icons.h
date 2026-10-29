// The Shtiker Book's page tab icons (ShtikerBook.addPageTab): one copy of
// romfs:/baked/book_tabs.ttm (bake_book_tabs.py), loaded on first use on the main thread.
#pragma once

namespace tt::ui {

// Draws page `page`'s icon (Book::PageIndex order) fitted into a size x size box centred on
// (cx, cy). Returns false when the atlas is missing.
bool drawBookTabIcon(int page, float cx, float cy, float size, float z = 0.5f);

} // namespace tt::ui

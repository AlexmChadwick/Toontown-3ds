// Host build of the Ui layout. Compiles the same static_assert as source/render/ui.cpp
// with an ARM compiler, then exposes the size as a symbol the shell script reads.
#include "render/ui.h"

static_assert(sizeof(tt::ui::Ui) <= 112 * 1024, "Ui grew: move large members to the heap");

char kUiBytes[sizeof(tt::ui::Ui)];

#pragma once
#include "render/model.h"
#include <citro3d.h>
namespace tt::render {
struct Renderer { void drawModel(Model&, const C3D_Mtx&) {} };
}

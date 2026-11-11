// OledRender.h - draws the screens of OledModel with the U8g2 C API.
// Platform independent apart from U8g2 itself: the same code runs in the firmware
// and in the PC preview (tools/oled_preview.py), which is how the pictures, the
// Russian texts and the layout are checked without a display.
#pragma once
#include "OledModel.h"

struct u8g2_struct;

namespace oled {

// Clears the frame buffer and draws the current view (no transfer to the display).
void render(u8g2_struct* u, const Model& m);

}  // namespace oled

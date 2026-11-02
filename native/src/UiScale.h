#pragma once

namespace ui_scale {
void load();
void setMonitorDpi(float dpi);
float combinedDpi();
int percent();
void adjust(int delta);
bool consumeChanged();
}

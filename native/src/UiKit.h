#pragma once

// CyWinTask presentation helpers. They sit on top of CyUI (palette, metrics,
// fonts, widgets) and only add the compositions this application needs:
// page headers, live meters, tinted table cells, stat values and avatars.
// Nothing here changes CyUI itself.

#include <cyui/Core.h>
#include <imgui.h>

namespace ui_kit {

// The French or English text, following the interface language.
const char* pick(const char* french, const char* english);

// A page heading row: the title, a dimmed subtitle on the same line, then the
// cursor is left on that line so actions can be placed with
// cyui::alignRight(). Always close with endPageHeader().
// `actionsWidth` is the room kept on the right; the subtitle is clipped to fit.
void beginPageHeader(const char* title, const char* subtitle = nullptr, float actionsWidth = 0.0f);
void endPageHeader(float spacing = 10.0f);

// Colour of a load from 0 to 1: the accent while calm, then warning and error.
ImU32 loadColour(double fraction);
// Tints the current table cell after a load (0..1); nothing for ~0.
void tintCell(double fraction);
// Colour of a 0..100 trust score (negative: unknown).
ImU32 trustColour(int score);

// A thin rounded bar: the track in bg3, the filled part in `colour`.
void drawMeter(ImDrawList* draw, ImVec2 min, ImVec2 max, double fraction, ImU32 colour);
// A live meter on its own lines: LABEL ........ value, then a thin bar.
void liveMeter(const char* label, const char* value, double fraction, ImU32 colour);
// A label (overline), a large value and an optional detail line.
void statValue(const char* label, const char* value, ImU32 valueColour, const char* detail = nullptr);
// A coloured dot then text, on the current line.
void dotText(ImU32 colour, const char* text);

// A rounded square with the first letter of `name` (when no icon exists).
void letterAvatar(ImDrawList* draw, ImVec2 position, float size, const char* name);

// An icon-only navigation button (narrow sidebar).
bool navIcon(const char* icon, const char* tooltip, bool active);

// True when the mouse is over [min, max] in the current window.
bool hovering(ImVec2 min, ImVec2 max);

}  // namespace ui_kit

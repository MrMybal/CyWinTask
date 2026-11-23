#include "UiKit.h"
#include "UiLocale.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace ui_kit {

const char* pick(const char* french, const char* english)
{
    return ui_locale::french() ? french : english;
}

namespace {
float headerTop {};
float headerHeight {};
}

void beginPageHeader(const char* title, const char* subtitle, const float actionsWidth)
{
    headerTop = ImGui::GetCursorPosY();
    const float titleSize = ImGui::GetFontSize() * 1.2f;
    headerHeight = std::max(ImGui::GetFrameHeight(), titleSize + cyui::dp(4.0f));
    ImGui::SetCursorPosY(headerTop + (headerHeight - titleSize) * 0.5f);
    cyui::heading(title, 2);
    if (subtitle && *subtitle) {
        ImGui::SameLine(0.0f, cyui::dp(12.0f));
        // Share the title's baseline: the subtitle is smaller.
        ImGui::SetCursorPosY(headerTop + (headerHeight - titleSize) * 0.5f
            + (titleSize - ImGui::GetFontSize()) * 0.78f);
        const float room = ImGui::GetContentRegionAvail().x - actionsWidth - cyui::dp(16.0f);
        if (room >= cyui::dp(48.0f)) {
            const float width = std::min(room, ImGui::CalcTextSize(subtitle).x);
            cyui::clippedText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), width,
                cyui::palette().textDim, subtitle);
            ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
        }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(headerTop + (headerHeight - ImGui::GetFrameHeight()) * 0.5f);
}

void endPageHeader(const float spacing)
{
    ImGui::NewLine();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), headerTop + headerHeight)
        + cyui::dp(spacing) - ImGui::GetStyle().ItemSpacing.y);
}

ImU32 loadColour(const double fraction)
{
    const auto& palette = cyui::palette();
    if (!std::isfinite(fraction) || fraction < 0.5) return palette.accent;
    if (fraction < 0.8) return palette.warning;
    return palette.error;
}

void tintCell(const double fraction)
{
    if (!std::isfinite(fraction) || fraction < 0.005) return;
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    // The square root keeps small loads visible without flooding the table.
    const float strength = static_cast<float>(0.04 + 0.36 * std::sqrt(clamped));
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cyui::alpha(loadColour(clamped), strength));
}

ImU32 trustColour(const int score)
{
    const auto& palette = cyui::palette();
    if (score < 0) return palette.textFaint;
    return score >= 80 ? palette.success : score >= 55 ? palette.info
        : score >= 30 ? palette.warning : palette.error;
}

void drawMeter(ImDrawList* draw, const ImVec2 min, const ImVec2 max, const double fraction, const ImU32 colour)
{
    const auto& palette = cyui::palette();
    const float rounding = (max.y - min.y) * 0.5f;
    draw->AddRectFilled(min, max, palette.bg3, rounding);
    if (!std::isfinite(fraction) || fraction <= 0.0) return;
    const float width = (max.x - min.x) * static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    draw->AddRectFilled(min, ImVec2(min.x + std::max(width, max.y - min.y), max.y), colour, rounding);
}

void liveMeter(const char* label, const char* value, const double fraction, const ImU32 colour)
{
    const auto& palette = cyui::palette();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float fontSize = ImGui::GetFontSize() * 0.86f;
    draw->AddText(ImGui::GetFont(), fontSize, origin, palette.textDim, label);
    const float valueWidth = ImGui::GetFont()->CalcTextSizeA(fontSize, 10000.0f, 0.0f, value).x;
    draw->AddText(ImGui::GetFont(), fontSize, ImVec2(origin.x + width - valueWidth, origin.y), palette.text, value);
    const float barTop = origin.y + fontSize + cyui::dp(4.0f);
    drawMeter(draw, ImVec2(origin.x, barTop), ImVec2(origin.x + width, barTop + cyui::dp(4.0f)), fraction, colour);
    ImGui::Dummy(ImVec2(width, fontSize + cyui::dp(9.0f)));
}

void statValue(const char* label, const char* value, const ImU32 valueColour, const char* detail)
{
    cyui::overline(label, cyui::palette().textFaint);
    {
        cyui::ScopedFont font(cyui::boldFont(), 1.35f);
        cyui::textColoured(valueColour, "%s", value);
    }
    if (detail && *detail) {
        cyui::ScopedFont font(nullptr, 0.9f);
        cyui::textDim("%s", detail);
    }
}

void dotText(const ImU32 colour, const char* text)
{
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float radius = cyui::dp(3.5f);
    const float lineHeight = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(position.x + radius, position.y + lineHeight * 0.5f), radius, colour);
    ImGui::Dummy(ImVec2(radius * 2.0f, lineHeight));
    ImGui::SameLine(0.0f, cyui::dp(6.0f));
    ImGui::TextUnformatted(text);
}

void letterAvatar(ImDrawList* draw, const ImVec2 position, const float size, const char* name)
{
    const auto& palette = cyui::palette();
    char letter[2] { '?', 0 };
    if (name && *name) {
        const unsigned char first = static_cast<unsigned char>(name[0]);
        letter[0] = first < 0x80 ? static_cast<char>(std::toupper(first)) : '?';
    }
    const ImVec2 max(position.x + size, position.y + size);
    draw->AddRectFilled(position, max, palette.bg4, cyui::dp(4.0f));
    const float fontSize = size * 0.62f;
    const auto textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, letter);
    draw->AddText(cyui::boldFont() ? cyui::boldFont() : ImGui::GetFont(), fontSize,
        ImVec2(position.x + (size - textSize.x) * 0.5f, position.y + (size - textSize.y) * 0.5f),
        palette.textDim, letter);
}

bool navIcon(const char* icon, const char* tooltip, const bool active)
{
    const auto& palette = cyui::palette();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() + cyui::dp(10.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(tooltip);
    const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + width, origin.y + height);
    const float rounding = ImGui::GetStyle().FrameRounding + cyui::dp(2.0f);
    if (active) draw->AddRectFilled(origin, max, cyui::alpha(palette.accent, 0.16f), rounding);
    else if (hovered) draw->AddRectFilled(origin, max, palette.bg3, rounding);
    const float fontSize = ImGui::GetFontSize() * 1.15f;
    const auto size = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, icon);
    draw->AddText(ImGui::GetFont(), fontSize,
        ImVec2(origin.x + (width - size.x) * 0.5f, origin.y + (height - size.y) * 0.5f),
        active ? palette.accent : hovered ? palette.text : palette.textDim, icon);
    if (hovered) cyui::tooltip(tooltip);
    return clicked;
}

bool hovering(const ImVec2 min, const ImVec2 max)
{
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        && ImGui::IsMouseHoveringRect(min, max, false);
}

}  // namespace ui_kit

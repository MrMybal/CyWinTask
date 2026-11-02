#include "UiScale.h"

#include <windows.h>

#include <algorithm>

namespace ui_scale {
namespace {
int zoomPercent = 115;
float monitorDpi = 1.0f;
bool zoomChanged = false;
constexpr wchar_t registryKey[] = L"Software\\CyWinTask";
constexpr wchar_t registryValue[] = L"UiScalePercent";
}

void load()
{
    DWORD value {};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, registryKey, registryValue, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
        && value >= 100 && value <= 170)
        zoomPercent = static_cast<int>(value);
}

void setMonitorDpi(const float dpi) { monitorDpi = std::max(0.75f, dpi); }
float combinedDpi() { return monitorDpi * static_cast<float>(zoomPercent) / 100.0f; }
int percent() { return zoomPercent; }

void adjust(const int delta)
{
    const int next = std::clamp(zoomPercent + delta, 100, 170);
    if (next == zoomPercent) return;
    zoomPercent = next;
    zoomChanged = true;
    HKEY key {};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, registryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        == ERROR_SUCCESS) {
        const DWORD value = static_cast<DWORD>(zoomPercent);
        RegSetValueExW(key, registryValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }
}

bool consumeChanged()
{
    const bool value = zoomChanged;
    zoomChanged = false;
    return value;
}
}

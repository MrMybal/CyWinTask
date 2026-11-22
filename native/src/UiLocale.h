#pragma once

#include <string>
#include <string_view>

namespace ui_locale {
void load();
bool french();
void setFrench(bool value);
const char* tr(const char* frenchText);
std::string text(std::string_view frenchText);
}

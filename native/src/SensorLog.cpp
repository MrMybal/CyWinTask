#include "SensorLog.h"
#include "UiLocale.h"

#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace {
std::vector<std::string> fields(std::string_view line, char separator)
{
    std::vector<std::string> result;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else quoted = !quoted;
        } else if (ch == separator && !quoted) {
            result.push_back(std::move(field)); field.clear();
        } else field += ch;
    }
    result.push_back(std::move(field));
    return result;
}
bool temperatureColumn(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value.find("°c") != std::string::npos || value.find("temp") != std::string::npos;
}
bool number(std::string value, double& result)
{
    std::replace(value.begin(), value.end(), ',', '.');
    const char* start = value.c_str();
    char* end {};
    result = std::strtod(start, &end);
    return end != start && std::isfinite(result) && result > -100 && result < 250;
}
}

bool SensorLog::chooseFile()
{
    wchar_t selected[32768] {};
    OPENFILENAMEW dialog {sizeof(dialog)};
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = ui_locale::french()
        ? L"Journaux CSV HWiNFO\0*.csv\0Tous les fichiers\0*.*\0\0"
        : L"HWiNFO CSV logs\0*.csv\0All files\0*.*\0\0";
    dialog.lpstrFile = selected;
    dialog.nMaxFile = static_cast<DWORD>(std::size(selected));
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;
    openFile(selected);
    return true;
}

void SensorLog::openFile(const std::filesystem::path& path)
{
    path_ = path;
    enabled_ = true;
    lastSize_ = 0;
    lastWrite_ = {};
    readings_.clear();
    error_.clear();
}

void SensorLog::clear()
{
    path_.clear(); enabled_ = false; lastSize_ = 0; lastWrite_ = {};
    readings_.clear(); error_.clear();
}

void SensorLog::refresh()
{
    if (!enabled_ || path_.empty()) return;
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error) { error_ = ui_locale::french() ? "Journal de capteurs indisponible." : "Sensor log unavailable."; return; }
    const auto written = std::filesystem::last_write_time(path_, error);
    if (error) return;
    if (size == lastSize_ && written == lastWrite_) return;
    lastSize_ = size; lastWrite_ = written;
    std::ifstream input(path_, std::ios::binary);
    if (!input) { error_ = ui_locale::french() ? "Impossible de lire le fichier CSV." : "Cannot read the CSV file."; return; }
    std::string header;
    if (!std::getline(input, header)) { error_ = ui_locale::french() ? "Journal CSV vide." : "Empty CSV log."; return; }
    if (header.rfind("\xef\xbb\xbf", 0) == 0) header.erase(0, 3);
    const char separator = std::count(header.begin(), header.end(), ';') > std::count(header.begin(), header.end(), ',') ? ';' : ',';
    const auto names = fields(header, separator);
    if (names.size() < 2) { error_ = ui_locale::french() ? "En-tête CSV non reconnu." : "Unrecognized CSV header."; return; }
    const auto tail = static_cast<std::streamoff>(std::min<std::uintmax_t>(size, 262144));
    input.clear(); input.seekg(static_cast<std::streamoff>(size) - tail);
    if (size > static_cast<std::uintmax_t>(tail)) { std::string partial; std::getline(input, partial); }
    std::string line, latest;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (fields(line, separator).size() >= names.size()) latest = std::move(line);
    }
    if (latest.empty()) { error_ = ui_locale::french() ? "En attente des mesures CSV." : "Waiting for CSV measurements."; return; }
    const auto values = fields(latest, separator);
    std::vector<TemperatureReading> next;
    for (std::size_t i = 0; i < names.size() && i < values.size() && next.size() < 256; ++i) {
        if (!temperatureColumn(names[i])) continue;
        double value {};
        if (!number(values[i], value)) continue;
        auto previous = std::find_if(readings_.begin(), readings_.end(), [&](const TemperatureReading& reading) {
            return reading.name == names[i];
        });
        next.push_back({names[i], value,
            previous == readings_.end() ? value : std::min(value, previous->minimum),
            previous == readings_.end() ? value : std::max(value, previous->maximum)});
    }
    readings_ = std::move(next);
    error_ = readings_.empty() ? (ui_locale::french() ? "Aucun capteur de température dans ce journal." :
        "No temperature sensors in this log.") : "";
}

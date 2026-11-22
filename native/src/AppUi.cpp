#include "AppUi.h"
#include "AssistantPane.h"
#include "BridgeClient.h"
#include "ConsolePane.h"
#include "HardwareInventory.h"
#include "ProcessTrust.h"
#include "SensorLog.h"
#include "UiScale.h"
#include "UiLocale.h"
#include "UiBridge.h"

#include <cyhwinfo/ui.hpp>
#include <cyui/Core.h>
#include <cyui/Locale.h>
#include <cyui/modules/Plots.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using cymcp::Json;
using cymcp::Error;

namespace {
struct Key {
    int id {};
    std::int64_t created {};
    bool operator==(const Key& other) const { return id == other.id && created == other.created; }
};
struct KeyHash {
    std::size_t operator()(const Key& key) const
    {
        return std::hash<std::int64_t>()((static_cast<std::int64_t>(key.id) << 32) ^ key.created);
    }
};
struct LiveSensorReading {
    std::string unit;
    double current {}, minimum {}, maximum {};
};
Key identity(const ProcessInfo& process) { return { process.id, process.created }; }
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::wstring wide(const std::string& text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring output(length, 0);
    if (length) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), output.data(), length);
    return output;
}
std::string format(const char* pattern, const double value)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), pattern, value);
    return buffer;
}
std::string processAge(const std::int64_t created, const std::int64_t unixMs)
{
    if (created <= 0 || unixMs <= 0) return "—";
    const auto now = 116444736000000000LL + unixMs * 10000LL;
    const auto seconds = std::max<std::int64_t>(0, (now - created) / 10000000LL);
    char buffer[48] {};
    if (seconds >= 86400) std::snprintf(buffer, sizeof(buffer), ui_locale::tr("%lld j %02lld h"), seconds / 86400, seconds / 3600 % 24);
    else std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return buffer;
}
const char* categoryName(const std::uint8_t category)
{
    return category == 0 ? ui_locale::tr("Applications") : category == 2 ? ui_locale::tr("Processus Windows") : ui_locale::tr("Arrière-plan");
}
const MetricInfo* metric(const DeviceInfo& device, const std::string& id)
{
    for (const auto& item : device.metrics) if (item.id == id) return &item;
    return nullptr;
}
double metricValue(const DeviceInfo& device, const std::string& id)
{
    const auto* item = metric(device, id);
    return item ? item->value : NAN;
}
std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(count), '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}
std::string registryValue(HKEY root, const wchar_t* subkey, const wchar_t* name)
{
    DWORD type {}, bytes {};
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS
        || bytes < sizeof(wchar_t) || bytes > 16384) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, value.data(), &bytes) != ERROR_SUCCESS)
        return {};
    value.resize(wcsnlen_s(value.c_str(), value.size()));
    return utf8(value);
}
bool elevated()
{
    HANDLE token {};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation {};
    DWORD bytes {};
    const bool result = GetTokenInformation(token, TokenElevation, &elevation,
        sizeof(elevation), &bytes) && elevation.TokenIsElevated;
    CloseHandle(token);
    return result;
}
bool restartElevated(const wchar_t* page)
{
    wchar_t executable[32768] {};
    if (!GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)))) return false;
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"runas", executable,
        page, nullptr, SW_SHOWNORMAL)) > 32;
}
ImU32 heat(const double percent, const bool memory)
{
    const auto value = memory ? percent * 5.0 : percent;
    const auto& palette = cyui::palette();
    return value >= 50 ? palette.error : value >= 20 ? palette.warning : value >= 5 ? palette.info : palette.accent;
}
void usageBar(const double percent, const std::string& text, const bool memory, const float heightDp = 22.0f)
{
    // CyUI's rounded progress bar enforces a minimum filled width. Process loads
    // below 1% need a proportional hairline so small differences remain visible.
    const auto& palette = cyui::palette();
    const auto pos = ImGui::GetCursorScreenPos();
    const float width = std::max(cyui::dp(55.0f), ImGui::GetContentRegionAvail().x);
    const float height = cyui::dp(heightDp);
    ImGui::Dummy(ImVec2(width, height));
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    draw->AddRectFilled(pos, max, palette.bg3, cyui::dp(3.0f));
    if (std::isfinite(percent) && percent > 0)
        draw->AddRectFilled(ImVec2(pos.x, max.y - cyui::dp(3.0f)),
            ImVec2(pos.x + width * static_cast<float>(std::clamp(percent / 100.0, 0.0, 1.0)), max.y),
            heat(percent, memory));
    draw->AddText(ImVec2(pos.x + cyui::dp(5.0f), pos.y + (height - ImGui::GetFontSize()) * 0.5f), palette.text, text.c_str());
}
void trustBar(const TrustResult& trust, const float height)
{
    const auto& colors = cyui::palette();
    const auto position = ImGui::GetCursorScreenPos();
    const float width = std::max(cyui::dp(64.0f), ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(width, height));
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(position, ImVec2(position.x + width, position.y + height), colors.bg3, cyui::dp(2.0f));
    if (trust.score >= 0) {
        const ImU32 colour = trust.score >= 80 ? colors.success
            : trust.score >= 55 ? colors.info : trust.score >= 30 ? colors.warning : colors.error;
        const float fill = width * static_cast<float>(trust.score) / 100.0f;
        draw->AddRectFilled(ImVec2(position.x, position.y + height - cyui::dp(3.0f)),
            ImVec2(position.x + fill, position.y + height), colour);
        const auto score = std::to_string(trust.score) + " / 100";
        draw->AddText(ImVec2(position.x + cyui::dp(5.0f),
            position.y + std::max(0.0f, (height - ImGui::GetFontSize()) * 0.5f - cyui::dp(1.0f))),
            colors.text, score.c_str());
    } else draw->AddText(ImVec2(position.x + cyui::dp(5.0f),
        position.y + (height - ImGui::GetFontSize()) * 0.5f), colors.textDim,
        trust.state == TrustState::pending ? "..." : "—");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s",
        ui_locale::text(trust.label).c_str(), ui_locale::text(trust.explanation).c_str(),
        ui_locale::tr("Indice local, pas une analyse antivirus."));
}
class IconCache {
    ID3D11Device* device_ {};
    std::unordered_map<std::string, ID3D11ShaderResourceView*> cache_;
    int budget_ {};
public:
    explicit IconCache(ID3D11Device* device) : device_(device) {}
    ~IconCache() { for (auto& item : cache_) if (item.second) item.second->Release(); }
    void beginFrame() { budget_ = 3; }
    ID3D11ShaderResourceView* get(const std::string& path)
    {
        if (path.empty()) return nullptr;
        if (const auto found = cache_.find(path); found != cache_.end()) return found->second;
        if (budget_-- <= 0) return nullptr;
        if (cache_.size() >= 2048) {
            for (auto& item : cache_) if (item.second) item.second->Release();
            cache_.clear();
        }
        SHFILEINFOW info {};
        const auto source = wide(path);
        if (!SHGetFileInfoW(source.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) || !info.hIcon)
            return cache_[path] = nullptr;
        constexpr int side = 28;
        BITMAPINFO bitmap {};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = side;
        bitmap.bmiHeader.biHeight = -side;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        void* pixels {};
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
        ID3D11ShaderResourceView* view {};
        if (dc && image && pixels) {
            HGDIOBJ previous = SelectObject(dc, image);
            std::memset(pixels, 0, side * side * 4);
            DrawIconEx(dc, 0, 0, info.hIcon, side, side, 0, nullptr, DI_NORMAL);
            auto* bytes = static_cast<unsigned char*>(pixels);
            for (int i = 0; i < side * side; ++i)
                if (bytes[i * 4 + 3] == 0 && (bytes[i * 4] || bytes[i * 4 + 1] || bytes[i * 4 + 2]))
                    bytes[i * 4 + 3] = 255;
            D3D11_TEXTURE2D_DESC desc {};
            desc.Width = side; desc.Height = side; desc.MipLevels = 1; desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data { pixels, side * 4, 0 };
            ID3D11Texture2D* texture {};
            if (SUCCEEDED(device_->CreateTexture2D(&desc, &data, &texture))) {
                device_->CreateShaderResourceView(texture, nullptr, &view);
                texture->Release();
            }
            SelectObject(dc, previous);
        }
        if (image) DeleteObject(image);
        if (dc) DeleteDC(dc);
        DestroyIcon(info.hIcon);
        return cache_[path] = view;
    }
};
struct PlotPoint { double time {}, value {}; };
using History = std::deque<PlotPoint>;
struct DisplayRow {
    int index { -1 };
    int depth {};
    int descendants {};
    bool groupHeader {}, hasChildren {};
    std::string title;
};
}

struct AppUi::State {
    explicit State(ID3D11Device* device, const bool demo, const bool showPerformance,
                   const bool showAssistant, const int mcpLocalPort, const int utilityAtStart,
                   const bool showPerformanceDetails)
        : icons(device), demoMode(demo), performancePage(showPerformance && !showAssistant), assistantPage(showAssistant)
    {
        if (showPerformanceDetails) graphMode = false;
        if (utilityAtStart) {
            performancePage = assistantPage = false;
            consolePage = utilityAtStart == 3;
            temperaturePage = utilityAtStart == 4;
            systemPage = utilityAtStart == 5;
        }
        if (demoMode) { fillDemo(); return; }
        if (!bridge.start()) { error = bridge.startupError(); return; }
        hardwareMonitor = std::make_unique<cyhwinfo::ui::Monitor>(cyhwinfo::ui::MonitorOptions{
            .interval = std::chrono::milliseconds(1000), .advanced_sensors = true,
            .gpu_processes = true, .storage_health = false});
        updateHardwareActivity();
        bridge.send(!assistantPage && !consolePage ? "M 1\n" : "M 0\n");
        if (mcpLocalPort >= 0) {
            assistantPage = true;
            try {
                assistantPane = std::make_unique<AssistantPane>(false, uiBridge);
                assistantPane->startLocalMcp(mcpLocalPort);
            } catch (const std::exception& ex) { error = cymcp::redact_error_message(ex.what()); }
        }
    }
    ~State() { uiBridge->close(); }
    BridgeClient bridge;
    IconCache icons;
    ProcessTrust trust;
    SystemSnapshot snapshot;
    bool demoMode {};
    std::shared_ptr<UiBridge> uiBridge = std::make_shared<UiBridge>();
    bool hasSnapshot {}, performancePage {}, assistantPage {}, consolePage {}, temperaturePage {}, systemPage {};
    bool paused {}, graphMode { true }, graphRows {}, detailGraphCards {true}, groupByType {}, treeMode { true };
    std::unique_ptr<AssistantPane> assistantPane;
    std::unique_ptr<ConsolePane> consolePane;
    std::unique_ptr<HardwareInventory> inventory;
    std::unique_ptr<cyhwinfo::ui::Monitor> hardwareMonitor;
    cyhwinfo::ui::Dashboard hardwareDashboard;
    bool hardwareCollectionActive {true};
    int sensorView {};
    int processView {};
    char sensorSearch[128] {};
    bool sensorAvailableOnly {};
    char gpuProcessSearch[128] {};
    SensorLog sensorLog;
    std::unordered_map<std::string, LiveSensorReading> liveTemperatures;
    double nextSensorRefresh {};
    bool compactGraphCards { true }, graphExtraDevices { true }, compactProcesses { true }, showProcessInspector { true };
    bool showCores {}, compactCores { true }, stopModal {}, detailsOpen {}, focusSearch {};
    bool initialExpansion { true };
    int categoryFilter {}, sortColumn {}, sortDirection { 1 }, lastSelected {-1}, nextRequest {1};
    int graphColumnsMode {};
    char search[256] {};
    std::string activeDevice {"cpu"}, detailText, statusMessage, error;
    std::string activeSensorKey;
    std::unordered_set<Key, KeyHash> selected, expanded;
    std::vector<Key> pendingStop;
    std::vector<DisplayRow> display;
    std::vector<int> topCpu, topMemory, topGpu;
    std::vector<TrustResult> trustScores;
    std::unordered_map<std::string, History> histories;
    std::unordered_map<std::string, std::vector<cyui::Point>> plotPoints;
    std::unordered_map<std::string, double> diskRateScale;
    std::map<std::string, bool> detached;
    bool dirty { true };
    double nextTrustRefresh {};

    void fillDemo()
    {
        hasSnapshot = true;
        snapshot.unixMs = 1700000000000;
        snapshot.cpu = 18.4;
        snapshot.memoryUsedGb = 15.8;
        snapshot.memoryTotalGb = 32.0;
        snapshot.collectMs = 2.7;
        const auto addProcess = [this](int id, int parent, const char* name, double cpu, double gpu, int memoryMb,
                                       int threads, std::uint8_t category) {
            ProcessInfo item;
            item.id = id; item.parent = parent; item.name = name; item.cpu = cpu; item.gpu = gpu; item.gpuGroup = gpu;
            item.gpuLocalBytes = gpu > 0 ? gpu * 11000000.0 : 0;
            item.gpuSharedBytes = gpu > 0 ? gpu * 2000000.0 : 0;
            item.gpuCommittedBytes = item.gpuLocalBytes + item.gpuSharedBytes;
            item.created = 133400000000000000LL + id * 10000000LL;
            item.workingSet = static_cast<std::int64_t>(memoryMb) * 1048576;
            item.threads = threads; item.handles = threads * 31 + id % 59; item.category = category;
            snapshot.processes.push_back(std::move(item));
        };
        addProcess(4200, 0, "Atelier.exe", 6.4, 4.2, 1840, 42, 0);
        addProcess(4201, 4200, "Atelier Renderer.exe", 3.1, 27.5, 920, 19, 1);
        addProcess(4202, 4200, "Atelier Media.exe", 0.8, 5.1, 215, 11, 1);
        addProcess(5600, 0, "Navigateur.exe", 2.3, 0.3, 690, 27, 0);
        addProcess(5601, 5600, "Navigateur Renderer.exe", 1.7, 8.6, 412, 15, 1);
        addProcess(6800, 0, "Notes.exe", 0.2, 0.0, 155, 12, 0);
        addProcess(8900, 0, "Synchronisation.exe", 0.6, 0.0, 188, 9, 1);
        addProcess(8901, 8900, "Synchronisation Worker.exe", 0.4, 0.0, 120, 7, 1);
        addProcess(1100, 0, "Système", 0.5, 0.0, 305, 84, 2);
        addProcess(1110, 1100, "Service audio", 0.1, 0.0, 68, 14, 2);
        for (int i = 0; i < 24; ++i)
            addProcess(12000 + i, 0, ("Service de démonstration " + std::to_string(i + 1)).c_str(),
                i % 6 == 0 ? 0.3 : 0.0, 0.0, 12 + i * 3, 2 + i % 7, i % 3 == 0 ? 2 : 1);
        snapshot.processes[0].gpuGroup = 31.7;
        snapshot.processes[3].gpuGroup = 8.9;
        snapshot.devices = {
            { "cpu", "Processeur", "Processeur de démonstration · 8 processeurs logiques", "cpu",
                { { "cpu", "Utilisation totale", "%", 18.4, 100 }, { "temperature", "Température CPU", "°C", 51, NAN },
                  { "telemetry:power", "CPU Package Power", "W", 68, NAN },
                  { "telemetry:clock", "CPU Core Clock", "MHz", 4700, NAN },
                  { "telemetry:voltage", "CPU Core Voltage", "V", 1.18, NAN },
                  { "peak", "Busiest logical processor", "%", 29.7, 100 } } },
            { "memory", "Mémoire", "15,8 / 32 Go utilisés", "memory",
                { { "used", "Mémoire physique utilisée", "Go", 15.8, 32 },
                  { "available", "Mémoire physique disponible", "Go", 16.2, 32 },
                  { "commit", "Mémoire virtuelle engagée", "Go", 21.3, 48 } } },
            { "disk0", "Disque 0", "SSD de démonstration", "disk",
                { { "active", "Temps d'activité", "%", 24, 100 },
                  { "read", "Lecture", "Mo/s", 18, NAN }, { "write", "Écriture", "Mo/s", 7, NAN } } },
            { "network0", "Ethernet", "Réseau de démonstration", "network",
                { { "receive", "Réception", "Mbits/s", 12.4, NAN },
                  { "send", "Envoi", "Mbits/s", 3.5, NAN } } },
            { "gpu0", "GPU 0", "GPU de démonstration", "gpu",
                { { "usage", "Utilisation", "%", 36.2, 100 },
                  { "temperature", "Température GPU", "°C", 58, NAN },
                  { "dedicated", "Mémoire dédiée", "Go", 5.2, 16 },
                  { "telemetry:power", "GPU Power", "W", 142, NAN },
                  { "telemetry:clock", "GPU Core Clock", "MHz", 1860, NAN },
                  { "telemetry:fan", "GPU Fan", "RPM", 1280, NAN } } },
            { "sensor:cpu", "Demo CPU", "Cpu", "sensor",
                { { "package", "CPU Package", "°C", 51, NAN },
                  { "core0", "Core 0", "°C", 48, NAN },
                  { "core1", "Core 1", "°C", 50, NAN },
                  { "power", "CPU Package Power", "W", 68, NAN } } },
            { "sensor:board", "Demo Board X1", "Motherboard", "sensor",
                { { "chipset", "Chipset", "°C", 39, NAN },
                  { "vrm", "VRM", "°C", 44, NAN },
                  { "fan", "Case Fan", "RPM", 920, NAN } } }
        };
        for (int i = 0; i < 8; ++i)
            snapshot.devices[0].metrics.push_back({ "core:" + std::to_string(i),
                "Processeur logique " + std::to_string(i), "%", 8.0 + i * 3.1, 100 });
        selected.insert(identity(snapshot.processes.front()));
        for (const auto& process : snapshot.processes) if (process.category == 0) expanded.insert(identity(process));
        initialExpansion = false;
        refreshTopProcesses();
        refreshTrustScores();
        const double now = snapshot.unixMs / 1000.0;
        for (const auto& device : snapshot.devices)
            for (const auto& value : device.metrics) {
                auto& points = histories[device.id + "/" + value.id];
                for (int i = 0; i < 60; ++i) {
                    const double wave = 0.8 + 0.15 * std::sin(i * 0.27 + static_cast<double>(value.id.size()));
                    points.push_back({ now - 59 + i, std::max(0.0, value.value * wave) });
                }
            }
        preparePlots();
        updateTemperatures();
    }

    void updateTemperatures()
    {
        for (const auto& device : snapshot.devices) {
            for (const auto& value : device.metrics) {
                if ((device.kind != "sensor" && value.unit != "°C") || !std::isfinite(value.value)) continue;
                const auto key = device.id + "/" + value.id;
                auto& reading = liveTemperatures[key];
                if (reading.unit.empty()) {
                    reading = {value.unit, value.value, value.value, value.value};
                } else {
                    reading.current = value.value;
                    reading.minimum = std::min(reading.minimum, value.value);
                    reading.maximum = std::max(reading.maximum, value.value);
                }
            }
        }
    }

    void updateHardwareActivity()
    {
        if (!hardwareMonitor) return;
        const bool processesPage = !performancePage && !assistantPage && !consolePage
            && !temperaturePage && !systemPage;
        const bool active = !paused && (performancePage || temperaturePage || systemPage
            || !detached.empty() || processesPage);
        if (active == hardwareCollectionActive) return;
        hardwareMonitor->set_paused(!active);
        hardwareCollectionActive = active;
    }
    void switchPage(const bool performance)
    {
        assistantPage = consolePage = temperaturePage = systemPage = false;
        performancePage = performance;
        if (!demoMode) bridge.send("M 1\n");
        updateHardwareActivity();
    }
    void switchAssistant()
    {
        consolePage = temperaturePage = systemPage = false;
        assistantPage = true;
        performancePage = false;
        if (!demoMode) bridge.send(detached.empty() ? "M 0\n" : "M 1\n");
        updateHardwareActivity();
    }
    void switchUtility(const int page)
    {
        assistantPage = performancePage = false;
        consolePage = page == 3;
        temperaturePage = page == 4;
        systemPage = page == 5;
        if (!demoMode) bridge.send(temperaturePage || systemPage || !detached.empty() ? "M 1\n" : "M 0\n");
        updateHardwareActivity();
    }
    int activePage() const
    {
        return assistantPage ? 2 : consolePage ? 3 : temperaturePage ? 4 : systemPage ? 5
            : performancePage ? 1 : 0;
    }
    Json uiState() const
    {
        Json chosen = Json::array(), windows = Json::array(), devices = Json::array();
        for (const auto& key : selected) chosen.push_back({{"pid", key.id}, {"created", key.created}});
        for (const auto& [name, open] : detached) if (open) windows.push_back(name);
        for (const auto& device : snapshot.devices) devices.push_back(device.id);
        return {{"ready", hasSnapshot},
            {"page", assistantPage ? "assistant" : consolePage ? "console" : temperaturePage ? "temperatures"
                : systemPage ? "system" : performancePage ? "performance" : "processes"},
            {"paused", paused}, {"scale_percent", ui_scale::percent()},
            {"processes", {{"query", search}, {"category", categoryFilter},
                {"view", processView == 1 ? "gpu" : "all"},
                {"group_by_type", groupByType}, {"related", treeMode}, {"compact", compactProcesses},
                {"inspector", showProcessInspector}, {"sort_column", sortColumn},
                {"descending", sortDirection < 0}, {"selected", std::move(chosen)}}},
            {"performance", {{"view", graphMode ? "graphs" : "detail"},
                {"device_id", activeDevice}, {"devices", std::move(devices)},
                {"columns", graphColumnsMode}, {"compact_cards", compactGraphCards},
                {"rows", graphRows}, {"detail_cards", detailGraphCards},
                {"extra_devices", graphExtraDevices}, {"show_cores", showCores},
                {"compact_cores", compactCores}}},
            {"windows", std::move(windows)}};
    }
    Json applyUiCommand(const std::string& operation, const Json& args)
    {
        if (operation == "ui.state") return uiState();
        if (operation == "ui.navigate") {
            const auto page = args.at("page").get<std::string>();
            if (page == "assistant") switchAssistant();
            else if (page == "console") switchUtility(3);
            else if (page == "temperatures") switchUtility(4);
            else if (page == "system") switchUtility(5);
            else switchPage(page == "performance");
            if (args.contains("device_id")) activeDevice = args.at("device_id").get<std::string>();
            if (args.contains("view")) graphMode = args.at("view") == "graphs";
            return uiState();
        }
        if (operation == "ui.processes.configure") {
            if (args.contains("view")) processView = args.at("view") == "gpu" ? 1 : 0;
            if (args.contains("query")) {
                const auto query = args.at("query").get<std::string>();
                std::snprintf(search, sizeof(search), "%s", query.c_str());
            }
            if (args.contains("category")) categoryFilter = args.at("category").get<int>();
            if (args.contains("group_by_type")) groupByType = args.at("group_by_type").get<bool>();
            if (args.contains("related")) treeMode = args.at("related").get<bool>();
            if (args.contains("compact")) compactProcesses = args.at("compact").get<bool>();
            if (args.contains("inspector")) showProcessInspector = args.at("inspector").get<bool>();
            if (args.contains("sort_column")) sortColumn = args.at("sort_column").get<int>();
            if (args.contains("descending")) sortDirection = args.at("descending").get<bool>() ? -1 : 1;
            dirty = true;
            switchPage(false);
            return uiState();
        }
        if (operation == "ui.processes.select") {
            const auto& requested = args.at("processes");
            if (requested.size() > 100) throw Error("InvalidOptions", "Select at most 100 processes");
            std::vector<Key> keys;
            for (const auto& item : requested) {
                Key key {item.at("pid").get<int>(), item.at("created").get<std::int64_t>()};
                if (!find(key)) throw Error("NotFound", "Process not found or PID reused");
                keys.push_back(key);
            }
            const auto mode = args.value("mode", std::string("replace"));
            if (mode == "replace") selected.clear();
            for (const auto& key : keys) {
                if (mode == "remove") selected.erase(key);
                else selected.insert(key);
            }
            switchPage(false);
            return uiState();
        }
        if (operation == "ui.processes.expand" || operation == "ui.processes.inspect"
            || operation == "ui.processes.trust" || operation == "ui.processes.open_location"
            || operation == "ui.processes.copy") {
            Key key {args.at("pid").get<int>(), args.at("created").get<std::int64_t>()};
            const auto* process = find(key);
            if (!process) throw Error("NotFound", "Process not found or PID reused");
            if (operation == "ui.processes.trust") {
                const auto score = trust.get(process->path);
                return {{"pid", key.id}, {"score", score.score < 0 ? Json(nullptr) : Json(score.score)},
                    {"state", static_cast<int>(score.state)}, {"label", score.label},
                    {"explanation", score.explanation}};
            }
            if (operation == "ui.processes.open_location") {
                if (process->path.empty()) throw Error("Unavailable", "Executable path unavailable");
                const auto path = wide(process->path);
                const auto parameter = L"/select,\"" + path + L"\"";
                if (reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", L"explorer.exe",
                        parameter.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
                    throw Error("Unavailable", "Explorer could not open the executable location");
                return {{"opened", true}};
            }
            if (operation == "ui.processes.copy") {
                const auto what = args.at("what").get<std::string>();
                if (what == "path" && process->path.empty())
                    throw Error("Unavailable", "Executable path unavailable");
                const auto value = what == "path" ? process->path
                    : process->name + " · PID " + std::to_string(process->id);
                ImGui::SetClipboardText(value.c_str());
                return {{"copied", true}, {"what", what}};
            }
            if (operation == "ui.processes.expand") {
                if (args.at("expanded").get<bool>()) expanded.insert(key);
                else expanded.erase(key);
                treeMode = true;
                dirty = true;
            } else { focusProcess(*process); requestDetails(*process); }
            return uiState();
        }
        if (operation == "ui.performance.configure") {
            if (args.contains("view")) graphMode = args.at("view") == "graphs";
            if (args.contains("device_id")) activeDevice = args.at("device_id").get<std::string>();
            if (args.contains("columns")) graphColumnsMode = args.at("columns").get<int>();
            if (args.contains("rows")) graphRows = args.at("rows").get<bool>();
            if (args.contains("detail_cards")) detailGraphCards = args.at("detail_cards").get<bool>();
            if (args.contains("compact_cards")) compactGraphCards = args.at("compact_cards").get<bool>();
            if (args.contains("extra_devices")) graphExtraDevices = args.at("extra_devices").get<bool>();
            if (args.contains("show_cores")) showCores = args.at("show_cores").get<bool>();
            if (args.contains("compact_cores")) compactCores = args.at("compact_cores").get<bool>();
            switchPage(true);
            return uiState();
        }
        if (operation == "ui.performance.history") {
            const auto key = args.at("device_id").get<std::string>() + "/"
                + args.at("metric_id").get<std::string>();
            const auto found = histories.find(key);
            if (found == histories.end()) throw Error("NotFound", "Graph metric is not available in this UI session");
            const auto limit = static_cast<std::size_t>(args.value("limit", 60));
            Json points = Json::array();
            const auto begin = found->second.size() > limit ? found->second.size() - limit : 0;
            for (std::size_t i = begin; i < found->second.size(); ++i)
                points.push_back({{"timestamp_s", found->second[i].time},
                    {"value", std::isfinite(found->second[i].value)
                        ? Json(found->second[i].value) : Json(nullptr)}});
            return {{"metric", key}, {"points", std::move(points)}};
        }
        if (operation == "ui.window.set") {
            const auto target = args.at("target").get<std::string>();
            if (target != "all" && std::none_of(snapshot.devices.begin(), snapshot.devices.end(),
                    [&](const DeviceInfo& device) { return device.id == target; }))
                throw Error("NotFound", "Device is not available in the current UI sample");
            const bool open = args.at("open").get<bool>();
            if (open) detached[target] = true;
            else detached.erase(target);
            updateHardwareActivity();
            if (!demoMode) bridge.send(!assistantPage && !consolePage || !detached.empty() ? "M 1\n" : "M 0\n");
            return uiState();
        }
        if (operation == "ui.pause") {
            paused = args.at("paused").get<bool>();
            if (!demoMode) bridge.send(paused ? "P 1\n" : "P 0\n");
            updateHardwareActivity();
            return uiState();
        }
        if (operation == "ui.scale") {
            ui_scale::adjust(args.at("percent").get<int>() - ui_scale::percent());
            return uiState();
        }
        throw Error("NotFound", "Unknown UI operation");
    }
    void receive()
    {
        if (demoMode) return;
        SystemSnapshot next;
        std::vector<BridgeReply> replies;
        std::string nextError;
        if (bridge.poll(next, replies, nextError)) {
            snapshot = std::move(next);
            mergeHardware();
            hasSnapshot = true;
            dirty = true;
            refreshTopProcesses();
            refreshTrustScores();
            error.clear();
            const double now = snapshot.unixMs / 1000.0;
            for (const auto& device : snapshot.devices)
                for (const auto& value : device.metrics) {
                    auto& points = histories[device.id + "/" + value.id];
                    points.push_back({ now, value.value });
                    while (!points.empty() && (points.size() > 120 || now - points.front().time > 60.0))
                        points.pop_front();
                }
            preparePlots();
            updateTemperatures();
            std::unordered_set<Key, KeyHash> alive;
            for (const auto& process : snapshot.processes) alive.insert(identity(process));
            if (initialExpansion) {
                for (const auto& process : snapshot.processes)
                    if (process.category == 0) expanded.insert(identity(process));
                initialExpansion = false;
            }
            for (auto it = selected.begin(); it != selected.end();)
                it = alive.count(*it) ? std::next(it) : selected.erase(it);
            for (auto it = expanded.begin(); it != expanded.end();)
                it = alive.count(*it) ? std::next(it) : expanded.erase(it);
        }
        if (!nextError.empty()) error = std::move(nextError);
        for (const auto& reply : replies) {
            if (reply.success) {
                statusMessage = reply.text;
                cyui::notify(reply.text.c_str(), cyui::Tone::success);
                if (reply.text != "Arrêt demandé.") { detailText = reply.text; detailsOpen = true; }
            } else {
                statusMessage = reply.text;
                cyui::notify(reply.text.c_str(), cyui::Tone::error);
            }
        }
    }
    void mergeHardware()
    {
        if (!hardwareMonitor) return;
        const auto hardware = hardwareMonitor->snapshot();
        if (!hardware) return;
        if (std::chrono::system_clock::now() - hardware->timestamp > std::chrono::seconds(3)) return;
        const auto assign = [](DeviceInfo& device, const std::string& id, const std::string& label,
                               const std::string& unit, double value, double maximum = NAN) {
            auto found = std::find_if(device.metrics.begin(), device.metrics.end(),
                [&](const MetricInfo& item) { return item.id == id; });
            if (found != device.metrics.end()) { found->value = value; found->maximum = maximum; }
            else device.metrics.push_back({id, label, unit, value, maximum});
        };
        const auto sensorUnit = [](cyhwinfo::SensorMetric metric) -> const char* {
            using M = cyhwinfo::SensorMetric;
            switch (metric) {
            case M::temperature_celsius: return "°C";
            case M::power_watts: return "W";
            case M::voltage_volts: return "V";
            case M::fan_rpm: return "RPM";
            case M::current_amperes: return "A";
            case M::clock_megahertz: return "MHz";
            case M::frequency_hertz: return "Hz";
            case M::load_percent: case M::control_percent: case M::level_percent:
            case M::c_state_residency_percent: return "%";
            case M::data_gibibytes: return "Go";
            case M::data_mebibytes: return "Mo";
            case M::transfer_rate_mtps: return "MT/s";
            default: return "";
            }
        };
        if (const auto cpuLoad = hardware->trend(cyhwinfo::ui::Summary::cpu).value;
            cpuLoad && std::isfinite(*cpuLoad)) {
            snapshot.cpu = *cpuLoad;
            for (auto& device : snapshot.devices) if (device.kind == "cpu")
                assign(device, "cpu", "Total utilization", "%", *cpuLoad, 100);
        }
        for (const auto& group : hardware->sensor_groups) if (group.id == "ram:usage") {
            double used = NAN, available = NAN;
            for (const auto& row : group.rows) {
                if (row.reading.status != cyhwinfo::Status::available || !row.reading.value.has_value()) continue;
                if (row.id == "used") used = *row.reading.value;
                else if (row.id == "available") available = *row.reading.value;
            }
            if (std::isfinite(used) && std::isfinite(available)) {
                snapshot.memoryUsedGb = used;
                snapshot.memoryTotalGb = used + available;
                for (auto& device : snapshot.devices) if (device.kind == "memory") {
                    assign(device, "used", "Physical memory used", "Go", used, snapshot.memoryTotalGb);
                    assign(device, "available", "Physical memory available", "Go", available, snapshot.memoryTotalGb);
                }
            }
        }
        for (const auto& group : hardware->sensor_groups) {
            if (group.id == "cpu:logical") continue;
            DeviceInfo sensors {"cyhwinfo:" + group.id, group.name, "CyHWInfo", "sensor", {}};
            DeviceInfo* target = nullptr;
            if (group.category == cyhwinfo::ui::Page::cpu) {
                for (auto& device : snapshot.devices) if (device.kind == "cpu") { target = &device; break; }
            } else if (group.category == cyhwinfo::ui::Page::gpu) {
                for (auto& device : snapshot.devices)
                    if (device.kind == "gpu" && lower(device.description) == lower(group.name)) {
                        target = &device; break;
                    }
            }
            for (const auto& row : group.rows) {
                if (row.reading.status != cyhwinfo::Status::available || !row.reading.value.has_value()
                    || !std::isfinite(*row.reading.value)) continue;
                const double value = *row.reading.value; // Zero is a valid reading.
                const std::string unit = sensorUnit(row.metric);
                if (unit.empty()) continue;
                assign(sensors, row.id, row.label, unit, value,
                    unit == "%" ? 100.0 : NAN);
                if (!target) continue;
                if (group.category == cyhwinfo::ui::Page::gpu && group.id.starts_with("gpu:")) {
                    if (row.id == "load") assign(*target, "usage", "GPU load", "%", value, 100);
                    else if (row.id == "temperature") assign(*target, "temperature", "GPU temperature", "°C", value);
                    else if (row.id == "vram") assign(*target, "dedicated", "Dedicated GPU memory", "Go", value);
                    else if (row.id == "power") assign(*target, "telemetry:power", "GPU power", "W", value);
                    else if (row.id == "clock") assign(*target, "telemetry:core-clock", "GPU core clock", "MHz", value);
                    else if (row.id == "memory-clock") assign(*target, "telemetry:memory-clock", "GPU memory clock", "MHz", value);
                    else if (row.id == "physical:0:fan") assign(*target, "telemetry:fan", "GPU fan", "RPM", value);
                    else if (row.id == "memory-load") assign(*target, "telemetry:memory-load", "GPU memory load", "%", value, 100);
                    else if (row.id == "video-load") assign(*target, "telemetry:video-load", "GPU video load", "%", value, 100);
                } else if (group.category == cyhwinfo::ui::Page::cpu && group.id.starts_with("lhm:")) {
                    if (row.metric == cyhwinfo::SensorMetric::temperature_celsius
                        && (row.label.find("Package") != std::string::npos
                            || row.label.find("Tctl") != std::string::npos
                            || !std::isfinite(metricValue(*target, "temperature"))))
                        assign(*target, "temperature", "CPU temperature", "°C", value);
                    else if (row.metric == cyhwinfo::SensorMetric::power_watts)
                        assign(*target, "telemetry:power", "CPU power", "W", value);
                    else if (row.metric == cyhwinfo::SensorMetric::clock_megahertz)
                        assign(*target, "telemetry:clock", "CPU clock", "MHz", value);
                }
            }
            if (!sensors.metrics.empty()) snapshot.devices.push_back(std::move(sensors));
        }
        const auto& notices = hardware->section(cyhwinfo::ui::Page::sensors).notices;
        if (!notices.empty()) snapshot.devices.push_back({"sensor-status", "Advanced sensors",
            notices.front().title, "sensor-status", {}});
    }
    void refreshTopProcesses()
    {
        topCpu.resize(snapshot.processes.size());
        std::iota(topCpu.begin(), topCpu.end(), 0);
        topMemory = topCpu;
        topGpu = topCpu;
        const auto keep = std::min<std::size_t>(5, snapshot.processes.size());
        const auto cpuValue = [this](const int index) {
            const double value = snapshot.processes[index].cpu;
            return std::isfinite(value) ? value : 0.0;
        };
        std::partial_sort(topCpu.begin(), topCpu.begin() + keep, topCpu.end(),
            [&](int a, int b) { return cpuValue(a) == cpuValue(b)
                ? snapshot.processes[a].workingSet > snapshot.processes[b].workingSet : cpuValue(a) > cpuValue(b); });
        std::partial_sort(topMemory.begin(), topMemory.begin() + keep, topMemory.end(),
            [&](int a, int b) { return snapshot.processes[a].workingSet == snapshot.processes[b].workingSet
                ? cpuValue(a) > cpuValue(b) : snapshot.processes[a].workingSet > snapshot.processes[b].workingSet; });
        std::partial_sort(topGpu.begin(), topGpu.begin() + keep, topGpu.end(),
            [&](int a, int b) {
                const auto gpuValue = [this](int index) {
                    const auto value = snapshot.processes[index].gpu;
                    return std::isfinite(value) ? value : -1.0;
                };
                return gpuValue(a) == gpuValue(b) ? cpuValue(a) > cpuValue(b) : gpuValue(a) > gpuValue(b);
            });
        topCpu.resize(keep);
        topMemory.resize(keep);
        topGpu.resize(keep);
    }
    bool refreshTrustScores()
    {
        std::vector<TrustResult> updated;
        updated.reserve(snapshot.processes.size());
        for (const auto& process : snapshot.processes) {
            if (demoMode) {
                const int score = process.id == 12009 ? 12 : process.id == 6800 ? 40
                    : process.category == 2 ? 94 : process.category == 0 ? 88 : 63;
                updated.push_back({ TrustState::verified, score, "Simulation", "Note fictive de démonstration." });
            } else updated.push_back(trust.get(process.path));
        }
        bool changed = updated.size() != trustScores.size();
        if (!changed)
            for (std::size_t i = 0; i < updated.size(); ++i)
                if (updated[i].state != trustScores[i].state || updated[i].score != trustScores[i].score) {
                    changed = true;
                    break;
                }
        trustScores = std::move(updated);
        return changed;
    }
    void preparePlots()
    {
        plotPoints.clear();
        diskRateScale.clear();
        const double now = snapshot.unixMs / 1000.0;
        for (const auto& device : snapshot.devices) {
            double rateMaximum = 1.0;
            if (device.kind == "disk") {
                for (const auto& value : device.metrics) {
                    if (value.id != "read" && value.id != "write") continue;
                    if (std::isfinite(value.value)) rateMaximum = std::max(rateMaximum, value.value);
                    if (const auto found = histories.find(device.id + "/" + value.id); found != histories.end())
                        for (const auto& point : found->second)
                            if (std::isfinite(point.value) && now - point.time <= 60.0)
                                rateMaximum = std::max(rateMaximum, point.value);
                }
                rateMaximum = std::max(1.0, rateMaximum * 1.15);
                diskRateScale[device.id] = rateMaximum;
            }
            for (const auto& value : device.metrics) {
                const auto key = device.id + "/" + value.id;
                const auto found = histories.find(key);
                if (found == histories.end()) continue;
                auto& prepared = plotPoints[key];
                prepared.reserve(found->second.size() * 2);
                double previousTime = NAN;
                for (const auto& point : found->second) {
                    if (now - point.time > 60.0) continue;
                    if (std::isfinite(previousTime) && point.time - previousTime > 3.5)
                        prepared.push_back({(previousTime + point.time) * 0.5, NAN});
                    // Plots has one Y axis; map disk throughput onto the activity
                    // percentage axis while retaining raw values for the tooltip.
                    const double y = device.kind == "disk" && (value.id == "read" || value.id == "write")
                        ? point.value / rateMaximum * 100.0 : point.value;
                    prepared.push_back({point.time, y});
                    previousTime = point.time;
                }
            }
        }
    }
    const ProcessInfo* find(const Key& key) const
    {
        for (const auto& process : snapshot.processes) if (identity(process) == key) return &process;
        return nullptr;
    }
    bool queryMatches(const ProcessInfo& process) const
    {
        const auto query = lower(search);
        if (query.rfind("pid:", 0) == 0) return std::to_string(process.id) == query.substr(4);
        return query.empty() || lower(process.name).find(query) != std::string::npos
            || std::to_string(process.id).find(query) != std::string::npos;
    }
    bool matches(const ProcessInfo& process) const
    {
        return (!categoryFilter || process.category == categoryFilter - 1) && queryMatches(process);
    }
    bool less(const int left, const int right) const
    {
        const auto& a = snapshot.processes[left];
        const auto& b = snapshot.processes[right];
        int comparison {};
        switch (sortColumn) {
        case 1: comparison = (trustScores[left].score > trustScores[right].score)
            - (trustScores[left].score < trustScores[right].score); break;
        case 2: comparison = (a.id > b.id) - (a.id < b.id); break;
        case 3: comparison = (a.cpu > b.cpu) - (a.cpu < b.cpu); break;
        case 4: { const double av = treeMode ? a.gpuGroup : a.gpu; const double bv = treeMode ? b.gpuGroup : b.gpu;
            if (std::isfinite(av) != std::isfinite(bv)) return std::isfinite(av);
            comparison = (av > bv) - (av < bv); break; }
        case 5: comparison = (a.workingSet > b.workingSet) - (a.workingSet < b.workingSet); break;
        case 6: comparison = (a.threads > b.threads) - (a.threads < b.threads); break;
        case 7: comparison = (a.handles > b.handles) - (a.handles < b.handles); break;
        case 8: comparison = (b.created > a.created) - (b.created < a.created); break;
        default: comparison = lower(a.name).compare(lower(b.name)); break;
        }
        if (comparison == 0) comparison = (a.id > b.id) - (a.id < b.id);
        return sortDirection >= 0 ? comparison < 0 : comparison > 0;
    }
    void buildRows()
    {
        if (!dirty) return;
        dirty = false;
        display.clear();
        std::vector<int> all(snapshot.processes.size());
        std::iota(all.begin(), all.end(), 0);
        if (!treeMode) {
            for (int group = 0; group < (groupByType ? 3 : 1); ++group) {
                std::vector<int> indices;
                for (int index : all)
                    if (matches(snapshot.processes[index]) && (!groupByType || snapshot.processes[index].category == group))
                        indices.push_back(index);
                std::sort(indices.begin(), indices.end(), [this](int a, int b) { return less(a, b); });
                if (groupByType && !indices.empty()) display.push_back({ -1, 0, 0, true, false,
                    std::string(categoryName(static_cast<std::uint8_t>(group))) + " (" + std::to_string(indices.size()) + ")" });
                for (int index : indices) display.push_back({ index });
            }
            return;
        }
        std::unordered_map<int, int> byId;
        for (int index : all) byId[snapshot.processes[index].id] = index;
        std::vector<std::vector<int>> children(all.size());
        std::vector<int> roots;
        for (int index : all) {
            const auto& child = snapshot.processes[index];
            const auto found = byId.find(child.parent);
            if (found != byId.end() && found->second != index
                && snapshot.processes[found->second].created <= child.created)
                children[found->second].push_back(index);
            else roots.push_back(index);
        }
        const std::string query = lower(search);
        std::vector<bool> retained(all.size(), query.empty());
        if (!query.empty()) {
            std::vector<bool> visitedDescendants(all.size(), false);
            std::function<void(int, int)> keepDescendants = [&](int index, int depth) {
                if (depth > 24 || visitedDescendants[index]) return;
                visitedDescendants[index] = true;
                retained[index] = true;
                for (int child : children[index]) keepDescendants(child, depth + 1);
            };
            for (int index : all) {
                const auto& process = snapshot.processes[index];
                if (!queryMatches(process)) continue;
                keepDescendants(index, 0);
                int current = index;
                for (std::size_t guard = 0; guard < all.size(); ++guard) {
                    retained[current] = true;
                    const auto parent = byId.find(snapshot.processes[current].parent);
                    if (parent == byId.end() || parent->second == current
                        || snapshot.processes[parent->second].created > snapshot.processes[current].created) break;
                    current = parent->second;
                }
            }
        }
        std::vector<int> descendantCounts(all.size(), -1);
        std::function<int(int, int)> countDescendants = [&](int index, int depth) -> int {
            if (depth > 24) return 0;
            if (descendantCounts[index] == -2) return 0;
            if (descendantCounts[index] >= 0) return descendantCounts[index];
            descendantCounts[index] = -2;
            int total {};
            for (int child : children[index]) if (retained[child]) total += 1 + countDescendants(child, depth + 1);
            return descendantCounts[index] = total;
        };
        std::sort(roots.begin(), roots.end(), [this](int a, int b) { return less(a, b); });
        for (auto& branch : children) std::sort(branch.begin(), branch.end(), [this](int a, int b) { return less(a, b); });
        std::function<void(int, int)> add = [&](int index, int depth) {
            if (depth > 24 || !retained[index]) return;
            const int descendants = countDescendants(index, 0);
            const bool hasChildren = descendants > 0;
            display.push_back({ index, depth, descendants, false, hasChildren, {} });
            const bool open = expanded.count(identity(snapshot.processes[index])) > 0 || (!query.empty() && hasChildren);
            if (open) for (int child : children[index]) add(child, depth + 1);
        };
        for (int group = 0; group < (groupByType ? 3 : 1); ++group) {
            std::vector<int> selectedRoots;
            for (int root : roots)
                if (retained[root] && (!categoryFilter || snapshot.processes[root].category == categoryFilter - 1)
                    && (!groupByType || snapshot.processes[root].category == group))
                    selectedRoots.push_back(root);
            if (groupByType && !selectedRoots.empty()) display.push_back({ -1, 0, 0, true, false,
                std::string(categoryName(static_cast<std::uint8_t>(group))) + " (" + std::to_string(selectedRoots.size()) + ")" });
            for (int root : selectedRoots) add(root, 0);
        }
    }
    void select(const Key key, const int displayIndex)
    {
        const auto& io = ImGui::GetIO();
        if (io.KeyShift && lastSelected >= 0) {
            if (!io.KeyCtrl) selected.clear();
            const int begin = std::max(0, std::min(lastSelected, displayIndex));
            const int end = std::min(static_cast<int>(display.size()) - 1, std::max(lastSelected, displayIndex));
            for (int index = begin; index <= end; ++index)
                if (!display[index].groupHeader) selected.insert(identity(snapshot.processes[display[index].index]));
        } else if (io.KeyCtrl) {
            if (!selected.erase(key)) selected.insert(key);
        } else { selected.clear(); selected.insert(key); }
        lastSelected = displayIndex;
    }
    void focusProcess(const ProcessInfo& process)
    {
        processView = 0;
        categoryFilter = 0;
        groupByType = false;
        treeMode = false;
        std::snprintf(search, sizeof(search), "pid:%d", process.id);
        selected.clear();
        selected.insert(identity(process));
        lastSelected = -1;
        dirty = true;
        switchPage(false);
    }
    void requestDetails(const ProcessInfo& process)
    {
        if (demoMode) {
            detailText = process.name + " · PID " + std::to_string(process.id) + ui_locale::tr("\nDonnées fictives de démonstration.");
            detailsOpen = true;
            return;
        }
        detailText = ui_locale::tr("Lecture des informations...");
        detailsOpen = true;
        bridge.send("D " + std::to_string(nextRequest++) + " " + std::to_string(process.id)
            + " " + std::to_string(process.created) + (ui_locale::french() ? "\n" : " en\n"));
    }
    void askStop()
    {
        if (demoMode) return;
        pendingStop.clear();
        for (const auto& key : selected) if (find(key)) pendingStop.push_back(key);
        stopModal = !pendingStop.empty();
    }
    void processActions(const ProcessInfo& process)
    {
        if (ImGui::MenuItem(selected.size() > 1 ? ui_locale::tr("Arrêter les tâches sélectionnées...") : ui_locale::tr("Arrêter le processus..."),
                            nullptr, false, !demoMode)) askStop();
        if (ImGui::MenuItem(ui_locale::tr("Informations avancées..."))) requestDetails(process);
        ImGui::Separator();
        const bool hasPath = !process.path.empty();
        if (ImGui::MenuItem(ui_locale::tr("Ouvrir l'emplacement"), nullptr, false, hasPath)) {
            const auto path = wide(process.path);
            const auto parameter = L"/select,\"" + path + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", parameter.c_str(), nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(ui_locale::tr("Copier le chemin"), nullptr, false, hasPath)) ImGui::SetClipboardText(process.path.c_str());
        if (ImGui::MenuItem(ui_locale::tr("Copier le nom et le PID"))) {
            const auto label = process.name + " · PID " + std::to_string(process.id);
            ImGui::SetClipboardText(label.c_str());
        }
    }
    void statusBar()
    {
        if (cyui::beginStatusBar("##status")) {
            if (demoMode) cyui::statusText(cyui::Tone::info, ui_locale::tr("Données fictives · Démonstration"));
            else if (paused) cyui::statusText(cyui::Tone::warning, ui_locale::tr("Actualisation en pause"));
            else if (!error.empty()) cyui::statusText(cyui::Tone::error, "%s", error.c_str());
            else if (!statusMessage.empty()) cyui::statusText(cyui::Tone::success, "%s", statusMessage.c_str());
            else if (hasSnapshot) cyui::statusText(cyui::Tone::neutral, ui_locale::tr("1 s · Collecte %.1f ms · %zu processus"), snapshot.collectMs, snapshot.processes.size());
            else cyui::statusText(cyui::Tone::neutral, ui_locale::tr("Connexion au collecteur..."));
            ImGui::SameLine();
            cyui::alignRight(cyui::dp(235.0f));
            cyui::textFaint(ui_locale::french() ? "Ctrl+1–6 · Ctrl+F · Ctrl/Maj" : "Ctrl+1–6 · Ctrl+F · Ctrl/Shift");
        }
        cyui::endStatusBar();
    }
    void topBar()
    {
        if (cyui::beginTopBar("##top")) {
            const float width = ImGui::GetWindowWidth();
            const bool compact = width < cyui::dp(1050.0f);
            const bool allPages = width >= cyui::dp(1160.0f);
            cyui::heading("CyWinTask", 3);
            if (demoMode && allPages) { ImGui::SameLine(); cyui::badge(ui_locale::tr("DÉMO"), cyui::Tone::warning); }
            ImGui::SameLine(0.0f, cyui::dp(compact ? 8.0f : 18.0f));
            const char* pages[] {ui_locale::tr("Processus"), "Performance", "Assistant",
                "Console", ui_locale::french() ? "Températures" : "Temperatures",
                ui_locale::french() ? "Informations" : "System info"};
            int page = activePage();
            if (allPages) {
                if (cyui::segmented("##page", pages, 6, &page, cyui::dp(510.0f))) {
                    if (page == 2) switchAssistant();
                    else if (page >= 3) switchUtility(page);
                    else switchPage(page == 1);
                }
            } else {
                int mainPage = std::min(page, 2);
                if (cyui::segmented("##page", pages, 3, &mainPage, cyui::dp(compact ? 220.0f : 300.0f))) {
                    if (mainPage == 2) switchAssistant(); else switchPage(mainPage == 1);
                }
                ImGui::SameLine(0.0f, cyui::dp(5.0f));
                if (cyui::button(ui_locale::french() ? "Plus" : "More",
                        page >= 3 ? cyui::Variant::primary : cyui::Variant::secondary))
                    ImGui::OpenPopup("##morePages");
                if (ImGui::BeginPopup("##morePages")) {
                    for (int i = 3; i < 6; ++i)
                        if (ImGui::MenuItem(pages[i], nullptr, activePage() == i)) switchUtility(i);
                    ImGui::EndPopup();
                }
            }
            ImGui::SameLine();
            if (width >= cyui::dp(1160.0f) && hasSnapshot) {
                if (width >= cyui::dp(1400.0f)) cyui::alignRight(cyui::dp(580.0f));
                const auto& colors = cyui::palette();
                cyui::textDim("CPU"); ImGui::SameLine(0.0f, cyui::dp(4.0f));
                cyui::textColoured(colors.info, "%.0f %%", snapshot.cpu);
                ImGui::SameLine(0.0f, cyui::dp(12.0f));
                cyui::textDim("RAM"); ImGui::SameLine(0.0f, cyui::dp(4.0f));
                cyui::textColoured(colors.text, "%.0f %%", snapshot.memoryTotalGb > 0
                    ? snapshot.memoryUsedGb / snapshot.memoryTotalGb * 100.0 : 0.0);
                ImGui::SameLine(0.0f, cyui::dp(12.0f));
                cyui::textDim(ui_locale::tr("Processus")); ImGui::SameLine(0.0f, cyui::dp(4.0f));
                cyui::textColoured(colors.text, "%zu", snapshot.processes.size());
                ImGui::SameLine();
            }
            cyui::alignRight(cyui::dp(245.0f));
            if (compact) {
                if (cyui::iconButton(cyui::icon::zoomOut, ui_locale::french() ? "Réduire le texte" : "Smaller text"))
                    ui_scale::adjust(-10);
                ImGui::SameLine(0.0f, cyui::dp(3.0f));
                ImGui::Text("%d %%", ui_scale::percent());
                ImGui::SameLine(0.0f, cyui::dp(3.0f));
                if (cyui::iconButton(cyui::icon::zoomIn, ui_locale::french() ? "Agrandir le texte" : "Larger text"))
                    ui_scale::adjust(10);
            } else {
                if (cyui::iconButton(cyui::icon::zoomOut, ui_locale::french() ? "Réduire le texte" : "Smaller text"))
                    ui_scale::adjust(-10);
                ImGui::SameLine(0.0f, cyui::dp(4.0f));
                ImGui::Text("%d %%", ui_scale::percent());
                ImGui::SameLine(0.0f, cyui::dp(4.0f));
                if (cyui::iconButton(cyui::icon::zoomIn, ui_locale::french() ? "Agrandir le texte" : "Larger text"))
                    ui_scale::adjust(10);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(cyui::dp(75.0f));
            const char* languages[] { "English", "Français" };
            int language = ui_locale::french() ? 1 : 0;
            if (ImGui::Combo("##language", &language, languages, 2)) {
                ui_locale::setFrench(language == 1);
                cyui::setLocale(language == 1 ? "fr" : "en");
                dirty = true;
                if (auto* window = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw))
                    SetWindowTextW(window, language == 1
                        ? L"CyWinTask 0.10.1 — Gestionnaire des tâches" : L"CyWinTask 0.10.1 — Task Manager");
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(demoMode);
            if (cyui::iconTextButton(paused ? cyui::icon::play : cyui::icon::pause,
                    paused ? ui_locale::tr("Reprendre") : ui_locale::tr("Pause"), cyui::Variant::secondary)) {
                paused = !paused;
                bridge.send(paused ? "P 1\n" : "P 0\n");
                updateHardwareActivity();
            }
            ImGui::EndDisabled();
            cyui::tooltip(ui_locale::tr("Suspendre ou reprendre la collecte"));
        }
        cyui::endTopBar();
    }
    void processTable(const float height)
    {
        buildRows();
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingStretchProp;
        const float innerWidth = std::max(ImGui::GetContentRegionAvail().x, cyui::dp(1210.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
            ImVec2(cyui::dp(5.0f), cyui::dp(compactProcesses ? 1.0f : 3.0f)));
        if (!ImGui::BeginTable("##processes", 9, flags, ImVec2(0, height), innerWidth)) {
            ImGui::PopStyleVar();
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(ui_locale::tr("Nom du processus"), ImGuiTableColumnFlags_DefaultSort, 0.31f, 0);
        ImGui::TableSetupColumn(ui_locale::tr("Confiance"), ImGuiTableColumnFlags_PreferSortDescending, 0.09f, 1);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_PreferSortAscending, 0.06f, 2);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_PreferSortDescending, 0.09f, 3);
        ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_PreferSortDescending, 0.09f, 4);
        ImGui::TableSetupColumn(ui_locale::tr("Mémoire"), ImGuiTableColumnFlags_PreferSortDescending, 0.16f, 5);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_PreferSortDescending, 0.06f, 6);
        ImGui::TableSetupColumn("Handles", ImGuiTableColumnFlags_PreferSortDescending, 0.07f, 7);
        ImGui::TableSetupColumn(ui_locale::tr("Durée"), ImGuiTableColumnFlags_PreferSortDescending, 0.07f, 8);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0) {
            sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
            sortDirection = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending ? -1 : 1;
            specs->SpecsDirty = false;
            dirty = true;
            buildRows();
        }
        const auto& palette = cyui::palette();
        const float rowHeight = cyui::dp(compactProcesses ? 23.0f : 30.0f);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(display.size()), rowHeight);
        while (clipper.Step()) for (int displayIndex = clipper.DisplayStart; displayIndex < clipper.DisplayEnd; ++displayIndex) {
            const auto& row = display[displayIndex];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            if (row.groupHeader) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, palette.bg4);
                cyui::textColoured(palette.accentHover, "%s", row.title.c_str());
                continue;
            }
            const auto& process = snapshot.processes[row.index];
            const Key key = identity(process);
            const bool active = selected.count(key) != 0;
            const auto position = ImGui::GetCursorScreenPos();
            const std::string id = "##row" + std::to_string(process.id) + "_" + std::to_string(process.created);
            if (active)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, palette.selection);
            else if (row.depth > 0 || row.hasChildren)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row.depth ? palette.bg3 : palette.bg4);
            const bool clicked = ImGui::Selectable(id.c_str(), active,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(0, rowHeight - cyui::dp(compactProcesses ? 2.0f : 6.0f)));
            const auto bounds = ImGui::GetItemRectMax();
            const bool arrowClick = row.hasChildren && ImGui::IsItemHovered()
                && ImGui::GetMousePos().x < position.x + cyui::dp(18.0f + row.depth * 15.0f);
            if (clicked) {
                if (arrowClick) { if (!expanded.erase(key)) expanded.insert(key); dirty = true; }
                else { select(key, displayIndex); if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) requestDetails(process); }
            }
            auto* draw = ImGui::GetWindowDrawList();
            const float textY = position.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            if (treeMode && (row.depth > 0 || row.hasChildren)) {
                const float rail = position.x + cyui::dp(2.0f);
                draw->AddRectFilled(ImVec2(rail, position.y), ImVec2(rail + cyui::dp(2.0f), position.y + rowHeight),
                    palette.lineStrong);
                if (row.depth == 0) draw->AddLine(position, ImVec2(bounds.x, position.y), palette.lineStrong);
            }
            if (active) {
                draw->AddRect(position, ImVec2(bounds.x, position.y + rowHeight), palette.accent, 0, 0, cyui::dp(2.0f));
                draw->AddRectFilled(position, ImVec2(position.x + cyui::dp(4.0f), position.y + rowHeight), palette.accent);
            }
            float x = position.x + cyui::dp(5.0f + row.depth * 15.0f);
            if (row.hasChildren) {
                draw->AddText(ImVec2(x, textY), palette.accentHover,
                    expanded.count(key) ? "▼" : "▶");
            }
            x += cyui::dp(17.0f);
            if (auto* icon = icons.get(process.path)) {
                const float iconSize = cyui::dp(compactProcesses ? 15.0f : 18.0f);
                const float iconY = position.y + (rowHeight - iconSize) * 0.5f;
                draw->AddImage(ImTextureRef(icon), ImVec2(x, iconY),
                    ImVec2(x + iconSize, iconY + iconSize));
            } else draw->AddText(ImVec2(x, textY), palette.textFaint, cyui::icon::code);
            x += cyui::dp(28.0f);
            const auto available = std::max(0.0f, ImGui::GetCurrentTable()->Columns[0].WidthGiven - (x - position.x) - cyui::dp(8.0f));
            const std::string title = row.hasChildren
                ? process.name + " (" + std::to_string(row.descendants) + ")" : process.name;
            cyui::clippedText(draw, ImVec2(x, textY), available,
                active ? palette.accentText : palette.text, title.c_str());
            if (ImGui::BeginPopupContextItem(id.c_str())) {
                if (!selected.count(key)) { selected.clear(); selected.insert(key); }
                processActions(process);
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1); trustBar(trustScores[row.index], cyui::dp(compactProcesses ? 17.0f : 22.0f));
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", process.id);
            ImGui::TableSetColumnIndex(3); usageBar(process.cpu, std::isfinite(process.cpu) ? format("%.1f %%", process.cpu) : "—", false,
                compactProcesses ? 18.0f : 22.0f);
            const double gpu = treeMode && row.hasChildren ? process.gpuGroup : process.gpu;
            ImGui::TableSetColumnIndex(4);
            usageBar(gpu, std::isfinite(gpu) ? format("%.1f %%", gpu) : "—", false,
                compactProcesses ? 18.0f : 22.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(treeMode && row.hasChildren
                    ? ui_locale::tr("Groupe avec processus associés") : ui_locale::tr("Processus seul"));
                ImGui::TextUnformatted(ui_locale::french() ? "Moteur GPU le plus sollicité" : "Busiest GPU engine");
                if (std::isfinite(process.gpuLocalBytes))
                    ImGui::Text(ui_locale::french() ? "VRAM locale : %.1f Mo" : "Local VRAM: %.1f MB",
                        process.gpuLocalBytes / 1048576.0);
                if (std::isfinite(process.gpuSharedBytes))
                    ImGui::Text(ui_locale::french() ? "Mémoire GPU partagée : %.1f Mo" : "Shared GPU memory: %.1f MB",
                        process.gpuSharedBytes / 1048576.0);
                ImGui::EndTooltip();
            }
            const double memoryMb = process.workingSet / 1048576.0;
            const double memoryPercent = snapshot.memoryTotalGb > 0 ? memoryMb / (snapshot.memoryTotalGb * 1024.0) * 100.0 : 0.0;
            ImGui::TableSetColumnIndex(5);
            usageBar(memoryPercent, format(ui_locale::tr("%.1f Mo"), memoryMb) + " · " + format("%.1f %%", memoryPercent), true,
                compactProcesses ? 18.0f : 22.0f);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%d", process.threads);
            ImGui::TableSetColumnIndex(7); ImGui::Text("%d", process.handles);
            ImGui::TableSetColumnIndex(8); ImGui::TextUnformatted(processAge(process.created, snapshot.unixMs).c_str());
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }
    void processInspector()
    {
        if (cyui::beginCard("##processInspector", nullptr, compactProcesses ? 120.0f : 145.0f)) {
            if (selected.empty()) {
                cyui::heading(ui_locale::tr("Inspection rapide"), 3);
                cyui::textDim(ui_locale::tr("Sélectionnez un processus pour voir son identité, son chemin et la note de confiance."));
                cyui::textFaint(ui_locale::tr("Indice local basé sur la signature Authenticode et l'emplacement du fichier ; ce n'est pas un antivirus."));
            } else if (selected.size() > 1) {
                cyui::heading(ui_locale::tr("Sélection multiple"), 3);
                cyui::textDim(ui_locale::tr("%zu processus sélectionnés · clic droit pour les actions disponibles."), selected.size());
            } else if (const auto* process = find(*selected.begin())) {
                const auto index = static_cast<std::size_t>(process - snapshot.processes.data());
                const auto& result = trustScores[index];
                cyui::heading(process->name.c_str(), 3);
                ImGui::SameLine(0.0f, cyui::dp(16.0f));
                if (result.score >= 0) cyui::textColoured(result.score >= 80 ? cyui::palette().success
                    : result.score >= 55 ? cyui::palette().info
                    : result.score >= 30 ? cyui::palette().warning : cyui::palette().error,
                    ui_locale::tr("Confiance %d/100 · %s"), result.score, ui_locale::text(result.label).c_str());
                else cyui::textFaint(ui_locale::tr("Confiance — · %s"), ui_locale::text(result.label).c_str());
                cyui::textDim(ui_locale::tr("PID %d · Parent %d · %s · %s · CPU %.1f %% · GPU %s · %.1f Mo"),
                    process->id, process->parent, categoryName(process->category),
                    processAge(process->created, snapshot.unixMs).c_str(),
                    process->cpu, std::isfinite(process->gpu) ? format("%.1f %%", process->gpu).c_str() : "—",
                    process->workingSet / 1048576.0);
                if (std::isfinite(process->gpuLocalBytes) || std::isfinite(process->gpuSharedBytes))
                    cyui::textDim(ui_locale::french()
                        ? "VRAM locale %s Mo · GPU partagé %s Mo · engagé %s Mo"
                        : "Local VRAM %s MB · shared GPU %s MB · committed %s MB",
                        std::isfinite(process->gpuLocalBytes) ? format("%.1f", process->gpuLocalBytes / 1048576.0).c_str() : "—",
                        std::isfinite(process->gpuSharedBytes) ? format("%.1f", process->gpuSharedBytes / 1048576.0).c_str() : "—",
                        std::isfinite(process->gpuCommittedBytes) ? format("%.1f", process->gpuCommittedBytes / 1048576.0).c_str() : "—");
                cyui::textFaint("%s", ui_locale::text(result.explanation).c_str());
                if (!process->path.empty()) {
                    cyui::textDim(ui_locale::tr("Fichier : %s"), process->path.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", process->path.c_str());
                } else cyui::textFaint(ui_locale::tr("Chemin du fichier indisponible."));
            }
        }
        cyui::endCard();
    }
    void processes()
    {
        if (!compactProcesses) {
            cyui::panelHeader(ui_locale::tr("Processus"), ui_locale::tr("Recherche, tri et inspection des tâches en cours"));
            cyui::space(4);
        }
        const char* processViews[] {ui_locale::french() ? "Tous les processus" : "All processes",
            ui_locale::french() ? "Processus GPU" : "GPU processes"};
        if (cyui::segmented("##processViews", processViews, 2, &processView, cyui::dp(340.0f)))
            updateHardwareActivity();
        if (hasSnapshot && ImGui::GetContentRegionAvail().x > cyui::dp(510.0f)) {
            double gpuUsage = NAN, vramUsed = 0.0, vramTotal = 0.0;
            for (const auto& device : snapshot.devices) if (device.kind == "gpu") {
                if (const auto* usage = metric(device, "usage"); usage && std::isfinite(usage->value))
                    gpuUsage = std::isfinite(gpuUsage) ? std::max(gpuUsage, usage->value) : usage->value;
                if (const auto* vram = metric(device, "dedicated"); vram && std::isfinite(vram->value)
                    && std::isfinite(vram->maximum) && vram->maximum > 0.0) {
                    vramUsed += vram->value; vramTotal += vram->maximum;
                }
            }
            ImGui::SameLine();
            cyui::alignRight(cyui::dp(460.0f));
            cyui::textDim("CPU %.0f %%", snapshot.cpu); ImGui::SameLine(0.0f, cyui::dp(12.0f));
            cyui::textDim("RAM %.0f %%", snapshot.memoryTotalGb > 0.0
                ? snapshot.memoryUsedGb / snapshot.memoryTotalGb * 100.0 : 0.0);
            ImGui::SameLine(0.0f, cyui::dp(12.0f));
            if (std::isfinite(gpuUsage)) cyui::textDim("GPU %.0f %%", gpuUsage);
            else cyui::textDim("GPU —");
            ImGui::SameLine(0.0f, cyui::dp(12.0f));
            if (vramTotal > 0.0) cyui::textDim("VRAM %.0f %%", vramUsed / vramTotal * 100.0);
            else cyui::textDim("VRAM —");
        }
        cyui::space(5);
        if (hasSnapshot && !demoMode && ImGui::GetTime() >= nextTrustRefresh) {
            if (refreshTrustScores()) dirty = true;
            nextTrustRefresh = ImGui::GetTime() + 0.75;
        }
        if (processView == 1) { gpuProcesses(); return; }
        cyui::space(2);
        if (cyui::beginToolbar("##processToolbar")) {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) focusSearch = true;
            if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
            const float actions = cyui::dp(search[0] ? 325.0f : 255.0f);
            const float width = std::max(cyui::dp(180.0f), ImGui::GetContentRegionAvail().x - actions);
            if (cyui::searchField("##search", search, sizeof(search), ui_locale::tr("Nom, PID ou pid:1234..."), width))
                dirty = true;
            cyui::tooltip(ui_locale::tr("Saisissez pid:1234 pour isoler un processus précis."));
            if (search[0]) {
                ImGui::SameLine();
                if (cyui::button(ui_locale::tr("Effacer"), cyui::Variant::ghost)) { search[0] = 0; dirty = true; }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(selected.size() != 1);
            if (cyui::button(ui_locale::tr("Inspecter"), cyui::Variant::secondary) && selected.size() == 1)
                if (const auto* process = find(*selected.begin())) requestDetails(*process);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(demoMode || selected.empty());
            if (cyui::dangerButton(selected.size() > 1 ? (ui_locale::tr("Arrêter ") + std::to_string(selected.size()) + ui_locale::tr(" tâches")).c_str()
                                                       : ui_locale::tr("Arrêter la tâche")))
                askStop();
            ImGui::EndDisabled();
        }
        cyui::endToolbar();
        cyui::space(2);
        const bool narrowFilters = ImGui::GetContentRegionAvail().x < cyui::dp(830.0f);
        const auto filterOptions = [this] {
            if (ImGui::Checkbox(ui_locale::tr("Regrouper par type"), &groupByType)) dirty = true;
            ImGui::SameLine(0.0f, cyui::dp(16.0f));
            if (ImGui::Checkbox(ui_locale::tr("Processus associés"), &treeMode)) dirty = true;
            ImGui::SameLine(0.0f, cyui::dp(16.0f));
            ImGui::Checkbox(ui_locale::tr("Compact"), &compactProcesses);
            ImGui::SameLine(0.0f, cyui::dp(16.0f));
            ImGui::Checkbox(ui_locale::tr("Inspection"), &showProcessInspector);
        };
        if (cyui::beginToolbar("##processFilters")) {
            cyui::textDim(ui_locale::tr("Afficher")); ImGui::SameLine();
            ImGui::SetNextItemWidth(cyui::dp(220.0f));
            const char* filters[] { ui_locale::tr("Tous les processus"), ui_locale::tr("Applications"), ui_locale::tr("Arrière-plan"), ui_locale::tr("Processus Windows") };
            if (ImGui::Combo("##categoryFilter", &categoryFilter, filters, 4)) dirty = true;
            if (!narrowFilters) { ImGui::SameLine(0.0f, cyui::dp(16.0f)); filterOptions(); }
        }
        cyui::endToolbar();
        if (narrowFilters) {
            if (cyui::beginToolbar("##processFilterOptions")) filterOptions();
            cyui::endToolbar();
        }
        cyui::space(2);
        if (hasSnapshot) buildRows();
        const float available = ImGui::GetContentRegionAvail().y;
        const bool inspectorVisible = showProcessInspector && available >= cyui::dp(340.0f);
        const float inspectorHeight = cyui::dp(compactProcesses ? 120.0f : 145.0f);
        const float tableHeight = inspectorVisible ? std::max(cyui::dp(140.0f), available - inspectorHeight - cyui::dp(5.0f)) : available;
        if (!hasSnapshot) cyui::emptyState(ui_locale::tr("En attente des processus"), ui_locale::tr("Le collecteur démarre."), cyui::icon::refresh);
        else if (display.empty()) cyui::emptyState(ui_locale::tr("Aucun processus trouvé"), ui_locale::tr("Modifiez la recherche ou le filtre."), cyui::icon::search);
        else processTable(tableHeight);
        if (inspectorVisible) { cyui::space(2); processInspector(); }
    }
    ImU32 seriesColor(const DeviceInfo& device, const MetricInfo& value) const
    {
        return cyui::categorical(seriesIndex(device, value));
    }
    int seriesIndex(const DeviceInfo& device, const MetricInfo& value) const
    {
        if (device.kind == "memory") return value.id == "used" ? 3 : value.id == "available" ? 4 : 5;
        if (device.kind == "disk") return value.id == "active" ? 6 : value.id == "read" ? 7 : 8;
        if (device.kind == "network") return value.id == "receive" ? 9 : 10;
        if (device.kind == "gpu") return 11;
        return 0;
    }
    void chart(const DeviceInfo& device, const std::vector<const MetricInfo*>& series, const float height)
    {
        const bool disk = device.kind == "disk" && series.size() > 1;
        const double now = snapshot.unixMs / 1000.0;
        const auto maximumFor = [&](const MetricInfo* value) {
            if (!value) return 1.0;
            if (std::isfinite(value->maximum) && value->maximum > 0) return value->maximum;
            double maximum = std::isfinite(value->value) ? value->value : 0.0;
            if (const auto found = histories.find(device.id + "/" + value->id); found != histories.end())
                for (const auto& point : found->second)
                    if (std::isfinite(point.value) && now - point.time <= 60.0)
                        maximum = std::max(maximum, point.value);
            return std::max(1.0, maximum * 1.15);
        };
        double leftMaximum = disk ? 100.0 : maximumFor(series.empty() ? nullptr : series.front());
        if (!disk)
            for (std::size_t i = 1; i < series.size(); ++i)
                leftMaximum = std::max(leftMaximum, maximumFor(series[i]));
        std::vector<cyui::PlotSeries> lines;
        lines.reserve(series.size());
        std::vector<std::string> translatedLabels;
        translatedLabels.reserve(series.size());
        for (const auto* value : series) {
            if (!value) continue;
            const auto found = plotPoints.find(device.id + "/" + value->id);
            if (found == plotPoints.end()) continue;
            cyui::PlotSeries line;
            line.id = static_cast<cyui::ItemId>(lines.size() + 1);
            translatedLabels.push_back(ui_locale::text(value->label));
            line.label = translatedLabels.back().c_str();
            line.points = cyui::ArrayView<cyui::Point>(found->second);
            line.colorIndex = seriesIndex(device, *value);
            lines.push_back(line);
        }
        cyui::PlotState state;
        state.x.range = {now - 60.0, now};
        state.y.range = {0.0, leftMaximum};
        cyui::PlotOptions options;
        options.navigation = false;
        options.legend = false;
        options.crosshair = false;
        options.axes = false;
        const std::string unit = series.empty() || !series.front() ? "" : ui_locale::text(series.front()->unit);
        const auto axisLabel = [&unit](double value) {
            return format(value < 10.0 ? "%.1f" : "%.0f", value) + (unit.empty() ? "" : " " + unit);
        };
        const float fontSize = std::max(cyui::dp(10.0f), ImGui::GetFontSize() * 0.78f);
        auto* font = ImGui::GetFont();
        const auto labelWidth = [font, fontSize](const std::string& text) {
            return font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text.c_str()).x;
        };
        const float leftMargin = std::max(cyui::dp(37.0f), labelWidth(axisLabel(leftMaximum)) + cyui::dp(7.0f));
        const double rateMaximum = disk && diskRateScale.count(device.id) ? diskRateScale.at(device.id) : 1.0;
        const float rightMargin = disk ? labelWidth(format("%.0f", rateMaximum) + ui_locale::tr(" Mo/s")) + cyui::dp(9.0f) : 0.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        const float width = std::max(cyui::dp(50.0f), available - leftMargin - rightMargin);
        const float bottomMargin = std::max(cyui::dp(17.0f), fontSize + cyui::dp(4.0f));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x + leftMargin, start.y));
        const std::string id = "##plot_" + device.id + "_" + (series.empty() || !series.front() ? "empty" : series.front()->id);
        const auto result = cyui::plot(id.c_str(), state, cyui::ArrayView<cyui::PlotSeries>(lines),
            ImVec2(width, std::max(cyui::dp(42.0f), cyui::dp(height) - bottomMargin)), options);
        const ImVec2 plotMin = ImGui::GetItemRectMin();
        const ImVec2 plotMax = ImGui::GetItemRectMax();
        auto* draw = ImGui::GetWindowDrawList();
        for (int step = 0; step <= 4; ++step) {
            const float y = plotMax.y - (plotMax.y - plotMin.y) * step / 4.0f;
            const auto left = axisLabel(leftMaximum * step / 4.0);
            draw->AddText(font, fontSize, ImVec2(plotMin.x - cyui::dp(5.0f) - labelWidth(left),
                std::clamp(y - fontSize * 0.5f, plotMin.y, plotMax.y - fontSize)),
                cyui::palette().textDim, left.c_str());
            if (disk) {
                const auto label = format("%.0f", rateMaximum * step / 4.0) + ui_locale::tr(" Mo/s");
                draw->AddText(font, fontSize,
                    ImVec2(plotMax.x + cyui::dp(3.0f),
                        std::clamp(y - fontSize * 0.5f, plotMin.y, plotMax.y - fontSize)),
                    cyui::palette().textDim, label.c_str());
            }
        }
        for (int step = 0; step <= 2; ++step) {
            const auto label = std::to_string(-60 + step * 30) + " s";
            const float x = plotMin.x + (plotMax.x - plotMin.x) * step / 2.0f;
            draw->AddText(font, fontSize,
                ImVec2(std::clamp(x - labelWidth(label) * 0.5f, plotMin.x,
                    std::max(plotMin.x, plotMax.x - labelWidth(label))), plotMax.y + cyui::dp(2.0f)),
                cyui::palette().textDim, label.c_str());
        }
        if (result.hovered) {
            const double secondsAgo = std::clamp(now - result.mouse.x, 0.0, 60.0);
            draw->AddLine(ImVec2(ImGui::GetMousePos().x, plotMin.y),
                ImVec2(ImGui::GetMousePos().x, plotMax.y), cyui::palette().textDim, cyui::dp(1.0f));
            ImGui::BeginTooltip();
            ImGui::Text(ui_locale::tr("Il y a %.0f s"), secondsAgo);
            for (const auto* value : series) {
                if (!value) continue;
                const auto found = histories.find(device.id + "/" + value->id);
                if (found == histories.end()) continue;
                const PlotPoint* closest = nullptr;
                double distance = 2.5;
                for (const auto& point : found->second)
                    if (std::isfinite(point.value) && std::abs(point.time - result.mouse.x) < distance) {
                        closest = &point;
                        distance = std::abs(point.time - result.mouse.x);
                    }
                if (closest) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cyui::categorical(seriesIndex(device, *value))),
                    "%s : %.1f %s", ui_locale::text(value->label).c_str(), closest->value,
                    ui_locale::text(value->unit).c_str());
            }
            ImGui::EndTooltip();
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, plotMax.y));
        ImGui::Dummy(ImVec2(available, bottomMargin));
    }
    void chartMetric(const DeviceInfo& device, const MetricInfo& value, const float height)
    {
        ImGui::TextUnformatted(ui_locale::text(value.label).c_str());
        ImGui::SameLine();
        if (std::isfinite(value.value)) cyui::textColoured(seriesColor(device, value), "%.1f %s", value.value,
            ui_locale::text(value.unit).c_str());
        else cyui::textFaint(ui_locale::tr("Indisponible"));
        chart(device, { &value }, height);
    }
    void resourceDetails(const DeviceInfo& device)
    {
        cyui::heading(ui_locale::text(device.title).c_str(), 2);
        if (device.kind == "gpu") {
            ImGui::SameLine();
            cyui::alignRight(cyui::dp(250.0f));
            cyui::textDim("%s", ui_locale::text(device.description).c_str());
            const auto* temperature = metric(device, "temperature");
            const auto* usage = metric(device, "usage");
            if (temperature && std::isfinite(temperature->value))
                cyui::textDim(ui_locale::french() ? "Température %.1f °C" : "Temperature %.1f °C", temperature->value);
            else cyui::textFaint(ui_locale::french() ? "Température indisponible" : "Temperature unavailable");
            ImGui::SameLine(0.0f, cyui::dp(14.0f));
            if (usage && std::isfinite(usage->value))
                cyui::textColoured(cyui::palette().success, ui_locale::french()
                    ? "Utilisation totale %.1f %%" : "Total usage %.1f %%", usage->value);
            else cyui::textFaint(ui_locale::french() ? "Utilisation indisponible" : "Usage unavailable");
            ImGui::SameLine();
            cyui::alignRight(cyui::dp(135.0f));
            if (cyui::button(ui_locale::french() ? "Processus GPU" : "GPU processes", cyui::Variant::secondary)) {
                processView = 1;
                switchPage(false);
            }
        } else {
            cyui::textDim("%s", ui_locale::text(device.description).c_str());
        }
        cyui::space(5);
        if (device.kind == "cpu") {
            ImGui::Checkbox(ui_locale::tr("Afficher les processeurs logiques"), &showCores);
            if (showCores && !detailGraphCards) {
                const char* modes[] { ui_locale::tr("Compacte (Windows)"), ui_locale::tr("Détaillée") };
                int mode = compactCores ? 0 : 1;
                if (cyui::segmented("##cpuMode", modes, 2, &mode, cyui::dp(340.0f))) compactCores = mode == 0;
            }
        }
        if (detailGraphCards) {
            std::vector<const MetricInfo*> visible;
            for (const auto& value : device.metrics) {
                if (value.id.rfind("core:", 0) == 0 && !showCores) continue;
                visible.push_back(&value);
            }
            if (visible.empty()) return;
            const float available = ImGui::GetContentRegionAvail().x;
            const int columns = std::clamp(static_cast<int>(available / cyui::dp(330.0f)), 1,
                std::min(3, static_cast<int>(visible.size())));
            if (columns > 0 && ImGui::BeginTable("##detailCards", columns,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
                for (const auto* value : visible) {
                    ImGui::TableNextColumn();
                    const std::string id = "##detail_" + device.id + "/" + value->id;
                    if (cyui::beginCard(id.c_str(), nullptr, 185.0f)) {
                        cyui::textDim("%s", ui_locale::text(value->label).c_str());
                        if (std::isfinite(value->value))
                            cyui::textColoured(seriesColor(device, *value), "%.1f %s", value->value,
                                ui_locale::text(value->unit).c_str());
                        else cyui::textFaint(ui_locale::tr("Indisponible"));
                        chart(device, {value}, 112.0f);
                    }
                    cyui::endCard();
                }
                ImGui::EndTable();
            }
            return;
        }
        cyui::space(6);
        if (device.kind == "cpu") {
            if (const auto* total = metric(device, "cpu")) {
                if (!showCores || !compactCores) chartMetric(device, *total, 165);
                else cyui::textDim(ui_locale::tr("Utilisation totale : %.1f %%"), total->value);
            }
            if (showCores) {
                std::vector<const MetricInfo*> cores;
                for (const auto& value : device.metrics) if (value.id.rfind("core:", 0) == 0) cores.push_back(&value);
                const int columns = compactCores ? 4 : 2;
                if (ImGui::BeginTable("##cores", columns, ImGuiTableFlags_SizingStretchSame))
                {
                    for (const auto* core : cores) {
                        ImGui::TableNextColumn();
                        chartMetric(device, *core, compactCores ? 70.0f : 110.0f);
                    }
                    ImGui::EndTable();
                }
            }
        } else if (device.kind == "disk") {
            const std::vector<const MetricInfo*> series { metric(device, "active"), metric(device, "read"), metric(device, "write") };
            for (const auto* value : series)
                if (value) { cyui::textColoured(seriesColor(device, *value), "● %s : %.1f %s",
                    ui_locale::text(value->label).c_str(), value->value,
                    ui_locale::text(value->unit).c_str()); ImGui::SameLine(); }
            ImGui::NewLine();
            chart(device, series, 340.0f);
        } else {
            for (const auto& value : device.metrics) {
                if (value.id == "temperature") continue;
                chartMetric(device, value, device.kind == "gpu" ? 110.0f : 155.0f);
                cyui::space(8);
            }
        }
    }
    void dashboardCard(const DeviceInfo& device, const bool single, const bool wideRow = false)
    {
        const auto* primary = metric(device, device.kind == "cpu" ? "cpu" : device.kind == "memory" ? "used"
            : device.kind == "disk" ? "active" : device.kind == "network" ? "receive" : "usage");
        if (!primary) return;
        std::vector<const MetricInfo*> series { primary };
        if (device.kind == "memory") series.push_back(metric(device, "commit"));
        else if (device.kind == "disk") {
            series.push_back(metric(device, "read"));
            series.push_back(metric(device, "write"));
        } else if (device.kind == "network") series.push_back(metric(device, "send"));
        const float chartHeight = wideRow ? (compactGraphCards ? 145.0f : 210.0f)
            : single ? (compactGraphCards ? 155.0f : 225.0f)
            : (compactGraphCards ? 83.0f : 160.0f);
        const float cardHeight = wideRow ? (compactGraphCards ? 235.0f : 310.0f)
            : single ? (compactGraphCards ? 265.0f : 335.0f)
            : (compactGraphCards ? 191.0f : 260.0f);
        const std::string id = "##graphCard_" + device.id;
        if (cyui::beginCard(id.c_str(), nullptr, cardHeight)) {
            cyui::heading(device.kind == "memory" ? ui_locale::tr("Mémoire") : ui_locale::text(device.title).c_str(), 3);
            const bool memoryInline = device.kind == "memory" && !single && !wideRow;
            if (memoryInline) {
                ImGui::SameLine(0.0f, cyui::dp(12.0f));
                if (std::isfinite(primary->value))
                    cyui::textColoured(seriesColor(device, *primary), "%.1f %s", primary->value,
                        ui_locale::text(primary->unit).c_str());
                else cyui::textFaint(ui_locale::tr("Mesure indisponible"));
            }
            if (!single || wideRow) {
                ImGui::SameLine();
                cyui::alignRight(cyui::dp(28.0f));
                if (cyui::iconButton(cyui::icon::newWindow, ui_locale::tr("Ouvrir ce graphique dans une fenêtre séparée"),
                    false, 0, cyui::dp(26.0f, 24.0f))) {
                    detached[device.id] = true;
                    if (!demoMode) bridge.send("M 1\n");
                }
            }
            if (!memoryInline) {
                cyui::ScopedFont font(cyui::boldFont(), 1.08f);
                if (std::isfinite(primary->value))
                    cyui::textColoured(seriesColor(device, *primary), "%.1f %s", primary->value,
                        ui_locale::text(primary->unit).c_str());
                else cyui::textFaint(ui_locale::tr("Mesure indisponible"));
            }
            {
                cyui::ScopedFont font(nullptr, 0.88f);
                if (series.size() > 1) {
                    for (std::size_t i = 0; i < series.size(); ++i) {
                        const auto* value = series[i];
                        if (!value) continue;
                        if (device.kind == "memory" && i == 0) continue;
                        const char* shortName = device.kind == "memory" ? (i == 0 ? "RAM" : "Virt.")
                            : device.kind == "disk" ? (i == 0 ? "Act." : i == 1 ? (ui_locale::french() ? "L" : "R")
                                : (ui_locale::french() ? "E" : "W"))
                            : (i == 0 ? "R" : (ui_locale::french() ? "E" : "S"));
                        if (i > 0 && device.kind != "memory") ImGui::SameLine(0.0f, cyui::dp(8.0f));
                        if (std::isfinite(value->value))
                            cyui::textColoured(seriesColor(device, *value),
                                device.kind == "disk" ? "%s %.0f" : "%s %.1f", shortName, value->value);
                        else cyui::textFaint("%s —", shortName);
                    }
                } else {
                    std::string detail;
                    if (const auto* temperature = metric(device, "temperature"); temperature && std::isfinite(temperature->value))
                        detail = ui_locale::tr("Température ") + format("%.0f", temperature->value) + " °C";
                    if (const auto* dedicated = metric(device, "dedicated"); dedicated && std::isfinite(dedicated->value))
                        detail += (detail.empty() ? "" : "  ·  ") + std::string("VRAM ")
                            + format("%.1f", dedicated->value) + ui_locale::tr(" Go");
                    if (detail.empty()) detail = ui_locale::tr("Historique des 60 dernières secondes");
                    cyui::textDim("%s", detail.c_str());
                }
            }
            cyui::space(3.0f);
            chart(device, series, chartHeight);
        }
        cyui::endCard();
    }
    void topProcessCard(const char* id, const char* title, const std::vector<int>& ranking, const int valueKind)
    {
        if (cyui::beginCard(id, nullptr, 230.0f)) {
            cyui::heading(title, 3);
            if (ranking.empty()) cyui::textFaint(ui_locale::tr("Aucun processus mesuré"));
            else if (ImGui::BeginTable((std::string("##ranking_") + id).c_str(), 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn(ui_locale::tr("Processus"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(ui_locale::tr("Valeur"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(105.0f));
                for (const int index : ranking) {
                    const auto& process = snapshot.processes[index];
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, cyui::dp(27.0f));
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(process.id);
                    if (ImGui::Selectable(process.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns,
                        ImVec2(0.0f, cyui::dp(25.0f)))) focusProcess(process);
                    if (ImGui::IsItemHovered()) cyui::tooltip(ui_locale::tr("Afficher ce processus dans la liste"));
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(1);
                    if (valueKind == 0 && std::isfinite(process.cpu))
                        cyui::textColoured(heat(process.cpu, false), "%.1f %%", process.cpu);
                    else if (valueKind == 2 && std::isfinite(process.gpu))
                        cyui::textColoured(heat(process.gpu, false), "%.1f %%", process.gpu);
                    else if (valueKind == 1)
                        cyui::textColoured(cyui::palette().text, ui_locale::tr("%.0f Mo"), process.workingSet / 1048576.0);
                    else cyui::textFaint("—");
                }
                ImGui::EndTable();
            }
        }
        cyui::endCard();
    }
    void topProcesses()
    {
        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = available >= cyui::dp(1060.0f) ? 3 : available >= cyui::dp(690.0f) ? 2 : 1;
        if (ImGui::BeginTable("##topProcesses", columns,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextColumn();
            topProcessCard("##topCpu", ui_locale::tr("Processus · CPU"), topCpu, 0);
            ImGui::TableNextColumn();
            topProcessCard("##topMemory", ui_locale::tr("Processus · mémoire"), topMemory, 1);
            ImGui::TableNextColumn();
            topProcessCard("##topGpu", ui_locale::french() ? "Processus · GPU" : "Processes · GPU", topGpu, 2);
            ImGui::EndTable();
        }
    }
    void hardwareHighlights()
    {
        struct Highlight { const char* label; const MetricInfo* value; };
        const DeviceInfo* cpu = nullptr;
        const DeviceInfo* gpu = nullptr;
        for (const auto& device : snapshot.devices) {
            if (device.kind == "cpu" && !cpu) cpu = &device;
            if (device.kind == "gpu" && !gpu) gpu = &device;
        }
        if (!cpu && !gpu) return;
        const MetricInfo* power = nullptr;
        const MetricInfo* clock = nullptr;
        if (gpu) for (const auto& item : gpu->metrics) {
            if (item.id.rfind("telemetry:", 0) != 0) continue;
            if (!power && item.unit == "W") power = &item;
            if (!clock && item.unit == "MHz" && item.label.find("Core") != std::string::npos) clock = &item;
        }
        std::vector<Highlight> values;
        if (cpu) values.push_back({ui_locale::french() ? "Température CPU" : "CPU temperature", metric(*cpu, "temperature")});
        if (gpu) values.push_back({ui_locale::french() ? "Température GPU" : "GPU temperature", metric(*gpu, "temperature")});
        if (power) values.push_back({ui_locale::french() ? "Puissance GPU" : "GPU power", power});
        if (clock) values.push_back({ui_locale::french() ? "Fréquence GPU" : "GPU clock", clock});
        values.erase(std::remove_if(values.begin(), values.end(), [](const auto& value) {
            return !value.value || !std::isfinite(value.value->value);
        }), values.end());
        if (values.empty()) return;
        cyui::space(8);
        cyui::heading(ui_locale::french() ? "Matériel en direct" : "Live hardware", 4);
        const int columns = std::min(4, static_cast<int>(values.size()));
        if (ImGui::BeginTable("##hardwareHighlights", columns,
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                ImGui::TableNextColumn();
                const std::string id = "##hardwareHighlight" + std::to_string(i);
                if (cyui::beginCard(id.c_str(), nullptr, 86.0f)) {
                    cyui::textDim("%s", values[i].label);
                    cyui::textColoured(cyui::palette().text, "%.1f %s",
                        values[i].value->value, ui_locale::text(values[i].value->unit).c_str());
                }
                cyui::endCard();
            }
            ImGui::EndTable();
        }
    }
    void dashboard(const std::string& only, const bool withTopProcesses = false)
    {
        std::vector<const DeviceInfo*> visible;
        for (const char* kind : { "cpu", "memory", "gpu", "disk", "network" }) {
            if (only.empty() && !graphExtraDevices && std::strcmp(kind, "disk") == 0) continue;
            if (only.empty() && !graphExtraDevices && std::strcmp(kind, "network") == 0) continue;
            for (const auto& device : snapshot.devices)
                if (device.kind == kind && (only.empty() || device.id == only)) visible.push_back(&device);
        }
        if (visible.empty()) {
            cyui::emptyState(ui_locale::tr("Mesures en attente"), ui_locale::tr("Les courbes apparaîtront après la collecte."), cyui::icon::refresh);
            return;
        }
        const float available = ImGui::GetContentRegionAvail().x;
        const float gap = ImGui::GetStyle().CellPadding.x * 2.0f;
        const int automatic = std::max(1, static_cast<int>((available + gap)
            / (cyui::dp(compactGraphCards ? 240.0f : 380.0f) + gap)));
        const int safe = std::max(1, static_cast<int>((available + gap) / (cyui::dp(225.0f) + gap)));
        const int requested = only.empty() && !graphRows ? (graphColumnsMode ? graphColumnsMode : automatic) : 1;
        const int columns = std::clamp(std::min(requested, safe), 1, std::min(5, static_cast<int>(visible.size())));
        if (ImGui::BeginTable(("##dashboardGrid" + only).c_str(), columns,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings)) {
            for (const auto* device : visible) {
                ImGui::TableNextColumn();
                dashboardCard(*device, !only.empty() || graphRows, only.empty() && graphRows);
            }
            ImGui::EndTable();
        }
        if (withTopProcesses) {
            cyui::space(10.0f);
            topProcesses();
            hardwareHighlights();
        }
    }
    std::string resourceSummary(const DeviceInfo& device) const
    {
        const auto value = [&](const char* id) {
            const auto* current = metric(device, id);
            return current && std::isfinite(current->value) ? current->value : NAN;
        };
        const double temperature = value("temperature");
        if (device.kind == "cpu" || device.kind == "gpu") {
            const double usage = value(device.kind == "cpu" ? "cpu" : "usage");
            if (!std::isfinite(usage)) return ui_locale::tr("Mesure indisponible");
            std::string summary = format("%.1f", usage) + " %";
            if (std::isfinite(temperature)) summary += "  ·  " + format("%.0f", temperature) + " °C";
            return summary;
        }
        if (device.kind == "memory") {
            const auto* used = metric(device, "used");
            if (!used || !std::isfinite(used->value)) return ui_locale::tr("Mesure indisponible");
            std::string summary = format("%.1f", used->value) + ui_locale::tr(" Go");
            if (std::isfinite(used->maximum) && used->maximum > 0)
                summary += " / " + format("%.1f", used->maximum) + ui_locale::tr(" Go");
            return summary;
        }
        if (device.kind == "disk") {
            const double active = value("active"), read = value("read"), write = value("write");
            std::string summary = std::isfinite(active) ? format("%.0f", active) + " %" : ui_locale::tr("Activité indisponible");
            if (std::isfinite(read) && std::isfinite(write))
                summary += (ui_locale::french() ? "  ·  L " : "  ·  R ") + format("%.1f", read)
                    + (ui_locale::french() ? " / E " : " / W ") + format("%.1f", write)
                    + ui_locale::tr(" Mo/s");
            return summary;
        }
        if (device.kind == "network") {
            const double receive = value("receive"), send = value("send");
            if (std::isfinite(receive) && std::isfinite(send))
                return "R " + format("%.1f", receive)
                    + (ui_locale::french() ? " / E " : " / S ") + format("%.1f", send)
                    + ui_locale::tr(" Mbits/s");
        }
        return ui_locale::tr("Mesure indisponible");
    }
    void performance()
    {
        cyui::panelHeader("Performance", ui_locale::tr("Mesures en direct · 60 secondes d'historique · survolez les courbes pour lire les valeurs"));
        cyui::space(5);
        if (cyui::beginToolbar("##performanceToolbar")) {
            const char* views[] { ui_locale::tr("Vue graphiques"), ui_locale::tr("Vue détaillée") };
            int view = graphMode ? 0 : 1;
            if (cyui::segmented("##performanceView", views, 2, &view, cyui::dp(275.0f))) graphMode = view == 0;
            ImGui::SameLine();
            if (graphMode) {
                const char* layouts[] {ui_locale::french() ? "Cartes" : "Cards",
                    ui_locale::french() ? "Lignes" : "Rows"};
                int layout = graphRows ? 1 : 0;
                if (cyui::segmented("##graphLayout", layouts, 2, &layout, cyui::dp(155.0f)))
                    graphRows = layout == 1;
                ImGui::SameLine();
                if (cyui::button(ui_locale::tr("Disposition"), cyui::Variant::secondary)) ImGui::OpenPopup("##graphDisposition");
                if (ImGui::BeginPopup("##graphDisposition")) {
                    if (ImGui::MenuItem(ui_locale::tr("Colonnes automatiques"), nullptr, graphColumnsMode == 0)) graphColumnsMode = 0;
                    if (ImGui::MenuItem(ui_locale::tr("2 colonnes"), nullptr, graphColumnsMode == 2)) graphColumnsMode = 2;
                    if (ImGui::MenuItem(ui_locale::tr("3 colonnes"), nullptr, graphColumnsMode == 3)) graphColumnsMode = 3;
                    if (ImGui::MenuItem(ui_locale::tr("4 colonnes"), nullptr, graphColumnsMode == 4)) graphColumnsMode = 4;
                    if (ImGui::MenuItem(ui_locale::tr("5 colonnes"), nullptr, graphColumnsMode == 5)) graphColumnsMode = 5;
                    ImGui::Separator();
                    ImGui::MenuItem(ui_locale::tr("Cartes compactes"), nullptr, &compactGraphCards);
                    ImGui::MenuItem(ui_locale::tr("Afficher disques et réseau"), nullptr, &graphExtraDevices);
                    ImGui::EndPopup();
                }
            } else {
                const char* layouts[] {ui_locale::french() ? "Cartes" : "Cards",
                    ui_locale::french() ? "Lignes" : "Rows"};
                int layout = detailGraphCards ? 0 : 1;
                if (cyui::segmented("##detailLayout", layouts, 2, &layout, cyui::dp(155.0f)))
                    detailGraphCards = layout == 0;
            }
            ImGui::SameLine();
            cyui::alignRight(cyui::dp(145.0f));
            if (cyui::button(ui_locale::tr("Fenêtre séparée"), cyui::Variant::secondary)) {
                detached["all"] = true;
                if (!demoMode) bridge.send("M 1\n");
            }
        }
        cyui::endToolbar();
        cyui::space(6);
        if (!hasSnapshot || snapshot.devices.empty()) {
            cyui::emptyState(ui_locale::tr("Mesures en attente"), ui_locale::tr("La collecte démarre dans cet onglet."), cyui::icon::refresh);
            return;
        }
        if (graphMode) {
            ImGui::BeginChild("##dashboardScroll", ImVec2(0, 0), ImGuiChildFlags_None);
            dashboard("", !graphRows);
            ImGui::EndChild();
            return;
        }
        ImGui::BeginChild("##resources", ImVec2(cyui::dp(245.0f), 0), ImGuiChildFlags_Borders);
        for (const auto& device : snapshot.devices) {
            if (device.kind != "cpu" && device.kind != "memory" && device.kind != "gpu"
                && device.kind != "disk" && device.kind != "network") continue;
            const std::string label = "##resource_" + device.id;
            const bool active = activeDevice == device.id;
            if (ImGui::Selectable(label.c_str(), active, 0, ImVec2(0, cyui::dp(47.0f))))
                activeDevice = device.id;
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const auto& colors = cyui::palette();
            auto* draw = ImGui::GetWindowDrawList();
            if (active) draw->AddRectFilled(itemMin, ImVec2(itemMin.x + cyui::dp(3.0f), itemMax.y), colors.accent);
            const float left = itemMin.x + cyui::dp(10.0f);
            const float sparkWidth = cyui::dp(71.0f);
            const float sparkLeft = itemMax.x - sparkWidth - cyui::dp(8.0f);
            const float available = std::max(1.0f, sparkLeft - left - cyui::dp(5.0f));
            const auto title = ui_locale::text(device.title);
            cyui::clippedText(draw, ImVec2(left, itemMin.y + cyui::dp(5.0f)), available,
                active ? colors.accentText : colors.text, title.c_str());
            const auto summary = resourceSummary(device);
            const float summarySize = ImGui::GetFontSize() * 0.82f;
            draw->PushClipRect(ImVec2(left, itemMin.y), ImVec2(itemMax.x - cyui::dp(5.0f), itemMax.y), true);
            draw->AddText(ImGui::GetFont(), summarySize, ImVec2(left, itemMin.y + cyui::dp(26.0f)),
                active ? colors.accentText : colors.textDim, summary.c_str());
            draw->PopClipRect();
            const auto* primary = metric(device, device.kind == "cpu" ? "cpu" : device.kind == "memory" ? "used"
                : device.kind == "disk" ? "active" : device.kind == "network" ? "receive" : "usage");
            if (primary) {
                const auto found = histories.find(device.id + "/" + primary->id);
                if (found != histories.end() && found->second.size() > 1) {
                    const float top = itemMin.y + cyui::dp(8.0f);
                    const float bottom = itemMax.y - cyui::dp(8.0f);
                    double maximum = std::isfinite(primary->maximum) && primary->maximum > 0
                        ? primary->maximum : 1.0;
                    if (maximum <= 1.0)
                        for (const auto& point : found->second)
                            if (std::isfinite(point.value)) maximum = std::max(maximum, point.value * 1.15);
                    const auto& history = found->second;
                    ImVec2 previous {};
                    bool havePrevious = false;
                    for (std::size_t i = 0; i < history.size(); ++i) {
                        if (!std::isfinite(history[i].value)) { havePrevious = false; continue; }
                        const ImVec2 point(sparkLeft + sparkWidth * static_cast<float>(i)
                                / static_cast<float>(history.size() - 1),
                            bottom - (bottom - top) * static_cast<float>(std::clamp(history[i].value / maximum, 0.0, 1.0)));
                        if (havePrevious) draw->AddLine(previous, point, seriesColor(device, *primary), cyui::dp(1.1f));
                        previous = point;
                        havePrevious = true;
                    }
                }
            }
            if (ImGui::IsItemHovered()) cyui::tooltip(ui_locale::text(device.description).c_str());
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##resourceDetails", ImVec2(0, 0), ImGuiChildFlags_None);
        const DeviceInfo* chosen {};
        for (const auto& device : snapshot.devices)
            if (device.id == activeDevice && device.kind != "sensor" && device.kind != "sensor-status") {
                chosen = &device; break;
            }
        if (!chosen) for (const auto& device : snapshot.devices)
            if (device.kind == "cpu" || device.kind == "memory" || device.kind == "gpu"
                || device.kind == "disk" || device.kind == "network") {
                chosen = &device; activeDevice = device.id; break;
            }
        if (chosen) resourceDetails(*chosen);
        ImGui::EndChild();
    }
    void permissionControls(const wchar_t* page)
    {
        const bool isElevated = demoMode ? false : elevated();
        cyui::textDim(isElevated ? (ui_locale::french() ? "Administrateur" : "Administrator")
            : (ui_locale::french() ? "Droits standard" : "Standard permissions"));
        if (!isElevated && !demoMode) {
            ImGui::SameLine();
            if (cyui::button(ui_locale::french() ? "Redémarrer en administrateur" : "Restart as administrator",
                    cyui::Variant::secondary)) {
                if (restartElevated(page)) PostQuitMessage(0);
                else cyui::notify(ui_locale::french() ? "Élévation annulée ou indisponible."
                    : "Elevation cancelled or unavailable.", cyui::Tone::warning);
            }
        }
    }
    void gpuProcesses()
    {
        struct Row {
            std::string gpu, name, engine, local, shared;
            int pid {};
            const ProcessInfo* process {};
            std::string gpuKey, nameKey;
            double engineValue {NAN}, localBytes {NAN}, sharedBytes {NAN};
            double trustValue {NAN}, cpuValue {NAN}, memoryBytes {NAN};
        };
        std::vector<Row> rows;
        std::unordered_map<int, const ProcessInfo*> byPid;
        for (const auto& process : snapshot.processes) byPid[process.id] = &process;
        if (hardwareMonitor) {
            const auto hardware = hardwareMonitor->snapshot();
            if (hardware && std::chrono::system_clock::now() - hardware->timestamp <= std::chrono::seconds(3))
                for (const auto& gpu : hardware->section(cyhwinfo::ui::Page::gpu).devices)
                    for (const auto& table : gpu.tables) if (table.title == "GPU processes")
                        for (const auto& values : table.rows) {
                            if (values.size() < 4) continue;
                            const int pid = std::atoi(values[0].c_str());
                            if (pid <= 0) continue;
                            const auto found = byPid.find(pid);
                            const auto* process = found == byPid.end() ? nullptr : found->second;
                            rows.push_back({gpu.name, process ? process->name : "PID " + values[0],
                                values[1], values[2], values[3], pid, process});
                        }
        }
        if (rows.empty()) {
            for (const auto& process : snapshot.processes) {
                if (!(std::isfinite(process.gpu) && process.gpu > 0.0)
                    && !(std::isfinite(process.gpuLocalBytes) && process.gpuLocalBytes > 0.0)
                    && !(std::isfinite(process.gpuSharedBytes) && process.gpuSharedBytes > 0.0)) continue;
                rows.push_back({ui_locale::french() ? "Tous les GPU" : "All GPUs", process.name,
                    std::isfinite(process.gpu) ? format("%.1f %%", process.gpu) : "—",
                    std::isfinite(process.gpuLocalBytes)
                        ? format("%.1f MiB", process.gpuLocalBytes / 1048576.0) : "—",
                    std::isfinite(process.gpuSharedBytes)
                        ? format("%.1f MiB", process.gpuSharedBytes / 1048576.0) : "—",
                    process.id, &process});
            }
        }
        cyui::searchField("##gpuProcessFind", gpuProcessSearch, sizeof(gpuProcessSearch),
            ui_locale::french() ? "Nom du processus, PID ou GPU" : "Process name, PID or GPU",
            cyui::dp(300.0f));
        ImGui::SameLine();
        cyui::textDim("%s", ui_locale::french()
            ? "Cliquer sur un processus pour l'inspecter."
            : "Click a process to inspect it.");
        const auto query = lower(gpuProcessSearch);
        if (!query.empty()) std::erase_if(rows, [&](const Row& row) {
            return lower(row.name).find(query) == std::string::npos
                && lower(row.gpu).find(query) == std::string::npos
                && std::to_string(row.pid).find(query) == std::string::npos;
        });
        if (rows.empty()) {
            cyui::emptyState(ui_locale::french() ? "Aucun processus GPU" : "No GPU processes",
                ui_locale::french() ? "Aucune utilisation GPU mesurée pour le moment."
                    : "No GPU usage has been measured yet.", cyui::icon::view);
            return;
        }
        const auto parseAmount = [](const std::string& text) {
            char* end {};
            const double number = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || !std::isfinite(number))
                return std::numeric_limits<double>::quiet_NaN();
            const auto suffix = lower(end);
            if (suffix.find("gib") != std::string::npos) return number * 1073741824.0;
            if (suffix.find("mib") != std::string::npos) return number * 1048576.0;
            if (suffix.find("kib") != std::string::npos) return number * 1024.0;
            return number;
        };
        for (auto& row : rows) {
            row.gpuKey = lower(row.gpu);
            row.nameKey = lower(row.name);
            row.engineValue = parseAmount(row.engine);
            row.localBytes = parseAmount(row.local);
            row.sharedBytes = parseAmount(row.shared);
            if (!row.process) continue;
            const auto index = static_cast<std::size_t>(row.process - snapshot.processes.data());
            if (index < trustScores.size() && trustScores[index].score >= 0)
                row.trustValue = trustScores[index].score;
            row.cpuValue = row.process->cpu;
            row.memoryBytes = static_cast<double>(row.process->workingSet);
        }
        cyui::space(6);
        const float available = ImGui::GetContentRegionAvail().y;
        const bool inspectorVisible = showProcessInspector && available >= cyui::dp(340.0f);
        const float inspectorHeight = cyui::dp(compactProcesses ? 120.0f : 145.0f);
        const float tableHeight = inspectorVisible ? available - inspectorHeight - cyui::dp(5.0f) : available;
        if (ImGui::BeginTable("##gpuProcesses", 9,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                    ImGuiTableFlags_SizingStretchProp,
                ImVec2(0, tableHeight), std::max(ImGui::GetContentRegionAvail().x, cyui::dp(1160.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("GPU", 0, 0.15f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Processus" : "Process", 0, 0.18f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Confiance" : "Trust", 0, 0.11f);
            ImGui::TableSetupColumn("PID", 0, 0.06f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Moteur" : "Engine",
                ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 0.10f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Mémoire locale" : "Local memory", 0, 0.11f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Mémoire partagée" : "Shared memory", 0, 0.11f);
            ImGui::TableSetupColumn("CPU", 0, 0.08f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Mémoire" : "Memory", 0, 0.10f);
            ImGui::TableHeadersRow();
            if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
                const int column = specs->Specs[0].ColumnIndex;
                const bool descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                specs->SpecsDirty = false;
                const auto number = [column](const Row& row) {
                    switch (column) {
                    case 2: return row.trustValue;
                    case 3: return static_cast<double>(row.pid);
                    case 4: return row.engineValue;
                    case 5: return row.localBytes;
                    case 6: return row.sharedBytes;
                    case 7: return row.cpuValue;
                    case 8: return row.memoryBytes;
                    default: return std::numeric_limits<double>::quiet_NaN();
                    }
                };
                std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                    if (column >= 2) {
                        const double left = number(a), right = number(b);
                        const bool leftMissing = !std::isfinite(left), rightMissing = !std::isfinite(right);
                        if (leftMissing != rightMissing) return !leftMissing;
                        if (!leftMissing && left != right) return descending ? left > right : left < right;
                    } else {
                        const auto& left = column == 0 ? a.gpuKey : a.nameKey;
                        const auto& right = column == 0 ? b.gpuKey : b.nameKey;
                        if (left != right) return descending ? left > right : left < right;
                    }
                    if (a.nameKey != b.nameKey) return a.nameKey < b.nameKey;
                    if (a.pid != b.pid) return a.pid < b.pid;
                    return a.gpuKey < b.gpuKey;
                });
            }
            const float rowHeight = cyui::dp(compactProcesses ? 23.0f : 30.0f);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()), rowHeight);
            while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& row = rows[static_cast<std::size_t>(i)];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(row.gpu.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(row.gpu.c_str()); ImGui::PushID(row.pid);
                const bool active = row.process && selected.count(identity(*row.process)) != 0;
                if (active) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, cyui::palette().selection);
                if (ImGui::Selectable(row.name.c_str(), active, ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowHeight - cyui::dp(2.0f))) && row.process) {
                    selected.clear(); selected.insert(identity(*row.process));
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusProcess(*row.process);
                }
                if (row.process && ImGui::BeginPopupContextItem("##gpuProcessActions")) {
                    selected.clear(); selected.insert(identity(*row.process));
                    processActions(*row.process);
                    ImGui::EndPopup();
                }
                ImGui::PopID(); ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                if (row.process) {
                    const auto index = static_cast<std::size_t>(row.process - snapshot.processes.data());
                    if (index < trustScores.size()) trustBar(trustScores[index], cyui::dp(18.0f));
                    else ImGui::TextUnformatted("—");
                } else ImGui::TextUnformatted("—");
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", row.pid);
                const double gpuPercent = std::strtod(row.engine.c_str(), nullptr);
                ImGui::TableSetColumnIndex(4);
                usageBar(gpuPercent, row.engine, false, 18.0f);
                ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(row.local.c_str());
                ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(row.shared.c_str());
                ImGui::TableSetColumnIndex(7);
                if (row.process) usageBar(row.process->cpu, format("%.1f %%", row.process->cpu), false, 18.0f);
                else ImGui::TextUnformatted("—");
                ImGui::TableSetColumnIndex(8);
                if (row.process) {
                    const double memoryMb = row.process->workingSet / 1048576.0;
                    const double memoryPercent = snapshot.memoryTotalGb > 0.0
                        ? memoryMb / (snapshot.memoryTotalGb * 1024.0) * 100.0 : 0.0;
                    usageBar(memoryPercent, format("%.0f MB", memoryMb), true, 18.0f);
                } else ImGui::TextUnformatted("—");
            }
            ImGui::EndTable();
        }
        if (inspectorVisible) { cyui::space(2); processInspector(); }
    }
    void temperatures()
    {
        cyui::heading(ui_locale::french() ? "Températures" : "Temperatures", 2);
        ImGui::SameLine();
        cyui::alignRight(cyui::dp(310.0f));
        permissionControls(L"--temperatures");
        cyui::space(4);
        const char* views[] {ui_locale::french() ? "Capteurs" : "Sensors",
            ui_locale::french() ? "Courbes" : "Charts"};
        cyui::segmented("##sensorViews", views, 2, &sensorView, cyui::dp(280.0f));
        if (sensorView == 1) {
            ImGui::SameLine();
            ImGui::BeginDisabled(demoMode);
            if (cyui::button(ui_locale::french() ? "Importer un CSV HWiNFO" : "Import HWiNFO CSV", cyui::Variant::secondary))
                sensorLog.chooseFile();
            ImGui::EndDisabled();
            if (sensorLog.enabled()) {
                ImGui::SameLine();
                if (cyui::button(ui_locale::french() ? "Fermer le CSV" : "Close CSV", cyui::Variant::secondary))
                    sensorLog.clear();
                if (ImGui::GetTime() >= nextSensorRefresh) {
                    sensorLog.refresh(); nextSensorRefresh = ImGui::GetTime() + 1.0;
                }
            }
        }
        cyui::space(8);
        for (const auto& device : snapshot.devices) if (device.kind == "sensor-status") {
            const bool pawnio = device.description.find("PawnIO") != std::string::npos;
            if (sensorView == 1) cyui::callout(cyui::Tone::warning, pawnio
                ? (ui_locale::french()
                    ? "PawnIO absent ou inaccessible : certains capteurs avancés sont indisponibles. Les mesures natives restent actives."
                    : "PawnIO missing or inaccessible: some advanced sensors are unavailable. Native measurements remain active.")
                : (ui_locale::french()
                    ? "Capteurs avancés indisponibles. Les mesures natives restent actives."
                    : "Advanced sensors unavailable. Native measurements remain active."));
            if (pawnio && cyui::button(ui_locale::french() ? "Installer PawnIO (site officiel)"
                    : "Install PawnIO (official site)", cyui::Variant::secondary))
                ShellExecuteW(nullptr, L"open", L"https://pawnio.eu/", nullptr, nullptr, SW_SHOWNORMAL);
            if (pawnio) {
                ImGui::SameLine();
                cyui::textDim("%s", ui_locale::french()
                    ? "Choisir l'édition officielle signée ; validation Windows possible."
                    : "Choose the signed Official edition; Windows approval may be required.");
            }
        }
        if (sensorView == 0) {
            cyui::searchField("##sensorFind", sensorSearch, sizeof(sensorSearch),
                ui_locale::french() ? "Rechercher un capteur" : "Find a sensor", cyui::dp(260.0f));
            ImGui::SameLine();
            ImGui::Checkbox(ui_locale::french() ? "Disponibles uniquement" : "Available only", &sensorAvailableOnly);
            ImGui::SameLine();
            if (cyui::button(ui_locale::french() ? "Réinitialiser les statistiques" : "Reset statistics",
                    cyui::Variant::secondary) && hardwareMonitor)
                hardwareMonitor->reset_sensor_statistics();
            cyui::space(5);
            const auto hardware = hardwareMonitor ? hardwareMonitor->snapshot() : nullptr;
            if (!hardware || hardware->timestamp == cyhwinfo::Timestamp{}) {
                cyui::textDim("%s", ui_locale::french()
                    ? "Chargement des capteurs…" : "Loading sensors…");
            } else if (sensorSearch[0] || sensorAvailableOnly) {
                auto filtered = *hardware;
                const auto query = lower(sensorSearch);
                std::erase_if(filtered.sensor_groups, [&](cyhwinfo::ui::SensorGroup& group) {
                    const bool groupMatch = lower(group.name).find(query) != std::string::npos;
                    std::erase_if(group.rows, [&](const cyhwinfo::ui::SensorRow& row) {
                        const bool unavailable = row.reading.status != cyhwinfo::Status::available
                            || !row.reading.value.has_value();
                        return (sensorAvailableOnly && unavailable)
                            || (!groupMatch && lower(row.label).find(query) == std::string::npos);
                    });
                    return group.rows.empty();
                });
                hardwareDashboard.draw_panel(cyhwinfo::ui::Page::sensors, filtered);
            } else hardwareDashboard.draw_panel(cyhwinfo::ui::Page::sensors, *hardware);
            return;
        }
        const DeviceInfo* selectedDevice {};
        const MetricInfo* selectedMetric {};
        const auto availableWidth = ImGui::GetContentRegionAvail().x;
        const auto tableWidth = std::min(availableWidth * 0.57f,
            std::max(availableWidth * 0.48f, cyui::dp(400.0f)));
        bool selectedStillAvailable = false;
        for (const auto& device : snapshot.devices)
            for (const auto& value : device.metrics)
                if ((device.kind == "sensor" || value.unit == "°C") && std::isfinite(value.value)
                    && activeSensorKey == device.id + "/" + value.id) selectedStillAvailable = true;
        if (!selectedStillAvailable) activeSensorKey.clear();
        if (ImGui::BeginTable("##temperatureLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##sensors", ImGuiTableColumnFlags_WidthFixed, tableWidth);
            ImGui::TableSetupColumn("##sensorDetail", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            if (ImGui::BeginTable("##liveTemperatures", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0, ImGui::GetContentRegionAvail().y))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(ui_locale::french() ? "Capteur" : "Sensor", 0, 0.48f);
                ImGui::TableSetupColumn(ui_locale::french() ? "Actuel" : "Current", 0, 0.18f);
                ImGui::TableSetupColumn(ui_locale::french() ? "Min" : "Min", 0, 0.17f);
                ImGui::TableSetupColumn(ui_locale::french() ? "Max" : "Max", 0, 0.17f);
                ImGui::TableHeadersRow();
                for (const auto& device : snapshot.devices) {
                    bool groupShown = false;
                    for (const auto& value : device.metrics) {
                        if ((device.kind != "sensor" && value.unit != "°C") || !std::isfinite(value.value)) continue;
                        const auto key = device.id + "/" + value.id;
                        const auto reading = liveTemperatures.find(key);
                        if (reading == liveTemperatures.end()) continue;
                        if (activeSensorKey.empty()) activeSensorKey = key;
                        if (key == activeSensorKey) { selectedDevice = &device; selectedMetric = &value; }
                        if (!groupShown) {
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(ui_locale::text(device.title).c_str());
                            groupShown = true;
                        }
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        if (ImGui::Selectable((ui_locale::text(value.label) + "##" + key).c_str(),
                                activeSensorKey == key, ImGuiSelectableFlags_SpanAllColumns))
                            activeSensorKey = key;
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f %s", reading->second.current, value.unit.c_str());
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", reading->second.minimum);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", reading->second.maximum);
                    }
                }
                if (sensorLog.enabled()) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("HWiNFO CSV");
                    for (const auto& reading : sensorLog.readings()) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(reading.name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f °C", reading.current);
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", reading.minimum);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f", reading.maximum);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TableNextColumn();
            if (selectedDevice && selectedMetric) {
                cyui::heading(ui_locale::text(selectedMetric->label).c_str(), 3);
                cyui::textDim("%s", ui_locale::text(selectedDevice->title).c_str());
                const auto& reading = liveTemperatures.at(activeSensorKey);
                cyui::textColoured(cyui::palette().warning, "%.1f %s", reading.current, reading.unit.c_str());
                cyui::textDim("Min %.1f %s   ·   Max %.1f %s", reading.minimum, reading.unit.c_str(),
                    reading.maximum, reading.unit.c_str());
                cyui::space(8);
                chart(*selectedDevice, {selectedMetric}, 255.0f);
                cyui::space(12);
                cyui::heading(ui_locale::french() ? "Autres mesures" : "Related readings", 4);
                if (ImGui::BeginTable("##relatedSensors", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Name", 0, 0.7f);
                    ImGui::TableSetupColumn("Value", 0, 0.3f);
                    for (const auto& value : selectedDevice->metrics) {
                        if (value.id == selectedMetric->id || !std::isfinite(value.value)) continue;
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        ImGui::TextUnformatted(ui_locale::text(value.label).c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.1f %s", value.value, value.unit.c_str());
                    }
                    ImGui::EndTable();
                }
            } else {
                cyui::emptyState(ui_locale::french() ? "Capteurs en attente" : "Waiting for sensors",
                    ui_locale::french() ? "La détection matérielle démarre avec cette page."
                        : "Hardware discovery starts with this page.", cyui::icon::refresh);
            }
            if (!sensorLog.error().empty()) cyui::textDim("%s", sensorLog.error().c_str());
            ImGui::EndTable();
        }
    }
    void systemInfo()
    {
        cyui::panelHeader(ui_locale::french() ? "Informations système" : "System information",
            ui_locale::french() ? "Résumé matériel et version de Windows" : "Hardware overview and Windows version");
        permissionControls(L"--system-info");
        cyui::space(10);
        const auto unknown = ui_locale::french() ? "Indisponible" : "Unavailable";
        const auto field = [&](const char* label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); cyui::textDim("%s", label);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(value.empty() ? unknown : value.c_str());
        };
        const auto reg = [&](const wchar_t* key, const wchar_t* name) {
            return demoMode ? std::string() : registryValue(HKEY_LOCAL_MACHINE, key, name);
        };
        if (ImGui::BeginTable("##systemInfo", 2,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(ui_locale::french() ? "Élément" : "Item", 0, 0.25f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Valeur" : "Value", 0, 0.75f);
            ImGui::TableHeadersRow();
            field(ui_locale::french() ? "Système" : "OS", demoMode ? "Windows Demo Edition" :
                reg(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName"));
            field(ui_locale::french() ? "Version" : "Version", demoMode ? "Demo 1" :
                reg(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion"));
            field(ui_locale::french() ? "Carte mère" : "Motherboard", demoMode ? "Demo Board X1" :
                reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardManufacturer") + " " +
                reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct"));
            field("BIOS", demoMode ? "Demo BIOS" :
                reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSVersion"));
            field(ui_locale::french() ? "Date du BIOS" : "BIOS date", demoMode ? "2025-01-01" :
                reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSReleaseDate"));
            field(ui_locale::french() ? "Architecture" : "Architecture", "x64");
            field(ui_locale::french() ? "Processeurs logiques" : "Logical processors",
                demoMode ? "8" : std::to_string(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
            for (const auto& device : snapshot.devices) {
                if (device.kind == "cpu" || device.kind == "gpu" || device.kind == "disk")
                    field(device.kind == "cpu" ? "CPU" : device.kind == "gpu" ? "GPU" :
                        (ui_locale::french() ? "Disque" : "Disk"), ui_locale::text(device.description));
                if (device.kind == "gpu") {
                    const auto* vram = metric(device, "dedicated");
                    if (vram && std::isfinite(vram->maximum))
                        field("VRAM", format("%.1f", vram->maximum) +
                            (ui_locale::french() ? " Go" : " GB"));
                }
            }
            field("RAM", snapshot.memoryTotalGb > 0
                ? format("%.1f", snapshot.memoryTotalGb) + (ui_locale::french() ? " Go" : " GB") : "");
            ImGui::EndTable();
        }
        cyui::space(10);
        cyui::heading(ui_locale::french() ? "Matériel détaillé" : "Detailed hardware", 3);
        if (!demoMode && !inventory) inventory = std::make_unique<HardwareInventory>();
        if (inventory) inventory->poll();
        const std::vector<HardwareField> demoFields {
            {"CPU 0", "Model", "Demo 8-Core Processor"},
            {"CPU 0", "Physical cores", "8"}, {"CPU 0", "Logical cores", "16"},
            {"CPU 0", "L3 cache", "32.0 MiB"},
            {"Motherboard", "Model", "Demo Board X1"},
            {"Memory", "Installed", "32.0 GiB"},
            {"Memory module 0", "Capacity", "16.0 GiB"},
            {"Memory module 0", "Type", "DDR5"},
            {"GPU 0", "Dedicated memory", "12.0 GiB"},
            {"GPU 0", "Memory bus", "256 bits"},
            {"GPU 0", "Power limit", "220.0 W"}
        };
        const auto& fields = demoMode ? demoFields : inventory->fields();
        if (!demoMode && !inventory->ready()) cyui::textDim(ui_locale::french()
            ? "Lecture de l'inventaire matériel…" : "Reading hardware inventory…");
        else if (ImGui::BeginTable("##hardwareInventory", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(ui_locale::french() ? "Groupe" : "Group", 0, 0.18f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Mesure" : "Metric", 0, 0.30f);
            ImGui::TableSetupColumn(ui_locale::french() ? "Valeur" : "Value", 0, 0.52f);
            ImGui::TableHeadersRow();
            for (const auto& item : fields) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); cyui::textDim("%s", item.group.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(item.label.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(item.value.c_str());
            }
            ImGui::EndTable();
        }
    }
    void dialogs()
    {
        if (cyui::beginModal(ui_locale::tr("Arrêter les tâches ?###stopModal"), &stopModal, 430.0f)) {
            cyui::callout(cyui::Tone::warning, ui_locale::tr("Les modifications non enregistrées peuvent être perdues."));
            cyui::space(8);
            cyui::textDim(ui_locale::tr("%zu processus sélectionné(s)"), pendingStop.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(8, pendingStop.size()); ++i)
                if (const auto* process = find(pendingStop[i])) ImGui::BulletText("%s · PID %d", process->name.c_str(), process->id);
            if (pendingStop.size() > 8) cyui::textFaint(ui_locale::tr("... et %zu autres"), pendingStop.size() - 8);
            const int action = cyui::modalButtons(ui_locale::tr("Arrêter"), ui_locale::tr("Annuler"), true);
            if (action == 1) {
                for (const auto& key : pendingStop)
                    bridge.send("E " + std::to_string(nextRequest++) + " " + std::to_string(key.id)
                        + " " + std::to_string(key.created) + "\n");
                stopModal = false;
            } else if (action == 2) stopModal = false;
            cyui::endModal();
        }
        if (detailsOpen) {
            ImGui::SetNextWindowSize(cyui::dp(780.0f, 580.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(ui_locale::tr("Informations avancées###advanced"), &detailsOpen)) {
                if (cyui::button(ui_locale::tr("Copier tout"), cyui::Variant::secondary)) ImGui::SetClipboardText(detailText.c_str());
                ImGui::Separator();
                ImGui::BeginChild("##report");
                ImGui::TextUnformatted(detailText.c_str());
                ImGui::EndChild();
            }
            ImGui::End();
        }
    }
    void detachedWindows()
    {
        bool anyOpen {};
        for (auto& [id, open] : detached) {
            if (!open) continue;
            const std::string title = id == "all" ? std::string(ui_locale::tr("Graphiques CPU, RAM et GPU###detached_all"))
                : std::string(ui_locale::tr("Graphique ")) + id + "###detached_" + id;
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowSize(cyui::dp(id == "all" ? 970.0f : 650.0f, id == "all" ? 650.0f : 420.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x - cyui::dp(60.0f), main->Pos.y + cyui::dp(55.0f)), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoDocking)) {
                ImGui::BeginChild(("##popoutScroll" + id).c_str(), ImVec2(0, 0));
                dashboard(id == "all" ? "" : id);
                ImGui::EndChild();
            }
            ImGui::End();
            anyOpen |= open;
        }
        if (!anyOpen && (assistantPage || consolePage) && !detached.empty()) bridge.send("M 0\n");
        for (auto it = detached.begin(); it != detached.end();)
            it = it->second ? std::next(it) : detached.erase(it);
        updateHardwareActivity();
    }
    void assistant()
    {
        if (!assistantPane) assistantPane = std::make_unique<AssistantPane>(demoMode, uiBridge);
        assistantPane->draw();
    }
};

AppUi::AppUi(ID3D11Device* device, const bool demoMode, const bool performanceAtStart,
             const bool assistantAtStart, const int mcpLocalPort, const int utilityAtStart,
             const bool performanceDetailsAtStart)
    : state_(new State(device, demoMode, performanceAtStart, assistantAtStart, mcpLocalPort,
        utilityAtStart, performanceDetailsAtStart)) {}
AppUi::~AppUi() { delete state_; }
void AppUi::frame()
{
    auto& state = *state_;
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) ui_scale::adjust(10);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) ui_scale::adjust(-10);
        if (ImGui::IsKeyPressed(ImGuiKey_0)) ui_scale::adjust(115 - ui_scale::percent());
        if (ImGui::IsKeyPressed(ImGuiKey_1)) state.switchPage(false);
        if (ImGui::IsKeyPressed(ImGuiKey_2)) state.switchPage(true);
        if (ImGui::IsKeyPressed(ImGuiKey_3)) state.switchAssistant();
        if (ImGui::IsKeyPressed(ImGuiKey_4)) state.switchUtility(3);
        if (ImGui::IsKeyPressed(ImGuiKey_5)) state.switchUtility(4);
        if (ImGui::IsKeyPressed(ImGuiKey_6)) state.switchUtility(5);
    }
    state.icons.beginFrame();
    state.receive();
    state.uiBridge->drain([&state](const std::string& operation, const Json& args, bool unrestricted) {
        if (!unrestricted && (!state.assistantPane || !state.assistantPane->allows(operation)))
            throw Error("AccessDenied", "UI tool permission was revoked");
        return state.applyUiCommand(operation, args);
    });
    state.topBar();
    state.statusBar();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##main", nullptr, flags)) {
        if (state.assistantPage) state.assistant();
        else if (state.performancePage) state.performance();
        else if (state.consolePage) {
            if (!state.consolePane) state.consolePane = std::make_unique<ConsolePane>();
            state.consolePane->draw(state.demoMode);
        }
        else if (state.temperaturePage) state.temperatures();
        else if (state.systemPage) state.systemInfo();
        else state.processes();
    }
    ImGui::End();
    state.dialogs();
    if (state.assistantPane) state.assistantPane->drawConfirmation();
    state.detachedWindows();
}

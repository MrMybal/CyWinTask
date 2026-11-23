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
#include "UiKit.h"

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
#include <cstdarg>
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
std::string textf(const char* pattern, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(buffer, sizeof(buffer), pattern, args);
    va_end(args);
    return buffer;
}
using ui_kit::pick;
class IconCache {
    ID3D11Device* device_ {};
    std::unordered_map<std::string, ID3D11ShaderResourceView*> cache_;
    int budget_ {};
public:
    explicit IconCache(ID3D11Device* device) : device_(device) {}
    ~IconCache() { for (auto& item : cache_) if (item.second) item.second->Release(); }
    // Purges only between frames: views already queued in this frame's draw
    // lists must stay alive until the frame is rendered.
    void beginFrame()
    {
        budget_ = 3;
        if (cache_.size() < 2048) return;
        for (auto& item : cache_) if (item.second) item.second->Release();
        cache_.clear();
    }
    ID3D11ShaderResourceView* get(const std::string& path)
    {
        if (path.empty()) return nullptr;
        if (const auto found = cache_.find(path); found != cache_.end()) return found->second;
        if (budget_-- <= 0) return nullptr;
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
    float sidebarFooterHeight {};
    std::unordered_set<int> detailRequests;
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
            if (detailRequests.erase(reply.requestId)) {
                // Advanced reports go to their window, never to a toast or the status bar.
                detailText = reply.text;
                detailsOpen = true;
                if (!reply.success) cyui::notify(pick("Informations avancées indisponibles.",
                    "Advanced information unavailable."), cyui::Tone::error);
            } else if (reply.success) {
                statusMessage = reply.text;
                cyui::notify(reply.text.c_str(), cyui::Tone::success);
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
        detailRequests.insert(nextRequest);
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
    // ------------------------------------------------------------------ shell
    struct LiveSummary {
        double cpu {NAN}, memoryUsed {NAN}, memoryTotal {NAN}, gpu {NAN}, vramUsed {}, vramTotal {};
    };
    LiveSummary liveSummary() const
    {
        LiveSummary summary;
        if (!hasSnapshot) return summary;
        summary.cpu = snapshot.cpu;
        summary.memoryUsed = snapshot.memoryUsedGb;
        summary.memoryTotal = snapshot.memoryTotalGb;
        for (const auto& device : snapshot.devices) if (device.kind == "gpu") {
            if (const auto* usage = metric(device, "usage"); usage && std::isfinite(usage->value))
                summary.gpu = std::isfinite(summary.gpu) ? std::max(summary.gpu, usage->value) : usage->value;
            if (const auto* vram = metric(device, "dedicated"); vram && std::isfinite(vram->value)
                && std::isfinite(vram->maximum) && vram->maximum > 0.0) {
                summary.vramUsed += vram->value;
                summary.vramTotal += vram->maximum;
            }
        }
        return summary;
    }
    void goTo(const int page)
    {
        if (page == 2) switchAssistant();
        else if (page >= 3) switchUtility(page);
        else switchPage(page == 1);
    }
    void togglePause()
    {
        if (demoMode) return;
        paused = !paused;
        bridge.send(paused ? "P 1\n" : "P 0\n");
        updateHardwareActivity();
    }
    void setLanguage(const bool french)
    {
        ui_locale::setFrench(french);
        cyui::setLocale(french ? "fr" : "en");
        dirty = true;
        if (auto* window = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw))
            SetWindowTextW(window, french
                ? L"CyWinTask 0.11.0 — Gestionnaire des tâches" : L"CyWinTask 0.11.0 — Task Manager");
    }
    void zoomControls(const float width)
    {
        const float button = ImGui::GetFrameHeight();
        if (cyui::iconButton(cyui::icon::zoomOut, pick("Réduire le texte (Ctrl+-)", "Smaller text (Ctrl+-)"),
                false, 0, ImVec2(button, button)))
            ui_scale::adjust(-10);
        ImGui::SameLine(0.0f, 0.0f);
        const float middle = std::max(cyui::dp(34.0f), width - button * 2.0f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##zoomReset", ImVec2(middle, button))) ui_scale::adjust(115 - ui_scale::percent());
        const bool hovered = ImGui::IsItemHovered();
        cyui::tooltip(pick("Taille par défaut (Ctrl+0)", "Default size (Ctrl+0)"));
        const auto label = std::to_string(ui_scale::percent()) + " %";
        cyui::centredText(ImGui::GetWindowDrawList(), at, ImVec2(at.x + middle, at.y + button),
            hovered ? cyui::palette().text : cyui::palette().textDim, label.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        if (cyui::iconButton(cyui::icon::zoomIn, pick("Agrandir le texte (Ctrl++)", "Larger text (Ctrl++)"),
                false, 0, ImVec2(button, button)))
            ui_scale::adjust(10);
    }
    void languageCombo(const float width)
    {
        const char* languages[] {"English", "Français"};
        int language = ui_locale::french() ? 1 : 0;
        if (cyui::combo("##language", &language, languages, 2, width)) setLanguage(language == 1);
    }
    void sidebar()
    {
        struct NavEntry { int page; const char* icon; const char* french; const char* english; const char* keys; };
        const NavEntry monitor[] {
            {0, cyui::icon::list, "Processus", "Processes", "Ctrl+1"},
            {1, cyui::icon::equalizer, "Performance", "Performance", "Ctrl+2"},
            {4, cyui::icon::brightness, "Températures", "Temperatures", "Ctrl+5"},
            {5, cyui::icon::info, "Système", "System", "Ctrl+6"}};
        const NavEntry tools[] {
            {2, cyui::icon::lightbulb, "Assistant", "Assistant", "Ctrl+3"},
            {3, cyui::icon::code, "Console", "Console", "Ctrl+4"}};
        const bool narrow = ImGui::GetMainViewport()->Size.x < cyui::dp(1080.0f);
        if (cyui::beginSidebar("##navigation", narrow ? 58.0f : 204.0f)) {
            const int page = activePage();
            const std::string count = hasSnapshot ? std::to_string(snapshot.processes.size()) : std::string();
            const auto entry = [&](const NavEntry& item) {
                const char* label = pick(item.french, item.english);
                if (narrow) {
                    const std::string tip = std::string(label) + " (" + item.keys + ")";
                    if (ui_kit::navIcon(item.icon, tip.c_str(), page == item.page)) goTo(item.page);
                } else if (cyui::sidebarItem(item.icon, label, page == item.page,
                               item.page == 0 && !count.empty() ? count.c_str() : nullptr)) goTo(item.page);
            };
            if (narrow) {
                cyui::ScopedFont font(cyui::boldFont(), 1.05f);
                const float width = ImGui::GetContentRegionAvail().x;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                cyui::centredText(ImGui::GetWindowDrawList(), at, ImVec2(at.x + width, at.y + ImGui::GetFontSize()),
                    cyui::palette().accent, "Cy");
                ImGui::Dummy(ImVec2(width, ImGui::GetFontSize()));
                cyui::space(12.0f);
                for (const auto& item : monitor) entry(item);
                cyui::divider(6.0f);
                for (const auto& item : tools) entry(item);
            } else {
                cyui::sidebarHeader("CyWinTask", demoMode ? ui_locale::tr("DÉMO") : nullptr);
                cyui::sidebarGroup(pick("SURVEILLANCE", "MONITORING"));
                for (const auto& item : monitor) entry(item);
                cyui::sidebarGroup(pick("OUTILS", "TOOLS"));
                for (const auto& item : tools) entry(item);
            }
            sidebarFooter(narrow);
        }
        cyui::endSidebar();
    }
    void sidebarFooter(const bool narrow)
    {
        const auto& colors = cyui::palette();
        const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + cyui::dp(12.0f), bottom - sidebarFooterHeight));
        const float start = ImGui::GetCursorPosY();
        const float width = ImGui::GetContentRegionAvail().x;
        if (narrow) {
            {
                cyui::ScopedDisabled disabled(demoMode);
                if (cyui::iconButton(paused ? cyui::icon::play : cyui::icon::pause,
                        paused ? ui_locale::tr("Reprendre") : ui_locale::tr("Pause"), paused, 0, ImVec2(width, 0)))
                    togglePause();
            }
            if (cyui::iconButton(cyui::icon::settings, pick("Affichage et langue", "Display and language"),
                    false, 0, ImVec2(width, 0)))
                ImGui::OpenPopup("##displayOptions");
            if (ImGui::BeginPopup("##displayOptions")) {
                cyui::sectionTitle(pick("Taille du texte", "Text size"));
                zoomControls(cyui::dp(150.0f));
                cyui::space(4.0f);
                cyui::sectionTitle(pick("Langue", "Language"));
                languageCombo(cyui::dp(150.0f));
                ImGui::EndPopup();
            }
        } else {
            const auto summary = liveSummary();
            const auto value = [](const char* pattern, const double number) {
                return std::isfinite(number) ? format(pattern, number) : std::string("—");
            };
            cyui::overline(pick("EN DIRECT", "LIVE"), colors.textFaint);
            cyui::space(3.0f);
            const double cpu = summary.cpu / 100.0;
            ui_kit::liveMeter("CPU", value("%.0f %%", summary.cpu).c_str(), cpu, ui_kit::loadColour(cpu));
            const double memory = summary.memoryTotal > 0.0 ? summary.memoryUsed / summary.memoryTotal : NAN;
            const std::string memoryText = std::isfinite(memory)
                ? textf("%.1f / %.0f %s", summary.memoryUsed, summary.memoryTotal, pick("Go", "GB")) : "—";
            ui_kit::liveMeter(ui_locale::tr("Mémoire"), memoryText.c_str(), memory, ui_kit::loadColour(memory));
            const double gpu = summary.gpu / 100.0;
            ui_kit::liveMeter("GPU", value("%.0f %%", summary.gpu).c_str(), gpu, ui_kit::loadColour(gpu));
            const double vram = summary.vramTotal > 0.0 ? summary.vramUsed / summary.vramTotal : NAN;
            const std::string vramText = std::isfinite(vram)
                ? textf("%.1f / %.0f %s", summary.vramUsed, summary.vramTotal, pick("Go", "GB")) : "—";
            ui_kit::liveMeter("VRAM", vramText.c_str(), vram, ui_kit::loadColour(vram));
            cyui::divider(6.0f);
            {
                cyui::ScopedDisabled disabled(demoMode);
                if (cyui::iconTextButton(paused ? cyui::icon::play : cyui::icon::pause,
                        paused ? ui_locale::tr("Reprendre") : pick("Mettre en pause", "Pause updates"),
                        paused ? cyui::Variant::primary : cyui::Variant::secondary, ImVec2(width, 0)))
                    togglePause();
            }
            cyui::tooltip(ui_locale::tr("Suspendre ou reprendre la collecte"));
            const float zoomWidth = ImGui::GetFrameHeight() * 2.0f + cyui::dp(42.0f);
            zoomControls(zoomWidth);
            ImGui::SameLine(0.0f, cyui::dp(6.0f));
            languageCombo(ImGui::GetContentRegionAvail().x);
        }
        sidebarFooterHeight = ImGui::GetCursorPosY() - start;
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
            const char* keys = pick("Ctrl+1–6 pages · Ctrl+F recherche · Ctrl +/− taille du texte",
                "Ctrl+1–6 pages · Ctrl+F search · Ctrl +/− text size");
            float keysWidth {};
            {
                cyui::ScopedFont font(nullptr, 0.86f);
                keysWidth = ImGui::CalcTextSize(keys).x;
            }
            ImGui::SameLine();
            cyui::alignRight(keysWidth);
            cyui::statusText(cyui::Tone::neutral, "%s", keys);
        }
        cyui::endStatusBar();
    }

    // -------------------------------------------------------------- processes
    template <typename Content>
    void withInspector(const char* id, Content&& content)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float gap = cyui::dp(10.0f);
        const float sideWidth = std::clamp(available.x * 0.25f, cyui::dp(260.0f), cyui::dp(330.0f));
        const bool side = showProcessInspector && available.x - sideWidth - gap >= cyui::dp(600.0f);
        const float bottomHeight = cyui::dp(compactProcesses ? 178.0f : 196.0f);
        const bool bottom = showProcessInspector && !side && available.y >= bottomHeight + cyui::dp(220.0f);
        const ImVec2 area(side ? available.x - sideWidth - gap : available.x,
            bottom ? available.y - bottomHeight - gap : available.y);
        ImGui::BeginChild(id, area, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        content(ImGui::GetContentRegionAvail());
        ImGui::EndChild();
        if (side) {
            ImGui::SameLine(0.0f, gap);
            processInspector(available.y, false);
        } else if (bottom) {
            ImGui::Dummy(ImVec2(0.0f, gap - ImGui::GetStyle().ItemSpacing.y * 2.0f));
            processInspector(bottomHeight, true);
        }
    }
    void processes()
    {
        std::size_t applications {};
        for (const auto& process : snapshot.processes) if (process.category == 0) ++applications;
        const std::string subtitle = hasSnapshot
            ? textf(pick("%zu processus · %zu applications", "%zu processes · %zu applications"),
                snapshot.processes.size(), applications)
            : std::string(ui_locale::tr("Connexion au collecteur..."));
        const float viewsWidth = cyui::dp(300.0f);
        ui_kit::beginPageHeader(ui_locale::tr("Processus"), subtitle.c_str(), viewsWidth);
        const char* processViews[] {pick("Tous les processus", "All processes"), pick("Processus GPU", "GPU processes")};
        cyui::alignRight(viewsWidth);
        if (cyui::segmented("##processViews", processViews, 2, &processView, viewsWidth))
            updateHardwareActivity();
        ui_kit::endPageHeader();
        if (hasSnapshot && !demoMode && ImGui::GetTime() >= nextTrustRefresh) {
            if (refreshTrustScores()) dirty = true;
            nextTrustRefresh = ImGui::GetTime() + 0.75;
        }
        if (processView == 1) { gpuProcesses(); return; }
        processToolbar();
        cyui::space(8.0f);
        if (hasSnapshot) buildRows();
        withInspector("##processArea", [this](const ImVec2 size) {
            if (!hasSnapshot) cyui::emptyState(ui_locale::tr("En attente des processus"), ui_locale::tr("Le collecteur démarre."), cyui::icon::refresh);
            else if (display.empty()) cyui::emptyState(ui_locale::tr("Aucun processus trouvé"), ui_locale::tr("Modifiez la recherche ou le filtre."), cyui::icon::search);
            else processTable(size);
        });
    }
    void processToolbar()
    {
        if (!cyui::beginToolbar("##processToolbar")) { cyui::endToolbar(); return; }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) focusSearch = true;
        const auto& style = ImGui::GetStyle();
        const float icon = ImGui::GetFrameHeight() + cyui::dp(2.0f);
        const std::string endLabel = selected.size() > 1
            ? ui_locale::tr("Arrêter ") + std::to_string(selected.size()) + ui_locale::tr(" tâches")
            : std::string(ui_locale::tr("Arrêter la tâche"));
        const char* detailsLabel = pick("Détails", "Details");
        const float detailsWidth = cyui::buttonWidth(detailsLabel) + ImGui::GetFontSize() * 1.8f;
        const float actionsWidth = detailsWidth + cyui::buttonWidth(endLabel.c_str()) + style.ItemSpacing.x;
        const float comboWidth = cyui::dp(190.0f);
        const float fixed = comboWidth + icon * 4.0f + actionsWidth + style.ItemSpacing.x * 6.0f + cyui::dp(40.0f);
        // Too narrow for one row: the actions move to a second toolbar.
        const bool twoRows = ImGui::GetContentRegionAvail().x - fixed < cyui::dp(200.0f);
        const float searchWidth = std::clamp(ImGui::GetContentRegionAvail().x - fixed + (twoRows ? actionsWidth : 0.0f),
            cyui::dp(150.0f), cyui::dp(520.0f));
        if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
        if (cyui::searchField("##search", search, sizeof(search), ui_locale::tr("Nom, PID ou pid:1234..."), searchWidth))
            dirty = true;
        cyui::tooltip(ui_locale::tr("Saisissez pid:1234 pour isoler un processus précis."));
        ImGui::SameLine();
        const char* filters[] {pick("Tous les types", "All types"), ui_locale::tr("Applications"),
            ui_locale::tr("Arrière-plan"), ui_locale::tr("Processus Windows")};
        if (cyui::combo("##categoryFilter", &categoryFilter, filters, 4, comboWidth)) dirty = true;
        cyui::toolbarSeparator();
        if (cyui::iconButton(cyui::icon::link, pick("Processus associés : regrouper les processus enfants sous leur parent",
                "Related processes: nest child processes under their parent"), treeMode)) {
            treeMode = !treeMode; dirty = true;
        }
        ImGui::SameLine(0.0f, cyui::dp(2.0f));
        if (cyui::iconButton(cyui::icon::tiles, pick("Regrouper par type (applications, arrière-plan, Windows)",
                "Group by type (apps, background, Windows)"), groupByType)) {
            groupByType = !groupByType; dirty = true;
        }
        ImGui::SameLine(0.0f, cyui::dp(2.0f));
        if (cyui::iconButton(cyui::icon::list, pick("Lignes compactes", "Compact rows"), compactProcesses))
            compactProcesses = !compactProcesses;
        ImGui::SameLine(0.0f, cyui::dp(2.0f));
        if (cyui::iconButton(cyui::icon::dockRight, pick("Panneau d'inspection", "Inspector panel"), showProcessInspector))
            showProcessInspector = !showProcessInspector;
        if (twoRows) {
            cyui::endToolbar();
            cyui::space(2.0f);
            if (!cyui::beginToolbar("##processActions")) { cyui::endToolbar(); return; }
        } else {
            ImGui::SameLine();
            cyui::alignRight(actionsWidth);
        }
        {
            cyui::ScopedDisabled disabled(selected.size() != 1);
            if (cyui::iconTextButton(cyui::icon::info, detailsLabel) && selected.size() == 1)
                if (const auto* process = find(*selected.begin())) requestDetails(*process);
        }
        cyui::tooltip(pick("Informations avancées du processus sélectionné (double-clic)",
            "Advanced information for the selected process (double-click)"));
        ImGui::SameLine();
        {
            cyui::ScopedDisabled disabled(demoMode || selected.empty());
            if (cyui::dangerButton(endLabel.c_str())) askStop();
        }
        cyui::endToolbar();
    }
    // Draws `text` right-aligned in the current table cell at `y`.
    static void cellText(ImDrawList* draw, const float y, const ImU32 colour, const std::string& text, const bool right = true)
    {
        const float x = ImGui::GetCursorScreenPos().x;
        const float width = ImGui::GetContentRegionAvail().x;
        const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
        draw->AddText(ImVec2(right ? x + std::max(0.0f, width - textWidth) : x, y), colour, text.c_str());
    }
    static std::string memoryText(const double bytes)
    {
        if (!std::isfinite(bytes)) return "—";
        const double megabytes = bytes / 1048576.0;
        if (megabytes >= 10240.0) return textf("%.1f %s", megabytes / 1024.0, pick("Go", "GB"));
        return textf(megabytes < 100.0 ? "%.1f %s" : "%.0f %s", megabytes, pick("Mo", "MB"));
    }
    void trustCell(ImDrawList* draw, const TrustResult& score, const float top, const float rowHeight, const float textY)
    {
        const auto& palette = cyui::palette();
        const ImVec2 cell = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        if (score.score >= 0) {
            const float radius = cyui::dp(4.0f);
            draw->AddCircleFilled(ImVec2(cell.x + radius, top + rowHeight * 0.5f), radius, ui_kit::trustColour(score.score));
            const auto label = std::to_string(score.score);
            draw->AddText(ImVec2(cell.x + radius * 2.0f + cyui::dp(7.0f), textY), palette.text, label.c_str());
        } else draw->AddText(ImVec2(cell.x, textY), palette.textFaint, score.state == TrustState::pending ? "…" : "—");
        if (ui_kit::hovering(ImVec2(cell.x, top), ImVec2(cell.x + width, top + rowHeight)))
            ImGui::SetTooltip("%s\n%s\n%s", ui_locale::text(score.label).c_str(),
                ui_locale::text(score.explanation).c_str(), ui_locale::tr("Indice local, pas une analyse antivirus."));
    }
    void processTable(const ImVec2 size)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH
            | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoBordersInBodyUntilResize
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
            | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_SizingFixedFit;
        const float padY = cyui::dp(5.0f);
        cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, ImVec2(cyui::dp(6.0f), padY));
        const float innerWidth = size.x >= cyui::dp(660.0f) ? 0.0f : cyui::dp(660.0f);
        if (!ImGui::BeginTable("##processes", 9, flags, size, innerWidth)) return;
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn(ui_locale::tr("Nom du processus"), ImGuiTableColumnFlags_DefaultSort
            | ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, 0.0f, 0);
        ImGui::TableSetupColumn(ui_locale::tr("Confiance"), ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(78.0f), 1);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_PreferSortAscending, cyui::dp(60.0f), 2);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(70.0f), 3);
        ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(70.0f), 4);
        ImGui::TableSetupColumn(ui_locale::tr("Mémoire"), ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(90.0f), 5);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, cyui::dp(64.0f), 6);
        ImGui::TableSetupColumn("Handles", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide, cyui::dp(68.0f), 7);
        ImGui::TableSetupColumn(ui_locale::tr("Durée"), ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(96.0f), 8);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0) {
            sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
            sortDirection = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending ? -1 : 1;
            specs->SpecsDirty = false;
            dirty = true;
            buildRows();
        }
        const auto& palette = cyui::palette();
        const float rowHeight = cyui::dp(compactProcesses ? 26.0f : 32.0f);
        const float indentStep = cyui::dp(16.0f);
        auto* draw = ImGui::GetWindowDrawList();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(display.size()), rowHeight);
        while (clipper.Step()) for (int displayIndex = clipper.DisplayStart; displayIndex < clipper.DisplayEnd; ++displayIndex) {
            const auto& row = display[displayIndex];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            const ImVec2 cell = ImGui::GetCursorScreenPos();
            const float top = cell.y - padY;
            const float textY = top + (rowHeight - ImGui::GetFontSize()) * 0.5f;
            if (row.groupHeader) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.bg2);
                {
                    cyui::ScopedFont font(cyui::boldFont(), 0.92f);
                    draw->AddText(ImVec2(cell.x, top + (rowHeight - ImGui::GetFontSize()) * 0.5f),
                        palette.textDim, row.title.c_str());
                }
                ImGui::Dummy(ImVec2(0.0f, rowHeight - padY * 2.0f));
                continue;
            }
            const auto& process = snapshot.processes[row.index];
            const Key key = identity(process);
            const bool active = selected.count(key) != 0;
            const bool open = expanded.count(key) != 0;
            const float nameWidth = ImGui::GetContentRegionAvail().x;
            const std::string id = "##row" + std::to_string(process.id) + "_" + std::to_string(process.created);
            if (active) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.selection);
            const bool clicked = ImGui::Selectable(id.c_str(), false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0.0f, rowHeight - padY * 2.0f));
            const float chevronRight = cell.x + row.depth * indentStep + indentStep;
            const bool overChevron = row.hasChildren && ImGui::IsItemHovered() && ImGui::GetMousePos().x < chevronRight;
            if (clicked) {
                if (overChevron) { if (!expanded.erase(key)) expanded.insert(key); dirty = true; }
                else {
                    select(key, displayIndex);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) requestDetails(process);
                }
            }
            if (ImGui::BeginPopupContextItem(id.c_str())) {
                if (!selected.count(key)) { selected.clear(); selected.insert(key); }
                processActions(process);
                ImGui::EndPopup();
            }
            if (active)
                draw->AddRectFilled(ImVec2(cell.x - cyui::dp(6.0f), top),
                    ImVec2(cell.x - cyui::dp(3.0f), top + rowHeight), palette.accent);
            if (treeMode)
                for (int level = 0; level < row.depth; ++level) {
                    const float x = cell.x + level * indentStep + cyui::dp(6.0f);
                    draw->AddLine(ImVec2(x, top), ImVec2(x, top + rowHeight), palette.line, std::max(1.0f, cyui::dp(1.0f)));
                }
            float x = cell.x + row.depth * indentStep;
            if (row.hasChildren) {
                const char* glyph = cyui::iconsAvailable()
                    ? (open ? cyui::icon::chevronDown : cyui::icon::chevronRight) : (open ? "v" : ">");
                const float glyphSize = ImGui::GetFontSize() * 0.68f;
                draw->AddText(ImGui::GetFont(), glyphSize, ImVec2(x + cyui::dp(1.0f), top + (rowHeight - glyphSize) * 0.5f),
                    overChevron ? palette.text : palette.textDim, glyph);
            }
            x += indentStep;
            const float iconSize = cyui::dp(compactProcesses ? 16.0f : 20.0f);
            const float iconY = top + (rowHeight - iconSize) * 0.5f;
            if (auto* icon = icons.get(process.path))
                draw->AddImage(ImTextureRef(icon), ImVec2(x, iconY), ImVec2(x + iconSize, iconY + iconSize));
            else ui_kit::letterAvatar(draw, ImVec2(x, iconY), iconSize, process.name.c_str());
            x += iconSize + cyui::dp(8.0f);
            const float room = std::max(0.0f, cell.x + nameWidth - x);
            const ImU32 nameColour = active ? palette.text : palette.text;
            if (row.hasChildren) {
                const std::string suffix = " " + std::to_string(row.descendants);
                const float suffixWidth = ImGui::CalcTextSize(suffix.c_str()).x;
                const float nameRoom = std::max(0.0f, room - suffixWidth);
                cyui::clippedText(draw, ImVec2(x, textY), nameRoom, nameColour, process.name.c_str());
                const float drawn = std::min(nameRoom, ImGui::CalcTextSize(process.name.c_str()).x);
                draw->AddText(ImVec2(x + drawn, textY), palette.textFaint, suffix.c_str());
            } else cyui::clippedText(draw, ImVec2(x, textY), room, nameColour, process.name.c_str());

            ImGui::TableSetColumnIndex(1);
            trustCell(draw, trustScores[row.index], top, rowHeight, textY);
            ImGui::TableSetColumnIndex(2);
            cellText(draw, textY, palette.textDim, std::to_string(process.id));
            ImGui::TableSetColumnIndex(3);
            ui_kit::tintCell(process.cpu / 100.0);
            cellText(draw, textY, std::isfinite(process.cpu) && process.cpu >= 0.05 ? palette.text : palette.textFaint,
                std::isfinite(process.cpu) ? format("%.1f %%", process.cpu) : "—");
            const double gpu = treeMode && row.hasChildren ? process.gpuGroup : process.gpu;
            ImGui::TableSetColumnIndex(4);
            ui_kit::tintCell(gpu / 100.0);
            {
                const ImVec2 gpuCell = ImGui::GetCursorScreenPos();
                const float gpuWidth = ImGui::GetContentRegionAvail().x;
                cellText(draw, textY, std::isfinite(gpu) && gpu >= 0.05 ? palette.text : palette.textFaint,
                    std::isfinite(gpu) ? format("%.1f %%", gpu) : "—");
                if (ui_kit::hovering(ImVec2(gpuCell.x, top), ImVec2(gpuCell.x + gpuWidth, top + rowHeight))) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(treeMode && row.hasChildren
                        ? ui_locale::tr("Groupe avec processus associés") : ui_locale::tr("Processus seul"));
                    cyui::textDim("%s", pick("Moteur GPU le plus sollicité", "Busiest GPU engine"));
                    if (std::isfinite(process.gpuLocalBytes))
                        ImGui::Text(pick("VRAM locale : %s", "Local VRAM: %s"), memoryText(process.gpuLocalBytes).c_str());
                    if (std::isfinite(process.gpuSharedBytes))
                        ImGui::Text(pick("Mémoire GPU partagée : %s", "Shared GPU memory: %s"), memoryText(process.gpuSharedBytes).c_str());
                    ImGui::EndTooltip();
                }
            }
            const double memoryPercent = snapshot.memoryTotalGb > 0
                ? process.workingSet / 1048576.0 / (snapshot.memoryTotalGb * 1024.0) * 100.0 : 0.0;
            ImGui::TableSetColumnIndex(5);
            ui_kit::tintCell(memoryPercent * 5.0 / 100.0);
            {
                const ImVec2 memoryCell = ImGui::GetCursorScreenPos();
                const float memoryWidth = ImGui::GetContentRegionAvail().x;
                cellText(draw, textY, palette.text, memoryText(static_cast<double>(process.workingSet)));
                if (ui_kit::hovering(ImVec2(memoryCell.x, top), ImVec2(memoryCell.x + memoryWidth, top + rowHeight)))
                    ImGui::SetTooltip(pick("%.1f %% de la mémoire physique", "%.1f %% of physical memory"), memoryPercent);
            }
            ImGui::TableSetColumnIndex(6);
            cellText(draw, textY, palette.textDim, std::to_string(process.threads));
            ImGui::TableSetColumnIndex(7);
            cellText(draw, textY, palette.textDim, std::to_string(process.handles));
            ImGui::TableSetColumnIndex(8);
            cellText(draw, textY, palette.textDim, processAge(process.created, snapshot.unixMs));
        }
        ImGui::EndTable();
    }
    void processInspector(const float heightPixels, const bool horizontal)
    {
        const auto& palette = cyui::palette();
        if (cyui::beginCard("##processInspector", nullptr, heightPixels / cyui::dp(1.0f))) {
            if (selected.empty()) {
                cyui::emptyState(pick("Aucune sélection", "Nothing selected"),
                    pick("Sélectionnez un processus pour l'inspecter.", "Select a process to inspect it."), cyui::icon::view);
            } else if (selected.size() > 1) {
                double cpu {}, memory {};
                std::vector<const ProcessInfo*> chosen;
                for (const auto& key : selected) if (const auto* process = find(key)) {
                    chosen.push_back(process);
                    if (std::isfinite(process->cpu)) cpu += process->cpu;
                    memory += static_cast<double>(process->workingSet);
                }
                cyui::heading(textf(ui_locale::tr("%zu processus sélectionnés · clic droit pour les actions disponibles."),
                    selected.size()).c_str(), 3);
                cyui::space(4.0f);
                if (cyui::beginProperties("##selectionTotals", 110.0f)) {
                    cyui::property(pick("CPU cumulé", "Total CPU"));
                    ImGui::Text("%.1f %%", cpu);
                    cyui::property(pick("Mémoire cumulée", "Total memory"));
                    ImGui::TextUnformatted(memoryText(memory).c_str());
                    cyui::endProperties();
                }
                cyui::space(4.0f);
                for (std::size_t i = 0; i < std::min<std::size_t>(8, chosen.size()); ++i)
                    cyui::textDim("%s · PID %d", chosen[i]->name.c_str(), chosen[i]->id);
                if (chosen.size() > 8) cyui::textFaint(ui_locale::tr("... et %zu autres"), chosen.size() - 8);
                cyui::space(8.0f);
                cyui::ScopedDisabled disabled(demoMode);
                const std::string label = ui_locale::tr("Arrêter ") + std::to_string(selected.size()) + ui_locale::tr(" tâches");
                if (cyui::dangerButton(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) askStop();
            } else if (const auto* process = find(*selected.begin())) {
                const auto index = static_cast<std::size_t>(process - snapshot.processes.data());
                const TrustResult unknown {};
                const auto& rating = index < trustScores.size() ? trustScores[index] : unknown;
                const auto identityBlock = [&] {
                    const float avatar = cyui::dp(36.0f);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    auto* draw = ImGui::GetWindowDrawList();
                    if (auto* icon = icons.get(process->path))
                        draw->AddImage(ImTextureRef(icon), at, ImVec2(at.x + avatar, at.y + avatar));
                    else ui_kit::letterAvatar(draw, at, avatar, process->name.c_str());
                    ImGui::Dummy(ImVec2(avatar, avatar));
                    ImGui::SameLine(0.0f, cyui::dp(10.0f));
                    ImGui::BeginGroup();
                    const float room = ImGui::GetContentRegionAvail().x;
                    {
                        cyui::ScopedFont font(cyui::boldFont(), 1.1f);
                        const ImVec2 name = ImGui::GetCursorScreenPos();
                        cyui::clippedText(ImGui::GetWindowDrawList(), name, room, palette.text, process->name.c_str());
                        ImGui::Dummy(ImVec2(room, ImGui::GetFontSize()));
                    }
                    cyui::textDim("%s · PID %d", categoryName(process->category), process->id);
                    ImGui::EndGroup();
                };
                const auto trustBlock = [&] {
                    cyui::overline(pick("CONFIANCE", "TRUST"), palette.textFaint);
                    if (rating.score >= 0) {
                        const float lineTop = ImGui::GetCursorPosY();
                        {
                            cyui::ScopedFont font(cyui::boldFont(), 1.4f);
                            cyui::textColoured(ui_kit::trustColour(rating.score), "%d", rating.score);
                        }
                        ImGui::SameLine(0.0f, cyui::dp(6.0f));
                        ImGui::SetCursorPosY(lineTop + ImGui::GetFontSize() * 0.32f);
                        cyui::textDim("/ 100 · %s", ui_locale::text(rating.label).c_str());
                        const auto tone = rating.score >= 80 ? cyui::Tone::success : rating.score >= 55 ? cyui::Tone::info
                            : rating.score >= 30 ? cyui::Tone::warning : cyui::Tone::error;
                        cyui::progressBar(rating.score / 100.0f, ImVec2(-1.0f, cyui::dp(4.0f)), nullptr, tone);
                    } else cyui::textDim("%s", ui_locale::text(rating.label).c_str());
                    ImGui::PushTextWrapPos(0.0f);
                    cyui::textFaint("%s", ui_locale::text(rating.explanation).c_str());
                    ImGui::PopTextWrapPos();
                };
                const auto propertiesBlock = [&] {
                    if (!cyui::beginProperties("##inspectorProperties", 96.0f)) return;
                    cyui::property(pick("Parent", "Parent")); ImGui::Text("%d", process->parent);
                    cyui::property(ui_locale::tr("Durée"));
                    ImGui::TextUnformatted(processAge(process->created, snapshot.unixMs).c_str());
                    cyui::property("CPU");
                    ImGui::TextUnformatted(std::isfinite(process->cpu) ? format("%.1f %%", process->cpu).c_str() : "—");
                    cyui::property("GPU");
                    ImGui::TextUnformatted(std::isfinite(process->gpu) ? format("%.1f %%", process->gpu).c_str() : "—");
                    cyui::property(ui_locale::tr("Mémoire"));
                    ImGui::TextUnformatted(memoryText(static_cast<double>(process->workingSet)).c_str());
                    cyui::property("Threads / Handles");
                    ImGui::Text("%d / %d", process->threads, process->handles);
                    if (std::isfinite(process->gpuLocalBytes) || std::isfinite(process->gpuSharedBytes)) {
                        cyui::property(pick("VRAM locale", "Local VRAM"));
                        ImGui::TextUnformatted(memoryText(process->gpuLocalBytes).c_str());
                        cyui::property(pick("GPU partagé", "Shared GPU"));
                        ImGui::TextUnformatted(memoryText(process->gpuSharedBytes).c_str());
                    }
                    cyui::endProperties();
                };
                const auto pathBlock = [&] {
                    cyui::overline(pick("EMPLACEMENT", "LOCATION"), palette.textFaint);
                    if (process->path.empty()) { cyui::textFaint(ui_locale::tr("Chemin du fichier indisponible.")); return; }
                    {
                        cyui::ScopedFont font(cyui::monoFont(), 0.86f);
                        ImGui::PushTextWrapPos(0.0f);
                        cyui::textDim("%s", process->path.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    if (cyui::iconTextButton(cyui::icon::folder, pick("Ouvrir", "Open"), cyui::Variant::ghost)) {
                        const auto path = wide(process->path);
                        const auto parameter = L"/select,\"" + path + L"\"";
                        ShellExecuteW(nullptr, L"open", L"explorer.exe", parameter.c_str(), nullptr, SW_SHOWNORMAL);
                    }
                    cyui::tooltip(ui_locale::tr("Ouvrir l'emplacement"));
                    ImGui::SameLine(0.0f, cyui::dp(4.0f));
                    if (cyui::iconTextButton(cyui::icon::copy, pick("Copier", "Copy"), cyui::Variant::ghost))
                        ImGui::SetClipboardText(process->path.c_str());
                    cyui::tooltip(ui_locale::tr("Copier le chemin"));
                };
                const auto actionsBlock = [&] {
                    const float width = ImGui::GetContentRegionAvail().x;
                    if (cyui::iconTextButton(cyui::icon::info, pick("Informations avancées", "Advanced information"),
                            cyui::Variant::secondary, ImVec2(width, 0.0f)))
                        requestDetails(*process);
                    cyui::ScopedDisabled disabled(demoMode);
                    if (cyui::dangerButton(ui_locale::tr("Arrêter le processus..."), ImVec2(width, 0.0f))) askStop();
                };
                if (horizontal && ImGui::BeginTable("##inspectorColumns", 3,
                        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV)) {
                    cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(10.0f, 0.0f));
                    ImGui::TableNextColumn(); identityBlock(); cyui::space(6.0f); trustBlock();
                    ImGui::TableNextColumn(); propertiesBlock();
                    ImGui::TableNextColumn(); pathBlock(); cyui::space(6.0f); actionsBlock();
                    ImGui::EndTable();
                } else if (!horizontal) {
                    identityBlock();
                    cyui::space(10.0f);
                    trustBlock();
                    cyui::divider(8.0f);
                    propertiesBlock();
                    cyui::divider(8.0f);
                    pathBlock();
                    cyui::space(10.0f);
                    actionsBlock();
                }
            }
        }
        cyui::endCard();
    }

    // ------------------------------------------------------------ performance
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
        const float fontSize = std::max(cyui::dp(10.0f), ImGui::GetFontSize() * 0.76f);
        auto* font = ImGui::GetFont();
        const auto labelWidth = [font, fontSize](const std::string& text) {
            return font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, text.c_str()).x;
        };
        const float leftMargin = std::max(cyui::dp(34.0f), labelWidth(axisLabel(leftMaximum)) + cyui::dp(7.0f));
        const double rateMaximum = disk && diskRateScale.count(device.id) ? diskRateScale.at(device.id) : 1.0;
        const float rightMargin = disk ? labelWidth(format("%.0f", rateMaximum) + ui_locale::tr(" Mo/s")) + cyui::dp(9.0f) : 0.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        const float width = std::max(cyui::dp(50.0f), available - leftMargin - rightMargin);
        const float bottomMargin = std::max(cyui::dp(16.0f), fontSize + cyui::dp(4.0f));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x + leftMargin, start.y));
        const std::string id = "##plot_" + device.id + "_" + (series.empty() || !series.front() ? "empty" : series.front()->id);
        const auto result = cyui::plot(id.c_str(), state, cyui::ArrayView<cyui::PlotSeries>(lines),
            ImVec2(width, std::max(cyui::dp(42.0f), cyui::dp(height) - bottomMargin)), options);
        const ImVec2 plotMin = ImGui::GetItemRectMin();
        const ImVec2 plotMax = ImGui::GetItemRectMax();
        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 axisColour = cyui::palette().textFaint;
        // Fewer ticks on short charts so the labels never overlap.
        const int steps = plotMax.y - plotMin.y < cyui::dp(110.0f) ? 2 : 4;
        for (int step = 0; step <= steps; ++step) {
            const float y = plotMax.y - (plotMax.y - plotMin.y) * step / static_cast<float>(steps);
            const auto left = axisLabel(leftMaximum * step / steps);
            draw->AddText(font, fontSize, ImVec2(plotMin.x - cyui::dp(5.0f) - labelWidth(left),
                std::clamp(y - fontSize * 0.5f, plotMin.y, plotMax.y - fontSize)), axisColour, left.c_str());
            if (disk) {
                const auto label = format("%.0f", rateMaximum * step / steps) + ui_locale::tr(" Mo/s");
                draw->AddText(font, fontSize, ImVec2(plotMax.x + cyui::dp(3.0f),
                    std::clamp(y - fontSize * 0.5f, plotMin.y, plotMax.y - fontSize)), axisColour, label.c_str());
            }
        }
        for (int step = 0; step <= 2; ++step) {
            const auto label = step == 2 ? std::string(pick("maintenant", "now")) : std::to_string(60 - step * 30) + " s";
            const float x = plotMin.x + (plotMax.x - plotMin.x) * step / 2.0f;
            draw->AddText(font, fontSize,
                ImVec2(std::clamp(x - labelWidth(label) * 0.5f, plotMin.x,
                    std::max(plotMin.x, plotMax.x - labelWidth(label))), plotMax.y + cyui::dp(2.0f)),
                axisColour, label.c_str());
        }
        if (result.hovered) {
            const double secondsAgo = std::clamp(now - result.mouse.x, 0.0, 60.0);
            draw->AddLine(ImVec2(ImGui::GetMousePos().x, plotMin.y),
                ImVec2(ImGui::GetMousePos().x, plotMax.y), cyui::palette().textDim, cyui::dp(1.0f));
            ImGui::BeginTooltip();
            cyui::textDim(ui_locale::tr("Il y a %.0f s"), secondsAgo);
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
                if (closest) cyui::textColoured(cyui::categorical(seriesIndex(device, *value)),
                    "%s : %.1f %s", ui_locale::text(value->label).c_str(), closest->value,
                    ui_locale::text(value->unit).c_str());
            }
            ImGui::EndTooltip();
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, plotMax.y));
        ImGui::Dummy(ImVec2(available, bottomMargin));
    }
    static std::string valueText(const MetricInfo* value, const char* pattern = "%.1f")
    {
        if (!value || !std::isfinite(value->value)) return ui_locale::tr("Indisponible");
        const auto unit = ui_locale::text(value->unit);
        return format(pattern, value->value) + (unit.empty() ? "" : " " + unit);
    }
    // A large value with a smaller detail on the same baseline.
    static void bigValue(const std::string& value, const ImU32 colour, const std::string& detail, const float factor = 1.5f)
    {
        const float top = ImGui::GetCursorPosY();
        float bigSize {};
        {
            cyui::ScopedFont font(cyui::boldFont(), factor);
            bigSize = ImGui::GetFontSize();
            cyui::textColoured(colour, "%s", value.c_str());
        }
        if (detail.empty()) return;
        ImGui::SameLine(0.0f, cyui::dp(10.0f));
        ImGui::SetCursorPosY(top + (bigSize - ImGui::GetFontSize()) * 0.78f);
        cyui::textDim("%s", detail.c_str());
    }
    const char* seriesName(const DeviceInfo& device, const MetricInfo& value) const
    {
        if (device.kind == "memory") return value.id == "used" ? pick("Utilisée", "In use") : pick("Engagée", "Committed");
        if (device.kind == "disk") return value.id == "active" ? pick("Activité", "Active")
            : value.id == "read" ? pick("Lecture", "Read") : pick("Écriture", "Write");
        if (device.kind == "network") return value.id == "receive" ? pick("Réception", "Receive") : pick("Envoi", "Send");
        return "";
    }
    std::string deviceDetail(const DeviceInfo& device) const
    {
        std::string detail;
        const auto append = [&detail](const std::string& part) {
            if (part.empty()) return;
            detail += (detail.empty() ? "" : "  ·  ") + part;
        };
        if (const auto* temperature = metric(device, "temperature"); temperature && std::isfinite(temperature->value))
            append(format("%.0f °C", temperature->value));
        if (device.kind == "cpu")
            if (const auto* clock = metric(device, "telemetry:clock"); clock && std::isfinite(clock->value))
                append(format("%.0f MHz", clock->value));
        if (device.kind == "gpu")
            if (const auto* vram = metric(device, "dedicated"); vram && std::isfinite(vram->value))
                append(std::isfinite(vram->maximum) && vram->maximum > 0
                    ? textf("VRAM %.1f / %.0f %s", vram->value, vram->maximum, pick("Go", "GB"))
                    : textf("VRAM %.1f %s", vram->value, pick("Go", "GB")));
        if (device.kind == "memory")
            if (const auto* used = metric(device, "used"); used && std::isfinite(used->value)
                && std::isfinite(used->maximum) && used->maximum > 0)
                append(textf(pick("sur %.1f Go · %.0f %%", "of %.1f GB · %.0f %%"), used->maximum,
                    used->value / used->maximum * 100.0));
        return detail;
    }
    void legend(const DeviceInfo& device, const std::vector<const MetricInfo*>& series)
    {
        cyui::ScopedFont font(nullptr, 0.88f);
        bool first = true;
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (const auto* value : series) {
            if (!value) continue;
            const auto text = std::string(seriesName(device, *value)) + "  " + valueText(value, "%.1f");
            const float width = ImGui::CalcTextSize(text.c_str()).x + cyui::dp(13.0f);
            if (!first) {
                ImGui::SameLine(0.0f, cyui::dp(14.0f));
                // Wrap the legend instead of letting it run under the card edge.
                if (ImGui::GetCursorScreenPos().x + width > right) ImGui::NewLine();
            }
            first = false;
            ui_kit::dotText(seriesColor(device, *value), text.c_str());
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
        std::erase(series, nullptr);
        const float cardHeight = wideRow ? (compactGraphCards ? 230.0f : 300.0f)
            : single ? (compactGraphCards ? 290.0f : 360.0f)
            : (compactGraphCards ? 214.0f : 290.0f);
        const std::string id = "##graphCard_" + device.id;
        if (cyui::beginCard(id.c_str(), nullptr, cardHeight)) {
            const std::string title = device.kind == "memory" ? ui_locale::tr("Mémoire") : ui_locale::text(device.title);
            {
                cyui::ScopedFont font(cyui::boldFont(), 0.95f);
                cyui::textDim("%s", title.c_str());
            }
            if (!single || wideRow) {
                ImGui::SameLine();
                const float button = ImGui::GetFontSize() + cyui::dp(8.0f);
                cyui::alignRight(button);
                if (cyui::iconButton(cyui::icon::newWindow, ui_locale::tr("Ouvrir ce graphique dans une fenêtre séparée"),
                        false, 0, ImVec2(button, ImGui::GetFontSize() + cyui::dp(4.0f)))) {
                    detached[device.id] = true;
                    if (!demoMode) bridge.send("M 1\n");
                }
            }
            const bool network = device.kind == "network";
            bigValue(valueText(primary, device.kind == "disk" ? "%.0f" : "%.1f"), seriesColor(device, *primary),
                network ? std::string() : deviceDetail(device), 1.45f);
            if (series.size() > 1) legend(device, series);
            else if (!wideRow) cyui::textFaint("%s", ui_locale::text(device.description).c_str());
            cyui::space(4.0f);
            const float remaining = ImGui::GetContentRegionAvail().y / cyui::dp(1.0f);
            chart(device, series, std::max(60.0f, remaining - 2.0f));
        }
        cyui::endCard();
    }
    void topProcessCard(const char* id, const char* title, const std::vector<int>& ranking, const int valueKind)
    {
        const auto& palette = cyui::palette();
        if (cyui::beginCard(id, nullptr, 0.0f)) {
            {
                cyui::ScopedFont font(cyui::boldFont(), 0.95f);
                cyui::textDim("%s", title);
            }
            cyui::space(2.0f);
            const auto valueOf = [this, valueKind](const ProcessInfo& process) {
                const double value = valueKind == 0 ? process.cpu : valueKind == 2 ? process.gpu
                    : static_cast<double>(process.workingSet);
                return std::isfinite(value) ? value : 0.0;
            };
            double maximum {};
            for (const int index : ranking) maximum = std::max(maximum, valueOf(snapshot.processes[index]));
            if (ranking.empty()) cyui::textFaint(ui_locale::tr("Aucun processus mesuré"));
            auto* draw = ImGui::GetWindowDrawList();
            for (const int index : ranking) {
                const auto& process = snapshot.processes[index];
                cyui::ScopedID scoped(process.id);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                const float rowHeight = ImGui::GetFontSize() + cyui::dp(16.0f);
                if (ImGui::Selectable("##rank", false, 0, ImVec2(width, rowHeight))) focusProcess(process);
                cyui::tooltip(ui_locale::tr("Afficher ce processus dans la liste"));
                const double value = valueOf(process);
                const std::string text = valueKind == 1 ? memoryText(value)
                    : std::isfinite(valueKind == 0 ? process.cpu : process.gpu) ? format("%.1f %%", value) : "—";
                const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
                const float y = at.y + cyui::dp(3.0f);
                cyui::clippedText(draw, ImVec2(at.x + cyui::dp(4.0f), y),
                    width - textWidth - cyui::dp(16.0f), palette.text, process.name.c_str());
                draw->AddText(ImVec2(at.x + width - textWidth - cyui::dp(4.0f), y), palette.textDim, text.c_str());
                const float barY = at.y + rowHeight - cyui::dp(6.0f);
                const double fraction = maximum > 0.0 ? value / maximum : 0.0;
                ui_kit::drawMeter(draw, ImVec2(at.x + cyui::dp(4.0f), barY),
                    ImVec2(at.x + width - cyui::dp(4.0f), barY + cyui::dp(3.0f)), fraction,
                    valueKind == 1 ? cyui::categorical(3) : valueKind == 2 ? cyui::categorical(11) : palette.accent);
            }
        }
        cyui::endCard();
    }
    void topProcesses()
    {
        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = available >= cyui::dp(900.0f) ? 3 : available >= cyui::dp(600.0f) ? 2 : 1;
        cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
        if (ImGui::BeginTable("##topProcesses", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextColumn();
            topProcessCard("##topCpu", ui_locale::tr("Processus · CPU"), topCpu, 0);
            ImGui::TableNextColumn();
            topProcessCard("##topMemory", ui_locale::tr("Processus · mémoire"), topMemory, 1);
            ImGui::TableNextColumn();
            topProcessCard("##topGpu", pick("Processus · GPU", "Processes · GPU"), topGpu, 2);
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
        const auto telemetry = [](const DeviceInfo* device, const char* unit, const char* labelPart) -> const MetricInfo* {
            if (!device) return nullptr;
            for (const auto& item : device->metrics)
                if (item.id.rfind("telemetry:", 0) == 0 && item.unit == unit
                    && (!labelPart || item.label.find(labelPart) != std::string::npos
                        || item.id.find("clock") != std::string::npos)) return &item;
            return nullptr;
        };
        std::vector<Highlight> values;
        if (cpu) values.push_back({pick("TEMPÉRATURE CPU", "CPU TEMPERATURE"), metric(*cpu, "temperature")});
        values.push_back({pick("PUISSANCE CPU", "CPU POWER"), telemetry(cpu, "W", nullptr)});
        values.push_back({pick("FRÉQUENCE CPU", "CPU CLOCK"), telemetry(cpu, "MHz", nullptr)});
        if (gpu) values.push_back({pick("TEMPÉRATURE GPU", "GPU TEMPERATURE"), metric(*gpu, "temperature")});
        values.push_back({pick("PUISSANCE GPU", "GPU POWER"), telemetry(gpu, "W", nullptr)});
        values.push_back({pick("FRÉQUENCE GPU", "GPU CLOCK"), telemetry(gpu, "MHz", "Core")});
        values.erase(std::remove_if(values.begin(), values.end(), [](const auto& value) {
            return !value.value || !std::isfinite(value.value->value);
        }), values.end());
        if (values.empty()) return;
        cyui::space(6.0f);
        cyui::sectionTitle(pick("Capteurs clés", "Key sensors"));
        cyui::space(4.0f);
        if (cyui::beginCard("##hardwareHighlights", nullptr, 0.0f)) {
            const int columns = std::min<int>(static_cast<int>(values.size()),
                std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cyui::dp(150.0f))));
            if (ImGui::BeginTable("##highlightGrid", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                for (const auto& item : values) {
                    ImGui::TableNextColumn();
                    const bool hot = item.value->unit == "°C" && item.value->value >= 80.0;
                    ui_kit::statValue(item.label, valueText(item.value, item.value->unit == "MHz" ? "%.0f" : "%.1f").c_str(),
                        hot ? cyui::palette().warning : cyui::palette().text);
                }
                ImGui::EndTable();
            }
        }
        cyui::endCard();
    }
    void dashboard(const std::string& only, const bool withTopProcesses = false)
    {
        std::vector<const DeviceInfo*> visible;
        for (const char* kind : { "cpu", "memory", "gpu", "disk", "network" }) {
            if (only.empty() && !graphExtraDevices && (std::strcmp(kind, "disk") == 0 || std::strcmp(kind, "network") == 0))
                continue;
            for (const auto& device : snapshot.devices)
                if (device.kind == kind && (only.empty() || device.id == only)) visible.push_back(&device);
        }
        if (visible.empty()) {
            cyui::emptyState(ui_locale::tr("Mesures en attente"), ui_locale::tr("Les courbes apparaîtront après la collecte."), cyui::icon::refresh);
            return;
        }
        const float available = ImGui::GetContentRegionAvail().x;
        const float gap = cyui::dp(10.0f);
        const int automatic = std::max(1, static_cast<int>((available + gap)
            / (cyui::dp(compactGraphCards ? 230.0f : 360.0f) + gap)));
        const int safe = std::max(1, static_cast<int>((available + gap) / (cyui::dp(220.0f) + gap)));
        const int requested = only.empty() && !graphRows ? (graphColumnsMode ? graphColumnsMode : automatic) : 1;
        int columns = std::clamp(std::min(requested, safe), 1, std::min(5, static_cast<int>(visible.size())));
        if (!graphColumnsMode && columns > 1) {
            // Balance the rows: five cards on four columns become 3 + 2, not 4 + 1.
            const int count = static_cast<int>(visible.size());
            const int rows = (count + columns - 1) / columns;
            columns = (count + rows - 1) / rows;
        }
        {
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
            if (ImGui::BeginTable(("##dashboardGrid" + only).c_str(), columns,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                for (const auto* device : visible) {
                    ImGui::TableNextColumn();
                    dashboardCard(*device, !only.empty() || graphRows, only.empty() && graphRows);
                }
                ImGui::EndTable();
            }
        }
        if (withTopProcesses) {
            cyui::space(6.0f);
            cyui::sectionTitle(pick("Processus les plus actifs", "Most active processes"));
            cyui::space(4.0f);
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
    void layoutPopup()
    {
        if (!ImGui::BeginPopup("##graphDisposition")) return;
        cyui::sectionTitle(pick("Vue d'ensemble", "Overview"));
        if (ImGui::MenuItem(pick("Cartes", "Cards"), nullptr, !graphRows)) graphRows = false;
        if (ImGui::MenuItem(pick("Lignes", "Rows"), nullptr, graphRows)) graphRows = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ui_locale::tr("Colonnes automatiques"), nullptr, graphColumnsMode == 0)) graphColumnsMode = 0;
        for (int columns = 2; columns <= 5; ++columns) {
            const std::string label = std::to_string(columns) + pick(" colonnes", " columns");
            if (ImGui::MenuItem(label.c_str(), nullptr, graphColumnsMode == columns)) graphColumnsMode = columns;
        }
        ImGui::Separator();
        ImGui::MenuItem(ui_locale::tr("Cartes compactes"), nullptr, &compactGraphCards);
        ImGui::MenuItem(ui_locale::tr("Afficher disques et réseau"), nullptr, &graphExtraDevices);
        cyui::space(4.0f);
        cyui::sectionTitle(pick("Détails", "Details"));
        if (ImGui::MenuItem(pick("Graphiques en cartes", "Charts as cards"), nullptr, detailGraphCards)) detailGraphCards = true;
        if (ImGui::MenuItem(pick("Graphiques en lignes", "Charts as rows"), nullptr, !detailGraphCards)) detailGraphCards = false;
        ImGui::EndPopup();
    }
    void performance()
    {
        const char* views[] {pick("Vue d'ensemble", "Overview"), pick("Détails", "Details")};
        const float viewsWidth = cyui::dp(230.0f);
        const float icon = ImGui::GetFrameHeight() + cyui::dp(2.0f);
        const float actions = viewsWidth + icon * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        ui_kit::beginPageHeader("Performance", pick("Historique de 60 secondes · survolez une courbe pour lire ses valeurs",
            "60-second history · hover a chart to read its values"), actions);
        cyui::alignRight(actions);
        int view = graphMode ? 0 : 1;
        if (cyui::segmented("##performanceView", views, 2, &view, viewsWidth)) graphMode = view == 0;
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::settings, pick("Disposition des graphiques", "Chart layout")))
            ImGui::OpenPopup("##graphDisposition");
        layoutPopup();
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::newWindow, pick("Ouvrir les graphiques dans une fenêtre séparée",
                "Open the charts in a separate window"))) {
            detached["all"] = true;
            if (!demoMode) bridge.send("M 1\n");
        }
        ui_kit::endPageHeader();
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
        const float height = ImGui::GetContentRegionAvail().y;
        cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, ImVec2(cyui::dp(6.0f), 0.0f));
        if (ImGui::BeginTable("##performanceDetail", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("##resources", ImGuiTableColumnFlags_WidthFixed, cyui::dp(250.0f));
            ImGui::TableSetupColumn("##details", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            resourceList(height);
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("##resourceDetails", ImVec2(0.0f, height), ImGuiChildFlags_None);
            if (chosen) resourceDetails(*chosen);
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    void resourceList(const float height)
    {
        const auto& colors = cyui::palette();
        if (cyui::beginCard("##resources", nullptr, height / cyui::dp(1.0f) - 1.0f)) {
            cyui::overline(pick("RESSOURCES", "RESOURCES"), colors.textFaint);
            cyui::space(4.0f);
            for (const auto& device : snapshot.devices) {
                if (device.kind != "cpu" && device.kind != "memory" && device.kind != "gpu"
                    && device.kind != "disk" && device.kind != "network") continue;
                const std::string label = "##resource_" + device.id;
                const bool active = activeDevice == device.id;
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                const float itemHeight = ImGui::GetFontSize() * 2.2f + cyui::dp(14.0f);
                auto* draw = ImGui::GetWindowDrawList();
                if (active) draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + itemHeight),
                    cyui::alpha(colors.accent, 0.14f), ImGui::GetStyle().FrameRounding + cyui::dp(2.0f));
                if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(width, itemHeight))) activeDevice = device.id;
                if (ImGui::IsItemHovered()) cyui::tooltip(ui_locale::text(device.description).c_str());
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 itemMax = ImGui::GetItemRectMax();
                if (active) draw->AddRectFilled(ImVec2(itemMin.x, itemMin.y + cyui::dp(6.0f)),
                    ImVec2(itemMin.x + cyui::dp(3.0f), itemMax.y - cyui::dp(6.0f)), colors.accent, cyui::dp(2.0f));
                const float left = itemMin.x + cyui::dp(12.0f);
                const float sparkWidth = cyui::dp(64.0f);
                const float sparkLeft = itemMax.x - sparkWidth - cyui::dp(8.0f);
                const float room = std::max(1.0f, sparkLeft - left - cyui::dp(6.0f));
                const auto title = ui_locale::text(device.title);
                {
                    cyui::ScopedFont font(cyui::boldFont(), 1.0f);
                    cyui::clippedText(draw, ImVec2(left, itemMin.y + cyui::dp(7.0f)), room, colors.text, title.c_str());
                }
                const auto summary = resourceSummary(device);
                const float summarySize = ImGui::GetFontSize() * 0.84f;
                draw->PushClipRect(ImVec2(left, itemMin.y), ImVec2(sparkLeft - cyui::dp(4.0f), itemMax.y), true);
                draw->AddText(ImGui::GetFont(), summarySize,
                    ImVec2(left, itemMax.y - summarySize - cyui::dp(8.0f)), colors.textDim, summary.c_str());
                draw->PopClipRect();
                const auto* primary = metric(device, device.kind == "cpu" ? "cpu" : device.kind == "memory" ? "used"
                    : device.kind == "disk" ? "active" : device.kind == "network" ? "receive" : "usage");
                if (!primary) continue;
                const auto found = histories.find(device.id + "/" + primary->id);
                if (found == histories.end() || found->second.size() < 2) continue;
                const float top = itemMin.y + cyui::dp(10.0f);
                const float bottom = itemMax.y - cyui::dp(10.0f);
                double maximum = std::isfinite(primary->maximum) && primary->maximum > 0 ? primary->maximum : 1.0;
                if (maximum <= 1.0)
                    for (const auto& point : found->second)
                        if (std::isfinite(point.value)) maximum = std::max(maximum, point.value * 1.15);
                const auto& history = found->second;
                const ImU32 colour = seriesColor(device, *primary);
                ImVec2 previous {};
                bool havePrevious = false;
                for (std::size_t i = 0; i < history.size(); ++i) {
                    if (!std::isfinite(history[i].value)) { havePrevious = false; continue; }
                    const ImVec2 point(sparkLeft + sparkWidth * static_cast<float>(i) / static_cast<float>(history.size() - 1),
                        bottom - (bottom - top) * static_cast<float>(std::clamp(history[i].value / maximum, 0.0, 1.0)));
                    if (havePrevious) {
                        draw->AddQuadFilled(previous, point, ImVec2(point.x, bottom), ImVec2(previous.x, bottom),
                            cyui::alpha(colour, 0.12f));
                        draw->AddLine(previous, point, colour, cyui::dp(1.2f));
                    }
                    previous = point;
                    havePrevious = true;
                }
            }
        }
        cyui::endCard();
    }
    void resourceDetails(const DeviceInfo& device)
    {
        ui_kit::beginPageHeader(ui_locale::text(device.title).c_str(), ui_locale::text(device.description).c_str(),
            device.kind == "gpu" || device.kind == "cpu" ? cyui::dp(300.0f) : 0.0f);
        if (device.kind == "gpu") {
            const char* label = pick("Processus GPU", "GPU processes");
            cyui::alignRight(cyui::buttonWidth(label) + ImGui::GetFontSize() * 1.8f);
            if (cyui::iconTextButton(cyui::icon::list, label)) {
                processView = 1;
                switchPage(false);
            }
        } else if (device.kind == "cpu") {
            const float width = ImGui::GetFrameHeight() * 1.8f + ImGui::CalcTextSize(ui_locale::tr("Afficher les processeurs logiques")).x
                + cyui::dp(10.0f);
            cyui::alignRight(width);
            cyui::toggleSwitch(ui_locale::tr("Afficher les processeurs logiques"), &showCores);
        }
        ui_kit::endPageHeader(8.0f);
        if (device.kind == "cpu" && showCores && !detailGraphCards) {
            const char* modes[] { ui_locale::tr("Compacte (Windows)"), ui_locale::tr("Détaillée") };
            int mode = compactCores ? 0 : 1;
            if (cyui::segmented("##cpuMode", modes, 2, &mode, cyui::dp(320.0f))) compactCores = mode == 0;
            cyui::space(6.0f);
        }
        if (detailGraphCards) {
            std::vector<const MetricInfo*> visible;
            for (const auto& value : device.metrics) {
                if (value.id.rfind("core:", 0) == 0 && !showCores) continue;
                visible.push_back(&value);
            }
            if (visible.empty()) return;
            const float available = ImGui::GetContentRegionAvail().x;
            const int columns = std::clamp(static_cast<int>(available / cyui::dp(300.0f)), 1,
                std::min(3, static_cast<int>(visible.size())));
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
            if (ImGui::BeginTable("##detailCards", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                for (const auto* value : visible) {
                    ImGui::TableNextColumn();
                    const std::string id = "##detail_" + device.id + "/" + value->id;
                    if (cyui::beginCard(id.c_str(), nullptr, 196.0f)) {
                        cyui::textDim("%s", ui_locale::text(value->label).c_str());
                        bigValue(valueText(value), seriesColor(device, *value), std::string(), 1.3f);
                        cyui::space(2.0f);
                        chart(device, {value}, std::max(60.0f, ImGui::GetContentRegionAvail().y / cyui::dp(1.0f) - 2.0f));
                    }
                    cyui::endCard();
                }
                ImGui::EndTable();
            }
            return;
        }
        if (device.kind == "cpu") {
            if (const auto* total = metric(device, "cpu")) {
                if (!showCores || !compactCores) chartRow(device, *total, 170.0f);
                else cyui::textDim(ui_locale::tr("Utilisation totale : %.1f %%"), total->value);
            }
            if (showCores) {
                std::vector<const MetricInfo*> cores;
                for (const auto& value : device.metrics) if (value.id.rfind("core:", 0) == 0) cores.push_back(&value);
                const int columns = compactCores ? 4 : 2;
                cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
                if (ImGui::BeginTable("##cores", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                    for (const auto* core : cores) {
                        ImGui::TableNextColumn();
                        chartRow(device, *core, compactCores ? 80.0f : 120.0f);
                    }
                    ImGui::EndTable();
                }
            }
        } else if (device.kind == "disk") {
            const std::vector<const MetricInfo*> series { metric(device, "active"), metric(device, "read"), metric(device, "write") };
            std::vector<const MetricInfo*> present;
            for (const auto* value : series) if (value) present.push_back(value);
            if (cyui::beginCard("##diskChart", nullptr, 0.0f)) {
                legend(device, present);
                cyui::space(4.0f);
                chart(device, present, 340.0f);
            }
            cyui::endCard();
        } else {
            for (const auto& value : device.metrics) {
                if (value.id == "temperature") continue;
                chartRow(device, value, device.kind == "gpu" ? 120.0f : 160.0f);
            }
        }
    }
    void chartRow(const DeviceInfo& device, const MetricInfo& value, const float height)
    {
        const std::string id = "##chartRow_" + device.id + "/" + value.id;
        if (cyui::beginCard(id.c_str(), nullptr, 0.0f)) {
            cyui::textDim("%s", ui_locale::text(value.label).c_str());
            ImGui::SameLine();
            const auto text = valueText(&value);
            {
                cyui::ScopedFont font(cyui::boldFont(), 1.0f);
                cyui::alignRight(ImGui::CalcTextSize(text.c_str()).x);
                cyui::textColoured(seriesColor(device, value), "%s", text.c_str());
            }
            chart(device, { &value }, height);
        }
        cyui::endCard();
    }

    // ------------------------------------------------------------- utilities
    void permissionControls(const wchar_t* page)
    {
        const bool isElevated = demoMode ? false : elevated();
        const char* state = isElevated ? pick("Administrateur", "Administrator") : pick("Droits standard", "Standard permissions");
        const char* restart = pick("Redémarrer en administrateur", "Restart as administrator");
        const bool offerRestart = !isElevated && !demoMode;
        const float width = ImGui::CalcTextSize(state).x + cyui::dp(14.0f)
            + (offerRestart ? cyui::buttonWidth(restart) + ImGui::GetStyle().ItemSpacing.x * 2.0f : 0.0f);
        cyui::alignRight(width);
        const float lineTop = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(lineTop + ImGui::GetStyle().FramePadding.y);
        ui_kit::dotText(isElevated ? cyui::palette().success : cyui::palette().textFaint, state);
        if (offerRestart) {
            ImGui::SameLine();
            ImGui::SetCursorPosY(lineTop);
            if (cyui::button(restart, cyui::Variant::secondary)) {
                if (restartElevated(page)) PostQuitMessage(0);
                else cyui::notify(pick("Élévation annulée ou indisponible.", "Elevation cancelled or unavailable."),
                    cyui::Tone::warning);
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
                rows.push_back({pick("Tous les GPU", "All GPUs"), process.name,
                    std::isfinite(process.gpu) ? format("%.1f %%", process.gpu) : "—",
                    std::isfinite(process.gpuLocalBytes)
                        ? format("%.1f MiB", process.gpuLocalBytes / 1048576.0) : "—",
                    std::isfinite(process.gpuSharedBytes)
                        ? format("%.1f MiB", process.gpuSharedBytes / 1048576.0) : "—",
                    process.id, &process});
            }
        }
        if (cyui::beginToolbar("##gpuToolbar")) {
            cyui::searchField("##gpuProcessFind", gpuProcessSearch, sizeof(gpuProcessSearch),
                pick("Nom du processus, PID ou GPU", "Process name, PID or GPU"),
                std::min(cyui::dp(360.0f), ImGui::GetContentRegionAvail().x * 0.5f));
            ImGui::SameLine();
            cyui::textFaint("%s", pick("Double-clic : afficher dans la liste complète · clic droit : actions",
                "Double-click: show in the full list · right-click: actions"));
            ImGui::SameLine();
            const float icon = ImGui::GetFrameHeight() + cyui::dp(2.0f);
            cyui::alignRight(icon);
            if (cyui::iconButton(cyui::icon::dockRight, pick("Panneau d'inspection", "Inspector panel"), showProcessInspector))
                showProcessInspector = !showProcessInspector;
        }
        cyui::endToolbar();
        cyui::space(8.0f);
        const auto query = lower(gpuProcessSearch);
        if (!query.empty()) std::erase_if(rows, [&](const Row& row) {
            return lower(row.name).find(query) == std::string::npos
                && lower(row.gpu).find(query) == std::string::npos
                && std::to_string(row.pid).find(query) == std::string::npos;
        });
        if (rows.empty()) {
            cyui::emptyState(pick("Aucun processus GPU", "No GPU processes"),
                pick("Aucune utilisation GPU mesurée pour le moment.", "No GPU usage has been measured yet."), cyui::icon::view);
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
        withInspector("##gpuArea", [&](const ImVec2 size) {
            const float padY = cyui::dp(5.0f);
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, ImVec2(cyui::dp(8.0f), padY));
            if (!ImGui::BeginTable("##gpuProcesses", 9,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersInnerV
                        | ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_Hideable
                        | ImGuiTableFlags_SizingFixedFit,
                    size, size.x >= cyui::dp(860.0f) ? 0.0f : cyui::dp(860.0f))) return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, cyui::dp(150.0f));
            ImGui::TableSetupColumn(pick("Processus", "Process"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn(pick("Confiance", "Trust"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(78.0f));
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, cyui::dp(60.0f));
            ImGui::TableSetupColumn(pick("Moteur", "Engine"), ImGuiTableColumnFlags_WidthFixed
                | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(76.0f));
            ImGui::TableSetupColumn(pick("Mém. locale", "Local mem."), ImGuiTableColumnFlags_WidthFixed
                | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(96.0f));
            ImGui::TableSetupColumn(pick("Mém. partagée", "Shared mem."), ImGuiTableColumnFlags_WidthFixed
                | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(104.0f));
            ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(70.0f));
            ImGui::TableSetupColumn(pick("Mémoire", "Memory"), ImGuiTableColumnFlags_WidthFixed
                | ImGuiTableColumnFlags_PreferSortDescending, cyui::dp(90.0f));
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
            const auto& palette = cyui::palette();
            const float rowHeight = cyui::dp(compactProcesses ? 26.0f : 32.0f);
            auto* draw = ImGui::GetWindowDrawList();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()), rowHeight);
            while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& row = rows[static_cast<std::size_t>(i)];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                ImGui::TableSetColumnIndex(0);
                const ImVec2 cell = ImGui::GetCursorScreenPos();
                const float top = cell.y - padY;
                const float textY = top + (rowHeight - ImGui::GetFontSize()) * 0.5f;
                const bool active = row.process && selected.count(identity(*row.process)) != 0;
                if (active) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.selection);
                cyui::ScopedID gpuId(row.gpu.c_str());
                cyui::ScopedID pidId(row.pid);
                if (ImGui::Selectable("##gpuRow", false, ImGuiSelectableFlags_SpanAllColumns
                        | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(0.0f, rowHeight - padY * 2.0f)) && row.process) {
                    selected.clear(); selected.insert(identity(*row.process));
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusProcess(*row.process);
                }
                if (row.process && ImGui::BeginPopupContextItem("##gpuProcessActions")) {
                    selected.clear(); selected.insert(identity(*row.process));
                    processActions(*row.process);
                    ImGui::EndPopup();
                }
                if (active) draw->AddRectFilled(ImVec2(cell.x - cyui::dp(8.0f), top),
                    ImVec2(cell.x - cyui::dp(5.0f), top + rowHeight), palette.accent);
                cyui::clippedText(draw, ImVec2(cell.x, textY), ImGui::GetContentRegionAvail().x, palette.textDim, row.gpu.c_str());
                ImGui::TableSetColumnIndex(1);
                {
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float iconSize = cyui::dp(compactProcesses ? 16.0f : 20.0f);
                    const float iconY = top + (rowHeight - iconSize) * 0.5f;
                    if (auto* icon = row.process ? icons.get(row.process->path) : nullptr)
                        draw->AddImage(ImTextureRef(icon), ImVec2(at.x, iconY), ImVec2(at.x + iconSize, iconY + iconSize));
                    else ui_kit::letterAvatar(draw, ImVec2(at.x, iconY), iconSize, row.name.c_str());
                    cyui::clippedText(draw, ImVec2(at.x + iconSize + cyui::dp(8.0f), textY),
                        ImGui::GetContentRegionAvail().x - iconSize - cyui::dp(8.0f), palette.text, row.name.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                if (row.process) {
                    const auto index = static_cast<std::size_t>(row.process - snapshot.processes.data());
                    if (index < trustScores.size()) trustCell(draw, trustScores[index], top, rowHeight, textY);
                    else cellText(draw, textY, palette.textFaint, "—", false);
                } else cellText(draw, textY, palette.textFaint, "—", false);
                ImGui::TableSetColumnIndex(3);
                cellText(draw, textY, palette.textDim, std::to_string(row.pid));
                ImGui::TableSetColumnIndex(4);
                ui_kit::tintCell(row.engineValue / 100.0);
                cellText(draw, textY, palette.text, row.engine);
                ImGui::TableSetColumnIndex(5);
                cellText(draw, textY, palette.text, memoryText(row.localBytes));
                ImGui::TableSetColumnIndex(6);
                cellText(draw, textY, palette.textDim, memoryText(row.sharedBytes));
                ImGui::TableSetColumnIndex(7);
                if (row.process) {
                    ui_kit::tintCell(row.process->cpu / 100.0);
                    cellText(draw, textY, palette.text, std::isfinite(row.process->cpu) ? format("%.1f %%", row.process->cpu) : "—");
                } else cellText(draw, textY, palette.textFaint, "—");
                ImGui::TableSetColumnIndex(8);
                cellText(draw, textY, row.process ? palette.text : palette.textFaint,
                    row.process ? memoryText(row.memoryBytes) : std::string("—"));
            }
            ImGui::EndTable();
        });
    }
    void sensorFallbackTable()
    {
        const auto& palette = cyui::palette();
        const auto query = lower(sensorSearch);
        cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(8.0f, 5.0f));
        if (!ImGui::BeginTable("##sensorFallback", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH
                | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
            return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(pick("Capteur", "Sensor"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(pick("Actuel", "Current"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(110.0f));
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, cyui::dp(90.0f));
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, cyui::dp(90.0f));
        ImGui::TableHeadersRow();
        for (const auto& device : snapshot.devices) {
            const bool groupMatch = query.empty() || lower(device.title).find(query) != std::string::npos;
            bool groupShown = false;
            for (const auto& value : device.metrics) {
                if ((device.kind != "sensor" && value.unit != "°C") || !std::isfinite(value.value)) continue;
                if (!groupMatch && lower(value.label).find(query) == std::string::npos) continue;
                const auto reading = liveTemperatures.find(device.id + "/" + value.id);
                if (reading == liveTemperatures.end()) continue;
                if (!groupShown) {
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.bg2);
                    ImGui::TableSetColumnIndex(0);
                    cyui::ScopedFont font(cyui::boldFont(), 0.92f);
                    cyui::textDim("%s", ui_locale::text(device.title).c_str());
                    groupShown = true;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Indent(cyui::dp(12.0f));
                ImGui::TextUnformatted(ui_locale::text(value.label).c_str());
                ImGui::Unindent(cyui::dp(12.0f));
                ImGui::TableSetColumnIndex(1);
                cyui::textColoured(temperatureColour(reading->second.current, value.unit), "%.1f %s",
                    reading->second.current, ui_locale::text(value.unit).c_str());
                ImGui::TableSetColumnIndex(2); cyui::textDim("%.1f", reading->second.minimum);
                ImGui::TableSetColumnIndex(3); cyui::textDim("%.1f", reading->second.maximum);
            }
        }
        ImGui::EndTable();
    }
    static ImU32 temperatureColour(const double value, const std::string& unit)
    {
        const auto& palette = cyui::palette();
        if (unit != "°C") return palette.text;
        return value >= 85.0 ? palette.error : value >= 70.0 ? palette.warning : palette.text;
    }
    void temperatures()
    {
        ui_kit::beginPageHeader(pick("Températures", "Temperatures"),
            pick("Capteurs matériels en direct · min et max depuis l'ouverture", "Live hardware sensors · min and max since launch"),
            cyui::dp(demoMode ? 180.0f : 420.0f));
        permissionControls(L"--temperatures");
        ui_kit::endPageHeader();
        if (cyui::beginToolbar("##sensorToolbar")) {
            const char* views[] {pick("Capteurs", "Sensors"), pick("Courbes", "Charts")};
            cyui::segmented("##sensorViews", views, 2, &sensorView, cyui::dp(220.0f));
            cyui::toolbarSeparator();
            if (sensorView == 0) {
                cyui::searchField("##sensorFind", sensorSearch, sizeof(sensorSearch),
                    pick("Rechercher un capteur", "Find a sensor"), cyui::dp(260.0f));
                ImGui::SameLine();
                cyui::toggleSwitch(pick("Disponibles uniquement", "Available only"), &sensorAvailableOnly);
                ImGui::SameLine();
                const char* reset = pick("Réinitialiser min/max", "Reset min/max");
                cyui::alignRight(cyui::buttonWidth(reset) + ImGui::GetFontSize() * 1.8f);
                if (cyui::iconTextButton(cyui::icon::refresh, reset, cyui::Variant::ghost)) {
                    if (hardwareMonitor) hardwareMonitor->reset_sensor_statistics();
                    for (auto& [key, reading] : liveTemperatures)
                        reading.minimum = reading.maximum = reading.current;
                }
            } else {
                {
                    cyui::ScopedDisabled disabled(demoMode);
                    if (cyui::iconTextButton(cyui::icon::importFile, pick("Importer un CSV HWiNFO", "Import HWiNFO CSV")))
                        sensorLog.chooseFile();
                }
                if (sensorLog.enabled()) {
                    ImGui::SameLine();
                    if (cyui::iconTextButton(cyui::icon::close, pick("Fermer le CSV", "Close CSV"), cyui::Variant::ghost))
                        sensorLog.clear();
                    if (ImGui::GetTime() >= nextSensorRefresh) {
                        sensorLog.refresh(); nextSensorRefresh = ImGui::GetTime() + 1.0;
                    }
                }
            }
        }
        cyui::endToolbar();
        cyui::space(8.0f);
        for (const auto& device : snapshot.devices) if (device.kind == "sensor-status") {
            const bool pawnio = device.description.find("PawnIO") != std::string::npos;
            cyui::callout(cyui::Tone::warning, pawnio
                ? pick("PawnIO absent ou inaccessible : certains capteurs avancés sont indisponibles. Les mesures natives restent actives.",
                    "PawnIO missing or inaccessible: some advanced sensors are unavailable. Native measurements remain active.")
                : pick("Capteurs avancés indisponibles. Les mesures natives restent actives.",
                    "Advanced sensors unavailable. Native measurements remain active."));
            if (pawnio) {
                if (cyui::iconTextButton(cyui::icon::download, pick("Installer PawnIO (site officiel)", "Install PawnIO (official site)")))
                    ShellExecuteW(nullptr, L"open", L"https://pawnio.eu/", nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::SameLine();
                cyui::textFaint("%s", pick("Choisir l'édition officielle signée ; validation Windows possible.",
                    "Choose the signed Official edition; Windows approval may be required."));
            }
            cyui::space(6.0f);
            break;
        }
        if (sensorView == 0) {
            const auto hardware = hardwareMonitor ? hardwareMonitor->snapshot() : nullptr;
            if (!hardwareMonitor) sensorFallbackTable();
            else if (!hardware || hardware->timestamp == cyhwinfo::Timestamp{}) {
                cyui::emptyState(pick("Chargement des capteurs…", "Loading sensors…"),
                    pick("La détection matérielle peut prendre quelques secondes.", "Hardware discovery can take a few seconds."),
                    cyui::icon::refresh);
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
        temperatureCharts();
    }
    void temperatureCharts()
    {
        const auto& palette = cyui::palette();
        const DeviceInfo* selectedDevice {};
        const MetricInfo* selectedMetric {};
        bool selectedStillAvailable = false;
        for (const auto& device : snapshot.devices)
            for (const auto& value : device.metrics)
                if ((device.kind == "sensor" || value.unit == "°C") && std::isfinite(value.value)
                    && activeSensorKey == device.id + "/" + value.id) selectedStillAvailable = true;
        if (!selectedStillAvailable) activeSensorKey.clear();
        const float height = ImGui::GetContentRegionAvail().y;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float listWidth = std::clamp(availableWidth * 0.45f, cyui::dp(340.0f), cyui::dp(560.0f));
        cyui::ScopedVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(cyui::dp(5.0f), 0.0f));
        if (!ImGui::BeginTable("##temperatureLayout", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
            return;
        ImGui::TableSetupColumn("##sensors", ImGuiTableColumnFlags_WidthFixed, listWidth);
        ImGui::TableSetupColumn("##sensorDetail", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(8.0f, 5.0f));
            if (ImGui::BeginTable("##liveTemperatures", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH
                    | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, height))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(pick("Capteur", "Sensor"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(pick("Actuel", "Current"), ImGuiTableColumnFlags_WidthFixed, cyui::dp(84.0f));
                ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, cyui::dp(56.0f));
                ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, cyui::dp(56.0f));
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
                            ImGui::TableNextRow();
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.bg2);
                            ImGui::TableSetColumnIndex(0);
                            cyui::ScopedFont font(cyui::boldFont(), 0.92f);
                            cyui::textDim("%s", ui_locale::text(device.title).c_str());
                            groupShown = true;
                        }
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const bool active = activeSensorKey == key;
                        if (active) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.selection);
                        ImGui::Indent(cyui::dp(12.0f));
                        if (ImGui::Selectable((ui_locale::text(value.label) + "##" + key).c_str(), false,
                                ImGuiSelectableFlags_SpanAllColumns))
                            activeSensorKey = key;
                        ImGui::Unindent(cyui::dp(12.0f));
                        ImGui::TableSetColumnIndex(1);
                        cyui::textColoured(temperatureColour(reading->second.current, value.unit), "%.1f %s",
                            reading->second.current, ui_locale::text(value.unit).c_str());
                        ImGui::TableSetColumnIndex(2); cyui::textDim("%.1f", reading->second.minimum);
                        ImGui::TableSetColumnIndex(3); cyui::textDim("%.1f", reading->second.maximum);
                    }
                }
                if (sensorLog.enabled()) {
                    ImGui::TableNextRow();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, palette.bg2);
                    ImGui::TableSetColumnIndex(0);
                    cyui::textDim("HWiNFO CSV");
                    for (const auto& reading : sensorLog.readings()) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::Indent(cyui::dp(12.0f));
                        ImGui::TextUnformatted(reading.name.c_str());
                        ImGui::Unindent(cyui::dp(12.0f));
                        ImGui::TableSetColumnIndex(1);
                        cyui::textColoured(temperatureColour(reading.current, "°C"), "%.1f °C", reading.current);
                        ImGui::TableSetColumnIndex(2); cyui::textDim("%.1f", reading.minimum);
                        ImGui::TableSetColumnIndex(3); cyui::textDim("%.1f", reading.maximum);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::TableSetColumnIndex(1);
        if (cyui::beginCard("##sensorDetailCard", nullptr, height / cyui::dp(1.0f) - 1.0f)) {
            if (selectedDevice && selectedMetric) {
                const auto& reading = liveTemperatures.at(activeSensorKey);
                cyui::textDim("%s", ui_locale::text(selectedDevice->title).c_str());
                cyui::heading(ui_locale::text(selectedMetric->label).c_str(), 2);
                cyui::space(4.0f);
                const auto unit = ui_locale::text(reading.unit);
                if (ImGui::BeginTable("##sensorStats", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableNextColumn();
                    ui_kit::statValue(pick("ACTUEL", "CURRENT"), textf("%.1f %s", reading.current, unit.c_str()).c_str(),
                        temperatureColour(reading.current, reading.unit) == palette.text ? palette.accent
                            : temperatureColour(reading.current, reading.unit));
                    ImGui::TableNextColumn();
                    ui_kit::statValue("MIN", textf("%.1f %s", reading.minimum, unit.c_str()).c_str(), palette.text);
                    ImGui::TableNextColumn();
                    ui_kit::statValue("MAX", textf("%.1f %s", reading.maximum, unit.c_str()).c_str(), palette.text);
                    ImGui::EndTable();
                }
                cyui::space(8.0f);
                chart(*selectedDevice, {selectedMetric}, 240.0f);
                cyui::divider(8.0f);
                cyui::sectionTitle(pick("Autres mesures du composant", "Other readings of this device"));
                cyui::space(2.0f);
                if (cyui::beginProperties("##relatedSensors", 0.0f)) {
                    for (const auto& value : selectedDevice->metrics) {
                        if (value.id == selectedMetric->id || !std::isfinite(value.value)) continue;
                        cyui::property(ui_locale::text(value.label).c_str());
                        ImGui::Text("%.1f %s", value.value, ui_locale::text(value.unit).c_str());
                    }
                    cyui::endProperties();
                }
            } else {
                cyui::emptyState(pick("Capteurs en attente", "Waiting for sensors"),
                    pick("La détection matérielle démarre avec cette page.", "Hardware discovery starts with this page."),
                    cyui::icon::refresh);
            }
            if (!sensorLog.error().empty()) cyui::callout(cyui::Tone::warning, sensorLog.error().c_str());
        }
        cyui::endCard();
        ImGui::EndTable();
    }
    void systemInfo()
    {
        ui_kit::beginPageHeader(pick("Système", "System"),
            pick("Windows, carte mère et inventaire matériel", "Windows, motherboard and hardware inventory"),
            cyui::dp(demoMode ? 180.0f : 420.0f));
        permissionControls(L"--system-info");
        ui_kit::endPageHeader();
        const auto unknown = pick("Indisponible", "Unavailable");
        const auto reg = [&](const wchar_t* key, const wchar_t* name) {
            return demoMode ? std::string() : registryValue(HKEY_LOCAL_MACHINE, key, name);
        };
        const auto field = [&](const char* label, const std::string& value) {
            cyui::property(label);
            if (value.empty() || value == " ") cyui::textFaint("%s", unknown);
            else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(value.c_str());
                ImGui::PopTextWrapPos();
            }
        };
        if (!demoMode && !inventory) inventory = std::make_unique<HardwareInventory>();
        if (inventory) inventory->poll();
        ImGui::BeginChild("##systemScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = available >= cyui::dp(980.0f) ? 3 : available >= cyui::dp(640.0f) ? 2 : 1;
        {
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
            if (ImGui::BeginTable("##systemCards", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableNextColumn();
                if (cyui::beginCard("##windowsCard", "Windows", 0.0f)) {
                    if (cyui::beginProperties("##windowsProperties", 120.0f)) {
                        field(pick("Système", "OS"), demoMode ? "Windows Demo Edition"
                            : reg(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName"));
                        field("Version", demoMode ? "Demo 1"
                            : reg(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion"));
                        field("Architecture", "x64");
                        field(pick("Processeurs logiques", "Logical processors"),
                            demoMode ? "8" : std::to_string(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
                        cyui::endProperties();
                    }
                }
                cyui::endCard();
                ImGui::TableNextColumn();
                if (cyui::beginCard("##boardCard", pick("Carte mère", "Motherboard"), 0.0f)) {
                    if (cyui::beginProperties("##boardProperties", 120.0f)) {
                        field(pick("Modèle", "Model"), demoMode ? "Demo Board X1"
                            : reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardManufacturer") + " "
                              + reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BaseBoardProduct"));
                        field("BIOS", demoMode ? "Demo BIOS" : reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSVersion"));
                        field(pick("Date du BIOS", "BIOS date"), demoMode ? "2025-01-01"
                            : reg(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"BIOSReleaseDate"));
                        cyui::endProperties();
                    }
                }
                cyui::endCard();
                ImGui::TableNextColumn();
                if (cyui::beginCard("##componentsCard", pick("Composants", "Components"), 0.0f)) {
                    if (cyui::beginProperties("##componentProperties", 120.0f)) {
                        for (const auto& device : snapshot.devices) {
                            if (device.kind == "cpu" || device.kind == "gpu" || device.kind == "disk")
                                field(device.kind == "cpu" ? "CPU" : device.kind == "gpu" ? "GPU" : pick("Disque", "Disk"),
                                    ui_locale::text(device.description));
                            if (device.kind == "gpu")
                                if (const auto* vram = metric(device, "dedicated"); vram && std::isfinite(vram->maximum))
                                    field("VRAM", format("%.1f", vram->maximum) + pick(" Go", " GB"));
                        }
                        field("RAM", snapshot.memoryTotalGb > 0
                            ? format("%.1f", snapshot.memoryTotalGb) + pick(" Go", " GB") : std::string());
                        cyui::endProperties();
                    }
                }
                cyui::endCard();
                ImGui::EndTable();
            }
        }
        cyui::space(8.0f);
        cyui::sectionTitle(pick("Matériel détaillé", "Detailed hardware"));
        cyui::space(4.0f);
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
        if (!demoMode && (!inventory || !inventory->ready())) {
            cyui::spinner("##inventorySpinner");
            ImGui::SameLine();
            cyui::textDim("%s", pick("Lecture de l'inventaire matériel…", "Reading hardware inventory…"));
        } else {
            const auto& fields = demoMode ? demoFields : inventory->fields();
            std::vector<std::string> groups;
            for (const auto& item : fields)
                if (std::find(groups.begin(), groups.end(), item.group) == groups.end()) groups.push_back(item.group);
            cyui::ScopedVar padding(ImGuiStyleVar_CellPadding, cyui::dp(5.0f, 5.0f));
            if (!groups.empty() && ImGui::BeginTable("##inventoryCards", columns,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                for (const auto& group : groups) {
                    ImGui::TableNextColumn();
                    const std::string id = "##inventory_" + group;
                    if (cyui::beginCard(id.c_str(), group.c_str(), 0.0f)) {
                        if (cyui::beginProperties((id + "_properties").c_str(), 130.0f)) {
                            for (const auto& item : fields) if (item.group == group) field(item.label.c_str(), item.value);
                            cyui::endProperties();
                        }
                    }
                    cyui::endCard();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    void dialogs()
    {
        if (cyui::beginModal(ui_locale::tr("Arrêter les tâches ?###stopModal"), &stopModal, 460.0f)) {
            cyui::callout(cyui::Tone::warning, ui_locale::tr("Les modifications non enregistrées peuvent être perdues."));
            cyui::space(8);
            cyui::textDim(ui_locale::tr("%zu processus sélectionné(s)"), pendingStop.size());
            cyui::space(2);
            for (std::size_t i = 0; i < std::min<std::size_t>(8, pendingStop.size()); ++i)
                if (const auto* process = find(pendingStop[i])) ImGui::BulletText("%s · PID %d", process->name.c_str(), process->id);
            if (pendingStop.size() > 8) cyui::textFaint(ui_locale::tr("... et %zu autres"), pendingStop.size() - 8);
            cyui::space(6);
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
                if (cyui::iconTextButton(cyui::icon::copy, ui_locale::tr("Copier tout"), cyui::Variant::secondary))
                    ImGui::SetClipboardText(detailText.c_str());
                cyui::divider(4.0f);
                {
                    cyui::ScopedColor ground(ImGuiCol_ChildBg, cyui::palette().bg0);
                    cyui::ScopedVar padding(ImGuiStyleVar_WindowPadding, cyui::dp(10.0f, 8.0f));
                    ImGui::BeginChild("##report", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_HorizontalScrollbar);
                    cyui::ScopedFont mono(cyui::monoFont(), 0.95f);
                    ImGui::TextUnformatted(detailText.c_str());
                    ImGui::EndChild();
                }
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
void AppUi::idle()
{
    auto& state = *state_;
    state.receive();
    state.uiBridge->drain([&state](const std::string& operation, const Json& args, bool unrestricted) {
        if (!unrestricted && (!state.assistantPane || !state.assistantPane->allows(operation)))
            throw Error("AccessDenied", "UI tool permission was revoked");
        return state.applyUiCommand(operation, args);
    });
}
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
    state.statusBar();
    state.sidebar();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    bool shown {};
    {
        cyui::ScopedVar padding(ImGuiStyleVar_WindowPadding, cyui::dp(18.0f, 14.0f));
        padding.push(ImGuiStyleVar_WindowBorderSize, 0.0f);
        shown = ImGui::Begin("##main", nullptr, flags);
    }
    if (shown) {
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

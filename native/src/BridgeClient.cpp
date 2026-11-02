#include "BridgeClient.h"

#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace {
std::string windowsError(const DWORD code)
{
    char* message {};
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPSTR>(&message), 0, nullptr);
    std::string result = message != nullptr ? message : "Erreur Windows";
    if (message != nullptr) LocalFree(message);
    return result;
}
struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t offset {};
    template <class T> T value()
    {
        if (sizeof(T) > size - offset) throw std::runtime_error("Trame incomplète");
        T result;
        std::memcpy(&result, data + offset, sizeof(T));
        offset += sizeof(T);
        return result;
    }
    std::string text()
    {
        const auto length = value<std::int32_t>();
        if (length < 0 || static_cast<std::size_t>(length) > size - offset) throw std::runtime_error("Chaîne invalide");
        std::string result(reinterpret_cast<const char*>(data + offset), static_cast<std::size_t>(length));
        offset += length;
        return result;
    }
};
SystemSnapshot parseSnapshot(Reader& r)
{
    SystemSnapshot snapshot;
    snapshot.unixMs = r.value<std::int64_t>();
    snapshot.cpu = r.value<double>();
    snapshot.memoryUsedGb = r.value<double>();
    snapshot.memoryTotalGb = r.value<double>();
    snapshot.collectMs = r.value<double>();
    const auto count = r.value<std::int32_t>();
    if (count < 0 || count > 100000) throw std::runtime_error("Nombre de processus invalide");
    snapshot.processes.reserve(count);
    for (int i = 0; i < count; ++i) {
        ProcessInfo process;
        process.id = r.value<std::int32_t>();
        process.created = r.value<std::int64_t>();
        process.parent = r.value<std::int32_t>();
        process.cpu = r.value<double>();
        process.workingSet = r.value<std::int64_t>();
        process.threads = r.value<std::int32_t>();
        process.handles = r.value<std::int32_t>();
        process.category = r.value<std::uint8_t>();
        process.name = r.text();
        process.path = r.text();
        snapshot.processes.push_back(std::move(process));
    }
    const auto deviceCount = r.value<std::int32_t>();
    if (deviceCount < 0 || deviceCount > 4096) throw std::runtime_error("Nombre de ressources invalide");
    snapshot.devices.reserve(deviceCount);
    for (int i = 0; i < deviceCount; ++i) {
        DeviceInfo device;
        device.id = r.text();
        device.title = r.text();
        device.description = r.text();
        device.kind = r.text();
        const auto metricCount = r.value<std::int32_t>();
        if (metricCount < 0 || metricCount > 4096) throw std::runtime_error("Nombre de mesures invalide");
        device.metrics.reserve(metricCount);
        for (int j = 0; j < metricCount; ++j) {
            MetricInfo metric;
            metric.id = r.text();
            metric.label = r.text();
            metric.unit = r.text();
            metric.value = r.value<double>();
            metric.maximum = r.value<double>();
            device.metrics.push_back(std::move(metric));
        }
        snapshot.devices.push_back(std::move(device));
    }
    return snapshot;
}
}

BridgeClient::BridgeClient() = default;
BridgeClient::~BridgeClient()
{
    send("Q\n");
    if (input_ != nullptr) { CloseHandle(input_); input_ = nullptr; }
    if (process_ != nullptr) {
        if (WaitForSingleObject(process_, 2000) == WAIT_TIMEOUT) TerminateProcess(process_, 1);
    }
    if (reader_.joinable()) reader_.join();
    if (output_ != nullptr) CloseHandle(output_);
    if (process_ != nullptr) CloseHandle(process_);
}
bool BridgeClient::start()
{
    wchar_t module[MAX_PATH] {};
    if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) {
        startupError_ = "Chemin de l'application indisponible.";
        return false;
    }
    const auto bridge = std::filesystem::path(module).parent_path() / L"bridge" / L"CyWinTask.Bridge.exe";
    if (!std::filesystem::exists(bridge)) {
        startupError_ = "Collecteur introuvable : bridge/CyWinTask.Bridge.exe";
        return false;
    }
    SECURITY_ATTRIBUTES security { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE childRead {}, childWrite {};
    if (!CreatePipe(&output_, &childWrite, &security, 0)
        || !SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0)
        || !CreatePipe(&childRead, &input_, &security, 0)
        || !SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0)) {
        startupError_ = windowsError(GetLastError());
        if (childRead) CloseHandle(childRead);
        if (childWrite) CloseHandle(childWrite);
        return false;
    }
    HANDLE nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childRead;
    startup.hStdOutput = childWrite;
    startup.hStdError = nullError;
    PROCESS_INFORMATION info {};
    std::wstring command = L"\"" + bridge.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId());
    const bool ok = CreateProcessW(bridge.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, bridge.parent_path().c_str(), &startup, &info) != FALSE;
    CloseHandle(childRead);
    CloseHandle(childWrite);
    if (nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
    if (!ok) {
        startupError_ = windowsError(GetLastError());
        return false;
    }
    process_ = info.hProcess;
    CloseHandle(info.hThread);
    reader_ = std::thread([this] { readLoop(); });
    return true;
}
void BridgeClient::send(const std::string& command)
{
    std::lock_guard lock(inputMutex_);
    if (input_ == nullptr) return;
    DWORD written {};
    WriteFile(input_, command.data(), static_cast<DWORD>(command.size()), &written, nullptr);
}
bool BridgeClient::readExact(void* destination, const DWORD length)
{
    auto* data = static_cast<std::uint8_t*>(destination);
    DWORD done {};
    while (done < length) {
        DWORD count {};
        if (!ReadFile(output_, data + done, length - done, &count, nullptr) || count == 0) return false;
        done += count;
    }
    return true;
}
void BridgeClient::readLoop()
{
    try {
        while (true) {
            std::uint32_t length {};
            if (!readExact(&length, sizeof(length))) break;
            if (length == 0 || length > 16 * 1024 * 1024) throw std::runtime_error("Taille de trame invalide");
            std::vector<std::uint8_t> bytes(length);
            if (!readExact(bytes.data(), length)) break;
            Reader reader { bytes.data(), bytes.size() };
            const auto type = reader.value<std::uint8_t>();
            if (type == 1) {
                auto snapshot = std::make_shared<SystemSnapshot>(parseSnapshot(reader));
                std::lock_guard lock(mutex_);
                latest_ = std::move(snapshot);
                ++serial_;
            } else if (type == 2) {
                BridgeReply reply;
                reply.requestId = reader.value<std::int32_t>();
                reply.success = reader.value<std::uint8_t>() != 0;
                reply.text = reader.text();
                std::lock_guard lock(mutex_);
                replies_.push_back(std::move(reply));
            } else if (type == 3) {
                std::lock_guard lock(mutex_);
                error_ = reader.text();
            }
        }
        std::lock_guard lock(mutex_);
        if (error_.empty()) error_ = "Le collecteur s'est arrêté.";
    } catch (const std::exception& ex) {
        std::lock_guard lock(mutex_);
        error_ = ex.what();
    }
}
bool BridgeClient::poll(SystemSnapshot& snapshot, std::vector<BridgeReply>& replies, std::string& error)
{
    std::lock_guard lock(mutex_);
    const bool changed = serial_ != consumed_ && latest_ != nullptr;
    if (changed) { snapshot = *latest_; consumed_ = serial_; }
    replies.swap(replies_);
    error.swap(error_);
    return changed;
}

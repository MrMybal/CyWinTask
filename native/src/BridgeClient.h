#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ProcessInfo {
    int id {}, parent {}, threads {}, handles {};
    std::int64_t created {}, workingSet {};
    double cpu {};
    std::uint8_t category {};
    std::string name, path;
};
struct MetricInfo {
    std::string id, label, unit;
    double value {}, maximum {};
};
struct DeviceInfo {
    std::string id, title, description, kind;
    std::vector<MetricInfo> metrics;
};
struct SystemSnapshot {
    std::int64_t unixMs {};
    double cpu {}, memoryUsedGb {}, memoryTotalGb {}, collectMs {};
    std::vector<ProcessInfo> processes;
    std::vector<DeviceInfo> devices;
};
struct BridgeReply {
    int requestId {};
    bool success {};
    std::string text;
};

class BridgeClient {
public:
    BridgeClient();
    ~BridgeClient();
    BridgeClient(const BridgeClient&) = delete;
    BridgeClient& operator=(const BridgeClient&) = delete;
    bool start();
    bool poll(SystemSnapshot& snapshot, std::vector<BridgeReply>& replies, std::string& error);
    void send(const std::string& command);
    bool running() const { return process_ != nullptr; }
    const std::string& startupError() const { return startupError_; }
private:
    HANDLE process_ {}, input_ {}, output_ {};
    std::thread reader_;
    std::mutex mutex_;
    std::mutex inputMutex_;
    std::shared_ptr<SystemSnapshot> latest_;
    std::vector<BridgeReply> replies_;
    std::string error_;
    std::string startupError_;
    std::uint64_t serial_ {}, consumed_ {};
    void readLoop();
    bool readExact(void* destination, DWORD length);
};

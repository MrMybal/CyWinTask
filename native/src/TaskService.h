#pragma once

#include "BridgeClient.h"
#include <cymcp/mcp.hpp>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

// A headless connection to CyWinTask's existing Windows collector. The same
// operation registry is used by the chat, CLI and MCP server.
class TaskService {
public:
    TaskService();
    ~TaskService();
    TaskService(const TaskService&) = delete;
    TaskService& operator=(const TaskService&) = delete;
    bool start();
    void allowStop(bool enabled) { setAllowed("process.terminate", enabled); }
    void setAllowed(const std::string& operation, bool enabled);
    bool isAllowed(const std::string& operation) const;
    void setConfirmation(std::function<bool(const ProcessInfo&, const cymcp::CallContext&)> callback)
        { confirmation_ = std::move(callback); }
    bool stopAllowed() const { return isAllowed("process.terminate"); }
    std::shared_ptr<cymcp::Registry> registry() const { return registry_; }
    const std::string& startupError() const { return bridge_.startupError(); }
private:
    BridgeClient bridge_;
    std::shared_ptr<cymcp::Registry> registry_;
    std::atomic_bool running_ {false};
    mutable std::mutex permissionMutex_;
    std::map<std::string, bool> grants_;
    std::thread poller_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::shared_ptr<const SystemSnapshot> snapshot_;
    std::map<int, BridgeReply> replies_;
    std::string error_;
    int nextRequest_ {1000000};
    int activeDeviceCalls_ {};
    std::function<bool(const ProcessInfo&, const cymcp::CallContext&)> confirmation_;
    std::shared_ptr<const SystemSnapshot> snapshot(const cymcp::CallContext&) const;
    std::shared_ptr<const SystemSnapshot> devicesSnapshot(const cymcp::CallContext&);
    BridgeReply request(char kind, const ProcessInfo&, const cymcp::CallContext&);
};

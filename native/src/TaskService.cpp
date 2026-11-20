#include "TaskService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <string>

using cymcp::CallContext;
using cymcp::Error;
using cymcp::Json;

namespace {
Json finite(double value) { return std::isfinite(value) ? Json(value) : Json(nullptr); }
Json row(const ProcessInfo& p, bool path = false)
{
    Json value = {{"pid", p.id}, {"created", p.created}, {"parent_pid", p.parent},
                  {"name", p.name}, {"cpu_percent", finite(p.cpu)},
                  {"gpu_percent", finite(p.gpu)}, {"gpu_group_percent", finite(p.gpuGroup)},
                  {"working_set_bytes", p.workingSet}, {"threads", p.threads},
                  {"handles", p.handles}, {"category", p.category}};
    if (path) value["path"] = p.path;
    return value;
}
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
const Json percentage = {{"type", {"number", "null"}}};
const Json processSchema = {{"type", "object"}, {"properties", {
    {"pid", {{"type", "integer"}}}, {"created", {{"type", "integer"}}},
    {"parent_pid", {{"type", "integer"}}}, {"name", {{"type", "string"}}},
    {"cpu_percent", percentage}, {"gpu_percent", percentage},
    {"gpu_group_percent", percentage}, {"working_set_bytes", {{"type", "integer"}}},
    {"threads", {{"type", "integer"}}}, {"handles", {{"type", "integer"}}},
    {"category", {{"type", "integer"}}}
}}};
}

TaskService::TaskService()
    : registry_(std::make_shared<cymcp::Registry>(cymcp::Policy(
          [this](const CallContext&, const std::string& name, const Json&) {
              return isAllowed(name) ? cymcp::Decision::allow : cymcp::Decision::deny;
          }))),
      unrestrictedRegistry_(std::make_shared<cymcp::Registry>(cymcp::Policy::allow_all()))
{
    const auto add = [this](cymcp::Operation operation) {
        auto unrestricted = operation;
        if (unrestricted.name == "process.terminate")
            unrestricted.input_schema["required"] = {"pid", "created"};
        unrestrictedRegistry_->add(std::move(unrestricted));
        registry_->add(std::move(operation));
    };
    add({"permissions.list", "Read which CyWinTask operations the current access profile permits.",
        {{"type", "object"}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {
            {"items", {{"type", "array"}, {"items", {{"type", "object"}}}}}
        }}, {"required", {"items"}}},
        true, [this](const Json&, const CallContext& context) {
            Json items = Json::array();
            for (const auto& operation : registry_->describe()) {
                const auto name = operation.at("name").get<std::string>();
                items.push_back({{"name", name}, {"allowed", context.principal == unrestrictedMcpPrincipal || isAllowed(name)},
                    {"read_only", operation.at("annotations").at("readOnlyHint")}});
            }
            return Json{{"items", std::move(items)}};
        }});
    add({"system.summary", "Read the latest CPU, physical memory and process count from CyWinTask.",
        {{"type", "object"}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {
            {"timestamp_ms", {{"type", "integer"}}}, {"cpu_percent", percentage},
            {"memory_used_gb", {{"type", "number"}}}, {"memory_total_gb", {{"type", "number"}}},
            {"process_count", {{"type", "integer"}}}
        }}, {"required", {"timestamp_ms", "cpu_percent", "memory_used_gb", "memory_total_gb", "process_count"}}},
        true, [this](const Json&, const CallContext& context) {
            const auto s = snapshot(context);
            return Json{{"timestamp_ms", s->unixMs}, {"cpu_percent", finite(s->cpu)},
                {"memory_used_gb", s->memoryUsedGb}, {"memory_total_gb", s->memoryTotalGb},
                {"process_count", s->processes.size()}};
        }});
    add({"system.devices", "Read current CPU, memory, GPU, disk or network device metrics; filter by kind and paginate.",
        {{"type", "object"}, {"properties", {
            {"kind", {{"type", "string"}, {"maxLength", 64}}},
            {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100000000}}},
            {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 30}}}
        }}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {
            {"timestamp_ms", {{"type", "integer"}}}, {"total", {{"type", "integer"}}},
            {"offset", {{"type", "integer"}}}, {"items", {{"type", "array"}, {"items", {
                {"type", "object"}, {"properties", {
                    {"id", {{"type", "string"}}}, {"title", {{"type", "string"}}},
                    {"description", {{"type", "string"}}}, {"kind", {{"type", "string"}}},
                    {"metrics", {{"type", "array"}, {"items", {{"type", "object"}}}}}
                }}}}}}
        }}, {"required", {"timestamp_ms", "total", "offset", "items"}}},
        true, [this](const Json& args, const CallContext& context) {
            const auto s = devicesSnapshot(context);
            const std::string kind = lower(args.value("kind", std::string()));
            const int offset = args.value("offset", 0), limit = args.value("limit", 20);
            Json items = Json::array();
            int matched = 0;
            for (const auto& device : s->devices) {
                context.cancellation.check();
                if (!kind.empty() && lower(device.kind).find(kind) == std::string::npos) continue;
                if (matched >= offset && matched - offset < limit) {
                    Json metrics = Json::array();
                    for (const auto& metric : device.metrics)
                        metrics.push_back({{"id", metric.id}, {"label", metric.label},
                            {"unit", metric.unit}, {"value", finite(metric.value)},
                            {"maximum", finite(metric.maximum)}});
                    items.push_back({{"id", device.id}, {"title", device.title},
                        {"description", device.description}, {"kind", device.kind},
                        {"metrics", std::move(metrics)}});
                }
                ++matched;
            }
            return Json{{"timestamp_ms", s->unixMs}, {"total", matched},
                {"offset", offset}, {"items", std::move(items)}};
        }});
    add({"process.list", "Find running processes by name or PID; paginate results before inspecting one process.",
        {{"type", "object"}, {"properties", {
            {"query", {{"type", "string"}, {"maxLength", 128}}},
            {"offset", {{"type", "integer"}, {"minimum", 0}}},
            {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}
        }}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {
            {"total", {{"type", "integer"}}}, {"offset", {{"type", "integer"}}},
            {"items", {{"type", "array"}, {"items", processSchema}}}
        }}, {"required", {"total", "offset", "items"}}},
        true, [this](const Json& args, const CallContext& context) {
            const auto s = snapshot(context);
            const std::string query = lower(args.value("query", std::string()));
            const int offset = args.value("offset", 0), limit = args.value("limit", 50);
            Json items = Json::array();
            int matched = 0;
            for (const auto& p : s->processes) {
                context.cancellation.check();
                if (!query.empty() && lower(p.name).find(query) == std::string::npos
                    && std::to_string(p.id).find(query) == std::string::npos) continue;
                if (matched >= offset && matched - offset < limit) items.push_back(row(p));
                ++matched;
            }
            return Json{{"total", matched}, {"offset", offset}, {"items", std::move(items)}};
        }});
    add({"process.get", "Inspect one running process using its PID and optional creation timestamp to reject PID reuse.",
        {{"type", "object"}, {"properties", {
            {"pid", {{"type", "integer"}, {"minimum", 1}}},
            {"created", {{"type", "integer"}, {"minimum", 1}}}
        }}, {"required", {"pid"}}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {{"process", processSchema}}}, {"required", {"process"}}},
        true, [this](const Json& args, const CallContext& context) {
            const auto s = snapshot(context);
            const int pid = args.at("pid").get<int>();
            const auto found = std::find_if(s->processes.begin(), s->processes.end(),
                [pid](const ProcessInfo& p) { return p.id == pid; });
            if (found == s->processes.end() || (args.contains("created")
                && found->created != args.at("created").get<std::int64_t>()))
                throw Error("NotFound", "Process not found or PID reused");
            return Json{{"process", row(*found, true)}};
        }});
    add({"process.details", "Read a bounded page of the application's advanced process report, including loaded modules when accessible.",
        {{"type", "object"}, {"properties", {
            {"pid", {{"type", "integer"}, {"minimum", 1}}},
            {"created", {{"type", "integer"}, {"minimum", 1}}},
            {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100000000}}},
            {"length", {{"type", "integer"}, {"minimum", 1}, {"maximum", 8192}}}
        }}, {"required", {"pid", "created"}}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {
            {"total_bytes", {{"type", "integer"}}}, {"offset", {{"type", "integer"}}},
            {"text", {{"type", "string"}}}, {"next_offset", {{"type", {"integer", "null"}}}}
        }}, {"required", {"total_bytes", "offset", "text", "next_offset"}}},
        true, [this](const Json& args, const CallContext& context) {
            const auto s = snapshot(context);
            const int pid = args.at("pid").get<int>();
            const auto found = std::find_if(s->processes.begin(), s->processes.end(),
                [pid](const ProcessInfo& p) { return p.id == pid; });
            if (found == s->processes.end() || found->created != args.at("created").get<std::int64_t>())
                throw Error("NotFound", "Process not found or PID reused");
            const auto reply = request('D', *found, context);
            if (!reply.success) throw Error("ProcessError", reply.text);
            const auto offset = static_cast<std::size_t>(args.value("offset", std::int64_t(0)));
            const auto length = static_cast<std::size_t>(args.value("length", 4096));
            auto start = std::min(offset, reply.text.size());
            const auto continuation = [&](std::size_t position) {
                return position < reply.text.size()
                    && (static_cast<unsigned char>(reply.text[position]) & 0xC0) == 0x80;
            };
            while (continuation(start)) ++start;
            auto end = std::min(start + length, reply.text.size());
            while (end > start && continuation(end)) --end;
            if (end == start && start < reply.text.size()) {
                ++end;
                while (continuation(end)) ++end;
            }
            return Json{{"total_bytes", reply.text.size()}, {"offset", start},
                {"text", reply.text.substr(start, end - start)},
                {"next_offset", end < reply.text.size() ? Json(end) : Json(nullptr)}};
        }});
    add({"process.terminate", "Stop one process using a matching PID and creation timestamp. Never stops descendants implicitly.",
        {{"type", "object"}, {"properties", {
            {"pid", {{"type", "integer"}, {"minimum", 1}}},
            {"created", {{"type", "integer"}, {"minimum", 1}}},
            {"confirm", {{"type", "boolean"}}}
        }}, {"required", {"pid", "created", "confirm"}}, {"additionalProperties", false}},
        {{"type", "object"}, {"properties", {{"accepted", {{"type", "boolean"}}}}},
            {"required", {"accepted"}}},
        false, [this](const Json& args, const CallContext& context) {
            if (context.principal == unrestrictedMcpPrincipal) {
                if (args.contains("confirm") && !args.at("confirm").get<bool>())
                    throw Error("ConfirmationRequired", "Explicit confirm:false refused the stop");
            } else if (!args.at("confirm").get<bool>())
                throw Error("ConfirmationRequired", "Explicit confirm:true is required");
            const auto s = snapshot(context);
            const int pid = args.at("pid").get<int>();
            const auto found = std::find_if(s->processes.begin(), s->processes.end(),
                [pid](const ProcessInfo& p) { return p.id == pid; });
            if (found == s->processes.end() || found->created != args.at("created").get<std::int64_t>())
                throw Error("NotFound", "Process not found or PID reused");
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (now - s->unixMs > 3000) throw Error("StaleSnapshot", "Process sample is too old");
            if (confirmation_ && context.principal != unrestrictedMcpPrincipal && !confirmation_(*found, context))
                throw Error("ConfirmationRequired", "Process stop was not approved by the user");
            const auto reply = request('E', *found, context);
            if (!reply.success) throw Error("ProcessError", reply.text);
            return Json{{"accepted", true}};
        }});
}

TaskService::~TaskService()
{
    running_ = false;
    changed_.notify_all();
    if (poller_.joinable()) poller_.join();
}

void TaskService::setAllowed(const std::string& operation, bool enabled)
{
    std::lock_guard lock(permissionMutex_);
    grants_[operation] = enabled;
}

bool TaskService::isAllowed(const std::string& operation) const
{
    std::lock_guard lock(permissionMutex_);
    if (const auto found = grants_.find(operation); found != grants_.end()) return found->second;
    return operation == "system.summary" || operation == "system.devices"
        || operation == "process.list" || operation == "process.get"
        || operation == "permissions.list" || operation == "ui.state"
        || operation == "ui.performance.history";
}

bool TaskService::start()
{
    if (running_) return true;
    if (!bridge_.start()) return false;
    running_ = true;
    poller_ = std::thread([this] {
        while (running_) {
            SystemSnapshot next;
            std::vector<BridgeReply> replies;
            std::string error;
            const bool received = bridge_.poll(next, replies, error);
            if (received || !replies.empty() || !error.empty()) {
                std::lock_guard lock(mutex_);
                if (received) { snapshot_ = std::make_shared<SystemSnapshot>(std::move(next)); error_.clear(); }
                for (auto& reply : replies) replies_.emplace(reply.requestId, std::move(reply));
                if (!error.empty()) error_ = std::move(error);
                changed_.notify_all();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    });
    return true;
}

std::shared_ptr<const SystemSnapshot> TaskService::snapshot(const CallContext& context) const
{
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!snapshot_ && error_.empty() && std::chrono::steady_clock::now() < deadline) {
        context.cancellation.check();
        changed_.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (!error_.empty()) throw Error("Unavailable", error_);
    if (!snapshot_) throw Error("Unavailable", "No process sample yet");
    return snapshot_;
}

std::shared_ptr<const SystemSnapshot> TaskService::devicesSnapshot(const CallContext& context)
{
    std::unique_lock lock(mutex_);
    const auto baseline = snapshot_ ? snapshot_->unixMs : 0;
    if (++activeDeviceCalls_ == 1) {
        try { bridge_.send("M 1\n"); }
        catch (...) { --activeDeviceCalls_; throw; }
    }
    const auto release = [this] {
        std::lock_guard guard(mutex_);
        if (--activeDeviceCalls_ == 0) {
            try { bridge_.send("M 0\n"); } catch (...) {}
        }
    };
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (( !snapshot_ || snapshot_->unixMs <= baseline || snapshot_->devices.empty())
            && error_.empty() && std::chrono::steady_clock::now() < deadline) {
            context.cancellation.check();
            changed_.wait_for(lock, std::chrono::milliseconds(50));
        }
        if (!error_.empty()) throw Error("Unavailable", error_);
        if (!snapshot_ || snapshot_->unixMs <= baseline || snapshot_->devices.empty())
            throw Error("Unavailable", "Device metrics are not available");
        auto result = snapshot_;
        lock.unlock();
        release();
        return result;
    } catch (...) {
        if (lock.owns_lock()) lock.unlock();
        release();
        throw;
    }
}

BridgeReply TaskService::request(char kind, const ProcessInfo& p, const CallContext& context)
{
    context.cancellation.check();
    int id;
    { std::lock_guard lock(mutex_); id = nextRequest_++; }
    bridge_.send(std::string(1, kind) + " " + std::to_string(id) + " " + std::to_string(p.id)
        + " " + std::to_string(p.created) + "\n");
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (replies_.find(id) == replies_.end() && error_.empty()
        && std::chrono::steady_clock::now() < deadline) {
        context.cancellation.check();
        changed_.wait_for(lock, std::chrono::milliseconds(50));
    }
    const auto found = replies_.find(id);
    if (found == replies_.end()) throw Error("Unavailable", error_.empty() ? "Collector did not reply" : error_);
    BridgeReply reply = std::move(found->second);
    replies_.erase(found);
    return reply;
}

#include "UiBridge.h"
#include "TaskService.h"
#include <algorithm>
#include <chrono>

cymcp::Json UiBridge::invoke(std::string operation, cymcp::Json arguments,
                            const cymcp::CallContext& context)
{
    auto request = std::make_shared<Request>();
    request->operation = std::move(operation);
    request->arguments = std::move(arguments);
    request->unrestricted = context.principal == unrestrictedMcpPrincipal;
    std::unique_lock lock(mutex_);
    if (closed_) throw cymcp::Error("Unavailable", "The UI is closing");
    queue_.push_back(request);
    while (!request->done) {
        lock.unlock();
        try { context.cancellation.check(); }
        catch (...) {
            lock.lock();
            request->cancelled = true;
            auto found = std::find(queue_.begin(), queue_.end(), request);
            if (found != queue_.end()) queue_.erase(found);
            throw;
        }
        lock.lock();
        request->ready.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (request->error) std::rethrow_exception(request->error);
    return std::move(request->result);
}

void UiBridge::drain(const Handler& handler)
{
    for (int count = 0; count < 16; ++count) {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) break;
            request = queue_.front();
            queue_.pop_front();
        }
        try {
            {
                std::lock_guard lock(mutex_);
                if (request->cancelled) throw cymcp::Error("Cancelled", "UI command cancelled");
            }
            request->result = handler(request->operation, request->arguments, request->unrestricted);
        }
        catch (...) { request->error = std::current_exception(); }
        {
            std::lock_guard lock(mutex_);
            request->done = true;
        }
        request->ready.notify_all();
    }
}

void UiBridge::close()
{
    std::lock_guard lock(mutex_);
    closed_ = true;
    for (auto& request : queue_) {
        request->error = std::make_exception_ptr(cymcp::Error("Unavailable", "The UI is closing"));
        request->done = true;
        request->ready.notify_all();
    }
    queue_.clear();
}

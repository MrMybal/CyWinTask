#pragma once
#include <cymcp/core.hpp>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// Moves UI operations to the ImGui thread. Worker threads never touch AppUi state.
class UiBridge {
public:
    using Handler = std::function<cymcp::Json(const std::string&, const cymcp::Json&, bool)>;
    cymcp::Json invoke(std::string operation, cymcp::Json arguments, const cymcp::CallContext& context);
    void drain(const Handler& handler);
    void close();
private:
    struct Request {
        std::string operation;
        cymcp::Json arguments;
        cymcp::Json result;
        std::exception_ptr error;
        bool done {}, cancelled {}, unrestricted {};
        std::condition_variable ready;
    };
    std::mutex mutex_;
    std::deque<std::shared_ptr<Request>> queue_;
    bool closed_ {};
};

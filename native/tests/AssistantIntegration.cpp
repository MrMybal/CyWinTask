#include "../src/TaskService.h"
#include <cymcp/chat_ui.hpp>
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace cymcp;

namespace {
struct Target { int pid = 0; std::int64_t created = 0; };
class LocalFixture final : public Provider {
    std::shared_ptr<const Registry> tools_;
    std::shared_ptr<Target> target_;
public:
    LocalFixture(std::shared_ptr<const Registry> tools, std::shared_ptr<Target> target)
        : tools_(std::move(tools)), target_(std::move(target)) {}
    bool uses_host_history() const noexcept override { return true; }
    ProviderCapabilities capabilities() const override {
        ProviderCapabilities value;
        value.streaming = true; value.tools = true;
        return value;
    }
    void connect(const CallContext& context) override { context.cancellation.check(); }
    Session start_session(std::string model, std::string, const CallContext&, std::string) override {
        return {"fixture-session", std::move(model)};
    }
    ChatResult send(Session&, Json& history, const CallContext& context) override {
        const std::string prompt = history.back().at("content").get<std::string>();
        if (prompt == "slow") {
            for (int i = 0; i < 100; ++i) {
                context.cancellation.check();
                context.emit("TextDelta", {{"text", "..."}});
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        const std::string tool = prompt == "stop" ? "process.terminate" : "system.summary";
        context.emit("ToolStarted", {{"name", tool}});
        Json result = prompt == "stop"
            ? tools_->invoke(tool, {{"pid", target_->pid}, {"created", target_->created}, {"confirm", true}}, context)
            : tools_->invoke(tool, Json::object(), context);
        context.emit("ToolCompleted", {{"name", tool}});
        const std::string answer = prompt == "stop" && result.at("accepted").get<bool>()
            ? "Arrêt du processus de test confirmé."
            : "Mesure des processus disponible.";
        context.emit("TextDelta", {{"text", answer}});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        history.push_back({{"role", "assistant"}, {"content", answer}});
        return {answer, "stop"};
    }
    void disconnect() noexcept override {}
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
void wait(ChatController& chat) {
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    do {
        chat.poll();
        if (!chat.state().busy) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (Clock::now() < deadline);
    throw std::runtime_error("Chat operation timeout");
}
}

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    if (argc > 1 && std::string(argv[1]) == "--child") {
        Sleep(60000);
        return 0;
    }
    try {
        TaskService tasks;
        require(tasks.start(), "collector starts");
        auto target = std::make_shared<Target>();
        auto providers = std::make_shared<ProviderRegistry>(false);
        providers->add({"local-fixture", "Local fixture", "in-process", "none", {}},
            [target](const ProviderConfig& config) {
                return std::make_shared<LocalFixture>(config.tools, target);
            });
        ChatController chat(providers);
        ChatConnection connection;
        connection.provider_id = "local-fixture";
        connection.model = "local-fixture";
        connection.provider.tools = tasks.registry();
        chat.open(connection);
        wait(chat);
        require(chat.state().connected, "chat connects without external provider");
        chat.send("summary");
        bool streamed = false;
        while (chat.state().busy) {
            chat.poll();
            streamed |= chat.state().busy && chat.state().messages.size() > 1
                && !chat.state().messages.back().markdown.empty();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!streamed || chat.state().messages.back().markdown.find("processus") == std::string::npos)
            std::cerr << "Chat state: " << chat.state().phase << " / " << chat.state().error_code
                      << " / " << chat.state().error_message << '\n';
        require(streamed && chat.state().messages.back().markdown.find("processus") != std::string::npos,
            "chat streams a real registry read");
        chat.send("slow");
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        chat.cancel();
        wait(chat);
        require(chat.state().phase == "interrupted", "chat cancellation stops the turn");

        wchar_t executable[MAX_PATH] {};
        require(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "test executable found");
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --child";
        STARTUPINFOW startup {sizeof(startup)};
        PROCESS_INFORMATION child {};
        require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &child) != FALSE, "owned test process started");
        CloseHandle(child.hThread);
        try {
            target->pid = static_cast<int>(child.dwProcessId);
            for (int attempt = 0; attempt < 60 && !target->created; ++attempt) {
                const auto rows = tasks.registry()->invoke("process.list",
                    {{"query", std::to_string(target->pid)}, {"limit", 100}});
                for (const auto& row : rows.at("items"))
                    if (row.at("pid") == target->pid) target->created = row.at("created").get<std::int64_t>();
                if (!target->created) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            require(target->created != 0, "owned process identity sampled");
            tasks.allowStop(true);
            tasks.setConfirmation([](const ProcessInfo&, const CallContext&) { return false; });
            chat.send("stop");
            wait(chat);
            require(chat.state().phase == "failed" && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT,
                "chat tool respects an explicit user refusal");
            tasks.setConfirmation([](const ProcessInfo&, const CallContext&) { return true; });
            chat.send("stop");
            wait(chat);
            require(chat.state().phase == "ready" && WaitForSingleObject(child.hProcess, 8000) == WAIT_OBJECT_0,
                "chat tool stops only its owned test process");
        } catch (...) {
            TerminateProcess(child.hProcess, 1);
            CloseHandle(child.hProcess);
            throw;
        }
        CloseHandle(child.hProcess);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

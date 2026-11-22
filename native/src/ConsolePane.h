#pragma once

#include <windows.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct ImGuiInputTextCallbackData;

// A persistent Windows shell; its state survives successive commands.
class ConsolePane {
public:
    ConsolePane() = default;
    ~ConsolePane();
    ConsolePane(const ConsolePane&) = delete;
    ConsolePane& operator=(const ConsolePane&) = delete;
    void draw(bool demoMode);
    bool startSession(int shell);
    void stopSession();
    bool executeCommand(const std::string& command, int shell);
    std::string output();
private:
    void append(std::string_view text);
    void readOutput();
    bool startPipeSession(int shell);
    void clearOutput();
    static int historyCallback(ImGuiInputTextCallbackData* data);
    char command_[4096] {};
    int shell_ {};
    int activeShell_ {-1};
    std::atomic<bool> running_ {false};
    std::thread reader_;
    std::mutex mutex_;
    HANDLE child_ {}, inputWrite_ {}, outputRead_ {};
    std::deque<std::string> lines_;
    std::string pending_;
    std::deque<std::string> displayedLines_;
    std::uint64_t revision_ {}, displayedRevision_ {};
    int escape_ {};
    bool carriageReturn_ {};
    std::vector<std::string> history_;
    int historyIndex_ {-1};
};

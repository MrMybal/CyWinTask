#include "ConsolePane.h"
#include "UiLocale.h"

#include <cyui/Core.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace {
std::wstring shellPath(int shell)
{
    wchar_t folder[MAX_PATH] {};
    const UINT count = GetSystemDirectoryW(folder, MAX_PATH);
    if (!count || count >= MAX_PATH) return {};
    std::wstring path(folder, count);
    path += shell == 0 ? L"\\WindowsPowerShell\\v1.0\\powershell.exe" : L"\\cmd.exe";
    return path;
}
}

ConsolePane::~ConsolePane() { stopSession(); }

void ConsolePane::append(std::string_view text)
{
    std::lock_guard lock(mutex_);
    for (unsigned char byte : text) {
        if (escape_ == 1) { escape_ = byte == '[' ? 2 : byte == ']' ? 3 : 0; continue; }
        if (escape_ == 2) { if (byte >= 0x40 && byte <= 0x7e) escape_ = 0; continue; }
        if (escape_ == 3) { if (byte == 7) escape_ = 0; else if (byte == 27) escape_ = 4; continue; }
        if (escape_ == 4) { escape_ = byte == '\\' ? 0 : 3; continue; }
        if (byte == 27) { escape_ = 1; continue; }
        if (byte == '\r') { carriageReturn_ = true; continue; }
        if (byte == '\n') {
            lines_.push_back(std::move(pending_)); pending_.clear(); carriageReturn_ = false;
            if (lines_.size() > 4000) lines_.pop_front();
            continue;
        }
        if (byte < 32 && byte != '\t') continue;
        if (carriageReturn_) { pending_.clear(); carriageReturn_ = false; }
        if (pending_.size() >= 8192) { lines_.push_back(std::move(pending_)); pending_.clear(); }
        pending_ += static_cast<char>(byte);
    }
    ++revision_;
}

void ConsolePane::readOutput()
{
    char buffer[4096];
    DWORD count {};
    while (ReadFile(outputRead_, buffer, sizeof(buffer), &count, nullptr) && count)
        append(std::string_view(buffer, count));
    running_ = false;
}

bool ConsolePane::startSession(int shell)
{
    if (shell < 0 || shell > 1) return false;
    stopSession();
    return startPipeSession(shell);
}

bool ConsolePane::startPipeSession(int shell)
{
    clearOutput();
    const auto application = shellPath(shell);
    if (application.empty()) return false;
    SECURITY_ATTRIBUTES security {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead {}, outputWrite {};
    if (!CreatePipe(&inputRead, &inputWrite_, &security, 0)) return false;
    if (!CreatePipe(&outputRead_, &outputWrite, &security, 0)) {
        CloseHandle(inputRead); CloseHandle(inputWrite_); inputWrite_ = nullptr; return false;
    }
    SetHandleInformation(inputWrite_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead_, HANDLE_FLAG_INHERIT, 0);
    SIZE_T attributeBytes {};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
        CloseHandle(inputRead); CloseHandle(outputWrite); stopSession(); return false;
    }
    HANDLE inherited[] {inputRead, outputWrite};
    const bool configured = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
    STARTUPINFOEXW startup {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead;
    startup.StartupInfo.hStdOutput = outputWrite;
    startup.StartupInfo.hStdError = outputWrite;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process {};
    std::wstring arguments = L"\"" + application + L"\"" +
        (shell == 0 ? L" -NoLogo -NoProfile -NoExit" : L" /d /q /k");
    std::vector<wchar_t> commandLine(arguments.begin(), arguments.end());
    commandLine.push_back(L'\0');
    const bool launched = configured && CreateProcessW(application.c_str(), commandLine.data(),
        nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &process) != FALSE;
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(inputRead); CloseHandle(outputWrite);
    if (!launched) { stopSession(); return false; }
    CloseHandle(process.hThread);
    child_ = process.hProcess;
    activeShell_ = shell;
    running_ = true;
    reader_ = std::thread([this] { readOutput(); });
    if (shell == 0) {
        const char wake[] = "\r\n";
        DWORD written {};
        WriteFile(inputWrite_, wake, sizeof(wake) - 1, &written, nullptr);
    }
    return true;
}

void ConsolePane::stopSession()
{
    running_ = false;
    if (child_) TerminateProcess(child_, 1);
    if (reader_.joinable()) {
        CancelSynchronousIo(reader_.native_handle());
        reader_.join();
    }
    if (child_) { CloseHandle(child_); child_ = nullptr; }
    if (inputWrite_) { CloseHandle(inputWrite_); inputWrite_ = nullptr; }
    if (outputRead_) { CloseHandle(outputRead_); outputRead_ = nullptr; }
    activeShell_ = -1;
}

bool ConsolePane::executeCommand(const std::string& command, int shell)
{
    if (command.empty()) return false;
    if (activeShell_ != shell && !startSession(shell)) return false;
    if (!running_ || !inputWrite_) return false;
    const std::string input = command + (shell == 0 ? "\r\n\r\n" : "\r\n");
    DWORD written {};
    if (!WriteFile(inputWrite_, input.data(), static_cast<DWORD>(input.size()), &written, nullptr)
        || written != input.size()) return false;
    if (history_.empty() || history_.back() != command) history_.push_back(command);
    if (history_.size() > 100) history_.erase(history_.begin());
    historyIndex_ = -1;
    return true;
}

void ConsolePane::clearOutput()
{
    std::lock_guard lock(mutex_);
    lines_.clear(); pending_.clear(); displayedLines_.clear(); ++revision_;
}

std::string ConsolePane::output()
{
    std::lock_guard lock(mutex_);
    std::string result;
    for (const auto& line : lines_) result += line + "\n";
    result += pending_;
    return result;
}

int ConsolePane::historyCallback(ImGuiInputTextCallbackData* data)
{
    auto& self = *static_cast<ConsolePane*>(data->UserData);
    if (self.history_.empty()) return 0;
    if (self.historyIndex_ < 0) self.historyIndex_ = static_cast<int>(self.history_.size());
    if (data->EventKey == ImGuiKey_UpArrow) self.historyIndex_ = std::max(0, self.historyIndex_ - 1);
    if (data->EventKey == ImGuiKey_DownArrow)
        self.historyIndex_ = std::min(static_cast<int>(self.history_.size()), self.historyIndex_ + 1);
    const std::string value = self.historyIndex_ == self.history_.size() ? "" : self.history_[self.historyIndex_];
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, value.c_str());
    return 0;
}

void ConsolePane::draw(bool demoMode)
{
    if (!demoMode && activeShell_ < 0) startSession(shell_);
    const char* shells[] {"PowerShell", "CMD"};
    ImGui::SetNextItemWidth(cyui::dp(160.0f));
    if (ImGui::Combo("##consoleShell", &shell_, shells, 2) && !demoMode) startSession(shell_);
    ImGui::SameLine();
    if (cyui::button(ui_locale::french() ? "Redémarrer la session" : "Restart session",
            cyui::Variant::secondary) && !demoMode) startSession(shell_);
    ImGui::SameLine();
    if (cyui::button(ui_locale::french() ? "Effacer l'affichage" : "Clear view",
            cyui::Variant::secondary)) clearOutput();
    ImGui::SameLine();
    cyui::textDim(running_ ? (ui_locale::french() ? "Session active" : "Session active")
        : demoMode ? (ui_locale::french() ? "Démonstration" : "Demo mode")
        : (ui_locale::french() ? "Session indisponible" : "Session unavailable"));
    if (ImGui::BeginChild("##terminal", ImVec2(0, -cyui::dp(43.0f)), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        bool changed = false;
        {
            std::lock_guard lock(mutex_);
            if (revision_ != displayedRevision_) {
                displayedLines_ = lines_;
                if (!pending_.empty()) displayedLines_.push_back(pending_);
                displayedRevision_ = revision_;
                changed = true;
            }
        }
        cyui::ScopedFont mono(cyui::monoFont());
        if (demoMode) {
            ImGui::TextUnformatted(shell_ == 0 ? "PS> Get-Process" : "C:\\> tasklist");
            ImGui::TextUnformatted("Demo session · commands disabled");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(displayedLines_.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    ImGui::TextUnformatted(displayedLines_[i].c_str());
            if (changed && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - cyui::dp(50.0f))
                ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(demoMode || !running_);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##terminalInput", command_, sizeof(command_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
            historyCallback, this)) {
        if (executeCommand(command_, shell_)) command_[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
}

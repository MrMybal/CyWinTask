#include "ProcessTrust.h"

#include <windows.h>
#include <softpub.h>
#include <wintrust.h>

#include <algorithm>
#include <cwchar>

namespace {
std::wstring wide(const std::string& text)
{
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(),
        static_cast<int>(text.size()), result.data(), length);
    return result;
}
bool inside(const std::wstring& path, const std::wstring& directory)
{
    return !directory.empty() && path.size() > directory.size()
        && _wcsnicmp(path.c_str(), directory.c_str(), directory.size()) == 0
        && (directory.back() == L'\\' || path[directory.size()] == L'\\');
}
std::wstring envPath(const wchar_t* name)
{
    wchar_t buffer[32768] {};
    const auto length = GetEnvironmentVariableW(name, buffer, 32768);
    return length > 0 && length < 32768 ? std::wstring(buffer, length) : std::wstring {};
}
TrustResult inspect(const std::string& utf8Path)
{
    const auto path = wide(utf8Path);
    if (path.empty() || path.rfind(L"\\\\", 0) == 0)
        return { TrustState::unavailable, -1, "Non évalué", "Fichier distant ou chemin indisponible." };
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\'
        && GetDriveTypeW(path.substr(0, 3).c_str()) == DRIVE_REMOTE)
        return { TrustState::unavailable, -1, "Non évalué", "Fichier sur un lecteur réseau." };
    static const std::wstring windows = [] {
        wchar_t buffer[MAX_PATH] {};
        const auto length = GetWindowsDirectoryW(buffer, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : std::wstring {};
    }();
    static const std::wstring programFiles = envPath(L"ProgramFiles");
    static const std::wstring programFilesX86 = envPath(L"ProgramFiles(x86)");
    const bool system = inside(path, windows);
    const bool installed = inside(path, programFiles) || inside(path, programFilesX86);

    WINTRUST_FILE_INFO file {};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data {};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_IGNORE;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG code = WinVerifyTrust(nullptr, &action, &data);
    if (code == ERROR_SUCCESS)
        return { TrustState::verified, system ? 95 : installed ? 90 : 85,
            "Signature valide", system ? "Signature Authenticode valide · dossier Windows."
                : installed ? "Signature Authenticode valide · dossier d'installation."
                : "Signature Authenticode valide." };
    if (code == TRUST_E_NOSIGNATURE || code == TRUST_E_SUBJECT_FORM_UNKNOWN || code == TRUST_E_PROVIDER_UNKNOWN)
        return { TrustState::unsignedFile, system ? 55 : installed ? 40 : 25,
            "Signature non confirmée", system ? "Signature intégrée non confirmée · dossier Windows. Un catalogue système peut signer ce fichier."
                : installed ? "Signature intégrée non confirmée · dossier d'installation."
                : "Signature intégrée non confirmée." };
    if (code == TRUST_E_BAD_DIGEST || code == TRUST_E_EXPLICIT_DISTRUST
        || code == CERT_E_UNTRUSTEDROOT || code == TRUST_E_SUBJECT_NOT_TRUSTED)
        return { TrustState::invalid, 10, "Signature non valide", "La vérification Authenticode a échoué." };
    return { TrustState::error, -1, "Non évalué", "La vérification de signature n'a pas abouti." };
}
}

ProcessTrust::ProcessTrust() : worker_([this] { run(); }) {}
ProcessTrust::~ProcessTrust()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    ready_.notify_one();
    if (worker_.joinable()) worker_.join();
}
TrustResult ProcessTrust::get(const std::string& path)
{
    if (path.empty()) return {};
    std::lock_guard lock(mutex_);
    if (const auto found = results_.find(path); found != results_.end()) return found->second;
    if (results_.size() >= 4096 || queue_.size() >= 1024)
        return { TrustState::unavailable, -1, "Non évalué", "Limite du cache atteinte." };
    results_.emplace(path, TrustResult { TrustState::pending, -1, "Analyse...", "Vérification locale en cours." });
    queue_.push_back(path);
    ready_.notify_one();
    return { TrustState::pending, -1, "Analyse...", "Vérification locale en cours." };
}
void ProcessTrust::run()
{
    while (true) {
        std::string path;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            path = std::move(queue_.front());
            queue_.pop_front();
        }
        const auto result = inspect(path);
        std::lock_guard lock(mutex_);
        results_[path] = result;
    }
}

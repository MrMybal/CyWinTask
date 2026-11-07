#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

enum class TrustState { unavailable, pending, verified, unsignedFile, invalid, error };

struct TrustResult {
    TrustState state { TrustState::unavailable };
    int score { -1 };
    const char* label { "Non évalué" };
    const char* explanation { "Chemin du fichier indisponible." };
};

class ProcessTrust {
public:
    ProcessTrust();
    ~ProcessTrust();
    ProcessTrust(const ProcessTrust&) = delete;
    ProcessTrust& operator=(const ProcessTrust&) = delete;
    TrustResult get(const std::string& utf8Path);

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> queue_;
    std::unordered_map<std::string, TrustResult> results_;
    std::thread worker_;
    bool stopping_ {};
    void run();
};

#include "McpAccess.h"
#include <windows.h>
#include <fstream>
#include <stdexcept>
#include <string>

namespace mcp_access {
const Option& option(cymcp::LocalAccessProfile profile)
{
    for (const auto& entry : options) if (entry.profile == profile) return entry;
    throw cymcp::Error("InvalidOptions", "Unknown local MCP access profile");
}

cymcp::LocalAccessProfile parse(std::string_view id)
{
    for (const auto& entry : options) if (entry.id == id) return entry.profile;
    if (id == "protected_access") return cymcp::LocalAccessProfile::protected_access;
    throw cymcp::Error("InvalidOptions", "Unknown local MCP access profile");
}

cymcp::LocalAccessProfile fromConfig(const cymcp::Json& config)
{
    if (!config.is_object()) throw cymcp::Error("InvalidOptions", "Invalid MCP configuration");
    if (!config.contains("access_profile")) return defaultProfile;
    if (!config.at("access_profile").is_string())
        throw cymcp::Error("InvalidOptions", "MCP access profile must be a string");
    return parse(config.at("access_profile").get<std::string>());
}

std::filesystem::path configPath()
{
    wchar_t value[32768] {};
    DWORD count = GetEnvironmentVariableW(L"CYWINTASK_MCP_CONFIG", value, 32768);
    if (count > 0 && count < 32768) return value;
    count = GetEnvironmentVariableW(L"LOCALAPPDATA", value, 32768);
    if (count == 0 || count >= 32768) throw cymcp::Error("Unavailable", "LOCALAPPDATA unavailable");
    return std::filesystem::path(value) / L"CyWinTask" / L"mcp-access.json";
}

cymcp::LocalAccessProfile load(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) return defaultProfile;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw cymcp::Error("Unavailable", "Cannot read MCP access configuration");
    try {
        const auto config = cymcp::Json::parse(input);
        const auto profile = fromConfig(config);
        if (!config.contains("access_profile")) {
            input.close();
            save(path, profile);
        }
        return profile;
    }
    catch (const cymcp::Error&) { throw; }
    catch (...) { throw cymcp::Error("InvalidOptions", "Invalid MCP access configuration"); }
}

void save(const std::filesystem::path& path, cymcp::LocalAccessProfile profile)
{
    cymcp::Json config = cymcp::Json::object();
    if (std::filesystem::exists(path)) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw cymcp::Error("Unavailable", "Cannot read MCP access configuration");
        try { config = cymcp::Json::parse(input); }
        catch (...) { config = cymcp::Json::object(); }
        if (!config.is_object()) config = cymcp::Json::object();
    }
    config["access_profile"] = option(profile).id;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!output) throw cymcp::Error("Unavailable", "Cannot write MCP access configuration");
        output << config.dump(2) << '\n';
        if (!output) throw cymcp::Error("Unavailable", "Cannot write MCP access configuration");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw cymcp::Error("Unavailable", "Cannot replace MCP access configuration");
    }
}
}

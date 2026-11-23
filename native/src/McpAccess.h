#pragma once

#include <cymcp/core.hpp>
#include <cymcp/http.hpp>
#include <filesystem>
#include <string_view>

namespace mcp_access {
struct Option {
    cymcp::LocalAccessProfile profile;
    std::string_view id;
    const char* label;
};

inline constexpr Option options[] {
    {cymcp::LocalAccessProfile::free, "free", "Free"},
    {cymcp::LocalAccessProfile::local_controlled, "local_controlled", "Local contrôlé"},
    {cymcp::LocalAccessProfile::token_only, "token_only", "Jeton seul"},
    {cymcp::LocalAccessProfile::protected_access, "protected", "Protégé"},
};

// Used when nothing was chosen (no file, or a file without a profile).
// Local controlled: loopback address only, per-tool permissions, and ending a
// process still asks for confirmation.
inline constexpr cymcp::LocalAccessProfile defaultProfile = cymcp::LocalAccessProfile::local_controlled;

const Option& option(cymcp::LocalAccessProfile profile);
cymcp::LocalAccessProfile parse(std::string_view id);
cymcp::LocalAccessProfile fromConfig(const cymcp::Json& config);
std::filesystem::path configPath();
cymcp::LocalAccessProfile load(const std::filesystem::path& path);
void save(const std::filesystem::path& path, cymcp::LocalAccessProfile profile);
}

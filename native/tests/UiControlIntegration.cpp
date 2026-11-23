#include "../src/TaskService.h"
#include "../src/McpAccess.h"
#include "../src/UiBridge.h"
#include "../src/UiOperations.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <fstream>
#include <thread>

int main()
{
    try {
        using cymcp::LocalAccessProfile;
        if (mcp_access::fromConfig(cymcp::Json::object()) != LocalAccessProfile::local_controlled ||
            mcp_access::fromConfig({{"old_setting", true}}) != LocalAccessProfile::local_controlled ||
            mcp_access::fromConfig({{"access_profile", "free"}}) != LocalAccessProfile::free ||
            mcp_access::fromConfig({{"access_profile", "protected"}}) != LocalAccessProfile::protected_access ||
            mcp_access::fromConfig({{"access_profile", "local_controlled"}}) != LocalAccessProfile::local_controlled ||
            mcp_access::fromConfig({{"access_profile", "token_only"}}) != LocalAccessProfile::token_only)
            throw std::runtime_error("MCP profile migration or explicit choice failed");
        const auto configPath = std::filesystem::temp_directory_path() /
            ("cywintask-mcp-profile-test-" + std::to_string(GetCurrentProcessId()) + ".json");
        {
            std::ofstream legacy(configPath);
            legacy << "{\"old_setting\":true}";
        }
        if (mcp_access::load(configPath) != LocalAccessProfile::local_controlled)
            throw std::runtime_error("Legacy MCP configuration was not migrated to Local controlled");
        {
            std::ifstream migrated(configPath);
            const auto contents = cymcp::Json::parse(migrated);
            if (contents.at("access_profile") != "local_controlled" || contents.at("old_setting") != true)
                throw std::runtime_error("MCP migration did not preserve other configuration");
        }
        mcp_access::save(configPath, LocalAccessProfile::protected_access);
        if (mcp_access::load(configPath) != LocalAccessProfile::protected_access)
            throw std::runtime_error("Explicit MCP profile was not preserved");
        std::filesystem::remove(configPath);
        TaskService service;
        auto bridge = std::make_shared<UiBridge>();
        registerUiOperations(service, bridge);
        const auto catalogue = service.registry()->describe();
        bool found = false;
        for (const auto& tool : catalogue) if (tool.at("name") == "ui.navigate") found = true;
        if (!found || !service.isAllowed("ui.state") || service.isAllowed("ui.navigate"))
            throw std::runtime_error("Wrong UI operation defaults");
        const auto rights = service.registry()->invoke("permissions.list", cymcp::Json::object());
        bool navigationDenied = false;
        for (const auto& item : rights.at("items"))
            if (item.at("name") == "ui.navigate" && !item.at("allowed").get<bool>())
                navigationDenied = true;
        if (!navigationDenied) throw std::runtime_error("Permission catalogue omitted UI denial");
        try {
            service.registry()->invoke("ui.navigate", {{"page", "performance"}});
            throw std::runtime_error("Unauthorized navigation accepted");
        } catch (const cymcp::Error& error) {
            if (error.code != "AccessDenied") throw;
        }
        service.setAllowed("ui.navigate", true);
        auto action = std::async(std::launch::async, [&] {
            return service.registry()->invoke("ui.navigate", {{"page", "performance"}});
        });
        for (int i = 0; i < 100 && action.wait_for(std::chrono::milliseconds(0))
                != std::future_status::ready; ++i) {
            bridge->drain([](const std::string& operation, const cymcp::Json& args, bool unrestricted) {
                if (operation != "ui.navigate") throw std::runtime_error("Wrong UI operation");
                if (unrestricted) throw std::runtime_error("Controlled UI command marked unrestricted");
                return cymcp::Json{{"page", args.at("page")}};
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (action.get().at("page") != "performance") throw std::runtime_error("UI dispatch failed");
        auto revoked = std::async(std::launch::async, [&] {
            return service.registry()->invoke("ui.navigate", {{"page", "processes"}});
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        service.setAllowed("ui.navigate", false);
        if (service.isAllowed("ui.navigate")) throw std::runtime_error("Revocation failed");
        bridge->drain([&](const std::string& operation, const cymcp::Json&, bool unrestricted) -> cymcp::Json {
            if (!unrestricted && !service.isAllowed(operation))
                throw cymcp::Error("AccessDenied", "UI tool permission was revoked");
            return cymcp::Json::object();
        });
        try {
            revoked.get();
            throw std::runtime_error("Queued command bypassed revocation");
        } catch (const cymcp::Error& error) {
            if (error.code != "AccessDenied") throw;
        }
        cymcp::CallContext freeContext;
        freeContext.principal = unrestrictedMcpPrincipal;
        auto freeCommand = std::async(std::launch::async, [&] {
            return service.unrestrictedRegistry()->invoke("ui.navigate", {{"page", "assistant"}}, freeContext);
        });
        for (int i = 0; i < 100 && freeCommand.wait_for(std::chrono::milliseconds(0))
                != std::future_status::ready; ++i) {
            bridge->drain([](const std::string&, const cymcp::Json& args, bool unrestricted) {
                if (!unrestricted) throw std::runtime_error("Free command was not marked unrestricted");
                return cymcp::Json{{"page", args.at("page")}};
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (freeCommand.get().at("page") != "assistant")
            throw std::runtime_error("Free UI command failed");
        try {
            service.unrestrictedRegistry()->invoke("ui.navigate", {{"page", "invalid"}}, freeContext);
            throw std::runtime_error("Free profile bypassed argument validation");
        } catch (const cymcp::Error& error) {
            if (error.code != "InvalidParameters") throw;
        }
        bridge->close();
        std::cout << "PASS MCP profile migration, controlled UI, Free UI and argument validation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

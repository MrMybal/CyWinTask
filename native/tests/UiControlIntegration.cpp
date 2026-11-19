#include "../src/TaskService.h"
#include "../src/UiBridge.h"
#include "../src/UiOperations.h"
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

int main()
{
    try {
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
            bridge->drain([](const std::string& operation, const cymcp::Json& args) {
                if (operation != "ui.navigate") throw std::runtime_error("Wrong UI operation");
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
        bridge->drain([&](const std::string& operation, const cymcp::Json&) -> cymcp::Json {
            if (!service.isAllowed(operation))
                throw cymcp::Error("AccessDenied", "UI tool permission was revoked");
            return cymcp::Json::object();
        });
        try {
            revoked.get();
            throw std::runtime_error("Queued command bypassed revocation");
        } catch (const cymcp::Error& error) {
            if (error.code != "AccessDenied") throw;
        }
        bridge->close();
        std::cout << "PASS UI catalogue, default denial, dispatch and revocation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

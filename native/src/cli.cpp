#include "TaskService.h"
#include "McpAccess.h"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
void help()
{
    std::cout << "CyWinTask CLI\n"
              << "  CyWinTaskCli.exe list\n"
              << "  CyWinTaskCli.exe describe NAME\n"
              << "  CyWinTaskCli.exe invoke NAME '{JSON}' [--allow-stop] [--allow-details]\n"
              << "  CyWinTaskCli.exe mcp [--allow-stop] [--allow-details]  (MCP over stdio)\n"
              << "  CyWinTaskCli.exe serve [PORT] [--profile free|local_controlled|token_only|protected] [--allow-stop] [--allow-details]\n"
              << "  CyWinTaskCli.exe remote URL list|describe NAME|invoke NAME '{JSON}'\n"
              << "HTTP serve defaults to Free: 127.0.0.1 address only, all registered operations.\n"
              << "Token only and Protected require CYWINTASK_MCP_TOKEN (32+ characters).\n"
              << "Stopping a process also requires confirm:true and its creation timestamp.\n";
}
}

int main(int argc, char** argv)
{
    using namespace cymcp;
    try {
        const std::string command = argc > 1 ? argv[1] : "help";
        if (command == "help" || command == "--help" || command == "-h") { help(); return 0; }
        if (command == "remote") {
            if (argc < 4) { std::cerr << "Usage: remote URL list|describe NAME|invoke NAME '{JSON}'\n"; return 2; }
            const std::string url = argv[2], action = argv[3];
            if (url.rfind("http://127.0.0.1:", 0) != 0 || url.find("/mcp") == std::string::npos)
                throw Error("InvalidOptions", "Remote UI URL must be a 127.0.0.1 MCP endpoint");
            const char* token = std::getenv("CYWINTASK_MCP_TOKEN");
            McpClient client(url, token ? token : "");
            client.connect();
            Json result;
            if (action == "list") result = client.list_tools();
            else if (action == "describe" && argc >= 5) result = client.get_tool(argv[4]);
            else if (action == "invoke" && argc >= 6)
                result = client.call_tool(argv[4], parse_json(argv[5]));
            else { std::cerr << "Unknown remote action or missing argument. Use --help.\n"; return 2; }
            std::cout << result.dump() << '\n';
            return result.is_object() && result.value("isError", false) ? 1 : 0;
        }
        TaskService service;
        const bool allowStop = std::find_if(argv + 1, argv + argc, [](const char* arg) {
            return std::string(arg) == "--allow-stop";
        }) != argv + argc;
        service.allowStop(allowStop);
        const bool allowDetails = std::find_if(argv + 1, argv + argc, [](const char* arg) {
            return std::string(arg) == "--allow-details";
        }) != argv + argc;
        service.setAllowed("process.details", allowDetails);
        if (command == "list") { std::cout << service.registry()->describe().dump(2) << '\n'; return 0; }
        if (command == "describe" && argc >= 3) {
            std::cout << service.registry()->describe(argv[2]).dump(2) << '\n'; return 0;
        }
        if (command != "invoke" && command != "mcp" && command != "serve") {
            std::cerr << "Unknown command or missing argument. Use --help.\n";
            return 2;
        }
        if (!service.start()) throw Error("Unavailable", service.startupError());
        if (command == "invoke") {
            if (argc < 4) { std::cerr << "Usage: invoke NAME '{JSON}' [--allow-stop]\n"; return 2; }
            const Json arguments = parse_json(argv[3]);
            std::cout << service.registry()->invoke(argv[2], arguments).dump() << '\n';
            return 0;
        }
        if (command == "mcp") {
            McpServer server(*service.registry(), "CyWinTask");
            server.serve_stdio(std::cin, std::cout);
            return 0;
        }
        LocalAccessProfile profile = LocalAccessProfile::free;
        int port = 0;
        bool portSet = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--profile" && i + 1 < argc) profile = mcp_access::parse(argv[++i]);
            else if (arg == "--allow-stop" || arg == "--allow-details") continue;
            else if (!portSet && !arg.empty() && arg[0] != '-') {
                std::size_t parsed {};
                port = std::stoi(arg, &parsed);
                if (parsed != arg.size() || port < 0 || port > 65535)
                    throw Error("InvalidOptions", "Invalid MCP port");
                portSet = true;
            } else throw Error("InvalidOptions", "Unknown serve option");
        }
        const auto access = local_access_settings(profile);
        const char* rawToken = std::getenv("CYWINTASK_MCP_TOKEN");
        if (access.require_token && (!rawToken || std::string(rawToken).size() < 32))
            throw Error("AuthenticationRequired", "Set CYWINTASK_MCP_TOKEN to a random 32+ character token");
        McpServer server(*(access.control_operations ? service.registry() : service.unrestrictedRegistry()), "CyWinTask");
        auto endpoint = [&](const Json& request, const CallContext& context) {
            auto call = context;
            if (!access.control_operations) call.principal = unrestrictedMcpPrincipal;
            return server.handle(request, call);
        };
        std::unique_ptr<LocalHttpServer> http = access.require_token
            ? std::make_unique<LocalHttpServer>(rawToken, endpoint)
            : std::make_unique<LocalHttpServer>(LocalHttpServer::UnauthenticatedLoopback{}, endpoint);
        port = http->start(port);
        std::cerr << "MCP listening at http://127.0.0.1:" << port << "/mcp\n";
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        while (!stopped) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        http->stop();
        return 0;
    } catch (const Error& error) {
        std::cerr << Json{{"error", {{"code", error.code}, {"message", error.what()}}}}.dump() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << Json{{"error", {{"code", "Failure"}, {"message", error.what()}}}}.dump() << '\n';
        return 1;
    }
}

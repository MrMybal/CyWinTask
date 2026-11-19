#include "AssistantPane.h"
#include "TaskService.h"
#include "UiBridge.h"
#include "UiOperations.h"
#include <cymcp/chat_ui.hpp>
#include <cyui/Core.h>
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <iterator>
#include <mutex>
#include <optional>

using namespace cymcp;

namespace {
ChatPanelOptions chatPanelOptions()
{
    ChatPanelOptions options;
    options.pick_attachments = [] {
        wchar_t selected[32768] {};
        OPENFILENAMEW dialog {sizeof(dialog)};
        dialog.hwndOwner = GetActiveWindow();
        dialog.lpstrFilter = L"Tous les fichiers\0*.*\0\0";
        dialog.lpstrFile = selected;
        dialog.nMaxFile = static_cast<DWORD>(std::size(selected));
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return std::vector<std::filesystem::path>{};
        return std::vector<std::filesystem::path>{std::filesystem::path(selected)};
    };
    return options;
}
}

struct AssistantPane::State {
    struct PendingStop {
        std::string name;
        int pid {};
        bool done {}, approved {};
    };
    bool demo = false;
    std::shared_ptr<UiBridge> ui;
    std::mutex confirmationMutex;
    std::condition_variable confirmationReady;
    std::deque<std::shared_ptr<PendingStop>> pendingStops;
    bool confirmationOpen {}, closing {};
    TaskService tasks;
    std::shared_ptr<ProviderRegistry> providers = std::make_shared<ProviderRegistry>();
    Json providerCatalogue = providers->describe();
    ChatController chat{providers};
    ChatPanel panel{chat, chatPanelOptions()};
    std::unique_ptr<McpServer> mcp;
    std::unique_ptr<LocalHttpServer> http;
    std::future<std::vector<DiscoveredConnection>> discovery;
    std::vector<DiscoveredConnection> detected;
    std::string providerId = "ollama", token, error;
    char model[128] {}, address[256] {}, keyEnvironment[80] {};
    bool saveHistory = false, restoreHistory = false;
    int contextKilobytes = 0;
    int port = 0;

    explicit State(bool demonstration, std::shared_ptr<UiBridge> uiBridge)
        : demo(demonstration), ui(std::move(uiBridge))
    {
        if (ui) registerUiOperations(tasks, ui);
        tasks.setConfirmation([this](const ProcessInfo& process, const CallContext& context) {
            auto pending = std::make_shared<PendingStop>();
            pending->name = process.name;
            pending->pid = process.id;
            std::unique_lock lock(confirmationMutex);
            if (closing) return false;
            pendingStops.push_back(pending);
            try {
                while (!pending->done) {
                    lock.unlock();
                    context.cancellation.check();
                    lock.lock();
                    confirmationReady.wait_for(lock, std::chrono::milliseconds(50));
                }
            } catch (...) {
                if (!lock.owns_lock()) lock.lock();
                pending->done = true;
                confirmationReady.notify_all();
                throw;
            }
            return pending->approved;
        });
    }
    ~State()
    {
        {
            std::lock_guard lock(confirmationMutex);
            closing = true;
            for (auto& pending : pendingStops) pending->done = true;
        }
        confirmationReady.notify_all();
        stopServer();
    }

    void drawConfirmation()
    {
        std::shared_ptr<PendingStop> pending;
        {
            std::lock_guard lock(confirmationMutex);
            while (!pendingStops.empty() && pendingStops.front()->done) pendingStops.pop_front();
            if (!pendingStops.empty()) pending = pendingStops.front();
        }
        constexpr const char* title = "Confirmer l'arrêt du processus##assistantStop";
        if (!pending) {
            if (confirmationOpen && ImGui::BeginPopupModal(title)) {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            confirmationOpen = false;
            return;
        }
        if (!confirmationOpen) { ImGui::OpenPopup(title); confirmationOpen = true; }
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!ImGui::IsPopupOpen(title)) {
                {
                    std::lock_guard lock(confirmationMutex);
                    pending->done = true;
                }
                confirmationReady.notify_all();
                confirmationOpen = false;
            }
            return;
        }
        ImGui::TextUnformatted("Un outil demande l'arrêt de ce processus :");
        ImGui::Text("%s  (PID %d)", pending->name.c_str(), pending->pid);
        ImGui::TextWrapped("CyWinTask vérifiera encore son identité avant l'arrêt.");
        ImGui::Spacing();
        bool approved = false, answered = false;
        if (cyui::button("Arrêter ce processus", cyui::Variant::danger)) {
            approved = true; answered = true;
        }
        ImGui::SameLine();
        if (cyui::button("Annuler", cyui::Variant::secondary)) answered = true;
        if (answered) {
            {
                std::lock_guard lock(confirmationMutex);
                pending->approved = approved;
                pending->done = true;
            }
            confirmationReady.notify_all();
            ImGui::CloseCurrentPopup();
            confirmationOpen = false;
        }
        ImGui::EndPopup();
    }

    void stopServer()
    {
        if (http) http->stop();
        http.reset();
        mcp.reset();
        token.clear();
        port = 0;
    }
    void startServer()
    {
        if (http) return;
        if (!tasks.start()) throw Error("Unavailable", tasks.startupError());
        token = random_token();
        mcp = std::make_unique<McpServer>(*tasks.registry(), "CyWinTask");
        auto server = std::make_unique<LocalHttpServer>(token,
            [this](const Json& request, const CallContext& context) { return mcp->handle(request, context); });
        port = server->start();
        http = std::move(server);
    }
    std::optional<DiscoveredConnection> detectedProvider() const
    {
        for (const auto& value : detected)
            if (value.provider_id == providerId && value.available()) return value;
        return std::nullopt;
    }
    void connect()
    {
        if (!tasks.start()) throw Error("Unavailable", tasks.startupError());
        ChatConnection connection;
        connection.provider_id = providerId;
        connection.model = model;
        connection.system_prompt = "Tu aides à analyser les processus et les performances de CyWinTask. "
            "Utilise les outils de lecture pour répondre avec des mesures actuelles. "
            "Ne demande jamais l'arrêt d'un processus sans une instruction explicite de l'utilisateur. "
            "Les mesures peuvent être indisponibles et les PID réutilisés.";
        connection.automatic_model = connection.model.empty()
            && (providerId == "ollama" || providerId == "lmstudio" || providerId == "llama.cpp");
        if (contextKilobytes > 0)
            connection.context.max_bytes = static_cast<std::size_t>(std::clamp(contextKilobytes, 64, 4096)) * 1024;
        if (auto found = detectedProvider()) connection.provider = found->config;
        connection.provider.tools = tasks.registry();
        connection.provider.approvals = Policy::deny_all();
        if (address[0]) connection.provider.settings["base_url"] = address;
        if (keyEnvironment[0]) {
            const std::string variable = keyEnvironment;
            if (variable.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")
                != std::string::npos) throw Error("InvalidOptions", "Nom de variable de clé invalide");
            if (const char* key = std::getenv(variable.c_str())) connection.provider.api_key = key;
        }
        if (saveHistory) {
            wchar_t folder[32768] {};
            const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, 32768);
            if (count == 0 || count >= 32768) throw Error("Unavailable", "LOCALAPPDATA indisponible");
            connection.history_directory = std::filesystem::path(folder) / L"CyWinTask" / L"chat";
            connection.history_id = "assistant";
            connection.restore = restoreHistory;
        }
        chat.open(std::move(connection));
    }
    void pollDiscovery()
    {
        if (!discovery.valid() || discovery.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try { detected = discovery.get(); }
        catch (const std::exception& ex) { error = redact_error_message(ex.what()); }
    }
    void draw()
    {
        chat.poll();
        pollDiscovery();
        cyui::heading("Assistant", 2);
        ImGui::SameLine();
        cyui::textDim("CyMCPSDK · lecture et contrôle des processus");
        if (demo) cyui::textDim("Démonstration : connexion et serveur désactivés ; aucune donnée réelle chargée.");
        ImGui::Separator();
        ImGui::BeginDisabled(demo);
        ImGui::TextUnformatted("Fournisseur");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cyui::dp(260.0f));
        if (ImGui::BeginCombo("##assistantProvider", providerId.c_str())) {
            for (const auto& entry : providerCatalogue) {
                const std::string id = entry.at("id").get<std::string>();
                const std::string label = entry.at("name").get<std::string>() + " (" + id + ")";
                if (ImGui::Selectable(label.c_str(), id == providerId)) providerId = id;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (cyui::button("Détecter", cyui::Variant::secondary) && !discovery.valid()) {
            discovery = std::async(std::launch::async, [registry = providers] { return registry->discover(); });
        }
        if (discovery.valid()) { ImGui::SameLine(); cyui::textDim("Recherche en cours..."); }
        if (auto found = detectedProvider()) {
            ImGui::SameLine(); cyui::textColoured(cyui::palette().success, "Disponible");
        }
        ImGui::TextUnformatted("Modèle");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cyui::dp(360.0f));
        ImGui::InputTextWithHint("##assistantModel", "ID exact ; vide = choix automatique local", model, sizeof(model));
        ImGui::SameLine();
        ImGui::TextUnformatted("URL");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cyui::dp(420.0f));
        ImGui::InputTextWithHint("##assistantAddress", "Adresse du fournisseur (facultative)", address, sizeof(address));
        ImGui::TextUnformatted("Clé d'environnement");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cyui::dp(280.0f));
        ImGui::InputTextWithHint("##assistantKey", "Ex. OPENAI_API_KEY", keyEnvironment, sizeof(keyEnvironment));
        ImGui::Checkbox("Sauver l'historique", &saveHistory);
        ImGui::SameLine();
        ImGui::BeginDisabled(!saveHistory);
        ImGui::Checkbox("Restaurer la session", &restoreHistory);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cyui::dp(90.0f));
        ImGui::InputInt("Contexte (Kio, 0 = complet)", &contextKilobytes);
        contextKilobytes = std::clamp(contextKilobytes, 0, 4096);
        if (cyui::button("Connecter", cyui::Variant::primary)) {
            try { error.clear(); connect(); }
            catch (const std::exception& ex) { error = redact_error_message(ex.what()); }
        }
        ImGui::SameLine();
        if (cyui::button("Annuler", cyui::Variant::secondary)) chat.cancel();
        ImGui::SameLine();
        if (cyui::button("Déconnecter", cyui::Variant::secondary)) chat.disconnect();
        ImGui::SameLine();
        cyui::textDim("%s", chat.state().phase.c_str());
        if (!error.empty()) cyui::textColoured(cyui::palette().error, "%s", error.c_str());
        if (!chat.state().error_message.empty())
            cyui::textColoured(cyui::palette().error, "%s", chat.state().error_message.c_str());
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Droits des outils")) {
            cyui::textDim("Droits de cette session. Un arrêt demande toujours une confirmation par processus.");
            const auto catalogue = tasks.registry()->describe();
            if (cyui::button("Lecture seule", cyui::Variant::secondary)) {
                for (const auto& entry : catalogue) {
                    const auto name = entry.at("name").get<std::string>();
                    tasks.setAllowed(name, name == "permissions.list"
                        || name == "system.summary" || name == "system.devices"
                        || name == "process.list" || name == "process.get"
                        || name == "ui.state" || name == "ui.performance.history");
                }
            }
            ImGui::SameLine();
            if (cyui::button("Contrôle de l'interface", cyui::Variant::secondary)) {
                for (const auto& entry : catalogue) {
                    const auto name = entry.at("name").get<std::string>();
                    if (name.rfind("ui.", 0) == 0) tasks.setAllowed(name, true);
                }
            }
            ImGui::SameLine();
            if (cyui::button("Tout autoriser", cyui::Variant::danger))
                for (const auto& entry : catalogue)
                    tasks.setAllowed(entry.at("name").get<std::string>(), true);
            for (const auto& entry : catalogue) {
                const std::string name = entry.at("name").get<std::string>();
                bool allowed = tasks.isAllowed(name);
                if (ImGui::Checkbox(name.c_str(), &allowed)) tasks.setAllowed(name, allowed);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.at("description").get<std::string>().c_str());
            }
        }
        bool exposed = http != nullptr;
        if (ImGui::Checkbox("Serveur MCP local", &exposed)) {
            try { error.clear(); if (exposed) startServer(); else stopServer(); }
            catch (const std::exception& ex) { stopServer(); error = redact_error_message(ex.what()); }
        }
        if (http) {
            const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
            cyui::textDim("%s", url.c_str());
            ImGui::SameLine();
            if (cyui::button("Copier l'adresse", cyui::Variant::secondary)) ImGui::SetClipboardText(url.c_str());
            ImGui::SameLine();
            if (cyui::button("Copier le jeton", cyui::Variant::secondary)) ImGui::SetClipboardText(token.c_str());
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::BeginDisabled(demo);
        panel.draw("CyWinTask Assistant", 0, ImGui::GetContentRegionAvail().y);
        ImGui::EndDisabled();
    }
};

AssistantPane::AssistantPane(bool demo, std::shared_ptr<UiBridge> ui)
    : state_(std::make_unique<State>(demo, std::move(ui))) {}
AssistantPane::~AssistantPane() = default;
void AssistantPane::draw() { state_->draw(); }
void AssistantPane::drawConfirmation() { state_->drawConfirmation(); }
bool AssistantPane::allows(const std::string& operation) const { return state_->tasks.isAllowed(operation); }

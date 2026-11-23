#include "AssistantPane.h"
#include "McpAccess.h"
#include "UiLocale.h"
#include "TaskService.h"
#include "UiBridge.h"
#include "UiOperations.h"
#include "UiKit.h"
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
        dialog.lpstrFilter = ui_locale::french() ? L"Tous les fichiers\0*.*\0\0" : L"All files\0*.*\0\0";
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
    cymcp::LocalAccessProfile accessProfile = mcp_access::defaultProfile;
    std::filesystem::path accessConfigPath;
    bool accessConfigValid = true;
    char model[128] {}, address[256] {}, keyEnvironment[80] {};
    bool saveHistory = false, restoreHistory = false;
    int contextKilobytes = 0;
    int port = 0;

    explicit State(bool demonstration, std::shared_ptr<UiBridge> uiBridge)
        : demo(demonstration), ui(std::move(uiBridge))
    {
        if (!demo) {
            try {
                accessConfigPath = mcp_access::configPath();
                accessProfile = mcp_access::load(accessConfigPath);
            } catch (const std::exception& ex) {
                accessConfigValid = false;
                error = redact_error_message(ex.what());
            }
        }
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
        const char* title = ui_locale::tr("Confirmer l'arrêt du processus##assistantStop");
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
        ImGui::TextUnformatted(ui_locale::tr("Un outil demande l'arrêt de ce processus :"));
        ImGui::Text("%s  (PID %d)", pending->name.c_str(), pending->pid);
        ImGui::TextWrapped(ui_locale::tr("CyWinTask vérifiera encore son identité avant l'arrêt."));
        ImGui::Spacing();
        bool approved = false, answered = false;
        if (cyui::button(ui_locale::tr("Arrêter ce processus"), cyui::Variant::danger)) {
            approved = true; answered = true;
        }
        ImGui::SameLine();
        if (cyui::button(ui_locale::tr("Annuler"), cyui::Variant::secondary)) answered = true;
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
    void startServer(int requestedPort = 0)
    {
        if (http) return;
        if (demo) throw Error("Unavailable", "MCP is disabled in demo mode");
        if (!accessConfigValid) throw Error("InvalidOptions", "Choose a valid MCP access profile first");
        if (!tasks.start()) throw Error("Unavailable", tasks.startupError());
        const auto access = local_access_settings(accessProfile);
        token.clear();
        if (access.require_token) {
            if (const char* configured = std::getenv("CYWINTASK_MCP_TOKEN")) {
                if (std::string(configured).size() < 32)
                    throw Error("InvalidOptions", "CYWINTASK_MCP_TOKEN must contain at least 32 characters");
                token = configured;
            } else token = random_token();
        }
        mcp = std::make_unique<McpServer>(
            *(access.control_operations ? tasks.registry() : tasks.unrestrictedRegistry()), "CyWinTask");
        auto endpoint = [this, unrestricted = !access.control_operations]
            (const Json& request, const CallContext& context) {
                auto call = context;
                if (unrestricted) call.principal = unrestrictedMcpPrincipal;
                return mcp->handle(request, call);
            };
        auto server = access.require_token
            ? std::make_unique<LocalHttpServer>(token, endpoint)
            : std::make_unique<LocalHttpServer>(LocalHttpServer::UnauthenticatedLoopback{}, endpoint);
        port = server->start(requestedPort);
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
        connection.system_prompt = ui_locale::french()
            ? "Tu aides à analyser les processus et les performances de CyWinTask. "
              "Utilise les outils de lecture pour répondre avec des mesures actuelles. "
              "Ne demande jamais l'arrêt d'un processus sans une instruction explicite de l'utilisateur. "
              "Les mesures peuvent être indisponibles et les PID réutilisés."
            : "You help analyze CyWinTask processes and performance. "
              "Use read tools to answer with current measurements. "
              "Never request process termination without an explicit user instruction. "
              "Measurements may be unavailable and PIDs may be reused.";
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
                != std::string::npos) throw Error("InvalidOptions", ui_locale::french()
                    ? "Nom de variable de clé invalide" : "Invalid key environment variable name");
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
        using ui_kit::pick;
        chat.poll();
        pollDiscovery();
        const auto& state = chat.state();
        const char* phase = pick("Déconnecté", "Disconnected");
        auto tone = cyui::Tone::neutral;
        if (state.phase == "ready") { phase = pick("Connecté", "Connected"); tone = cyui::Tone::success; }
        else if (state.phase == "busy") { phase = pick("Réponse en cours", "Working"); tone = cyui::Tone::info; }
        else if (state.phase == "connecting") { phase = pick("Connexion…", "Connecting…"); tone = cyui::Tone::info; }
        else if (state.phase == "needs_reconnect") { phase = pick("Reconnexion requise", "Reconnect needed"); tone = cyui::Tone::warning; }
        else if (state.phase == "failed") { phase = pick("Échec", "Failed"); tone = cyui::Tone::error; }
        const std::string server = http ? "MCP · 127.0.0.1:" + std::to_string(port) : std::string();
        ui_kit::beginPageHeader("Assistant", ui_locale::tr("CyMCPSDK · lecture et contrôle des processus"),
            cyui::dp(server.empty() ? 160.0f : 380.0f));
        {
            float width = ImGui::CalcTextSize(phase).x + cyui::dp(28.0f);
            if (!server.empty()) width += ImGui::CalcTextSize(server.c_str()).x + cyui::dp(28.0f);
            cyui::alignRight(width);
            const float top = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(top + ImGui::GetStyle().FramePadding.y);
            if (!server.empty()) {
                ui_kit::dotText(cyui::palette().accent, server.c_str());
                ImGui::SameLine(0.0f, cyui::dp(14.0f));
            }
            ui_kit::dotText(tone == cyui::Tone::neutral ? cyui::palette().textFaint : cyui::toneColour(tone), phase);
        }
        ui_kit::endPageHeader();
        if (demo) {
            cyui::callout(cyui::Tone::info, ui_locale::tr("Démonstration : connexion et serveur désactivés ; aucune donnée réelle chargée."));
            cyui::space(6.0f);
        }
        const float height = ImGui::GetContentRegionAvail().y;
        const float settingsWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, cyui::dp(340.0f), cyui::dp(440.0f));
        cyui::ScopedVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(cyui::dp(5.0f), 0.0f));
        if (!ImGui::BeginTable("##assistantLayout", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
            return;
        ImGui::TableSetupColumn("##settings", ImGuiTableColumnFlags_WidthFixed, settingsWidth);
        ImGui::TableSetupColumn("##chat", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (cyui::beginCard("##assistantSettings", nullptr, height / cyui::dp(1.0f) - 1.0f)) {
            cyui::ScopedDisabled disabled(demo);
            settings();
        }
        cyui::endCard();
        ImGui::TableSetColumnIndex(1);
        {
            cyui::ScopedDisabled disabled(demo);
            panel.draw("CyWinTask Assistant", 0, height);
        }
        ImGui::EndTable();
    }
    void settings()
    {
        using ui_kit::pick;
        const auto full = [] { ImGui::SetNextItemWidth(-FLT_MIN); };
        if (cyui::beginSection((std::string(pick("Connexion", "Connection")) + "###assistantConnection").c_str())) {
            if (cyui::beginProperties("##assistantProvider", 124.0f)) {
                cyui::property(ui_locale::tr("Fournisseur"));
                const float detect = ImGui::GetFrameHeight() + cyui::dp(2.0f);
                ImGui::SetNextItemWidth(std::max(cyui::dp(80.0f), ImGui::GetContentRegionAvail().x - detect
                    - ImGui::GetStyle().ItemSpacing.x));
                if (ImGui::BeginCombo("##assistantProvider", providerId.c_str())) {
                    for (const auto& entry : providerCatalogue) {
                        const std::string id = entry.at("id").get<std::string>();
                        const std::string label = entry.at("name").get<std::string>() + " (" + id + ")";
                        if (ImGui::Selectable(label.c_str(), id == providerId)) providerId = id;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (cyui::iconButton(cyui::icon::search, ui_locale::tr("Détecter")) && !discovery.valid())
                    discovery = std::async(std::launch::async, [registry = providers] { return registry->discover(); });
                if (discovery.valid()) {
                    cyui::spinner("##discovery", 6.0f);
                    ImGui::SameLine();
                    cyui::textDim("%s", ui_locale::tr("Recherche en cours..."));
                } else if (detectedProvider()) ui_kit::dotText(cyui::palette().success, ui_locale::tr("Disponible"));
                cyui::property(ui_locale::tr("Modèle"));
                full();
                ImGui::InputTextWithHint("##assistantModel", ui_locale::tr("ID exact ; vide = choix automatique local"), model, sizeof(model));
                cyui::property("URL");
                full();
                ImGui::InputTextWithHint("##assistantAddress", ui_locale::tr("Adresse du fournisseur (facultative)"), address, sizeof(address));
                cyui::property(ui_locale::tr("Clé d'environnement"),
                    pick("Nom d'une variable d'environnement contenant la clé API. La clé n'est jamais saisie ici.",
                        "Name of an environment variable holding the API key. The key itself is never typed here."));
                full();
                ImGui::InputTextWithHint("##assistantKey", "Ex. OPENAI_API_KEY", keyEnvironment, sizeof(keyEnvironment));
                cyui::property(pick("Contexte (Kio)", "Context (KiB)"), pick("0 = contexte complet.", "0 = full context."));
                full();
                ImGui::InputInt("##assistantContext", &contextKilobytes, 64, 256);
                contextKilobytes = std::clamp(contextKilobytes, 0, 4096);
                cyui::property(pick("Historique", "History"));
                cyui::toggleSwitch(ui_locale::tr("Sauver l'historique"), &saveHistory);
                cyui::property("Session");
                {
                    cyui::ScopedDisabled noHistory(!saveHistory);
                    cyui::toggleSwitch(ui_locale::tr("Restaurer la session"), &restoreHistory);
                }
                cyui::endProperties();
            }
            cyui::space(4.0f);
            const float width = ImGui::GetContentRegionAvail().x;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            if (cyui::primaryButton(ui_locale::tr("Connecter"), ImVec2((width - spacing * 2.0f) * 0.4f, 0.0f))) {
                try { error.clear(); connect(); }
                catch (const std::exception& ex) { error = redact_error_message(ex.what()); }
            }
            ImGui::SameLine();
            if (cyui::button(ui_locale::tr("Déconnecter"), cyui::Variant::secondary, ImVec2((width - spacing * 2.0f) * 0.35f, 0.0f)))
                chat.disconnect();
            ImGui::SameLine();
            if (cyui::button(ui_locale::tr("Annuler"), cyui::Variant::ghost, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                chat.cancel();
            if (!error.empty()) { cyui::space(4.0f); cyui::callout(cyui::Tone::error, error.c_str()); }
            if (!chat.state().error_message.empty()) {
                cyui::space(4.0f);
                cyui::callout(cyui::Tone::error, chat.state().error_message.c_str());
            }
            cyui::endSection();
        }
        if (cyui::beginSection((std::string(pick("Serveur MCP local", "Local MCP server")) + "###assistantServer").c_str())) {
            if (cyui::beginProperties("##assistantMcp", 124.0f)) {
                cyui::property(ui_locale::tr("Profil MCP"));
                full();
                if (ImGui::BeginCombo("##mcpAccessProfile", ui_locale::tr(mcp_access::option(accessProfile).label))) {
                    for (const auto& choice : mcp_access::options) {
                        if (ImGui::Selectable(ui_locale::tr(choice.label), choice.profile == accessProfile)) {
                            try {
                                mcp_access::save(accessConfigPath, choice.profile);
                                accessProfile = choice.profile;
                                accessConfigValid = true;
                                error.clear();
                                if (http) { stopServer(); startServer(); }
                            } catch (const std::exception& ex) {
                                stopServer(); error = redact_error_message(ex.what());
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                cyui::property(pick("Serveur", "Server"));
                bool exposed = http != nullptr;
                if (cyui::toggleSwitch(http ? pick("Actif###mcpServer", "Running###mcpServer")
                        : pick("Arrêté###mcpServer", "Stopped###mcpServer"), &exposed)) {
                    try { error.clear(); if (exposed) startServer(); else stopServer(); }
                    catch (const std::exception& ex) { stopServer(); error = redact_error_message(ex.what()); }
                }
                cyui::endProperties();
            }
            const auto access = local_access_settings(accessProfile);
            ImGui::PushTextWrapPos(0.0f);
            cyui::textFaint("%s", access.require_token
                ? ui_locale::tr("Jeton requis ; copier l'adresse et le jeton pour se connecter.")
                : ui_locale::tr("Adresse seule sur 127.0.0.1 ; tout processus local peut appeler les outils autorisés."));
            ImGui::PopTextWrapPos();
            if (!access.control_operations) {
                cyui::space(4.0f);
                cyui::callout(cyui::Tone::warning, pick(
                    "Ce profil autorise tous les outils, y compris l'arrêt de processus, sans confirmation.",
                    "This profile allows every tool, including ending processes, without confirmation."));
            }
            if (http) {
                const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
                cyui::space(4.0f);
                {
                    cyui::ScopedFont mono(cyui::monoFont(), 0.9f);
                    cyui::textDim("%s", url.c_str());
                }
                if (cyui::iconTextButton(cyui::icon::copy, ui_locale::tr("Copier l'adresse"), cyui::Variant::ghost))
                    ImGui::SetClipboardText(url.c_str());
                if (!token.empty()) {
                    ImGui::SameLine();
                    if (cyui::iconTextButton(cyui::icon::lock, ui_locale::tr("Copier le jeton"), cyui::Variant::ghost))
                        ImGui::SetClipboardText(token.c_str());
                }
            }
            cyui::endSection();
        }
        if (cyui::beginSection((std::string(ui_locale::tr("Droits des outils")) + "###assistantRights").c_str(), false)) {
            ImGui::PushTextWrapPos(0.0f);
            cyui::textFaint("%s", ui_locale::tr("Droits du chat et des profils MCP contrôlés. Free et Jeton seul autorisent tous les outils."));
            ImGui::PopTextWrapPos();
            const auto catalogue = tasks.registry()->describe();
            if (cyui::button(ui_locale::tr("Lecture seule"), cyui::Variant::secondary)) {
                for (const auto& entry : catalogue) {
                    const auto name = entry.at("name").get<std::string>();
                    tasks.setAllowed(name, name == "permissions.list"
                        || name == "system.summary" || name == "system.devices"
                        || name == "process.list" || name == "process.get"
                        || name == "ui.state" || name == "ui.performance.history");
                }
            }
            ImGui::SameLine();
            if (cyui::button(ui_locale::tr("Contrôle de l'interface"), cyui::Variant::secondary)) {
                for (const auto& entry : catalogue) {
                    const auto name = entry.at("name").get<std::string>();
                    if (name.rfind("ui.", 0) == 0) tasks.setAllowed(name, true);
                }
            }
            ImGui::SameLine();
            if (cyui::button(ui_locale::tr("Tout autoriser"), cyui::Variant::danger))
                for (const auto& entry : catalogue)
                    tasks.setAllowed(entry.at("name").get<std::string>(), true);
            cyui::space(4.0f);
            for (const auto& entry : catalogue) {
                const std::string name = entry.at("name").get<std::string>();
                bool allowed = tasks.isAllowed(name);
                {
                    cyui::ScopedFont mono(cyui::monoFont(), 0.9f);
                    if (ImGui::Checkbox(name.c_str(), &allowed)) tasks.setAllowed(name, allowed);
                }
                cyui::tooltip(entry.at("description").get<std::string>().c_str());
            }
            cyui::endSection();
        }
    }
};

AssistantPane::AssistantPane(bool demo, std::shared_ptr<UiBridge> ui)
    : state_(std::make_unique<State>(demo, std::move(ui))) {}
AssistantPane::~AssistantPane() = default;
void AssistantPane::draw() { state_->draw(); }
void AssistantPane::drawConfirmation() { state_->drawConfirmation(); }
bool AssistantPane::allows(const std::string& operation) const { return state_->tasks.isAllowed(operation); }
void AssistantPane::startLocalMcp(int port)
{
    try { state_->startServer(port); }
    catch (...) { state_->stopServer(); throw; }
}

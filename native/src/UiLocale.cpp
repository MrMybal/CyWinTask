#include "UiLocale.h"

#include <windows.h>

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ui_locale {
namespace {
bool useFrench = false;
constexpr wchar_t keyName[] = L"Software\\CyWinTask";
constexpr wchar_t valueName[] = L"UiLanguage";
const std::unordered_map<std::string_view, const char*> english {
    {"Processus", "Processes"}, {"Processus Windows", "Windows processes"},
    {"Arrière-plan", "Background"}, {"Applications", "Applications"},
    {"Processeur", "CPU"}, {"Mémoire", "Memory"}, {"Réseau", "Network"},
    {"Système", "System"}, {"Disque 0", "Disk 0"},
    {"Utilisation", "Usage"}, {"Utilisation totale", "Total usage"},
    {"Utilisation · moteur le plus actif", "Usage · busiest engine"},
    {"Température CPU", "CPU temperature"}, {"Température GPU", "GPU temperature"},
    {"Mémoire physique utilisée", "Physical memory used"},
    {"Mémoire physique disponible", "Physical memory available"},
    {"Mémoire virtuelle engagée", "Committed virtual memory"},
    {"Mémoire GPU dédiée", "Dedicated GPU memory"},
    {"Mémoire GPU partagée", "Shared GPU memory"},
    {"Mémoire dédiée", "Dedicated memory"},
    {"Temps d'activité", "Active time"}, {"Temps d’activité", "Active time"},
    {"Lecture", "Read"}, {"Écriture", "Write"},
    {"Réception", "Receive"}, {"Envoi", "Send"},
    {"Encodage vidéo", "Video encode"}, {"Décodage vidéo", "Video decode"},
    {"Processeur de démonstration · 8 processeurs logiques", "Demo CPU · 8 logical processors"},
    {"15,8 / 32 Go utilisés", "15.8 / 32 GB used"},
    {"SSD de démonstration", "Demo SSD"}, {"Réseau de démonstration", "Demo network"},
    {"GPU de démonstration", "Demo GPU"},
    {"Mesure indisponible", "Measurement unavailable"},
    {"Indisponible", "Unavailable"},
    {" Go", " GB"}, {" Mo/s", " MB/s"}, {" Mbits/s", " Mbps"},
    {"%.1f Mo", "%.1f MB"}, {"%.0f Mo", "%.0f MB"},
    {"Go", "GB"}, {"Mo/s", "MB/s"}, {"Mbit/s", "Mbps"}, {"Mbits/s", "Mbps"},
    {"Température indisponible", "Temperature unavailable"},
    {"Activité indisponible", "Activity unavailable"},
    {"Aucun processus mesuré", "No measured processes"},
    {"Aucun processus trouvé", "No processes found"},
    {"Mesures en attente", "Waiting for measurements"},
    {"En attente des processus", "Waiting for processes"},
    {"Les courbes apparaîtront après la collecte.", "Charts will appear after collection starts."},
    {"Le collecteur démarre.", "The collector is starting."},
    {"La collecte démarre dans cet onglet.", "Collection starts on this tab."},
    {"Modifiez la recherche ou le filtre.", "Change the search or filter."},
    {"Arrêter les tâches sélectionnées...", "End selected tasks..."},
    {"Arrêter le processus...", "End process..."},
    {"Arrêter la tâche", "End task"}, {"Arrêter ", "End "}, {" tâches", " tasks"},
    {"Arrêter", "End"}, {"Annuler", "Cancel"},
    {"Informations avancées...", "Advanced information..."},
    {"Informations avancées###advanced", "Advanced information###advanced"},
    {"Ouvrir l'emplacement", "Open file location"},
    {"Copier le chemin", "Copy path"}, {"Copier le nom et le PID", "Copy name and PID"},
    {"Copier tout", "Copy all"},
    {"Arrêt demandé.", "Termination requested."},
    {"Lecture des informations...", "Loading information..."},
    {"Note fictive de démonstration.", "Fictitious demo rating."},
    {"\nDonnées fictives de démonstration.", "\nFictitious demo data."},
    {"Données fictives · Démonstration", "Fictitious data · Demo"},
    {"Actualisation en pause", "Refresh paused"},
    {"1 s · Collecte %.1f ms · %zu processus", "1 s · Collection %.1f ms · %zu processes"},
    {"Connexion au collecteur...", "Connecting to collector..."},
    {"Ctrl+1/2/3 · Ctrl+F · Ctrl/Maj", "Ctrl+1/2/3 · Ctrl+F · Ctrl/Shift"},
    {"DÉMO", "DEMO"}, {"Texte", "Text"},
    {"Reprendre", "Resume"}, {"Pause", "Pause"},
    {"Suspendre ou reprendre la collecte", "Pause or resume collection"},
    {"%zu sélectionné(s)", "%zu selected"},
    {"Nom du processus", "Process name"}, {"Confiance", "Trust"},
    {"Durée", "Elapsed"}, {"Valeur", "Value"},
    {"Groupe avec processus associés", "Group with related processes"},
    {"Processus seul", "Single process"},
    {" · compteur indisponible", " · counter unavailable"},
    {" · moteur GPU le plus sollicité%s", " · busiest GPU engine%s"},
    {"Indice local, pas une analyse antivirus.", "Local rating, not an antivirus scan."},
    {"Inspection rapide", "Quick inspection"},
    {"Sélectionnez un processus pour voir son identité, son chemin et la note de confiance.",
        "Select a process to see its identity, path and trust rating."},
    {"Indice local basé sur la signature Authenticode et l'emplacement du fichier ; ce n'est pas un antivirus.",
        "Local rating based on Authenticode signature and file location; it is not an antivirus scan."},
    {"Sélection multiple", "Multiple selection"},
    {"%zu processus sélectionnés · clic droit pour les actions disponibles.",
        "%zu processes selected · right-click for available actions."},
    {"Confiance %d/100 · %s", "Trust %d/100 · %s"},
    {"Confiance — · %s", "Trust — · %s"},
    {"PID %d · Parent %d · %s · %s · CPU %.1f %% · GPU %s · %.1f Mo",
        "PID %d · Parent %d · %s · %s · CPU %.1f %% · GPU %s · %.1f MB"},
    {"Fichier : %s", "File: %s"},
    {"Chemin du fichier indisponible.", "File path unavailable."},
    {"Recherche, tri et inspection des tâches en cours", "Search, sort and inspect running tasks"},
    {"Nom, PID ou pid:1234...", "Name, PID or pid:1234..."},
    {"Saisissez pid:1234 pour isoler un processus précis.", "Enter pid:1234 to isolate a specific process."},
    {"Effacer", "Clear"}, {"Inspecter", "Inspect"},
    {"Regrouper par type", "Group by type"},
    {"Processus associés", "Related processes"},
    {"Compact", "Compact"}, {"Inspection", "Inspector"},
    {"Afficher", "Show"}, {"Tous les processus", "All processes"},
    {"Il y a %.0f s", "%.0f s ago"},
    {"%lld j %02lld h", "%lld d %02lld h"},
    {"Afficher les processeurs logiques", "Show logical processors"},
    {"Compacte (Windows)", "Compact (Windows)"}, {"Détaillée", "Detailed"},
    {"Utilisation totale : %.1f %%", "Total usage: %.1f %%"},
    {"Ouvrir ce graphique dans une fenêtre séparée", "Open this chart in a separate window"},
    {"Température ", "Temperature "},
    {"Historique des 60 dernières secondes", "Last 60 seconds"},
    {"Afficher ce processus dans la liste", "Show this process in the list"},
    {"Processus · CPU", "Processes · CPU"},
    {"Processus · mémoire", "Processes · memory"},
    {"Mesures en direct · 60 secondes d'historique · survolez les courbes pour lire les valeurs",
        "Live measurements · 60-second history · hover over charts to read values"},
    {"Vue graphiques", "Charts"}, {"Vue détaillée", "Details"},
    {"Disposition", "Layout"}, {"Colonnes automatiques", "Automatic columns"},
    {"2 colonnes", "2 columns"}, {"3 colonnes", "3 columns"},
    {"4 colonnes", "4 columns"}, {"5 colonnes", "5 columns"},
    {"Cartes compactes", "Compact cards"},
    {"Afficher disques et réseau", "Show disks and network"},
    {"Choisissez une ressource dans la liste à gauche", "Choose a resource from the list on the left"},
    {"Fenêtre séparée", "Separate window"},
    {"Arrêter les tâches ?###stopModal", "End tasks?###stopModal"},
    {"Les modifications non enregistrées peuvent être perdues.", "Unsaved changes may be lost."},
    {"%zu processus sélectionné(s)", "%zu selected processes"},
    {"... et %zu autres", "... and %zu more"},
    {"Graphiques CPU, RAM et GPU###detached_all", "CPU, RAM and GPU charts###detached_all"},
    {"Graphique ", "Chart "},
    {"Confirmer l'arrêt du processus##assistantStop", "Confirm process termination##assistantStop"},
    {"Un outil demande l'arrêt de ce processus :", "A tool requests termination of this process:"},
    {"CyWinTask vérifiera encore son identité avant l'arrêt.",
        "CyWinTask will verify its identity again before termination."},
    {"Arrêter ce processus", "End this process"},
    {"CyMCPSDK · lecture et contrôle des processus", "CyMCPSDK · process monitoring and control"},
    {"Démonstration : connexion et serveur désactivés ; aucune donnée réelle chargée.",
        "Demo: connection and server disabled; no real data loaded."},
    {"Fournisseur", "Provider"}, {"Détecter", "Detect"},
    {"Recherche en cours...", "Searching..."}, {"Disponible", "Available"},
    {"Modèle", "Model"},
    {"ID exact ; vide = choix automatique local", "Exact ID; empty = automatic local choice"},
    {"Adresse du fournisseur (facultative)", "Provider address (optional)"},
    {"Clé d'environnement", "Key environment variable"},
    {"Sauver l'historique", "Save history"},
    {"Restaurer la session", "Restore session"},
    {"Contexte (Kio, 0 = complet)", "Context (KiB, 0 = full)"},
    {"Connecter", "Connect"}, {"Déconnecter", "Disconnect"},
    {"Droits des outils", "Tool permissions"},
    {"Droits du chat et des profils MCP contrôlés. Free et Jeton seul autorisent tous les outils.",
        "Permissions for chat and controlled MCP profiles. Free and Token only allow all tools."},
    {"Lecture seule", "Read only"},
    {"Contrôle de l'interface", "UI control"},
    {"Tout autoriser", "Allow all"},
    {"Profil MCP", "MCP profile"},
    {"Jeton requis ; copier l'adresse et le jeton pour se connecter.",
        "Token required; copy the address and token to connect."},
    {"Adresse seule sur 127.0.0.1 ; tout processus local peut appeler les outils autorisés.",
        "Address only on 127.0.0.1; any local process can call the allowed tools."},
    {"Serveur MCP local", "Local MCP server"},
    {"Copier l'adresse", "Copy address"}, {"Copier le jeton", "Copy token"},
    {"Local contrôlé", "Local controlled"}, {"Jeton seul", "Token only"},
    {"Protégé", "Protected"},
    {"Signature valide", "Valid signature"},
    {"Signature non confirmée", "Unconfirmed signature"},
    {"Signature non valide", "Invalid signature"},
    {"Non évalué", "Not assessed"}, {"Analyse...", "Analyzing..."},
    {"Fichier distant ou chemin indisponible.", "Remote file or unavailable path."},
    {"Fichier sur un lecteur réseau.", "File on a network drive."},
    {"Signature Authenticode valide · dossier Windows.", "Valid Authenticode signature · Windows folder."},
    {"Signature Authenticode valide · dossier d'installation.", "Valid Authenticode signature · installation folder."},
    {"Signature Authenticode valide.", "Valid Authenticode signature."},
    {"Signature intégrée non confirmée · dossier Windows. Un catalogue système peut signer ce fichier.",
        "Embedded signature unconfirmed · Windows folder. A system catalog may sign this file."},
    {"Signature intégrée non confirmée · dossier d'installation.",
        "Embedded signature unconfirmed · installation folder."},
    {"Signature intégrée non confirmée.", "Embedded signature unconfirmed."},
    {"La vérification Authenticode a échoué.", "Authenticode verification failed."},
    {"La vérification de signature n'a pas abouti.", "Signature verification did not complete."},
    {"Limite du cache atteinte.", "Cache limit reached."},
    {"Vérification locale en cours.", "Local verification in progress."},
};

std::string replaceFirst(std::string value, std::string_view from, std::string_view to)
{
    const auto at = value.find(from);
    if (at != std::string::npos) value.replace(at, from.size(), to);
    return value;
}
}

void load()
{
    DWORD value {};
    DWORD size = sizeof(value);
    useFrench = RegGetValueW(HKEY_CURRENT_USER, keyName, valueName, RRF_RT_REG_DWORD,
        nullptr, &value, &size) == ERROR_SUCCESS && value == 1;
}

bool french() { return useFrench; }

void setFrench(bool value)
{
    if (useFrench == value) return;
    useFrench = value;
    HKEY key {};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyName, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const DWORD setting = value ? 1 : 0;
        RegSetValueExW(key, valueName, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&setting), sizeof(setting));
        RegCloseKey(key);
    }
}

const char* tr(const char* frenchText)
{
    if (useFrench || !frenchText) return frenchText;
    const auto found = english.find(frenchText);
    return found == english.end() ? frenchText : found->second;
}

std::string text(std::string_view frenchText)
{
    if (useFrench) return std::string(frenchText);
    if (const auto found = english.find(frenchText); found != english.end()) return found->second;
    std::string value(frenchText);
    constexpr std::string_view corePrefix = "Processeur logique ";
    constexpr std::string_view diskPrefix = "Disque ";
    constexpr std::string_view physicalDiskPrefix = "Disque physique · ";
    if (value.rfind(corePrefix, 0) == 0) return "Logical processor " + value.substr(corePrefix.size());
    if (value.rfind(physicalDiskPrefix, 0) == 0) return "Physical disk · " + value.substr(physicalDiskPrefix.size());
    if (value.rfind(diskPrefix, 0) == 0) return "Disk " + value.substr(diskPrefix.size());
    value = replaceFirst(std::move(value), " processeurs logiques", " logical processors");
    if (value.find(" Go utilisés") != std::string::npos) {
        value = replaceFirst(std::move(value), " Go utilisés", " GB used");
        std::replace(value.begin(), value.end(), ',', '.');
    }
    return value;
}
}

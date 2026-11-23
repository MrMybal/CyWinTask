# Assistant, CLI et MCP

L'Assistant, le CLI et les quatre profils d'accès MCP décrits ci-dessous sont disponibles dans CyWinTask depuis la version 0.9.0.

## Compilation et versions

CyWinTask compile CyMCPSDK **0.7.2** depuis les sources à la révision `e562c731a146d599e3404718ea8414bb5b96b66d` (voir `native/CYMCP_REVISION`, descendant compatible de `34d4bca`). CyUI est utilisé à la révision `6ef01cfcdb197f3a89d64618ce5ef8818c704728` (voir `native/CYUI_REVISION`). Les copies locales placées à côté du dépôt CyWinTask sont privilégiées. Si les sources manquent, `native/Sync-CyMCPSDK.ps1` et `native/Sync-CyUI.ps1` récupèrent les révisions fixées. La compilation échoue si une source présente ne correspond pas à la révision attendue.

```powershell
./native/Sync-CyMCPSDK.ps1
./native/Sync-CyUI.ps1
./native/Build-Native.ps1 -Offline -Checks
```

`-Offline` emploie le cache NuGet local. Omettre cette option si les paquets nécessaires n'y sont pas. `-Checks` compile et exécute les tests intégrés du chat et du contrôle d'interface, puis enlève leurs exécutables du dossier de distribution. La compilation active les modules CyMCPSDK Core, Platform, Http, MCP, AI et ChatUI. Local, l'application de démonstration, les exemples et les tests propres au SDK sont désactivés. CyUI fournit Core, Plots et DocumentViews. CyToolsCore n'était pas utilisé dans ce projet.

## Opérations communes

Le panneau Assistant, le CLI et MCP utilisent le registre de CyWinTask. Les sept opérations ci-dessous sont disponibles sans interface. Une instance graphique ouverte ajoute les commandes `ui.*` décrites plus bas à son registre. Chaque processus est identifié par PID et date de création.

| Opération | Entrée | Résultat | Usage |
| --- | --- | --- | --- |
| `permissions.list` | `{}` | Droits courants par outil | Permet à l'IA de connaître les actions autorisées, sans pouvoir s'accorder de droits. |
| `system.summary` | `{}` | Horodatage, CPU, mémoire physique, nombre de processus | Vue rapide des mesures actuelles. |
| `system.devices` | `kind?`, `offset?`, `limit?` (1 à 30, défaut 20) | Périphériques et métriques courantes | Inspection ciblée du CPU, GPU, disque et réseau. |
| `process.list` | `query?`, `offset?`, `limit?` (1 à 100, défaut 50) | Total, position et page de processus | Recherche ciblée et pagination. |
| `process.get` | `pid`, `created?` | Informations du processus, chemin si accessible | Inspection précise ; `created` protège contre la réutilisation d'un PID. |
| `process.details` | `pid`, `created`, `offset?`, `length?` (1 à 8192 octets) | Page du rapport avancé, longueur totale et position suivante | Analyse progressive des modules et compteurs ; désactivée par défaut. |
| `process.terminate` | `pid`, `created` ; `confirm: true` requis en accès contrôlé | `accepted` | Arrêt d'un seul processus après nouvelle validation de son identité. |

Les schémas complets d'entrée et de sortie sont retournés par `describe` et `tools/list`. Le collecteur revérifie l'identité et ses protections de processus au moment de l'arrêt. Une branche entière n'est jamais arrêtée implicitement. Les valeurs Windows indisponibles sont `null`. Chaque appel travaille sur la machine où tourne le point d'entrée : le CLI autonome démarre son propre collecteur ; le MCP activé dans l'interface utilise le collecteur propre à son panneau. Le rapport avancé peut aussi s'ouvrir dans l'interface via `ui.processes.inspect`. Les pages `process.details` sont bornées et relues à chaque appel, sans stockage durable.
La collecte détaillée des périphériques est activée à la demande pendant `system.devices`, puis arrêtée lorsque l'appel se termine.

## Commandes de l'instance graphique

Ces opérations sont disponibles dans le chat intégré et sur le serveur MCP **de l'application ouverte**. Elles sont exécutées sur le thread de l'interface et agissent sur les mêmes champs et fenêtres que les commandes manuelles.

| Opération | Action |
| --- | --- |
| `ui.state` | Lire page, filtres, sélection, disposition, périphériques et fenêtres ouvertes. |
| `ui.navigate` | Ouvrir Processus, Performance, Assistant, Console, Températures ou Informations système ; choisir une vue ou un périphérique. |
| `ui.processes.configure` | Choisir la liste générale ou le sous onglet GPU (`view: "all"` ou `"gpu"`), puis régler recherche, type, regroupement, arbre, densité, inspection et tri. |
| `ui.processes.select` | Sélectionner plusieurs processus, ajouter ou retirer des lignes. |
| `ui.processes.expand` | Déplier ou replier une branche de processus associés. |
| `ui.processes.inspect` | Ouvrir le rapport avancé d'un processus. |
| `ui.processes.trust` | Lire la note de confiance locale d'un processus. |
| `ui.processes.open_location` | Ouvrir son fichier dans l'Explorateur. |
| `ui.processes.copy` | Copier son chemin ou son nom et PID. |
| `ui.performance.configure` | Régler vue, colonnes, disposition en cartes ou lignes, et graphes de cœurs CPU. |
| `ui.performance.history` | Lire jusqu'à 120 points déjà mesurés pour une courbe. |
| `ui.window.set` | Ouvrir ou fermer le tableau de bord ou un graphique détaché. |
| `ui.pause` | Mettre en pause ou reprendre les mesures affichées. |
| `ui.scale` | Régler le zoom de 100 à 170 %. |

Dans l'onglet **Assistant → Droits des outils**, chaque opération possède sa propre case. Les lectures de base sont actives au départ ; le rapport avancé, les commandes qui modifient l'interface, la copie, l'Explorateur, l'inspection et l'arrêt sont désactivés. Les préréglages **Lecture seule**, **Contrôle de l'interface** et **Tout autoriser** facilitent le réglage. Ces droits valent pour la session ouverte et s'appliquent au chat ainsi qu'aux profils MCP **Local contrôlé** et **Protégé**. `permissions.list` retourne les droits effectifs du client. En accès contrôlé, un arrêt exige `confirm:true`, le PID, la date de création et, dans l'interface, une validation humaine dans une fenêtre modale. Les profils **Free** et **Jeton seul** autorisent toutes les opérations enregistrées : l'arrêt demande le PID et la date de création, sans argument `confirm` ni boîte supplémentaire ; `confirm:false` est refusé s'il est fourni. Le collecteur revérifie toujours l'identité du processus. Le mode CLI `invoke` et le transport stdio gardent leurs droits indépendants, avec `--allow-details` pour le rapport avancé et `--allow-stop` pour l'arrêt non interactif.

## Profils MCP locaux

**Local contrôlé** est le profil par défaut : adresse seule sur `127.0.0.1`, droits par outil et confirmation avant tout arrêt. **Free** doit être choisi explicitement. Le serveur HTTP écoute seulement sur `127.0.0.1` et le client utilise l'adresse `/mcp` seule. Free utilise `LocalAccessProfile::free`, `LocalHttpServer::UnauthenticatedLoopback` et un registre `Policy::allow_all()`. Tout processus de la machine peut appeler les opérations exposées, y compris les modifications et l'arrêt d'un processus. Les schémas d'arguments et les contrôles du collecteur restent appliqués.

| Profil | Connexion | Opérations |
| --- | --- | --- |
| **Free** | Adresse seule | Toutes les opérations enregistrées. |
| **Local contrôlé** | Adresse seule | Droits cochés dans l'Assistant. |
| **Jeton seul** | Adresse et jeton | Toutes les opérations enregistrées. |
| **Protégé** | Adresse et jeton | Droits cochés dans l'Assistant. |

Le choix se fait dans **Assistant → Profil MCP** et est enregistré dans `%LOCALAPPDATA%\CyWinTask\mcp-access.json`. Une configuration sans `access_profile` explicite (ou l'absence de fichier) utilise Local contrôlé ; une valeur explicite `free`, `local_controlled`, `token_only` ou `protected` est conservée. Modifier le profil redémarre le serveur local. Pour un test isolé, `CYWINTASK_MCP_CONFIG` peut désigner un autre fichier JSON. Dans les profils à jeton, CyWinTask génère un jeton ou utilise `CYWINTASK_MCP_TOKEN` s'il contient au moins 32 caractères.

## CLI autonome

Le compagnon console est placé à côté de `CyWinTask.exe` et du dossier `bridge`. Il ne lance pas l'interface. Les résultats sont du JSON sur stdout ; les erreurs sont écrites sur stderr. Code de sortie 0 en cas de succès, 1 pour l'échec de l'opération ou du service et 2 pour une commande incomplète ou inconnue.

```powershell
./dist-native/CyWinTaskCli.exe --help
./dist-native/CyWinTaskCli.exe list
./dist-native/CyWinTaskCli.exe describe process.list
./dist-native/CyWinTaskCli.exe invoke system.summary '{}'
./dist-native/CyWinTaskCli.exe invoke process.list '{"query":"exemple","limit":10}'
```

Pour un arrêt via `invoke` ou stdio, ajouter `--allow-stop` à **cette invocation** et transmettre `pid`, `created` et `confirm:true`. Ce parcours conserve les droits contrôlés. Le serveur HTTP Free autorise l'arrêt avec `pid` et `created` sans confirmation supplémentaire. Dans l'interface, le chat et les profils MCP contrôlés affichent toujours une confirmation modale pour un arrêt autorisé.

## Chat intégré

Ouvrir l'onglet **Assistant** ou démarrer `CyWinTask.exe --assistant` (`Ctrl+3` depuis l'interface). Sélectionner un fournisseur et son modèle, puis **Connecter**. La détection des fournisseurs est déclenchée par **Détecter**. Ollama et les autres modèles locaux disponibles peuvent sélectionner automatiquement un modèle si le champ reste vide. Les fournisseurs API exigent leur configuration habituelle ; le champ « Clé d'environnement » lit la clé dans une variable d'environnement, sans l'afficher. Les messages arrivent progressivement ; **Annuler** interrompt une génération. Les erreurs de connexion et de génération sont visibles dans le panneau.

Les pièces jointes sont sélectionnées manuellement au moyen du bouton du chat et utilisées seulement si le fournisseur accepte leur format. L'historique local est facultatif et désactivé par défaut ; si activé, il se trouve sous `%LOCALAPPDATA%\CyWinTask\chat`. La restauration est une option distincte. La fenêtre de contexte en Kio est réglable pour les fournisseurs qui la gèrent ; `0` conserve le comportement du fournisseur. En mode `--demo`, le panneau est visible mais ses connexions et ses actions réelles sont désactivées.

## MCP externe

Le transport **stdio** se lance directement depuis un client MCP en donnant l'exécutable et l'argument `mcp` :

```json
{"command":"D:\\chemin\\CyWinTask\\CyWinTaskCli.exe","args":["mcp"]}
```

Il utilise les flux du processus enfant et n'a pas d'authentification réseau. Pour autoriser les arrêts sur **cette instance**, ajouter `--allow-stop` aux arguments ; l'outil exige aussi `confirm:true` et la date de création. Ajouter `--allow-details` pour lire le rapport avancé.

Le serveur HTTP autonome écoute uniquement sur `127.0.0.1`, chemin `/mcp`. `CyWinTaskCli.exe serve 8765` démarre en **Local contrôlé** sans jeton ; `0` choisit un port libre et l'adresse exacte est écrite sur stderr. Utiliser `--profile free`, `--profile token_only` ou `--profile protected` pour choisir un autre profil. Les deux profils à jeton demandent `CYWINTASK_MCP_TOKEN` avec au moins 32 caractères, transmis par le client dans `Authorization: Bearer <jeton>`. Dans l'interface, cocher **Serveur MCP local** démarre le serveur avec le profil sélectionné ; copier l'adresse et, si nécessaire, le jeton depuis le panneau. Cette instance agit sur l'état réel lu par l'application ouverte. `CyWinTask.exe --assistant --mcp-local 8765` démarre directement l'instance graphique et son serveur sur ce port.

Pour piloter cette instance depuis un terminal, utiliser `remote` avec l'adresse copiée. En profil Free ou Local contrôlé, aucun jeton n'est nécessaire :

```powershell
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp list
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp describe ui.navigate
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp invoke ui.navigate '{"page":"performance"}'
```

Remplacer `8765` par le port affiché dans l'interface. Pour Jeton seul et Protégé, définir aussi `$env:CYWINTASK_MCP_TOKEN = '<jeton de la fenêtre>'`. `remote invoke` renvoie le résultat MCP complet en JSON, notamment `structuredContent` ou `content`. Il renvoie un code d'échec si l'outil signale `isError`. Le profil de l'instance ouverte s'applique aussi à ces appels.

La révision utilisée prend en charge la découverte moderne `server/discover`, `tools/list` et `tools/call` sur stdio et HTTP, avec la version de protocole MCP `2026-07-28` et les métadonnées de capacités requises. Le catalogue reste assez petit pour la découverte complète. Le serveur annonce un TTL de découverte de 30 secondes ; un client peut rafraîchir la découverte à tout moment. Les résultats des mesures et des mutations ne sont pas mis en cache par CyWinTask. La pagination de `process.list` et `system.devices` et la limite de `ui.performance.history` bornent les réponses. Aucune recherche de fichiers, ressource textuelle, journal de changements, abonnement ou stockage de gros résultats sur disque n'est nécessaire pour ces opérations. Aucun serveur MCP d'une autre application n'est connecté au chat, car l'analyse des processus de CyWinTask n'en dépend pas.

## Vérifications locales

`Build-Native.ps1 -Checks` vérifie la connexion du chat à un fournisseur de test en mémoire, l'affichage progressif, l'annulation, une lecture du registre, le refus d'une confirmation et l'arrêt autorisé d'un **processus créé par le test**. Il vérifie aussi la migration des profils MCP, le catalogue des commandes d'interface, leur refus initial, le passage au thread UI, la révocation d'un droit et la validation des arguments en Free. `python native/tests/mcp_integration.py` vérifie les parcours CLI et stdio ainsi que les quatre profils HTTP. `python native/tests/mcp_app_integration.py` lance la vraie interface avec une configuration isolée pour vérifier le profil par défaut Local contrôlé (lecture permise, arrêt refusé), puis lecture, modification et arrêt d'un processus créé par le test en Free sans jeton, puis le refus sans jeton en Protégé. Les tests ne nécessitent aucun fournisseur IA externe.

Limites actuelles : le CLI autonome et le panneau ouvert possèdent chacun leur propre session du collecteur ; pour modifier la fenêtre ouverte, utiliser `remote` ou son serveur MCP. Les thèmes et la densité choisis uniquement au lancement ne se changent pas encore à chaud. L'appel d'un outil par un fournisseur IA réel dépend de la prise en charge des outils par le modèle choisi. L'annulation d'un arrêt déjà transmis au collecteur ne peut pas le défaire.

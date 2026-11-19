# Assistant, CLI et MCP

Cette intégration est disponible dans les binaires de CyWinTask 0.8.0 et dans les sources.

## Compilation et versions

CyWinTask compile CyMCPSDK **0.7.0** depuis les sources à la révision `791d4d7d7680213b282ec8529d248a10f8494d77` (voir `native/CYMCP_REVISION`). CyUI est utilisé à la révision `6ef01cfcdb197f3a89d64618ce5ef8818c704728` (voir `native/CYUI_REVISION`). Les copies locales placées à côté du dépôt CyWinTask sont privilégiées. Si les sources manquent, `native/Sync-CyMCPSDK.ps1` et `native/Sync-CyUI.ps1` récupèrent les révisions fixées. La compilation échoue si une source présente ne correspond pas à la révision attendue.

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
| `process.terminate` | `pid`, `created`, `confirm: true` | `accepted` | Arrêt d'un seul processus après autorisation et confirmation. |

Les schémas complets d'entrée et de sortie sont retournés par `describe` et `tools/list`. Le collecteur revérifie l'identité et ses protections de processus au moment de l'arrêt. Une branche entière n'est jamais arrêtée implicitement. Les valeurs Windows indisponibles sont `null`. Chaque appel travaille sur la machine où tourne le point d'entrée : le CLI autonome démarre son propre collecteur ; le MCP activé dans l'interface utilise le collecteur propre à son panneau. Le rapport avancé peut aussi s'ouvrir dans l'interface via `ui.processes.inspect`. Les pages `process.details` sont bornées et relues à chaque appel, sans stockage durable.
La collecte détaillée des périphériques est activée à la demande pendant `system.devices`, puis arrêtée lorsque l'appel se termine.

## Commandes de l'instance graphique

Ces opérations sont disponibles dans le chat intégré et sur le serveur MCP **de l'application ouverte**. Elles sont exécutées sur le thread de l'interface et agissent sur les mêmes champs et fenêtres que les commandes manuelles.

| Opération | Action |
| --- | --- |
| `ui.state` | Lire page, filtres, sélection, disposition, périphériques et fenêtres ouvertes. |
| `ui.navigate` | Ouvrir Processus, Performance ou Assistant, choisir une vue ou un périphérique. |
| `ui.processes.configure` | Régler recherche, type, regroupement, arbre, densité, inspection et tri. |
| `ui.processes.select` | Sélectionner plusieurs processus, ajouter ou retirer des lignes. |
| `ui.processes.expand` | Déplier ou replier une branche de processus associés. |
| `ui.processes.inspect` | Ouvrir le rapport avancé d'un processus. |
| `ui.processes.trust` | Lire la note de confiance locale d'un processus. |
| `ui.processes.open_location` | Ouvrir son fichier dans l'Explorateur. |
| `ui.processes.copy` | Copier son chemin ou son nom et PID. |
| `ui.performance.configure` | Régler vue, colonnes, cartes et graphes de cœurs CPU. |
| `ui.performance.history` | Lire jusqu'à 120 points déjà mesurés pour une courbe. |
| `ui.window.set` | Ouvrir ou fermer le tableau de bord ou un graphique détaché. |
| `ui.pause` | Mettre en pause ou reprendre les mesures affichées. |
| `ui.scale` | Régler le zoom de 100 à 170 %. |

Dans l'onglet **Assistant → Droits des outils**, chaque opération possède sa propre case. Les lectures de base sont actives au départ ; le rapport avancé, les commandes qui modifient l'interface, la copie, l'Explorateur, l'inspection et l'arrêt sont désactivés. Les préréglages **Lecture seule**, **Contrôle de l'interface** et **Tout autoriser** facilitent le réglage. Les droits valent pour la session ouverte et s'appliquent immédiatement au chat et au serveur MCP de cette fenêtre. `permissions.list` rend cet état lisible par l'IA, sans lui permettre de changer les droits. Un arrêt exige toujours `confirm:true`, le PID, la date de création et une validation humaine dans une fenêtre modale. Le mode CLI autonome n'a pas d'interface à piloter : ses sept opérations de base restent disponibles, avec `--allow-details` pour le rapport avancé et `--allow-stop` pour l'arrêt non interactif.

## CLI autonome

Le compagnon console est placé à côté de `CyWinTask.exe` et du dossier `bridge`. Il ne lance pas l'interface. Les résultats sont du JSON sur stdout ; les erreurs sont écrites sur stderr. Code de sortie 0 en cas de succès, 1 pour l'échec de l'opération ou du service et 2 pour une commande incomplète ou inconnue.

```powershell
./dist-native/CyWinTaskCli.exe --help
./dist-native/CyWinTaskCli.exe list
./dist-native/CyWinTaskCli.exe describe process.list
./dist-native/CyWinTaskCli.exe invoke system.summary '{}'
./dist-native/CyWinTaskCli.exe invoke process.list '{"query":"exemple","limit":10}'
```

La commande `invoke system.summary '{}'` a été exécutée sur l'application compilée. Pour un arrêt non interactif, ajouter `--allow-stop` à **cette invocation** et transmettre `pid`, `created` et `confirm:true`. La confirmation d'une opération réelle doit provenir de la décision de l'utilisateur ; la présence du drapeau ne remplace pas l'identité du processus ou la confirmation. Dans l'interface, chaque demande d'arrêt autorisée affiche aussi une confirmation modale, même si un autre onglet est ouvert.

## Chat intégré

Ouvrir l'onglet **Assistant** ou démarrer `CyWinTask.exe --assistant` (`Ctrl+3` depuis l'interface). Sélectionner un fournisseur et son modèle, puis **Connecter**. La détection des fournisseurs est déclenchée par **Détecter**. Ollama et les autres modèles locaux disponibles peuvent sélectionner automatiquement un modèle si le champ reste vide. Les fournisseurs API exigent leur configuration habituelle ; le champ « Clé d'environnement » lit la clé dans une variable d'environnement, sans l'afficher. Les messages arrivent progressivement ; **Annuler** interrompt une génération. Les erreurs de connexion et de génération sont visibles dans le panneau.

Les pièces jointes sont sélectionnées manuellement au moyen du bouton du chat et utilisées seulement si le fournisseur accepte leur format. L'historique local est facultatif et désactivé par défaut ; si activé, il se trouve sous `%LOCALAPPDATA%\CyWinTask\chat`. La restauration est une option distincte. La fenêtre de contexte en Kio est réglable pour les fournisseurs qui la gèrent ; `0` conserve le comportement du fournisseur. En mode `--demo`, le panneau est visible mais ses connexions et ses actions réelles sont désactivées.

## MCP externe

Le transport **stdio** se lance directement depuis un client MCP en donnant l'exécutable et l'argument `mcp` :

```json
{"command":"D:\\chemin\\CyWinTask\\CyWinTaskCli.exe","args":["mcp"]}
```

Il utilise les flux du processus enfant et n'a pas d'authentification réseau. Pour autoriser les arrêts sur **cette instance**, ajouter `--allow-stop` aux arguments ; l'outil exige aussi `confirm:true` et la date de création. Ajouter `--allow-details` pour lire le rapport avancé.

Le serveur HTTP autonome écoute uniquement sur `127.0.0.1`, chemin `/mcp`. Définir un jeton aléatoire d'au moins 32 caractères dans `CYWINTASK_MCP_TOKEN`, puis lancer `CyWinTaskCli.exe serve 8765` ; `0` choisit un port libre et l'adresse exacte est écrite sur stderr. Le client envoie `Authorization: Bearer <jeton>`. Dans l'interface, cocher **Serveur MCP local** démarre un serveur HTTP local à port libre ; copier son adresse et son jeton depuis le panneau. Cette instance agit sur l'état réel lu par l'application ouverte.

Pour piloter cette instance depuis un terminal, copier le jeton affiché dans l'interface dans la variable d'environnement du terminal, puis utiliser `remote` avec l'adresse copiée :

```powershell
$env:CYWINTASK_MCP_TOKEN = '<jeton de la fenêtre>'
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp list
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp describe ui.navigate
./dist-native/CyWinTaskCli.exe remote http://127.0.0.1:8765/mcp invoke ui.navigate '{"page":"performance"}'
```

Remplacer `8765` par le port affiché dans l'interface. `remote invoke` renvoie le résultat MCP complet en JSON, notamment `structuredContent` ou `content`. Il renvoie un code d'échec si l'outil signale `isError`. Les droits de l'instance ouverte s'appliquent aussi à ces appels.

La révision utilisée prend en charge la découverte moderne `server/discover`, `tools/list` et `tools/call` sur stdio et HTTP, avec la version de protocole MCP `2026-07-28` et les métadonnées de capacités requises. Le catalogue reste assez petit pour la découverte complète. Le serveur annonce un TTL de découverte de 30 secondes ; un client peut rafraîchir la découverte à tout moment. Les résultats des mesures et des mutations ne sont pas mis en cache par CyWinTask. La pagination de `process.list` et `system.devices` et la limite de `ui.performance.history` bornent les réponses. Aucune recherche de fichiers, ressource textuelle, journal de changements, abonnement ou stockage de gros résultats sur disque n'est nécessaire pour ces opérations. Aucun serveur MCP d'une autre application n'est connecté au chat, car l'analyse des processus de CyWinTask n'en dépend pas.

## Vérifications locales

`Build-Native.ps1 -Checks` vérifie la connexion du chat à un fournisseur de test en mémoire, l'affichage progressif, l'annulation, une lecture du registre, le refus d'une confirmation et l'arrêt autorisé d'un **processus créé par le test**. Il vérifie aussi le catalogue de commandes d'interface, leur refus initial, le passage au thread UI et la révocation d'un droit. `python native/tests/mcp_integration.py` vérifie découverte, lecture, refus par défaut, confirmation et arrêt autorisé sur CLI/MCP, le jeton HTTP et la connexion CLI à une instance MCP en cours. Les tests ne nécessitent aucun fournisseur IA externe. Les captures de démonstration utilisent des mesures fictives.

Limites actuelles : le CLI autonome et le panneau ouvert possèdent chacun leur propre session du collecteur ; pour modifier la fenêtre ouverte, utiliser `remote` ou son serveur MCP. Les thèmes et la densité choisis uniquement au lancement ne se changent pas encore à chaud. L'appel d'un outil par un fournisseur IA réel dépend de la prise en charge des outils par le modèle choisi. L'annulation d'un arrêt déjà transmis au collecteur ne peut pas le défaire.

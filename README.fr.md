# CyWinTask

[English](README.md) · **Français**

Gestionnaire de tâches Windows x64. L’interface actuelle utilise **Dear ImGui**, **CyUI** et Direct3D 11 ; un collecteur .NET 8 séparé fournit les mesures Windows par un flux local. L’ancienne interface WPF reste dans le dépôt pour référence, mais les nouvelles distributions lancent l’interface native.

## Captures d’écran

Ces captures proviennent du mode démonstration de CyWinTask, avec des processus et du matériel fictifs.

**Processus** — tableau avec recherche, barres d’utilisation et groupes de processus associés repliables.

![Tableau des processus de CyWinTask en mode démonstration](docs/screenshots/processes.png)

**Performance** — graphiques compacts du CPU, de la mémoire, du GPU, des disques et du réseau.

![Tableau de bord Performance de CyWinTask en mode démonstration](docs/screenshots/performance.png)

## Démarrage et compilation

Depuis les sources, installer le SDK .NET 8 et Visual Studio 2022 avec les outils C++. CyUI est actuellement un dépôt privé : les personnes autorisées doivent récupérer sa révision figée avant de compiler. La commande suivante crée ensuite un dossier autonome `dist-native` :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/Build-Native.ps1
./dist-native/CyWinTask.exe
```

Avant la première compilation, exécuter `powershell -NoProfile -ExecutionPolicy Bypass -File native/Sync-CyUI.ps1` avec un accès GitHub au dépôt CyUI. Le commit utilisé est indiqué dans `native/CYUI_REVISION` ; la copie locale dans `third_party/CyUI` reste hors du dépôt public.

Le dossier `bridge` doit rester à côté de `CyWinTask.exe`. Le script accepte `-Offline` si les paquets .NET requis sont déjà dans le cache NuGet local. Le runtime .NET nécessaire au collecteur est inclus dans la sortie ; l’interface Dear ImGui est un binaire natif. CyWinTask ne remplace pas les raccourcis ni le gestionnaire de tâches de Windows.

Pour visualiser l’interface avec des valeurs fictives, lancer `./dist-native/CyWinTask.exe --demo`. L’option `--performance` ouvre directement l’onglet Performance.

Pour examiner la présentation, les options `--theme navy|mid-grey|light-grey|amber-night|violet`, `--density compact|comfortable` et `--ui-scale 1.25` changent respectivement le thème, la densité et l'échelle de cette session. Le lancement ordinaire conserve le thème navy et la densité comfortable.

La taille du texte et des contrôles est réglée à **115 %** au premier lancement. Les boutons **A− / A+** dans la barre supérieure ajustent le zoom de 100 à 170 % ; `Ctrl++`, `Ctrl+-` et `Ctrl+0` l’augmentent, le diminuent ou le rétablissent à 115 %. Le réglage est conservé pour les lancements suivants et s’adapte aussi à l’échelle d’affichage Windows.

## Processus

- Recherche immédiate par nom ou PID (`Ctrl+F`) et tri dans les deux sens par chaque colonne : nom, PID, CPU, mémoire, threads, handles.
- Filtres Applications, Arrière-plan et Processus Windows ; regroupement par type facultatif. Le classement utilise les fenêtres visibles et le chemin de l’exécutable lorsqu’il est accessible.
- Mode **Processus associés** : branches parent/enfants repliables. Un PID réutilisé ne rattache pas les enfants à un nouveau processus. Les valeurs affichées restent celles de chaque ligne, sans addition des descendants.
- Sélection multiple avec Ctrl ou Maj, mise en évidence distincte des cadres de groupe, barres d’utilisation CPU et mémoire, icônes d’exécutables accessibles chargées progressivement.
- Clic droit : arrêt avec confirmation, informations avancées, ouverture du dossier, copie du chemin ou du nom/PID. Les actions valident le PID et la date de création ; les processus critiques et CyWinTask lui-même ne peuvent pas être arrêtés depuis l’application. Seules les lignes sélectionnées sont visées, jamais automatiquement leurs enfants.

Les informations avancées incluent notamment le chemin, la date de démarrage, la session, la priorité, l’affinité, les compteurs de mémoire et les modules accessibles. Certains processus protégés limitent ces informations.

## Performance

L’onglet **Performance** propose les mesures CPU, mémoire physique et virtuelle engagée, disques, réseau et GPU. L’historique conserve au plus 60 secondes de mesures réelles ; il ne crée pas de points artificiels avant le lancement. Les compteurs manquants apparaissent comme indisponibles.

Chaque ressource affiche sa valeur courante sous son nom. Les graphiques comportent une grille avec échelles de valeur et repères temporels ; le survol donne les mesures du point le plus proche. Le graphique des disques utilise une échelle distincte pour le pourcentage d’activité et les débits en Mo/s.

- CPU : charge totale, processeurs logiques et grilles **Compacte (Windows)** ou **Détaillée**.
- Mémoire : physique utilisée, disponible et mémoire virtuelle engagée.
- Disques : activité, lecture et écriture sur un même graphique, avec trois couleurs.
- GPU : utilisation, moteurs et mémoires exposés par Windows. La température CPU/GPU apparaît seulement si un capteur compatible est disponible.
- **Vue graphiques** : tableau de bord compact par défaut, avec CPU, RAM, GPU, disques et réseau en cartes côte à côte. Le menu **Disposition** choisit les colonnes automatiques ou 2 à 4 colonnes, des cartes plus grandes et une vue limitée au CPU, à la RAM et aux GPU. **Fenêtre séparée** ouvre le même tableau de bord ; chaque carte peut aussi s’ouvrir seule. Les fenêtres réutilisent les mêmes mesures et la même pause.

Les valeurs CPU et GPU peuvent différer du Gestionnaire de tâches de Windows. Les températures dépendent des capteurs disponibles ; l’application n’installe pas de pilote. Les données GPU viennent des compteurs WDDM et de DXGI, et certains pilotes ne publient pas tous les moteurs.

## Architecture et performance

Le collecteur réutilise un buffer `NtQuerySystemInformation` pour les processus et des requêtes PDH pour les mesures de performance. Il envoie un instantané au frontal environ une fois par seconde. La collecte de performance ne s’active que dans l’onglet correspondant ou lorsqu’une fenêtre de graphiques est ouverte. Les icônes sont mises en cache et leur chargement est limité par trame ; le tableau ne dessine que les lignes visibles. Le rendu natif ralentit lorsque la fenêtre perd le focus et suspend le dessin lorsqu’elle est minimisée.

Le frontal et le collecteur sont compilés indépendamment : `native/CMakeLists.txt` et `Bridge/CyWinTask.Bridge.csproj`. L'interface native utilise les modules `CyUI::Core` et `CyUI::Plots` ; `CyUI::Canvas` est une dépendance de Plots. Audio, Gallery et Demo sont désactivés. L'application fournit un seul target Dear ImGui partagé avec CyUI. CyUI 1.2.0 est figé au commit indiqué dans `native/CYUI_REVISION` ; ses sources restent dans le dépôt privé CyUI. Les licences et notices des composants distribués se trouvent dans le dossier `licenses` de la sortie. Le frontal WPF historique reste constructible avec `dotnet build CyWinTask.csproj -c Release` ; ses tests existants se lancent avec `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release`.

Les tracés, la grille et les courbes utilisent `CyUI::Plots`. Ce module n'a qu'un axe Y : CyWinTask adapte les débits du disque à l'échelle du pourcentage et affiche les valeurs réelles sur l'axe droit et au survol. La barre fine du tableau des processus reste propre à l'application pour distinguer les charges inférieures à 1 %, que la barre arrondie de CyUI représente avec une largeur minimale.

## Portable et installateur

Le script ci-dessous construit le portable et l’installateur Inno Setup 6 à partir du frontal Dear ImGui :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/Build-Release.ps1 -Version 0.7.0 -InnoCompiler "C:\chemin\vers\ISCC.exe"
```

Les fichiers produits se trouvent dans `artifacts/releases/<version>/`. Le portable doit être extrait entièrement avant de lancer `CyWinTask.exe`. L’installateur place l’application dans le profil du compte courant, ajoute un raccourci au menu Démarrer et propose un raccourci bureau.

## Limites

La mémoire par processus est son working set ; leur somme ne représente pas la mémoire physique utilisée. Les processus protégés peuvent cacher leur chemin, leur icône ou leurs modules. Le regroupement applicatif reste une heuristique et ne reproduit pas exactement celle du Gestionnaire de tâches Windows. L’historique des applications, les programmes au démarrage et la gestion des services ne sont pas encore inclus.

Références : [NtQuerySystemInformation](https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation), [compteurs PDH](https://learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw), [DXGI](https://learn.microsoft.com/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc1).

# CyWinTask 0.7.0

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyWinTask/releases/tag/v0.7.0)

**Statut : en développement.** L’application publiée est la version native Windows x64. L’ancienne interface WPF reste dans le dépôt de sources pour référence ; elle n’est pas lancée par l’installateur ou le portable 0.7.0.

## Changements de la version 0.7.0

- Nouvelle interface native Dear ImGui construite avec CyUI et collecteur Windows .NET 8 séparé.
- Tableau compact des processus avec recherche, tri par colonne, regroupement facultatif par type, arborescence parent/enfants, multisélection et inspection avancée.
- Barres de charge CPU et mémoire, icônes de processus accessibles et arrêt confirmé depuis le menu contextuel.
- Tableau de bord Performance compact et graphiques détaillés du CPU, de la mémoire, des disques, du réseau et du GPU. Le tableau de bord et chaque graphique peuvent s’ouvrir dans une fenêtre séparée.
- Températures CPU/GPU lorsqu’un capteur compatible les fournit.
- Installateur et ZIP portable Windows x64 avec le runtime nécessaire au collecteur.

## Limites connues

- La mémoire par processus correspond au working set ; la somme des lignes n’est pas la mémoire physique totale utilisée.
- Les processus protégés peuvent masquer leur chemin, leur icône ou leurs modules. Le regroupement des applications est heuristique et peut différer du Gestionnaire de tâches Windows.
- Les valeurs CPU/GPU peuvent différer de celles du Gestionnaire de tâches Windows. Les températures et les moteurs GPU disponibles dépendent du matériel, des capteurs et des pilotes.
- L’historique des applications, la gestion des programmes au démarrage et celle des services ne sont pas encore inclus. CyWinTask ne remplace pas le Gestionnaire de tâches Windows ni ses raccourcis.
- La compilation depuis les sources demande un accès au dépôt privé CyUI ; l’installateur et le portable publiés comprennent l’interface compilée.

La [version 0.7.0](https://github.com/MrMybal/CyWinTask/releases/tag/v0.7.0) propose l’installateur, le ZIP portable et les sommes SHA-256. Elle ne publie pas de liste détaillée des bogues ; consulter le [suivi des problèmes](https://github.com/MrMybal/CyWinTask/issues) pour les problèmes signalés et confirmés.

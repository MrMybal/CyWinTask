# CyWinTask 0.8.0

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyWinTask/releases/tag/v0.8.0)

**Statut : en développement.** L’application publiée est la version native Windows x64. L’ancienne interface WPF reste dans le dépôt de sources pour référence.

## Changements de la version 0.8.0

- Ajout d’un Assistant intégré avec choix du fournisseur, chat et serveur MCP local.
- Ajout de `CyWinTaskCli.exe` pour les commandes JSON et les connexions MCP depuis un terminal ou un assistant externe.
- Ajout d’outils de lecture du système et des processus, de requêtes paginées sur les périphériques et les processus, et d’un rapport avancé sur un processus.
- Ajout de commandes pour la navigation, les filtres et la sélection de processus, la disposition des graphiques, les fenêtres détachables, la pause et le zoom dans l’instance graphique ouverte.
- Ajout de droits individuels par outil dans l’onglet Assistant, avec préréglages de lecture seule, contrôle de l’interface et accès complet. L’arrêt d’un processus exige une autorisation et une confirmation explicites.
- Mise à jour de l’installateur et du ZIP portable Windows x64 avec l’Assistant, le CLI et le guide d’intégration.

## Limites connues

- Le fournisseur d’IA doit prendre en charge les appels d’outils pour utiliser ceux de l’application. Les commandes d’interface nécessitent une instance graphique ouverte ; le CLI autonome n’a pas d’interface à piloter.
- Les droits des outils dans l’application graphique s’appliquent à la session en cours. Le CLI autonome dispose de drapeaux séparés `--allow-details` et `--allow-stop`.
- La mémoire par processus correspond au working set ; la somme des lignes n’est pas la mémoire physique totale utilisée.
- Les processus protégés peuvent masquer leur chemin, leur icône ou leurs modules. Le regroupement des applications est heuristique et peut différer du Gestionnaire de tâches Windows.
- Les valeurs CPU/GPU peuvent différer de celles du Gestionnaire de tâches Windows. Les températures et les moteurs GPU disponibles dépendent du matériel, des capteurs et des pilotes.
- L’historique des applications, la gestion des programmes au démarrage et celle des services ne sont pas encore inclus. CyWinTask ne remplace pas le Gestionnaire de tâches Windows ni ses raccourcis.
- La compilation depuis les sources demande un accès aux dépôts privés CyUI et CyMCPSDK ; les paquets publiés comprennent leurs composants compilés.

Voir le [guide Assistant, CLI et MCP](docs/CyMCPSDK.fr.md) pour les commandes, les connexions et les droits. La [version 0.8.0](https://github.com/MrMybal/CyWinTask/releases/tag/v0.8.0) propose l’installateur, le ZIP portable et les sommes SHA-256.

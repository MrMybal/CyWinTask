# CyWinTask 0.9.0

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyWinTask/releases/tag/v0.9.0)

**Statut : en développement.** Cette version comprend l’application native Windows x64, son collecteur et le CLI autonome.

## Changements de la version 0.9.0

- Mise à jour de l’Assistant et de MCP vers CyMCPSDK 0.7.2.
- Ajout de quatre profils d’accès MCP locaux. **Free** est le profil par défaut : les clients se connectent à l’adresse locale sans jeton et peuvent appeler toutes les opérations enregistrées, y compris les modifications, sans confirmation supplémentaire. Les schémas d’arguments et la vérification de l’identité des processus restent appliqués.
- Conservation de **Local contrôlé** pour l’accès par adresse avec droits par outil, de **Jeton seul** pour l’accès à toutes les opérations avec jeton, et de **Protégé** pour l’accès avec jeton et droits par outil.
- Enregistrement des profils choisis explicitement et migration vers Free des configurations sans profil sélectionné.
- Mise à jour du CLI : le serveur HTTP MCP démarre en Free par défaut et le client rejoint un serveur Free ouvert avec son adresse seule. L’application graphique peut démarrer son serveur local avec `--mcp-local PORT`.
- Reconstruction de l’installateur et du ZIP portable Windows x64 avec ces changements.

## Limites connues

- En Free, tout processus du même ordinateur peut appeler les opérations exposées, y compris l’arrêt d’un processus. Choisir Local contrôlé ou Protégé lorsqu’un contrôle des droits est nécessaire.
- Le chat et le transport stdio du CLI conservent leurs droits contrôlés distincts. Le fournisseur doit prendre en charge les appels d’outils pour utiliser ceux de l’Assistant.
- Les commandes d’interface nécessitent une instance graphique ouverte ; le CLI autonome n’a pas d’interface à piloter.
- Les processus protégés peuvent masquer leur chemin, leur icône ou leurs modules. Les valeurs CPU/GPU et les températures dépendent des compteurs Windows, du matériel et des pilotes.
- L’historique des applications, la gestion des programmes au démarrage et celle des services ne sont pas encore inclus. CyWinTask ne remplace pas le Gestionnaire de tâches Windows ni ses raccourcis.
- La compilation depuis les sources demande un accès aux dépôts privés CyUI et CyMCPSDK ; les paquets publiés comprennent leurs composants compilés.

Voir le [guide Assistant, CLI et MCP](docs/CyMCPSDK.fr.md) pour les profils, les commandes et les connexions. La [version 0.9.0](https://github.com/MrMybal/CyWinTask/releases/tag/v0.9.0) propose l’installateur, le ZIP portable et les sommes SHA-256.

# CyWinTask 0.11.0

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyWinTask/releases/tag/v0.11.0)

**Statut : en développement.** Cette version comprend l’application native Windows x64, son collecteur et le CLI autonome.

## Changements de la version 0.11.0

- Remplacement de la barre supérieure chargée par une barre latérale CyUI avec indicateurs CPU, RAM, GPU et VRAM, et mode à icônes pour les fenêtres étroites.
- Refonte de Processus : tableau plus lisible, liens parents/enfants plus nets, cellules colorées selon la charge et panneau latéral d’inspection.
- Refonte de Performance : cartes de graphiques compactes, légendes avec valeurs, processus les plus actifs, capteurs clés et détails en cartes.
- Présentation améliorée des pages Températures, Système et Assistant, avec une Console persistante au style terminal.
- Passage du profil MCP local par défaut à **Local contrôlé** : connexion par adresse avec droits par outil. **Free** reste disponible sur sélection explicite.
- Corrections de la gestion des icônes, de l’arrêt de la console, des réponses d’interface lorsque la fenêtre est réduite et des erreurs temporaires du collecteur.

## Déjà présent dans la version 0.10.1

- Ajout du tri par clic sur chacune des colonnes des processus GPU, avec comparaison numérique pour la confiance, les charges CPU/GPU, la mémoire du processus et les mémoires GPU en KiB, MiB ou GiB.
- Placement des mesures indisponibles en fin de tri numérique.

## Autres changements depuis la version 0.9.0

- Intégration de CyHWInfo pour l’inventaire matériel, la télémétrie GPU, les compteurs GPU par processus et les capteurs avancés disponibles, avec repli lorsque PawnIO est inaccessible.
- Ajout de l’arborescence des capteurs, du sous onglet des processus GPU, des barres de confiance et d’utilisation, de l’inspection rapide et du menu contextuel.
- Ajout des pourcentages CPU, RAM, GPU et VRAM sur la page Processus et resserrement des cartes de graphiques et de la page Capteurs.
- Affichage de la température et de l’utilisation totale dans les détails GPU, avec le raccourci vers les processus GPU à côté de l’en-tête.
- Reconstruction de l’installateur et du ZIP portable Windows x64 à partir de l’application native mise à jour.

## Déjà présent dans la version 0.9.0

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
- La compilation depuis les sources demande un accès aux dépôts privés CyUI, CyMCPSDK et CyHWInfo ; les paquets publiés comprennent leurs composants compilés.

Voir le [guide matériel](docs/Hardware.fr.md) et le [guide Assistant, CLI et MCP](docs/CyMCPSDK.fr.md). La [version 0.11.0](https://github.com/MrMybal/CyWinTask/releases/tag/v0.11.0) fournit l’installateur, le ZIP portable et les sommes SHA-256.

# Analyse matérielle dans CyWinTask 0.11.0

[README](../README.fr.md) · [Notes de version](../VERSION.fr.md)

## Processus GPU

Ouvrir **Processus → Processus GPU**. Chaque ligne associe un processus à un adaptateur GPU. Un même PID peut donc apparaître sur plusieurs GPU. Le tableau affiche l'utilisation du moteur le plus chargé, la mémoire GPU locale et partagée, ainsi que la confiance, le CPU et la mémoire du processus lorsque ces informations sont accessibles.

Cliquer sur un en-tête trie la colonne ; cliquer de nouveau inverse l'ordre. Les mémoires sont triées selon leur quantité réelle, quelle que soit l'unité affichée (KiB, MiB ou GiB). Les mesures indisponibles restent en fin de tri numérique. Cliquer sur une ligne ouvre l'inspection rapide ; un double-clic retrouve le processus dans la liste générale. Le clic droit propose les actions de processus disponibles.

La note de confiance provient d'une vérification locale de la signature Authenticode et de l'emplacement du fichier. Elle ne remplace pas une analyse de sécurité. Certains processus système ou protégés ne donnent ni chemin ni note.

## Performance et capteurs

L'onglet **Performance** présente des cartes compactes ou des lignes pour le CPU, la mémoire physique et virtuelle, les GPU, les disques et le réseau. La vue détaillée permet de choisir une ressource et d'ouvrir ses courbes dans une fenêtre séparée. Les graphiques conservent jusqu'à 60 secondes de mesures réelles.

L'onglet **Températures** classe les capteurs par matériel dans une arborescence. Il affiche la valeur actuelle et, lorsqu'elles existent, les valeurs minimale, maximale et moyenne de la session. Le filtre permet de n'afficher que les capteurs disponibles. Un journal CSV HWiNFO peut être importé séparément.

CyHWInfo fournit l'inventaire CPU, GPU, mémoire et carte mère, la télémétrie GPU et les capteurs avancés accessibles. Le collecteur avancé utilise .NET 8. Certains capteurs exigent le pilote PawnIO signé, à installer explicitement depuis [pawnio.eu](https://pawnio.eu/). Si le pilote est absent ou inaccessible, l'application conserve les mesures natives et indique les capteurs concernés comme indisponibles. Le redémarrage en administrateur peut donner accès à davantage de capteurs, sans garantir que le pilote fonctionne.

La disponibilité des températures, des moteurs GPU et de la mémoire vidéo dépend du matériel et des pilotes. Une valeur zéro fournie par un capteur est affichée comme une mesure valide.

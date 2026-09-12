# CyWinTask

Application native Windows x64 en C# / WPF (.NET 8).

## Démarrage

Lancer `dist\CyWinTask.exe`. Le runtime .NET Desktop 8 x64 est requis. Depuis les sources, installer le SDK .NET 8 sur Windows x64 puis utiliser les commandes de compilation ci-dessous pour générer le dossier `dist`. CyWinTask reste indépendant du gestionnaire Windows et ne modifie pas les raccourcis système.

## Processus

- CPU, mémoire résidente, threads et handles ; tri par clic sur chacune des six colonnes (second clic pour inverser, flèche visible, valeurs numériques triées numériquement globalement par défaut, ou dans chaque groupe si le regroupement est activé) et recherche par nom ou PID (`Ctrl+F`).
- Multisélection avec Ctrl et Maj ; arrêt avec confirmation et bilan des échecs. Identité validée par PID + date de création. Arrêt refusé pour CyWinTask lui-même et les processus critiques.
- Icônes extraites des exécutables accessibles, chargées progressivement en arrière-plan et mises en cache. Symbole neutre si inaccessible ou sans icône.
- Menu **Afficher** : tous les processus, Applications, arrière-plan ou Windows ; se combine avec la recherche. Case **Regrouper par type** facultative, désactivée au démarrage pour permettre un tri global.
- Groupes Applications, Processus en arrière-plan et Processus Windows. Applications = processus possédant une fenêtre visible de premier niveau ; Windows = processus sans cette fenêtre et dont le chemin accessible est sous le dossier Windows, ainsi que System. Ce classement pratique ne reproduit pas exactement les règles internes de Task Manager ; un processus protégé peut rester en arrière-plan faute de chemin accessible.
- Inspecteur : chemin, PID parent, date de lancement et compteurs au moment de son ouverture.

## Performance

Sélectionner **Performance**, puis une ressource dans la liste :

- Processeur : charge totale et option pour afficher chaque processeur logique.
- Mémoire : mémoire physique utilisée et disponible.
- Disques physiques : temps actif, lecture et écriture.
- Interfaces réseau : réception et envoi, y compris les interfaces virtuelles.
- GPU : cartes identifiées par DXGI et associées aux compteurs par leur LUID ; 3D, copie, encodage, décodage, mémoire dédiée et partagée. Les autres moteurs sont accessibles par une case à cocher.

Historique réel de 60 secondes, sans remplissage artificiel du passé. Les trous de collecte restent des trous. Les taux utilisent au moins deux échantillons ; les premiers compteurs peuvent être indisponibles. Les échelles des pourcentages sont fixes à 100 %, celles des débits sont automatiques et indiquées sur chaque graphique. L’utilisation GPU correspond au moteur physique le plus occupé, après addition des contributions des processus sur un même moteur.

La disponibilité des compteurs GPU dépend du pilote. Un moteur non exposé est marqué indisponible. Températures, versions des pilotes, caractéristiques détaillées des disques, historique des applications, démarrage et services ne sont pas encore inclus. La détection des cartes graphiques est effectuée à la première ouverture de Performance ; relancer CyWinTask après un changement matériel GPU.

## Coût et architecture

- Collecte des processus groupée via NtQuerySystemInformation ; buffer natif réutilisé, sans WMI ni handle ouvert par processus à chaque rafraîchissement.
- Collecte PDH native uniquement dans l’onglet Performance, query et buffers conservés. Pas de processus PowerShell ni de commande externe dans la boucle de l’application.
- Une seule collecte en arrière-plan à la fois ; attente d’une seconde entre les cycles. Pause et minimisation suspendent la collecte.
- Tableau à lignes virtualisées, notifications ciblées, filtre différé de 150 ms. Les six colonnes sont rendues ensemble pour assurer leur visibilité avec les groupes.
- Extraction des métadonnées limitée à 32 nouveaux processus par cycle. Les chemins et icônes sont conservés pendant la vie de chaque processus.
- Graphiques dessinés directement par WPF, sans animation ni contrôle individuel par point. Historique borné à 60 secondes, maximum 120 points par série.

`diagnostics.json` et `performance-diagnostics.json` contiennent des mesures locales du banc de test. Elles ne prouvent pas une supériorité sur le gestionnaire Windows. La mémoire de WPF et le coût de la première extraction des icônes restent à surveiller. Le temps de mise à jour affiché n’inclut pas tout le rendu différé.

## Compilation et tests

```powershell
dotnet build CyWinTask.csproj -c Release
dotnet run --project Tests/CyWinTask.Tests.csproj -c Release
dotnet publish CyWinTask.csproj -c Release --no-restore -o dist
```

Le test d’intégration est adapté au poste de développement équipé d’un GPU avec compteurs WDDM. Il vérifie les compteurs réels, le regroupement, les icônes, la recherche, les sélections, la navigation et l’arrêt d’un processus créé uniquement pour le test. Les assertions purement logiques couvrent l’agrégation des moteurs GPU et la limite de l’historique. Les aperçus sont enregistrés dans `preview.png` et `preview-performance.png`.

## Limites techniques

L’API native de processus utilise le format Windows x64, dont les limites de buffer sont vérifiées. Ce format peut évoluer. Les accès restent soumis aux permissions Windows ; aucune élévation automatique. La mémoire par processus est son working set total, et sa somme ne représente pas la mémoire physique utilisée. Les mesures de temps CPU peuvent différer de Task Manager ; le compteur de la vue Processus nécessite une validation supplémentaire au-delà de 64 processeurs logiques.

Références Microsoft :
- https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation
- https://learn.microsoft.com/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw
- https://learn.microsoft.com/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc1

Test ciblé du tri : `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --sorting`. Vérifie les deux sens sur les six en-têtes, les flèches, le tri dynamique et les sélections.

## Graphiques et repères visuels (version 0.3)

- Mémoire : graphique supplémentaire de mémoire virtuelle engagée et limite mesurée avec GetPerformanceInfo. Il représente les allocations garanties par Windows ; ce n’est pas le volume réellement présent dans les fichiers de pagination.
- Disques : trois courbes superposées. Vert = activité (échelle gauche, 0–100 %), bleu = lecture, orange = écriture (échelle droite automatique commune en Mo/s). Une légende indique les valeurs courantes.
- CPU : cocher **Afficher les processeurs logiques**, puis choisir **Compacte (Windows)** pour la grille de mini-graphiques, ou **Détaillée** pour les graphiques plus grands. Le total reste affiché dans le résumé. Les processeurs logiques sont ordonnés numériquement.
- Processus : barres CPU et mémoire, valeurs toujours lisibles et tri numérique conservé. La largeur est proportionnelle à la capacité CPU totale ou à la RAM physique totale. La mémoire reste le working set ; les pages partagées peuvent être comptées dans plusieurs processus.
- Couleurs CPU : bleu sous 5 %, jaune dès 5 %, orange dès 20 %, rouge dès 50 %. Couleurs RAM : bleu sous 1 %, jaune dès 1 %, orange dès 4 %, rouge dès 10 % de la RAM totale. Ces seuils sont des repères visuels, pas des diagnostics. Les infobulles rappellent ces seuils.

Les graphiques et barres utilisent le dessin WPF direct, sans animations ni collecte supplémentaire par processus. Référence mémoire : https://learn.microsoft.com/windows/win32/api/psapi/ns-psapi-performance_information

## Processus associés (version 0.4)

Cocher **Processus associés** pour afficher une arborescence dépliable. Le nombre entre parenthèses compte les descendants ; les valeurs CPU, RAM, threads et handles restent celles de la ligne individuelle, sans addition des enfants. Cliquer sur le chevron pour déplier ou replier.

Les liens reposent sur le PID parent enregistré par Windows et les dates de création. Un parent disparu ou un PID réutilisé ne produit pas de rattachement à un nouveau processus. Les applications possédant une fenêtre visible démarrent leur propre branche pour éviter de toutes les ranger sous Explorer. Ce modèle ne reproduit pas les heuristiques internes de regroupement applicatif de Task Manager et ne regroupe pas arbitrairement les processus portant le même nom.

La recherche conserve les ancêtres nécessaires pour situer un enfant et ouvre les branches correspondantes. Chercher un parent montre aussi ses descendants. Le repliage manuel reste possible pendant la recherche. Le filtre de type porte sur la racine de la branche : Applications inclut ainsi les processus de fond de ces applications. Le regroupement par type reste compatible.

Le tri agit entre les racines et entre les enfants du même parent. Décocher le mode associé rétablit le tri global. Les ouvertures sont conservées par PID et date de création, avec suppression des identités disparues. L’arrêt cible uniquement les lignes explicitement sélectionnées, jamais automatiquement les descendants cachés.

Tests dédiés : `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --tree`. Option de lancement : `dist\CyWinTask.exe --tree`.

## Menu contextuel et sélection (version 0.5)

Clic droit sur une ligne : arrêter le processus ou les lignes sélectionnées avec confirmation, ouvrir les informations avancées, afficher le fichier dans Explorer, copier le chemin ou le nom/PID. Le clic droit sur une ligne déjà sélectionnée conserve la multisélection ; sur une autre ligne, il sélectionne uniquement cette ligne. Les actions d’arrêt et de fichier conservent la validation PID + date de création. Aucun arrêt implicite des enfants.

La fenêtre avancée charge ses données en arrière-plan à la demande : chemin, version et éditeur déclarés dans le fichier, date de lancement, session, priorité, affinité, temps CPU, mémoires résidente/privée/virtuelle, threads, handles et jusqu’à 200 modules. C’est un instantané ; certains processus protégés limitent les informations disponibles. L’éditeur déclaré ne constitue pas une vérification de signature.

La sélection utilise un fond violet et un contour doré, distincts du cadre bleuté des branches. Test ciblé : `dotnet run --project Tests/CyWinTask.Tests.csproj -c Release -- --context`.

## Fichiers partagés sur GitHub

Le dépôt contient les sources, les tests et le logo final dans `Assets/cywintask-logo-blue.png`. Les sorties de compilation (`bin`, `obj`, `dist`), les réglages personnels des IDE, les historiques locaux des outils, les diagnostics et les captures `preview*.png` restent locaux grâce au `.gitignore`.

Les tests peuvent enregistrer des noms de processus, des PID, des chemins et des caractéristiques de la machine dans les diagnostics ou captures. Ne jamais publier ces fichiers réels : utiliser uniquement des données fictives pour les démonstrations et captures partagées. Ne pas publier de secrets ni de certificats privés ; les exclusions Git ne remplacent pas une revue des nouveaux fichiers.

Avant un commit, contrôler `git status --short` et `git diff --cached`. Pour consulter les fichiers non suivis qui seraient inclus, utiliser `git ls-files --others --exclude-standard`. Le `.gitignore` ne retire pas les fichiers déjà suivis.

## Binaires Windows x64

Les distributions autonomes incluent .NET Desktop : aucune installation séparée du runtime n’est nécessaire.

- **Portable** : extraire tout le ZIP puis lancer `CyWinTask.exe`. Conserver tous les fichiers du dossier extrait.
- **Installateur** : installation pour le compte courant dans `%LOCALAPPDATA%\Programs\CyWinTask`, raccourci du menu Démarrer et raccourci bureau facultatif. Désinstallation via les paramètres Windows.

Pour fabriquer les deux distributions, utiliser le SDK .NET 8 ou supérieur et [Inno Setup 6](https://jrsoftware.org/isdl.php) :

```powershell
powershell -NoProfile -File packaging/Build-Release.ps1 -Version 0.5.2 -InnoCompiler "C:\chemin\vers\ISCC.exe"
```

Les livrables et leurs empreintes SHA-256 sont placés dans `artifacts/releases/0.5.2/`. Ce dossier est ignoré par Git ; les binaires sont destinés aux pièces jointes des versions GitHub (Releases). Le script utilise un dossier de publication neuf à chaque exécution pour éviter d’intégrer des fichiers provenant d’une ancienne compilation. Les binaires ne sont pas signés avec un certificat de signature de code.

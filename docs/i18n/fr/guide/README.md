# Guide d’utilisation de DiaDraw

Ce guide s’adresse aux personnes qui dessinent des diagrammes avec DiaDraw, dans l’éditeur de texte, sur le canevas ou les deux. Il explique à quoi servent les fonctionnalités et quand les utiliser. Pour la syntaxe exacte, il renvoie à la [référence du DSL](../dsl.md) plutôt que de la répéter.

## Chapitres

- [Exemples](examples.md) : les diagrammes fournis avec chaque application (C4, organigrammes de traitement, diagrammes UML de classes, de séquence, d’activité, d’états, de cas d’utilisation et de composants, ainsi que BPMN) et la façon dont ils sont dessinés.
- [Relations](relationships.md) : relier des éléments, choisir flèches, libellés et ports, reconnecter une ligne sur le canevas et savoir quand nommer une relation.
- [Extrémités et styles de trait](line-styles.md) : flèches, losanges, pattes d’oie et autres extrémités ; traits pointillés, doubles, courbes ou arrondis ; comment les choisir dans l’application ou dans le texte.
- [Dispositions](layouts.md) : les algorithmes de disposition (`layered`, `tree`, `grid`, `radial`, `force`, `fixed`, `sequence`), leur choix et leur interaction avec les indications de placement et les conteneurs.
- [Modifier les propriétés sur le canevas](element-properties.md) : l’inspecteur des éléments et des relations, ce que chaque champ écrit, comment nommer une ligne et comment les modifications se traduisent dans le texte.
- [Styles et thèmes sur le canevas](styles-and-themes.md) : les blocs de style pour les types, les étiquettes et les relations, le choix et la création de thèmes, et la transcription des modifications dans le texte.
- [Déplacer des blocs](moving.md) : faire glisser un bloc dans n’importe quelle vue, comprendre la ligne `pin … at` qui en conserve la position, le placer par rapport à ses voisins avec Option ou sélectionner plusieurs blocs pour les déplacer ou les supprimer ensemble.
- [Redimensionner et faire pivoter](resize-and-rotate.md) : agrandir, réduire ou faire pivoter un bloc dans toute vue, et conserver ces changements grâce aux lignes `size` et `rotate`.
- [La grille](grid.md) : afficher une grille sur le canevas et y aligner les déplacements, les redimensionnements et les points de passage tant qu’elle est visible.
- [Dessiner à la main dans les vues fixes](fixed-views.md) : déplacer et redimensionner des blocs, infléchir des lignes sur le canevas et conserver le résultat grâce aux lignes `pin` et `route`.
- [Dessiner avec des agents IA](agents.md) : le serveur MCP (`diadraw mcp`) et la compétence pour Claude Code, qui permettent à un agent comme Claude Code d’écrire, vérifier, rendre et corriger des diagrammes.
- [Diagrammes dans Markdown](markdown.md) : écrire un diagramme dans un bloc ` ```diadraw ` d’une page Markdown, et le dessiner sur les sites de documentation (remark, markdown-it) ou dans le navigateur (`diadraw.js`).
- [Diagrammes dans Confluence](confluence.md) : la macro DiaDraw diagram pour Confluence Cloud, son éditeur, les thèmes, l’import depuis Mermaid et PlantUML, l’enregistrement des diagrammes en images et la publication de fichiers `.draw` depuis un dépôt.
- [Google Drive et OneDrive](cloud-storage.md) : ouvrir et enregistrer des diagrammes dans l’un ou l’autre service depuis chaque application, la boîte de dialogue Storage de l’application web, ce que DiaDraw peut y voir et comment enregistrer une version de DiaDraw auprès de Google et de Microsoft.
- [Signaler un bogue et donner son avis](feedback.md) : les commandes Report a Bug et Give Feedback dans chaque application, ce qu’elles renseignent pour vous et ce qu’elles ne transmettent jamais.

D’autres chapitres viendront enrichir le guide : modèles et vues, notes et décorations, diagrammes de séquence, bibliothèques et importation depuis d’autres outils.

# Dessiner avec des agents IA

Les diagrammes DiaDraw sont du texte brut : un agent IA les écrit aussi facilement que du code. L’agent écrit un fichier `.draw`, le vérifie, en fait le rendu, regarde l’image et corrige ce qui se lit mal, la même boucle qu’une personne suit dans l’application. Deux éléments facilitent cette boucle et fonctionnent bien ensemble :

- **Le serveur MCP**, `diadraw mcp` : donne à tout agent qui parle le [Model Context Protocol](https://modelcontextprotocol.io) (Claude Code, Claude Desktop et d’autres) des outils pour vérifier, rendre et importer des diagrammes. Le rendu renvoie une image : l’agent voit le diagramme qu’il a dessiné.
- **La compétence (skill) pour Claude Code** : apprend à Claude Code le langage et la boucle vérifier, rendre, regarder et corriger. Installez-la avec `/plugin marketplace add mykulyak/diadraw` puis `/plugin install diadraw@diadraw` (voir [plugins/diadraw/README.md](../../plugins/diadraw/README.md)).

## Ce que l’agent peut faire

| Outil       | Ce qu’il fait                                                                                                                          |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `check`     | Signale les erreurs et avertissements sous la forme `ligne:colonne: gravité: message` et liste les vues du document                    |
| `render`    | Dessine une vue et la renvoie en image PNG (ou en SVG) ; peut aussi écrire un fichier `.svg`, `.png` ou `.pdf`                         |
| `format`    | Renvoie le document sous sa forme canonique, ou réécrit le fichier                                                                     |
| `import`    | Convertit des diagrammes Mermaid, PlantUML, Structurizr, draw.io et BPMN en document DiaDraw                                           |
| `reference` | Liste les types d’éléments, formes, thèmes, dispositions et bibliothèques, ou les types et formes d’une bibliothèque (`library: "c4"`) |
| `examples`  | Liste les [diagrammes d’exemple](examples.md) et renvoie n’importe lequel, pour que l’agent parte d’un modèle proche de sa tâche       |

Chaque outil prend le document soit en `source` (son texte), soit en `path` (un fichier). Préférez les fichiers de votre projet : le diagramme vit alors dans le contrôle de version comme tout le reste. Les bibliothèques sont résolues hors ligne, comme en ligne de commande : les bibliothèques intégrées, les jeux de gabarits de draw.io et ce que `diadraw lib install` a placé dans le cache.

## Mise en place

Le serveur fait partie de l’outil en ligne de commande `diadraw`. Les paquets Linux et Windows l’incluent ; ailleurs, compilez-le depuis le dépôt (voir [src/README.md](../../src/README.md)) :

```sh
cmake --preset release && cmake --build --preset release
# l’exécutable est ../diadraw-build/release/diadraw
```

Indiquez ensuite à votre agent où il se trouve. Donnez son chemin complet s’il n’est pas dans votre `PATH`.

### Claude Code

```sh
claude mcp add diadraw -- /chemin/vers/diadraw mcp
```

Le serveur est ajouté au projet courant, pour vous seul. Ajoutez `--scope user` pour l’avoir dans tous vos projets, ou `--scope project` pour l’écrire dans le fichier `.mcp.json` du projet et le partager avec toutes les personnes qui y travaillent. Tapez `/mcp` dans Claude Code pour vérifier la connexion.

### Claude Desktop

Ouvrez **Settings › Developer › Edit Config** et ajoutez DiaDraw à `claude_desktop_config.json` :

```json
{
  "mcpServers": {
    "diadraw": {
      "command": "/chemin/vers/diadraw",
      "args": ["mcp"]
    }
  }
}
```

Redémarrez Claude Desktop. Claude Desktop lance le serveur en dehors de tout projet : demandez-lui d’utiliser des chemins complets pour les fichiers qu’il lit et écrit.

### Autres agents

Tout client MCP peut lancer `diadraw mcp` comme serveur local (stdio) : il lit un message JSON-RPC par ligne sur l’entrée standard et répond sur la sortie standard.

## Demander un diagramme

Demandez avec vos propres mots et indiquez où placer le fichier, par exemple :

> Dessine les conteneurs de notre service de paiement en diagramme C4 dans `docs/checkout.draw`, fais-en le rendu dans `docs/checkout.svg` et vérifie que l’image se lit bien.

L’agent écrit le fichier, lance `check` jusqu’à ce qu’il n’y ait plus d’erreur, puis appelle `render` et regarde l’image. Si des boîtes se chevauchent ou si le flux va dans le mauvais sens, il modifie la vue, par exemple avec `layout layered direction right` ou `pin db below api`, et refait le rendu. Vous pouvez ensuite ouvrir le même fichier dans n’importe quelle application DiaDraw et continuer à le modifier à la main ou sur le canevas.

# Bibliothèques de formes

Une bibliothèque est un ensemble nommé et versionné de **formes** (dessins), de **formes composites** (assemblages d’autres formes), de **types** (types d’éléments non intégrés au DSL) et de **fragments** (extraits de source DiaDraw à insérer). Elle contient aussi des **correspondances** avec les noms utilisés par d’autres outils. C’est ainsi qu’on étend DiaDraw : un greffon est une bibliothèque. Les documents nomment les bibliothèques qu’ils utilisent ; les applications hôtes les résolvent. Ni le rendu ni l’importation n’accèdent au réseau.

```
use "c4@^1"
use "aws-icons@^2.1" as aws

model {
  user = person "User" shape c4.person
  fn = component "Handler" tech "Node" shape aws.lambda
  user -> fn
}
```

- `use "<name>[@<range>]" [as <alias>]` précède le modèle. Sans `as`, l’alias est le dernier segment du nom, les caractères non admis étant remplacés par `_` (`@acme/cloud-shapes` → `cloud_shapes`). Chaque alias doit être unique dans le document.
- `shape <alias>.<shape>` désigne une forme simple ou composite de la bibliothèque. Si l’hôte ne peut pas résoudre la référence, il utilise la forme habituelle du type de l’élément ; `diadraw check`, `diadraw render` et l’éditeur émettent un avertissement.
- `<id> = <alias>.<kind>` déclare un élément d’un type ajouté par la bibliothèque (voir [Types d’éléments](#types-déléments)). Un type inconnu est représenté par un bloc et produit le même avertissement.
- Les plages de versions suivent semver : `^1`, `~1.2`, `1.x`, `>=1.2 <2`, `1.2 - 1.6`, `^1 || ^2`, ou des versions exactes.

## Résolution

Pour résoudre un `use`, DiaDraw cherche dans cet ordre :

1. la version fixée par `diadraw-lock.json` pour cette combinaison exacte `name@range` (le fichier de verrouillage le plus proche, dans le dossier du document ou un dossier parent), si elle est en cache ;
2. la version en cache la plus élevée qui satisfait la plage ;
3. la version intégrée la plus élevée qui satisfait la plage.

Le cache se trouve dans `$DIADRAW_CACHE`, à défaut dans `$XDG_CACHE_HOME/diadraw/libraries`, sinon dans `~/.cache/diadraw/libraries`. Il contient une archive JSON par version. Rien n’est téléchargé à cette étape : une bibliothèque absente de tous ces emplacements est signalée comme non installée. L’application web ne dispose que des bibliothèques intégrées.

## Commandes

```bash
diadraw lib init shapes --name team-shapes   # manifest, an example shape and fragment, README
diadraw lib build shapes                     # check and build → shapes/dist/library.json
diadraw lib pack shapes                      # → shapes/dist/team-shapes-0.1.0.ddlib and its integrity
diadraw lib add shapes/dist/team-shapes-0.1.0.ddlib   # install into the cache, pin in diadraw-lock.json
diadraw lib add https://example.com/libs/aws-icons-2.1.0.ddlib
diadraw lib add c4@^1                        # pin a built-in (or cached) library
diadraw lib install                          # make the cache hold everything the lockfile pins
diadraw lib list                             # built-in and installed libraries
diadraw lib build --drawio stencils.min.js --set aws4,flowchart -o libs
```

`add` accepte un dossier de bibliothèque, une archive `.ddlib` ou `.json`, une URL `http(s)` ou le `name[@range]` d’une bibliothèque intégrée ou en cache. La commande affiche la ligne `use` à coller. Seules `add` et `install` utilisent le réseau, uniquement pour les URL fournies à `add` ou inscrites dans le fichier de verrouillage. `install` vérifie chaque archive par rapport à la version fixée et au champ `integrity` du fichier de verrouillage (`sha256-…` du JSON canonique de l’archive).

Le fichier de verrouillage `diadraw-lock.json` est destiné à être versionné :

```json
{
  "lockfileVersion": 1,
  "libraries": {
    "c4@^1": { "version": "1.0.0", "resolved": "builtin", "integrity": "sha256-…" },
    "team-shapes@^0.1.0": {
      "version": "0.1.0",
      "resolved": "libs/team-shapes-0.1.0.ddlib",
      "integrity": "sha256-…"
    }
  }
}
```

`resolved` est un chemin relatif au fichier de verrouillage, une URL, `builtin` ou `cache:<name>@<version>` pour une bibliothèque présente uniquement dans le cache local (que `install` ne peut pas restaurer ailleurs).

## Le manifeste

Le dossier source d’une bibliothèque contient `diadraw-library.json` :

```json
{
  "name": "team-shapes",
  "version": "0.1.0",
  "description": "Shapes for our services",
  "license": "MIT",
  "source": { "kind": "original" },
  "shapes": {
    "gateway": {
      "label": "API gateway",
      "category": "Edge",
      "kind": "component",
      "svg": "shapes/gateway.svg",
      "width": 48,
      "height": 48,
      "keywords": ["api", "ingress"]
    },
    "store": { "label": "Store", "base": "cylinder", "kind": "database" }
  },
  "kinds": {
    "edge": {
      "label": "Edge service",
      "description": "Anything that terminates client traffic",
      "shape": "gateway",
      "stereotype": "edge"
    },
    "topic": { "label": "Topic", "base": "hexagon", "fill": "#fde68a", "stroke": "#b45309" }
  },
  "composites": {
    "gateway_with_store": {
      "width": 120,
      "height": 48,
      "parts": [
        { "shape": "gateway", "x": 0, "y": 0, "width": 48, "height": 48 },
        { "shape": "store", "x": 72, "y": 0, "width": 48, "height": 48 }
      ]
    }
  },
  "fragments": {
    "edge": {
      "label": "Gateway in front of a service",
      "params": [{ "name": "service", "default": "api" }],
      "text": "fragments/edge.draw"
    }
  },
  "mappings": { "drawio": { "mxgraph.team.gateway": "gateway" } },
  "themes": {
    "corporate": {
      "extends": "default",
      "background": "#fafafa",
      "edge": "#4a4a4a",
      "nodes": { "system": { "fill": "#aa0000", "stroke": "#7a0000", "text": "#ffffff" } }
    }
  },
  "dependencies": { "c4": "^1" }
}
```

| Champ | Signification |
| --- | --- |
| `name`, `version` | nom au format npm (minuscules, `-._`, préfixe `@scope/` facultatif), version semver |
| `license` | expression SPDX pour les dessins ; `licenseNotes` pour les conditions supplémentaires |
| `source` | origine des dessins : `original`, `drawio-stencils`, `plantuml-stdlib`, `svg`, `other`, avec `url`, `commit`, `sets` |
| `shapes` | identifiant → `svg` (chemin ou balisage en ligne) ou `base` (forme de base du moteur de rendu), `label`, `category`, `kind` pour les nouveaux éléments, taille, `keywords`, `style` |
| `composites` | forme assemblée à partir de `parts`, chacune étant une forme de cette bibliothèque (ou `dep.shape` d’une dépendance) placée dans le cadre de la forme composite |
| `kinds` | identifiant → type d’élément : `shape` (forme simple ou composite de cette bibliothèque, ou `dep.shape`) ou `base`, `fill`, `stroke`, `fontColor`, `stereotype`, `label`, `description`, `category`, `keywords` |
| `fragments` | source DiaDraw (fichier `.draw` ou texte en ligne) à insérer dans `model { … }` ; `${param}` reçoit un paramètre, `${alias}` l’alias attribué à la bibliothèque |
| `open` | accepter tout nom de forme et la dessiner avec `base` (pour les jeux d’icônes dont les noms ne sont pas énumérés) |
| `mappings` | pour chaque format (`plantuml`, `plantuml-include`, `plantuml-local`, `structurizr`, `drawio`, `bpmn`), nom externe → identifiant de forme |
| `themes` | identifiant → thème comme dans `theme … { … }` ([dsl.md](dsl.md#vos-propres-thèmes)) : `extends` (thème intégré, de cette bibliothèque ou `dep.theme`), clés de thème, `nodes` |
| `dependencies` | bibliothèques sur lesquelles reposent les formes composites, `name` → plage de versions |

Le `kind` d’une forme simple ou composite (le type de l’élément que la palette ajoute pour elle) désigne un type propre à la bibliothèque si elle en possède un de ce nom ; sinon, il désigne un type intégré.

## Types d’éléments

Le DSL intègre quelques types d’éléments (`person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note`, ainsi que les types UML et BPMN utilisés par ses importateurs). Une bibliothèque en ajoute sous `kinds` et les documents les utilisent via son alias :

```
use "team-shapes@^0.1" as team

model {
  lb = team.edge "Load balancer"
  orders = team.topic "Orders"
  lb -> orders
}
```

Le type donne aux éléments leur apparence. Tout le reste (libellés, `tech`, enfants, ports, relations, vues) fonctionne comme pour n’importe quel élément.

- **Forme.** `shape` utilise un dessin de la bibliothèque (dimensionné et légendé comme une référence `shape`) ; `base` emploie une forme de base du moteur de rendu (`hexagon`, `cylinder`, etc.). La `shape` propre à l’élément prime.
- **Couleurs.** `fill`, `stroke` et `fontColor` remplacent les couleurs de nœud par défaut du thème pour ce type. Le `style` de l’élément prime et les éléments `external` gardent leurs couleurs externes.
- **Stéréotype.** Un élément de ce type ayant des enfants est représenté par un cadre portant le libellé `Name [stereotype]`.
- **Où les trouver.** Le serveur de langage propose les types `alias.` après `=` et signale (à l’emplacement du type) ceux que la bibliothèque ne contient pas ; `diadraw check` et `render` les signalent aussi. La palette web les présente sous _Kinds_ et en ajouter un déclare un élément de ce type.

Les types restent qualifiés dans le modèle (`team.topic`) et sont donc conservés lors de la génération du texte.

`lib build` lit les SVG, les assainit (ni scripts, ni gestionnaires d’événements, ni objets étrangers, ni références externes ; les identifiants reçoivent un préfixe propre à la forme pour éviter tout conflit entre icônes d’une même page), regroupe tout dans une archive (`format: "diadraw-library"`, `formatVersion: 1`) et vérifie que chaque fragment peut être analysé. Un `.ddlib` est cette archive compressée avec gzip.

## Licences

Chaque bibliothèque possède une licence, vérifiée par `lib build`, `lib add` et le générateur des bibliothèques intégrées :

| Statut | Condition | Effet |
| --- | --- | --- |
| `allowed` | tous les termes d’une des options figurent sur la liste autorisée (MIT, MIT-0, Apache-2.0, BSD-2/3-Clause, ISC, 0BSD, CC0-1.0, CC-BY-4.0, Unlicense), sans condition supplémentaire ; ou œuvre originale du projet | distribuable avec DiaDraw |
| `restricted` | licences autorisées assorties de `licenseNotes` | avertissement ; utilisable localement, non distribuée |
| `disallowed` | toutes les options comportent une restriction commerciale, une interdiction de modification ou une licence copyleft (CC-BY-NC/ND, GPL, AGPL, LGPL, SSPL, BUSL) | échec de `lib build` |
| `unknown` | tout autre cas | avertissement |

Seules les bibliothèques `allowed` de `assets/builtin/` sont compilées dans le moteur. Une seule exception : il a été décidé de distribuer les collections de gabarits draw.io avec le statut `restricted` (voir ci-dessous et `docs/decisions.md`).

## Bibliothèques intégrées

Les sources se trouvent dans `assets/builtin/`. La commande `node tools/gen/builtin_libraries.mjs` (après `npm run build -w @diadraw/wasm`) les construit avec le noyau et écrit `src/library/src/builtin_libraries.inc`, que le noyau intègre à la compilation.

- **`c4`** : personne, système logiciel, conteneur, composant, base de données, file d’attente, navigateur web et application mobile, avec une variante externe pour chaque forme qui en possède une dans C4 ; une forme composite « service avec base de données » ; des fragments « contexte du système » et « web, API et base de données ». Correspondances : macros C4-PlantUML (`Person`, `System_Ext`, `ContainerDb`, `ComponentQueue`, etc.) et formes Structurizr (`WebBrowser`, `Cylinder`, `Pipe`, etc.). Dessins originaux.
- **`drawio-*`** : les 204 collections de gabarits draw.io, sous restrictions ; voir [Gabarits draw.io](#gabarits-drawio).
- **`plantuml-aws`, `plantuml-azure`, `plantuml-gcp`, `plantuml-kubernetes`, `plantuml-tupadr3`, `plantuml-office`, `plantuml-cloudinsight`, `plantuml-logos`, `plantuml-material`, `plantuml-elastic`, `plantuml-osa`, `plantuml-cloudogu`, `plantuml-ibm`, `plantuml-archimate`** : les jeux d’icônes de la bibliothèque standard PlantUML, sous forme de bibliothèques ouvertes. Grâce aux correspondances `plantuml-include` et `plantuml-local`, l’importateur PlantUML reconnaît `!include <awslib/…>`, les URL habituelles et les copies intégrées au projet. Une macro d’icône telle que `Lambda(fn, "Handler")` est importée comme `shape aws.Lambda` avec `use "plantuml-aws@^1" as aws`. Les icônes elles-mêmes ne sont pas incluses (leurs licences varient) : ces formes sont représentées par des blocs d’icône.
- **`bpmn`** : formes BPMN 2.0 de création originale. Toutes les positions d’événement (début, début non interruptif, début de sous-processus événementiel, réception intermédiaire, événement de bordure non interruptif, émission intermédiaire, fin) avec tous les marqueurs BPMN permis à chaque position ; tâches de tous types, activités d’appel, sous-processus, sous-processus événementiels, transactions et sous-processus ad hoc, chacun sans marqueur ou avec un marqueur de boucle, d’instances multiples parallèles ou séquentielles, ou de compensation ; toutes les passerelles ; objets de données, collections, entrées, sorties et magasins ; messages, annotations textuelles, groupes, conversations et tâches de chorégraphie. Le libellé des activités reste dans leur forme. S’y ajoutent une forme composite « tâche avec minuteur » et les fragments « boucle d’approbation » et « tâche avec délai maximal ». La correspondance `bpmn` indique la forme de chaque type d’élément BPMN. Générée par `tools/gen/bpmn_library.mjs`.

## Gabarits draw.io

Toutes les collections de gabarits de draw.io sont distribuées comme bibliothèques intégrées nommées `drawio-<set>` (`drawio-aws4`, `drawio-azure`, `drawio-cisco19`, `drawio-flowchart`, etc.) : 204 collections et 9 088 formes provenant de draw.io 32.6.9. Utilisez-les comme toute autre bibliothèque :

```
use "drawio-aws4@^32" as aws4

model {
  fn = component "Handler" shape aws4.lambda_function
}
```

**Ces gabarits sont soumis à des restrictions.** draw.io est sous licence Apache-2.0, mais il est interdit, sans autorisation écrite, d’utiliser ses gabarits dans des produits Atlassian ou sur l’Atlassian Marketplace, de les distribuer pour un tel usage ou de les y incorporer. Les collections des fournisseurs (AWS, Azure, GCP, Cisco, IBM, etc.) sont soumises aux conditions de ces derniers concernant les marques. Voir `assets/NOTICE`. `diadraw lib list` et la palette web signalent ces restrictions pour chaque collection.

Les collections sont stockées au format gzip dans `assets/drawio/` et chargées à la demande : une collection n’est lue (hôtes natifs et hôtes WebAssembly disposant d’un système de fichiers fourni par `setFileSystem(fs, { drawio })`) ou récupérée et ajoutée avec `addLibrary` (application web) que lorsqu’un document l’utilise ou que la palette l’ouvre.

L’importateur draw.io dessine les gabarits au moyen de ces bibliothèques, avec les couleurs de remplissage et de contour de la cellule (y compris les tuiles d’icônes de ressources). La conversion suit mxStencil : chemins, rectangles, rectangles arrondis et ellipses, avec remplissage, contour, couleurs, tirets, transparence et sauvegarde/restauration de l’état. Les textes, images et gabarits imbriqués ne sont pas dessinés ; les formes dessinées par du code dans draw.io ne sont pas des gabarits.

Pour régénérer les collections depuis la version fixée (`DRAWIO` dans le script) :

```bash
npm run build -w @diadraw/wasm
node tools/gen/drawio_stencils.mjs                   # downloads the pinned files
node tools/gen/drawio_stencils.mjs --from ../drawio  # or a local checkout
```

`diadraw lib build --drawio <stencils.min.js|set.xml> -o <dir>` permet toujours de convertir d’autres versions ou des collections individuelles en bibliothèques distinctes.

## Où les bibliothèques interviennent

- **Ligne de commande** : `render` et `check` résolvent les bibliothèques du document ; `render` dessine leurs formes.
- **Serveur de langage** : il propose les noms de bibliothèques dans `use "…"`, les formes après `shape` (toutes s’il y en a au plus 400, sinon les alias d’abord) et après les préfixes `alias.`, et les types de bibliothèque après `=`. Dans `model { … }`, il propose les fragments comme extraits à insérer, avec des champs à compléter pour leurs paramètres. Il signale les bibliothèques manquantes et les formes ou types inconnus.
- **Éditeur web** : le bouton **Shapes** ouvre une palette des formes simples et composites, types et fragments des bibliothèques intégrées, avec recherche par nom et mot-clé. Cliquer sur une forme ajoute un élément avec cette forme (et le `use` de la bibliothèque si le document ne l’a pas) en une seule opération annulable ; cliquer sur un fragment insère son texte à la fin du modèle.
- **Thèmes** : `theme alias.name` choisit un thème de bibliothèque ; `RenderOptions.themes` (`LibrarySet::theme_resolver()`) le résout. La ligne de commande et le serveur de langage signalent un thème absent de la bibliothèque.
- **Moteur de rendu** : `RenderOptions.shapes` résout les références aux dessins, qui apparaissent avec leur libellé en dessous ; `RenderOptions.kinds` résout les types de bibliothèque en forme et couleurs. `create_measure(theme, shapes, kinds)` les dimensionne en conséquence. `LibrarySet::renderer()` fournit les deux résolveurs.

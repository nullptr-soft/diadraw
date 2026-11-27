# Le DSL de DiaDraw

Une même grammaire sert à tous les types de diagrammes : un bloc `model`, à la manière de Structurizr, définit les éléments une seule fois, puis des blocs `view` les sélectionnent, les agencent et leur imposent des contraintes. Les fichiers portent l’extension `.draw` et font foi : les différences sont donc faciles à lire et à examiner dans Git.

La grammaire est spécifiée dans [`diadraw.grammar`](diadraw.grammar), en notation Lezer. Elle sert de référence ; rien ne la compile. L’analyseur du noyau (`src/dsl/src/syntax.cpp`) est un analyseur descendant récursif, écrit à la main et tolérant aux erreurs. Il construit l’arbre décrit par cette grammaire, avec les mêmes noms de nœuds. `parse(source)` (`src/dsl/src/lower.cpp`) traduit l’arbre syntaxique en opérations sur le modèle et renvoie le modèle, des diagnostics avec ligne et colonne et une table de correspondance avec le texte source. `print(model)` produit le texte canonique ; pour tout fichier du corpus, le passage par `print` puis `parse` restitue le modèle inchangé.

Ce document est la référence du langage. Pour savoir à quoi servent les fonctionnalités et quand les utiliser, consultez le [guide d’utilisation](guide/README.md).

## Exemple

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web    // written by a canvas drag
}
```

## Diagrammes à main levée

Les diagrammes dessinés à la main (ou importés de draw.io) conservent leur géométrie dans le texte :

```
model {
  a = process "Billing" style "fill=#dae8fc;stroke=#6c8ebf;rounded=1" prop "owner" "Ann"
  b = decision "Paid?" shape rhombus
  a -> b "check" style "endArrow=open;dashed=1;edgeStyle=orthogonalEdgeStyle"
}

view page1 {
  include *
  layout fixed
  pin a at 40, 40 size 120, 60
  pin b at 240, 30 size 100, 80
  route "a->b" via 200, 70 200, 70 from 1, 0.5 to 0, 0.5
}
```

- `style "k=v;k=v"` définit l’apparence d’un élément ou d’une relation. Clés des éléments : `fill`, `stroke`, `strokeWidth`, `dashed`, `dashPattern`, `rounded`, `opacity` (0–100), `fontColor`, `fontSize`, `fontStyle` (gras 1, italique 2, souligné 4), `align`, `verticalAlign`, `labelPosition`, `verticalLabelPosition`. Clés des relations : `stroke`, `strokeWidth`, `dashed`, `dashPattern` (avec `dashed=1` : `1 3` pour des pointillés, `8 3 1 3` pour un trait mixte, etc.), `double` (deux lignes), `curved`, `rounded` (angles arrondis), `edgeStyle`, `startArrow`, `endArrow`, `startFill`, `endFill`. Les extrémités reprennent les noms de draw.io : `classic`, `block`, `open`, `async`, `openAsync`, `doubleBlock`, `diamond`, `oval`, `box`, `circlePlus`, `halfCircle`, `baseDash`, `dash`, `cross`, `none`, et les extrémités en patte d’oie `ERone`, `ERmandOne`, `ERzeroToOne`, `ERmany`, `ERoneToMany`, `ERzeroToMany`. Avec `startFill=0`/`endFill=0`, la forme est creuse. Un élément doté d’un style est dessiné à la manière de draw.io (fond blanc et contour noir par défaut) ; un conteneur doté d’un style est représenté par sa propre forme et non par un cadre en tirets. Les clés inconnues sont conservées.
- `prop "key" "value"` enregistre une propriété quelconque (instruction répétable).
- `shape` accepte une forme de base : `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible frame module lifeline`.
- `layout fixed` respecte exactement chaque `pin … at X, Y size W, H` (les nœuds non épinglés s’alignent en dessous). `route "relationship id" via X, Y … from EX, EY to NX, NY` fixe les points de passage d’une arête ainsi que ses points de sortie et d’entrée (coordonnées relatives au bloc, de 0 à 1). Sans `from`/`to`, l’arête sort en direction du premier point de passage. Sur le canevas d’une vue fixe, déplacer ou redimensionner un nœud réécrit son `pin … at … size` et déplacer ses points de passage réécrit sa `route` (voir [Dessiner à la main dans les vues fixes](guide/fixed-views.md)).

## Styles et thèmes

`theme` choisit les couleurs du document. Les blocs `style` donnent une apparence commune aux éléments d’un même type, aux objets portant une même étiquette ou à l’ensemble des relations :

```
theme dark

style container {
  fill "#438dd5"
  stroke "#3c7fc0"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true endArrow open }

model { … }

view checkout of shop {
  include *
  theme default        // this view only
}
```

- Thèmes : `default`, `dark` (fond sombre) et `mono` (noir sur blanc, remplissages gris, pour l’impression), les thèmes clairs `ocean`, `forest`, `sunset`, `lavender`, `sand`, `graphite`, `pastel`, `vivid`, `contrast` (noir sur blanc avec un texte plus grand, pour l’accessibilité) et les thèmes sombres `blueprint`, `midnight`, `aurora`, `ember` et `terminal`. Le `theme` d’une vue prime sur celui du document ; un nom inconnu produit un avertissement et utilise `default` pour le rendu. Sans instruction `theme`, toutes les vues utilisent `default`, exactement comme auparavant.
- Sélecteurs : `*` (tous les éléments), un type (`container`), `tag "…"`, un type et une étiquette (`container tag "legacy"`), `relationship` et `relationship tag "…"`. Dans un sélecteur, `relationship` et `tag` sont des mots-clés ; on ne peut donc pas sélectionner un type qui porterait l’un de ces deux noms.
- Chaque ligne d’un bloc a la forme `key value`. Les clés sont celles du style local `style "…"` (voir ci-dessus ; les clés inconnues sont conservées). La valeur est une chaîne, un nombre ou un mot ; `true` et `false` sont enregistrés sous la forme `1` et `0`, comme dans draw.io.
- Par priorité croissante : le thème, les blocs `*`, les blocs de type, les blocs d’étiquette, les blocs combinant type et étiquette (à priorité égale, le dernier l’emporte), puis le `style "…"` local de l’objet.
- Un bloc `style` ne modifie que ce qu’il définit : l’élément conserve les autres couleurs du thème et son libellé en gras. Pour les valeurs omises, un `style "…"` local utilise toujours l’apparence de draw.io (fond blanc, contour noir). Pour un type doté d’enfants, les blocs modifient le cadre (`fill`, `stroke`, `strokeWidth`, `dashed`, `fontColor`).
- `style` et `theme` sont des instructions de premier niveau ; `theme` peut aussi figurer dans une vue. Dans `model { … }`, ces mots restent des noms ordinaires et `style "…"` sur un élément désigne son style local.
- `print` écrit d’abord les déclarations de thèmes, puis le thème choisi, puis les blocs dans l’ordre, avec une paire `key value` par ligne.

### Vos propres thèmes

Le document déclare un thème en lui donnant un corps, puis le sélectionne comme un thème intégré :

```
theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"
  fontFamily "IBM Plex Sans, sans-serif"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
  node external { fill "#444444" }
}

theme brand
```

- `extends` désigne un thème intégré, un autre thème déclaré ou un thème de bibliothèque ; sans cette clause, le thème part de `default`. Chaque thème ne change que ce qu’il définit.
- Clés de thème : `background`, `fontFamily`, `fontSize`, `smallFontSize`, `edge` et `edgeText` (traits et libellés des relations), `boundary` et `boundaryText` (cadres des conteneurs et des groupes), `surface` (activations, pointes de flèches creuses), `dividerFill`, `labelText` (libellés sous les icônes).
- `node <kind> { fill … stroke … text … }` colore un type ; `node default` s’applique à tout type sans entrée propre, `node external` aux éléments portant l’étiquette `external`.
- Les bibliothèques fournissent aussi des thèmes (voir [Bibliothèques](libraries.md#le-manifeste)) : avec `use "acme-brand@^1" as acme`, `theme acme.corporate` en sélectionne un et `theme mine extends acme.corporate { … }` en dérive un autre.
- Les clés inconnues, les cibles `extends` inconnues, les noms répétés ou réservés aux thèmes intégrés et les cycles de `extends` sont signalés. Si un thème ne peut être résolu, le rendu utilise `default`.

## Vues de séquence

Une vue avec `layout sequence` représente les éléments inclus par des lignes de vie, dans l’ordre de `include`, et dessine les étapes du bloc `sequence` de haut en bas :

```
view login {
  include user web api
  layout sequence
  sequence {
    autonumber
    user -> web "Log in"          // ->  call, ->> async, --> reply
    activate web
    alt "valid" {
      web --> user "Welcome"
      note over web api "token issued"
    } else "invalid" {
      web --> user "Error"
      note right of web "logged"
    }
    deactivate web
    divider "Later"
    delay "5 minutes"
    ref over user web "Logout"
    loop "every 30 s" { web ->> api "heartbeat" }
  }
}
```

Les cadres sont `alt`, `opt`, `loop`, `par`, `critical`, `break` et `group`, avec pour chacun des sections facultatives `else "label" { … }`. Les messages appartiennent à la vue et non au modèle : les mêmes participants peuvent figurer dans plusieurs vues de séquence et les relations du modèle restent synthétiques.

## Notes et décorations

Les notes, les textes libres et les cadres appartiennent à une seule vue, pas au modèle. Ils annotent le diagramme sans y ajouter d’éléments : les autres vues du modèle restent épurées.

```
view checkout of shop {
  include *
  layout layered direction right
  text "Checkout, container level" style "fontSize=18;fontStyle=1"
  note consistency "Orders are eventually consistent" below db
  note "Owned by team Web" left of web
  box pci "PCI scope" around api db
  note "Check the cache TTL"
  note "Draft" at 40, -80 size 160, 60
}
```

| Objet | Disposition |
| --- | --- |
| `note [name] "text" [anchor] [style "…"]` | Note adhésive ; un connecteur en tirets la relie à l’élément auquel elle est attachée. |
| `text [name] "text" [anchor] [style "…"]` | Texte libre, titre ou légende. |
| `box [name] ["label"] around a b … [style "…"]` | Cadre arrondi en tirets entourant les blocs des éléments, libellé en haut à gauche. |
| `box [name] ["label"] at X, Y size W, H [style "…"]` | Cadre à coordonnées fixes. |

Un ancrage place l’objet par rapport à un élément de la vue (`below`, `above`, `left of` ou `right of`) ou utilise `at X, Y`, avec une taille facultative `size W, H`. Sans ancrage, les textes s’empilent au-dessus du diagramme et les notes en colonne à sa droite. Les décorations sont placées après le calcul de la disposition et ne déplacent donc jamais les éléments : si l’une d’elles devait recouvrir un nœud, un libellé d’arête ou une autre note, elle s’écarterait davantage de son élément. Le texte d’une note revient à la ligne à 200 px.

- Le nom est facultatif et doit être unique dans la vue. Il permet aux opérations d’édition et aux applications hôtes de désigner l’objet (le SVG le porte dans `data-name` ; chaque décoration a un attribut `data-decoration="<index>"` et la classe `dd-decoration dd-note|dd-text|dd-box`).
- `style` accepte les clés d’élément : `fill`, `stroke`, `strokeWidth`, `dashed` (les cadres sont en tirets sauf si `dashed=0`), `rounded`, `opacity`, `fontColor`, `fontSize`, `fontStyle`, `align`, `verticalAlign`.
- Supprimer un élément supprime les décorations qui lui sont attachées ou qui l’encadrent.
- `note`, `text`, `box` et `around` sont des mots-clés contextuels : des éléments peuvent toujours porter ces noms. Dans `sequence { … }`, `note over …` / `note left of …` gardent leur sens propre aux séquences.

## Règles lexicales

- Les espaces ne sont pas significatifs ; les instructions ne nécessitent pas de séparateur.
- Commentaires : `// to end of line` et `/* block */`. Les deux sont conservés dans l’arbre syntaxique.
- Identifiants : `[A-Za-z_][A-Za-z0-9_]*`. Les références aux éléments imbriqués utilisent des points : `shop.web`.
- Chaînes : entre guillemets doubles, sur une seule ligne, avec `\` comme caractère d’échappement (`\n` est un saut de ligne). Une **chaîne bloc** s’étend sur plusieurs lignes : elle s’ouvre par `"""` et se termine au `"""` suivant, avec les mêmes échappements. La fin de la ligne après le `"""` ouvrant et la ligne du `"""` fermant sont ignorées si elles sont vides, ainsi que l’indentation commune aux autres lignes, ce qui permet d’indenter le texte avec l’instruction qui l’entoure. Les lignes vides à l’intérieur sont conservées :

  ```
  db = container "Orders DB" description """
    Orders and their lines.

    Archived after two years.
  """
  ```

  Les chaînes blocs s’utilisent partout où une chaîne est permise. `format` écrit une `description` sur plusieurs lignes en chaîne bloc, et toute autre chaîne avec `\n`.
- Nombres : `-?digits(.digits)?`.
- Les mots-clés sont contextuels : `use as model view of group port north south east west tech shape tag
description style prop include exclude layout direction spacing right left down up pin below
above at size route via from to align horizontally vertically order sequence activate
deactivate note over divider delay ref autonumber alt opt loop par critical break else theme relationship extends node
text box around` sont des mots-clés là où la grammaire en attend un et des noms ordinaires ailleurs (`layout = container "Layout"` est valable). Si les deux lectures sont possibles, par exemple lorsqu’une liste de sélecteurs est suivie de `layout …`, c’est le mot-clé qui l’emporte.

## Grammaire informelle

```
Document     := (Use | ThemeDef | Theme | StyleBlock | ModelBlock | ViewBlock)*

Use          := "use" String ("as" Name)?          // see docs/libraries.md

Theme        := "theme" QName
ThemeDef     := "theme" Name ("extends" QName)? "{" (Name Value | "node" Name "{" (Name Value)* "}")* "}"
StyleBlock   := "style" StyleSelector "{" (Name (String | Number | Ident))* "}"
StyleSelector:= ("*" | "relationship" | Kind) ("tag" String)? | "tag" String

ModelBlock   := "model" ModelBody
ModelBody    := "{" ModelItem* "}"
ModelItem    := ElementDecl | Relationship | Group | Port

ElementDecl  := Name "=" Kind Label? Property* ModelBody?
Kind         := Ident | Alias "." Ident               // built in, or a library's kind
Relationship := (Name "=")? QName Arrow QName Label? Property*
Arrow        := "->" (solid) | "-->" (dashed) | "<->" (bidirectional)
Group        := "group" Label ModelBody
Port         := "port" Name ("north" | "south" | "east" | "west")?
Property     := "tech" String
              | "shape" QName                      // a base shape, or alias.shape from a library
              | "tag" String
              | "description" String
              | "style" String                     // "key=value;key=value"
              | "prop" String String               // repeatable

ViewBlock    := "view" Name ("of" QName)? "{" ViewItem* "}"
ViewItem     := "include" Selector+
              | "exclude" Selector+
              | "layout" Algorithm ("direction" Dir | "spacing" Number)*
              | "pin" QName (Placement QName | "at" Coord ("size" Coord)?)
              | "route" String ("via" Coord+)? ("from" Coord)? ("to" Coord)?
              | "size" QName Coord                 // width, height in this view
              | "rotate" QName Number              // degrees, clockwise
              | "align" QName QName+ ("horizontally" | "vertically")?
              | "order" QName QName+
              | "sequence" SeqBody
              | "theme" QName
              | ("note" | "text") Name? String Anchor? ("style" String)?
              | "box" Name? String? ("around" QName+ | "at" Coord "size" Coord)
                ("style" String)?
Selector     := "*" | QName
Algorithm    := Name                               // layered (default), tree, grid, radial, force, fixed, sequence
Dir          := "right" | "left" | "down" | "up"
Placement    := "below" | "above" | "left" "of" | "right" "of"
Coord        := Number "," Number
Anchor       := Placement QName | "at" Coord ("size" Coord)?

SeqBody      := "{" SeqItem* "}"
SeqItem      := QName MessageArrow QName Label?
              | ("activate" | "deactivate") QName
              | "note" ("over" QName+ | ("left" | "right") "of" QName) Label
              | "divider" Label?
              | "delay" Label?
              | "ref" "over" QName+ Label
              | "autonumber"
              | FrameKind Label? SeqBody ("else" Label? SeqBody)*
MessageArrow := "->" (call) | "->>" (async) | "-->" (reply)
FrameKind    := "alt" | "opt" | "loop" | "par" | "critical" | "break" | "group"

Label        := String
Kind         := Name
QName        := Name ("." Name)*
```

## Sémantique

| Syntaxe | Modèle |
| --- | --- |
| `web = container "Web app"` dans `shop` | élément `shop.web`, type `container`, nom `Web app`, parent `shop` |
| sans libellé | l’identifiant sert de nom par défaut |
| `tech`, `shape`, `tag`, `description` | champs de même nom sur l’élément ou la relation |
| `style "k=v;…"`, `prop "k" "v"` | style et propriétés de l’élément ou de la relation |
| `a -> b "label"` | relation d’identifiant `a->b` (`a->b#2`, `#3`, etc. en cas de répétition) |
| `pay = a -> b` | relation d’identifiant explicite `pay` |
| `a.http -> b` | si `a` déclare `port http`, ce port est l’extrémité |
| `group "Build" { … }` | groupe ; le champ `group` est défini sur ses éléments |
| `view v of shop` | vue limitée aux enfants de `shop` |
| `include *` | tous les éléments dans la portée ; une vue sans `include` inclut `*` |
| `layout …` | algorithme, direction et espacement de la vue (`layered` par défaut) |
| `pin a below b` | `PinHint` avec placement `Below` |
| `pin a at 10, 20 size 120, 60` | `PinAtHint` avec position et taille facultative (placement libre explicite) |
| `route "a->b" via 1, 2 from … to …` | `RouteHint` : points de passage, de sortie et d’entrée (entre 0 et 1 sur le bloc) |
| `size a 200, 80` | `SizeHint` : taille de l’élément dans cette vue, respectée par toutes les dispositions |
| `rotate a 30` | `RotateHint` : rotation de 30° dans le sens horaire autour du centre (feuilles uniquement) |
| `align a b horizontally` | `AlignHint` (axe horizontal par défaut) |
| `order a b c` | `OrderHint` |
| `sequence { … }` | étapes de la séquence de la vue ; `autonumber` active la numérotation où qu’il se trouve dans le bloc |
| `theme dark` | `model.theme` (dans une vue : `view.theme`) |
| `theme brand extends dark { … }` | `model.themes[]` : `{ name, extends?, …keys, nodes? }` |
| `style container { fill "#438dd5" }` | `model.styles[]` : `{ target: 'elements', kind, tag?, style }` |
| `note n "x" right of a` | décoration `{ kind: 'note', name: 'n', placement: 'right-of' }` |
| `box "x" around a b` | décoration `{ kind: 'box', around: [a, b] }` |

Les types d’éléments sont des identifiants libres. Types intégrés : `person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note`, `class`, `object`, `event`, `task`, `subprocess`, `gateway`, `data`, `annotation`, `group`, `pool`, `lane` et `conversation`. Tout autre type est accepté avec un avertissement ; les bibliothèques en ajoutent d’autres (voir ci-dessous). Un élément `person` est représenté par un bonhomme bâton avec son nom en dessous. Comme ce nom repose sur le canevas, il prend la couleur `labelText` du thème (`fontColor` dans un style l’emporte, et un `style` local conserve le noir de draw.io). La couleur de remplissage s’applique à la tête ; `node person { text }` dans le thème ne colore le nom que lorsqu’une `shape` représente la personne par un bloc rempli.

L’algorithme de disposition est `layered` (par défaut), `tree`, `grid`, `radial`, `force`, `fixed` ou `sequence` (voir le [guide d’utilisation](guide/layouts.md) pour le rôle de chacun). Tout autre nom est conservé, mais c’est le moteur par couches qui s’exécute, avec un avertissement sur la ligne `layout`.

## Types d’éléments des bibliothèques

Une bibliothèque peut ajouter des types d’éléments (champ `kinds` de son manifeste, voir [libraries.md](libraries.md#types-déléments)). Le document les désigne par l’alias de la bibliothèque, comme ses formes :

```
use "acme-platform@^1" as acme

model {
  orders = acme.service "Orders" tech "Go"
  events = acme.topic "Order events"
  jobs = acme.queue "Jobs" {
    worker = component "Worker"
  }
  orders -> events "publishes"
}
```

- Un élément d’un type de bibliothèque est dessiné avec la forme du type (dessin de bibliothèque ou forme de base) et ses couleurs, sauf s’il définit une `shape` ou un `style` propre. S’il a des enfants, il est représenté, comme les types C4, par un cadre dont le libellé indique le stéréotype du type (`Jobs [queue]`).
- Le type est enregistré tel quel, `acme.topic`, et conservé dans le modèle et le texte produit.
- Si aucun `use` ne déclare l’alias du type, l’analyse produit un avertissement. Si la bibliothèque ne contient pas le type, `diadraw check`, `diadraw render` et l’éditeur le signalent comme une forme inconnue. L’élément prend alors la forme d’un bloc aux couleurs par défaut. Dans les deux cas, le document est quand même traduit en modèle.
- Les types de bibliothèque sont toujours qualifiés ; ils n’entrent donc en conflit ni avec les types intégrés ni entre eux.

## Références

À moins qu’elle ne soit nommée, une relation tire son identifiant de ses extrémités. Les identifiants dérivés suivent l’arête : déplacer une extrémité change l’identifiant et supprimer la première de deux arêtes parallèles transforme `a->b#2` en `a->b`. Une `route` qui désigne cet identifiant est donc perdue si l’on modifie le texte à la main (reconnecter depuis le canevas réécrit ces routes avec les nouveaux identifiants). Nommer l’arête, `pay = a -> b`, lui attribue définitivement l’identifiant `pay` : il survit aux changements d’extrémités et d’ordre et `route "pay" …` continue de s’appliquer (voir [Identifiants des relations](guide/relationships.md#identifiants-des-relations) dans le guide pour savoir quand nommer une arête). Le nom est utilisé tel quel, quel que soit le lieu de déclaration (le chemin de l’élément englobant n’est pas ajouté en préfixe). Il doit être unique parmi les relations, mais peut reprendre le nom d’un élément. Nommer une arête ne décale pas les identifiants dérivés des autres. `print` écrit un identifiant qui est un nom sous la forme `name = …` ; les identifiants qui ne sont pas des noms (issus de JSON, par exemple) ne peuvent pas être écrits et redeviennent des identifiants dérivés.

Une référence à points est résolue de la portée la plus interne vers l’extérieur : dans `shop { … }`, `web` signifie `shop.web`, et `shop.web` fonctionne partout. Les sélecteurs et indications d’une vue sont résolus de même, relativement à la portée de la vue : `view checkout of shop { pin db below web }` est donc valable. Si le dernier segment d’une référence est un port de l’élément précédent (`api.http`), elle désigne ce port.

## Diagnostics

L’analyse ne lève jamais d’exception. L’analyseur se remet seul des erreurs de syntaxe : il marque l’instruction non reconnue, puis saute jusqu’à la fin de son bloc ou jusqu’à la prochaine ligne qui y commence une instruction valide. Le reste du fichier est donc toujours traduit en modèle. Le texte sauté est signalé par `Unexpected "…"` et une instruction tronquée par `Syntax error: incomplete statement`.

Les erreurs de modèle proviennent des opérations du noyau et de la résolution des références. Elles sont signalées sur le passage en cause : éléments inconnus, relations inconnues dans `route`, identifiants en double, ports déclarés hors d’un élément ou deux fois, noms `use` invalides ou alias répétés. Avertissements : types d’éléments inconnus, types de bibliothèque dont aucun `use` ne déclare l’alias, `shape` ou `description` sur une relation (ignorés). Les lignes et colonnes sont numérotées à partir de 1 ; les colonnes comptent les unités de code UTF-16, comme dans les éditeurs. `diadraw check file.draw` affiche les diagnostics sous la forme `file:line:column: error|warning: message`.

## Règles de l’aller-retour

1. **Analyse sans perte.** L’arbre conserve les commentaires et les espaces.
2. **Les modifications sémantiques deviennent des retouches du texte.** Un ajout, un renommage ou une reconnexion sur le canevas modifie la plus petite portion de texte nécessaire, au bon endroit. Une reconnexion ne réécrit que l’extrémité déplacée, avec la référence la plus courte qui désigne le nouvel élément depuis la déclaration de la relation, ainsi que chaque `route` dont l’identifiant de relation change (une relation nommée garde son identifiant ; les identifiants dérivés sont recalculés, ce qui peut renuméroter des arêtes parallèles ; voir le [guide d’utilisation](guide/relationships.md#reconnecter-une-ligne)). Modifier les propriétés d’un élément dans l’inspecteur (type, nom, `tech`, `shape`, `tag`, `description`, `style`, `prop`) ne réécrit que les clauses concernées : une valeur modifiée est remplacée sur place, une valeur effacée est supprimée et une nouvelle valeur est ajoutée après la dernière clause de la déclaration. Ainsi, les commentaires, l’ordre des clauses et le corps de l’élément sont préservés (voir le [guide d’utilisation](guide/element-properties.md)). Le libellé, `tech`, la flèche, `tag`, `style` et `prop` d’une relation se modifient de la même manière. Un nouveau nom s’inscrit sous la forme `name = ` devant la ligne (le préfixe est supprimé avec le nom) ; chaque `route` désignant un identifiant qui change est réécrite avec le nouvel identifiant, y compris lorsqu’un identifiant dérivé est renuméroté en conséquence. Les blocs de style et les thèmes modifiés depuis le canevas suivent la même règle, entrée par entrée : une valeur modifiée est réécrite sur place, l’entrée d’une clé retirée est supprimée, une nouvelle clé est ajoutée à la fin de son bloc, et les nouveaux blocs et instructions de thème sont placés près des déclarations du même type (voir le [guide](guide/styles-and-themes.md)).
3. **Les modifications géométriques deviennent des indications, pas des coordonnées.** Un déplacement écrit `pin`, `align` ou `order`.
4. **Les annotations libres restent dans le texte.** Notes, textes et cadres appartiennent à la vue (`note`, `text`, `box` ; voir [Notes et décorations](#notes-et-décorations)).
5. **Des importateurs, pas une couche de compatibilité.** PlantUML, Mermaid, Structurizr et draw.io sont importés dans le modèle ; leur syntaxe n’est pas analysée nativement.

## Questions ouvertes

Les travaux restant à faire sur le DSL et son édition depuis le canevas sont suivis dans le [carnet de tâches](backlog.md#dsl).

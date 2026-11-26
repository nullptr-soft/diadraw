# Dispositions

La ligne `layout` d’une vue choisit l’algorithme qui place ses éléments et trace ses lignes. Les algorithmes automatiques ne demandent aucune coordonnée : choisissez celui qui convient au dessin, donnez-lui une direction et corrigez son placement avec des indications si nécessaire.

```
view checkout of shop {
  include *
  layout tree direction right spacing 40
}
```

`direction` (`down`, `right`, `up`, `left`) et `spacing` (espacement en pixels entre les blocs) sont facultatifs. Sans ligne `layout`, la vue utilise `layered`, de haut en bas.

## Choisir un algorithme

| Algorithme | Placement des éléments | Traits | Usages |
| --- | --- | --- | --- |
| `layered` | par couches dans le sens du flux, avec peu de croisements | à angle droit | architecture, organigrammes de traitement, tout diagramme présentant un flux |
| `tree` | en arbre ordonné, parents centrés sur leurs enfants | comme dans un organigramme | organigrammes, hiérarchies, arbres de décision, menus |
| `grid` | en lignes ou colonnes, dans l’ordre de la liste | à angle droit | catalogues, inventaires, pages « tous nos services » |
| `radial` | sur des anneaux autour de l’élément le plus connecté | droits | réseaux en étoile, système et voisinage |
| `force` | les éléments reliés s’attirent, les autres se repoussent | droits | réseaux et cartes sans direction principale |
| `fixed` | exactement là où les place `pin … at` | selon le tracé défini | diagrammes dessinés à la main ou importés |
| `sequence` | en lignes de vie, avec les étapes de `sequence { … }` | messages | interactions dans le temps (voir la référence du DSL) |

En cas de doute, commencez par `layered` : il accepte tout graphe et respecte le sens des flèches. Changez d’algorithme si le dessin souhaité a une structure particulière.

### layered

C’est la disposition par défaut. Les éléments forment des couches pour que la plupart des lignes suivent la direction de la vue. L’ordre à l’intérieur de chaque couche est choisi pour éviter les croisements. Les traits contournent les blocs à angle droit.

### tree

Chaque élément est suspendu à celui dont la ligne l’atteint en premier : `a -> b` fait de `b` un enfant de `a`. Les éléments sans ligne entrante sont les racines, placées côte à côte dans l’ordre de votre liste ; les enfants suivent l’ordre de leurs lignes. Une ligne qui ne fait pas partie de l’arbre (second parent, remontée vers un ancêtre) est tout de même tracée en contournant les blocs.

Comme dans un organigramme, les traits partent du parent, rejoignent une barre juste après sa rangée, la suivent puis rejoignent l’enfant. Chaque libellé se place sur le dernier segment, près de l’enfant ; l’écart entre les rangées grandit pour l’accueillir. `direction right` couche l’arbre sur le côté ; `up` et `left` inversent son sens.

### grid

Les éléments remplissent une grille dans l’ordre indiqué par la vue (ordre des `include`, ou du modèle pour `include *`), avec à peu près autant de lignes que de colonnes : neuf éléments donnent trois rangées de trois, dix donnent des rangées de quatre (quatre, quatre et deux). Chaque colonne prend la largeur de son bloc le plus large et chaque ligne la hauteur du plus haut ; les blocs sont centrés dans leur cellule.

`direction down` (par défaut) remplit la grille ligne par ligne vers le bas ; `right` la remplit colonne par colonne vers la droite. `up` et `left` commencent de l’autre côté. Les traits passent entre les cellules : la grille convient donc aux vues avec peu de lignes. S’il y en a beaucoup, choisissez `layered`.

### radial

L’élément ayant le plus de lignes se place au centre ; ses voisins directs occupent un premier anneau, leurs voisins l’anneau suivant, et ainsi de suite. Chaque branche reçoit un secteur du cercle proportionnel au nombre d’éléments qui en dépendent. Les anneaux sont espacés de façon à loger les blocs et les libellés. Les parties non reliées de la vue se placent côte à côte. La direction n’intervient pas.

### force

Une simulation : chaque paire d’éléments se repousse, chaque ligne rapproche ses extrémités et le mouvement se stabilise sur un nombre fixe d’itérations. Les éléments étroitement reliés se retrouvent proches les uns des autres, révélant des groupes parfois invisibles dans une vue par couches. Le résultat est toujours identique pour une même vue, mais ajouter ou supprimer un élément peut déplacer sensiblement les autres. Si la disposition doit rester stable, utilisez des indications ou passez à `fixed` une fois le dessin à votre goût. La direction n’intervient pas.

## Conteneurs

Tous les algorithmes automatiques agencent d’abord l’intérieur de chaque conteneur (les conteneurs d’un système, les membres d’un groupe) avec le même algorithme. Puis ils placent le conteneur comme un seul bloc parmi ses voisins. Les lignes entre éléments de conteneurs distincts contournent les blocs intermédiaires.

## Les indications restent valables

`pin a below b`, `align a b horizontally` et `order a b c` fonctionnent avec tous les algorithmes automatiques. Si le placement initial ne respecte pas une indication, une passe de correction déplace ensuite les éléments et recalcule les lignes affectées. `diadraw render` signale toute indication qui reste insatisfaite. Faire glisser un élément sur le canevas écrit `pin … at` (voir [Déplacer des blocs](moving.md)). Avec Option ou Alt enfoncée, le déplacement écrit plutôt ces indications relatives ; la même règle s’applique donc à ces modifications sur le canevas.

## Noms inconnus de DiaDraw

Pour un nom mal orthographié ou non pris en charge, comme `layout circular` ou `layout layred`, DiaDraw dessine la vue avec `layered` et affiche sur la ligne `layout` un avertissement énumérant les noms reconnus. Le nom reste dans votre fichier ; rien n’est perdu si une version ultérieure le prend en charge.

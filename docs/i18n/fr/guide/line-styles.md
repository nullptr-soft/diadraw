# Extrémités et styles de trait

La flèche d’une relation (`->`, `-->`, `<->`) détermine son aspect habituel : un trait continu ou en tirets avec une pointe pleine. Quand un diagramme suit une notation, comme UML, un modèle entité-association ou un flux de données, les extrémités et le trait lui-même ont un sens. Donnez un style à la relation pour les choisir, sur une ligne particulière ou pour toutes celles qui portent une étiquette.

## Les choisir dans l’application

Sélectionnez une ligne. La section **Style** du panneau des propriétés propose une commande pour chaque réglage :

- **Start** et **End** choisissent les extrémités. Sur macOS et Linux, chaque entrée présente un petit aperçu du trait. Sur macOS et sur le web, les entrées sont regroupées en flèches, formes, traits et pattes d’oie (Arrows, Shapes, Lines et Crow’s foot).
- **Filled start** et **Filled end**, une fois décochés, laissent creux les triangles, demi-flèches, losanges, cercles ou carrés.
- **Line style** choisit le tracé de la ligne : continu, en tirets, pointillé, tirets courts ou longs, tiret-point ou tiret-deux points. Sous macOS et Linux, chaque entrée montre un petit aperçu de la ligne.
- **Double line** dessine deux traits fins au lieu d’un ; **Line width** épaissit le trait.
- **Routing** trace la ligne à angle droit, **Curved** la courbe en douceur à travers les points de passage et **Rounded corners** arrondit ses angles.

Chaque commande écrit une clé dans le `style "…"` de la relation sans toucher au reste du texte. Remettre un réglage à sa valeur par défaut supprime sa clé.

## Les écrire dans le texte

Les mêmes clés se placent dans `style "key=value;…"` après la relation :

```
model {
  customer = process "Customer"
  order = process "Order"
  customer -> order "places" style "startArrow=ERmandOne;endArrow=ERzeroToMany"
  order -> customer "notifies" style "dashed=1;dashPattern=1 3;endArrow=open;curved=1"
}
```

Pour appliquer le style à toutes les lignes portant une étiquette, utilisez plutôt un bloc de style (voir [Styles et thèmes](styles-and-themes.md)) :

```
style relationship tag "async" { dashed true dashPattern "1 3" endArrow open }
```

### Extrémités

`startArrow` et `endArrow` acceptent les valeurs ci-dessous. Ce sont les noms de draw.io : un diagramme importé de draw.io conserve donc ses extrémités.

| Valeur | Rendu | Usage courant |
| --- | --- | --- |
| `classic` | flèche | appel ou dépendance (extrémité finale par défaut) |
| `block` | triangle | héritage UML (creux, `endFill=0`) |
| `open` | flèche ouverte | dépendance UML, message |
| `async`, `openAsync` | demi-flèche, demi-flèche ouverte | message asynchrone |
| `doubleBlock` | double triangle | flux rapide ou prioritaire |
| `diamond` | losange | composition UML (plein), agrégation (creux) |
| `oval` | cercle | point de connexion |
| `box` | carré | port ou qualificateur |
| `circlePlus` | cercle avec un signe plus | inclusion UML |
| `halfCircle` | demi-cercle en forme de prise | interface requise |
| `baseDash`, `dash`, `cross` | barre, barre oblique, croix | arrêt, rupture, flux interdit |
| `ERone`, `ERmandOne`, `ERzeroToOne` | patte d’oie : un | un, exactement un, zéro ou un |
| `ERmany`, `ERoneToMany`, `ERzeroToMany` | patte d’oie : plusieurs | plusieurs, un ou plusieurs, zéro ou plusieurs |
| `none` | rien | ligne non orientée |

Sans `startArrow`, la ligne n’a pas de marque initiale, sauf si elle est écrite avec `<->`. Sans `endArrow`, elle se termine par une flèche. `startFill=0` et `endFill=0` rendent les formes creuses.

### Le trait

| Clé | Effet |
| --- | --- |
| `dashed=1` | dessine le trait en tirets (comme `-->`) |
| `dashPattern=…` | motif : `1 3` pointillé, `3 3` tirets courts, `12 6` longs, `8 3 1 3` tiret-point |
| `double=1` | deux traits séparés par la couleur de fond |
| `strokeWidth=…` | épaisseur, 1,25 par défaut |
| `stroke=#…` | couleur |
| `edgeStyle=orthogonalEdgeStyle` | tracé à angle droit |
| `curved=1` | courbes lisses passant par les points de passage |
| `rounded=1` | angles arrondis |

Un `dashPattern` n’est visible que sur une ligne en tirets ; la commande Line style règle les deux propriétés.

## À savoir

- Un trait droit n’ayant pas d’angles, `curved` et `rounded` n’y changent rien. Leur effet apparaît dès que la ligne s’infléchit : lorsqu’une disposition lui fait contourner des blocs, à angle droit ou en suivant les points de passage d’une `route`.
- L’espace entre les traits d’une ligne double prend la couleur de fond de la page. Si elle traverse un groupe ou un conteneur rempli, cet espace laisse donc voir la couleur de la page.
- À l’exportation vers draw.io, les extrémités, `dashPattern`, `curved` et `rounded` accompagnent la ligne. Ce n’est pas le cas de `double`, car draw.io représente les lignes doubles par un autre type d’arête.
- À l’importation depuis draw.io, les extrémités sont reprises mais pas encore les courbes, les angles arrondis ni les motifs de tirets. Les pages importées ont donc le même aspect qu’avant l’ajout de cette fonctionnalité.

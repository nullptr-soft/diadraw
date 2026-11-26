# Redimensionner et faire pivoter

Vous pouvez agrandir, réduire ou faire pivoter un bloc sur le canevas dans n’importe quelle vue, quelle que soit sa disposition. Une ligne de texte enregistre la modification dans cette vue et uniquement dans celle-ci : le même élément peut être grand dans une vue détaillée et petit dans une vue d’ensemble.

```
view checkout of shop {
  include *
  size web 220, 90
  rotate db -15
}
```

## Redimensionner

Sélectionnez un seul bloc : de petites poignées carrées apparaissent à ses coins. Faites-en glisser une : le coin opposé reste fixe et la nouvelle taille s’affiche au-dessus du bloc pendant le déplacement. La taille minimale est de 10 × 10.

Maintenez **Maj** pendant le geste pour conserver les proportions du bloc : largeur et hauteur augmentent ou diminuent ensemble, selon la dimension où le pointeur demande la plus grande variation. Les tailles étant arrondies à l’entier, les proportions sont conservées à une unité près. Lorsque la grille est affichée, un redimensionnement avec Maj ne s’y aligne pas : les pas de la grille préservent rarement un rapport de dimensions.

- Dans une vue **à disposition automatique** (`layered`, `tree`, `grid`, `radial`, `force`), le relâchement écrit `size web 220, 90`. L’algorithme conserve cette taille et agence tout le reste autour ; le bloc peut donc changer de position pour faire de la place.
- Dans une vue **de séquence**, la taille s’applique à l’en-tête du participant.
- Dans une vue **fixe**, la taille est inscrite dans la ligne `pin … at … size` du bloc, comme auparavant (voir [Dessiner à la main dans les vues fixes](fixed-views.md)).
- Les conteneurs (blocs contenant d’autres blocs) sont dimensionnés par la disposition autour de leurs enfants. Ils ne peuvent donc être redimensionnés que dans les vues fixes.

## Ajuster un bloc à son contenu

Pour annuler un redimensionnement et rendre au bloc la taille requise par son nom et sa forme, **double-cliquez sur une poignée d’angle** ou choisissez **Size to Fit Content** dans le menu Edit sur Mac (⌥⌘=) et sous Windows, ou dans le menu principal de l’application Linux (Ctrl+Alt+=). Dans l’application web, c’est le bouton **Fit** de la barre d’outils (⌥⌘= sur Mac, Ctrl+Alt+= ailleurs). La commande agit sur tous les blocs sélectionnés en une fois et s’annule en une étape.

L’ajustement retire la taille du texte au lieu d’en écrire une nouvelle. Dès lors, le bloc suit son contenu : renommez-le et il s’agrandit ou rétrécit en conséquence.

- Dans une vue automatique ou de séquence, la ligne `size web …` disparaît.
- Dans une vue fixe, le bloc garde sa position : `pin web at 40, 60 size 220, 90` devient `pin web at 40, 60`.
- Un conteneur dans une vue fixe perd la totalité de sa ligne `pin`, même si elle n’indiquait pas de taille : il épouse de nouveau ses enfants.
- Un bloc sans taille propre est déjà ajusté ; la commande ne change rien.

## Faire pivoter

Un bloc sélectionné possède aussi une poignée ronde légèrement au-delà de son coin supérieur droit. Faites-la glisser autour du bloc : il tourne selon l’angle parcouru par le pointeur autour de son centre. La rotation s’aligne sur les multiples de 15° à moins de 5° de l’un d’eux et sinon sur les degrés entiers. L’angle apparaît au-dessus du bloc pendant le geste. Au relâchement, l’éditeur écrit `rotate db -15` (en degrés, dans le sens horaire ; une valeur négative tourne dans le sens antihoraire). Remettre le bloc à l’endroit supprime la ligne.

Le libellé tourne avec la forme. Les lignes se rattachent au contour après rotation et les dispositions automatiques réservent la place correspondante : un bloc long tourné à la verticale occupe ainsi la place d’un bloc haut.

Les conteneurs et les participants de séquence ne pivotent pas. Un `rotate` sur l’un d’eux est signalé comme une indication de disposition impossible à respecter.

## Effet sur le texte

- Un `size` ou un `rotate` existant est réécrit **sur place** : le commentaire qui le suit et le nom tel que vous l’avez écrit sont conservés. Une nouvelle indication s’ajoute après les autres lignes de la vue.
- Déplacer un bloc ne change jamais son `size` ni son `rotate`.
- Les tailles sont arrondies à l’entier ; les angles au dixième de degré, entre -180 et 180.

## Disponibilité

Les applications macOS, Linux (GTK), Windows et web disposent des poignées de redimensionnement et de rotation dans chaque vue.

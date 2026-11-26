# Déplacer des blocs

Faites glisser un bloc pour le déplacer. Il reste là où vous le déposez, dans chaque vue, quelle que soit sa disposition. Au relâchement, une ligne est inscrite dans la vue :

```
view main {
  include *
  layout layered direction down
  pin db at 262, 246
}
```

Comme `size` ou `rotate`, cette ligne appartient uniquement à la vue : un même élément peut donc occuper une position différente dans chaque vue. En disposition automatique, l’algorithme continue d’agencer les autres blocs ; tout bloc sur lequel vous déposez le bloc déplacé est repoussé et les lignes qui le touchent sont recalculées. Déplacez de nouveau le même bloc : sa ligne `pin … at` prend la nouvelle position.

## Placer par rapport aux voisins

Maintenez Option (Alt sous Windows et Linux) pendant le déplacement pour placer le bloc par rapport aux autres. Pendant le geste, la ligne qui serait écrite s’affiche au-dessus du bloc :

- S’il est aligné avec un autre bloc à 14 pixels près, le geste écrit `align api db horizontally` (ou `vertically`).
- Sinon, il écrit `pin db right of api` (ou `left of`, `above`, `below`) par rapport au bloc le plus proche.

Ces lignes gardent leur sens si la disposition change ; elles conviennent donc aux diagrammes qui continuent de grandir. Si l’indication est déjà satisfaite, par exemple lorsque deux blocs sont déjà sur la même rangée, le bloc reste là où l’algorithme l’avait placé.

Dans une vue `layout fixed`, le geste écrit toujours `pin … at`, avec ou sans Option. Voir [Dessiner à la main dans les vues fixes](fixed-views.md).

## Sélectionner et déplacer plusieurs blocs

Cliquez sur un bloc pour le sélectionner. Un Maj-clic (ou Commande-clic sur macOS, Ctrl-clic sous Windows et Linux) ajoute un bloc ou une ligne à la sélection, ou l’en retire. Pour sélectionner une région, appuyez sur une zone vide du canevas et faites glisser un rectangle : tout ce qui se trouve entièrement à l’intérieur, blocs comme lignes, est sélectionné (maintenez Maj ou Commande/Ctrl pour étendre la sélection existante). Sur le canevas, Commande-A (Ctrl-A dans l’application Linux et dans l’application web sous Windows et Linux) sélectionne tout le contenu de la vue. Échap ou un clic sur une zone vide efface la sélection. Cliquer sans le déplacer sur un bloc parmi plusieurs blocs sélectionnés le sélectionne seul.

Faites glisser un des blocs sélectionnés : tous se déplacent ensemble en conservant leurs positions relatives. Le contour de chacun apparaît à sa future position. Au relâchement, une ligne `pin … at` est écrite pour chaque bloc en une seule modification. Une seule annulation les remet donc tous en place :

```
view main {
  include *
  layout layered direction down
  pin api at 262, 120
  pin db at 262, 246
}
```

Un groupe se pose toujours là où vous le déposez : Option (Alt) ne le place pas par rapport à ses voisins. Dans une vue `layout fixed`, une ligne dont les deux extrémités bougent conserve sa forme, car ses points de passage se déplacent avec elle. Suppr (ou Retour arrière) supprime toute la sélection.

Appuyer à l’intérieur d’un conteneur sélectionne ce dernier au lieu de commencer un rectangle de sélection. Commencez donc le rectangle à l’extérieur, ou sélectionnez les blocs par Maj-clic.

## Rendre le placement à l’algorithme

Supprimez la ligne `pin`, `align` ou `order` du bloc dans la vue : l’algorithme le place de nouveau.

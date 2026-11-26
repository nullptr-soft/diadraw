# Dessiner à la main dans les vues fixes

La plupart des vues sont agencées pour vous : vous indiquez leur contenu et, si vous le souhaitez, ajustez la disposition avec des indications comme `pin db below web`. Parfois, vous voulez l’inverse : que chaque bloc reste exactement là où vous le placez, à la taille choisie, et que les lignes s’infléchissent là où vous le décidez. C’est le rôle de `layout fixed`. Les diagrammes importés de draw.io l’utilisent, et vous pouvez aussi l’utiliser dans toute vue que vous écrivez :

```
view checkout {
  include *
  layout fixed
  pin web at 40, 40 size 160, 70
  pin db at 320, 200
  route "writes" via 380, 90
}
```

`pin … at X, Y` place le coin supérieur gauche d’un bloc à un point précis. `size W, H` lui donne une largeur et une hauteur (sinon le bloc garde la taille nécessaire à son libellé). `route "id" via X, Y …` énumère les points où une ligne change de direction, en désignant la ligne par [l’identifiant de la relation](relationships.md#identifiants-des-relations). Les éléments sans `pin` s’alignent en rangée sous les éléments épinglés.

Vous pouvez tout écrire dans le texte, ou simplement dessiner sur le canevas.

## Déplacer

Faites glisser un bloc pour le déplacer. Dans une vue fixe, avec ou sans Option, il se place exactement où vous le déposez ; sa ligne `pin … at` indique sa nouvelle position. Sa taille éventuelle est conservée.

## Redimensionner

Sélectionnez un seul bloc : quatre petites poignées carrées apparaissent aux coins. Faites-en glisser une pour redimensionner le bloc. Le coin opposé reste fixe et la nouvelle taille s’affiche au-dessus du bloc pendant le geste. La taille minimale est de 10 × 10.

Au relâchement, le `pin` du bloc reçoit la nouvelle position et la nouvelle taille :

```
  pin web at 40, 40   // storefront
```

devient

```
  pin web at 40, 40 size 180, 82   // storefront
```

## Infléchir les lignes

Sélectionnez une seule ligne pour afficher ses poignées :

- une poignée ronde sur chaque **point de passage** : faites-la glisser pour déplacer ce point ;
- une poignée plus petite et plus claire **au milieu de chaque segment** : faites-la glisser pour ajouter un point de passage ;
- déplacez un point sur la droite reliant ses voisins pour le **supprimer**.

Au relâchement, la `route` de la ligne énumère tous ses points de passage dans l’ordre :

```
  route "writes" via 380, 90 380, 150
```

Tout `from` ou `to` déjà présent sur la route (les points où la ligne quitte un bloc et entre dans l’autre) est conservé. Une route sans point de passage, ni `from`, ni `to` est supprimée, et la ligne redevient droite.

Le tracé d’une relation sans nom utilise son identifiant dérivé, par exemple `route "web->db"`. Cet identifiant change si vous modifiez les extrémités de la relation ou supprimez une relation parallèle. La route cesse alors de s’appliquer : [nommez la relation](relationships.md#quand-nommer-une-relation) si vous tracez sa ligne à la main : `writes = web -> db`.

## Effet sur le texte

Comme toute modification sur le canevas, le changement se traduit par une petite retouche du fichier et une seule étape dans l’historique commun avec l’éditeur de texte (⌘Z l’annule de chaque côté) :

- un `pin` ou une `route` existants sont réécrits **sur place** ; un commentaire à leur suite reste intact et, dans une vue d’un système, le nom reste tel que vous l’avez écrit (`pin web` et non `pin shop.web`) ;
- un nouveau `pin` ou une nouvelle `route` s’ajoutent après les autres lignes de la vue ;
- les coordonnées sont arrondies à l’entier.

## À savoir

- **Points de passage uniquement dans les vues fixes.** Les vues automatiques n’affichent pas de poignées de tracé : les lignes sont calculées par l’algorithme. Les blocs peuvent être redimensionnés et pivotés dans toutes les vues ; voir [Redimensionner et faire pivoter](resize-and-rotate.md). Ajoutez `layout fixed` pour dessiner une vue à la main, ou continuez à guider la disposition avec des indications.
- **Un objet à la fois.** Les poignées apparaissent quand un seul bloc ou une seule ligne est sélectionné.
- **Les lignes orthogonales** (`edgeStyle=orthogonalEdgeStyle`) conservent leurs angles droits : après le déplacement d’un point de passage, les angles nécessaires sont ajoutés au tracé.
- **Disponibilité :** applications web, macOS, Linux (GTK) et Windows.

# Styles et thèmes sur le canevas

L’apparence d’un diagramme provient de trois sources. Le **thème** définit les couleurs et les polices utilisées pour l’ensemble du dessin. Les **blocs de style** donnent une apparence commune à tous les éléments d’un type, aux objets portant une étiquette ou à toutes les relations. Le **style local** d’un élément (champ Style de l’[inspecteur](element-properties.md)) ne change que cet élément. Dans le texte, ces trois sources se présentent ainsi :

```
theme brand extends dark {
  background "#101418"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
}

theme brand

style container {
  fill "#438dd5"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true }
```

La [référence du DSL](../dsl.md#styles-et-thèmes) recense tous les sélecteurs et toutes les clés. Ce chapitre explique comment les modifier depuis le canevas : vous pouvez tout régler sans quitter le diagramme et chaque modification se retrouve dans votre texte.

## Quel réglage utiliser ?

- Utilisez un **bloc de style** si l’apparence dépend d’un type ou d’une étiquette : « toutes les bases de données sont orange », « tout ce qui porte l’étiquette `external` est gris avec un contour en tirets ». Tout nouvel élément de ce type ou portant cette étiquette hérite du style sans intervention.
- Utilisez le champ **Style** de l’élément pour celui qui doit se distinguer des autres.
- Utilisez un **thème** pour tout le diagramme : arrière-plan, couleur des traits, police et couleurs par défaut de chaque type. Choisissez un thème intégré (`default`, `dark`, `mono` pour l’impression) ou créez le vôtre.

Quand plusieurs réglages s’appliquent, le plus précis l’emporte : d’abord le thème, puis les blocs `*`, les blocs de type, ceux d’étiquette, ceux combinant type et étiquette, et enfin le style propre à l’élément.

## Styles partagés d’un élément

Sélectionnez un seul élément : le champ **Shared styles** apparaît en bas de l’inspecteur. Il énumère les blocs de style applicables à cet élément, un par ligne, dans leur ordre d’écriture :

```
container: fill=#438dd5
tag "external": fill=#999999;dashed=1
```

Chaque ligne contient un sélecteur, deux-points, puis les clés au format du champ Style (paires `key=value` séparées par `;`). Modifier ce champ modifie les blocs :

| Votre action | Effet sur le texte |
| --- | --- |
| modifier une valeur | réécrit la ligne correspondante du bloc |
| ajouter une clé à une ligne | ajoute la clé à la fin du bloc |
| retirer une clé | supprime sa ligne dans le bloc |
| ajouter une ligne comme `database: fill=red` | ajoute un bloc `style` après les blocs affichés |
| supprimer une ligne | supprime son bloc |
| modifier le sélecteur d’une ligne | change le sélecteur du bloc sans toucher à son corps |

Modifier ce champ change l’apparence de **tous** les éléments auxquels le bloc s’applique, pas seulement de la sélection : c’est le but. Pour ne changer que cet élément, utilisez plutôt son champ Style.

Le champ accepte uniquement les sélecteurs qui s’appliquent à l’élément sélectionné : son type, ses étiquettes, son type et une de ses étiquettes, ou `*`. Une ligne visant un autre type ou les relations est refusée avec un message dans la ligne d’état. Pour les modifier, désélectionnez tout (voir ci-dessous).

## Thème et styles du document

Cliquez sur une zone vide du canevas. Tant que rien n’est sélectionné, le panneau des propriétés à côté du canevas présente le document :

- **Theme** : l’instruction `theme` du document. Saisissez `default`, `dark`, `mono`, le nom d’un thème déclaré ou un thème de bibliothèque (`acme.corporate`). Vider le champ supprime l’instruction.
- **This view’s theme** : le `theme` de la vue active, qui remplace celui du document uniquement pour cette vue. Un champ vide signifie que la vue utilise le thème du document.
- **Style blocks** : tous les blocs de style du document, y compris ceux des relations comme `relationship tag "async": dashed=1`, un par ligne comme ci-dessus.

### Créer votre propre thème

Saisissez dans Theme (ou This view’s theme) un nom de thème encore inutilisé, par exemple `night`. DiaDraw le déclare pour vous à partir du thème qu’il remplace, puis le sélectionne :

```
theme night extends dark {}

theme night
```

Quand la vue est dessinée avec un thème déclaré dans le document, le panneau affiche sa section, **Theme night**, avec trois champs supplémentaires :

- **Extends** : le thème de départ. Un champ vide signifie `default`.
- **Theme keys** : une paire `key=value` par ligne : `background`, `fontFamily`, `fontSize`, `edge`, `edgeText`, `boundary` et les autres clés énumérées dans la [référence du DSL](../dsl.md#vos-propres-thèmes).
- **Node colours** : un type par ligne avec ses clés `fill`, `stroke` et `text` : `person: fill=#08427b;stroke=#052e56;text=#ffffff`. `default` couvre tout type sans entrée propre ; `external`, tout élément portant l’étiquette `external`. Le `text` d’une personne ne colore son nom que lorsqu’une `shape` la représente comme un bloc rempli. Pour le bonhomme bâton, le nom repose sur le canevas et prend la couleur `labelText`.

Les thèmes intégrés ou de bibliothèque ne sont pas modifiables sur place : créez votre propre thème qui en dérive.

### Dans les applications macOS, web et Linux

Ces applications présentent les champs sous forme de commandes qui écrivent le même texte :

- **Theme**, **This view’s theme** et **Extends** sont des listes modifiables contenant les thèmes intégrés, ceux de vos bibliothèques et ceux déclarés dans le document. Vous pouvez aussi saisir un nouveau nom.
- **Style blocks** est une liste avec une ligne par bloc. Choisissez ou saisissez son sélecteur (`*`, `relationship`, un type, `tag "external"`, `relationship tag "async"`). Dépliez la ligne avec le triangle pour modifier ses clés dans le même éditeur de style que celui de l’inspecteur d’élément ; − supprime la ligne. **Add style block** ajoute une ligne.
- **Theme keys** propose un bouton de couleur pour chaque clé de couleur (`background`, `edge`, `boundary`, etc.) et un champ de texte pour `fontFamily` et les tailles de police. Les boutons proposent la même palette prédéfinie que l’inspecteur, avec Custom… pour toute autre couleur. Le × à côté d’un bouton retire la clé : le thème reprend alors la valeur du thème dont il dérive. Les clés non affichées dans le panneau sont conservées.
- **Node colours** présente une ligne par type avec les boutons de couleur Fill, Line et Text. Choisissez un type et cliquez sur Add pour ajouter une ligne ; elle est écrite dès qu’une couleur lui est attribuée.

Dans l’application web, les champs de thème suggèrent les thèmes intégrés et ceux déclarés dans le document ; le triangle d’un bloc de style le replie en un résumé de ses clés sur une ligne.

Dans l’application Linux (GTK), Theme, This view’s theme et Extends sont des champs avec un ▾ qui liste les thèmes. Style blocks présente une liste de blocs dont le ▸ ouvre l’éditeur de style. Theme keys et Node colours proposent des boutons de couleur.

## Appliquer une modification

Les champs fonctionnent comme dans l’inspecteur d’élément : après une modification, appuyez sur Entrée ou quittez le champ pour la valider ; Échap rétablit sa valeur. Les champs multilignes (Shared styles, Style blocks, Theme keys, Node colours) se valident quand vous les quittez, ou avec ⌘Entrée / Ctrl+Entrée dans l’application web. Sur macOS, Entrée valide Shared styles et ⌥Entrée commence une nouvelle ligne. Dans les deux applications, les commandes de thème et de styles appliquent immédiatement un choix ou une couleur sélectionnée.

Chaque modification forme une seule étape d’annulation partagée avec l’éditeur de texte : ⌘Z l’annule de chaque côté.

## Effet sur le texte

Le texte fait toujours foi et le panneau le modifie le moins possible :

- une valeur modifiée est réécrite sur place, ce qui préserve les commentaires et l’alignement de la ligne ;
- la ligne d’une clé retirée est supprimée ; un bloc écrit sur une seule ligne, comme `style tag "external" { fill "#999999" dashed true }`, reste sur une ligne ;
- les nouvelles clés vont à la fin de leur bloc, les nouveaux blocs après les autres blocs de style (ou au-dessus du bloc `model` s’il n’y en a pas), et une nouvelle instruction de sélection du thème après les déclarations de thèmes ;
- l’ordre dans lequel vous avez écrit les entrées est préservé, quel que soit l’ordre de saisie dans le champ.

Par exemple, changer la couleur de remplissage des conteneurs dans le panneau transforme :

```
style container {
  fill "#438dd5"   // brand blue
  stroke "#3c7fc0"
}
```

en :

```
style container {
  fill "#1168bd"   // brand blue
  stroke "#3c7fc0"
}
```

## À savoir

- **`true` et `false`** sont enregistrés sous la forme `1` et `0`, comme dans draw.io : `dashed true` apparaît donc comme `dashed=1` dans les champs. Une entrée non modifiée conserve le mot que vous avez écrit.
- Les **valeurs** ne peuvent pas contenir `;` dans les champs de style, comme dans les styles locaux. En revanche, les clés de thème étant écrites à raison d’une par ligne, leurs valeurs peuvent en contenir.
- **Plusieurs blocs avec le même sélecteur** figurent sur des lignes distinctes ; s’ils définissent la même clé, le dernier l’emporte.
- **Un thème comportant deux blocs `node` pour un même type** est réécrit lorsque vous modifiez les couleurs de ce type depuis le panneau, car le panneau ne peut pas savoir quel bloc vous visiez.
- **Renommer ou supprimer une déclaration de thème** se fait dans le texte ; le panneau modifie le thème utilisé pour dessiner la vue.
- **Disponibilité :** applications web, macOS et Linux (GTK). L’application Windows ne possède pas encore d’inspecteur.

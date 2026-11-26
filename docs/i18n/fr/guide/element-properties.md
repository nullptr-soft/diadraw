# Modifier les propriétés sur le canevas

Un élément est plus qu’un bloc portant un nom. Son type détermine son apparence ; `tech` et `description` apportent des précisions, `shape` choisit un contour, les étiquettes le regroupent avec d’autres pour appliquer un style, `style` modifie son aspect et `prop` conserve toute autre information souhaitée :

```
model {
  db = container "Orders DB" tech "Postgres" shape cylinder tag "pii" prop "owner" "Ann"
}
```

Vous pouvez tout écrire dans le texte, mais ce n’est pas obligatoire. Sélectionnez un seul élément sur le canevas : ses propriétés apparaissent dans un panneau à côté du canevas, l’inspecteur. Sélectionnez plutôt une seule relation (cliquez sur sa ligne) pour afficher ses propriétés ; voir [Relations](#relations).

## L’inspecteur

Le panneau des propriétés reste à côté du canevas quelle que soit la sélection : sélectionner un objet ne déplace donc jamais le diagramme. L’inspecteur apparaît lorsqu’un seul élément ou une seule relation est sélectionné. Sans sélection, le panneau présente le [thème et les styles du document](styles-and-themes.md#thème-et-styles-du-document) ; si plusieurs objets sont sélectionnés, il en indique le nombre. Pour un élément, le titre de l’inspecteur est son identifiant.

| Champ | Texte écrit | Saisie |
| --- | --- | --- |
| Kind (type) | le type après `=` | `container`, `database`, `person`, etc., ou `alias.kind` d’une bibliothèque |
| Name (nom) | le libellé entre guillemets | texte libre ; si le champ est vide, l’identifiant de l’élément sert de nom |
| Tech | `tech "…"` | texte libre |
| Shape (forme) | `shape …` | forme de base comme `cylinder` ou `hexagon`, ou forme d’une bibliothèque |
| Tags (étiquettes) | un `tag "…"` par étiquette | séparées par des virgules : `external, legacy` |
| Description | `description "…"` | texte libre |
| Style | `style "…"` | paires `key=value` séparées par `;` : `fill=#dae8fc;stroke=#6c8ebf` |
| Properties (propriétés) | un `prop "…" "…"` par clé | une paire `key=value` par ligne |
| Shared styles (styles partagés) | les blocs `style` applicables | un `selector: key=value;…` par ligne (voir [Styles et thèmes](styles-and-themes.md)) |

Dans l’application web, Kind et Shape suggèrent les noms intégrés à mesure que vous tapez. Style accepte les mêmes clés qu’un `style "…"` local dans le texte ; la [référence du DSL](../dsl.md#diagrammes-à-main-levée) les énumère.

### Commandes dans les applications macOS, web et Linux

Les applications proposent des commandes plutôt que du texte libre pour la plupart des champs ; chacune écrit le texte décrit dans le tableau ci-dessus.

- **Kind, Shape et Tech** sont des listes qui acceptent aussi une valeur saisie au clavier. Kind propose les types intégrés et ceux des bibliothèques du document. Shape propose les formes de base et celles des bibliothèques (vide, il utilise la forme du type). Tech propose les technologies déjà utilisées dans le document.
- **Tags** représente chaque étiquette par un jeton : tapez une étiquette puis une virgule ; le champ suggère celles que le document utilise déjà.
- **Description** grandit avec son texte ; Entrée commence une nouvelle ligne et quitter le champ valide la saisie.
- **Style** est un éditeur visuel. Pour les éléments, il propose des boutons de couleur pour le fond, le trait et le texte, ainsi que des réglages pour l’épaisseur du trait, les tirets, les angles arrondis, l’opacité, la taille des caractères, le gras, l’italique, le soulignement, l’alignement et la position du libellé. Pour les relations, il règle la couleur du trait, son épaisseur, ses tirets et leur motif (pointillé, tiret-point, etc.), le trait double, le tracé à angle droit, les courbes et les angles arrondis, les extrémités de départ et d’arrivée (flèches, losanges, cercles, barres, pattes d’oie, chacune avec aperçu), leur remplissage, ainsi que la couleur et la taille du libellé. Chaque commande écrit une seule clé sans changer les autres. Rétablir sa valeur par défaut (ou cliquer sur ⓧ à côté d’une couleur) supprime sa clé. Les clés sans commande sont conservées dans **Other**, sous la forme `key=value;…` ; **Clear style** supprime tout le style.
- Les **boutons de couleur** ouvrent une palette prédéfinie : des gris du blanc au noir, puis des nuances claires, vives et foncées de dix teintes. Choisir les mêmes échantillons permet de garder des couleurs cohérentes entre les éléments et les diagrammes. **Custom…** ouvre le sélecteur de couleurs du système pour toute autre couleur ; **Default** supprime la clé. Les applications macOS, Linux et web proposent les mêmes échantillons.
- **Properties** est un tableau de clés et de valeurs : modifiez une cellule, ajoutez une ligne avec + ou sélectionnez-en une et supprimez-la avec −.

L’application web diffère sur quelques points : les étiquettes sont des pastilles avec un × pour les retirer (Entrée ou une virgule ajoute l’étiquette saisie ; Retour arrière dans le champ vide retire la dernière). Le triangle à côté du titre de Style replie l’éditeur pour montrer le texte du style. Chaque ligne de propriété possède son propre − et le champ Arrow d’une relation propose trois boutons montrant le trait. Le champ Name d’une relation affiche en grisé l’identifiant dérivé de ses extrémités tant que vous n’avez pas saisi de nom.

L’application Linux (GTK) propose les mêmes commandes, avec quelques différences de présentation : Kind, Shape et Tech sont des champs munis d’un bouton ▾ qui liste les choix, regroupés par bibliothèque. Les étiquettes sont des pastilles avec un × pour les retirer ; le champ en dessous ajoute une étiquette sur Entrée ou virgule (son ▾ liste les étiquettes du document). Chaque ligne de propriété possède son propre −. Dans une palette de couleurs, **Custom…** ouvre le sélecteur de couleurs de GNOME.

### Appliquer une modification

Après avoir modifié un champ, appuyez sur Entrée ou passez à un autre champ pour valider. Échap rétablit le champ dans son état initial. Effacer Tech, Shape, Description ou Style supprime la propriété correspondante ; effacer Tags ou Properties les supprime toutes.

Dans le champ Description, Entrée insère une nouvelle ligne : quittez le champ pour valider (ou appuyez sur ⌘Entrée / Ctrl+Entrée dans l’application web). Dans le tableau des propriétés, Entrée valide la cellule.

Le canevas affiche la description sous le nom et la technologie, en plus petits caractères, et agrandit la boîte pour la contenir (et l’élargit, jusqu’à 240 px, 320 px pour un losange ou une ellipse, pour les longues lignes). Les lignes plus longues sont renvoyées à la ligne ; les sauts de ligne et les lignes vides sont conservés. Les cercles, les libellés placés hors de la forme (acteurs, `labelPosition`, `verticalLabelPosition`), les dessins au libellé intérieur ou avec leur propre `style` et les éléments dessinés comme cadre autour de leurs enfants n’affichent que le nom. Une description sur plusieurs lignes est écrite dans le texte en chaîne bloc, ligne par ligne, indentée sous l’élément :

```
model {
  db = container "Orders DB" tech "Postgres" description """
    Orders and their lines.

    Archived after two years.
  """
}
```

Vous pouvez aussi l’écrire ainsi dans le texte : `"""` l’ouvre et la ferme, et l’indentation commune aux lignes ne fait pas partie de la description (voir la [référence du DSL](../dsl.md#règles-lexicales)).

Chaque modification forme une seule étape d’annulation partagée avec l’éditeur de texte : ⌘Z l’annule de chaque côté.

## Relations

Le titre de l’inspecteur d’une relation est sa ligne telle que vous l’écririez, par exemple `web -> db` ou `checkout = web -> db`. Ses champs sont les suivants :

| Champ | Texte écrit | Saisie |
| --- | --- | --- |
| Label (libellé) | le libellé entre guillemets | texte libre ; sans valeur, la ligne n’a pas de libellé |
| Tech | `tech "…"` | texte libre |
| Arrow (flèche) | `->`, `-->` ou `<->` | choisir : trait continu, en tirets ou fléché aux deux extrémités |
| Name (nom) | `name =` avant la ligne | identifiant tel que `checkout` ; si le champ est vide, l’identifiant est dérivé |
| Tags (étiquettes) | un `tag "…"` par étiquette | séparées par des virgules : `async, legacy` |
| Style | `style "…"` | paires `key=value` séparées par `;` : `stroke=#c00;dashed=1` |
| Properties (propriétés) | un `prop "…" "…"` par clé | une paire `key=value` par ligne |

Le choix d’une flèche s’applique immédiatement ; les autres champs se valident comme pour les éléments. Les extrémités d’une ligne ne figurent pas dans l’inspecteur : pour la reconnecter, modifiez le texte.

### Nommer une relation depuis l’inspecteur

Le champ Name contient l’identifiant de la relation si vous lui en avez donné un (voir [Relations nommées](relationships.md#relations-nommées)) et reste vide si l’identifiant est dérivé des extrémités. Saisir un nom écrit `name = ` devant la ligne ; effacer le nom retire ce préfixe. La ligne reste sélectionnée sous son nouvel identifiant, et chaque `route "…"` qui désignait l’ancien est mise à jour pour désigner le nouveau. Les points de passage manuels sont donc conservés. Donner à la ligne de cet exemple :

```
model {
  …
  web -> db "writes"
}

view main {
  route "web->db" via 200, 80
}
```

le nom `calls` modifie deux endroits :

```
  calls = web -> db "writes"
  …
  route "calls" via 200, 80
```

Nommer une ligne ou lui retirer son nom peut changer les identifiants dérivés des autres lignes entre les deux mêmes éléments, car `#2` et `#3` dépendent du nombre de lignes sans nom qui les précèdent. Leurs routes sont également mises à jour : aucun tracé manuel n’est perdu. Un nom qui n’est pas un identifiant ou qu’une autre relation porte déjà est refusé ; la ligne d’état indique pourquoi.

## Effet sur le texte

Le texte fait toujours foi : chaque modification dans l’inspecteur modifie le fichier, et l’inspecteur écrit le moins possible :

- une valeur modifiée est remplacée sur place : `shape rect tech "React"` conserve cet ordre si vous changez la technologie, et choisir une nouvelle flèche remplace uniquement la flèche ;
- une propriété effacée est supprimée avec l’espace qui la précède ;
- une nouvelle propriété est ajoutée après la dernière de la ligne, avant tout corps `{ … }` ou commentaire (le nouveau libellé d’une relation est placé juste après sa cible, à l’endroit prévu pour les libellés) ;
- les étiquettes conservées restent en place et les nouvelles s’ajoutent à la fin.

Les commentaires, l’alignement, le corps de l’élément et tout le reste du fichier restent intacts. Si vous changez une technologie dans l’inspecteur :

```
    web = container "Web app" shape rect tech "React" tag "edge"  // storefront
```

devient

```
    web = container "Web app" shape rect tech "Vue" tag "edge"  // storefront
```

## À savoir

- **Un objet à la fois.** Sélectionnez un seul élément ou une seule relation pour le modifier ; avec plusieurs objets, le panneau indique seulement leur nombre. Un double-clic sur une ligne permet toujours de modifier son libellé sur place.
- **Les identifiants d’éléments ne changent pas.** L’inspecteur modifie les caractéristiques d’un élément, pas sa place dans le modèle : pour renommer son identifiant, le déplacer vers un autre parent ou ajouter des ports, modifiez le texte. L’identifiant d’une relation fait exception grâce au champ Name.
- **Les relations ignorent `shape` et `description`.** L’inspecteur ne les propose pas et leur présence dans le texte produit un avertissement.
- **Types et formes sont des noms.** Un type ou une forme contenant des espaces ou d’autres signes de ponctuation est refusé. La ligne d’état indique pourquoi et rien n’est écrit.
- **Les clés de style** ne peuvent contenir ni `=` ni `;`, et les valeurs ne peuvent contenir `;`, comme dans le texte.
- **Une valeur qui serait refusée est entourée de rouge** pendant la saisie, et l’infobulle du champ
  dit pourquoi : un Id qui n’est pas un nom ou qui est déjà pris, un type ou une forme qui n’est pas
  un nom, une clé de style contenant `=` ou `;`, une ligne de styles partagés illisible ou qui ne
  s’applique pas à l’élément, et un nom de relation qui n’est pas un nom ou qui est déjà pris. Le
  contour disparaît dès que la valeur est valide.
- **L’apparence de tout un type** se définit dans un bloc `style` plutôt que dans le champ Style de chaque élément. Le champ Shared styles de l’inspecteur modifie les blocs applicables à l’élément (voir [Styles et thèmes sur le canevas](styles-and-themes.md)).
- **Disponibilité :** applications web, macOS et Linux (GTK). L’application Windows ne possède pas encore d’inspecteur.

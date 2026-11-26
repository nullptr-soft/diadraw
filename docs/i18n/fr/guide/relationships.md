# Relations

Une relation est une ligne entre deux éléments : appel, flux de données ou dépendance. Comme les éléments, les relations sont déclarées une seule fois dans le bloc `model` ; chaque vue montrant leurs deux extrémités les dessine.

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app"
    db  = container "Orders DB"
    web -> db "writes" tech "SQL"
  }
  customer -> shop.web "places order"
}
```

## Flèches

| Flèche | Rendu | Usage courant |
| --- | --- | --- |
| `->` | trait continu, une pointe | appel, requête, dépendance |
| `-->` | trait en tirets | flux asynchrone ou indirect |
| `<->` | pointes aux deux extrémités | échange bidirectionnel |

Après la cible viennent un libellé facultatif et, comme pour les éléments, les clauses `tech`, `tag`, `style` et `prop` que vous souhaitez. Le diagramme affiche le libellé et la technologie sur la ligne.

## Extrémités et styles de trait

La flèche donne à la ligne son apparence habituelle. Pour obtenir une flèche ouverte, un losange, des extrémités en patte d’oie, un trait pointillé, double ou courbe, donnez un style à la relation. Voir [Extrémités et styles de trait](line-styles.md).

## Éléments aux extrémités et ports

Chaque extrémité est un élément, désigné comme vous le feriez depuis le lieu de déclaration de la relation : `web` dans `shop { … }`, `shop.web` partout. Si l’élément déclare un port (`api = container { port http north }`), `api.http` rattache la ligne à ce port.

## Reconnecter une ligne

Pour rattacher une ligne à un autre élément, cliquez sur la ligne pour la sélectionner. Un petit cercle apparaît à chaque extrémité. Faites-en glisser un sur un autre élément et relâchez : pendant le déplacement, l’élément sous le pointeur est mis en évidence. Si vous relâchez dans le vide ou sur un élément déjà touché par la ligne, rien ne change.

Le texte ne change qu’à l’endroit où cette extrémité est écrite :

```
web -> legacy "REST" tech "HTTPS"  // being migrated
```

devient, après déplacement de la pointe de Legacy API vers API :

```
web -> api "REST" tech "HTTPS"  // being migrated
```

Le libellé, `tech`, les étiquettes, les commentaires et la flèche restent intacts. La nouvelle extrémité est écrite sous la forme la plus courte qui désigne l’élément depuis la déclaration de la ligne : `api` dans `shop { … }`, `shop.api` à l’extérieur. Dans les rares cas où aucun nom ne convient (dans `shop`, un `db` de premier niveau est masqué par `shop.db`), le fichier est plutôt reformaté et la ligne d’état le signale.

À savoir :

- **Uniquement des éléments, pas des ports.** Déposer une extrémité sur un élément la rattache à l’élément lui-même. Pour l’attacher à un port (`api.http`), modifiez le texte. Redéposer une extrémité déjà sur un port sur le même élément conserve le port.
- **Les conteneurs comptent aussi.** Déposer la ligne sur le cadre d’un conteneur, à l’écart de ses éléments internes, la relie au conteneur.
- **Pas de boucle sur soi-même.** Vous ne pouvez pas déposer une extrémité sur l’élément qui porte l’autre.
- **Les lignes dessinées implicitement par une vue ne peuvent pas être reconnectées.** Quand une vue masque un élément, elle rattache ses lignes au plus proche ancêtre visible (relation implicite). Ces lignes n’existent pas sous cette forme dans le texte et n’ont donc pas de poignées. Reconnectez la vraie ligne dans une vue qui montre ses deux extrémités, ou dans le texte.
- **Annuler** (⌘Z ou Ctrl+Z) remet l’extrémité en place dans le texte et sur le canevas, comme pour toute autre modification du canevas.

Le sort de l’identifiant de la ligne et de ce qui le référence dépend de la présence d’un nom. La section suivante explique les identifiants, puis détaille [l’effet de la reconnexion sur les identifiants](#effet-de-la-reconnexion-sur-les-identifiants).

## Identifiants des relations

Chaque relation possède un identifiant. Il n’apparaît pas sur le diagramme, mais d’autres parties de DiaDraw l’utilisent pour désigner une ligne précise :

- **Tracé manuel.** Lorsque vous [déplacez les points de passage d’une ligne](fixed-views.md#infléchir-les-lignes) sur le canevas ou importez un diagramme draw.io, son tracé est enregistré dans la vue sous la forme `route "<id>" via …`.
- **Modifications sur le canevas.** La sélection, la modification du libellé et la suppression d’une ligne utilisent son identifiant, tout comme les petites retouches que le canevas écrit dans votre fichier.
- **SVG exporté.** Chaque ligne est dessinée sous la forme `<g class="dd-edge" data-id="<id>">` ; une page web, un test ou une feuille de style peuvent ainsi la retrouver, par exemple pour mettre en évidence `[data-id="checkout"]`.
- **Relecture.** Avec un identifiant stable, une comparaison montre que « cette ligne pointe désormais ailleurs », plutôt qu’une ligne supprimée et une autre ajoutée.

### Identifiants dérivés

Sans nom explicite, la relation tire son identifiant de ses extrémités : `web -> db` donne `web->db`, `web -> api.http` donne `web->api.http` et une deuxième ligne entre les mêmes éléments prend `web->db#2`, puis `#3`, etc.

C’est pratique, mais l’identifiant suit les extrémités. Il change lorsque vous :

- **reconnectez la ligne** : `web -> legacy` devient `web -> api` ; son identifiant passe de `web->legacy` à `web->api` ;
- **supprimez une ligne parallèle antérieure** : avec `order -> billing` et un second `order -> billing`, supprimer le premier renumérote le second de `order->billing#2` en `order->billing`.

Si vous changez les extrémités en modifiant le texte, tout ce qui désignait l’ancien identifiant cesse de fonctionner : une `route` qui le mentionne est signalée par `Unknown relationship`, et les points de passage manuels sont perdus. La reconnexion sur le canevas s’en charge pour vous (voir ci-dessous).

### Relations nommées

Faites précéder une relation d’un nom et de `=` pour lui donner son propre identifiant :

```
model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"

  calls = web -> legacy "REST" tech "HTTPS"
}

view main {
  route "calls" via 200, 80
}
```

L’identifiant est maintenant `calls` pour de bon. Modifiez la ligne en `calls = web -> api` : le tracé s’applique toujours. Vous pouvez aussi nommer une ligne depuis le canevas : sélectionnez-la et saisissez son nom dans le champ Name de l’inspecteur, qui met ses routes à jour (voir [Nommer une relation depuis l’inspecteur](element-properties.md#nommer-une-relation-depuis-linspecteur)). Des lignes nommées et sans nom peuvent coexister :

```
order -> billing "charge"
refund = order -> billing "refund" tag "async"
order -> billing "audit"
```

Leurs identifiants sont `order->billing`, `refund` et `order->billing#2`. Nommer une ligne ne renumérote pas les autres dans cet exemple.

Règles applicables aux noms :

- Un nom est un identifiant (lettres, chiffres et `_`, sans chiffre initial).
- Il est utilisé tel quel, même à l’intérieur d’un élément : `writes = web -> db` dans `shop { … }` a pour identifiant `writes` et non `shop.writes`.
- Il doit être unique parmi les relations ; un second `x = …` est une erreur. Il peut reprendre le nom d’un élément, car les éléments et les relations ont des identifiants distincts.
- Des mots comme `group`, `port` ou `layout` conviennent comme noms ; la flèche indique à DiaDraw que la ligne est une relation.

### Quand nommer une relation

Nommez une relation si quelque chose en dehors de sa propre ligne en dépend :

- vous avez dessiné son tracé à la main ou comptez le faire ;
- plusieurs lignes relient les deux mêmes éléments et vous pourriez en supprimer ou en changer l’ordre ;
- vous prévoyez de la reconnecter au fil de la conception, par exemple lors de la migration d’un service à un autre ;
- une page, un test ou une feuille de style la référence dans le SVG exporté.

Sinon, laissez-la sans nom : l’identifiant dérivé convient et le texte reste plus court.

### Effet de la reconnexion sur les identifiants

Lorsque vous reconnectez une ligne sur le canevas :

- **Une ligne nommée garde son nom.** `calls = web -> legacy` devient `calls = web -> api` et `route "calls" …` s’applique toujours.
- **Une ligne sans nom prend l’identifiant de ses nouvelles extrémités** : `web->legacy` devient `web->api`. Chaque `route` qui désignait l’ancien identifiant est réécrite avec le nouveau ; les points de passage restent donc associés à la ligne.
- **Les lignes parallèles sont renumérotées et leurs routes les suivent.** Avec deux lignes sans nom `order -> billing`, reconnecter la première à `ledger` fait passer la seconde de `order->billing#2` à `order->billing`. Une route écrite pour la première est réécrite avec `order->ledger`, et celle de la seconde avec `order->billing` : aucun des tracés ne se retrouve sur l’autre ligne. L’inverse vaut aussi : si vous reconnectez une ligne à une paire d’éléments déjà reliée par une ligne déclarée plus loin, cette dernière devient `#2`.
- **La ligne reste sélectionnée**, sous son nouvel identifiant.

Une route conserve ses points de passage après reconnexion de sa ligne. Comme ils ont été placés pour les anciennes extrémités, le tracé peut maintenant faire un détour. Déplacez de nouveau ces points, ou supprimez la ligne `route` de la vue pour laisser l’algorithme calculer le tracé.

Les routes sont les seuls endroits du texte qui désignent une ligne sans nom ; elles seules sont donc réécrites. Une page, un test ou une feuille de style qui retrouve la ligne par `data-id` dans le SVG exporté est externe au fichier et n’est pas mis à jour. Nommez la ligne si quelque chose de ce genre en dépend.

### Identifiants impossibles à écrire comme noms

Un modèle peut aussi être chargé depuis du JSON ou construit par un programme ; ses identifiants de relations ne sont pas forcément des noms valides (`edge-1`, par exemple). Lorsque ce modèle est sérialisé en texte, ces lignes sont écrites sans nom. À la relecture du texte, elles reçoivent donc des identifiants dérivés.

## Voir aussi

- [Référence du DSL : sémantique](../dsl.md#sémantique) pour les règles exactes.
- [Diagrammes à main levée](../dsl.md#diagrammes-à-main-levée) pour `route` et les autres indications de disposition.
- [Dessiner à la main dans les vues fixes](fixed-views.md) pour infléchir les lignes sur le canevas.

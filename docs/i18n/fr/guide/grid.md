# La grille

Activez la grille pour aligner les blocs à la main. Sur macOS, choisissez **View ▸ Show Grid** (⌘') ou cliquez sur le bouton de grille de la barre d’outils ; sous Windows et Linux, choisissez **Show Grid** dans le menu (Ctrl+' sous Linux) ; dans l’application web, cliquez sur **Grid** dans la barre d’outils (⌘' ou Ctrl+'). La grille est tracée derrière le diagramme, avec un pas de 10 unités du diagramme par défaut et une ligne plus sombre tous les cinq pas. Quand on dézoome beaucoup, les lignes claires s’estompent les premières.

Tant que la grille est visible, les gestes sur le canevas s’y alignent :

- **Déplacer un bloc** place son coin supérieur gauche sur le point de grille le plus proche. Sa ligne `pin … at` contient donc des coordonnées de la grille (`pin db at 260, 250` pour un pas de 10). Voir [Déplacer des blocs](moving.md).
- **Redimensionner un bloc** dans une vue `layout fixed` place le coin déplacé sur un point de la grille. Dans les autres vues, où la disposition détermine la position du bloc et où seule sa `size` est inscrite, ainsi que pour un bloc ayant pivoté, ce sont la largeur et la hauteur qui deviennent des multiples entiers du pas. Un redimensionnement avec Maj, qui conserve les proportions, ne s’aligne pas sur la grille. Voir [Redimensionner et faire pivoter](resize-and-rotate.md).
- **Infléchir une ligne** dans une vue fixe place son point de passage sur un point de la grille.

Le glisser avec Option (Alt sous Windows et Linux) n’est pas affecté : le bloc est placé par rapport à ses voisins, selon la disposition. Dans une vue fixe, où ce geste écrit tout de même `pin … at`, il place le bloc exactement là où vous le déposez, sans suivre la grille. La rotation conserve ses propres crans de 15°.

La grille est un réglage de l’application et non du dessin : elle n’inscrit rien dans le texte et reste activée ou désactivée pour tous les documents que vous ouvrez. Sur macOS, **View ▸ Grid Size** permet de choisir un pas de 5, 10, 20 ou 40 unités (et active la grille), tout comme la liste des tailles à côté de **Grid** dans l’application web. Le choix est mémorisé, dans le navigateur pour l’application web. Masquer la grille ne déplace rien ; les déplacements suivants se terminent exactement là où vous relâchez le pointeur.

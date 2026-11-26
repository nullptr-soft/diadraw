# Diagrammes dans Confluence

DiaDraw pour Confluence Cloud ajoute la macro **DiaDraw diagram** aux pages Confluence. Vous écrivez le texte du diagramme dans l’éditeur de la macro, et la page affiche le diagramme, dessiné dans le navigateur du lecteur par le même cœur que les applications. Le texte est enregistré avec la macro : toute personne qui peut modifier la page peut rouvrir le diagramme et le changer.

L’application est une application Atlassian Forge. Rien n’est dessiné sur un serveur DiaDraw : le texte du diagramme reste dans votre site Confluence.

## Ajouter un diagramme

1. Modifiez une page, tapez `/diadraw` et choisissez **DiaDraw diagram**.
2. L’éditeur s’ouvre avec un petit exemple. Remplacez-le par votre diagramme : le texte est à gauche et le dessin à droite se met à jour pendant la saisie. Les erreurs sont listées sous le texte avec leur ligne et leur colonne ; cliquez sur l’une d’elles pour vous y rendre.
3. Choisissez la vue à afficher dans **View** quand le texte en a plusieurs, et un thème dans **Theme**.
4. Cliquez sur **Save** (ou Ctrl+S, ⌘S sur Mac). **Cancel** laisse la macro telle qu’elle était.

Pour modifier le diagramme plus tard, sélectionnez la macro dans l’éditeur de page et cliquez sur son bouton **Edit**.

```diadraw render
model {
  author = person "Author"
  confluence = system "Confluence page" {
    macro = container "DiaDraw macro" tech "Forge app"
    core = container "DiaDraw core" tech "WebAssembly"
  }
  author -> confluence.macro "writes the text"
  confluence.macro -> confluence.core "draws it in the browser"
}

view page of confluence {
  include *
  layout layered direction right
}
```

## Choisir un thème

Le sélecteur de thème liste les thèmes intégrés (`default`, `dark`, `mono`) et ceux que votre texte déclare. Choisir un thème écrit une ligne `theme` dans le texte, celle que vous auriez tapée, pour que le texte reste la description complète du diagramme. Choisir `default` retire la ligne. Si la vue a sa propre ligne `theme`, le sélecteur modifie celle-ci.

## Importer Mermaid, PlantUML et d’autres diagrammes

**Import…** convertit un diagramme d’un autre outil en texte DiaDraw : collez-le ou choisissez un fichier, puis le format (Mermaid, PlantUML, Structurizr DSL, draw.io ou BPMN 2.0). L’import remplace le texte de l’éditeur, et Ctrl+Z ramène l’ancien texte. Ce que l’import n’a pas pu reprendre est listé sous l’éditeur, pour que vous l’ajoutiez à la main. C’est un moyen rapide de passer des macros Mermaid ou PlantUML à DiaDraw.

## Enregistrer un diagramme comme image

Survolez un diagramme dans la page et deux boutons apparaissent dans son coin : **SVG** et **PNG** enregistrent le diagramme dans un fichier. Le PNG est dessiné au double de la taille du diagramme, net pour des diapositives.

## Exporter la page

Les exports PDF et Word de Confluence ne peuvent pas exécuter l’application : dans un export, la macro montre donc le texte du diagramme en bloc de code sous le nom de sa vue. Enregistrez le diagramme en SVG ou PNG et joignez-le quand un export doit montrer l’image.

## Publier des diagrammes depuis un dépôt

Quand vos diagrammes sont des fichiers `.draw` dans un dépôt, publiez-les sur une page au lieu de les coller dans des macros. Le publieur dessine chaque vue de chaque fichier en SVG et la joint à la page : `checkout.draw` devient `checkout.svg`, ou `checkout-<vue>.svg` quand le fichier a plusieurs vues. Chaque nouvelle publication ajoute une version aux pièces jointes dont le dessin a changé et ignore les autres : les images de la page suivent le dépôt.

Insérez une pièce jointe dans la page une fois (avec le sélecteur d’images de Confluence, ou `--insert`, qui ajoute une image de chaque nouvelle pièce jointe à la fin de la page) ; les publications suivantes la mettent à jour sur place. L’identifiant de la page est le nombre de son URL après `/pages/`.

Dans un terminal, avec le dépôt compilé (voir le [README de l’application](../../src/apps/confluence/README.md)) :

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net
export CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…   # from id.atlassian.com, Security, API tokens
node path/to/diadraw/src/apps/confluence/dist/cli.js publish docs/diagrams/*.draw --page 123456 --insert
```

`--view` ne publie que les vues nommées, `--dry-run` liste les pièces jointes sans contacter Confluence, et un fichier avec des erreurs arrête la publication avec ses erreurs, comme `diadraw check`.

Dans GitHub Actions, l’action `DiaDraw to Confluence` fait de même à chaque push. Gardez le compte et son jeton d’API dans les secrets du dépôt :

```yaml
name: Diagrams to Confluence
on:
  push:
    branches: [main]
    paths: ['docs/diagrams/**']
jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mykulyak/diadraw/src/apps/confluence@main
        with:
          files: docs/diagrams/**/*.draw
          page-id: '123456'
          base-url: https://example.atlassian.net
          email: ${{ secrets.CONFLUENCE_EMAIL }}
          api-token: ${{ secrets.CONFLUENCE_API_TOKEN }}
```

L’action compile le cœur de DiaDraw à chaque exécution, ce qui prend quelques minutes, en attendant que le cœur soit publié comme paquet.

## Installer l’application

L’application n’est pas encore sur l’Atlassian Marketplace. Un administrateur du site l’installe depuis le dépôt DiaDraw avec l’outil en ligne de commande Forge ; les étapes sont dans [src/apps/confluence/README.md](../../src/apps/confluence/README.md).

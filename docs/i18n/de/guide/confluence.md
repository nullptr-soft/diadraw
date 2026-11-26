# Diagramme in Confluence

DiaDraw für Confluence Cloud fügt Confluence-Seiten das Makro **DiaDraw diagram** hinzu. Du schreibst den Text des Diagramms im Editor des Makros, und die Seite zeigt das Diagramm, gezeichnet im Browser der Leser vom selben Kern wie in den Apps. Der Text wird mit dem Makro gespeichert, sodass jeder, der die Seite bearbeiten darf, das Diagramm wieder öffnen und ändern kann.

Die App ist eine Atlassian-Forge-App. Nichts wird auf einem DiaDraw-Server gezeichnet: Der Text des Diagramms bleibt in deiner Confluence-Site.

## Ein Diagramm einfügen

1. Bearbeite eine Seite, tippe `/diadraw` und wähle **DiaDraw diagram**.
2. Der Editor öffnet sich mit einem kleinen Beispiel. Ersetze es durch dein Diagramm: Links steht der Text, rechts die Zeichnung, die sich beim Tippen aktualisiert. Fehler stehen mit Zeile und Spalte unter dem Text; ein Klick springt zur Stelle.
3. Wähle unter **View** die Ansicht, wenn der Text mehrere hat, und unter **Theme** ein Theme.
4. Klicke auf **Save** (oder Strg+S, ⌘S auf dem Mac). **Cancel** lässt das Makro, wie es war.

Um das Diagramm später zu ändern, wähle das Makro im Seiteneditor aus und klicke auf seine Schaltfläche **Edit**.

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

## Ein Theme wählen

Die Theme-Auswahl listet die eingebauten Themes (`default`, `dark`, `mono`) und die Themes, die dein Text deklariert. Die Wahl schreibt eine `theme`-Zeile in den Text, dieselbe Zeile, die du tippen würdest, sodass der Text das Diagramm vollständig beschreibt. `default` nimmt die Zeile wieder heraus. Hat die Ansicht eine eigene `theme`-Zeile, ändert die Auswahl diese.

## Mermaid, PlantUML und andere Diagramme importieren

**Import…** wandelt ein Diagramm aus einem anderen Werkzeug in DiaDraw-Text um: Füge es ein oder wähle eine Datei und das Format (Mermaid, PlantUML, Structurizr DSL, draw.io oder BPMN 2.0). Der Import ersetzt den Text im Editor, Strg+Z holt den alten Text zurück. Was der Import nicht übernehmen konnte, steht unter dem Editor, damit du es von Hand ergänzen kannst. So ziehst du Diagramme schnell aus Mermaid- oder PlantUML-Makros zu DiaDraw um.

## Ein Diagramm als Bild speichern

Zeigst du auf ein Diagramm in der Seite, erscheinen in seiner Ecke zwei Schaltflächen: **SVG** und **PNG** speichern das Diagramm als Datei. Das PNG wird in doppelter Größe gezeichnet, scharf genug für Folien.

## Die Seite exportieren

Die PDF- und Word-Exporte von Confluence können die App nicht ausführen. Im Export zeigt das Makro daher den Text des Diagramms als Codeblock unter dem Namen seiner Ansicht. Speichere das Diagramm als SVG oder PNG und hänge es an, wenn ein Export das Bild zeigen muss.

## Diagramme aus einem Repository veröffentlichen

Liegen deine Diagramme als `.draw`-Dateien in einem Repository, veröffentliche sie auf einer Seite, statt sie in Makros einzufügen. Der Publisher zeichnet jede Ansicht jeder Datei als SVG und hängt sie an die Seite an: Aus `checkout.draw` wird `checkout.svg`, oder `checkout-<view>.svg`, wenn die Datei mehrere Ansichten hat. Bei jeder weiteren Veröffentlichung bekommt jeder Anhang, dessen Zeichnung sich geändert hat, eine neue Version; der Rest wird übersprungen. So folgen die Bilder auf der Seite dem Repository.

Füge einen Anhang einmal in die Seite ein (mit der Bildauswahl von Confluence oder mit `--insert`, das am Ende der Seite ein Bild jedes neuen Anhangs anfügt); spätere Veröffentlichungen aktualisieren ihn an Ort und Stelle. Die ID der Seite ist die Zahl in ihrer URL nach `/pages/`.

Im Terminal, mit gebautem Repository (siehe [README der App](../../src/apps/confluence/README.md)):

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net
export CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…   # from id.atlassian.com, Security, API tokens
node path/to/diadraw/src/apps/confluence/dist/cli.js publish docs/diagrams/*.draw --page 123456 --insert
```

`--view` veröffentlicht nur die genannten Ansichten, `--dry-run` listet die Anhänge auf, ohne Confluence zu kontaktieren, und eine Datei mit Fehlern bricht die Veröffentlichung mit ihren Fehlern ab, wie `diadraw check`.

In GitHub Actions erledigt die Action `DiaDraw to Confluence` dasselbe bei jedem Push. Lege das Konto und sein API-Token in Repository-Secrets ab:

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

Die Action baut den DiaDraw-Kern bei jedem Lauf, was einige Minuten dauert, bis der Kern als Paket veröffentlicht ist.

## Die App installieren

Die App ist noch nicht im Atlassian Marketplace. Ein Site-Administrator installiert sie aus dem DiaDraw-Repository mit dem Forge-Kommandozeilenwerkzeug; die Schritte stehen in [src/apps/confluence/README.md](../../src/apps/confluence/README.md).

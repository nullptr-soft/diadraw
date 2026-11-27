# Stile und Designs auf der Zeichenfläche

Drei Dinge bestimmen das Aussehen eines Diagramms. Das **Design** legt die Farben und Schriften für die gesamte Zeichnung fest. **Stilblöcke** geben allen Elementen einer Art, allen Objekten mit einem Tag oder allen Beziehungen ein gemeinsames Aussehen. Der **Inline-Stil** eines Elements (das Feld „Style“ im [Inspektor](element-properties.md)) ändert nur dieses Element. Im Text sieht das so aus:

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

Die [DSL-Referenz](../dsl.md#stile-und-designs) nennt alle Selektoren und Schlüssel. Dieses Kapitel erklärt, wie du sie auf der Zeichenfläche bearbeitest: Du musst das Diagramm dafür nicht verlassen, und jede Änderung landet weiterhin im Text.

## Wann verwende ich was?

- Verwende einen **Stilblock**, wenn das Aussehen an eine Art oder ein Tag gebunden ist: „Alle Datenbanken sind orange“, „Alles mit dem Tag `external` ist grau und gestrichelt“. Neue Elemente dieser Art oder mit diesem Tag übernehmen den Stil automatisch.
- Verwende das Feld **Style** des Elements für das eine Element, das anders aussehen soll.
- Verwende ein **Design** für das gesamte Diagramm: Hintergrund, Linienfarbe, Schrift und Standardfarben jeder Art. Wähle ein eingebautes Design (`default`, `dark`, `mono` für den Druck, `ocean`, `blueprint` und weitere, siehe [DSL-Referenz](../dsl.md)) oder erstelle ein eigenes.

Treffen mehrere Vorgaben zu, gewinnt die spezifischere: zuerst das Design, dann `*`-Blöcke, Art-Blöcke, Tag-Blöcke, Blöcke für Art und Tag zusammen und zuletzt der eigene Stil des Elements.

## Gemeinsame Stile eines Elements

Wähle ein einzelnes Element aus. Unten im Inspektor erscheint **Shared styles**. Das Feld listet die auf dieses Element zutreffenden Stilblöcke in ihrer Reihenfolge im Text auf, einen pro Zeile:

```
container: fill=#438dd5
tag "external": fill=#999999;dashed=1
```

Jede Zeile enthält einen Selektor, einen Doppelpunkt und die Schlüssel in der Schreibweise des Style-Felds (`key=value`, getrennt durch `;`). Änderst du das Feld, bearbeitest du die Blöcke:

| Deine Änderung | Wirkung im Text |
| --- | --- |
| Wert ändern | Die entsprechende Zeile des Blocks wird neu geschrieben. |
| Schlüssel zu einer Zeile hinzufügen | Der Schlüssel wird am Blockende ergänzt. |
| Schlüssel entfernen | Seine Zeile im Block wird gelöscht. |
| Zeile hinzufügen, etwa `database: fill=red` | Ein neuer `style`-Block folgt auf die angezeigten Blöcke. |
| Zeile löschen | Ihr Block wird gelöscht. |
| Selektor einer Zeile ändern | Der Selektor des Blocks ändert sich, sein Inhalt bleibt. |

Das gestaltet bewusst **jedes** Element neu, auf das der Block zutrifft, nicht nur das ausgewählte. Um nur dieses Element zu ändern, verwende stattdessen sein Style-Feld.

Das Feld akzeptiert nur Selektoren, die auf das ausgewählte Element zutreffen: seine Art, eines seiner Tags, die Art zusammen mit einem seiner Tags oder `*`. Eine Zeile für eine andere Art oder für Beziehungen wird mit einer Meldung in der Statuszeile abgelehnt. Bearbeite solche Zeilen ohne Auswahl (siehe unten).

## Design und Stile des Dokuments

Klicke auf eine leere Stelle der Zeichenfläche. Solange nichts ausgewählt ist, zeigt der Eigenschaftenbereich daneben das Dokument:

- **Theme**: die `theme`-Anweisung des Dokuments. Gib ein eingebautes Design (etwa `default`, `dark`, `ocean`), ein selbst deklariertes Design oder ein Bibliotheksdesign (`acme.corporate`) ein. Leeren des Feldes entfernt die Anweisung.
- **This view's theme**: das `theme` in der aktuellen Ansicht, das nur für diese Ansicht Vorrang vor dem Dokumentdesign hat. Leer bedeutet, dass die Ansicht das Dokumentdesign verwendet.
- **Style blocks**: alle Stilblöcke im Dokument, auch Beziehungsstile wie `relationship tag "async": dashed=1`, einer pro Zeile wie oben.

### Ein eigenes Design erstellen

Trage unter „Theme“ (oder „This view's theme“) einen noch nicht vergebenen Designnamen ein, etwa `night`. DiaDraw deklariert das Design auf Grundlage des ersetzten Designs und wählt es aus:

```
theme night extends dark {}

theme night
```

Wird die Ansicht mit einem im Dokument deklarierten Design gezeichnet, zeigt der Bereich den zugehörigen Abschnitt **Theme night** mit drei weiteren Feldern:

- **Extends**: das Design, von dem es ausgeht. Leer bedeutet `default`.
- **Theme keys**: ein `key=value` pro Zeile: `background`, `fontFamily`, `fontSize`, `edge`, `edgeText`, `boundary` sowie die übrigen in der [DSL-Referenz](../dsl.md#eigene-designs) aufgeführten Schlüssel.
- **Node colours**: eine Art pro Zeile mit `fill`, `stroke` und `text`: `person: fill=#08427b;stroke=#052e56;text=#ffffff`. `default` steht für jede Art ohne eigene Zeile, `external` für jedes Element mit dem Tag `external`. Bei einer Person färbt `text` den Namen nur, wenn `shape` sie als gefüllten Kasten zeichnet. Der Name eines Strichmännchens steht auf der Zeichenfläche in der Farbe `labelText`.

Eingebaute Designs und Bibliotheksdesigns lassen sich nicht direkt bearbeiten. Erstelle ein eigenes Design, das sie erweitert.

### In den Apps für macOS, Web und Linux

Die Apps zeigen diese Felder als Bedienelemente an, die denselben Text schreiben:

- **Theme**, **This view's theme** und **Extends** sind Kombinationsfelder mit den eingebauten Designs, den Bibliotheksdesigns und den im Dokument deklarierten Designs. Neue Namen lassen sich weiterhin eintippen.
- **Style blocks** enthält eine Zeile pro Block. Wähle den Selektor oder tippe ihn ein (`*`, `relationship`, eine Art, `tag "external"`, `relationship tag "async"`). Klappe die Zeile mit dem Dreieck auf, um ihre Schlüssel im selben Stileditor wie im Elementinspektor zu ändern. Mit − entfernst du sie, **Add style block** fügt eine Zeile hinzu.
- **Theme keys** bietet eine Farbschaltfläche für jeden Farbschlüssel (`background`, `edge`, `boundary`, …) und Textfelder für `fontFamily` und Schriftgrößen. Die Farbschaltflächen bieten dieselbe Palette wie der Elementinspektor, mit „Custom…“ für weitere Farben. Das × neben der Schaltfläche entfernt den Schlüssel, sodass wieder der Wert aus dem zugrunde liegenden Design gilt. Nicht aufgeführte Schlüssel bleiben erhalten.
- **Node colours** zeigt eine Zeile pro Art mit Farbschaltflächen für Füllung, Linie und Text. Wähle eine Art und klicke für eine neue Zeile auf „Add“. Sie wird geschrieben, sobald sie eine Farbe hat.

In der Web-App schlagen die Designfelder die eingebauten und die im Dokument deklarierten Designs vor. Das Dreieck eines Stilblocks klappt ihn zu einer einzeiligen Übersicht seiner Schlüssel ein.

In der Linux-App (GTK) sind Theme, This view's theme und Extends Eingabefelder mit einem ▾ für die Designliste. Style blocks ist eine Blockliste mit einem ▸ zum Öffnen des Stileditors; Theme keys und Node colours bieten Farbschaltflächen.

## Eine Änderung übernehmen

Die Felder funktionieren wie im Elementinspektor. Die Eingabetaste oder das Verlassen eines geänderten Felds übernimmt die Änderung, Escape setzt sie zurück. Mehrzeilige Felder (Shared styles, Style blocks, Theme keys, Node colours) werden beim Verlassen übernommen, in der Web-App auch mit ⌘Enter / Strg+Enter. In der macOS-App übernimmt die Eingabetaste „Shared styles“; ⌥Return beginnt eine neue Zeile. In beiden Apps gilt eine ausgewählte Farbe oder Option in den Design- und Stileinstellungen sofort.

Jede Änderung ist ein einzelner Rückgängig-Schritt, gemeinsam mit dem Texteditor: ⌘Z nimmt sie auf beiden Seiten zurück.

## Was sich im Text ändert

Der Text bleibt maßgeblich; der Eigenschaftenbereich ändert so wenig wie möglich:

- Ein geänderter Wert wird an Ort und Stelle ersetzt, sodass Kommentare und Ausrichtung der Zeile erhalten bleiben.
- Bei einem entfernten Schlüssel wird seine Zeile gelöscht. Ein einzeiliger Block wie `style tag "external" { fill "#999999" dashed true }` bleibt einzeilig.
- Neue Schlüssel kommen ans Blockende, neue Blöcke nach den übrigen Stilblöcken (oder vor den `model`-Block, wenn es noch keine gibt), eine neue Designanweisung nach den Designdeklarationen.
- Deine Reihenfolge im Text bleibt erhalten, unabhängig von der Eingabereihenfolge im Feld.

Änderst du etwa die Containerfüllung im Eigenschaftenbereich, wird aus

```
style container {
  fill "#438dd5"   // brand blue
  stroke "#3c7fc0"
}
```

folgender Text:

```
style container {
  fill "#1168bd"   // brand blue
  stroke "#3c7fc0"
}
```

## Gut zu wissen

- **`true` und `false`** werden wie bei draw.io als `1` und `0` gespeichert. Deshalb erscheint `dashed true` in den Feldern als `dashed=1`. Ein unveränderter Eintrag behält das von dir geschriebene Wort.
- **Werte** dürfen in Stilfeldern wie in Inline-Stilen kein `;` enthalten. Designschlüssel stehen einzeln pro Zeile, dort dürfen Werte daher `;` enthalten.
- **Mehrere Blöcke mit demselben Selektor** erscheinen als mehrere Zeilen. Setzen sie denselben Schlüssel, gewinnt der spätere.
- **Ein Design mit zwei `node`-Blöcken für dieselbe Art** wird neu ausgegeben, wenn du die Farben der Art im Eigenschaftenbereich änderst. Der Bereich kann nicht erkennen, welchen Block du meinst.
- **Das Umbenennen oder Löschen einer Designdeklaration** erfolgt im Text. Der Eigenschaftenbereich bearbeitet das Design, mit dem die Ansicht gezeichnet wird.
- **Verfügbar in:** der Web-App und den Apps für macOS und Linux (GTK). Die Windows-App hat noch keinen Inspektor.

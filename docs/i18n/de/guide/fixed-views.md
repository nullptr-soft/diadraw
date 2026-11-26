# Von Hand in festen Ansichten zeichnen

Die meisten Ansichten werden automatisch angeordnet: Du gibst an, was sie enthalten, und steuerst das Layout bei Bedarf mit Hinweisen wie `pin db below web`. Manchmal möchtest du das Gegenteil: jeden Kasten genau dort, wo du ihn abgesetzt hast, in der gewählten Größe und mit Linien, die genau an den vorgegebenen Stellen abknicken. Dafür ist `layout fixed` gedacht. Aus draw.io importierte Diagramme verwenden es, und du kannst es in jeder selbst geschriebenen Ansicht einsetzen:

```
view checkout {
  include *
  layout fixed
  pin web at 40, 40 size 160, 70
  pin db at 320, 200
  route "writes" via 380, 90
}
```

`pin … at X, Y` setzt die linke obere Ecke eines Kastens auf einen Punkt; `size W, H` legt Breite und Höhe fest (ohne diese Angabe richtet sich die Größe nach der Beschriftung). `route "id" via X, Y …` führt die Knickpunkte einer Linie auf, die über die [ID der Beziehung](relationships.md#beziehungs-ids) angegeben wird. Elemente ohne `pin` stehen in einer Reihe unter den fixierten Elementen.

Das alles kannst du als Text schreiben – oder einfach auf der Zeichenfläche zeichnen.

## Verschieben

Ziehe einen Kasten, um ihn zu verschieben. In einer festen Ansicht landet er genau dort, wo du loslässt, mit oder ohne Wahltaste. Seine `pin … at`-Zeile erhält die neue Position. Eine vorhandene Größenangabe bleibt erhalten.

## Größe ändern

Wähle einen einzelnen Kasten aus. An seinen Ecken erscheinen vier kleine quadratische Griffe. Ziehe an einem, um die Größe zu ändern. Die gegenüberliegende Ecke bleibt stehen, und über dem Kasten erscheint beim Ziehen die neue Größe. Kästen werden nicht kleiner als 10 × 10.

Beim Loslassen erhält der `pin` des Kastens die neue Position und Größe:

```
  pin web at 40, 40   // storefront
```

wird zu

```
  pin web at 40, 40 size 180, 82   // storefront
```

## Linien knicken

Wähle eine einzelne Linie aus. Auf ihr erscheinen Griffe:

- ein runder Griff an jedem **Knickpunkt**: Ziehe ihn, um den Knick zu verschieben;
- ein kleinerer, hellerer Griff **in der Mitte jedes Segments**: Ziehe ihn, um dort einen Knick einzufügen;
- ziehe einen Knick auf die Gerade zwischen seinen Nachbarn, um ihn zu **entfernen**.

Beim Loslassen enthält die `route` der Linie alle Knickpunkte in ihrer Reihenfolge:

```
  route "writes" via 380, 90 380, 150
```

Ein vorhandenes `from` oder `to` bleibt erhalten (also die Stelle, an der die Linie aus einem Kasten austritt oder in ihn eintritt). Hat eine Route weder Knickpunkte noch `from` oder `to`, wird sie entfernt und die Linie wieder gerade gezeichnet.

Eine unbenannte Beziehung erhält ihre Route über die abgeleitete ID, etwa `route "web->db"`. Änderst du die Endpunkte der Beziehung oder löschst du eine parallele Beziehung, ändert sich diese ID und die Route greift nicht mehr. [Benenne die Beziehung](relationships.md#wann-sich-ein-name-lohnt) deshalb, wenn du ihre Linie von Hand formst: `writes = web -> db`.

## Was sich im Text ändert

Wie bei jeder Änderung auf der Zeichenfläche wird nur ein kleiner Teil deiner Datei geändert. Sie bildet einen einzelnen Rückgängig-Schritt, gemeinsam mit dem Texteditor (⌘Z nimmt sie auf beiden Seiten zurück):

- Ein vorhandenes `pin` oder `route` wird **an Ort und Stelle** überschrieben. Ein nachgestellter Kommentar bleibt erhalten, ebenso die Schreibweise des Namens in der Ansicht eines Systems (`pin web`, nicht `pin shop.web`).
- Ein neues `pin` oder `route` wird nach den übrigen Zeilen der Ansicht eingefügt.
- Koordinaten werden auf ganze Zahlen gerundet.

## Gut zu wissen

- **Knickpunkte nur in festen Ansichten.** Automatisch angeordnete Ansichten zeigen keine Knickgriffe: Ihre Linien werden vom Layout bestimmt. Kästen kannst du in jeder Ansicht skalieren und drehen; siehe [Größe ändern und drehen](resize-and-rotate.md). Füge der Ansicht `layout fixed` hinzu, um von Hand zu zeichnen, oder steuere das Layout weiterhin mit Hinweisen.
- **Immer nur ein Objekt.** Griffe erscheinen, wenn genau ein Kasten oder genau eine Linie ausgewählt ist.
- **Orthogonale Linien** (`edgeStyle=orthogonalEdgeStyle`) behalten ihre rechten Winkel: Verschiebst du einen Knick, erhält die Linie die dafür erforderlichen Ecken.
- **Verfügbar in:** der Web-App sowie den Apps für macOS, Linux (GTK) und Windows.

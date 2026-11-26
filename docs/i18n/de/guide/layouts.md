# Layouts

Die `layout`-Zeile einer Ansicht wählt den Algorithmus, der ihre Elemente platziert und ihre Linien zeichnet. Bei automatischen Layouts musst du keine Koordinaten angeben: Wähle den passenden Algorithmus, gib eine Richtung vor und korrigiere ihn bei Bedarf mit Hinweisen.

```
view checkout of shop {
  include *
  layout tree direction right spacing 40
}
```

`direction` (`down`, `right`, `up`, `left`) und `spacing` (Pixel zwischen Kästen) sind optional. Ohne `layout`-Zeile verwendet die Ansicht `layered` mit Fluss nach unten.

## Einen Algorithmus auswählen

| Algorithmus | Platzierung der Elemente | Linien | Geeignet für |
| --- | --- | --- | --- |
| `layered` | in Schichten entlang des Flusses mit wenigen Kreuzungen | rechtwinklig | Architektur, Flussdiagramme, alles mit Flussrichtung |
| `tree` | als geordneter Baum, Eltern mittig über ihren Kindern | wie im Organigramm | Organigramme, Hierarchien, Entscheidungsbäume, Menüs |
| `grid` | zeilen- oder spaltenweise in der angegebenen Reihenfolge | rechtwinklig | Kataloge, Inventare, Übersichten aller Dienste |
| `radial` | in Ringen um das am besten vernetzte Element | gerade | Nabe und Speichen, ein System und seine Nachbarn |
| `force` | verbundene Elemente ziehen sich an, andere stoßen sich ab | gerade | Netze und Karten ohne Hauptrichtung |
| `fixed` | genau dort, wo `pin … at` sie platziert | wie vorgegeben | von Hand gezeichnete und importierte Diagramme |
| `sequence` | als Lebenslinien mit den Schritten von `sequence { … }` | Nachrichten | Interaktionen im Zeitverlauf (siehe DSL-Referenz) |

Beginne im Zweifel mit `layered`: Es verarbeitet jeden Graphen und berücksichtigt die Pfeilrichtung. Wechsle, wenn das gewünschte Bild eine bestimmte Form hat.

### layered

Der Standard. Elemente stehen in Schichten, sodass die meisten Linien in die Richtung der Ansicht weisen. Die Reihenfolge innerhalb jeder Schicht wird so gewählt, dass Kreuzungen vermieden werden. Linien führen rechtwinklig um Kästen herum.

### tree

Jedes Element hängt unter dem Element, dessen Linie es zuerst erreicht. `a -> b` macht `b` also zum Kind von `a`. Elemente ohne eingehende Linie sind die Wurzeln und stehen in der angegebenen Reihenfolge nebeneinander. Kinder folgen der Reihenfolge ihrer Linien. Auch Linien außerhalb des Baums (ein zweites Elternelement, eine Linie zurück nach oben) werden um die Kästen herum gezeichnet.

Baumlinien laufen wie in einem Organigramm vom Elternelement zu einem Balken kurz hinter seiner Reihe, dann quer und weiter zum Kind. Die Beschriftung sitzt am letzten Stück der Linie neben dem Kind. Der Reihenabstand wächst, bis sie hineinpasst. `direction right` legt den Baum auf die Seite; `up` und `left` kehren die Richtung um.

### grid

Elemente füllen ein annähernd quadratisches Raster in der Reihenfolge der Ansicht (`include`-Reihenfolge, bei `include *` Modellreihenfolge). Neun Elemente ergeben drei Dreierreihen, zehn ergeben Viererreihen (vier, vier und zwei). Jede Spalte ist so breit wie ihr breitester Kasten, jede Zeile so hoch wie ihr höchster. Kästen stehen mittig in ihren Zellen.

`direction down` (Standard) füllt zeilenweise von oben nach unten, `right` spaltenweise nach rechts. `up` und `left` beginnen auf der anderen Seite. Linien verlaufen zwischen den Zellen. Das Raster eignet sich deshalb für Ansichten mit wenigen Linien; wähle bei vielen Linien `layered`.

### radial

Das Element mit den meisten Linien steht in der Mitte. Seine direkten Nachbarn sitzen auf einem Ring darum, deren Nachbarn auf dem nächsten Ring und so weiter. Jeder Zweig erhält einen Kreissektor entsprechend der Zahl seiner Elemente. Die Ringabstände lassen Platz für Kästen und Beschriftungen. Nicht miteinander verbundene Teile der Ansicht liegen nebeneinander. Eine Richtung ist hier nicht anwendbar.

### force

Eine Simulation: Jedes Elementpaar stößt sich ab, jede Linie zieht ihre Enden zusammen. Die Bewegung kommt nach einer festen Anzahl von Runden zum Stillstand. Eng verbundene Elemente liegen am Ende nah beieinander. So werden Gruppen sichtbar, die du in einer Schichtenansicht vielleicht nicht erkennst. Dieselbe Ansicht ergibt jedes Mal dasselbe Bild, aber schon ein zusätzliches oder entferntes Element kann die anderen merklich verschieben. Soll alles an Ort und Stelle bleiben, nutze Hinweise oder wechsle zu `fixed`, sobald dir das Bild gefällt. Eine Richtung ist hier nicht anwendbar.

## Container

Alle automatischen Algorithmen ordnen zuerst das Innere jedes Containers mit demselben Algorithmus an (etwa die Container eines Systems oder die Mitglieder einer Gruppe). Anschließend platzieren sie den Container als einen Kasten zwischen seinen Nachbarn. Linien zwischen Elementen verschiedener Container werden um dazwischenliegende Kästen herumgeführt.

## Hinweise gelten weiterhin

`pin a below b`, `align a b horizontally` und `order a b c` funktionieren mit jedem automatischen Algorithmus. Erfüllt die Platzierung einen Hinweis nicht, verschiebt ein Korrekturdurchlauf die Elemente nachträglich und führt betroffene Linien neu. `diadraw render` warnt bei Hinweisen, die sich dennoch nicht erfüllen lassen. Beim Ziehen eines Elements auf der Zeichenfläche entsteht `pin … at` (siehe [Kästen verschieben](moving.md)); hältst du die Wahltaste oder Alt gedrückt, entstehen stattdessen diese relativen Hinweise. Dasselbe gilt also auch für solche Änderungen auf der Zeichenfläche.

## Unbekannte Namen

Bei einem falsch geschriebenen oder nicht unterstützten Namen wie `layout circular` oder `layout layred` zeichnet DiaDraw die Ansicht mit `layered`. Eine Warnung an der `layout`-Zeile nennt die bekannten Namen. Der Name bleibt in der Datei stehen. So geht nichts verloren, falls eine spätere Version ihn unterstützt.

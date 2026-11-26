# Kästen verschieben

Ziehe einen Kasten, um ihn zu verschieben. Er bleibt dort, wo du loslässt – in jeder Ansicht und unabhängig vom Layout. Beim Loslassen wird eine Zeile in die Ansicht geschrieben:

```
view main {
  include *
  layout layered direction down
  pin db at 262, 246
}
```

Wie `size` oder `rotate` gehört die Zeile nur zu dieser Ansicht. Dasselbe Element kann also in verschiedenen Ansichten an verschiedenen Stellen stehen. In einem automatischen Layout ordnet der Algorithmus die anderen Kästen weiterhin an. Landet der verschobene Kasten auf einem anderen, weicht dieser aus, und berührende Linien werden neu geführt. Ziehst du den Kasten erneut, erhält seine `pin … at`-Zeile die neue Position.

## An Nachbarn ausrichten

Halte beim Ziehen die Wahltaste gedrückt (Alt unter Windows und Linux), um den Kasten stattdessen relativ zu anderen zu platzieren. Über dem Kasten siehst du währenddessen die Zeile, die beim Loslassen geschrieben würde:

- Liegt er mit einem anderen Kasten bis auf 14 Pixel auf einer Linie, entsteht `align api db horizontally` (oder `vertically`).
- Andernfalls entsteht `pin db right of api` (oder `left of`, `above`, `below`), bezogen auf den nächsten Kasten.

Diese Zeilen behalten ihre Bedeutung, wenn sich das Layout ändert. Sie eignen sich deshalb für wachsende Diagramme. Ist die Aussage bereits wahr, etwa wenn zwei Kästen schon in einer Reihe liegen, bleibt der Kasten an der vom Layout bestimmten Stelle.

In einer Ansicht mit `layout fixed` entsteht beim Loslassen immer `pin … at`, mit oder ohne Wahltaste. Siehe [Von Hand in festen Ansichten zeichnen](fixed-views.md).

## Mehrere Kästen auswählen und verschieben

Klicke auf einen Kasten, um ihn auszuwählen. Mit Umschalt-Klick (oder Befehlstaste auf dem Mac, Strg unter Windows und Linux) fügst du einen Kasten oder eine Linie zur Auswahl hinzu oder nimmst das Objekt wieder heraus. Um einen Bereich auszuwählen, ziehe von einer freien Stelle der Zeichenfläche aus einen Auswahlrahmen. Alles, was vollständig darin liegt, wird ausgewählt – Kästen ebenso wie Linien. Halte Umschalt oder die Befehlstaste/Strg gedrückt, um die bestehende Auswahl zu erweitern. Auf der Zeichenfläche wählt ⌘A (Strg+A in der Linux-App und in der Web-App unter Windows und Linux) alles in der Ansicht aus. Escape oder ein Klick auf eine freie Stelle hebt die Auswahl auf. Ein Klick ohne Ziehen auf einen von mehreren ausgewählten Kästen wählt nur ihn aus.

Ziehe einen der ausgewählten Kästen: Alle bewegen sich gemeinsam und behalten ihre Lage zueinander. Der Umriss jedes Kastens zeigt seine Zielposition. Beim Loslassen wird für jeden Kasten eine `pin … at`-Zeile geschrieben. Das zählt als eine Änderung; einmal Rückgängig bringt alle zurück:

```
view main {
  include *
  layout layered direction down
  pin api at 262, 120
  pin db at 262, 246
}
```

Eine Gruppe landet immer dort, wo du loslässt. Die Wahltaste (Alt) richtet sie nicht an ihren Nachbarn aus. In einer Ansicht mit `layout fixed` behält eine Linie ihre Form, wenn beide Enden verschoben werden, denn ihre Knickpunkte wandern mit. Entf (oder die Rückschritttaste) löscht alles Ausgewählte.

Ein Klick innerhalb eines Containers wählt den Container aus, statt einen Rahmen aufzuziehen. Beginne den Rahmen also außerhalb oder wähle die Kästen mit Umschalt-Klick aus.

## Einen Kasten zurücksetzen

Lösche seine `pin`-, `align`- oder `order`-Zeile aus der Ansicht, dann übernimmt wieder das Layout die Platzierung.

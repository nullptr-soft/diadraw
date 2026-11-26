# Größe ändern und drehen

Auf der Zeichenfläche kannst du Kästen in jeder Ansicht unabhängig vom Layout vergrößern, verkleinern und drehen. Die Änderung wird als Textzeile in die Ansicht geschrieben und gilt nur dort: Dasselbe Element kann in einer Detailansicht groß und in einer Übersicht klein sein.

```
view checkout of shop {
  include *
  size web 220, 90
  rotate db -15
}
```

## Größe ändern

Wähle einen einzelnen Kasten aus. An seinen Ecken erscheinen kleine quadratische Griffe. Ziehe an einem: Die gegenüberliegende Ecke bleibt stehen und über dem Kasten erscheint die neue Größe. Kästen werden nicht kleiner als 10 × 10.

Halte beim Ziehen die **Umschalttaste** gedrückt, um die Proportionen beizubehalten: Breite und Höhe wachsen oder schrumpfen gemeinsam, entsprechend der stärkeren Bewegung des Zeigers. Da Größen auf ganze Zahlen gerundet werden, bleibt das Seitenverhältnis bis auf eine Einheit erhalten. Bei eingeblendetem Raster rastet der Kasten mit Umschalttaste nicht ein, da Rasterschritte das Seitenverhältnis selten wahren.

- In einer **automatisch angeordneten** Ansicht (`layered`, `tree`, `grid`, `radial`, `force`) entsteht beim Loslassen `size web 220, 90`. Das Layout behält die Größe bei und ordnet alles andere darum herum an. Der Kasten kann sich also verschieben, wenn das Layout Platz schafft.
- In einer **Sequenzansicht** gilt die Größe für den Kopf des Teilnehmers.
- In einer **festen** Ansicht steht die Größe wie bisher in der `pin … at … size`-Zeile des Kastens (siehe [Von Hand in festen Ansichten zeichnen](fixed-views.md)).
- Container (Kästen mit weiteren Kästen darin) werden vom Layout an ihre Kindelemente angepasst. Nur in festen Ansichten lässt sich ihre Größe ändern.

## Einen Kasten an seinen Inhalt anpassen

Um eine Größenänderung zurückzunehmen und dem Kasten wieder die für seinen Namen und seine Form nötige Größe zu geben, **doppelklicke auf einen seiner Eckgriffe** oder wähle **Size to Fit Content**: auf dem Mac (⌥⌘=) und unter Windows im Menü Edit, in der Linux-App im Hauptmenü (Strg+Alt+=). In der Web-App ist es die Schaltfläche **Fit** in der Werkzeugleiste (⌥⌘= auf dem Mac, sonst Strg+Alt+=). Der Befehl wirkt auf alle ausgewählten Kästen zugleich und lässt sich in einem Schritt rückgängig machen.

Beim Anpassen wird die Größe aus dem Text entfernt, statt eine neue zu schreiben. Der Kasten folgt fortan seinem Inhalt: Benennst du ihn um, wächst oder schrumpft er mit.

- In einer automatisch angeordneten Ansicht und in einer Sequenzansicht entfällt die Zeile `size web …`.
- In einer festen Ansicht bleibt der Kasten an seinem Platz: Aus `pin web at 40, 60 size 220, 90` wird `pin web at 40, 60`.
- Bei einem Container in einer festen Ansicht entfällt die gesamte `pin`-Zeile, selbst wenn sie keine Größe enthält. Der Container umschließt wieder seine Kindelemente.
- Hat ein Kasten keine eigene Größenangabe, passt er bereits und bleibt unverändert.

## Drehen

Ein ausgewählter Kasten hat knapp außerhalb der rechten oberen Ecke auch einen runden Griff. Ziehe ihn um den Kasten: Dieser dreht sich so weit, wie der Zeiger um seinen Mittelpunkt wandert. Liegt der Winkel höchstens 5° neben einem Vielfachen von 15°, rastet er dort ein, andernfalls bei ganzen Grad. Der Winkel erscheint beim Ziehen über dem Kasten. Beim Loslassen entsteht `rotate db -15` (Grad im Uhrzeigersinn; negative Werte gegen den Uhrzeigersinn). Drehst du den Kasten wieder aufrecht, entfällt die Zeile.

Die Beschriftung dreht sich mit. Linien schließen an den gedrehten Umriss an, und automatische Layouts schaffen Platz für die gedrehte Form. Ein hochkant gedrehter langer Kasten beansprucht also den Platz eines hohen Kastens.

Container und Sequenzteilnehmer werden nicht gedreht. Ein `rotate` für einen solchen Kasten erscheint als nicht erfüllbarer Layout-Hinweis.

## Was sich im Text ändert

- Ein vorhandenes `size` oder `rotate` wird **an Ort und Stelle** überschrieben. Nachgestellte Kommentare und die Schreibweise des Namens bleiben erhalten. Neue Zeilen folgen auf die übrigen Zeilen der Ansicht.
- Beim Verschieben eines Kastens ändern sich `size` und `rotate` nie.
- Größen werden auf ganze Zahlen gerundet, Drehwinkel auf ein Zehntelgrad zwischen -180 und 180.

## Verfügbarkeit

Die Apps für macOS, Linux (GTK) und Windows sowie die Web-App bieten die Griffe zum Skalieren und Drehen in jeder Ansicht.

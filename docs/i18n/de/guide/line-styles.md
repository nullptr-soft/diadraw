# Linienenden und Linienstile

Der Pfeil einer Beziehung (`->`, `-->`, `<->`) bestimmt ihr übliches Aussehen: eine durchgezogene oder gestrichelte Linie mit gefüllter Pfeilspitze. In Notationen wie UML, einem Entity-Relationship-Modell oder einem Datenflussdiagramm haben die Enden und die Linie selbst eine Bedeutung. Mit einem Beziehungsstil wählst du sie für eine einzelne Linie oder für alle Linien mit einem bestimmten Tag.

## Auswahl in der App

Wähle eine Linie aus. Der Abschnitt **Style** im Eigenschaftenbereich bietet folgende Einstellungen:

- **Start** und **End** wählen die Enden aus. Unter macOS und Linux zeigt jeder Eintrag ein kleines Bild der Linie. Unter macOS und im Web sind die Einträge nach Pfeilen, Formen, Linien und Krähenfüßen gruppiert.
- **Filled start** und **Filled end**: Ohne Häkchen bleiben Dreieck, Halbpfeil, Raute, Kreis oder Quadrat ungefüllt.
- **Line style** wählt, wie die Linie gezeichnet wird: durchgezogen, gestrichelt, gepunktet, mit kurzen oder langen Strichen, Strich-Punkt oder Strich-Punkt-Punkt. Unter macOS und Linux zeigt jeder Eintrag ein kleines Bild der Linie.
- **Double line** zeichnet zwei dünne Linien statt einer; **Line width** macht die Linie dicker.
- **Routing** führt die Linie rechtwinklig, **Curved** zeichnet glatte Kurven durch ihre Knickpunkte, und **Rounded corners** rundet ihre Ecken ab.

Jede Einstellung schreibt einen Schlüssel in `style "…"` der Beziehung. Der übrige Text bleibt unverändert. Setzt du die Einstellung auf den Standardwert zurück, entfällt der Schlüssel wieder.

## Angabe im Text

Dieselben Schlüssel stehen in `style "key=value;…"` hinter der Beziehung:

```
model {
  customer = process "Customer"
  order = process "Order"
  customer -> order "places" style "startArrow=ERmandOne;endArrow=ERzeroToMany"
  order -> customer "notifies" style "dashed=1;dashPattern=1 3;endArrow=open;curved=1"
}
```

Für alle Linien mit einem Tag verwende stattdessen einen Stilblock (siehe [Stile und Designs](styles-and-themes.md)):

```
style relationship tag "async" { dashed true dashPattern "1 3" endArrow open }
```

### Enden

`startArrow` und `endArrow` akzeptieren die folgenden Werte. Es sind die Namen von draw.io, damit importierte Diagramme ihre Linienenden behalten.

| Wert | Darstellung | Typische Verwendung |
| --- | --- | --- |
| `classic` | Pfeil | Aufruf oder Abhängigkeit (Standardende) |
| `block` | Dreieck | UML-Vererbung (ungefüllt, `endFill=0`) |
| `open` | offener Pfeil | UML-Abhängigkeit, Nachricht |
| `async`, `openAsync` | Halbpfeil, offener Halbpfeil | asynchrone Nachricht |
| `doubleBlock` | Doppeldreieck | schneller oder priorisierter Fluss |
| `diamond` | Raute | UML-Komposition (gefüllt), Aggregation (ungefüllt) |
| `oval` | Kreis | Verbindungspunkt |
| `box` | Quadrat | Port oder Qualifizierer |
| `circlePlus` | Kreis mit Plus | UML-Einschluss |
| `halfCircle` | Buchse | benötigte Schnittstelle |
| `baseDash`, `dash`, `cross` | Balken, Schrägstrich, Kreuz | Stopp, Unterbrechung, verbotener Fluss |
| `ERone`, `ERmandOne`, `ERzeroToOne` | Krähenfußnotation: eins | eins, genau eins, null oder eins |
| `ERmany`, `ERoneToMany`, `ERzeroToMany` | Krähenfußnotation: viele | viele, eins oder viele, null oder viele |
| `none` | nichts | ungerichtete Linie |

Ohne `startArrow` hat eine Linie keine Markierung am Anfang, außer sie ist mit `<->` geschrieben. Ohne `endArrow` endet sie mit einem Pfeil. `startFill=0` und `endFill=0` lassen die Formen ungefüllt.

### Die Linie

| Schlüssel | Wirkung |
| --- | --- |
| `dashed=1` | strichelt die Linie (wie `-->`) |
| `dashPattern=…` | Strichmuster: `1 3` gepunktet, `3 3` kurz, `12 6` lang, `8 3 1 3` Strich-Punkt |
| `double=1` | zwei Linien mit dem Hintergrund dazwischen |
| `strokeWidth=…` | Breite, standardmäßig 1,25 |
| `stroke=#…` | Farbe |
| `edgeStyle=orthogonalEdgeStyle` | rechtwinklige Linienführung |
| `curved=1` | glatte Kurven durch die Knickpunkte |
| `rounded=1` | abgerundete Ecken |

Ein `dashPattern` erscheint nur auf einer gestrichelten Linie. Die Einstellung „Line style“ in der App setzt beides.

## Gut zu wissen

- Eine gerade Linie hat keine Ecken, deshalb ändern `curved` und `rounded` dort nichts. Sie werden sichtbar, sobald die Linie abknickt: in einem Layout, das sie um Kästen führt, bei rechtwinkliger Führung oder entlang der Knickpunkte einer `route`.
- Die Lücke in einer Doppellinie hat die Hintergrundfarbe der Seite. Überquert sie eine gefüllte Gruppe oder einen Container, bleibt zwischen den Linien die Seitenfarbe sichtbar.
- Beim Export nach draw.io werden die Enden, `dashPattern`, `curved` und `rounded` übernommen, `double` jedoch nicht: draw.io zeichnet Doppellinien als einen anderen Kantentyp.
- Beim Import aus draw.io werden die Enden übernommen, Kurven, abgerundete Ecken und Strichmuster einer Kante jedoch noch nicht. Importierte Seiten sehen also so aus wie vor Einführung dieser Funktion.

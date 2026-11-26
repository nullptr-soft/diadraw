# Beziehungen

Eine Beziehung ist eine Linie zwischen zwei Elementen: ein Aufruf, ein Datenfluss oder eine Abhängigkeit. Wie Elemente werden Beziehungen einmal im `model`-Block deklariert. Jede Ansicht, die beide Enden zeigt, zeichnet sie.

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

## Pfeile

| Pfeil | Darstellung | Typische Verwendung |
| --- | --- | --- |
| `->` | durchgezogene Linie mit einer Pfeilspitze | Aufruf, Anfrage, Abhängigkeit |
| `-->` | gestrichelte Linie | asynchroner oder indirekter Fluss |
| `<->` | Pfeilspitzen an beiden Enden | wechselseitiger Austausch |

Nach dem Ziel können eine Beschriftung und wie bei Elementen `tech`, `tag`, `style` und `prop` folgen. Auf der Linie zeigt das Diagramm die Beschriftung und die Technologie an.

## Linienenden und Linienstile

Der Pfeil bestimmt das übliche Aussehen einer Linie. Für andere Darstellungen – etwa einen offenen Pfeil, eine Raute, Krähenfußenden, eine gepunktete oder doppelte Linie oder eine Kurve – gib der Beziehung einen Stil. Siehe [Linienenden und Linienstile](line-styles.md).

## Endpunkte und Ports

Ein Endpunkt ist ein Element. Schreibe es so, wie du es vom Deklarationsort der Beziehung aus ansprichst: `web` innerhalb von `shop { … }`, `shop.web` überall. Deklariert das Element einen Port (`api = container { port http north }`), schließt `api.http` die Linie an diesen Port an.

## Eine Linie neu verbinden

Um eine Linie auf ein anderes Element zeigen zu lassen, wähle sie per Klick aus. An jedem Ende erscheint ein kleiner Kreis. Ziehe einen davon auf ein anderes Element und lasse los. Beim Ziehen wird das Element unter dem Zeiger hervorgehoben. Lässt du im freien Raum oder über einem bereits von der Linie berührten Element los, ändert sich nichts.

Der Text ändert sich nur an der Stelle, an der dieses Ende steht:

```
web -> legacy "REST" tech "HTTPS"  // being migrated
```

wird nach dem Ziehen der Pfeilspitze von Legacy API auf API zu

```
web -> api "REST" tech "HTTPS"  // being migrated
```

Beschriftung, `tech`, Tags, Kommentare und Pfeil bleiben unverändert. Das neue Ende erhält den kürzesten Namen, der das Element vom Deklarationsort der Linie aus erreicht: `api` innerhalb von `shop { … }`, `shop.api` außerhalb. Ist es ausnahmsweise von dort aus unter keinem Namen erreichbar (etwa weil innerhalb von `shop` das Element `shop.db` ein übergeordnetes `db` auf oberster Ebene verdeckt), wird die Datei stattdessen neu formatiert. Die Statuszeile weist darauf hin.

Gut zu wissen:

- **Nur Elemente, keine Ports.** Setzt du ein Ende auf einem Element ab, wird es direkt mit dem Element verbunden. Für eine Verbindung zu einem Port (`api.http`) bearbeite den Text. Setzt du ein schon an einem Port liegendes Ende wieder auf demselben Element ab, bleibt der Port erhalten.
- **Auch Container sind Ziele.** Setzt du das Ende auf dem Rahmen eines Containers ab, abseits seiner inneren Elemente, wird die Linie mit dem Container verbunden.
- **Keine Selbstschleifen.** Du kannst ein Ende nicht auf dem Element absetzen, an dem das andere Ende liegt.
- **Von einer Ansicht erzeugte Linien lassen sich nicht neu verbinden.** Blendet eine Ansicht ein Element aus, zeichnet sie dessen Linien zum nächsten sichtbaren Vorfahren (implizite Beziehung). Diese Linien stehen so nicht im Text und haben daher keine Griffe. Verbinde die eigentliche Linie in einer Ansicht neu, die beide Enden zeigt, oder bearbeite den Text.
- **Rückgängig** (⌘Z oder Strg+Z) setzt das Ende im Text und auf der Zeichenfläche zurück, wie bei jeder anderen Änderung auf der Zeichenfläche.

Was mit der ID der Linie und den Verweisen darauf passiert, hängt davon ab, ob sie einen Namen hat. Der nächste Abschnitt erklärt die IDs, danach folgt, [wie sich das Neuverbinden auf IDs auswirkt](#wie-sich-das-neuverbinden-auf-ids-auswirkt).

## Beziehungs-IDs

Jede Beziehung hat eine ID. Sie erscheint nicht im Diagramm, aber andere Teile von DiaDraw verwenden sie, um eine bestimmte Linie anzusprechen:

- **Manuelle Linienführung.** Wenn du [Knickpunkte einer Linie ziehst](fixed-views.md#linien-knicken) oder ein draw.io-Diagramm importierst, wird die Linienform in der Ansicht als `route "<id>" via …` gespeichert.
- **Änderungen auf der Zeichenfläche.** Auswahl, Umbeschriften und Löschen einer Linie erfolgen über ihre ID. Dasselbe gilt für die kleinen Textänderungen, die die Zeichenfläche in deine Datei zurückschreibt.
- **Exportiertes SVG.** Jede Linie wird als `<g class="dd-edge" data-id="<id>">` gezeichnet. So kann eine Webseite, ein Test oder ein Stylesheet sie finden, etwa um `[data-id="checkout"]` hervorzuheben.
- **Reviews.** Mit stabiler ID zeigt ein Diff „diese Linie führt jetzt woandershin“, statt eine entfernte und eine hinzugefügte Linie zu zeigen.

### Abgeleitete IDs

Ohne Namen erhält eine Beziehung ihre ID aus ihren Endpunkten: `web -> db` wird zu `web->db`, `web -> api.http` zu `web->api.http`. Eine zweite Linie zwischen denselben Elementen erhält `web->db#2`, die nächste `#3` und so weiter.

Das ist praktisch, aber die ID folgt den Endpunkten. Sie ändert sich, wenn du:

- **die Linie neu verbindest**: Aus `web -> legacy` wird `web -> api`, also ändert sich die ID von `web->legacy` in `web->api`;
- **eine frühere parallele Linie löschst**: Bei `order -> billing` und einem zweiten `order -> billing` wird nach dem Löschen der ersten Linie die zweite von `order->billing#2` in `order->billing` umnummeriert.

Änderst du die Endpunkte im Text, funktionieren Verweise auf die alte ID nicht mehr. Eine `route`, die sie nennt, wird als `Unknown relationship` gemeldet, und die von Hand gesetzten Knickpunkte gehen verloren. Beim Neuverbinden auf der Zeichenfläche erledigt DiaDraw die Anpassung für dich (siehe unten).

### Benannte Beziehungen

Stelle der Beziehung einen Namen und `=` voran, um ihr eine eigene ID zu geben:

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

Jetzt lautet die ID dauerhaft `calls`. Änderst du die Linie zu `calls = web -> api`, gilt die Route weiterhin. Du kannst eine Linie auch auf der Zeichenfläche benennen: Wähle sie aus und trage den Namen ins Feld „Name“ des Inspektors ein. Er aktualisiert ihre Routen für dich (siehe [Eine Beziehung im Inspektor benennen](element-properties.md#eine-beziehung-im-inspektor-benennen)). Benannte und unbenannte Linien können nebeneinander stehen:

```
order -> billing "charge"
refund = order -> billing "refund" tag "async"
order -> billing "audit"
```

Ihre IDs sind `order->billing`, `refund` und `order->billing#2`. Der Name einer Linie nummeriert die anderen nicht um.

Regeln für Namen:

- Ein Name ist ein Bezeichner (Buchstaben, Ziffern und `_`, aber keine Ziffer am Anfang).
- Er gilt genau wie geschrieben, auch innerhalb eines Elements. `writes = web -> db` in `shop { … }` hat die ID `writes`, nicht `shop.writes`.
- Er muss unter den Beziehungen eindeutig sein; ein zweites `x = …` ist ein Fehler. Er darf mit dem Namen eines Elements übereinstimmen, da Elemente und Beziehungen getrennte IDs haben.
- Auch Wörter wie `group`, `port` oder `layout` sind gültige Namen. Am Pfeil erkennt DiaDraw, dass die Zeile eine Beziehung ist.

### Wann sich ein Name lohnt

Benenne eine Beziehung, wenn außerhalb der Linie etwas von ihr abhängt:

- du hast ihre Route von Hand gezeichnet oder hast es vor;
- sie ist eine von mehreren Linien zwischen denselben Elementen und du möchtest diese vielleicht löschen oder umordnen;
- du wirst sie voraussichtlich im Laufe des Entwurfs neu verbinden, etwa bei einer Migration von einem Dienst zu einem anderen;
- eine Seite, ein Test oder ein Stylesheet verweist über das exportierte SVG auf sie.

Trifft nichts davon zu, lass sie unbenannt: Die abgeleitete ID genügt und der Text bleibt kürzer.

### Wie sich das Neuverbinden auf IDs auswirkt

Wenn du eine Linie auf der Zeichenfläche neu verbindest:

- **Eine benannte Linie behält ihren Namen.** Aus `calls = web -> legacy` wird `calls = web -> api`, und `route "calls" …` gilt weiterhin.
- **Eine unbenannte Linie erhält die ID ihrer neuen Endpunkte.** Aus `web->legacy` wird `web->api`. Jede `route` mit der alten ID wird auf die neue umgeschrieben, sodass die Knickpunkte bei der Linie bleiben.
- **Parallele Linien werden umnummeriert und ihre Routen angepasst.** Gibt es zwei unbenannte `order -> billing`-Linien und du verbindest die erste neu mit `ledger`, wird die zweite von `order->billing#2` zu `order->billing`. Die Route der ersten Linie wird auf `order->ledger` umgeschrieben, die der zweiten auf `order->billing`. Keine Route landet auf der jeweils anderen Linie. Umgekehrt gilt dasselbe: Verschiebst du eine Linie auf ein Elementpaar, das schon durch eine später deklarierte Linie verbunden ist, erhält diese spätere Linie `#2`.
- **Die Linie bleibt ausgewählt**, jetzt unter ihrer neuen ID.

Beim Neuverbinden behält eine Route ihre Knickpunkte. Weil sie für die alten Enden gezeichnet wurden, kann die Linie jetzt einen Umweg machen. Verschiebe die Knickpunkte erneut oder lösche die `route`-Zeile aus der Ansicht, damit das Layout die Linie führt.

Nur Routen verweisen im Text auf eine unbenannte Linie; deshalb werden nur sie umgeschrieben. Eine Seite, ein Test oder ein Stylesheet, das die Linie im exportierten SVG anhand von `data-id` findet, liegt außerhalb der Datei und wird nicht aktualisiert. Benenne die Linie, wenn etwas auf diese Weise von ihr abhängt.

### IDs, die keine gültigen Namen sind

Ein Modell kann auch aus JSON geladen oder von einem Programm erstellt werden. Seine Beziehungs-IDs sind unter Umständen keine gültigen Namen (zum Beispiel `edge-1`). Wird ein solches Modell als Text ausgegeben, erscheinen diese Linien ohne Namen. Beim erneuten Lesen des Textes erhalten sie deshalb abgeleitete IDs.

## Siehe auch

- [DSL-Referenz: Semantik](../dsl.md#semantik) für die genauen Regeln.
- [Freiformdiagramme](../dsl.md#freiformdiagramme) für `route` und die übrigen Layout-Hinweise.
- [Von Hand in festen Ansichten zeichnen](fixed-views.md) zum Knicken von Linien auf der Zeichenfläche.

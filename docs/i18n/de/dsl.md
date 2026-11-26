# Die DSL von DiaDraw

Eine Grammatik für alle Diagrammtypen: Ein `model`-Block im Stil von Structurizr definiert die Elemente einmalig; `view`-Blöcke wählen sie aus, ordnen sie an und geben Bedingungen für ihre Platzierung vor. Dateien haben die Endung `.draw` und sind die maßgebliche Quelle. So lassen sich Änderungen an Diagrammen in Git leicht vergleichen und begutachten.

Die Grammatik ist in [`diadraw.grammar`](diadraw.grammar) in Lezer-Notation als Referenz festgehalten; sie wird nicht kompiliert. Der Parser des Kerns (`src/dsl/src/syntax.cpp`) ist ein handgeschriebener, fehlertoleranter Parser mit rekursivem Abstieg. Er erzeugt den von der Grammatik beschriebenen Baum mit denselben Knotennamen. `parse(source)` (`src/dsl/src/lower.cpp`) übersetzt den Syntaxbaum in Modelloperationen und liefert das Modell, Diagnosen mit Zeile und Spalte sowie eine Zuordnung zu den Quelltextstellen. `print(model)` schreibt kanonischen Text. Bei jeder Datei des Korpus ergibt die Ausgabe mit anschließendem erneuten Parsen dasselbe Modell.

Dies ist die Referenz. Wozu die Funktionen dienen und wann du sie einsetzt, erklärt das [Benutzerhandbuch](guide/README.md).

## Beispiel

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web    // written by a canvas drag
}
```

## Freiformdiagramme

Bei von Hand gezeichneten oder aus draw.io importierten Diagrammen steht die Geometrie im Text:

```
model {
  a = process "Billing" style "fill=#dae8fc;stroke=#6c8ebf;rounded=1" prop "owner" "Ann"
  b = decision "Paid?" shape rhombus
  a -> b "check" style "endArrow=open;dashed=1;edgeStyle=orthogonalEdgeStyle"
}

view page1 {
  include *
  layout fixed
  pin a at 40, 40 size 120, 60
  pin b at 240, 30 size 100, 80
  route "a->b" via 200, 70 200, 70 from 1, 0.5 to 0, 0.5
}
```

- `style "k=v;k=v"` legt das Aussehen eines Elements oder einer Beziehung fest. Elementschlüssel: `fill`, `stroke`, `strokeWidth`, `dashed`, `dashPattern`, `rounded`, `opacity` (0–100), `fontColor`, `fontSize`, `fontStyle` (fett 1, kursiv 2, unterstrichen 4), `align`, `verticalAlign`, `labelPosition`, `verticalLabelPosition`. Beziehungsschlüssel: `stroke`, `strokeWidth`, `dashed`, `dashPattern` (mit `dashed=1`: `1 3` gepunktet, `8 3 1 3` Strich-Punkt, …), `double` (zwei Linien), `curved`, `rounded` (abgerundete Ecken), `edgeStyle`, `startArrow`, `endArrow`, `startFill`, `endFill`. Die Enden verwenden die Namen von draw.io: `classic`, `block`, `open`, `async`, `openAsync`, `doubleBlock`, `diamond`, `oval`, `box`, `circlePlus`, `halfCircle`, `baseDash`, `dash`, `cross`, `none` sowie die Krähenfußenden `ERone`, `ERmandOne`, `ERzeroToOne`, `ERmany`, `ERoneToMany`, `ERzeroToMany`. `startFill=0`/`endFill=0` lässt eine Form ungefüllt. Ein Element mit Stil wird wie in draw.io gezeichnet (standardmäßig weiße Füllung, schwarze Kontur), ein Container mit Stil als eigene Form statt als gestrichelte Begrenzung. Unbekannte Schlüssel bleiben erhalten.
- `prop "key" "value"` speichert eine beliebige Eigenschaft (wiederholbar).
- `shape` akzeptiert eine Grundform: `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible frame module lifeline`.
- `layout fixed` übernimmt jedes `pin … at X, Y size W, H` exakt (nicht fixierte Knoten reihen sich darunter auf). `route "relationship id" via X, Y … from EX, EY to NX, NY` legt die Knickpunkte einer Kante fest sowie Austritts- und Eintrittspunkt an Quelle und Ziel (relativ zum Kasten, 0–1). Ohne `from`/`to` verlässt die Kante die Quelle in Richtung ihres ersten Knickpunkts. Verschiebst oder skalierst du einen Knoten auf der Zeichenfläche einer festen Ansicht, wird sein `pin … at … size` angepasst; beim Ziehen von Knickpunkten wird seine `route` angepasst (siehe [Von Hand in festen Ansichten zeichnen](guide/fixed-views.md)).

## Stile und Designs

`theme` wählt die Farben des Dokuments. `style`-Blöcke geben allen Elementen einer Art, allen Objekten mit einem Tag oder allen Beziehungen ein gemeinsames Aussehen:

```
theme dark

style container {
  fill "#438dd5"
  stroke "#3c7fc0"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true endArrow open }

model { … }

view checkout of shop {
  include *
  theme default        // this view only
}
```

- Designs: `default`, `dark` (dunkler Hintergrund) und `mono` (Schwarz auf Weiß mit grauen Füllungen für den Druck). Das `theme` einer Ansicht hat Vorrang vor dem des Dokuments. Bei unbekanntem Namen erscheint eine Warnung und die Ansicht wird mit `default` gezeichnet. Ohne `theme`-Anweisung verwendet jede Ansicht wie bisher `default`.
- Selektoren: `*` (alle Elemente), eine Art (`container`), `tag "…"`, Art und Tag (`container tag "legacy"`), `relationship` und `relationship tag "…"`. Im Selektor sind `relationship` und `tag` Schlüsselwörter. Für gleichnamige Arten gibt es deshalb keinen Selektor.
- Jede Blockzeile hat die Form `key value`. Die Schlüssel entsprechen denen des Inline-Stils `style "…"` (siehe oben; unbekannte Schlüssel bleiben erhalten). Ein Wert ist eine Zeichenkette, eine Zahl oder ein Wort. `true` und `false` werden wie bei draw.io als `1` und `0` gespeichert.
- Rangfolge, beginnend mit der niedrigsten Priorität: Design, `*`-Blöcke, Art-Blöcke, Tag-Blöcke, kombinierte Art-und-Tag-Blöcke (innerhalb einer Stufe gewinnt der spätere Block), schließlich der eigene Inline-Stil `style "…"` des Objekts.
- Ein `style`-Block ändert nur das, was er setzt: Das Element behält die übrigen Designfarben und seine fette Beschriftung. Ein Inline-Stil `style "…"` verwendet für ausgelassene Angaben weiterhin das Aussehen von draw.io (weiße Füllung, schwarze Kontur). Blöcke für eine Art mit Kindelementen gestalten deren Begrenzung (`fill`, `stroke`, `strokeWidth`, `dashed`, `fontColor`).
- `style` und `theme` sind Anweisungen der obersten Ebene; `theme` darf auch in einer Ansicht stehen. In `model { … }` bleiben sie gewöhnliche Namen. `style "…"` an einem Element ist der Inline-Stil.
- `print` schreibt zuerst die Designdeklarationen, dann das gewählte Design und schließlich die Blöcke in ihrer Reihenfolge, mit einem `key value` pro Zeile.

### Eigene Designs

Ein Dokument deklariert ein Design mit einem Block und wählt es wie ein eingebautes Design aus:

```
theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"
  fontFamily "IBM Plex Sans, sans-serif"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
  node external { fill "#444444" }
}

theme brand
```

- `extends` nennt ein eingebautes, ein anderes deklariertes oder ein Bibliotheksdesign. Ohne diese Angabe geht das Design von `default` aus. Jedes Design ändert nur die von ihm gesetzten Werte.
- Designschlüssel: `background`, `fontFamily`, `fontSize`, `smallFontSize`, `edge` und `edgeText` (Beziehungslinien und ihre Beschriftungen), `boundary` und `boundaryText` (Container- und Gruppenrahmen), `surface` (Aktivierungen, ungefüllte Pfeilspitzen), `dividerFill`, `labelText` (Beschriftungen unter Symbolen).
- `node <kind> { fill … stroke … text … }` färbt eine Art ein. `node default` gilt für jede Art ohne eigenen Eintrag, `node external` für Elemente mit dem Tag `external`.
- Auch Bibliotheken liefern Designs (siehe [Bibliotheken](libraries.md#das-manifest)): Mit `use "acme-brand@^1" as acme` wählt `theme acme.corporate` eines davon, und `theme mine extends acme.corporate { … }` baut darauf auf.
- Unbekannte Schlüssel und `extends`-Ziele, doppelte Namen, Namen eingebauter Designs und `extends`-Zyklen werden gemeldet. Ein nicht auflösbares Design wird als `default` gezeichnet.

## Sequenzansichten

Eine Ansicht mit `layout sequence` zeichnet die aufgenommenen Elemente in `include`-Reihenfolge als Lebenslinien und die Schritte ihres `sequence`-Blocks von oben nach unten:

```
view login {
  include user web api
  layout sequence
  sequence {
    autonumber
    user -> web "Log in"          // ->  call, ->> async, --> reply
    activate web
    alt "valid" {
      web --> user "Welcome"
      note over web api "token issued"
    } else "invalid" {
      web --> user "Error"
      note right of web "logged"
    }
    deactivate web
    divider "Later"
    delay "5 minutes"
    ref over user web "Logout"
    loop "every 30 s" { web ->> api "heartbeat" }
  }
}
```

Rahmen sind `alt`, `opt`, `loop`, `par`, `critical`, `break` und `group`, jeweils mit optionalen `else "label" { … }`-Abschnitten. Nachrichten gehören zur Ansicht, nicht zum Modell. Dieselben Teilnehmer können in vielen Sequenzansichten erscheinen, und die Beziehungen des Modells bleiben eine Zusammenfassung.

## Notizen und Dekorationen

Notizen, freier Text und Rahmen gehören zu einer Ansicht, nicht zum Modell. Sie erläutern ein Diagramm, ohne Elemente hinzuzufügen. Andere Ansichten desselben Modells bleiben dadurch aufgeräumt.

```
view checkout of shop {
  include *
  layout layered direction right
  text "Checkout, container level" style "fontSize=18;fontStyle=1"
  note consistency "Orders are eventually consistent" below db
  note "Owned by team Web" left of web
  box pci "PCI scope" around api db
  note "Check the cache TTL"
  note "Draft" at 40, -80 size 160, 60
}
```

| Objekt | Darstellung und Platzierung |
| --- | --- |
| `note [name] "text" [anchor] [style "…"]` | Haftnotiz; an ein Element angeheftet erhält sie eine gestrichelte Verbindungslinie |
| `text [name] "text" [anchor] [style "…"]` | freier Text, Titel oder Bildunterschrift |
| `box [name] ["label"] around a b … [style "…"]` | gestrichelter, abgerundeter Rahmen um die Kästen der Elemente, Beschriftung oben links |
| `box [name] ["label"] at X, Y size W, H [style "…"]` | Rahmen an festen Koordinaten |

Die Verankerung erfolgt `below`, `above`, `left of` oder `right of` relativ zu einem Element der Ansicht oder mit `at X, Y` und optionalem `size W, H`. Ohne Anker werden Texte über dem Diagramm gestapelt, Notizen in einer Spalte rechts daneben. Dekorationen werden nach dem Layout platziert und verschieben daher nie Elemente. Würde eine Dekoration einen Knoten, eine Kantenbeschriftung oder eine andere Notiz verdecken, rückt sie weiter von ihrem Bezugselement ab. Notizen brechen bei 200 px um.

- Der Name ist optional und muss innerhalb der Ansicht eindeutig sein. Über ihn können Bearbeitungsvorgänge und Hosts das Objekt ansprechen (im SVG steht er als `data-name`; jede Dekoration hat `data-decoration="<index>"` und die Klasse `dd-decoration dd-note|dd-text|dd-box`).
- `style` akzeptiert die Elementschlüssel `fill`, `stroke`, `strokeWidth`, `dashed` (Rahmen sind gestrichelt, außer bei `dashed=0`), `rounded`, `opacity`, `fontColor`, `fontSize`, `fontStyle`, `align`, `verticalAlign`.
- Wird ein Element entfernt, entfallen auch Dekorationen, die daran befestigt sind oder es einrahmen.
- `note`, `text`, `box` und `around` sind kontextabhängige Schlüsselwörter: Elemente dürfen weiterhin diese Namen tragen. In `sequence { … }` behalten `note over …` / `note left of …` ihre Bedeutung für Sequenzdiagramme.

## Lexikalische Regeln

- Leerraum ist nicht bedeutungstragend; Anweisungen benötigen keine Trennzeichen.
- Kommentare: `// to end of line` und `/* block */`. Beide bleiben im Syntaxbaum erhalten.
- Bezeichner: `[A-Za-z_][A-Za-z0-9_]*`. Verschachtelte Elemente werden mit Punkten angesprochen: `shop.web`.
- Zeichenketten: einzeilig, in doppelten Anführungszeichen, mit `\` als Escape-Zeichen (`\n` ist ein Zeilenumbruch). Ein **Blockstring** erstreckt sich über mehrere Zeilen: Er beginnt mit `"""` und endet beim nächsten `"""`, mit denselben Escapes. Der Rest der Zeile nach dem öffnenden `"""` und die Zeile des schließenden `"""` entfallen, wenn sie leer sind, ebenso die Einrückung, die alle übrigen Zeilen gemeinsam haben. So lässt sich der Text mit der umgebenden Anweisung einrücken. Leerzeilen darin bleiben erhalten:

  ```
  db = container "Orders DB" description """
    Orders and their lines.

    Archived after two years.
  """
  ```

  Blockstrings sind überall erlaubt, wo eine Zeichenkette steht. `format` schreibt eine mehrzeilige `description` als Blockstring und jede andere Zeichenkette mit `\n`.
- Zahlen: `-?digits(.digits)?`.
- Schlüsselwörter sind kontextabhängig: `use as model view of group port north south east west tech shape tag
description style prop include exclude layout direction spacing right left down up pin below
above at size route via from to align horizontally vertically order sequence activate
deactivate note over divider delay ref autonumber alt opt loop par critical break else theme relationship extends node
text box around` sind Schlüsselwörter, wo die Grammatik eines erwartet, und sonst gewöhnliche Namen (`layout = container "Layout"` ist gültig). Sind beide Lesarten möglich, etwa in einer Selektorliste gefolgt von `layout …`, hat die Lesart als Schlüsselwort Vorrang.

## Grammatik (informell)

```
Document     := (Use | ThemeDef | Theme | StyleBlock | ModelBlock | ViewBlock)*

Use          := "use" String ("as" Name)?          // see docs/libraries.md

Theme        := "theme" QName
ThemeDef     := "theme" Name ("extends" QName)? "{" (Name Value | "node" Name "{" (Name Value)* "}")* "}"
StyleBlock   := "style" StyleSelector "{" (Name (String | Number | Ident))* "}"
StyleSelector:= ("*" | "relationship" | Kind) ("tag" String)? | "tag" String

ModelBlock   := "model" ModelBody
ModelBody    := "{" ModelItem* "}"
ModelItem    := ElementDecl | Relationship | Group | Port

ElementDecl  := Name "=" Kind Label? Property* ModelBody?
Kind         := Ident | Alias "." Ident               // built in, or a library's kind
Relationship := (Name "=")? QName Arrow QName Label? Property*
Arrow        := "->" (solid) | "-->" (dashed) | "<->" (bidirectional)
Group        := "group" Label ModelBody
Port         := "port" Name ("north" | "south" | "east" | "west")?
Property     := "tech" String
              | "shape" QName                      // a base shape, or alias.shape from a library
              | "tag" String
              | "description" String
              | "style" String                     // "key=value;key=value"
              | "prop" String String               // repeatable

ViewBlock    := "view" Name ("of" QName)? "{" ViewItem* "}"
ViewItem     := "include" Selector+
              | "exclude" Selector+
              | "layout" Algorithm ("direction" Dir | "spacing" Number)*
              | "pin" QName (Placement QName | "at" Coord ("size" Coord)?)
              | "route" String ("via" Coord+)? ("from" Coord)? ("to" Coord)?
              | "size" QName Coord                 // width, height in this view
              | "rotate" QName Number              // degrees, clockwise
              | "align" QName QName+ ("horizontally" | "vertically")?
              | "order" QName QName+
              | "sequence" SeqBody
              | "theme" QName
              | ("note" | "text") Name? String Anchor? ("style" String)?
              | "box" Name? String? ("around" QName+ | "at" Coord "size" Coord)
                ("style" String)?
Selector     := "*" | QName
Algorithm    := Name                               // layered (default), tree, grid, radial, force, fixed, sequence
Dir          := "right" | "left" | "down" | "up"
Placement    := "below" | "above" | "left" "of" | "right" "of"
Coord        := Number "," Number
Anchor       := Placement QName | "at" Coord ("size" Coord)?

SeqBody      := "{" SeqItem* "}"
SeqItem      := QName MessageArrow QName Label?
              | ("activate" | "deactivate") QName
              | "note" ("over" QName+ | ("left" | "right") "of" QName) Label
              | "divider" Label?
              | "delay" Label?
              | "ref" "over" QName+ Label
              | "autonumber"
              | FrameKind Label? SeqBody ("else" Label? SeqBody)*
MessageArrow := "->" (call) | "->>" (async) | "-->" (reply)
FrameKind    := "alt" | "opt" | "loop" | "par" | "critical" | "break" | "group"

Label        := String
Kind         := Name
QName        := Name ("." Name)*
```

## Semantik

| Syntax | Modell |
| --- | --- |
| `web = container "Web app"` in `shop` | Element `shop.web`, Art `container`, Name `Web app`, Elternelement `shop` |
| keine Beschriftung | der Bezeichner dient als Name |
| `tech`, `shape`, `tag`, `description` | gleichnamige Felder des Elements bzw. der Beziehung |
| `style "k=v;…"`, `prop "k" "v"` | Stil und Eigenschaften des Elements oder der Beziehung |
| `a -> b "label"` | Beziehung mit ID `a->b` (`a->b#2`, `#3`, … bei Wiederholungen) |
| `pay = a -> b` | Beziehung mit expliziter ID `pay` |
| `a.http -> b` | hat `a` einen `port http`, ist dieser Port der Endpunkt |
| `group "Build" { … }` | eine Gruppe; bei den enthaltenen Elementen wird `group` gesetzt |
| `view v of shop` | Ansicht mit den Kindelementen von `shop` als Geltungsbereich |
| `include *` | jedes Element im Geltungsbereich; ohne `include` nimmt eine Ansicht `*` auf |
| `layout …` | Algorithmus, Richtung und Abstand der Ansicht (Standard `layered`) |
| `pin a below b` | `PinHint` mit Platzierung `Below` |
| `pin a at 10, 20 size 120, 60` | `PinAtHint` mit Position und optionaler Größe (explizite Freiformplatzierung) |
| `route "a->b" via 1, 2 from … to …` | `RouteHint`: Knickpunkte, Austritt und Eintritt (0–1 relativ zum Kasten) |
| `size a 200, 80` | `SizeHint`: Größe des Elements in dieser Ansicht, von jedem Layout beibehalten |
| `rotate a 30` | `RotateHint`: Drehung um den Mittelpunkt um 30° im Uhrzeigersinn (nur Blattelemente) |
| `align a b horizontally` | `AlignHint` (standardmäßig horizontale Achse) |
| `order a b c` | `OrderHint` |
| `sequence { … }` | Sequenzschritte der Ansicht; `autonumber` aktiviert an beliebiger Stelle die Nummerierung |
| `theme dark` | `model.theme` (in einer Ansicht: `view.theme`) |
| `theme brand extends dark { … }` | `model.themes[]`: `{ name, extends?, …keys, nodes? }` |
| `style container { fill "#438dd5" }` | `model.styles[]`: `{ target: 'elements', kind, tag?, style }` |
| `note n "x" right of a` | Dekoration `{ kind: 'note', name: 'n', placement: 'right-of' }` |
| `box "x" around a b` | Dekoration `{ kind: 'box', around: [a, b] }` |

Elementarten sind frei wählbare Bezeichner. Eingebaut sind `person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note`, `class`, `object`, `event`, `task`, `subprocess`, `gateway`, `data`, `annotation`, `group`, `pool`, `lane` und `conversation`. Andere Arten werden mit einer Warnung akzeptiert; Bibliotheken ergänzen weitere (siehe unten). Eine `person` erscheint als Strichmännchen mit Namen darunter. Da der Name auf der Zeichenfläche steht, verwendet er die Designfarbe `labelText` (`fontColor` eines Stils überschreibt sie; ein Inline-`style` behält das Schwarz von draw.io bei). Die Füllung färbt den Kopf. `node person { text }` im Design färbt den Namen nur, wenn `shape` die Person als gefüllten Kasten zeichnet.

Der Layout-Algorithmus ist `layered` (Standard), `tree`, `grid`, `radial`, `force`, `fixed` oder `sequence` (Erklärungen im [Benutzerhandbuch](guide/layouts.md)). Andere Namen bleiben erhalten, führen aber das Schichtenlayout aus und erzeugen eine Warnung an der `layout`-Zeile.

## Elementarten aus Bibliotheken

Eine Bibliothek kann Elementarten hinzufügen (`kinds` im Manifest, siehe [libraries.md](libraries.md#elementarten)). Wie bei Formen verwendet ein Dokument dazu den Alias der Bibliothek:

```
use "acme-platform@^1" as acme

model {
  orders = acme.service "Orders" tech "Go"
  events = acme.topic "Order events"
  jobs = acme.queue "Jobs" {
    worker = component "Worker"
  }
  orders -> events "publishes"
}
```

- Ein Element einer Bibliotheksart wird mit deren Form (Bibliothekszeichnung oder Grundform) und Farben gezeichnet, sofern es kein eigenes `shape` oder `style` setzt. Mit Kindelementen erscheint es wie bei C4-Arten als Begrenzung mit dem Stereotyp der Art in der Beschriftung (`Jobs [queue]`).
- Die Art wird wie geschrieben als `acme.topic` gespeichert und bleibt so im Modell und im ausgegebenen Text erhalten.
- Eine Art, deren Alias kein `use` deklariert, erzeugt eine Warnung beim Parsen. Fehlt die Art in der Bibliothek, warnen `diadraw check`, `diadraw render` und der Editor wie bei einer unbekannten Form; das Element erscheint als Kasten in den Standardfarben. In beiden Fällen wird der Text weiterhin in ein Modell übersetzt.
- Bibliotheksarten sind immer qualifiziert und kollidieren daher weder mit eingebauten Arten noch untereinander.

## Verweise

Die ID einer Beziehung ergibt sich aus ihren Endpunkten, sofern sie keinen Namen hat. Abgeleitete IDs folgen der Kante: Wird ein Endpunkt verschoben, ändert sich die ID; beim Löschen der ersten von zwei parallelen Kanten wird aus `a->b#2` die ID `a->b`. Bei einer Textänderung von Hand geht damit eine darauf verweisende `route` verloren (beim Neuverbinden auf der Zeichenfläche werden solche Routen auf die neuen IDs umgeschrieben). Gibst du der Kante einen Namen, etwa `pay = a -> b`, behält sie die ID `pay` dauerhaft, auch bei geänderten Endpunkten oder neuer Reihenfolge. `route "pay" …` gilt weiterhin (wann du eine Kante benennen solltest, erklärt der Abschnitt [Beziehungs-IDs](guide/relationships.md#beziehungs-ids) im Benutzerhandbuch). Der Name gilt unverändert, unabhängig vom Deklarationsort der Beziehung; der Pfad des umgebenden Elements wird nicht vorangestellt. Unter Beziehungen muss er eindeutig sein, darf aber dem Namen eines Elements entsprechen. Das Benennen einer Kante verschiebt die abgeleiteten IDs der anderen nicht. `print` schreibt eine als Name zulässige ID als `name = …`. Nicht als Name zulässige IDs (etwa aus JSON) lassen sich nicht schreiben und werden beim Einlesen erneut abgeleitet.

Ein punktgetrennter Verweis wird vom innersten Geltungsbereich nach außen aufgelöst: Innerhalb von `shop { … }` bedeutet `web` also `shop.web`; `shop.web` funktioniert überall. Ansichtsselektoren und Hinweise werden auf dieselbe Weise relativ zum Geltungsbereich der Ansicht aufgelöst. `view checkout of shop { pin db below web }` ist deshalb gültig. Ist das letzte Segment ein Port des vorangehenden Elements (`api.http`), verweist der Ausdruck auf diesen Port.

## Diagnosen

Beim Parsen werden keine Ausnahmen ausgelöst. Der Parser erholt sich selbstständig von Syntaxfehlern: Eine nicht lesbare Anweisung wird markiert. Die Eingabe wird bis zum Ende ihres Blocks oder bis zur nächsten Zeile übersprungen, die dort eine gültige Anweisung beginnt. Der Rest der Datei kann somit weiterhin in ein Modell übersetzt werden. Übersprungene Eingabe wird als `Unexpected "…"` gemeldet, eine abgebrochene Anweisung als `Syntax error: incomplete statement`.

Modellfehler stammen aus den Kernoperationen und der Auflösung von Verweisen. Sie werden an der betroffenen Textstelle gemeldet: unbekannte Elemente, unbekannte Beziehungen in `route`, doppelte IDs, Ports außerhalb eines Elements oder doppelt deklarierte Ports sowie ungültige `use`-Namen oder wiederholte Aliase. Warnungen gibt es für unbekannte Elementarten, Bibliotheksarten ohne zugehörige `use`-Aliasdeklaration sowie `shape` oder `description` an Beziehungen (beide werden dort ignoriert). Zeilen und Spalten beginnen bei 1. Spalten zählen wie in Editoren UTF-16-Codeeinheiten. `diadraw check file.draw` gibt Diagnosen als `file:line:column: error|warning: message` aus.

## Regeln für den Wechsel zwischen Modell und Text

1. **Verlustfreies Parsen.** Der Baum behält Kommentare und Leerraum bei.
2. **Semantische Änderungen werden zu gezielten Textänderungen.** Hinzufügen, Umbenennen oder Neuverbinden auf der Zeichenfläche ändert den kleinstmöglichen Textabschnitt an der richtigen Stelle. Beim Neuverbinden wird nur der verschobene Endpunkt ersetzt, und zwar durch den kürzesten Verweis, der vom Deklarationsort der Beziehung aus das neue Element erreicht. Außerdem wird jede `route` umgeschrieben, deren Beziehungs-ID sich ändert (eine benannte Beziehung behält ihre ID; abgeleitete IDs werden neu bestimmt, weshalb parallele Kanten neu nummeriert werden können; siehe [Benutzerhandbuch](guide/relationships.md#eine-linie-neu-verbinden)). Änderst du im Inspektor die Eigenschaften eines Elements (Art, Name, `tech`, `shape`, `tag`, `description`, `style`, `prop`), werden nur geänderte Klauseln angepasst: Ein geänderter Wert wird an Ort und Stelle ersetzt, ein geleerter gelöscht und ein neuer nach der letzten Klausel der Deklaration angehängt. Kommentare, Klauselreihenfolge und Elementinhalt bleiben erhalten (siehe [Benutzerhandbuch](guide/element-properties.md)). Beschriftung, `tech`, Pfeil, `tag`, `style` und `prop` einer Beziehung werden genauso bearbeitet. Ein neuer Name steht als `name = ` vor der Linie (oder wird samt diesem Präfix entfernt). Jede `route`, die auf eine geänderte ID verweist, wird auf die neue ID umgeschrieben, einschließlich dadurch neu nummerierter abgeleiteter IDs. Auf der Zeichenfläche bearbeitete Stilblöcke und Designs funktionieren ebenso, Eintrag für Eintrag: Geänderte Werte werden an Ort und Stelle ersetzt, Einträge entfernter Schlüssel gelöscht und neue Schlüssel am Blockende angehängt. Neue Blöcke und Designanweisungen stehen neben den anderen ihrer Art (siehe [Handbuch](guide/styles-and-themes.md)).
3. **Geometrische Änderungen werden zu Hinweisen statt zu Koordinaten.** Ziehen schreibt `pin`, `align` oder `order`.
4. **Auch Freiformobjekte stehen im Text.** Notizen, Texte und Rahmen sind Objekte der Ansicht (`note`, `text`, `box`; siehe [Notizen und Dekorationen](#notizen-und-dekorationen)).
5. **Import statt Syntaxkompatibilität.** PlantUML, Mermaid, Structurizr und draw.io werden in das Modell importiert; ihre Syntax wird nicht nativ geparst.

## Offene Punkte

Offene Arbeiten an der DSL und ihrer Bearbeitung über die Zeichenfläche stehen im [Backlog](backlog.md#dsl).

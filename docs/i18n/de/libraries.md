# Formenbibliotheken

Eine Bibliothek ist eine benannte, versionierte Sammlung von **Formen** (Zeichnungen), **zusammengesetzten Formen** (aus anderen Formen aufgebaut), **Arten** (nicht in die DSL eingebauten Elementarten) und **Fragmenten** (einfügbaren Stücken DiaDraw-Quelltext). Hinzu kommen **Zuordnungen** für die Namen anderer Werkzeuge. Über Bibliotheken lässt sich DiaDraw erweitern: Ein Plugin ist eine Bibliothek. Dokumente nennen die verwendeten Bibliotheken; die Host-Anwendungen lösen sie auf. Beim Rendern und Importieren erfolgt kein Netzwerkzugriff.

```
use "c4@^1"
use "aws-icons@^2.1" as aws

model {
  user = person "User" shape c4.person
  fn = component "Handler" tech "Node" shape aws.lambda
  user -> fn
}
```

- `use "<name>[@<range>]" [as <alias>]` steht vor dem Modell. Ohne `as` dient das letzte Namenssegment als Alias, wobei Sonderzeichen durch `_` ersetzt werden (`@acme/cloud-shapes` → `cloud_shapes`). Aliase müssen im Dokument eindeutig sein.
- `shape <alias>.<shape>` verweist auf eine einfache oder zusammengesetzte Form der Bibliothek. Kann der Host den Verweis nicht auflösen, erscheint die übliche Form der Elementart. `diadraw check`, `diadraw render` und der Editor geben eine Warnung aus.
- `<id> = <alias>.<kind>` deklariert ein Element einer von der Bibliothek ergänzten Art (siehe [Elementarten](#elementarten)). Eine unbekannte Art erscheint mit derselben Warnung als Kasten.
- Versionsbereiche folgen SemVer: `^1`, `~1.2`, `1.x`, `>=1.2 <2`, `1.2 - 1.6`, `^1 || ^2` oder exakte Versionen.

## Auflösung

Für ein `use` gilt folgende Suchreihenfolge:

1. die Version, die `diadraw-lock.json` für genau dieses `name@range` festschreibt (die nächstgelegene Lockdatei im Dokumentverzeichnis oder darüber), sofern sie im Cache liegt;
2. die höchste zum Bereich passende Version im Cache;
3. die höchste zum Bereich passende eingebaute Version.

Der Cache liegt unter `$DIADRAW_CACHE`, andernfalls unter `$XDG_CACHE_HOME/diadraw/libraries`, sonst unter `~/.cache/diadraw/libraries`. Er enthält ein JSON-Bundle je Version. Hier wird nichts heruntergeladen: Eine Bibliothek, die an keinem dieser Orte liegt, wird als nicht installiert gemeldet. Die Web-App enthält nur die eingebauten Bibliotheken.

## Befehle

```bash
diadraw lib init shapes --name team-shapes   # manifest, an example shape and fragment, README
diadraw lib build shapes                     # check and build → shapes/dist/library.json
diadraw lib pack shapes                      # → shapes/dist/team-shapes-0.1.0.ddlib and its integrity
diadraw lib add shapes/dist/team-shapes-0.1.0.ddlib   # install into the cache, pin in diadraw-lock.json
diadraw lib add https://example.com/libs/aws-icons-2.1.0.ddlib
diadraw lib add c4@^1                        # pin a built-in (or cached) library
diadraw lib install                          # make the cache hold everything the lockfile pins
diadraw lib list                             # built-in and installed libraries
diadraw lib build --drawio stencils.min.js --set aws4,flowchart -o libs
```

`add` akzeptiert einen Bibliotheksordner, eine `.ddlib`-Datei, ein `.json`-Bundle, eine `http(s)`-URL oder `name[@range]` einer eingebauten oder im Cache vorhandenen Bibliothek. Der Befehl gibt die einzufügende `use`-Zeile aus. Nur `add` und `install` greifen auf das Netzwerk zu, und nur für URLs, die an `add` übergeben wurden oder in der Lockdatei stehen. `install` prüft jedes Bundle gegen den `integrity`-Wert der Lockdatei (`sha256-…` des kanonischen Bundle-JSON) und die festgeschriebene Version.

Die Lockdatei `diadraw-lock.json` gehört in die Versionsverwaltung:

```json
{
  "lockfileVersion": 1,
  "libraries": {
    "c4@^1": { "version": "1.0.0", "resolved": "builtin", "integrity": "sha256-…" },
    "team-shapes@^0.1.0": {
      "version": "0.1.0",
      "resolved": "libs/team-shapes-0.1.0.ddlib",
      "integrity": "sha256-…"
    }
  }
}
```

`resolved` ist ein Pfad relativ zur Lockdatei, eine URL, `builtin` oder `cache:<name>@<version>` für eine Bibliothek, die ausschließlich im lokalen Cache lag (und die `install` daher anderswo nicht wiederherstellen kann).

## Das Manifest

Der Quellordner einer Bibliothek enthält `diadraw-library.json`:

```json
{
  "name": "team-shapes",
  "version": "0.1.0",
  "description": "Shapes for our services",
  "license": "MIT",
  "source": { "kind": "original" },
  "shapes": {
    "gateway": {
      "label": "API gateway",
      "category": "Edge",
      "kind": "component",
      "svg": "shapes/gateway.svg",
      "width": 48,
      "height": 48,
      "keywords": ["api", "ingress"]
    },
    "store": { "label": "Store", "base": "cylinder", "kind": "database" }
  },
  "kinds": {
    "edge": {
      "label": "Edge service",
      "description": "Anything that terminates client traffic",
      "shape": "gateway",
      "stereotype": "edge"
    },
    "topic": { "label": "Topic", "base": "hexagon", "fill": "#fde68a", "stroke": "#b45309" }
  },
  "composites": {
    "gateway_with_store": {
      "width": 120,
      "height": 48,
      "parts": [
        { "shape": "gateway", "x": 0, "y": 0, "width": 48, "height": 48 },
        { "shape": "store", "x": 72, "y": 0, "width": 48, "height": 48 }
      ]
    }
  },
  "fragments": {
    "edge": {
      "label": "Gateway in front of a service",
      "params": [{ "name": "service", "default": "api" }],
      "text": "fragments/edge.draw"
    }
  },
  "mappings": { "drawio": { "mxgraph.team.gateway": "gateway" } },
  "themes": {
    "corporate": {
      "extends": "default",
      "background": "#fafafa",
      "edge": "#4a4a4a",
      "nodes": { "system": { "fill": "#aa0000", "stroke": "#7a0000", "text": "#ffffff" } }
    }
  },
  "dependencies": { "c4": "^1" }
}
```

| Feld | Bedeutung |
| --- | --- |
| `name`, `version` | Name im npm-Stil (Kleinbuchstaben, `-._`, optional `@scope/`), SemVer-Version |
| `license` | SPDX-Ausdruck für die Zeichnungen; `licenseNotes` für zusätzliche Bedingungen |
| `source` | Herkunft der Zeichnungen: `original`, `drawio-stencils`, `plantuml-stdlib`, `svg`, `other`, mit `url`, `commit`, `sets` |
| `shapes` | ID (ein Bezeichner) → `svg` (Dateipfad oder Inline-Markup) oder `base` (Grundform des Renderers), `label`, `category`, `kind` für neue Elemente, Größe, `keywords`, `style` |
| `composites` | eine aus `parts` aufgebaute Form; jeder Teil ist eine Form dieser Bibliothek (oder `dep.shape` aus einer Abhängigkeit), platziert im Rahmen der zusammengesetzten Form |
| `kinds` | ID (ein Bezeichner) → Elementart: `shape` (einfache oder zusammengesetzte Form dieser Bibliothek oder `dep.shape`) oder `base`, `fill`, `stroke`, `fontColor`, `stereotype`, `label`, `description`, `category`, `keywords` |
| `fragments` | DiaDraw-Quelltext (`.draw`-Datei oder inline) für das Innere von `model { … }`; `${param}` übernimmt einen Parameter, `${alias}` den Alias, unter dem die Bibliothek verwendet wird |
| `open` | beliebige Formnamen akzeptieren und als `base` zeichnen (für Symbolsätze ohne Namensliste) |
| `mappings` | je Format (`plantuml`, `plantuml-include`, `plantuml-local`, `structurizr`, `drawio`, `bpmn`): Fremdname → Form-ID |
| `themes` | ID → Design wie in `theme … { … }` ([dsl.md](dsl.md#eigene-designs)): `extends` (Design dieser Bibliothek, eingebautes Design oder `dep.theme`; ein Design dieser Bibliothek hat Vorrang vor einem gleichnamigen eingebauten, außer ein Design erweitert seinen eigenen Namen), Designschlüssel, `nodes` |
| `dependencies` | Bibliotheken, auf denen zusammengesetzte Formen aufbauen: `name` → Versionsbereich |

`kind` einer einfachen oder zusammengesetzten Form (die Art des von der Palette dafür eingefügten Elements) bezeichnet eine bibliothekseigene Art, wenn es dort eine dieses Namens gibt, andernfalls eine eingebaute Art.

## Elementarten

Einige Elementarten sind in die DSL eingebaut (`person`, `system`, `container`, `component`, `database`, `process`, `decision`, `start`, `end`, `note` sowie die UML- und BPMN-Arten ihrer Importer). Eine Bibliothek ergänzt weitere unter `kinds`. Dokumente verwenden sie über den Alias:

```
use "team-shapes@^0.1" as team

model {
  lb = team.edge "Load balancer"
  orders = team.topic "Orders"
  lb -> orders
}
```

Eine Art bestimmt das Aussehen ihrer Elemente. Alles andere (Beschriftungen, `tech`, Kindelemente, Ports, Beziehungen, Ansichten) funktioniert wie bei jedem Element.

- **Form.** `shape` zeichnet die Elemente mit einer Bibliothekszeichnung (Größe und Beschriftung wie bei einem `shape`-Verweis), `base` mit einer Grundform des Renderers (`hexagon`, `cylinder`, …). Ein eigenes `shape` des Elements hat Vorrang.
- **Farben.** `fill`, `stroke` und `fontColor` ersetzen für diese Art die Standardknotenfarben des Designs. Der eigene `style` eines Elements hat Vorrang, und `external`-Elemente behalten die externen Farben.
- **Stereotyp.** Ein Element dieser Art mit Kindelementen erscheint als Begrenzung mit der Beschriftung `Name [stereotype]`.
- **Wo die Arten erscheinen.** Der Sprachserver vervollständigt `alias.`-Arten nach `=` und warnt an der Art selbst, wenn die Bibliothek sie nicht enthält. `diadraw check` und `render` warnen ebenso. Die Webpalette listet sie unter _Kinds_ auf. Das Hinzufügen deklariert ein Element dieser Art.

Arten bleiben im Modell qualifiziert (`team.topic`) und gehen bei der Textausgabe des Dokuments nicht verloren.

`lib build` liest die SVG-Dateien und bereinigt sie: keine Skripte, Ereignishandler, Fremdobjekte oder externen Verweise. IDs erhalten pro Form ein Präfix, damit Symbole auf derselben Seite nicht kollidieren. Der Befehl bettet alles in ein Bundle ein (`format: "diadraw-library"`, `formatVersion: 1`) und prüft, ob jedes Fragment geparst werden kann. Eine `.ddlib` ist dieses Bundle in gzip-komprimierter Form.

## Lizenzen

Jede Bibliothek enthält eine Lizenz, die `lib build`, `lib add` und der Generator für eingebaute Bibliotheken prüfen:

| Status | Bedingung | Wirkung |
| --- | --- | --- |
| `allowed` | Alle Bestandteile einer Lizenzalternative stehen auf der Positivliste (MIT, MIT-0, Apache-2.0, BSD-2/3-Clause, ISC, 0BSD, CC0-1.0, CC-BY-4.0, Unlicense), ohne Zusatzbedingungen; oder es handelt sich um eine eigene Arbeit dieses Projekts. | Darf mit DiaDraw ausgeliefert werden. |
| `restricted` | Erlaubte Lizenzen mit `licenseNotes`. | Warnung; lokale Nutzung möglich, keine Auslieferung. |
| `disallowed` | Jede Alternative enthält eine nichtkommerzielle Lizenz, ein Bearbeitungsverbot oder Copyleft (CC-BY-NC/ND, GPL, AGPL, LGPL, SSPL, BUSL). | `lib build` schlägt fehl. |
| `unknown` | Alle anderen Fälle. | Warnung. |

Nur Bibliotheken mit Status `allowed` werden aus `assets/builtin/` in die eingebauten Bibliotheken aufgenommen. Die einzige Ausnahme sind die Schablonensätze von draw.io, die aufgrund einer ausdrücklichen Entscheidung als `restricted` ausgeliefert werden (siehe unten und `docs/decisions.md`).

## Eingebaute Bibliotheken

Die Quellen liegen in `assets/builtin/`. `node tools/gen/builtin_libraries.mjs` baut sie nach `npm run build -w @diadraw/wasm` mit dem Kern und schreibt `src/library/src/builtin_libraries.inc`, das beim Kompilieren in den Kern eingebunden wird.

- **`c4`**: Person, Softwaresystem, Container, Komponente, Datenbank, Warteschlange, Webbrowser und mobile App, jeweils auch als externe Variante, sofern C4 eine vorsieht; eine zusammengesetzte Form „Dienst mit Datenbank“ sowie die Fragmente „Systemkontext“ und „Web, API und Datenbank“. Zuordnungen: die C4-PlantUML-Makros (`Person`, `System_Ext`, `ContainerDb`, `ComponentQueue`, …) und die Structurizr-Formen (`WebBrowser`, `Cylinder`, `Pipe`, …). Eigene Zeichnungen.
- **`drawio-*`**: die 204 Schablonensätze von draw.io, mit Einschränkungen; siehe [draw.io-Schablonen](#drawio-schablonen).
- **`plantuml-aws`, `plantuml-azure`, `plantuml-gcp`, `plantuml-kubernetes`, `plantuml-tupadr3`, `plantuml-office`, `plantuml-cloudinsight`, `plantuml-logos`, `plantuml-material`, `plantuml-elastic`, `plantuml-osa`, `plantuml-cloudogu`, `plantuml-ibm`, `plantuml-archimate`**: die Symbolsätze der PlantUML-Standardbibliothek als offene Bibliotheken. An ihren `plantuml-include`- und `plantuml-local`-Zuordnungen erkennt der PlantUML-Importer `!include <awslib/…>`, die üblichen URLs und lokal mitgelieferte Kopien. Ein Symbolmakro wie `Lambda(fn, "Handler")` wird als `shape aws.Lambda` mit `use "plantuml-aws@^1" as aws` importiert. Die Symbole selbst sind wegen ihrer unterschiedlichen Lizenzen nicht enthalten, daher erscheinen diese Formen als Symbolkästen.
- **`bpmn`**: BPMN-2.0-Formen als eigene Zeichnungen. Jede Ereignisposition (Start, nicht unterbrechender Start, Ereignisteilprozess-Start, eintretendes Zwischenereignis, nicht unterbrechendes Randereignis, auslösendes Zwischenereignis, Ende) mit allen dort von BPMN zugelassenen Markierungen; Aufgaben aller Typen, Aufrufaktivitäten, Teilprozesse, Ereignisteilprozesse, Transaktionen und Ad-hoc-Teilprozesse, jeweils ohne Markierung sowie mit Schleifen-, paralleler oder sequenzieller Mehrfachinstanz- oder Kompensationsmarkierung; alle Gateways; Datenobjekte, Sammlungen, Eingaben, Ausgaben und Speicher; Nachrichten, Textanmerkungen, Gruppen, Konversationen und Choreografieaufgaben. Aktivitätsformen tragen die Beschriftung innen. Dazu kommen die zusammengesetzte Form „Aufgabe mit Timer“ und die Fragmente „Genehmigungsschleife“ und „Aufgabe mit Zeitüberschreitung“. Die Zuordnung `bpmn` liefert die Form für jeden BPMN-Elementtyp. Erzeugt durch `tools/gen/bpmn_library.mjs`.

## draw.io-Schablonen

Alle Schablonensätze von draw.io werden als eingebaute Bibliotheken unter dem Namen `drawio-<set>` ausgeliefert (`drawio-aws4`, `drawio-azure`, `drawio-cisco19`, `drawio-flowchart`, …): 204 Sätze mit 9.088 Formen aus draw.io 32.6.9. Du verwendest sie wie jede Bibliothek:

```
use "drawio-aws4@^32" as aws4

model {
  fn = component "Handler" shape aws4.lambda_function
}
```

**Ihre Nutzung ist eingeschränkt.** draw.io steht unter Apache-2.0, aber seine Schablonen dürfen ohne schriftliche Genehmigung weder in Atlassian-Produkten oder dem Atlassian Marketplace verwendet oder in diese integriert noch zur Verwendung damit verbreitet werden. Herstellersätze (AWS, Azure, GCP, Cisco, IBM, …) unterliegen den Markenbedingungen der jeweiligen Hersteller. Siehe `assets/NOTICE`. `diadraw lib list` und die Webpalette kennzeichnen jeden Satz als eingeschränkt.

Sie liegen gzip-komprimiert in `assets/drawio/` und werden erst bei Bedarf geladen: Erst wenn ein Dokument einen Satz verwendet oder die Palette ihn öffnet, wird er eingelesen (native Hosts sowie WebAssembly-Hosts, denen mit `setFileSystem(fs, { drawio })` ein Dateisystem übergeben wurde) oder abgerufen und mit `addLibrary` hinzugefügt (Web-App).

Der draw.io-Importer zeichnet Schablonenformen mit diesen Bibliotheken in den Füll- und Linienfarben der Zelle, einschließlich der Kacheln für Ressourcensymbole. Die Konvertierung folgt mxStencil: Pfade, Rechtecke, abgerundete Rechtecke und Ellipsen mit Füllung, Kontur, Farben, Strichelung, Alpha und Sichern/Wiederherstellen des Zeichenzustands. Text, Bilder und verschachtelte Schablonen werden nicht gezeichnet. Formen, die draw.io per Code zeichnet, sind keine Schablonen.

So erzeugst du die Sätze aus der festgeschriebenen Version (`DRAWIO` im Skript) neu:

```bash
npm run build -w @diadraw/wasm
node tools/gen/drawio_stencils.mjs                   # downloads the pinned files
node tools/gen/drawio_stencils.mjs --from ../drawio  # or a local checkout
```

`diadraw lib build --drawio <stencils.min.js|set.xml> -o <dir>` konvertiert weiterhin andere Versionen oder einzelne Sätze in separate Bibliotheken.

## Wo Bibliotheken zum Einsatz kommen

- **CLI**: `render` und `check` lösen die Dokumentbibliotheken auf; `render` zeichnet Bibliotheksformen.
- **Sprachserver**: vervollständigt Bibliotheksnamen in `use "…"`, Formen nach `shape` (alle bei höchstens 400, sonst zuerst die Aliase) und nach Präfixen wie `alias.` sowie Bibliotheksarten nach `=`. Innerhalb von `model { … }` bietet er Fragmente als Snippets mit Platzhaltern für ihre Parameter an. Fehlende Bibliotheken, unbekannte Formen und unbekannte Arten lösen Warnungen aus.
- **Webeditor**: Die Schaltfläche **Shapes** öffnet eine nach Namen und Schlüsselwörtern durchsuchbare Palette mit einfachen und zusammengesetzten Formen, Arten und Fragmenten der eingebauten Bibliotheken. Ein Klick auf eine Form fügt in einem rückgängig machbaren Schritt ein Element mit dieser Form hinzu (sowie das `use` der Bibliothek, falls es noch fehlt). Ein Klick auf ein Fragment fügt dessen Text am Modellende ein.
- **Designs**: `theme alias.name` wählt ein Bibliotheksdesign. `RenderOptions.themes` (`LibrarySet::theme_resolver()`) löst es auf. CLI und Sprachserver warnen, wenn die Bibliothek das Design nicht enthält.
- **Renderer**: `RenderOptions.shapes` löst Verweise auf Zeichnungen auf, die mit der Beschriftung darunter gezeichnet werden; `RenderOptions.kinds` ordnet Bibliotheksarten eine Form und Farben zu. `create_measure(theme, shapes, kinds)` bestimmt die dazu passende Größe. `LibrarySet::renderer()` liefert beides.

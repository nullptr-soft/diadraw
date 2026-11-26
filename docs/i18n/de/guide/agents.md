# Zeichnen mit KI-Agenten

DiaDraw-Diagramme sind reiner Text, also schreibt ein KI-Agent sie so leicht wie Code. Der Agent schreibt eine `.draw`-Datei, prüft sie, rendert sie, sieht sich das Bild an und korrigiert, was schlecht lesbar ist – dieselbe Schleife, die ein Mensch in der App durchläuft. Zwei Bausteine machen diese Schleife einfach und arbeiten gut zusammen:

- **Der MCP-Server**, `diadraw mcp`: gibt jedem Agenten, der das [Model Context Protocol](https://modelcontextprotocol.io) spricht (Claude Code, Claude Desktop und andere), Werkzeuge zum Prüfen, Rendern und Importieren von Diagrammen. Das Rendern liefert ein Bild, sodass der Agent sieht, was er gezeichnet hat.
- **Der Skill für Claude Code**: bringt Claude Code die Sprache und die Schleife aus Prüfen, Rendern, Ansehen und Korrigieren bei. Installiere ihn mit `/plugin marketplace add mykulyak/diadraw` und danach `/plugin install diadraw@diadraw` (siehe [plugins/diadraw/README.md](../../plugins/diadraw/README.md)).

## Was der Agent kann

| Werkzeug    | Was es tut                                                                                                                      |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `check`     | Meldet Fehler und Warnungen als `Zeile:Spalte: Schweregrad: Meldung` und listet die Ansichten des Dokuments auf                 |
| `render`    | Zeichnet eine Ansicht und liefert sie als PNG-Bild (oder SVG); kann außerdem eine `.svg`-, `.png`- oder `.pdf`-Datei schreiben  |
| `format`    | Liefert das Dokument in kanonischer Form oder schreibt die Datei neu                                                            |
| `import`    | Wandelt Mermaid-, PlantUML-, Structurizr-, draw.io- und BPMN-Diagramme in ein DiaDraw-Dokument um                               |
| `reference` | Listet Elementarten, Formen, Themes, Layouts und Bibliotheken auf, oder die Arten und Formen einer Bibliothek (`library: "c4"`) |
| `examples`  | Listet die [Beispieldiagramme](examples.md) auf und liefert jedes davon, damit der Agent mit einem ähnlichen beginnen kann      |

Jedes Werkzeug nimmt das Dokument entweder als `source` (seinen Text) oder als `path` (eine Datei). Arbeite am besten mit Dateien in deinem Projekt: Dann liegt das Diagramm wie alles andere in der Versionsverwaltung. Bibliotheken werden offline aufgelöst, wie auf der Kommandozeile: die eingebauten, die Schablonensätze von draw.io und alles, was `diadraw lib install` in den Cache gelegt hat.

## Einrichtung

Der Server gehört zum Kommandozeilenwerkzeug `diadraw`. Die Pakete für Linux und Windows enthalten es; anderswo baust du es aus dem Repository (siehe [src/README.md](../../src/README.md)):

```sh
cmake --preset release && cmake --build --preset release
# das Programm liegt unter ../diadraw-build/release/diadraw
```

Sag deinem Agenten dann, wo es liegt. Gib den vollständigen Pfad an, wenn es nicht in deinem `PATH` ist.

### Claude Code

```sh
claude mcp add diadraw -- /pfad/zu/diadraw mcp
```

Damit wird der Server dem aktuellen Projekt nur für dich hinzugefügt. Mit `--scope user` steht er in jedem Projekt bereit, mit `--scope project` wird er in die `.mcp.json` des Projekts geschrieben und mit allen geteilt, die daran arbeiten. Gib in Claude Code `/mcp` ein, um die Verbindung zu prüfen.

### Claude Desktop

Öffne **Settings › Developer › Edit Config** und füge DiaDraw in `claude_desktop_config.json` hinzu:

```json
{
  "mcpServers": {
    "diadraw": {
      "command": "/pfad/zu/diadraw",
      "args": ["mcp"]
    }
  }
}
```

Starte Claude Desktop neu. Claude Desktop startet den Server außerhalb jedes Projekts; bitte es daher, für die Dateien, die es liest und schreibt, vollständige Pfade zu verwenden.

### Andere Agenten

Jeder MCP-Client kann `diadraw mcp` als lokalen Server (stdio) starten: Er liest eine JSON-RPC-Nachricht pro Zeile von der Standardeingabe und antwortet auf der Standardausgabe.

## Ein Diagramm anfordern

Bitte mit eigenen Worten darum und sag, wohin die Datei soll, zum Beispiel:

> Zeichne die Container unseres Checkout-Dienstes als C4-Diagramm in `docs/checkout.draw`, rendere es nach `docs/checkout.svg` und prüfe, ob das Bild gut lesbar ist.

Der Agent schreibt die Datei, führt `check` aus, bis keine Fehler mehr kommen, ruft dann `render` auf und sieht sich das Bild an. Überlappen sich Kästen oder läuft der Fluss in die falsche Richtung, ändert er die Ansicht, etwa mit `layout layered direction right` oder `pin db below api`, und rendert erneut. Danach kannst du dieselbe Datei in jeder DiaDraw-App öffnen und von Hand oder auf der Zeichenfläche weiterbearbeiten.

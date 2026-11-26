# Beispiele

Jede App enthält Beispieldiagramme zum Öffnen, Lesen und Ändern. Unter macOS, Linux und Windows findest du sie unter **File › Examples**, in der Web-App in der Liste **Examples…** in der Symbolleiste. Das gewählte Beispiel öffnet sich als neues, unbenanntes Dokument. Änderungen berühren das Original nicht; speichere das Dokument unter einem eigenen Namen, um es zu behalten.

| Beispiel | Was es zeigt |
| --- | --- |
| `bpmn-01-order-process` | BPMN-Prozess in einem Pool mit zwei Lanes: Nachrichten-Startereignis, Benutzer-, Sende-, manuelle und Serviceaufgaben, exklusive und parallele Gateways, angeheftetes Zeitereignis und Datenausgabe |
| `bpmn-02-claim-collaboration` | BPMN-Kollaboration zweier Pools mit Nachrichtenflüssen, ereignisbasiertem Gateway, Geschäftsregelaufgabe, Datenspeicher und Textanmerkung |
| `c4-01` … `c4-04` | C4-System- und Containerdiagramme, Gruppen und eine frei platzierte Notiz |
| `flow-01` … `flow-04` | Flussdiagramme mit Entscheidungen und Schleifen |
| `uml-01-class-library` | UML-Klassendiagramm: Attribute und Operationen, Schnittstelle, abstrakte Klasse, Vererbung, Realisierung, Komposition, Aggregation und Multiplizitäten |
| `uml-02-sequence-checkout` | UML-Sequenzdiagramm: Aktivierungen, je ein `loop`- und `alt`-Rahmen, Antworten, eine asynchrone Nachricht und eine Notiz |
| `uml-03-activity-order` | UML-Aktivitätsdiagramm: Start- und Endknoten, Entscheidung und Zusammenführung, Parallelisierung und Synchronisation |
| `uml-04-state-order` | UML-Zustandsautomat: Übergänge mit Ereignissen, Bedingungen und Aktionen sowie ein zusammengesetzter Zustand |
| `uml-05-use-case-shop` | UML-Anwendungsfalldiagramm: Akteure, Systemgrenze, «include» und «extend», Generalisierung von Akteuren |
| `uml-06-component-shop` | UML-Komponentendiagramm: Komponenten, angebotene Schnittstellen als Lollipops und benötigte Schnittstellen |

Die Beispiele sind die `.draw`-Dateien im Ordner `corpus/` des Repositorys. Sie dienen auch als Test- und Benchmarkdokumente: Jede Datei muss sich ohne Warnung parsen lassen, und jede Ansicht muss unter Beachtung sämtlicher Hinweise angeordnet werden können.

## Wie die UML- und BPMN-Beispiele gezeichnet werden

DiaDraw kennt keine eigenen UML- oder BPMN-Diagrammtypen. Die Beispiele verwenden die normale DSL:

- **Klassendiagramme** verwenden die Art `class` mit `prop "uml.attributes"` und `prop "uml.methods"` (ein Mitglied pro Zeile, dazwischen `\n`) sowie `prop "stereotype"`. Multiplizitäten stehen als `prop "uml.sourceMultiplicity"` und `prop "uml.targetMultiplicity"` an der Beziehung. Die Linienarten (Vererbung, Realisierung, Komposition, …) ergeben sich aus Beziehungsstilblöcken für ein Tag mit offenen oder gefüllten [Linienenden](line-styles.md).
- **Sequenzdiagramme** sind Ansichten mit `layout sequence` (siehe [DSL-Referenz](../dsl.md#sequenzansichten)).
- **Aktivitäts- und Zustandsdiagramme** verwenden Grundformen: `circle` für Start- und Endknoten, `bar` für Parallelisierung und Synchronisation, `rhombus` für Entscheidungen und Zusammenführungen, `rounded` für Zustände.
- **Anwendungsfalldiagramme** zeichnen Akteure mit `shape actor` und Anwendungsfälle mit `shape ellipse` innerhalb eines `system`-Elements, das als Systemgrenze gezeichnet wird.
- **BPMN** verwendet die eingebaute Bibliothek `bpmn` (`use "bpmn@^1" as bpmn`) für Ereignisse, Aufgaben, Gateways und Daten. Pools und Lanes sind `swimlane`-Formen in einer [festen Ansicht](fixed-views.md), wie BPMN-Werkzeuge einen Prozess anordnen. Ein angeheftetes Ereignis wird am Rand der Aufgabe fixiert, mit `prop "bpmn.attachedTo"`, damit es beim BPMN-Export angeheftet bleibt.

# DiaDraw-Benutzerhandbuch

Dieses Handbuch richtet sich an alle, die mit DiaDraw Diagramme zeichnen – im Texteditor, auf der Zeichenfläche oder mit beiden. Es erklärt, wozu die Funktionen dienen und wann du sie einsetzt. Die genaue Syntax steht in der [DSL-Referenz](../dsl.md); das Handbuch verweist darauf, statt sie zu wiederholen.

## Kapitel

- [Beispiele](examples.md): die mit jeder App ausgelieferten Beispieldiagramme (C4, Flussdiagramme, UML-Klassen-, Sequenz-, Aktivitäts-, Zustands-, Anwendungsfall- und Komponentendiagramme sowie BPMN) und wie sie gezeichnet werden.
- [Beziehungen](relationships.md): Elemente verbinden, Pfeile und Beschriftungen, Ports, eine Linie auf der Zeichenfläche neu verbinden und wann sich ein Name für eine Beziehung lohnt.
- [Linienenden und Linienstile](line-styles.md): Pfeile, Rauten, Krähenfüße und andere Enden, gepunktete, doppelte, gekrümmte und abgerundete Linien und ihre Auswahl in der App oder im Text.
- [Layouts](layouts.md): die Layout-Algorithmen (`layered`, `tree`, `grid`, `radial`, `force`, `fixed`, `sequence`), wann welcher passt und wie sie mit Hinweisen und Containern umgehen.
- [Eigenschaften auf der Zeichenfläche bearbeiten](element-properties.md): der Inspektor für Elemente und Beziehungen, was jedes Feld schreibt, wie du eine Linie benennst und wie die Änderung in deinen Text gelangt.
- [Stile und Designs auf der Zeichenfläche](styles-and-themes.md): Stilblöcke für Elementarten, Tags und Beziehungen, Designs auswählen und anlegen und wie sich das im Text niederschlägt.
- [Kästen verschieben](moving.md): einen Kasten in jeder Ansicht an die gewünschte Stelle ziehen, die daraus entstehende `pin … at`-Zeile, Ziehen mit der Wahltaste zur Platzierung relativ zu Nachbarn und die Mehrfachauswahl zum gemeinsamen Verschieben oder Löschen.
- [Größe ändern und drehen](resize-and-rotate.md): einen Kasten in jeder Ansicht vergrößern, verkleinern und drehen und die daraus entstehenden `size`- und `rotate`-Zeilen.
- [Das Raster](grid.md): ein Raster auf der Zeichenfläche einblenden und beim Verschieben, Skalieren und Knicken daran einrasten.
- [Von Hand in festen Ansichten zeichnen](fixed-views.md): Kästen auf der Zeichenfläche verschieben und skalieren, Linien knicken und die daraus entstehenden `pin`- und `route`-Zeilen.
- [Zeichnen mit KI-Agenten](agents.md): der MCP-Server (`diadraw mcp`) und der Skill für Claude Code, mit denen ein Agent wie Claude Code Diagramme schreibt, prüft, rendert und korrigiert.
- [Diagramme in Markdown](markdown.md): ein Diagramm als ` ```diadraw `-Block in eine Markdown-Seite schreiben und es auf Dokumentationswebsites (remark, markdown-it) oder im Browser (`diadraw.js`) zeichnen.
- [Diagramme in Confluence](confluence.md): das Makro DiaDraw diagram für Confluence Cloud, sein Editor, Themes, der Import aus Mermaid und PlantUML das Speichern von Diagrammen als Bilder und das Veröffentlichen von `.draw`-Dateien aus einem Repository.
- [Google Drive und OneDrive](cloud-storage.md): Diagramme aus jeder App in beiden Diensten öffnen und speichern, der Storage-Dialog der Web-App, was DiaDraw dort sieht und wie man einen eigenen DiaDraw-Build bei Google und Microsoft registriert.
- [Fehler melden und Feedback geben](feedback.md): wo du in jeder App „Report a Bug“ und „Give Feedback“ findest, was sie für dich eintragen und was sie nie senden.

Weitere Kapitel folgen, während das Handbuch wächst: Modelle und Ansichten, Notizen und Dekorationen, Sequenzdiagramme, Bibliotheken sowie der Import aus anderen Werkzeugen.

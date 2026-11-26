# Das Raster

Blende das Raster ein, um Kästen von Hand auszurichten. Wähle unter macOS **View ▸ Show Grid** (⌘') oder klicke auf die Raster-Schaltfläche in der Symbolleiste. Unter Windows und Linux wählst du **Show Grid** im Menü (unter Linux auch Strg+'); in der Web-App klickst du in der Symbolleiste auf **Grid** (⌘' oder Strg+'). Das Raster liegt hinter dem Diagramm, standardmäßig mit einer Linie alle 10 Diagrammeinheiten. Jede fünfte Linie ist dunkler. Beim Herauszoomen verschwinden die helleren Linien zuerst.

Solange das Raster sichtbar ist, rasten Änderungen auf der Zeichenfläche daran ein:

- **Verschiebst du einen Kasten**, landet seine linke obere Ecke auf dem nächsten Rasterpunkt. Die dabei geschriebene `pin … at`-Zeile enthält Rasterkoordinaten (`pin db at 260, 250` bei einem 10er-Raster). Siehe [Kästen verschieben](moving.md).
- **Änderst du die Größe eines Kastens** in einer Ansicht mit `layout fixed`, landet die gezogene Ecke auf einem Rasterpunkt. In anderen Ansichten, in denen das Layout die Position bestimmt und nur `size` geschrieben wird, sowie bei gedrehten Kästen werden stattdessen Breite und Höhe auf ganze Rasterschritte gerundet. Hältst du beim Skalieren die Umschalttaste gedrückt, bleiben die Proportionen erhalten und die Größe rastet nicht ein. Siehe [Größe ändern und drehen](resize-and-rotate.md).
- **Knickst du eine Linie** in einer festen Ansicht, landet der Knickpunkt auf einem Rasterpunkt.

Ziehen mit der Wahltaste (Alt unter Windows und Linux) bleibt unberührt: Das Layout platziert den Kasten relativ zu seinen Nachbarn. In einer festen Ansicht, in der auch dieses Ziehen `pin … at` schreibt, landet der Kasten genau dort, wo du loslässt, unabhängig vom Raster. Beim Drehen gelten weiterhin eigene Schritte von 15°.

Das Raster ist eine Einstellung der App, nicht der Zeichnung. Es schreibt nichts in deinen Text und bleibt für jedes geöffnete Dokument ein- oder ausgeschaltet. Unter macOS wählst du mit **View ▸ Grid Size** eine Schrittweite von 5, 10, 20 oder 40 Einheiten (und schaltest das Raster damit ein). In der Web-App verwendest du dafür die Größenliste neben **Grid**. Die Auswahl wird gespeichert, bei der Web-App im Browser. Schaltest du das Raster aus, bleibt alles an seinem Platz. Beim nächsten Ziehen landet das Element wieder genau dort, wo du loslässt.

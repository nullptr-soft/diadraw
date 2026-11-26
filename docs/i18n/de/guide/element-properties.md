# Eigenschaften auf der Zeichenfläche bearbeiten

Ein Element ist mehr als ein Kasten mit Namen. Seine Art bestimmt, wie es gezeichnet wird. `tech` und `description` ergänzen Details, `shape` wählt den Umriss, Tags gruppieren es für die Gestaltung, `style` ändert sein Aussehen und `prop` speichert weitere Angaben, die du am Element behalten möchtest:

```
model {
  db = container "Orders DB" tech "Postgres" shape cylinder tag "pii" prop "owner" "Ann"
}
```

Du kannst das alles im Text schreiben, musst es aber nicht. Wähle ein einzelnes Element auf der Zeichenfläche aus: Seine Eigenschaften erscheinen im Bereich daneben, dem Inspektor. Wählst du eine einzelne Beziehung aus (per Klick auf ihre Linie), zeigt der Inspektor stattdessen die Beziehung; siehe [Beziehungen](#beziehungen).

## Der Inspektor

Der Eigenschaftenbereich bleibt unabhängig von der Auswahl neben der Zeichenfläche, sodass sich das Diagramm beim Auswählen nicht verschiebt. Ist genau ein Element oder eine Beziehung ausgewählt, erscheint der Inspektor. Ohne Auswahl zeigt der Bereich [Design und Stile des Dokuments](styles-and-themes.md#design-und-stile-des-dokuments); bei mehreren ausgewählten Objekten zeigt er deren Anzahl. Bei einem Element trägt der Inspektor dessen ID als Titel.

| Feld | Schreibt | Eingabe |
| --- | --- | --- |
| Kind | die Art hinter `=` | `container`, `database`, `person`, … oder `alias.kind` einer Bibliothek |
| Name | die Beschriftung in Anführungszeichen | beliebiger Text; bei leerem Feld dient der Bezeichner des Elements als Name |
| Tech | `tech "…"` | beliebiger Text |
| Shape | `shape …` | eine Grundform wie `cylinder` oder `hexagon` oder eine Bibliotheksform |
| Tags | ein `tag "…"` pro Tag | durch Kommas getrennt: `external, legacy` |
| Description | `description "…"` | beliebiger Text |
| Style | `style "…"` | `key=value`-Paare, getrennt durch `;`: `fill=#dae8fc;stroke=#6c8ebf` |
| Properties | ein `prop "…" "…"` pro Schlüssel | ein `key=value` pro Zeile |
| Shared styles | die zutreffenden `style`-Blöcke | ein `selector: key=value;…` pro Zeile (siehe [Stile und Designs](styles-and-themes.md)) |

In der Web-App schlagen Kind und Shape beim Tippen die eingebauten Namen vor. Style akzeptiert dieselben Schlüssel wie `style "…"` direkt im Text. Die [DSL-Referenz](../dsl.md#freiformdiagramme) listet sie auf.

### Bedienelemente in den Apps für macOS, Web und Linux

Für die meisten Felder bieten die Apps eigene Bedienelemente statt reiner Texteingabe an. Jedes schreibt denselben Text wie oben beschrieben.

- **Kind, Shape und Tech** bieten Auswahllisten und nehmen auch neu eingetippte Werte an. Kind listet die eingebauten Arten und die der Dokumentbibliotheken auf, Shape die Grundformen und Bibliotheksformen (leer bedeutet die Form der gewählten Art), Tech die schon im Dokument verwendeten Technologien.
- **Tags** ist ein Eingabefeld für einzelne Tags: Tippe ein Tag und ein Komma. Bereits im Dokument verwendete Tags werden vorgeschlagen.
- **Description** wächst mit dem Text. Die Eingabetaste beginnt eine neue Zeile, das Verlassen des Feldes übernimmt die Änderung.
- **Style** ist ein visueller Editor. Für Elemente gibt es Farbschaltflächen für Füllung, Linie und Text sowie Einstellungen für Linienbreite, Strichelung, abgerundete Ecken, Deckkraft, Schriftgröße, Fett-, Kursiv- und Unterstreichungsformatierung sowie Ausrichtung und Position der Beschriftung. Für Beziehungen gibt es Linienfarbe, Breite, Strichelung und Muster (gepunktet, Strich-Punkt, …), Doppellinie, rechtwinklige Führung, Kurven und abgerundete Ecken, Anfang und Ende (Pfeile, Rauten, Kreise, Balken, Krähenfüße, jeweils mit Vorschau) samt Füllung sowie Farbe und Größe der Beschriftung. Jede Einstellung schreibt einen Schlüssel und lässt die übrigen unverändert. Zurücksetzen auf den Standardwert (bei Farben auch das ⓧ daneben) entfernt den Schlüssel. Schlüssel ohne eigenes Bedienelement bleiben unter **Other** als `key=value;…` erhalten. **Clear style** entfernt den gesamten Stil.
- **Farbschaltflächen** öffnen eine Palette mit Vorgaben: Graustufen von Weiß bis Schwarz, dann helle, kräftige und dunkle Varianten von zehn Farbtönen. Dieselben Farbfelder halten die Farben über Elemente und Diagramme hinweg einheitlich. **Custom…** öffnet die Farbauswahl des Systems für andere Farben, **Default** entfernt den Schlüssel. Alle Apps (macOS, Linux, Web) bieten dieselben Farbfelder.
- **Properties** ist eine Tabelle von Schlüsseln und Werten. Bearbeite eine Zelle, füge mit + eine Zeile hinzu oder wähle eine aus und entferne sie mit −.

Die Web-App unterscheidet sich in einigen Details: Tags erscheinen als kleine Felder mit × zum Entfernen (Eingabetaste oder Komma fügt das eingegebene Tag hinzu, die Rückschritttaste im leeren Feld entfernt das letzte). Style lässt sich mit dem Dreieck am Titel einklappen und zeigt dann den Stiltext. Jede Eigenschaftszeile hat ein eigenes −. Für „Arrow“ einer Beziehung gibt es drei Schaltflächen mit der jeweiligen Linie. Das Feld „Name“ zeigt die aus den Enden abgeleitete ID ausgegraut, bis du einen Namen einträgst.

Die Linux-App (GTK) bietet dieselben Bedienelemente mit einigen Unterschieden in der Darstellung: Kind, Shape und Tech sind Eingabefelder mit einer ▾-Schaltfläche, die nach Bibliothek gruppierte Optionen zeigt. Tags erscheinen als Felder mit × zum Entfernen. Ein Eingabefeld darunter fügt per Eingabetaste oder Komma ein Tag hinzu (sein ▾ listet die Tags des Dokuments auf). Jede Eigenschaftszeile hat ihr eigenes −. **Custom…** in einer Farbpalette öffnet die GNOME-Farbauswahl.

### Eine Änderung übernehmen

Eine Feldänderung wird übernommen, wenn du die Eingabetaste drückst oder in ein anderes Feld wechselst. Escape stellt den vorherigen Wert wieder her. Leerst du Tech, Shape, Description oder Style, entfällt die entsprechende Eigenschaft. Leerst du Tags oder Properties, werden alle zugehörigen Einträge entfernt.

Im Beschreibungsfeld beginnt die Eingabetaste eine neue Zeile. Verlasse das Feld zum Übernehmen (in der Web-App auch mit ⌘Enter / Strg+Enter). In der Eigenschaftentabelle übernimmt die Eingabetaste die Zelle.

Die Zeichenfläche zeigt die Beschreibung in kleinerer Schrift unter Name und Technologie und macht das Feld so hoch, dass sie hineinpasst (bei langen Zeilen auch breiter, bis 240 px, Rauten und Ellipsen bis 320 px). Längere Zeilen werden umbrochen, Zeilenumbrüche und Leerzeilen bleiben erhalten. Kreise, Beschriftungen außerhalb der Form (Akteure, `labelPosition`, `verticalLabelPosition`), Zeichnungen mit innenliegender Beschriftung oder eigenem `style` und Elemente, die als Rahmen um ihre Kinder gezeichnet werden, zeigen nur den Namen. Eine mehrzeilige Beschreibung wird als Blockstring in den Text geschrieben, Zeile für Zeile, eingerückt unter dem Element:

```
model {
  db = container "Orders DB" tech "Postgres" description """
    Orders and their lines.

    Archived after two years.
  """
}
```

So kannst du sie auch im Text schreiben: `"""` öffnet und schließt sie, und die gemeinsame Einrückung der Zeilen gehört nicht zur Beschreibung (siehe die [DSL-Referenz](../dsl.md#lexikalische-regeln)).

Jede Änderung ist ein einzelner Rückgängig-Schritt, gemeinsam mit dem Texteditor: ⌘Z nimmt sie auf beiden Seiten zurück.

## Beziehungen

Der Inspektor einer Beziehung trägt ihre Schreibweise als Titel, etwa `web -> db` oder `checkout = web -> db`, und bietet folgende Felder:

| Feld | Schreibt | Eingabe |
| --- | --- | --- |
| Label | Beschriftung in Anführungszeichen | beliebiger Text; leer bedeutet keine Beschriftung |
| Tech | `tech "…"` | beliebiger Text |
| Arrow | `->`, `-->` oder `<->` | Auswahl: durchgezogen, gestrichelt oder Pfeilspitzen an beiden Enden |
| Name | `name =` vor der Linie | ein Bezeichner wie `checkout`; bei leerem Feld wird die ID abgeleitet |
| Tags | ein `tag "…"` pro Tag | durch Kommas getrennt: `async, legacy` |
| Style | `style "…"` | `key=value`-Paare, getrennt durch `;`: `stroke=#c00;dashed=1` |
| Properties | ein `prop "…" "…"` pro Schlüssel | ein `key=value` pro Zeile |

Die Pfeilauswahl gilt sofort; die anderen Felder verhalten sich wie bei Elementen. Die Endpunkte stehen nicht im Inspektor. Bearbeite zum Neuverbinden den Text.

### Eine Beziehung im Inspektor benennen

Das Feld „Name“ enthält die ID der Beziehung, wenn du ihr eine gegeben hast (siehe [Benannte Beziehungen](relationships.md#benannte-beziehungen)). Bei einer von den Enden abgeleiteten ID bleibt es leer. Trägst du einen Namen ein, erscheint `name = ` vor der Linie; leerst du das Feld, entfällt dieser Teil. Die Linie bleibt unter ihrer neuen ID ausgewählt. Jede `route "…"`, die auf die alte ID verwies, wird auf die neue umgestellt. So bleiben manuell gesetzte Knickpunkte erhalten. Gibst du der Linie in

```
model {
  …
  web -> db "writes"
}

view main {
  route "web->db" via 200, 80
}
```

den Namen `calls`, ändern sich zwei Stellen:

```
  calls = web -> db "writes"
  …
  route "calls" via 200, 80
```

Das Benennen oder Entfernen eines Namens kann die abgeleiteten IDs anderer Linien zwischen denselben Elementen ändern, da `#2`, `#3` die vorherigen unbenannten Linien mitzählen. Auch ihre Routen werden aktualisiert, sodass keine manuell geführte Linie ihre Route verliert. Ist ein Name kein gültiger Bezeichner oder schon von einer Beziehung belegt, wird er abgelehnt; die Statuszeile erklärt, warum.

## Was sich im Text ändert

Der Text bleibt maßgeblich. Jede Änderung im Inspektor ändert deshalb die Datei, aber der Inspektor schreibt so wenig wie möglich:

- Ein geänderter Wert wird dort ersetzt, wo du ihn geschrieben hast. So bleibt `shape rect tech "React"` in dieser Reihenfolge, wenn du die Technologie änderst, und ein neuer Pfeil ersetzt nur den Pfeil.
- Eine geleerte Eigenschaft wird samt vorangestelltem Leerzeichen gelöscht.
- Eine neue Eigenschaft wird nach der letzten Eigenschaft der Zeile und vor einem etwaigen `{ … }`-Block oder Kommentar eingefügt. Eine neue Beziehungsbeschriftung folgt unmittelbar auf das Ziel, wo Beschriftungen stehen.
- Beibehaltene Tags bleiben an ihrem Platz, neue kommen ans Ende.

Kommentare, Ausrichtung, der Inhalt des Elements und alles andere in der Datei bleiben unverändert. Änderst du eine Technologie im Inspektor, wird aus

```
    web = container "Web app" shape rect tech "React" tag "edge"  // storefront
```

folgender Text:

```
    web = container "Web app" shape rect tech "Vue" tag "edge"  // storefront
```

## Gut zu wissen

- **Ein Objekt auf einmal.** Wähle ein einzelnes Element oder eine einzelne Beziehung zum Bearbeiten aus. Bei Mehrfachauswahl zeigt der Bereich nur die Anzahl. Per Doppelklick auf eine Linie lässt sich ihre Beschriftung weiterhin direkt bearbeiten.
- **Element-IDs ändern sich nicht.** Der Inspektor bearbeitet die Eigenschaften eines Elements, nicht seinen Platz im Modell. Um den Bezeichner zu ändern, das Element einem anderen Elternelement unterzuordnen oder Ports hinzuzufügen, bearbeite den Text. Eine Ausnahme bildet die ID einer Beziehung, die über „Name“ geändert werden kann.
- **Beziehungen ignorieren `shape` und `description`.** Der Inspektor bietet sie nicht an, und im Text lösen sie eine Warnung aus.
- **Arten und Formen sind Namen.** Enthält eine Art oder Form Leerzeichen oder sonstige Satzzeichen, wird sie abgelehnt. Die Statuszeile erklärt den Grund; es wird nichts geschrieben.
- **Stilschlüssel** dürfen weder `=` noch `;` enthalten, Werte kein `;`, wie im Text.
- **Ein Wert, der abgelehnt würde, wird schon beim Tippen rot umrandet**, und der Tooltip des Felds
  sagt warum: eine Id, die kein Name oder schon vergeben ist, eine Art oder Form, die kein Name ist,
  ein Stilschlüssel mit `=` oder `;`, eine Zeile der gemeinsamen Stile, die nicht lesbar ist oder
  nicht für das Element gilt, und ein Beziehungsname, der kein Name oder schon vergeben ist. Sobald
  der Wert gültig ist, verschwindet der Rahmen.
- **Das Aussehen einer ganzen Art** gehört in einen `style`-Block statt in das Style-Feld jedes Elements. Mit „Shared styles“ bearbeitest du die auf das Element zutreffenden Blöcke (siehe [Stile und Designs auf der Zeichenfläche](styles-and-themes.md)).
- **Verfügbar in:** der Web-App und den Apps für macOS und Linux (GTK). Die Windows-App hat noch keinen Inspektor.

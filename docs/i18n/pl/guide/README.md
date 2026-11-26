# Podręcznik użytkownika DiaDraw

Ten podręcznik jest dla osób, które rysują diagramy w DiaDraw — w edytorze tekstu, na płótnie lub na oba sposoby. Wyjaśnia, do czego służą poszczególne funkcje i kiedy ich używać. Dokładny opis składni znajdziesz w [dokumentacji DSL](../dsl.md); podręcznik odsyła do niej zamiast go powtarzać.

## Rozdziały

- [Przykłady](examples.md): jakie diagramy znajdziesz w każdej aplikacji (C4, schematy blokowe, diagramy klas, sekwencji, czynności, stanów, przypadków użycia i komponentów UML oraz diagramy BPMN) i jak je narysowano.
- [Relacje](relationships.md): jak łączyć elementy, używać strzałek, etykiet i portów, przepinać linię na płótnie i kiedy nadać relacji nazwę.
- [Zakończenia i style linii](line-styles.md): strzałki, romby, kurze łapki i inne zakończenia; linie kropkowane, podwójne, zakrzywione i zaokrąglone oraz ich wybór w aplikacji lub w tekście.
- [Układy](layouts.md): który algorytm układu wybrać (`layered`, `tree`, `grid`, `radial`, `force`, `fixed`, `sequence`) oraz jak poszczególne algorytmy obsługują wskazówki i kontenery.
- [Edycja właściwości na płótnie](element-properties.md): jak korzystać z inspektora elementów i relacji, co zapisuje każde pole, jak nadać linii nazwę i jak zmiana trafia do tekstu.
- [Style i motywy na płótnie](styles-and-themes.md): bloki stylów dla rodzajów elementów, tagów i relacji, wybieranie i tworzenie motywów oraz zapis tych zmian w tekście.
- [Przesuwanie elementów](moving.md): jak przeciągnąć element w wybrane miejsce w dowolnym widoku, jak wiersz `pin … at` zachowuje to położenie, jak zamiast tego użyć klawisza Option do ustawienia elementu względem sąsiadów i jak zaznaczyć kilka elementów, aby przesunąć je lub usunąć razem.
- [Zmiana rozmiaru i obracanie](resize-and-rotate.md): jak powiększyć, zmniejszyć i obrócić element w dowolnym widoku oraz jak wiersze `size` i `rotate` zachowują te ustawienia.
- [Siatka](grid.md): jak wyświetlić siatkę na płótnie i jak działa przyciąganie do widocznej siatki podczas przesuwania elementów, zmiany ich rozmiaru i zaginania linii.
- [Rysowanie ręczne w widokach o stałym układzie](fixed-views.md): jak przesuwać elementy, zmieniać ich rozmiar i zaginać linie na płótnie oraz jak wiersze `pin` i `route` zachowują te zmiany.
- [Rysowanie z agentami AI](agents.md): serwer MCP (`diadraw mcp`) i umiejętność dla Claude Code, dzięki którym agent, na przykład Claude Code, pisze, sprawdza, renderuje i poprawia diagramy.
- [Diagramy w Markdownie](markdown.md): zapisywanie diagramu jako bloku ` ```diadraw ` na stronie Markdown i rysowanie go w witrynach z dokumentacją (remark, markdown-it) lub w przeglądarce (`diadraw.js`).
- [Diagramy w Confluence](confluence.md): makro DiaDraw diagram dla Confluence Cloud, jego edytor, motywy, import z Mermaid i PlantUML zapisywanie diagramów jako obrazów oraz publikowanie plików `.draw` z repozytorium.
- [Google Drive i OneDrive](cloud-storage.md): otwieranie i zapisywanie diagramów w obu usługach z każdej aplikacji, okno Storage w aplikacji webowej, co DiaDraw tam widzi oraz jak zarejestrować własną kompilację DiaDraw w Google i Microsoft.
- [Zgłaszanie błędów i przekazywanie opinii](feedback.md): gdzie znaleźć polecenia Report a Bug i Give Feedback w każdej aplikacji, jakie informacje wpisują do formularza i czego nigdy nie wysyłają.

W miarę rozwoju podręcznika pojawią się kolejne rozdziały: modele i widoki, notatki i dekoracje, diagramy sekwencji, biblioteki oraz import z innych narzędzi.

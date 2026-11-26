# Edycja właściwości na płótnie

Element to nie tylko prostokąt z nazwą. Jego rodzaj decyduje o tym, jak zostanie narysowany. `tech` i `description` pozwalają podać szczegóły, `shape` określa kształt, a tagi grupują elementy, którym chcesz nadać wspólny styl. Za pomocą `style` zmienisz wygląd elementu, a w `prop` zapiszesz inne potrzebne informacje:

```
model {
  db = container "Orders DB" tech "Postgres" shape cylinder tag "pii" prop "owner" "Ann"
}
```

Możesz to wszystko zapisać w tekście, ale nie musisz. Zaznacz jeden element na płótnie, a w panelu obok pojawią się jego właściwości. Ten panel to inspektor. Jeśli zaznaczysz jedną relację (klikając jej linię), inspektor pokaże właściwości relacji; zobacz [Relacje](#relacje).

## Inspektor

Panel właściwości pozostaje obok płótna niezależnie od zaznaczenia, więc diagram się nie przesuwa. Gdy zaznaczysz dokładnie jeden element lub jedną relację, w panelu pojawi się inspektor. Bez zaznaczenia panel pokazuje [motyw i style dokumentu](styles-and-themes.md#motyw-i-style-dokumentu), a przy zaznaczeniu kilku obiektów podaje ich liczbę. W przypadku elementu tytułem inspektora jest identyfikator elementu.

| Pole | Co zapisuje | Jak je wypełnić |
| --- | --- | --- |
| Rodzaj | rodzaj po `=` | `container`, `database`, `person`, … lub `alias.kind` z biblioteki |
| Nazwa | etykietę w cudzysłowie | dowolny tekst; jeśli pole jest puste, nazwą jest identyfikator elementu |
| Technologia | `tech "…"` | dowolny tekst |
| Kształt | `shape …` | kształt podstawowy, np. `cylinder` lub `hexagon`, albo kształt z biblioteki |
| Tagi | po jednym `tag "…"` na tag | nazwy oddzielone przecinkami: `external, legacy` |
| Opis | `description "…"` | dowolny tekst |
| Styl | `style "…"` | pary `key=value` oddzielone średnikiem `;`: `fill=#dae8fc;stroke=#6c8ebf` |
| Właściwości | po jednym `prop "…" "…"` na klucz | jedna para `key=value` w wierszu |
| Style wspólne | bloki `style` dotyczące elementu | po jednym wpisie `selector: key=value;…` w wierszu (zobacz [Style i motywy](styles-and-themes.md)) |

W aplikacji internetowej pola Rodzaj i Kształt podpowiadają wbudowane nazwy podczas pisania. Pole Styl przyjmuje te same klucze co `style "…"` w tekście; ich listę znajdziesz w [dokumentacji DSL](../dsl.md#diagramy-swobodne).

### Elementy sterujące w aplikacjach na macOS i Linux oraz w aplikacji internetowej

Większość pól możesz edytować za pomocą kontrolek zamiast wpisywać tekst. Każda z nich zapisuje tekst zgodny z opisem pola w tabeli powyżej.

- **Rodzaj, Kształt i Technologia** to listy wyboru, w których możesz też wpisać nową wartość. Pole Rodzaj podaje rodzaje wbudowane i pochodzące z bibliotek dokumentu. Pole Kształt podaje kształty podstawowe i biblioteczne (puste pole oznacza kształt właściwy dla rodzaju). Pole Technologia podaje technologie już używane w dokumencie.
- **Tagi** to pole, w którym każdy tag jest osobną pozycją. Wpisz tag i przecinek, aby go dodać. Pole podpowiada tagi już używane w dokumencie.
- **Opis** powiększa się wraz z tekstem. Return zaczyna nowy wiersz, a opuszczenie pola zatwierdza opis.
- **Styl** to edytor wizualny. Dla elementu możesz ustawić kolor wypełnienia, linii i tekstu, szerokość linii, kreskowanie, zaokrąglenie, krycie, rozmiar czcionki, pogrubienie, kursywę i podkreślenie, a także wyrównanie i położenie etykiety. Dla relacji możesz wybrać kolor i szerokość linii, włączyć kreskowanie i wybrać jego wzór (kropki, kreska i kropka itp.), włączyć linię podwójną, prowadzenie pod kątem prostym, krzywe i zaokrąglone narożniki. Możesz też wybrać zakończenia początkowe i końcowe (strzałki, romby, kółka, poprzeczki i zakończenia w notacji kurzej stopki, każde z podglądem), określić, czy mają być wypełnione, oraz ustawić kolor i rozmiar etykiety. Każda kontrolka zapisuje jeden klucz i nie zmienia pozostałych. Przywrócenie wartości domyślnej (lub kliknięcie ⓧ obok koloru) usuwa dany klucz. Klucze bez własnej kontrolki pozostają w polu **Inne** w postaci `key=value;…`. Polecenie **Wyczyść styl** usuwa cały styl.
- **Przyciski kolorów** otwierają paletę gotowych kolorów: odcienie szarości od bieli do czerni, a następnie jasne, jaskrawe i ciemne warianty dziesięciu barw. Wybierając te same próbki, zachowasz spójne kolory na różnych elementach i diagramach. Opcja **Niestandardowy…** otwiera systemowy selektor, w którym możesz wybrać dowolny inny kolor. Opcja **Domyślny** usuwa klucz. We wszystkich aplikacjach (macOS, Linux, przeglądarka) dostępne są te same próbki.
- **Właściwości** to tabela kluczy i wartości: możesz edytować komórkę, dodać wiersz przyciskiem + lub zaznaczyć wiersz i usunąć go przyciskiem −.

Aplikacja internetowa różni się kilkoma szczegółami. Tagi mają postać znaczków z przyciskiem × do usuwania (Enter lub przecinek dodaje wpisany tag, a Backspace w pustym polu usuwa ostatni). Trójkąt przy tytule sekcji Styl zwija ją i pokazuje w zamian tekst stylu. Każdy wiersz właściwości ma własny przycisk −, a strzałkę relacji wybiera się spośród trzech przycisków z rysunkiem linii. Dopóki nie wpiszesz nazwy relacji, pole Nazwa pokazuje na szaro identyfikator, który DiaDraw wyprowadza z jej końców.

Aplikacja na Linux (GTK) ma te same kontrolki, choć wyglądają nieco inaczej. Rodzaj, Kształt i Technologia to pola z przyciskiem ▾, który wyświetla dostępne opcje pogrupowane według biblioteki. Tagi to znaczki z przyciskiem × do usuwania. Pod nimi znajduje się pole, w którym Enter lub przecinek dodaje tag, a ▾ wyświetla tagi dokumentu. Każdy wiersz właściwości ma własny przycisk −. Opcja **Niestandardowy…** w palecie przycisku koloru otwiera selektor kolorów środowiska GNOME.

### Zatwierdzanie zmian

Po zmianie wartości naciśnij Enter lub przejdź do innego pola, aby ją zatwierdzić. Escape przywraca poprzednią wartość. Wyczyszczenie pola Technologia, Kształt, Opis lub Styl usuwa daną właściwość. Wyczyszczenie pola Tagi lub Właściwości usuwa wszystkie jego wpisy.

W polu opisu Enter zaczyna nowy wiersz. Aby zatwierdzić opis, opuść pole (w aplikacji internetowej możesz też nacisnąć ⌘Enter / Ctrl+Enter). W tabeli właściwości Enter zatwierdza komórkę.

Kanwa rysuje opis pod nazwą i technologią mniejszą czcionką i powiększa ramkę tak, aby się zmieścił (dla długich wierszy także poszerza ją, do 240 px, a romb lub elipsa do 320 px). Dłuższe wiersze są zawijane, a podziały i puste wiersze zostają. Koła, etykiety rysowane poza kształtem (aktorzy, `labelPosition`, `verticalLabelPosition`), rysunki z etykietą w środku lub z własnym `style` oraz elementy rysowane jako ramka wokół swoich dzieci pokazują tylko nazwę. Opis z podziałami wierszy trafia do tekstu jako ciąg blokowy, wiersz po wierszu, z wcięciem pod elementem:

```
model {
  db = container "Orders DB" tech "Postgres" description """
    Orders and their lines.

    Archived after two years.
  """
}
```

Możesz też wpisać go tak w tekście: `"""` otwiera i zamyka opis, a wcięcie wspólne dla wierszy nie należy do opisu (zobacz [opis DSL](../dsl.md#reguły-leksykalne)).

Każda zmiana to jeden krok we wspólnej historii cofania inspektora i edytora tekstu: ⌘Z po dowolnej stronie cofa zmianę.

## Relacje

Tytułem inspektora relacji jest jej zapis tekstowy, np. `web -> db` lub `checkout = web -> db`. Inspektor zawiera następujące pola:

| Pole | Co zapisuje | Jak je wypełnić |
| --- | --- | --- |
| Etykieta | etykietę w cudzysłowie | dowolny tekst; jeśli pole jest puste, linia nie ma etykiety |
| Technologia | `tech "…"` | dowolny tekst |
| Strzałka | `->`, `-->` lub `<->` | wybierz linię ciągłą, kreskowaną lub linię z grotami na obu końcach |
| Nazwa | `name =` przed zapisem relacji | identyfikator, np. `checkout`; jeśli pole jest puste, identyfikator wynika z końców relacji |
| Tagi | po jednym `tag "…"` na tag | nazwy oddzielone przecinkami: `async, legacy` |
| Styl | `style "…"` | pary `key=value` oddzielone średnikiem `;`: `stroke=#c00;dashed=1` |
| Właściwości | po jednym `prop "…" "…"` na klucz | jedna para `key=value` w wierszu |

Wybór strzałki działa od razu. Pozostałe pola zatwierdza się tak jak dla elementów. Inspektor nie pokazuje końców linii: aby ją przepiąć, zmień tekst.

### Nadawanie nazwy relacji w inspektorze

Jeśli nadasz relacji identyfikator, pokaże się on w polu Nazwa (zobacz [Nazwane relacje](relationships.md#nazwane-relacje)). Jeśli identyfikator wynika z końców relacji, pole jest puste. Wpisanie nazwy dodaje `name = ` przed zapisem relacji, a wyczyszczenie pola usuwa ten przedrostek. Linia pozostaje zaznaczona pod nowym identyfikatorem. Każdy wpis `route "…"`, który wskazywał stary identyfikator, wskazuje teraz nowy, więc ręcznie ustawione punkty załamania pozostają na miejscu. Na przykład w dokumencie:

```
model {
  …
  web -> db "writes"
}

view main {
  route "web->db" via 200, 80
}
```

nadanie linii nazwy `calls` zmienia dwa miejsca:

```
  calls = web -> db "writes"
  …
  route "calls" via 200, 80
```

Nadanie lub usunięcie nazwy linii może zmienić identyfikatory wyprowadzone dla innych linii między tymi samymi elementami, ponieważ numeracja `#2`, `#3` zależy od liczby poprzedzających je linii bez nazwy. Ich trasy również są aktualizowane, więc żadna ręcznie poprowadzona linia nie traci trasy. Jeśli nazwa nie jest poprawnym identyfikatorem lub ma ją już inna relacja, aplikacja odrzuci ją i poda przyczynę na pasku stanu.

## Co się dzieje z tekstem

Tekst pozostaje źródłem prawdy: każda zmiana w inspektorze trafia do pliku, ale inspektor zmienia tylko to, co konieczne:

- zmienioną wartość zastępuje w miejscu jej zapisu, więc po zmianie technologii kolejność `shape rect tech "React"` zostaje zachowana, a nowa strzałka zastępuje tylko poprzednią strzałkę;
- wyczyszczoną właściwość usuwa razem z poprzedzającą ją spacją;
- nową właściwość dopisuje za ostatnią właściwością w wierszu, przed ewentualnym blokiem `{ … }` lub komentarzem (nową etykietę relacji wstawia bezpośrednio za celem, tam gdzie jest miejsce na etykietę);
- pozostawione tagi zachowuje na miejscu, a nowe dopisuje na końcu.

Komentarze, wyrównanie, treść bloku elementu i reszta pliku pozostają bez zmian. Jeśli zmienisz technologię w inspektorze, zapis:

```
    web = container "Web app" shape rect tech "React" tag "edge"  // storefront
```

zmieni się na:

```
    web = container "Web app" shape rect tech "Vue" tag "edge"  // storefront
```

## Warto wiedzieć

- **Jeden obiekt naraz.** Aby edytować element lub relację, zaznacz tylko ten obiekt. Jeśli zaznaczysz kilka, panel poda jedynie ich liczbę. Dwukrotne kliknięcie linii nadal pozwala edytować etykietę w miejscu.
- **Identyfikatory elementów się nie zmieniają.** Inspektor pozwala zmienić opis samego elementu, ale nie jego miejsce w strukturze: aby zmienić identyfikator, przenieść element do innego elementu nadrzędnego lub dodać porty, edytuj tekst. Wyjątkiem jest identyfikator relacji, który możesz zmienić w polu Nazwa.
- **Relacje ignorują `shape` i `description`.** Inspektor nie udostępnia tych pól, a użycie ich w tekście powoduje ostrzeżenie.
- **Rodzaje i kształty podaje się jako nazwy.** Jeśli rodzaj lub kształt zawiera spację lub niedozwolony znak interpunkcyjny, aplikacja go odrzuca. Pasek stanu podaje przyczynę; nic nie zostaje zapisane.
- **Klucze stylu** nie mogą zawierać `=` ani `;`, a wartości nie mogą zawierać `;` — tak samo jak w tekście.
- **Wartość, która zostałaby odrzucona, ma czerwoną obwódkę** już podczas pisania, a podpowiedź
  pola mówi dlaczego: Id, które nie jest nazwą albo jest zajęte, rodzaj lub kształt, który nie jest
  nazwą, klucz stylu z `=` lub `;`, wiersz stylów wspólnych, którego nie da się odczytać albo który
  nie dotyczy elementu, oraz nazwa relacji, która nie jest nazwą albo jest zajęta. Obwódka znika,
  gdy wartość jest poprawna.
- **Wygląd wszystkich elementów danego rodzaju** określ w bloku `style`, a nie w polu Styl każdego elementu z osobna. Pole Style wspólne w inspektorze pozwala edytować bloki dotyczące danego elementu (zobacz [Style i motywy na płótnie](styles-and-themes.md)).
- **Dostępność:** aplikacja internetowa oraz aplikacje na macOS i Linux (GTK). Aplikacja na Windows nie ma jeszcze inspektora.

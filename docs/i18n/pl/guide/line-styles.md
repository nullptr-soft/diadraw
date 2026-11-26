# Zakończenia i style linii

Strzałka relacji (`->`, `-->`, `<->`) określa jej podstawowy wygląd: linię ciągłą lub kreskowaną z wypełnionym grotem. Jeśli diagram korzysta z określonej notacji, np. UML, diagramu związków encji lub diagramu przepływu danych, zarówno zakończenia, jak i sama linia mają znaczenie. Możesz je dobrać za pomocą stylu relacji — dla pojedynczej linii lub dla wszystkich linii z danym tagiem.

## Wybór w aplikacji

Zaznacz linię. W sekcji **Styl** panelu właściwości znajdziesz osobne kontrolki:

- **Początek** i **Koniec** pozwalają wybrać zakończenia linii. Na macOS i Linuksie przy każdej pozycji widać miniaturę linii. Na macOS i w aplikacji internetowej pozycje pogrupowano w kategorie: strzałki, kształty, linie i kurza stopka.
- Wyłączenie opcji **Wypełniony początek** lub **Wypełniony koniec** pozostawia sam obrys trójkąta, półstrzałki, rombu, koła lub kwadratu na danym końcu.
- **Styl linii** określa, jak rysowana jest linia: ciągła, przerywana, kropkowana, z krótkich lub długich kresek, kreska z kropką albo kreska z dwiema kropkami. Na macOS i Linuksie przy każdej pozycji widać miniaturę linii.
- **Linia podwójna** rysuje dwie cienkie linie zamiast jednej. **Szerokość linii** pozwala pogrubić linię.
- **Prowadzenie linii** wyznacza trasę pod kątem prostym, **Krzywa** prowadzi gładką krzywą przez punkty załamania, a **Zaokrąglone narożniki** zaokrąglają narożniki linii.

Każda kontrolka zapisuje jeden klucz we właściwości `style "…"` relacji i nie zmienia reszty tekstu. Przywrócenie wartości domyślnej usuwa odpowiedni klucz.

## Zapis w tekście

Te same klucze możesz podać w `style "key=value;…"` po zapisie relacji:

```
model {
  customer = process "Customer"
  order = process "Order"
  customer -> order "places" style "startArrow=ERmandOne;endArrow=ERzeroToMany"
  order -> customer "notifies" style "dashed=1;dashPattern=1 3;endArrow=open;curved=1"
}
```

Aby nadać styl wszystkim liniom z danym tagiem, użyj bloku stylu (zobacz [Style i motywy](styles-and-themes.md)):

```
style relationship tag "async" { dashed true dashPattern "1 3" endArrow open }
```

### Zakończenia

Pola `startArrow` i `endArrow` przyjmują poniższe wartości. Nazwy pochodzą z draw.io, więc diagram importowany z draw.io zachowuje zakończenia linii.

| Wartość | Wygląd | Typowe zastosowanie |
| --- | --- | --- |
| `classic` | grot strzałki | wywołanie lub zależność (domyślne zakończenie) |
| `block` | trójkąt | dziedziczenie w UML (bez wypełnienia, `endFill=0`) |
| `open` | otwarty grot | zależność w UML, komunikat |
| `async`, `openAsync` | półstrzałka, otwarta półstrzałka | komunikat asynchroniczny |
| `doubleBlock` | podwójny trójkąt | szybki lub priorytetowy przepływ |
| `diamond` | romb | kompozycja w UML (wypełniony), agregacja (bez wypełnienia) |
| `oval` | koło | punkt połączenia |
| `box` | kwadrat | port lub kwalifikator |
| `circlePlus` | koło ze znakiem plus | zawieranie w UML |
| `halfCircle` | gniazdo | interfejs wymagany |
| `baseDash`, `dash`, `cross` | poprzeczka, ukośnik, krzyżyk | zatrzymanie, przerwa, niedozwolony przepływ |
| `ERone`, `ERmandOne`, `ERzeroToOne` | notacja kurzej stopki: jeden | jeden, dokładnie jeden, zero lub jeden |
| `ERmany`, `ERoneToMany`, `ERzeroToMany` | notacja kurzej stopki: wiele | wiele, jeden lub wiele, zero lub wiele |
| `none` | brak oznaczenia | linia bez kierunku |

Bez `startArrow` początek linii nie ma oznaczenia, chyba że linię zapisano za pomocą `<->`. Bez `endArrow` linia kończy się grotem strzałki. Ustawienia `startFill=0` i `endFill=0` pozostawiają sam obrys kształtu.

### Linia

| Klucz | Efekt |
| --- | --- |
| `dashed=1` | kreskowanie linii (tak jak przy `-->`) |
| `dashPattern=…` | wzór kreskowania: `1 3` — kropki, `3 3` — krótkie kreski, `12 6` — długie kreski, `8 3 1 3` — kreska i kropka |
| `double=1` | dwie linie z prześwitem w kolorze tła |
| `strokeWidth=…` | szerokość, domyślnie 1,25 |
| `stroke=#…` | kolor |
| `edgeStyle=orthogonalEdgeStyle` | prowadzenie pod kątem prostym |
| `curved=1` | gładkie krzywe przechodzące przez punkty załamania |
| `rounded=1` | zaokrąglone narożniki |

Wzór `dashPattern` jest widoczny tylko na linii kreskowanej. Pole Styl linii w aplikacji ustawia obie właściwości.

## Warto wiedzieć

- Prosta linia nie ma narożników, więc `curved` i `rounded` nie zmieniają jej wyglądu. Efekt widać dopiero tam, gdzie linia zmienia kierunek: gdy algorytm układu prowadzi ją wokół prostokątów, pod kątem prostym lub przez punkty załamania wpisu `route`.
- Prześwit w linii podwójnej ma kolor tła strony. Tam, gdzie linia podwójna przecina wypełnioną grupę lub kontener, w prześwicie widać więc kolor strony.
- Przy eksporcie do draw.io linia zachowuje zakończenia oraz ustawienia `dashPattern`, `curved` i `rounded`. Nie zachowuje `double`, ponieważ draw.io rysuje linie podwójne jako inny rodzaj krawędzi.
- Import z draw.io przenosi zakończenia, ale jeszcze nie krzywizny, zaokrąglone narożniki ani wzór kreskowania krawędzi. Pod tym względem importowane strony wyglądają więc tak samo jak przed dodaniem tych opcji.

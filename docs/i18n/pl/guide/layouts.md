# Układy

Wiersz `layout` w widoku wybiera algorytm, który rozmieszcza elementy i rysuje linie. Przy układach automatycznych nie musisz podawać współrzędnych: wybierz algorytm pasujący do diagramu, określ kierunek, a gdy wynik nie odpowiada Twoim oczekiwaniom, skoryguj go wskazówkami.

```
view checkout of shop {
  include *
  layout tree direction right spacing 40
}
```

Parametry `direction` (kierunek: `down`, `right`, `up`, `left`) i `spacing` (odstęp między elementami w pikselach) są opcjonalne. Bez wiersza `layout` widok korzysta z układu `layered` skierowanego w dół.

## Wybór algorytmu

| Algorytm | Rozmieszczenie elementów | Linie | Zastosowanie |
| --- | --- | --- | --- |
| `layered` | Warstwami zgodnie z kierunkiem przepływu, z niewielką liczbą przecięć | Pod kątem prostym | Architektura, schematy blokowe i inne diagramy przedstawiające przepływ |
| `tree` | W uporządkowane drzewo, z rodzicami wyśrodkowanymi nad dziećmi | Jak w schemacie organizacyjnym | Schematy organizacyjne, hierarchie, drzewa decyzyjne, menu |
| `grid` | W wierszach lub kolumnach, w kolejności zapisu | Pod kątem prostym | Katalogi, wykazy, strony typu „wszystkie nasze usługi” |
| `radial` | Na pierścieniach wokół elementu o największej liczbie połączeń | Proste | Układy z węzłem centralnym, system i jego sąsiedzi |
| `force` | Połączone elementy się przyciągają, pozostałe odpychają | Proste | Sieci i mapy bez głównego kierunku |
| `fixed` | Dokładnie tam, gdzie wskazuje `pin … at` | Według podanej trasy | Diagramy rysowane ręcznie i importowane |
| `sequence` | Na liniach życia, z krokami z bloku `sequence { … }` | Komunikaty | Interakcje w czasie (zobacz dokumentację DSL) |

Jeśli nie wiesz, który układ wybrać, zacznij od `layered`: działa z dowolnym grafem i uwzględnia kierunek strzałek. Zmień go, gdy chcesz nadać diagramowi określony kształt.

### layered

To układ domyślny. Umieszcza elementy w warstwach tak, aby większość linii prowadziła w kierunku wybranym dla widoku. Kolejność elementów w każdej warstwie dobiera tak, aby unikać przecięć. Linie omijają elementy, załamując się pod kątem prostym.

### tree

Każdy element trafia pod ten, od którego prowadzi do niego pierwsza linia: `a -> b` sprawia więc, że `b` jest dzieckiem `a`. Elementy, do których nie prowadzi żadna linia, są korzeniami i stoją obok siebie w kolejności zapisu. Dzieci są uporządkowane według kolejności linii. Linia spoza drzewa (np. od drugiego rodzica lub prowadząca z powrotem w górę) również jest rysowana i omija elementy.

Jak na schemacie organizacyjnym, linie biegną od rodzica do poprzeczki tuż za jego rzędem, następnie wzdłuż niej i dalej do dziecka. Etykieta linii znajduje się na jej ostatnim odcinku, obok dziecka. Odstęp między rzędami zwiększa się, by pomieścić etykietę. `direction right` układa drzewo poziomo; `up` i `left` odwracają odpowiedni kierunek.

### grid

Elementy wypełniają siatkę w kolejności podanej w widoku (według `include`, a przy `include *` — według kolejności w modelu). Liczba wierszy i kolumn jest zbliżona: dziewięć elementów daje trzy wiersze po trzy, a dziesięć — wiersze o czterech miejscach (cztery, cztery i dwa elementy). Szerokość kolumny wyznacza jej najszerszy element, a wysokość wiersza — najwyższy. Elementy są wyśrodkowane w komórkach.

Przy `direction down` (ustawieniu domyślnym) elementy wypełniają kolejne wiersze od góry do dołu; przy `right` — kolejne kolumny od lewej do prawej. `up` i `left` zaczynają od przeciwnej strony. Linie przebiegają między komórkami, dlatego siatka sprawdza się w widokach z małą liczbą linii. Gdy jest ich dużo, wybierz `layered`.

### radial

Element o największej liczbie linii trafia na środek. Elementy bezpośrednio z nim połączone zajmują pierwszy pierścień wokół niego, ich sąsiedzi — następny i tak dalej. Każdej gałęzi przypada wycinek koła proporcjonalny do liczby należących do niej elementów. Odstępy między pierścieniami pozwalają pomieścić elementy i etykiety. Niepołączone ze sobą części widoku są rozmieszczane obok siebie. Ustawienie kierunku nie ma tu zastosowania.

### force

Ten układ opiera się na symulacji: każda para elementów się odpycha, a każda linia przyciąga do siebie swoje końce. Ruch wygasa w ustalonej liczbie iteracji. Gęsto połączone elementy skupiają się blisko siebie, ujawniając grupy, których możesz nie dostrzec w układzie warstwowym. Wynik jest zawsze taki sam dla tego samego widoku, ale dodanie lub usunięcie jednego elementu może wyraźnie przesunąć pozostałe. Jeśli zależy Ci na zachowaniu rozmieszczenia, użyj wskazówek lub przełącz układ na `fixed`, gdy diagram wygląda już tak, jak chcesz. Ustawienie kierunku nie ma tu zastosowania.

## Kontenery

Każdy z automatycznych algorytmów najpierw rozmieszcza zawartość każdego kontenera (np. kontenery systemu lub elementy grupy), stosując ten sam algorytm również wewnątrz. Następnie umieszcza cały kontener wśród jego sąsiadów jako pojedynczy element. Linie między elementami w różnych kontenerach omijają elementy leżące po drodze.

## Wskazówki nadal działają

`pin a below b`, `align a b horizontally` i `order a b c` działają z każdym układem automatycznym. Jeśli początkowe rozmieszczenie nie spełnia wskazówki, algorytm na etapie korekty przesuwa elementy i wyznacza na nowo trasy linii, których dotyczy zmiana. `diadraw render` ostrzega o każdej wskazówce, której mimo to nie udało się spełnić. Przeciągnięcie elementu na płótnie zapisuje `pin … at` (zobacz [Przesuwanie elementów](moving.md)). Jeśli przytrzymasz Option lub Alt, zamiast współrzędnych zapiszą się wskazówki określające położenie względem innych elementów. Do takich zmian na płótnie stosują się więc te same zasady.

## Nazwy nierozpoznawane przez DiaDraw

Jeśli nazwa zawiera literówkę lub nie jest obsługiwana, jak `layout circular` czy `layout layred`, DiaDraw rysuje widok za pomocą `layered`. Przy wierszu `layout` wyświetla ostrzeżenie z listą rozpoznawanych nazw. Wpisana nazwa pozostaje w pliku, więc nic nie tracisz, jeśli późniejsza wersja doda jej obsługę.

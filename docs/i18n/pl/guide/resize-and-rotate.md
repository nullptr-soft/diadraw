# Zmiana rozmiaru i obracanie

Każdy element na płótnie możesz powiększyć, zmniejszyć lub obrócić w dowolnym widoku, niezależnie od układu. Zmiana zapisuje się jako wiersz tekstu w tym widoku i dotyczy tylko jego. Ten sam element może więc być duży w widoku szczegółowym, a mały w widoku ogólnym.

```
view checkout of shop {
  include *
  size web 220, 90
  rotate db -15
}
```

## Zmiana rozmiaru

Zaznacz jeden element. W jego narożnikach pojawią się małe kwadratowe uchwyty. Przeciągnij jeden z nich: przeciwległy narożnik pozostanie na miejscu, a podczas przeciągania nad elementem będzie widać nowy rozmiar. Minimalny rozmiar to 10 × 10.

Przytrzymaj **Shift** podczas przeciągania, aby zachować proporcje elementu: szerokość i wysokość rosną lub maleją razem, zgodnie z tym wymiarem, dla którego ruch wskaźnika oznacza większą zmianę. Wymiary są zaokrąglane do liczb całkowitych, więc proporcje są zachowane z dokładnością do jednej jednostki. Zmiana rozmiaru z klawiszem Shift nie przyciąga elementu nawet do widocznej siatki, ponieważ jej krok rzadko pozwala zachować proporcje.

- W widoku **z układem automatycznym** (`layered`, `tree`, `grid`, `radial`, `force`) po zwolnieniu przycisku myszy zapisuje się `size web 220, 90`. Algorytm zachowuje ten rozmiar i odpowiednio rozmieszcza pozostałe elementy. Sam element może się przesunąć, gdy algorytm robi dla niego miejsce.
- W widoku **sekwencji** zmiana rozmiaru dotyczy nagłówka uczestnika.
- W widoku **o stałym układzie** rozmiar, tak jak dotąd, trafia do wiersza `pin … at … size` danego elementu (zobacz [Rysowanie ręczne w widokach o stałym układzie](fixed-views.md)).
- Algorytm dobiera rozmiar kontenera (elementu zawierającego inne elementy) do jego zawartości, dlatego ręczna zmiana rozmiaru kontenera jest możliwa tylko w widoku o stałym układzie.

## Dopasowanie rozmiaru elementu do zawartości

Aby cofnąć zmianę rozmiaru i przywrócić wymiary wynikające z nazwy i kształtu elementu, **kliknij dwukrotnie jeden z uchwytów narożnych** albo wybierz **Size to Fit Content**. Na Macu (⌥⌘=) i na Windows polecenie znajduje się w menu Edit, a w aplikacji na Linux — w menu głównym (Ctrl+Alt+=). W aplikacji webowej to przycisk **Fit** na pasku narzędzi (⌥⌘= na Macu, Ctrl+Alt+= w innych systemach). Polecenie działa na wszystkie zaznaczone elementy naraz i można je cofnąć w jednym kroku.

Dopasowanie usuwa rozmiar z tekstu, zamiast zapisywać nową wartość. Od tej chwili wymiary elementu zależą od jego zawartości: jeśli zmienisz nazwę, element odpowiednio się powiększy lub zmniejszy.

- W widoku z układem automatycznym i w widoku sekwencji znika wiersz `size web …`.
- W widoku o stałym układzie element pozostaje na miejscu: `pin web at 40, 60 size 220, 90` zmienia się w `pin web at 40, 60`.
- W przypadku kontenera w widoku o stałym układzie znika cały wiersz `pin`, nawet jeśli nie określał rozmiaru. Dzięki temu kontener znów otacza swoje elementy.
- Element bez własnego ustawienia rozmiaru jest już dopasowany, więc polecenie niczego nie zmienia.

## Obracanie

Tuż za prawym górnym narożnikiem zaznaczonego elementu znajduje się też okrągły uchwyt. Aby obrócić element, przeciągnij uchwyt wokół niego: kąt obrotu odpowiada ruchowi wskaźnika wokół środka elementu. Jeśli kąt różni się od wielokrotności 15° o nie więcej niż 5°, zostaje do niej przyciągnięty; w przeciwnym razie jest zaokrąglany do pełnych stopni. Podczas przeciągania kąt widać nad elementem. Zwolnienie przycisku myszy zapisuje `rotate db -15` (w stopniach, zgodnie z ruchem wskazówek zegara; wartość ujemna oznacza obrót w przeciwną stronę). Obrócenie elementu z powrotem do pozycji wyjściowej usuwa ten wiersz.

Etykieta obraca się wraz z kształtem. Linie dochodzą do obróconego obrysu, a układy automatyczne zostawiają miejsce na obrócony kształt. Długi element ustawiony pionowo zajmuje więc tyle miejsca co wysoki.

Nie można obracać kontenerów ani uczestników sekwencji. Wiersz `rotate` dotyczący takiego elementu jest oznaczany jako wskazówka dla algorytmu układu, której nie udało się spełnić.

## Co się dzieje z tekstem

- Istniejący wiersz `size` lub `rotate` jest aktualizowany **w tym samym miejscu**, z zachowaniem komentarza na końcu oraz nazwy w zapisanej przez Ciebie postaci. Nowy wiersz jest dopisywany po pozostałych wierszach widoku.
- Przesunięcie elementu nigdy nie zmienia jego `size` ani `rotate`.
- Wymiary są zaokrąglane do liczb całkowitych, a kąty obrotu — do jednej dziesiątej stopnia, w zakresie od -180 do 180.

## Dostępność

W aplikacjach na macOS, Linux (GTK) i Windows oraz w aplikacji internetowej uchwyty zmiany rozmiaru i obrotu są dostępne w każdym widoku.

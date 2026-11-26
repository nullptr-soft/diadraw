# Przykłady

Każda aplikacja zawiera przykładowe diagramy, które możesz otworzyć, przeanalizować i zmienić. Na macOS, Linuksie i Windows znajdziesz je w menu **File › Examples**, a w aplikacji internetowej — na liście **Examples…** na pasku narzędzi. Wybrany przykład otwiera się jako nowy dokument bez nazwy, więc zmiany nie naruszają oryginału. Aby je zachować, zapisz dokument pod własną nazwą.

| Przykład | Co przedstawia |
| --- | --- |
| `bpmn-01-order-process` | Proces BPMN w jednej puli z dwoma torami: zdarzenie początkowe wywołane komunikatem, zadania użytkownika, wysyłania, ręczne i usługowe, bramki wykluczające i równoległe, brzegowe zdarzenie czasowe oraz dane wyjściowe |
| `bpmn-02-claim-collaboration` | Współpraca dwóch pul w BPMN z przepływami komunikatów, bramką opartą na zdarzeniach, zadaniem reguły biznesowej, magazynem danych i adnotacją tekstową |
| `c4-01` … `c4-04` | Diagramy systemów i kontenerów w notacji C4, grupy oraz swobodnie umieszczona notatka |
| `flow-01` … `flow-04` | Schematy blokowe z decyzjami i pętlami |
| `uml-01-class-library` | Diagram klas UML: atrybuty i operacje, interfejs, klasa abstrakcyjna, dziedziczenie, realizacja, kompozycja, agregacja i liczności |
| `uml-02-sequence-checkout` | Diagram sekwencji UML: aktywacje, ramki `loop` i `alt`, odpowiedzi, komunikat asynchroniczny i notatka |
| `uml-03-activity-order` | Diagram czynności UML: węzły początkowe i końcowe, węzeł decyzji i odpowiadający mu węzeł scalenia, rozwidlenie i synchronizacja |
| `uml-04-state-order` | Maszyna stanów UML: przejścia ze zdarzeniami, warunkami dozoru i akcjami oraz stan złożony |
| `uml-05-use-case-shop` | Diagram przypadków użycia UML: aktorzy, granica systemu, relacje «include» i «extend» oraz uogólnienie aktorów |
| `uml-06-component-shop` | Diagram komponentów UML: komponenty, interfejsy dostarczane w postaci kółek na końcach linii oraz interfejsy wymagane |

Przykłady to pliki `.draw` w katalogu `corpus/` repozytorium. Służą również do testów i pomiarów wydajności: każdy z nich musi dać się wczytać bez ostrzeżeń, a każdy widok musi dać się rozmieścić z uwzględnieniem wszystkich wskazówek.

## Jak narysowano przykłady UML i BPMN

DiaDraw nie ma osobnych typów diagramów UML ani BPMN. Przykłady korzystają ze zwykłych konstrukcji języka DSL:

- **Diagramy klas** korzystają z rodzaju `class` z właściwościami `prop "uml.attributes"` i `prop "uml.methods"` (po jednym składniku w wierszu, z `\n` jako separatorem) oraz `prop "stereotype"`. Liczności określają właściwości relacji `prop "uml.sourceMultiplicity"` i `prop "uml.targetMultiplicity"`. Rodzaje linii (dziedziczenie, realizacja, kompozycja itd.) określają bloki stylów relacji dla danego tagu, wykorzystujące puste i wypełnione [zakończenia linii](line-styles.md).
- **Diagramy sekwencji** to widoki z `layout sequence` (zobacz [dokumentację DSL](../dsl.md#widoki-sekwencji)).
- **Diagramy czynności i stanów** korzystają z podstawowych kształtów: `circle` dla węzłów początkowych i końcowych, `bar` dla rozwidleń i synchronizacji, `rhombus` dla decyzji i scaleń oraz `rounded` dla stanów.
- **Diagramy przypadków użycia** przedstawiają aktorów za pomocą `shape actor`, a przypadki użycia — za pomocą `shape ellipse` wewnątrz elementu `system`, którego obrys wyznacza granicę systemu.
- **BPMN** korzysta z wbudowanej biblioteki `bpmn` (`use "bpmn@^1" as bpmn`) do przedstawiania zdarzeń, zadań, bramek i danych. Pule i tory to kształty `swimlane` w [widoku o stałym układzie](fixed-views.md) — tak narzędzia BPMN rozmieszczają elementy procesu. Zdarzenie brzegowe jest przypięte do krawędzi zadania i ma właściwość `prop "bpmn.attachedTo"`, dzięki której przy eksporcie do BPMN zostaje powiązane z zadaniem.

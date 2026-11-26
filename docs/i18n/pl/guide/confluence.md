# Diagramy w Confluence

DiaDraw dla Confluence Cloud dodaje do stron Confluence makro **DiaDraw diagram**. Tekst diagramu piszesz w edytorze makra, a strona pokazuje diagram narysowany w przeglądarce czytelnika przez ten sam rdzeń co aplikacje. Tekst jest zapisywany razem z makrem, więc każdy, kto może edytować stronę, może ponownie otworzyć diagram i go zmienić.

Aplikacja jest aplikacją Atlassian Forge. Nic nie jest rysowane na serwerze DiaDraw: tekst diagramu zostaje w Twojej witrynie Confluence.

## Dodawanie diagramu

1. Edytuj stronę, wpisz `/diadraw` i wybierz **DiaDraw diagram**.
2. Edytor otwiera się z małym przykładem. Zastąp go swoim diagramem: tekst jest po lewej, a rysunek po prawej odświeża się podczas pisania. Błędy są wymienione pod tekstem z numerem wiersza i kolumny; kliknij błąd, aby do niego przejść.
3. Wybierz w polu **View** widok do pokazania, gdy tekst ma ich kilka, a w polu **Theme** motyw.
4. Kliknij **Save** (lub Ctrl+S, ⌘S na Macu). **Cancel** zostawia makro bez zmian.

Aby później zmienić diagram, zaznacz makro w edytorze strony i kliknij jego przycisk **Edit**.

```diadraw render
model {
  author = person "Author"
  confluence = system "Confluence page" {
    macro = container "DiaDraw macro" tech "Forge app"
    core = container "DiaDraw core" tech "WebAssembly"
  }
  author -> confluence.macro "writes the text"
  confluence.macro -> confluence.core "draws it in the browser"
}

view page of confluence {
  include *
  layout layered direction right
}
```

## Wybór motywu

Lista motywów zawiera motywy wbudowane (`default`, `dark`, `mono`) i motywy zadeklarowane w Twoim tekście. Wybór motywu wpisuje do tekstu wiersz `theme`, ten sam, który wpisał(a)byś ręcznie, więc tekst nadal w pełni opisuje diagram. Wybór `default` usuwa ten wiersz. Jeśli widok ma własny wiersz `theme`, zmienia się właśnie on.

## Import diagramów Mermaid, PlantUML i innych

**Import…** zamienia diagram z innego narzędzia na tekst DiaDraw: wklej go lub wybierz plik i wskaż format (Mermaid, PlantUML, Structurizr DSL, draw.io lub BPMN 2.0). Import zastępuje tekst w edytorze, a Ctrl+Z przywraca poprzedni. To, czego importer nie zdołał przenieść, jest wymienione pod edytorem, aby można było dodać to ręcznie. To szybki sposób na przeniesienie diagramów z makr Mermaid lub PlantUML do DiaDraw.

## Zapisywanie diagramu jako obrazu

Po najechaniu na diagram na stronie w jego rogu pojawiają się dwa przyciski: **SVG** i **PNG** zapisują diagram do pliku. PNG jest rysowany w podwójnym rozmiarze, wyraźny na slajdach.

## Eksport strony

Eksporty PDF i Word w Confluence nie mogą uruchomić aplikacji, dlatego w eksporcie makro pokazuje tekst diagramu jako blok kodu pod nazwą widoku. Zapisz diagram jako SVG lub PNG i dołącz go, gdy eksport musi pokazywać obraz.

## Publikowanie diagramów z repozytorium

Gdy diagramy są plikami `.draw` w repozytorium, publikuj je na stronie zamiast wklejać do makr. Publikator rysuje każdy widok każdego pliku jako SVG i dołącza go do strony: `checkout.draw` staje się `checkout.svg` albo `checkout-<widok>.svg`, gdy plik ma kilka widoków. Każda kolejna publikacja dodaje nową wersję tylko tym załącznikom, których rysunek się zmienił, więc obrazy na stronie nadążają za repozytorium.

Wstaw załącznik na stronę raz (wyborem obrazu w Confluence albo opcją `--insert`, która dodaje na końcu strony obraz każdego nowego załącznika), a kolejne publikacje zaktualizują go w miejscu. Identyfikator strony to liczba w jej adresie URL po `/pages/`.

W terminalu, po zbudowaniu repozytorium (zob. [README aplikacji](../../src/apps/confluence/README.md)):

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net
export CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…   # from id.atlassian.com, Security, API tokens
node path/to/diadraw/src/apps/confluence/dist/cli.js publish docs/diagrams/*.draw --page 123456 --insert
```

`--view` publikuje tylko wskazane widoki, `--dry-run` wypisuje załączniki bez łączenia się z Confluence, a plik z błędami przerywa publikację i wypisuje błędy, tak jak `diadraw check`.

W GitHub Actions to samo przy każdym pushu robi akcja `DiaDraw to Confluence`. Konto i jego token API trzymaj w sekretach repozytorium:

```yaml
name: Diagrams to Confluence
on:
  push:
    branches: [main]
    paths: ['docs/diagrams/**']
jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mykulyak/diadraw/src/apps/confluence@main
        with:
          files: docs/diagrams/**/*.draw
          page-id: '123456'
          base-url: https://example.atlassian.net
          email: ${{ secrets.CONFLUENCE_EMAIL }}
          api-token: ${{ secrets.CONFLUENCE_API_TOKEN }}
```

Akcja przy każdym uruchomieniu buduje rdzeń DiaDraw, co trwa kilka minut, dopóki rdzeń nie zostanie opublikowany jako pakiet.

## Instalacja aplikacji

Aplikacji nie ma jeszcze w Atlassian Marketplace. Administrator witryny instaluje ją z repozytorium DiaDraw narzędziem wiersza poleceń Forge; kroki opisuje [src/apps/confluence/README.md](../../src/apps/confluence/README.md).

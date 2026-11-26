# Діаграми в Confluence

DiaDraw для Confluence Cloud додає на сторінки Confluence макрос **DiaDraw diagram**. Ви пишете текст діаграми в редакторі макроса, а сторінка показує діаграму, яку малює в браузері читача те саме ядро, що й у застосунках. Текст зберігається разом із макросом, тож кожен, хто може редагувати сторінку, може знову відкрити діаграму й змінити її.

Застосунок створено на Atlassian Forge. На сервері DiaDraw нічого не малюється: текст діаграми лишається на вашому сайті Confluence.

## Як додати діаграму

1. Відредагуйте сторінку, введіть `/diadraw` і виберіть **DiaDraw diagram**.
2. Редактор відкриється з невеликим прикладом. Замініть його своєю діаграмою: текст ліворуч, а малюнок праворуч оновлюється під час введення. Помилки показано під текстом із рядком і стовпцем; клацніть помилку, щоб перейти до неї.
3. У полі **View** виберіть подання, якщо в тексті їх кілька, а в полі **Theme** — тему.
4. Натисніть **Save** (або Ctrl+S, ⌘S на Mac). **Cancel** лишає макрос без змін.

Щоб згодом змінити діаграму, виберіть макрос у редакторі сторінки й натисніть його кнопку **Edit**.

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

## Вибір теми

У списку тем є вбудовані теми (`default`, `dark`, `mono`) і ті, що оголошує ваш текст. Вибір теми записує в текст рядок `theme` — той самий, який ви ввели б вручну, — тож текст і далі повністю описує діаграму. Вибір `default` прибирає цей рядок. Якщо подання має власний рядок `theme`, змінюється саме він.

## Імпорт діаграм Mermaid, PlantUML та інших

**Import…** перетворює діаграму з іншого інструмента на текст DiaDraw: вставте її або виберіть файл і вкажіть формат (Mermaid, PlantUML, Structurizr DSL, draw.io або BPMN 2.0). Імпорт замінює текст у редакторі, а Ctrl+Z повертає попередній. Те, що імпорт не зміг перенести, показано під редактором, щоб ви могли додати це вручну. Так можна швидко перенести діаграми з макросів Mermaid чи PlantUML у DiaDraw.

## Як зберегти діаграму як зображення

Наведіть вказівник на діаграму на сторінці, і в її куті з’являться дві кнопки: **SVG** і **PNG** зберігають діаграму у файл. PNG малюється вдвічі більшим за діаграму, щоб був чітким на слайдах.

## Експорт сторінки

Експорт Confluence у PDF і Word не може запустити застосунок, тож в експорті макрос показує текст діаграми блоком коду під назвою її подання. Збережіть діаграму як SVG або PNG і долучіть її, коли експорт має показувати зображення.

## Публікація діаграм із репозиторію

Якщо ваші діаграми — це файли `.draw` у репозиторії, публікуйте їх на сторінку замість вставляння в макроси. Публікатор малює кожне подання кожного файлу як SVG і долучає його до сторінки: `checkout.draw` стає `checkout.svg`, або `checkout-<подання>.svg`, якщо у файлі кілька подань. Кожна наступна публікація додає нову версію лише тим вкладенням, малюнок яких змінився, тож зображення на сторінці встигають за репозиторієм.

Вставте вкладення на сторінку один раз (вибором зображення в Confluence або параметром `--insert`, який додає в кінець сторінки зображення кожного нового вкладення), і наступні публікації оновлюватимуть його на місці. Ідентифікатор сторінки — це число в її URL після `/pages/`.

У терміналі, зі зібраним репозиторієм (див. [README застосунку](../../src/apps/confluence/README.md)):

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net
export CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…   # from id.atlassian.com, Security, API tokens
node path/to/diadraw/src/apps/confluence/dist/cli.js publish docs/diagrams/*.draw --page 123456 --insert
```

`--view` публікує лише названі подання, `--dry-run` показує вкладення без звертання до Confluence, а файл із помилками зупиняє публікацію й показує помилки, як `diadraw check`.

У GitHub Actions те саме під час кожного push робить дія `DiaDraw to Confluence`. Зберігайте обліковий запис і його API-токен у секретах репозиторію:

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

Дія щоразу збирає ядро DiaDraw, що триває кілька хвилин, доки ядро не опубліковано як пакет.

## Встановлення застосунку

Застосунку ще немає в Atlassian Marketplace. Адміністратор сайту встановлює його з репозиторію DiaDraw за допомогою інструмента командного рядка Forge; кроки описано в [src/apps/confluence/README.md](../../src/apps/confluence/README.md).

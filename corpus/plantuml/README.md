# PlantUML test corpus

Real PlantUML files used to measure the coverage of the PlantUML importer (`node cpp/tools/import-coverage.mjs corpus/plantuml`). Test data only.
Files are unchanged and keep their folder layout so relative `!include`s still resolve. Each
folder holds the licence of its source.

| Folder                              | Source                                                                                          | Commit    | Licence      |
| ----------------------------------- | ----------------------------------------------------------------------------------------------- | --------- | ------------ |
| `C4-PlantUML/samples`, `…/percy`    | [plantuml-stdlib/C4-PlantUML](https://github.com/plantuml-stdlib/C4-PlantUML)                   | `f998c46` | MIT          |
| `Azure-PlantUML`                    | [plantuml-stdlib/Azure-PlantUML](https://github.com/plantuml-stdlib/Azure-PlantUML) `samples/`  | `f360835` | MIT          |
| `puml-themes`                       | [bschwarz/puml-themes](https://github.com/bschwarz/puml-themes)                                 | `01a2191` | MIT          |
| `tupadr3-examples`, `common.puml`   | [tupadr3/plantuml-icon-font-sprites](https://github.com/tupadr3/plantuml-icon-font-sprites) `examples/` | `1826ccd` | MIT |
| `josudoey-puml-tutorial`            | [josudoey/puml-tutorial](https://github.com/josudoey/puml-tutorial)                             | `998e7aa` | BSD-2-Clause |
| `mweagle-C4-PlantUML-Themes`        | [mweagle/C4-PlantUML-Themes](https://github.com/mweagle/C4-PlantUML-Themes)                     | `7bac284` | MIT          |
| `jabrena-plantuml-examples`         | [jabrena/plantuml-examples](https://github.com/jabrena/plantuml-examples)                       | `69fea15` | Apache-2.0   |
| `okunev-e-plantuml-examples`        | [okunev-e/plantuml_examples](https://github.com/okunev-e/plantuml_examples)                     | `e92dc29` | MIT          |
| `paweltarkowski-plantuml-examples`  | [paweltarkowski/plantuml-examples](https://github.com/paweltarkowski/plantuml-examples)         | `937c282` | MIT          |
| `jenokarthic-plantuml`              | [jenokarthic/plantuml](https://github.com/jenokarthic/plantuml)                                 | `64e3403` | MIT          |
| `fdb7772ef57e1013-UML-PlantUML`     | [fdb7772ef57e1013/UML-PlantUML](https://github.com/fdb7772ef57e1013/UML-PlantUML)               | `dbf96f7` | MIT          |
| `gabrieljoelc-plantuml-themes`      | [gabrieljoelc/plantuml-themes](https://github.com/gabrieljoelc/plantuml-themes)                 | `1213105` | MIT          |

Not committed (licence unclear): joelparkerhenderson/plantuml-examples and the examples of
awslabs/aws-icons-for-plantuml. They are kept in the git-ignored `corpus/local-only/plantuml/`
when present and only count toward coverage numbers.

# Structurizr test corpus

Real Structurizr DSL and workspace JSON files used to measure the coverage of the Structurizr importer (`node cpp/tools/import-coverage.mjs corpus/structurizr`). Test data only. Files are unchanged and
keep their folder layout so relative `!include`s and `workspace extends` still resolve. Only
`.dsl` files and `.json` files holding a workspace `model` were copied (no themes, docs or
images). Each folder holds the licence of its source.

| Folder                                      | Source                                                                                                  | Commit    | Licence    |
| ------------------------------------------- | ------------------------------------------------------------------------------------------------------- | --------- | ---------- |
| `structurizr_structurizr`                   | [structurizr/structurizr](https://github.com/structurizr/structurizr) (DSL and client test resources)   | `e3a736a` | Apache-2.0 |
| `MaikJaek_examples`                         | [MaikJaek/examples](https://github.com/MaikJaek/examples)                                               | `b5ef910` | Apache-2.0 |
| `avisi-cloud_structurizr-site-generatr`     | [avisi-cloud/structurizr-site-generatr](https://github.com/avisi-cloud/structurizr-site-generatr)       | `450f252` | Apache-2.0 |
| `dirkgroot_structurizr-dsl-intellij-plugin` | [dirkgroot/structurizr-dsl-intellij-plugin](https://github.com/dirkgroot/structurizr-dsl-intellij-plugin) | `0ef8ef3` | MIT        |
| `docToolchain_Bausteinsicht`                | [docToolchain/Bausteinsicht](https://github.com/docToolchain/Bausteinsicht)                             | `1baedbc` | MIT        |
| `max-arshinov_masad`                        | [max-arshinov/masad](https://github.com/max-arshinov/masad)                                             | `6088e6e` | MIT        |

Not copied: structurizr/java (archived; its test resources duplicate structurizr/structurizr).
Files whose licence is unclear go in the git-ignored `corpus/local-only/structurizr/` and only
count toward coverage numbers.

Some files are deliberately broken (the DSL parser's own error tests, workspace validation
fixtures) or refer to remote themes, includes and workspaces. The importer reports those as
dropped or unresolved; it never fetches them.

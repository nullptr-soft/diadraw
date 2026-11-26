# BPMN test corpus

Real BPMN 2.0 files used to measure the coverage of the BPMN importer (`node cpp/tools/import-coverage.mjs corpus/bpmn`). Test data only. Files are unchanged and keep
their folder layout. Each folder holds the licence of its source.

| Folder                           | Source                                                                                       | Commit    | Licence                                      |
| -------------------------------- | -------------------------------------------------------------------------------------------- | --------- | -------------------------------------------- |
| `bpmn-io_bpmn-js`                | [bpmn-io/bpmn-js](https://github.com/bpmn-io/bpmn-js) `test/fixtures`, `test/spec`           | `e7b7435` | MIT (bpmn.io licence; see below)             |
| `bpmn-io_bpmn-moddle`            | [bpmn-io/bpmn-moddle](https://github.com/bpmn-io/bpmn-moddle) `test/fixtures`                | `f35959a` | MIT                                          |
| `bpmn-miwg_bpmn-miwg-test-suite` | [bpmn-miwg/bpmn-miwg-test-suite](https://github.com/bpmn-miwg/bpmn-miwg-test-suite) `Reference` | `85240b8` | CC-BY-3.0, © the BPMN Model Interchange Working Group |

bpmn-js's licence is MIT with an added clause that the bpmn.io watermark in rendered diagrams
must stay visible. It concerns the bpmn-js viewer code; DiaDraw only reads these XML test
fixtures and uses none of that code.

Some files are deliberately invalid (bpmn-js and bpmn-moddle error tests, `*.part.bpmn`
fragments without `<definitions>`); the importer reports them rather than failing.

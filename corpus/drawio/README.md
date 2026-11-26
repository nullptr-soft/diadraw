# draw.io test corpus

Real draw.io files used to measure the coverage of the draw.io importer (`node cpp/tools/import-coverage.mjs corpus/drawio`). They are test data only; DiaDraw does not ship them.

| Folder             | Source                                                                                   | Licence                                   |
| ------------------ | ---------------------------------------------------------------------------------------- | ----------------------------------------- |
| `drawio-diagrams/` | [jgraph/drawio-diagrams](https://github.com/jgraph/drawio-diagrams) at commit `cc78dc2e` | Apache-2.0 (`drawio-diagrams/LICENSE`)    |
| `templates/`       | [jgraph/drawio](https://github.com/jgraph/drawio) `src/main/webapp/templates` at commit `3e707fd2` | CC BY 4.0 (`templates/LICENSE`) |

File names are the original paths with `/` and spaces replaced by `_`. The files are unchanged.

- `templates/`: every template diagram (`index.xml` and the preview PNGs left out).
- `drawio-diagrams/`: every fourth `.drawio`/`.xml` file under 100 KB (in path order), plus files
  that exercise other forms: `diagrams_flowchart.drawio.svg` and `diagrams_connect-shapes.svg`
  (diagram embedded in SVG, escaped and URL-encoded), `diagrams_from-ochre-to-online.drawio.png`
  (diagram in a PNG text chunk), `training-diagrams_HR_Recruiting_FlowchartB_start.vsdx` (Visio,
  reported as unsupported) and `blog_custom-templates.xml` (a shape library, reported as not a
  diagram).

The templates are © JGraph Ltd and contributors, licensed under the Creative Commons Attribution
4.0 International licence; the drawio-diagrams files are © JGraph Ltd, licensed under the Apache
License 2.0.

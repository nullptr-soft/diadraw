# Examples

Every app ships a set of example diagrams to open, read and change. On macOS, Linux and Windows
they are under **File › Examples**; in the web app they are in the **Examples…** list in the
toolbar. Choosing one opens it as a new, untitled document, so nothing you change touches the
example itself; save it under a name of your own to keep it.

| Example                       | What it shows                                                                                                                                                          |
| ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bpmn-01-order-process`       | A BPMN process in one pool with two lanes: message start, user, send, manual and service tasks, exclusive and parallel gateways, a timer boundary event, a data output |
| `bpmn-02-claim-collaboration` | A BPMN collaboration of two pools with message flows, an event-based gateway, a business rule task, a data store and a text annotation                                 |
| `c4-01` … `c4-04`             | C4 system and container diagrams, groups, a freeform note                                                                                                              |
| `flow-01` … `flow-04`         | Flowcharts with decisions and loops                                                                                                                                    |
| `uml-01-class-library`        | A UML class diagram: attributes and operations, an interface, an abstract class, inheritance, realization, composition, aggregation, multiplicities                    |
| `uml-02-sequence-checkout`    | A UML sequence diagram: activations, a `loop` and an `alt` frame, replies, an asynchronous message, a note                                                             |
| `uml-03-activity-order`       | A UML activity diagram: initial and final nodes, a decision and its merge, a fork and a join                                                                           |
| `uml-04-state-order`          | A UML state machine: transitions with events, guards and actions, a composite state                                                                                    |
| `uml-05-use-case-shop`        | A UML use case diagram: actors, a system boundary, «include» and «extend», actor generalization                                                                        |
| `uml-06-component-shop`       | A UML component diagram: components, provided interfaces as lollipops, required interfaces                                                                             |

The examples are the `.draw` files in the repository's `corpus/` folder, which also serve as
test and benchmark documents: every one of them must parse without a warning and lay out every
view with all its hints honoured.

## How the UML and BPMN examples are drawn

DiaDraw has no separate UML or BPMN diagram types; the examples use the ordinary DSL:

- **Class diagrams** use the `class` kind with `prop "uml.attributes"` and `prop "uml.methods"`
  (one member per line, `\n` between them) and `prop "stereotype"`; multiplicities are
  `prop "uml.sourceMultiplicity"` and `prop "uml.targetMultiplicity"` on a relationship. The
  kinds of line (inheritance, realization, composition, …) are relationship style blocks for a
  tag, using the hollow and filled [line ends](line-styles.md).
- **Sequence diagrams** are `layout sequence` views (see the [DSL reference](../dsl.md#sequence-views)).
- **Activity and state diagrams** use base shapes: `circle` for initial and final nodes, `bar`
  for forks and joins, `rhombus` for decisions and merges, `rounded` for states.
- **Use case diagrams** draw actors with `shape actor` and use cases with `shape ellipse`
  inside a `system` element, which is drawn as the system boundary.
- **BPMN** uses the built-in `bpmn` library (`use "bpmn@^1" as bpmn`) for events, tasks,
  gateways and data, with pools and lanes as `swimlane` shapes in a [fixed view](fixed-views.md),
  which is how BPMN tools lay a process out. A boundary event is an event pinned on its task's
  edge, with `prop "bpmn.attachedTo"` so a BPMN export attaches it.

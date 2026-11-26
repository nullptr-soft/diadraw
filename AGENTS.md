# Working on DiaDraw

Start with the [repository map](README.md), the [C++ core and build guide](src/README.md),
and the [design decisions](docs/decisions.md).

## Branches and pull requests

- Work in a fresh cloud clone of `github.com/mykulyak/diadraw`, not the owner's local checkout.
  Start from the latest `main` on a branch named `claude/<short-topic>`. Keep one change per
  branch and pull request; split unrelated work.
- Write an imperative commit summary, then a blank line and the reason for the change.
- Push the branch and open a pull request against `main`. Explain what changed and why,
  list the checks that passed, and identify anything that still needs local testing.
- Do not push directly to `main` or rewrite another contributor's branch. The owner merges.
  Start the next change from the updated `main`.

## Independent C++ review

Every C++ change, including changes to the core, CLI, or native shells, needs a fresh reviewer.
The author must not review their own change. Use the commands below when available; otherwise
have a separate agent or session follow the linked checklist.

| When                                                     | Reviewer and checklist                                                                                                                                                       |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Every C++ change                                         | `cpp-reviewer`: [/cpp-review](.claude/commands/cpp-review.md) checks correctness, resource ownership, undefined behavior, C++20 idioms, and repository conventions.          |
| A hot path changes or the change claims to improve speed | `cpp-perf-reviewer`: [/cpp-perf-review](.claude/commands/cpp-perf-review.md) also checks complexity, allocations, copies, build cost, binary size, and performance evidence. |

Hot paths include DSL parsing and patching, `resolve_view`, layout, rendering, canvas
hit-testing, and importers. The [reviewer definitions](.claude/agents/) describe each role.
Pass a branch, file, directory, commit range, or PR as the target. Without a target, review
the uncommitted diff or, if there is none, the branch's commits since `main`.

Reviewers report blockers, warnings, nitpicks, and positives with `file:line` references.
Fix each blocker and warning or explain in the PR why it does not apply. After nontrivial
fixes, request another round with the previous findings and the commit to review from.
List the rounds and fixes in the PR before handing it to the owner.

## Building and testing

Run these checks from the repository root in the cloud environment (CMake, Ninja, a C++20
compiler, and Node 22):

```bash
cmake --workflow --preset core     # core, CLI, and tests with ASan/UBSan, no native app
npm ci && npm run lint && npm run typecheck && npm test
```

The C++ build fetches pinned dependencies (miniz, lunasvg, and googletest) from `github.com`
and `codeload.github.com`. The WASM preset requires emsdk; build the WASM module before the
web app.

The gtkmm app builds in the cloud too, when the environment also allows the Ubuntu package
mirrors: after `sudo apt-get install -y libgtkmm-4.0-dev libcurl4-openssl-dev xvfb`,
`xvfb-run -a cmake --workflow --preset dev` builds it and runs its gesture tests under a virtual
display.

The owner checks the macOS AppKit app, the WinUI app, and signed-in workflows on their
machines. If a change touches one of these, say exactly what needs checking before merge.

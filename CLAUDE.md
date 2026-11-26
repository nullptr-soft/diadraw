# Working on DiaDraw with Claude

Read [README.md](README.md) for the repository map and [src/README.md](src/README.md) for the
C++ core and its build. Design decisions are in [docs/decisions.md](docs/decisions.md).

## Workflow

- Claude works in cloud sessions on a fresh clone of `github.com/mykulyak/diadraw`, not in the
  checkout on the owner's Mac.
- Start every change from the latest `main` on its own branch named `claude/<short-topic>`, for
  example `claude/reconnect-edge`. One branch and one pull request per piece of work.
- Push the branch and open a pull request against `main`. Keep it small enough to review; split
  unrelated changes. Do not push to `main` directly and do not rewrite history on someone else's
  branch.
- Write commit messages as one imperative summary line (like the existing history, e.g.
  "Reconnect an edge to another element from the canvas"), a blank line, then the why.
- The pull request description says what changed, why, and how it was tested: which commands
  ran and passed, and what could not be run in the cloud (see below).
- The owner merges. After a merge, start the next change from the new `main`.

## Reviewing C++ changes

Two review commands live in [.claude/commands/](.claude/commands/), each run by its own reviewer
agent from [.claude/agents/](.claude/agents/):

| Command                     | Agent               | Reviews                                                                                               |
| --------------------------- | ------------------- | ----------------------------------------------------------------------------------------------------- |
| `/cpp-review [target]`      | `cpp-reviewer`      | correctness, RAII, UB, modern C++20, AI blind spots and this repository's conventions                 |
| `/cpp-perf-review [target]` | `cpp-perf-reviewer` | complexity, allocations, copies, hot paths, build cost and binary size, and evidence for speed claims |

The target is a branch, file, directory, commit range or pull request; without one they review
the uncommitted diff, or the branch's commits since `main`. Each returns blockers, warnings,
nitpicks and positives, with `file:line` for every finding.

The rule for every change to C++ code (the core under `src/`, the CLI, the native shells):

- The agent that wrote the change never reviews it. When the change is done, a separate, fresh
  agent (the `cpp-reviewer` subagent, or another session) runs `/cpp-review` on it.
- Run `/cpp-perf-review` the same way, with `cpp-perf-reviewer`, when the change touches a hot
  path (DSL parsing and patching, `resolve_view`, layout, rendering, canvas hit-testing, importers)
  or claims to be faster.
- The author fixes every blocker and warning, or says in the pull request why one does not
  apply, and asks for another round (`round 2 since <commit>; earlier findings: …`) when the
  fixes were not trivial. Only then is the pull request ready for the owner's review.
- The pull request description lists the review rounds and what was fixed.

## Building and testing in the cloud

The cloud container has CMake, Ninja, a C++20 compiler and Node 22. From the repository root:

```bash
cmake --workflow --preset core     # core, CLI and tests with ASan/UBSan, no native app
npm ci && npm run lint && npm run typecheck && npm test
```

The C++ build downloads its pinned dependencies (miniz, lunasvg, googletest) from github.com, so
the session's cloud environment must allow `github.com` and `codeload.github.com`. The WASM
preset needs emsdk, and the web build needs the WASM module.

The gtkmm app builds in the cloud too, when the environment also allows the Ubuntu package
mirrors: after `sudo apt-get install -y libgtkmm-4.0-dev libcurl4-openssl-dev xvfb`,
`xvfb-run -a cmake --workflow --preset dev` builds it and runs its gesture tests under a virtual
display.

These need the owner's machines and cannot run in the cloud: the macOS AppKit app, the WinUI
app, and anything that needs a signed-in account. Say in the pull request which of these the
change touches so they get checked locally before merging.

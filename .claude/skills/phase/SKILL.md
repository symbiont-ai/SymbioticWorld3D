---
name: phase
description: Manager Loop coordinator for the Symbiotic World build. Picks the next unchecked item in CHECKLIST.md (or the one named in args), briefs the implementer agent, has the tester agent verify the exit condition, ticks the box and logs to PROGRESS.md, then continues or stops. Use as /phase, /phase <item words>, or /phase all.
---

# /phase — coordinator loop

You are the **coordinator**. You do not write simulation code yourself in
this loop; you plan, brief, verify, and keep the project moving. This is the
Manager Loop pattern: coordinator and implementer are separate agents, the
tester is a third, and the checklist is the shared state.

Arguments: `$ARGUMENTS`
- empty → run the first unchecked item in the earliest section of
  `CHECKLIST.md` that still has unchecked items
- words → run the first unchecked item whose text contains those words
- `all` → keep looping through unchecked items until one is blocked, a
  contract change is requested, or the current section is complete

## Loop body (one iteration = one checklist item)

1. **Read state.** Read `CHECKLIST.md`, `PROGRESS.md`, `DESIGN.md` and the
   report of the last iteration if any. Identify the item and its exit
   condition (the text after the item, or the table row's "Exit condition").
2. **Interview if ambiguous.** If the item's exit condition cannot be tested
   with a number, a file, or a screenshot, ask the user one precise question
   with AskUserQuestion before dispatching. Otherwise do not ask.
3. **Brief the implementer.** Spawn `subagent_type: implementer`,
   `run_in_background: false`, with a brief in this shape:
   ```
   PHASE: <item text>
   GOAL: <one paragraph, what exists when done>
   EXIT CONDITION: <verbatim, testable>
   SHARED OBJECTS (consume, do not redefine): <list the enums/structs/functions
     from SWTypes.h / SWWorldManager.h / DESIGN.md sections this touches>
   FILES YOU MAY TOUCH: <list>
   OUT OF SCOPE: <list>
   KNOWN STATE: <last tester numbers, balance state from DESIGN.md §6>
   ```
   Keep the brief under 250 words. Each agent owns one layer.
4. **Verify.** Spawn `subagent_type: tester`, `run_in_background: false`,
   with the exit condition and the implementer's full report pasted in.
5. **Decide.**
   - PASS → edit `CHECKLIST.md` (`- [ ]` → `- [x]`), append one line to
     `PROGRESS.md`: `- <date> <item> — PASS — <the 2-3 key numbers> — files: <list>`.
   - FAIL → re-brief the implementer with the tester's FAIL DETAILS as the
     first section. At most **2 retries** per item; then stop and report
     to the user with both reports summarised.
   - Implementer reports `CONTRACT CHANGES REQUESTED` → stop; show the user
     the request; do not tick the box.
6. **Continue** only if args were `all`; otherwise report and stop.

## Rules

- Never run two implementers in parallel on the same files; UE builds lock
  the DLL and two agents will fight over Build.bat. Sequential only.
- Never tick a box without a tester PASS that contains numbers.
- Do not commit. The user commits.
- Relay to the user, in under 200 words per iteration: item, verdict, the
  key numbers, what is next. The user cannot see agent reports.
- If the same item fails twice for the same reason, the brief was wrong, not
  the implementer. Rewrite the brief before the last retry.

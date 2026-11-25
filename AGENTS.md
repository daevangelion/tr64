# Agent rules

Read `docs/PROVENANCE.md` and the sections of `docs/architecture.md` your
brief names before anything else.

- Absolute paths. Repository: `<repo>`.
- Sources you may and may not use: `docs/PROVENANCE.md` sections 1 and 2.
- Read contracts as committed (`git show main:<path>` or your own worktree
  after merging main). An uncommitted file in the main working tree is
  somebody's draft.
- Game data: `$TR64_DATA/pc/` (PC `*.phd`) and
  `$TR64_DATA/psx/` (PlayStation `PSXDATA/*.PSX`), read-only.
  Never copy it into the repository or into a commit.
- Scratch only under `$TR64_WORK/{builds,tmp}/<your name>`, with
  `TMPDIR` there. `$TR64_WORK` is nearly full: delete your scratch when you
  finish and never write build output inside the repository.
- `$TR64_ARES_LOCKS/` holds the ares slot locks shared by every
  cart run on this host. Never delete it or anything in it. Never start
  ares by hand; use `test/run/ares.sh`.
- Parallel slices work in `git worktree`s under
  `$TR64_WORK/wt/<slice>` on branch `<milestone>-<slice>`. Only the
  integrator merges to `main`.
- Shared files (`Makefile`, `include/tr64/*.h`, `engine/core/eng.*`,
  `engine/mem/*`) have one owner named in the brief. Ask the owner
  (`write agent://<name>`); do not edit them.
- Commits: author and committer `daevangelion
  <150335908+daevangelion@users.noreply.github.com>`; `git commit -- <own
  paths>`; never `git add -A`; no rebase, reset, stash or push; `make
  provcheck` passes first.
- Commit to your own branch when a piece builds and its checks pass; no
  review request is needed for that. Main arranges one independent review
  per slice on working code (it looks for defects, not missing tests) and
  merges to `main`. Ask Main for a review before committing only when you
  change behaviour in the allocator, the loader, paging/code units, or
  the harness's verdict logic.
- Verification follows `docs/architecture.md` section 9: host tests and
  affected scenes only; cart scenes on ares at 4 MiB through the harness.
  For a change run what it can affect: its own directed script with its
  negative control and the 25 fixtures; for an oracle hunk also the
  identity check on the fixtures and the cutscenes. Nothing else.
- Broad batteries (the 16-level campaign, the block, switch, movement and
  combat sweeps, the cart sweep, `m0.sh`, the shipping gates, coverage)
  run once per merge set, when Main asks for them, on <remote-host>
  (`TR64_ARES_HOST=<remote-host>`, `test/run/host-remote.sh`): not per hunk, not
  per commit, not "to be safe". A brief that asks for more than this is
  wrong: say so. Batch oracle hunks into one publication. Kill your
  background runs when their result is no longer needed.
- Concurrency is capped: <remote-host> is the user's machine and other projects
  run there. At most 4 host-suite processes of yours on <remote-host> at once
  (`host-remote.sh`), at most 2 on this host; emulator runs only through
  the harness's slots. If a job does not finish within that, cut the
  job (sample it), not the cap, and say what was left out. Before your
  final report, check that nothing of yours is still running on either
  machine and kill what is.
- If a document is missing, unclear or contradicted by a measurement,
  write to Main (`write agent://Main`) and continue with other work. A
  measurement overrides a document; do not silently change either.
- Never weaken a budget, a limit or a gate threshold to pass. Report the
  measured number.
- No level-hardcoded gameplay: never branch gameplay, object dispatch,
  physics, effects, AI, or validation on a level name/ID or hardcoded
  item-table slot indices (e.g. `levelId == 2`, `strcmp(name, "boat")`,
  `item == 26`). Drive all object mechanics and validation generically
  from object types and authored level data, using dynamic pools for
  per-instance state. Only per-level campaign/script metadata (e.g.
  starting stock, secret bonuses) belongs in a declarative level-script
  table, and level-specific item indices belong strictly in test/oracle
  trace serializers (`game/trace.c`).
- Prefix shell commands with `rtk`.
- Final report: status by brief item, commit ids, the exact commands you
  ran with their observed results, and what you did not verify.

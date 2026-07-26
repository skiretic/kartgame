# The save migration corpus

Real captured profile saves, **one file per historical format version**, checked
in and never regenerated. `src/core/profile.h` is the format;
[ADR-0042](../../../docs/DECISIONS.md#adr-0042--a-save-always-loads-and-the-migration-tests-eat-real-old-files)
is why this directory exists.

## This directory is permanent and append-only

**Deleting anything from here is a defect, not housekeeping.** Regenerating a file
is the same defect with better intentions.

A migration test that generates its own v1 input tests nothing. It round-trips
today's writer through today's reader, which passes forever — including on the day
the migration is wrong, because the writer and the reader agree with each other by
construction and neither of them has ever seen a real old file. The only thing
that catches a broken `v1 -> v2` step is a v1 file that was written before v2
existed.

So the corpus only ever grows. A file in here is evidence, and evidence does not
get tidied up.

## What is here

| File | Version | Notes |
| --- | --- | --- |
| `v1.save` | 1 | Hand-authored, the start of the history. See below. |

`v1.save` is the honest start of a history rather than an invented past. It was
written by hand — a real file with real values, typed out, not produced by
`format_profile` — and committing it is what makes it a capture. Nothing has been
regenerated since.

There is no `v2.save` or `v3.save`, and there will not be until versions 2 and 3
actually exist. Inventing a fictional old version to have something to migrate
would put a fake file in a directory whose entire value is that its files are
real.

## Adding a file when the version bumps

When `PROFILE_FORMAT_VERSION` goes from N to N+1:

1. **Before** changing the writer, capture a real save from the current build —
   play far enough that the career, the standings and at least one best lap are
   all populated, then copy `user://profile.save` to `tests/data/saves/vN.save`.
   A capture taken after the writer changed is not a capture of version N.
2. Bump `PROFILE_FORMAT_VERSION` and add the `{ N, N+1, apply, summary }` entry to
   `PROFILE_MIGRATIONS`. The migration may be the identity function
   (`profile_migrate_identity`) and that is a supported answer, not a smell.
3. Add a test that loads `vN.save` and asserts the migrated result field by field
   — including the fields the migration was supposed to add or rename. Asserting
   only "it loaded" would pass on a migration that defaulted everything.
4. Leave every earlier file alone. The v1 test keeps running, and it is now
   exercising an N-step chain rather than a one-step one, which is exactly the
   coverage that a single-step-only test set would lose.

## The byte-identity assertion, and why a comment is a format change

`test_profile.cpp` asserts that `v1.save` is **byte-identical** to what
`format_profile` produces from the profile it parses to. That is deliberately
strict: it means changing anything the writer emits, down to a word in the comment
preamble, fails the suite until the version is bumped and a new file is captured.

ADR-0042 chose to bump on *every* format change rather than only on incompatible
ones, because the alternative asks somebody to correctly classify a change as
compatible at the moment they are thinking about something else. This assertion is
that rule made mechanical, and the cost is that the preamble is not a place to
write prose.

## Reading a file from a test

`tests/run.sh` runs the binary from the project root, but nothing may depend on
that: the paths come from `__FILE__` at compile time instead, which `run.sh`
passes absolutely. See `corpus_path()` in `tests/core/test_profile.cpp`.

# Save fixtures

Pumpkin keeps no save file, but two things hold carvings that people expect to come back: the
porch, which the page keeps in the browser (`localStorage` key `pumpkin-porch-v1`, a JSON list of
carving codes), and every `#p=` link that has been shared. Both are carving codes
(`encode`/`decode` in `src/carve/carving.h`), so the code is the save format, and its first byte is
`kCodeVersion`.

Each file here is a porch exactly as the page stores it, written by one code version, and kept so
that every later version of Pumpkin is tested against it: `tests/test_saves.cpp` decodes every
pumpkin on it and checks what it holds (variety, pieces, cuts and a fingerprint of every decoded
value). A change that cannot read one of these fails the build.

| File | Code version | Written by | What it holds |
|---|---|---|---|
| `v1-porch.save` | 1 | `make_save_fixtures` (2026-11-20; the format the arcade has served since 2026-10-30) | Five pumpkins: one uncarved, the classic face (two eyes, nose, grin), freehand cuts only (a hole and a slit), all fourteen face pieces, and a long wavy cut among stamps |

The file is written through `encode` (so it is exactly what the page stores) by
`tools/make_save_fixtures.cpp`:

```
cmake --build build --target make_save_fixtures
./build/make_save_fixtures tests/fixtures
```

That writes the current code version's file and refuses to overwrite one. When `kCodeVersion` is
raised, run it once to add the new version's fixture, keep decoding the older versions, and never
regenerate an older version's file: it stands for porches and links people already have.

# Reference-screenshot harness

This fork of ScummVM adds two one-shot rendering harnesses:

* **MM/Xeen harness:** consumed by `mm5e`, a Go re-port of *Might and Magic IV: Clouds of Xeen*. Sources live under `engines/mm/xeen/`.
* **KYRA/EOB2 harness:** consumed by `griddelve`, a Go EOB2-inspired engine. Sources live under `engines/kyra/engine/`.

Both harnesses share the CLI parsing, the headless-force machinery, and the engine-agnostic core flags `--screenshot`, `--level`, `--cell`, `--facing`, `--no-actors`. Engine-specific extras live under the `--mm-*` namespace for MM/Xeen and under stock ScummVM flags (`--save-slot`) for KYRA/EOB2.

## Invocation

MM/Xeen (Might and Magic IV target):

```bash
~/src/scummvm/scummvm \
    --path="/path/to/Might and Magic 4-5/" \
    --extrapath=/home/hqz/src/scummvm/dists/engine-data \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/mm.png \
    --level=28 --cell=8,8 --facing=N \
    worldofxeen
```

KYRA/EOB2 (Eye of the Beholder II target):

```bash
~/src/scummvm/scummvm \
    --path="/path/to/EOB2/" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/eob.png \
    --level=1 --cell=15,20 --facing=N \
    eob2
```

`--extrapath` points at `dists/engine-data/` which ships `mm.dat` (the MM/Xeen engine-data file). Drop it if `mm.dat` is installed system-wide. The audio flags silence the engine; ScummVM does not expose `--music-mute` style flags as CLI options.

The harness auto-forces `SDL_VIDEODRIVER=dummy` when `--screenshot`, `--mm-scale-test`, `--mm-screenshot-prefix`, `--eob-dump-state`, `--eob-batch` or `--eob-fire-triggers` is on the command line, so no game window pops up regardless of whether a real X / Wayland display is present. `--eob-play-sequence` is not on that list, which keeps the shared `posix-main.cpp` untouched; its callers set `SDL_VIDEODRIVER=dummy` themselves. To pick a different driver (e.g. `offscreen`), set `SDL_VIDEODRIVER=offscreen` explicitly; the harness only sets the env var when none is provided.

## Shared flags

These flags are parsed in `base/commandLine.cpp` and consumed by whichever engine the loaded target boots into. Each harness validates them against its own coordinate space.

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--screenshot` | yes | absolute filesystem path | Output PNG. Must be writable. Presence enables harness mode. |
| `--level` | yes (see KYRA save-slot exception) | integer | Map/level ID. Range is engine-specific: 1-128 for MM/Xeen, 1-16 for EOB2. |
| `--cell` | yes (see KYRA save-slot exception) | `X,Y` | Party cell coordinates. Range is engine-specific. |
| `--facing` | yes (see KYRA save-slot exception) | `N`, `E`, `S`, or `W` (case-insensitive) | Party facing direction. |
| `--no-actors` | no (default off) | (no value) | Suppress actor rendering (monsters). Static scene only (walls, objects, wall items). |

The corresponding ConfMan keys are `screenshot`, `level`, `cell_x`, `cell_y`, `facing`, `no_actors` (ScummVM rewrites option dashes to underscores in its config storage).

## MM/Xeen-specific flags

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--mm-side` | no (default 0) | `0` or `1` | World of Xeen side: 0=Clouds, 1=Dark Side. |
| `--mm-no-border-anims` | no (default off) | (no value) | Suppress the five animated border UI overlays. |
| `--mm-log-slots` | no (default off) | (no value) | Log SLOT_FILL diagnostics for every indoor object slot assignment. |
| `--mm-pin-anim-frames` | no (default off) | (no value) | Pin each animated object to its cycle-start frame for deterministic captures. |
| `--mm-input-script` | no | string of `F`/`B`/`L`/`R`/`<`/`>`/`S`/`.` | Replay a sequence of player inputs after the initial render. See "Input replay" below. Whitespace and commas in the string are ignored. Requires `--mm-screenshot-prefix`. |
| `--mm-screenshot-prefix` | no | absolute filesystem path prefix | Per-step PNG output prefix. With `--mm-input-script`, writes `<prefix>.NNN.png` for steps 0..N plus a sidecar `<prefix>.trace.txt`. May replace `--screenshot` entirely; if both are set, `--screenshot` receives the final frame. |
| `--mm-scale-test` | no | absolute filesystem path | Dump SpriteResource scaler reference PNGs to the given directory and exit. |

ConfMan keys: `mm_side`, `mm_no_border_anims`, `mm_log_slots`, `mm_pin_anim_frames`, `mm_input_script`, `mm_screenshot_prefix`, `mm_scale_test`.

## KYRA/EOB2-specific flags

The KYRA harness reuses ScummVM's stock `--save-slot` flag and adds its own `--eob-*` namespace.

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--save-slot` (alias `-x`) | no (default -1) | integer save slot | Restore the given save instead of doing a fresh-game bootstrap. When `--save-slot=N` is supplied, `--level`, `--cell`, and `--facing` become optional post-load overrides; omit them to capture the save's recorded position. This is how state-dependent scenes (open doors, pulled levers, scripted decoration changes) are captured for diffing. |
| `--eob-dump-state` | no | absolute filesystem path | Write a canonical engine state snapshot (see below) and exit. Requires `--level`; `--cell` and `--facing` are optional, because a snapshot describes the whole level rather than a viewpoint. Does not require `--screenshot`; if both are given, both outputs are produced. |
| `--eob-fire-triggers` | no | absolute filesystem path | Fire every trigger on `--level` from a clean state and record what each one changed. See "Trigger sweeps" below. |
| `--eob-dialog-answers` | no | comma-separated integers | Dialogue button answers, consumed in order. Past the end, answers default to 1. |
| `--eob-hand-item` | no (default 0) | decimal item-table index | With `--eob-fire-triggers` (or a batch `triggers` line): put that item-table record into the party's hand before each firing, as a player who had picked it up would carry it. 0 seeds nothing. Ignored by the other modes. See "Trigger sweeps" below. |
| `--eob-batch` | no | absolute filesystem path | Run many captures in one process. See "Batch mode" below. |
| `--eob-sequence-prefix` | no | absolute filesystem path prefix | With `--eob-fire-triggers`, a batch `triggers` line or `--eob-play-sequence`: write `<prefix>.NNNN.png` at each capture point plus a sidecar `<prefix>.trace.txt`. Refused without one of those modes. See "Sequence captures" and "Sequence plays" below. |
| `--eob-play-sequence` | no | `intro` or `finale` | Play the reference's intro (`DarkMoonEngine::seq_playIntro`) or finale (`seq_playFinale`, credits included) from a fresh bootstrap on a virtual clock, photographing it through `--eob-sequence-prefix` (required), then exit. Presence enables harness mode. Needs no `--level`, `--cell` or `--facing`. Refused with `--screenshot`, `--eob-dump-state`, `--eob-fire-triggers` or `--eob-batch`. Not forced headless: set `SDL_VIDEODRIVER=dummy`. See "Sequence plays" below. |

ConfMan keys: `eob_dump_state`, `eob_fire_triggers`, `eob_dialog_answers`, `eob_hand_item`, `eob_batch`, `eob_sequence_prefix`, `eob_play_sequence`.

### State snapshots

`--eob-dump-state=PATH` writes a line-oriented, deterministic text snapshot of engine
state. It is the shared primitive behind griddelve's movement, passability and trigger
conformance tests: griddelve emits the same format, and each conformance test is a diff
of two snapshots. The format is ordered and free of timestamps and absolute paths, so a
diff points at engine state rather than at formatting.

```
# eob2-state v1
level 4
party 15 10 N
flags <18 words, 8 hex digits each>
wallflags <256 bytes, 2 hex digits each>
door <i> <block> <wall> <state>          (3 lines, one per OpenDoorState slot)
block <idx> <w0> <w1> <w2> <w3> <flags> <assignedObjects> <drawObjects> <dir>
```

`wallflags` is `_wllWallFlags`, indexed by wall type; bit 0 decides passability, so that
table plus the per-block wall types is the entire movement input. `assignedObjects` is
the offset of the block's trigger script, so the `block` lines double as the trigger
enumeration both engines must agree on.

Reading `_flagTable` requires a `friend class ScreenshotHarness` declaration on
`EoBInfProcessor`: the public `setFlags`/`checkFlags` mask API cannot enumerate flags.

### Trigger sweeps

`--eob-fire-triggers=PATH` enumerates every block on the level whose `assignedObjects` is
non-zero, then fires each one from a freshly reloaded level and records what changed.
Each block is fired once per invocation kind a player can cause (`0x01` step-on, `0x02`
step-off, `0x40` wall click), skipping kinds the block's own flags do not admit; `run()`
gates on `subFlags = ((blockFlags & 0xFFF8) >> 3) | 0xE0`.

```
# eob2-triggers v2
level 4
hand-item <idx>                          (only when --eob-hand-item or the batch field is non-zero)
trigger <block> <x> <y> flags=<hex4> script=<hex4> invoke=<hex2>
  wall <block> <dir> <old> -> <new>
  flag <idx> <old> -> <new>
  party <x> <y> <dir> -> <x> <y> <dir>
  level <old> -> <new>
  door <slot> <block>/<state> -> <block>/<state>
  walls-not-compared level-changed
  item <idx> <where> <type>/<value>/<flags>/<icon> -> <where> <type>/<value>/<flags>/<icon>
  items <block> <list> -> <list>
  hand <old> -> <new>
  truncated opcode-budget-exceeded
  truncated flight-budget-exceeded
  dialogues <n>
  end
# <n> trigger block(s)
```

Only deltas are recorded, so a trigger that changes nothing is two lines. A trigger that
switches level leaves the block table describing a different map, so the wall and door
diffs are replaced by `walls-not-compared level-changed`; without that, one such trigger
emits over a thousand meaningless lines.

`item` records one line per item-table index whose record changed, in ascending index
order. `<where>` is `<level>:<block>:<pos>` in decimal (`block` and `pos` signed), and
the payload is `type`, `value` and `icon` in signed decimal with `flags` as two lowercase
hex digits. A record whose `block` is `-1` is shown as the bare word `free` with no
payload; a record the firing appended to the table has no before side and reads as
`free` there. Two records compare equal when both are free, or when level, block, pos,
type, value, flags and icon all match; `next` and `prev` are not compared, because the
`items` line covers order. Note that `duplicateItem` copies the template record, so an
item created into the hand or a character's pack keeps the template's location (usually
`0:0:0`) until something calls `setItemPosition`; `free` is only `block == -1`. The record
table is game-wide, so `item` lines are still emitted when the firing changed level.

`items` records one line per block whose item list changed, in ascending block order.
`<list>` is the walk the engine itself does in `countQueuedItems`: from the block's
`drawObjects` head along `prev` until it returns to the head, indices comma-separated
with no spaces, or the single character `-` for an empty list. Two lists are equal when
they hold the same indices in the same order, so a reordering shows up as well as an
addition or removal. The walk is bounded at 1024 steps against a corrupt ring. Like the
wall diff, `items` lines are skipped when the firing changed level, because the block
table then describes another map.

`hand` is `_itemInHand` before and after, emitted only when it changed. `0` is the empty
hand, the table's dummy record.

Within a firing the new lines follow the `door` lines: all `item` lines, then all `items`
lines, then `hand`, then the `truncated` and `dialogues` lines and `end`.

A script that launches an item (`oeob_launchObject`) parks it in a flying-object slot on
its start block at `pos | 4`; the game flies it from `timerProcessFlyingObjects`, a timer
that never ticks under the harness. The sweep drains the flights after the script
returns and before the after state is captured, calling that timer handler while any slot
is enabled, so the reference records where the item lands and the crossing (`0x10`) and
landing (`4`) scripts it fires on the way rather than an item hovering over its launch
block. The drain is bounded at 64 passes, because a landing script can launch again and a
magic object with unlimited range only stops at a wall; a firing that hits the bound with
a flight still enabled records `truncated flight-budget-exceeded`, the sibling of the
opcode-budget line. The flying-object slots are also cleared in the per-firing reset, for
the same reason as the door slots: nothing on the level load path empties them, and a
flight left enabled would otherwise land during the next firing's drain.

The level is fully reloaded between firings, with `_hasTempDataFlags` cleared first:
otherwise `loadBlockProperties` restores the modified block table instead of re-reading
the maze, and the previous trigger's edits leak into the next one's baseline.

The door animation slots are cleared alongside it, for the same reason and because
nothing else does it: `completeDoorOperations` empties them when the party moves or a
door finishes animating, and neither happens under the harness. Left alone, a firing that
opened a door leaves the block registered, and the next firing to work that door reads it
as one already in motion and reverses it instead.

The item table is reloaded from the game files alongside, and the hand emptied, because a
level load leaves both alone: `createItem` appends to the table, `deleteItem` and the
item moves rewrite it, and an item created into the hand stays there. Left alone, a
firing that creates an item changes what every later firing in the sweep finds on its
blocks and in the party's hand. An empty hand is item 0, the table's dummy record, which
is what the screenshot path leaves it at.

`--eob-hand-item=N` seeds the hand instead of emptying it: after each reload, record N of
the item table is put into the party's hand, the way a player who had picked it up before
walking to the trigger would carry it. That is what the lock scripts need, because they
test the hand item's type and value (a key of the right kind) and then consume it, and an
unseeded sweep never sees those branches. The record is not duplicated: when it lies on
the level being swept, `loadLevel` has just threaded it onto its block's item ring, and
the seeding takes it off again with `getQueuedItem`, the routine a real pick-up uses, so
the firing's `item N ... -> free` and `items <block>` lines read like a pick-up followed by
a use. A record lying on another level is simply pointed at the hand. Both the `before`
and `after` states of a firing are captured after the seeding, so the seeding itself never
shows as a delta. A seeded sweep writes `hand-item N` immediately after the `level` line,
before any `trigger` line; the line is absent from an unseeded sweep, and the format stays
`# eob2-triggers v2` because it is optional and additive. `0` is the default and seeds
nothing. The flag has no meaning outside a trigger sweep and is ignored by the other modes.

Setting `EOB_TRIG_PROGRESS=/path` writes the current trigger to that file, rewritten and
closed per firing. The main output is buffered, so if a script hangs or crashes this file
is the only record of which trigger was responsible. It is a debugging aid, not part of
the reference format.

All 15 levels sweep in about 12 seconds in one batched process, producing roughly 1560
firings across 1005 trigger blocks.

### Sequence captures

Under the harness every wait returns at once, so a script's picture sequence (between
`initDialogueSequence` and `restoreAfterDialogueSequence`) runs through inside a single
`runLevelScript` call, and the restore redraws the play field before anything could look.
`--eob-sequence-prefix=PATH` photographs the screen at the points a player would see it,
while a trigger firing runs (`--eob-fire-triggers`, or a batch `triggers` line). The same
prefix serves the intro and the finale; see "Sequence plays" below.

| Capture point | Where | Trace line |
|---------------|-------|------------|
| A frame is cut in | `EoBCoreEngine::drawSequenceBitmap`, after its `updateScreen` | `NNNN frame block=<b> file=<f> rect=<r> x=<x> y=<y> flags=<n>` |
| A page's text is drawn | `TextDisplayer_rpg::printDialogueText(int, const char *, ...)`, after `displayText` and before the wait | `NNNN page block=<b> text=<id> label=<q>` |
| A delay inside a sequence | `EoBCoreEngine::delay`, when `_dialogueField` is set | `NNNN delay block=<b> ticks=<n>` |

Each capture writes `<PATH>.NNNN.png`, numbered from 0000 across the whole process, and one
line of `<PATH>.trace.txt` naming it; the trace is truncated when the process starts and
flushed per line.

* `block` is the firing's trigger block, also for the crossing and landing scripts the
  flight drain runs, because the captures stay armed until the drain ends. The trace
  line does not name the invocation kind: with `--debuglevel=3 --debugflags=Script` each
  capture is mirrored as a `HARNESS-SEQUENCE <line>` log line, after the firing's
  `HARNESS-TRIGGER block=<b> invoke=<k>` marker.
* A frame's operands are `drawSequenceBitmap`'s own: the file name as the script spells
  it, the destination rectangle index, the source x in eight-pixel columns, the source y,
  and the flags.
* A page's `label` is the page-break string, quoted with `"` and `\` escaped, so an empty
  label (`""`, a page with no button) reads apart from a missing one, written `-` (label
  index `0xFFFF`, which `getString` returns as null).
* `ticks` is the delay in engine ticks (`millis / tickLength()`). Delays outside a
  sequence, which pace wall changes and lead-ins, are not captured.
* The page capture sits in the shared RPG text displayer, so it fires for any numbered
  dialogue page drawn during a firing, inside a sequence or not.
* A cross-fade (flag 2) still runs `Screen::crossFadeRegion`, which paces itself with
  `delayMillis` per row rather than through `EoBCoreEngine::delay`; the frame capture
  follows it, so it shows the whole cut.
* Levels load between firings with the captures disarmed, so nothing is written then.

### Sequence plays

EOB2's intro and finale are not script sequences. `DarkmoonSequenceHelper` plays them from
animation tables, and the other modes never reach them: the harness takes over in
`EoBCoreEngine::go` before the main menu, which plays the intro, and before `runLoop`, whose
tail plays the finale. `--eob-play-sequence=intro|finale` plays one of them directly and
photographs it:

* The harness bootstraps as a screenshot does (`startupNew` and level 1, or `--save-slot`),
  seeds the RNG with `0x5EED` as the trigger sweeps do (the finale's dissolves shuffle their
  pixel order with it), and selects the audio resource set the reference selects first:
  `kMusicIntro` and `loadSoundFile(0)`, as `DarkMoonEngine::mainMenu` does, or
  `kMusicFinale`, as `go` does. The party-transfer autosave `go` writes before the finale is
  not written.
* It plays `DarkMoonEngine::seq_playIntro` or `seq_playFinale` (credits included), closes
  the trace and exits 0.
* `--eob-sequence-prefix` is required, and no other mode may be combined with the flag.
* Callers set `SDL_VIDEODRIVER=dummy`: the flag is not in `posix-main.cpp`'s headless force.

```bash
SDL_VIDEODRIVER=dummy ./scummvm --path="/path/to/EOB2/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --eob-play-sequence=finale --eob-sequence-prefix=/tmp/finale eob2
```

#### The virtual clock

The helper paces itself on `_system->getMillis()` as well as through `EoBCoreEngine::delay`:
its holds busy-wait, the intro's scroll derives its state from elapsed time, the delayed
palette fade steps when a timer passes, and the credits wait between steps. Under the
harness every `delay` returns at once, so those would run at real speed, with states that
depend on scheduler jitter. While a play runs, the harness owns a millisecond clock instead:

* `EoBCoreEngine::delay` advances it by the amount it skips.
* `ScreenshotHarness::sequenceMillis()` returns it during a play, and the wall clock
  otherwise. The helper reads it in `delay`, `hScroll`, `initDelayedPaletteFade`,
  `processDelayedPaletteFade` and `animCommand` (commands 3 and 4), `seq_playIntro` for its
  scroll deadlines, `seq_playCredits` for its step pacing, and `KyraRpgEngine::delayUntil`
  for the current time.
* The finale's hold (the `palFading` branch of `DarkmoonSequenceHelper::delay`) only polls,
  so during a play it steps the clock with `_vm->delay(1)`, one millisecond a turn.
* The clock starts at 65536, because `hScroll` reads a zero start timestamp as "no scroll".
* The finale's last loop waits for a skip that nothing sends headlessly. During a play it
  photographs the screen once and returns, and the finale goes on to its fade to black.

Holds, the scroll, delayed fades and the credits then run in no time, with a repeatable step
count. Time the reference spends in `_system->delayMillis` (the text-colour fade's steps,
`crossFadeRegion`'s rows, the 10 ms after a palette set outside the scroll, the helper
destructor's 150 ms) is not on the virtual clock: it costs real time and draws nothing, but
a delayed palette fade in progress sees less time pass across a text fade than in real play.

#### Capture points

Each capture writes `<PATH>.NNNN.png` of page 0 in the **screen palette**
(`Screen::_screenPalette`, expanded with `(v * 0xFF) / 0x3F` as `Screen::setScreenPalette`
expands it), numbered from 0000, and one line of `<PATH>.trace.txt` ending in
`palette=<crc>`: the CRC-32 (zlib's) of the 768 six-bit screen palette bytes, as eight
lowercase hex digits. The flashes, fades and black screens of both sequences change the
screen palette and leave slot 0 alone, which is what `writePagePng` reads; the other modes
keep `writePagePng`, so their output is unchanged.

| Capture point | Where | Trace line |
|---------------|-------|------------|
| A record is drawn | `DarkmoonSequenceHelper::animCommand`, inside each case after the draw and the palette set, before the hold (command 6 after its sound) | `NNNN anim seq=<s> table=<i> rec=<r> cmd=<c> obj=<o> x1=<x> y1=<y> ticks=<d> pal=<p> x2=<x> y2=<y> w=<w> h=<h> palette=<crc>` |
| A trailing hold | `animCommand`, when `del > 0`, before the hold | `NNNN hold seq=<s> table=<i> ticks=<n> palette=<crc>` |
| A bare hold | `DarkmoonSequenceHelper::delay`, before the hold | `NNNN hold seq=<s> ticks=<n> palette=<crc>` |
| A scene reaches the screen | `loadScene` onto page 0 or 1, after `updateScreen` | `NNNN scene seq=<s> index=<i> palette=<crc>` |
| A page is copied to the screen | `update`, after `updateScreen` | `NNNN update seq=<s> page=<p> palette=<crc>` |
| Text | `printText` after its `updateScreen`; `fadeText` on return | `NNNN text seq=<s> index=<i> color=<c> palette=<crc>` / `NNNN untext seq=<s> palette=<crc>` |
| A palette set or fade ends | `setPalette`, `fadePalette`, `setPaletteWithoutTextColor`, on return | `NNNN palette seq=<s> index=<i> ticks=<n> palette=<crc>` |
| A dissolve ends | after each `crossFadeRegion` in `seq_playFinale` | `NNNN dissolve seq=finale palette=<crc>` |
| The scroll moves | `hScroll`, on each state change, wherever it is driven from | `NNNN scroll seq=intro state=<n> palette=<crc>` |
| The credits move | `seq_playCredits`, after each `updateScreen` | `NNNN credits seq=finale step=<n> palette=<crc>` |
| The last screen | `seq_playFinale`, before the skip wait | `NNNN final seq=finale palette=<crc>` |

* The helper's methods call one another, and only the outermost call photographs
  (`ScreenshotHarness::SequenceScope` counts the nesting). `printText`'s palette set,
  `animCommand`'s holds and palette sets, `update`'s palette set and the steps of
  `processDelayedPaletteFade` belong to the capture of the call that made them. The scroll,
  credits, dissolve and final captures fire wherever they are reached, so the scroll states
  drawn during the holds of the record the intro plays mid-scroll are photographed too.
* `seq` is `intro` or `finale`. `table` is the animation table index after
  `_platformAnimOffset` (0 on DOS) and `rec` the record's index in it. The other `anim`
  fields are the stored `DarkMoonAnimCommand` record's own, `x1` before `animCommand` halves
  a value at or above 320, so a transcription can print the same line from its table.
* `ticks` is in engine ticks; on a `palette` line it is `fadePalette`'s delay argument, and 0
  for a set. `color` is the colour the sequence passed to `printText`, before VGA moves the
  text to slot 255.
* A `setPaletteWithoutTextColor` whose palette is already on screen changes nothing and still
  writes its line, so the trace follows the calls.
* `step` counts credits steps from 0.
* As for the firing captures, the trace is truncated when the process starts, flushed per
  line, and each line is mirrored as `HARNESS-SEQUENCE <line>` with
  `--debuglevel=3 --debugflags=Script`.

One line of each kind, from the DOS English data:

```
0000 hold seq=intro ticks=1 palette=e30d871f
0001 palette seq=intro index=9 ticks=0 palette=f5a0f415
0004 anim seq=intro table=3 rec=0 cmd=0 obj=0 x1=0 y1=0 ticks=1 pal=2 x2=0 y2=0 w=0 h=0 palette=cf9d2cb6
0023 hold seq=intro table=6 ticks=18 palette=e30d871f
0153 text seq=intro index=0 color=16 palette=e53682b0
0159 untext seq=intro palette=620741d7
0198 scroll seq=intro state=0 palette=e53682b0
0484 update seq=intro page=2 palette=620741d7
1001 dissolve seq=finale palette=31d9776c
1076 credits seq=finale step=0 palette=4dc60184
1107 final seq=finale palette=dc6ac022
```

A sequence play does not photograph:

* the steps of `Screen::fadePalette`, of the text-colour fade, or of a delayed palette fade
  between captures (each capture's `palette` records the state reached);
* a dissolve in progress;
* sound and music: command 6 is traced and photographed with nothing new drawn;
* the main menu and the title screen;
* party portraits: the bootstrap installs no party, so the finale's portrait screen shows
  empty frames (`--save-slot=N` loads a save's party first; not exercised);
* other platforms and render modes: the design assumes DOS VGA.

#### What the DOS data does

Measured on the DOS English data with `--music-driver=null`:

* The intro writes 1374 captures in about 13 seconds and the finale 1109 in about 16. Most
  of that is `_system->delayMillis` pacing. Two plays of each produce identical traces and
  byte-identical PNGs.
* `--music-driver=null` detects as `MT_NULL`, which `EoBCoreEngine::init` maps to
  `Sound::kPCjr` with music disabled, so `waitForSongNotifier` returns at once. With an AdLib
  driver it would loop on `checkTrigger()`.
* No `scene` line is written: both sequences load every scene onto page 2 or 6.
* The credits draw one shape and stop after 26 steps. `seq_playCredits` reads `CREDITS.TXT`
  first, and the copy in this data directory has lost its 0x0D line separators (it is the
  static credits table with every 0x0D removed, ending in its NUL). With no 0x0D the whole
  file is one item, and its first byte, 0x02, makes that item a shape (escape id 5). The loop
  ends once the shape's bottom edge has entered the credits window. Nothing crashes, and the
  captures before and after are intact. The harness does not repair the data; it records
  what the reference plays. After that one item the loop tests the byte one past the end of
  the file's buffer (`Resource::fileData` adds no terminator), which evidently read as zero
  in these runs, since no second item was parsed.

### Batch mode

Startup work is repeated per process, so bulk capture is much cheaper in one.
`--eob-batch=FILE` processes many captures in a single process.

(Historically this mattered far more: the harness appeared never to terminate and its
output only landed when the process was killed. Both were symptoms of the modal
original-save import blocking startup, described under "Headless execution" below. With
that skipped, a single capture now runs in about two seconds and exits 0.)

Each non-empty, non-`#` line of FILE is one capture:

```
state    <outpath> <level>
triggers <outpath> <level> [hand-item]
shot     <outpath> <level> <x> <y> <N|E|S|W>
```

`state` parks the party at block (0,0) facing north before dumping, so the snapshot's
`party` line stays deterministic. `shot` honours `--no-actors`, reapplying actor
suppression after each level load (`loadLevel` repopulates the monster table).

`triggers` takes an optional fourth field, the item-table index to carry in the hand for
that level's sweep (see `--eob-hand-item` under "Trigger sweeps"). A line without it uses
the `--eob-hand-item` value, or 0 when the flag was not given, so existing three-field
lines keep their meaning.

A batch `shot` produces a byte-identical PNG to the equivalent single `--screenshot`
invocation; verified against griddelve's committed reference set.

Example, capturing every level's state in one process:

```bash
printf 'state /tmp/L%d.state %d\n' 1 1 > /tmp/batch.txt
./scummvm --path="/path/to/EOB2/" --music-driver=null -m 0 -s 0 -r 0 \
    --eob-batch=/tmp/batch.txt --no-actors eob2
```

The KYRA harness refuses to run on anything other than EOB2 (`gameID == GI_EOB2`). EOB1 is rejected because the renderer paths and resource layout differ enough that sharing one harness would be brittle.

## Headless behavior

The harness is fully headless. No window pops up under any video driver because `posix-main.cpp` forces `SDL_VIDEODRIVER=dummy` (when none is set) before SDL initialises, for the flags listed under "Invocation"; a sequence play (`--eob-play-sequence`) relies on its caller setting it. Palette and surface data are read from each engine's own state instead of from the SDL backend, so the captured PNG is correct even under `dummy` and `offscreen` drivers (which return an empty palette via `getPaletteManager()->grabPalette()`).

No autosave is written. No prompts are emitted. Failure paths exit fast (no waiting for a user to dismiss the modal error dialog ScummVM normally shows).

## Input replay (MM/Xeen only)

When `--mm-input-script=STR` is supplied alongside `--mm-screenshot-prefix=PATH`, the harness still bootstraps and renders an initial frame at the requested cell+facing (saved as `<prefix>.000.png`), then dispatches each character of `STR` through the same code path the in-game keyboard handlers use:

| Char | Action |
|------|--------|
| `F` | Step forward (in current facing) |
| `B` | Step backward |
| `L` | Turn left in place |
| `R` | Turn right in place |
| `<` | Strafe left (facing unchanged) |
| `>` | Strafe right (facing unchanged) |
| `S` | Press Space (cell-script interact) |
| `.` | No-op (skip; useful to align step counts with another engine) |

After each step the harness re-runs `Interface::draw3d(true, false)` and writes `<prefix>.NNN.png`. A sidecar `<prefix>.trace.txt` records one line per step in the form `NNN <char> maze=<id> pos=<x>,<y> facing=<N|E|S|W>`. The first line uses `-` as the input char to mark the initial state. If `checkMoveDirection()` blocks a movement (wall / impassable terrain), the position is unchanged but a screenshot and trace line are still emitted, so step counts stay aligned with whatever schedule the calling test is replaying.

Example: turn left twice, step forward, then press Space at Vertigo cell (13,15) facing N:

```bash
./scummvm "--path=/path/to/Might and Magic 4-5/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot-prefix=/tmp/replay --level=28 --cell=13,15 --facing=N \
    --mm-input-script="LLFS" \
    --no-actors --mm-no-border-anims --mm-pin-anim-frames worldofxeen
```

Produces `/tmp/replay.000.png` ... `/tmp/replay.004.png` plus `/tmp/replay.trace.txt`.

KYRA/EOB2 has no equivalent input-replay mode yet. It is planned as the next slice of
griddelve's conformance work, alongside a trigger-firing mode; both will build on the
state snapshot described above rather than on per-step PNGs alone.

## Headless execution of game scripts

Firing scripts outside a real play session runs code that assumes a player and a
screen. The harness neutralises that under `ScreenshotHarness::isEnabled()`, which is
false unless one of the harness flags is on the command line, so normal ScummVM play is
untouched:

| Site | Why |
|------|-----|
| `EoBCoreEngine::go` | Skips the original-save import. Its modal prompt has nobody to dismiss it, and startup blocks in the dialogue loop until the process is asked to quit. |
| `EoBCoreEngine::runDialogue` | Returns a scripted answer from `--eob-dialog-answers` instead of waiting for a button press. |
| `EoBCoreEngine::delay` | Returns immediately. The single choke point for timed waits; `KyraRpgEngine::delayUntil` routes through it. During a sequence play it advances the virtual clock by what it skips. |
| `DarkMoonEngine::seq_playFinale` | During a sequence play, photographs the last screen and leaves the wait for a skip, which nothing sends headlessly. |
| `TextDisplayer_rpg::displayWaitButton` | Returns immediately; it otherwise spins on `processDialogue()` waiting for a click. |
| `TextDisplayer_rpg::textPageBreak` | Same, for the "more" prompt. |
| `TextDisplayer_rpg::printMessage` | Returns immediately. Message rendering is pure presentation and segfaults when driven outside the screen state it assumes. |

Consequence: **message text is not captured**. The state snapshot does not model it, so
conformance loses nothing today, but a future slice that wants to diff message output
will need to intercept at the `oeob_printMessage_*` opcode instead of suppressing the
renderer.

The harness also installs a fixed four-character party
(`ScreenshotHarness::installHarnessParty`). `startupNew()` only sets up the playfield and
creates no characters, so a harness run otherwise has an empty party. That is invisible
while only drawing frames, but `oeob_printMessage_v2` picks a speaker with
`while (!testCharacter(c, 3)) c = (c + 1) % 6;`, which spins forever when nobody
qualifies. The party is synthetic and fixed rather than imported from a save so that a
sweep is reproducible on any installation.

## Source pointers

### Shared

* CLI option parsing: `base/commandLine.cpp` (search for `--screenshot`).
* Headless force: `backends/platform/sdl/posix/posix-main.cpp` (early `setenv` of `SDL_VIDEODRIVER=dummy`).

### MM/Xeen

* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine palette accessor: `engines/mm/xeen/screen.h` (`Screen::getMainPalette`).
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.

### KYRA/EOB2

* Harness module: `engines/kyra/engine/screenshot_harness.{h,cpp}`.
* Script flag table access: `friend class ScreenshotHarness` on `EoBInfProcessor` in `engines/kyra/script/script_eob.h`.
* Engine hook: `engines/kyra/engine/eobcommon.cpp`, in `EoBCoreEngine::go()` (just after `loadItemDefs()`).
* Friend declarations for engine state access: `engines/kyra/engine/eobcommon.h`, `engines/kyra/engine/kyra_rpg.h`.
* Sequence capture points (`--eob-sequence-prefix`): `engines/kyra/engine/eobcommon.cpp` (`EoBCoreEngine::drawSequenceBitmap` and `EoBCoreEngine::delay`) and `engines/kyra/text/text_rpg.cpp` (`TextDisplayer_rpg::printDialogueText`, the numbered-page overload); CLI option in `base/commandLine.cpp`.
* Sequence plays (`--eob-play-sequence`): `ScreenshotHarness::playSequence`, the virtual clock and the `capturePlay*` points in `engines/kyra/engine/screenshot_harness.cpp`; the capture calls and clock reads in `engines/kyra/sequence/sequences_darkmoon.cpp` (`DarkmoonSequenceHelper`, `seq_playIntro`, `seq_playFinale`, `seq_playCredits`); the clock read in `KyraRpgEngine::delayUntil` (`engines/kyra/engine/kyra_rpg.cpp`) and its advance in `EoBCoreEngine::delay`; `friend class ScreenshotHarness` on `Screen` (`engines/kyra/graphics/screen.h`, for `_screenPalette`) and on `DarkMoonEngine` (`engines/kyra/engine/darkmoon.h`, for `seq_playIntro`); CLI option in `base/commandLine.cpp`.

## Modifying the harness

The `harness` branch serves both `mm5e` and `griddelve`. Treat shared infrastructure changes with care: anything you touch in `base/commandLine.cpp`, in the headless-force code in `posix-main.cpp`, or in the semantics of the core flags (`--screenshot`, `--level`, `--cell`, `--facing`, `--no-actors`) can break the other consumer even if it looks fine for the one you are testing against.

Before changing shared code:

* Build the fork (`./configure ...; make -j$(nproc)`).
* Run a smoke test against the consumer you are working on. For mm5e that means `scripts/capture-scummvm-refs.sh` with `OUTDIR` pointed at a scratch directory, compared byte for byte against the committed `internal/refs/scummvm/*.png`. Its `task test` does not invoke the fork, so passing it is necessary but says little, and mm5e has no `task test:integration`. For griddelve, `scripts/capture-scummvm-refs.sh` with `OUTDIR` in a scratch directory and `SEEDS` pointed at the committed `internal/refs/scummvm/triggers/hand-seeds.txt` (without it the seeded sweeps are skipped, because the default seeds path lies under `OUTDIR`), compared byte for byte against the committed PNGs, state snapshots and trigger sweeps.
* If you can run both consumers, do so. If you cannot, dispatch a sub-agent that can, or ask the user before pushing.

Engine-specific changes (anything under `engines/mm/xeen/` or `engines/kyra/`) are lower-risk but still benefit from a smoke test on the affected project.

Other guidelines:

* Prefer additive flags over changes to existing flag semantics. A new `--mm-foo` or KYRA-side `--kyra-foo` flag costs little; rewriting the meaning of `--facing` costs both downstream consumers.
* Never rename a flag without updating both consumers in lockstep. The last flag rename (when the core flags moved from `--mm-screenshot`/`--mm-level`/`--mm-cell`/`--mm-facing` to engine-agnostic `--screenshot`/`--level`/`--cell`/`--facing`) was coordinated with both mm5e and griddelve in the same change set. Future renames need the same care.
* Engine-specific flags belong in their own namespace. MM-only flags use `--mm-*`. KYRA-only flags, if added, should use `--kyra-*` or `--eob-*` rather than living in the shared set.
* Document new flags here, in both the table and the source-pointers section.

## Determinism

Two invocations with identical inputs produce byte-identical PNGs in MM/Xeen. Verified in smoke testing. Animation phases default to frame 0 via the engine's `_isAnimReset` mechanism on the first `drawScene` call, and no animation tick advances before the frame is captured. If you observe per-run pixel noise on a new map you sample, it is most likely from a sprite whose initial frame depends on something time-driven; the calling test suite should tolerate it via a fuzziness threshold rather than chasing it here.

KYRA/EOB2 determinism follows the same principle (single `drawScene(1)` then exit, no tick), but has not been exercised at the same volume as MM/Xeen. If griddelve starts seeing per-run noise it is worth auditing the EOB2 draw paths for any time-of-day or random-frame dependencies.

## Performance

Single invocation including ScummVM startup is sub-3s on a modest workstation (measured: ~2.1s success, ~1.2s validation failure) for the MM/Xeen harness. For large regression suites, invoke N renders in parallel with `xargs -P`; each invocation is a self-contained process with no shared state. KYRA/EOB2 startup is comparable.

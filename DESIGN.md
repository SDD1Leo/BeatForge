# BeatForge visual design: NEBULA

> Black is the canvas. Neon is the signal. Music is the hero.

NEBULA is the approved, canonical visual direction. The reference images in
`docs/design/` (`nebula-1…3`) are the visual source of truth; the codebase is
the source of truth for what actually works. Never add a control that does
nothing to match a picture; planned screens appear only when their feature
exists.

## Where the design lives

| What | File |
|---|---|
| Colour, geometry and typography tokens | `app/Source/ui/Theme.h` |
| Stock JUCE widgets (buttons, combo boxes, sliders, menus, tooltips, dialogs) | `app/Source/ui/BeatForgeLookAndFeel.*` |
| Custom components (transport buttons, step grid, meter, ruler…) | read tokens from `Theme.h`, never literal colours |

Dialogs: alerts use a compact badge (amber `!` warning, violet `?` question,
cyan `i` info); file dialogs use JUCE's browser so they inherit the theme.

Rule: no `juce::Colour (0x…)` outside `Theme.h` (checked with
`grep -rn "Colour (0x" app/Source | grep -v Theme.h`).

## Tokens

| Role | Token | Value |
|---|---|---|
| App canvas | `background` | `#070B18` |
| Panels | `panel` / `panelRaised` | `#0D1426` / `#121C32` |
| Controls, empty steps | `control` | `#18233A` |
| Borders | `border` / `borderStrong` (hover, focus) | `#253451` / `#34476B` |
| Text | `textPrimary` / `textSecondary` / `textMuted` | `#EDF2FF` / `#9BAAC7` / `#687997` |
| Primary accent (on, selected, playing) | `violet` = `accent` | `#8B5CFF` |
| Playhead | `brightViolet` = `playhead` | `#B14CFF` |
| Signal accents | `magenta`, `cyan`, `electricBlue`, `teal`, `amber` | `#F044C8`, `#20D9F2`, `#3987FF`, `#12D6B0`, `#FFAD42` |
| Status | `ok` (green) / `error` (red: errors, clip, later record) | `#3DDC84` / `#FF5268` |
| Mute on / Solo on | `muteOn` / `soloOn` | amber / cyan |
| Meter | `meterLow` → `meterMid` → `meterHigh` → `error` at 0 dBFS | cyan → violet (−12) → magenta (−3) → red |

Radii: panels 5 px, controls 4 px; borders 1 px.

Typography: **Ubuntu Sans** for UI text (bundled with Ubuntu; JUCE falls back
to its default elsewhere); **Ubuntu Sans Mono** (`theme::numericFont`) for
tabular values: position, time, BPM. Captions (`DRUMS`, `MASTER`, `BPM`) use
`theme::captionFont` (small, bold, tracked).

Section colours inside instruments: oscillator **violet**, filter **cyan**,
envelope **magenta** — knob arcs and displays use their section's colour.

## Track colours

Fixed per musical role (`theme::track`), identical in every view:

| Role | Colour |
|---|---|
| Kick | magenta |
| Snare | amber |
| Closed hat | cyan |
| Open hat | teal |
| Clap | violet |
| Synth (lead MIDI track) | electric blue |

Bass, lead, pads, vocals, FX and master get colours when those tracks exist.
Colour always comes with a text label; it is never the only signal.

## State conventions

- Toggle **on** = filled with its signal colour (Play/Loop/Metronome violet,
  Mute amber, Solo cyan). Off = `control` surface with `border`.
- Hover = `borderStrong` outline / slight lift; pressed = darker fill;
  disabled = half alpha.
- Playing step = the whole column lifts; a lit step flares white.
- Hit lights flash in the track colour and decay in ~150 ms.
- Brightest colours are reserved for musical information and active state;
  most of the UI stays dark and quiet. No bloom, heavy shadows or glass.

## Redesign status and plan

| # | Step | Status |
|---|---|---|
| 1 | Tokens + central LookAndFeel applied to every existing screen (no layout change) | done (0.3.x) |
| 2 | Transport bar: project button + saved state, captioned readout strip (BPM / SIG / BAR.BEAT.TICK / TIME), icon buttons; Nebula alert dialogs and file browser | done (0.4.0) |
| 3a | Rotary `Knob` (arc in section colour, pointer, hover/drag states, unit-aware value) | done (0.6.0) |
| 3 | Remaining shared controls: `Panel` with header, compact M/S, meter strip; replace one-off painting | planned |
| 4 | Step sequencer polish to the reference (velocity lane when velocity exists) | planned |
| 5 | Arrangement-first layout (track headers + clips, timeline takes most space) with Phase 7 | planned |
| 6a | Piano roll (keyboard, grid, notes in track colour, velocity lane, header controls) + workspace tabs | done (0.5.0) |
| 6b | Instrument editor (sections, wave buttons, live waveform/filter/envelope displays) | done (0.6.0) |
| 6 | Mixer, export dialog in the same language as each feature lands (Phases 7, 9, 11) | planned |

Not shown until implemented: record, menus (File/Edit…), browser, AI
co-producer, mixer, synth, export.

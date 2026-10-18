# BeatForge visual design

The visual reference is the Figma file **BeatForge UI Revamp**, exported to
`docs/ui-spec/`:

- `SPEC.md`: layout, sizes, typography, colour usage and components.
- `BeatForgeColours.h`: the colour tokens.
- `BeatForge UI Revamp/*.png`: the Instrument, Step Sequencer, Piano Roll and
  Components boards.

It replaces the earlier NEBULA direction (`docs/design/`, kept for history).
The codebase is the source of truth for what actually works. Never add a
control that does nothing to match a picture. Where the mockup shows something
BeatForge doesn't have, the app keeps what it really does (see "Differences from
the mockup").

## Where the design lives

| What | File |
|---|---|
| Colour, geometry and typography tokens | `app/Source/ui/Theme.h` (values from `BeatForgeColours.h`) |
| Fonts (Inter, JetBrains Mono; OFL) | `app/Resources/fonts/`, embedded with BinaryData |
| Stock JUCE widgets (buttons, combo boxes, sliders, knobs, menus, tooltips, dialogs) | `app/Source/ui/BeatForgeLookAndFeel.*` |
| Custom components (header, track headers, clips, meter, modules…) | read tokens from `Theme.h`, never literal colours |

Rule: no `juce::Colour (0x…)` outside `Theme.h` (checked with
`grep -rn "Colour (0x" app/Source | grep -v Theme.h`).

## Tokens (`theme::`)

| Group | Tokens |
|---|---|
| Backgrounds, darkest first | `bgApp` #060914, `bgInset` #080C1C, `bgPanel` #0B1022, `bgSurface` #111733, `bgRaised` #171E40, `bgControl` #1F2752 |
| Borders | `borderSubtle` #1A2146, `border` #262F5E, `borderStrong` #3A4580 |
| Text | `textPrimary` #ECEEFF, `textSecondary` #9BA2CC, `textMuted` #5D6492 |
| Accents | `purple` #A855F7 (primary: play, playhead, active tab), `purpleDim` #2E1A5C (behind active toggles), `pink`, `cyan`, `blue`, `orange`, `teal`, `violet` |
| Status | `green` (device OK, saved), `red` (record, errors, clip), `amber` (unsaved, mute) |
| Roles | `accent` = `playhead` = purple; `muteOn` amber; `soloOn` cyan; `meterGradient` cyan → violet (65 %) → pink |

Older names (`background`, `panel`, `panelRaised`, `control`) still exist and
point at the new values so every component reads the new palette.

Radii: panels 12, cards 8, module cards 10, buttons 6, M/S toggles and step
cells 5; borders 1 px.

Typography: **Inter** for text (Regular; "bold" = Semi Bold; Bold only for the
app name), **JetBrains Mono** (`theme::numericFont`) for every number that
changes while playing (readouts, knob values, dB scale, ruler, chips) so
digits don't jitter. Captions use `theme::captionFont` (Semi Bold, caps,
tracked). Both fonts are built into the app; the Latin subsets have no `∞`, so
the meters show `-inf`.

Module colours in the instrument: oscillator **purple**, filter **cyan**,
envelope **pink**; the knob arcs, displays and title dots use them.

## Track colours

New tracks take the next colour in creation order:
pink → blue → violet → orange → cyan → teal (`theme::track::forIndex`). A track
keeps its colour when tracks are reordered. Step-sequencer rows: kick pink,
snare orange, closed hat cyan, open hat teal, clap violet. Colour always comes
with a text label; it is never the only signal.

## State conventions (SPEC.md section 13)

- Button styles: **Primary** (purple fill: an active choice such as the current
  instrument), **Secondary** (`bgRaised` + `border`), **Ghost** (transparent +
  `borderSubtle`: Reset, zoom).
- Header toggles (loop, metronome, computer keys) on = `purpleDim` fill, purple
  outline and icon; Record armed = red at 15 %.
- M / S: off `bgControl`; Mute on amber, Solo on cyan, with dark text. A muted
  track's name turns `textMuted` and its clips draw at 45 % opacity.
- Hover raises the fill one step (`bgSurface` → `bgRaised` → `bgControl`);
  accent fills brighten; pressed darkens; disabled = 40 % opacity.
- Glows only on the Play button, the playhead and the instrument curves.

## Differences from the mockup (kept on purpose)

- **Menu button and project name are separate** (your request): the menu
  button opens the project menu; double-click the name to rename.
- **Nebula Poly / Nebula Bass are instruments, not presets.** They sit under an
  INSTRUMENT caption; the real preset menu sits beside them under PRESET.
- **Mixer** stays as a third editor tab.
- **Help (?)** stays beside the computer-keyboard button.
- No pause icon: Play glows while playing (stop with Stop or Space).
- Clips are unnamed: "Pattern · 1 bar" / "Clip · 2 bars".
- The header shows the real version.
- Narrow windows drop the version, the TIME cell, the wordmark and some
  project-name width, in that order.

## Redesign status (Phase 11b)

| # | Step | Status |
|---|---|---|
| 1 | Foundation: tokens, embedded fonts, LookAndFeel controls (knob 12.1, linear slider 12.7, button styles, combo box, menus, tooltips) | done |
| 2 | Shared chrome: 68 px header, arrangement panel (track headers 12.6, ruler, lanes, clips, playhead), master meter with peak readout, status bar | done |
| 3 | Instrument tab: editor shell header, three module cards, waveform / filter / envelope displays, wave selector, knob controls | done |
| 4 | Step Sequencer tab | next |
| 5 | Piano Roll tab | planned |
| 6 | Mixer tab restyle | planned |

Earlier NEBULA steps (0.3–0.11) built the screens this pass restyles; see the
CHANGELOG for their history.

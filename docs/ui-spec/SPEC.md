# BeatForge UI Revamp — Implementation Spec

Target: JUCE (C++). All measurements are in logical pixels at a 1920 × 1080 window.
Source of truth: Figma file **BeatForge UI Revamp** — https://www.figma.com/design/mguWCRHCzNCM5ZbFtdzjJU

| Screen | Selected track | Bottom editor tab | Figma node |
|---|---|---|---|
| `screens/01-instrument.png` | Lead (synth) | Instrument | `4:12` |
| `screens/02-step-sequencer.png` | Drums | Step Sequencer | `8:156` |
| `screens/03-piano-roll.png` | Lead (synth) | Piano Roll | `9:272` |
| `screens/00-components.png` | — | Components board | `3:2` |

Colours live in `BeatForgeColours.h`. Every colour in this spec is written as a token name (`bg/panel`) that maps to `Colours::bgPanel`.

**Scope rule:** the design only contains controls the engine already supports. Don't add AI, mixer, export, LFO, mod-matrix or second-oscillator UI.

The arrangement in the mockups is filled with an example session (Drums, Bass, Lead, Perc) only to show clip rendering. Real content comes from the project model.

---

## 1. Typography

| Role | Font | Size | Weight | Colour | Notes |
|---|---|---|---|---|---|
| App name | Inter | 18 | Bold | text/primary | Header logo |
| Panel / track name | Inter | 13 | Semi Bold | text/primary | |
| Body / button label | Inter | 12 | Semi Bold | text/primary | |
| Tab label | Inter | 13 | Semi Bold | active text/primary, inactive text/muted | |
| Section label | Inter | 9–11 | Semi Bold | text/muted or text/secondary | ALL CAPS, letter-spacing 10–14 % |
| Status bar | Inter | 11 | Regular | text/secondary | |
| Transport readout values | JetBrains Mono | 20 | Medium | text/primary (position: accent/cyan) | |
| Knob value, dB scale, ruler numbers, chips | JetBrains Mono | 9–11 | Regular / Medium | text/secondary or text/muted | |

Embed both families with BinaryData and register them as typefaces in the LookAndFeel. Use JetBrains Mono for every number that changes while playing, so digits don't jitter.

---

## 2. Window layout

```
┌──────────────────────────── Header 1920×68 ────────────────────────────┐
├─ Body (padding 12, gap 12) ────────────────────────────────┬──────────┤
│  Arrangement panel   1752 × 440                            │  Master  │
│  ─ gap 12 ─                                                │  132 ×   │
│  Editor panel        1752 × 506 (fills remaining height)   │  958     │
├──────────────────────────── Status bar 1920×30 ────────────┴──────────┤
```

| Region | x | y | w | h | Fill | Border | Radius |
|---|---|---|---|---|---|---|---|
| Header | 0 | 0 | 1920 | 68 | bg/panel | bottom 1 px border/subtle | 0 |
| Arrangement panel | 12 | 80 | 1752 | 440 (fixed) | bg/panel | 1 px border/subtle | 12 |
| Editor panel | 12 | 532 | 1752 | 506 (flex) | bg/panel | 1 px border/subtle | 12 |
| Master panel | 1776 | 80 | 132 (fixed) | 958 (flex) | bg/panel | 1 px border/subtle | 12 |
| Status bar | 0 | 1050 | 1920 | 30 | bg/panel | top 1 px border/subtle | 0 |

**Resizing behaviour:**
- The arrangement keeps its height.
- The editor takes the remaining vertical space.
- The master panel keeps its width.
- The arrangement and editor take the remaining width.

Panels clip their children to the rounded rect.

---

## 3. Colour usage

### 3.1 Surfaces (back to front)
- **bg/app:** the window background between panels.
- **bg/panel:** header, status bar and the three main panels.
- **bg/inset:** wells that sit inside a panel: the timeline, graph displays, time readout, meters and velocity lane.
- **bg/surface:** cards: track headers, instrument modules, transport group and project menu.
- **bg/raised:** secondary buttons, the selected track header and off step cells.
- **bg/control:** knob tracks, slider tracks, M/S off state and step cells in the accent beat group.

### 3.2 Translucent fills (base token + alpha)

| Use | Token | Alpha |
|---|---|---|
| Clip body | track colour | 0.16 |
| Clip header strip | track colour | 0.90 |
| Clip outline (unselected) | track colour | 0.70 |
| Lane background | bg/surface | 0.50 |
| Selected track's lane | track colour | 0.08 |
| Loop region on ruler | accent/purple (outline 0.9) | 0.30 |
| Graph fill under curve | module accent | 0.10–0.12 |
| Unsaved pill background | status/amber | 0.12 |
| Muted track's clips | whole clip opacity | 0.45 |
| Beat grid line | border/subtle | 0.7 |
| Bar grid line | border/default | 0.9 |

### 3.3 Glows
Glows are drop shadows with no offset, drawn with `juce::DropShadow` (or the third-party `melatonin_blur` module for faster blurs).

| Element | Colour | Radius |
|---|---|---|
| Play button | accent/purple @ 0.55 | 16 |
| Playhead line | accent/purple @ 0.70 | 8 |
| Graph curves | module accent @ 0.5–0.6 | 10 |
| Active step under playhead | row colour @ 0.80 | 14 |

---

## 4. Header (h 68, padding left 20, right 16, gap 12, items vertically centred)

Left to right:

1. **Logo:** a 28 × 26 mark made of six bars (3 px wide, 5 px pitch, heights 8/16/26/14/22/10, radius 1.5). Bars alternate accent/purple and accent/pink. Then "BeatForge" (Inter Bold 18), gap 10, then 12 px of spacing.
2. **Project menu:** 208 × 38, radius 8, bg/surface, 1 px border/default. Contents: list icon 16, project name (Semi Bold 13, flexes), chevron 16. Padding 12 / 10.
3. **Save-state pill:** radius 20, padding 10 × 5, status/amber @ 0.12. A 6 px dot plus "Unsaved" (11, text/secondary). When saved, use a status/green dot and the text "Saved".
4. **Undo / Redo:** 36 × 36 icon buttons, radius 8, bg/surface, border/subtle. Icons are 18 px; text/secondary when available, text/muted when disabled.
5. *(flex spacer)*
6. **Transport group:** 202 × 50, radius 12, bg/surface, border/subtle, padding 4, gap 4.
   - Stop: 42 × 40, transparent, filled square icon 16, text/primary.
   - Play: 54 × 40, radius 8, accent/purple with glow, filled play icon 18. Show a pause icon while playing.
   - Record: 42 × 40, transparent, filled circle 16, status/red. When armed, use a status/red @ 0.15 background.
   - Loop: 42 × 40. On: accent/purple-dim fill, 1 px accent/purple border, accent/purple icon. Off: transparent, text/secondary icon.
7. **Time display:** radius 12, bg/inset, border/default, padding 8 / 6. Four cells separated by 1 × 30 border/default dividers.

   | Cell | Width | Value | Label |
   |---|---|---|---|
   | BPM | 92 | `120.00` | BPM |
   | SIG | 64 | `4/4` | SIG |
   | Position | 124 | `1.1.000` in accent/cyan | BAR.BEAT.TICK |
   | Time | 124 | `00:00.000` | TIME |

   Values are Mono Medium 20. Labels are Inter Semi Bold 9 in text/muted, with 10 % tracking.
8. **Count-in beats:** four 9 px dots, gap 5, with a "BEAT" label (8 px) underneath. The current beat is accent/purple; the others are bg/control with a border/default outline.
9. **Metronome toggle and Computer-keyboard MIDI toggle:** 40 × 40, radius 8. Off: bg/surface, text/secondary icon. On: accent/purple-dim fill, accent/purple border and icon.
10. *(flex spacer)*
11. Version text (`v0.10.0`, Mono 11, text/muted), then a 40 × 40 Settings icon button.

Icons are Lucide-style strokes (2 px, round caps) recoloured to the token. SVG paths for every header icon are in the Figma file. Use `juce::Drawable::createFromSVG` and `replaceColour`.

---

## 5. Arrangement panel (1752 × 440)

### 5.1 Track list (left column, 264 wide)
- Fill bg/panel, with a 1 px border/subtle line on the right edge.
- Padding 8, vertical gap 6.
- **Toolbar** (height 36):
  - "TRACKS" label (9–10 px caps, text/muted) and a count chip (bg/control, radius 8, padding 6 × 1).
  - Flex spacer.
  - "+ Drums" and "+ Synth" secondary buttons with padding 10 × 6.
- **Track headers:** 248 × 64 each, gap 6. The first one starts at y = 50 from the panel top. See component 9.6.
- *(flex spacer)*
- **Zoom row:** "ZOOM" label, then ghost buttons "−", "+" and "Fit" (padding 10 × 4).

### 5.2 Timeline (fills the rest: 1488 × 440)
- **Background:** bg/inset.
- **Geometry:** `x0 = 12`. With 16 visible bars, `barWidth = (width − 24) / 16`, which is 91.5 at this size. In code, derive barWidth from the zoom level and keep `x0` as the left inset.

**Ruler** (y 0–40):
- bg/panel with a bottom border/subtle line.
- Bar number: Mono Medium 10, at x + 5, y 21. Use text/secondary inside the loop range and text/muted outside it.
- Bar tick: 1 × 16 border/strong, from y 24 to 40.
- Beat ticks: 1 × 6 border/default, from y 34 to 40.
- Loop region: a bar across the top of the ruler at y 5, height 8, radius 3, accent/purple @ 0.3 fill with a 1 px accent/purple @ 0.9 outline. It is drawn whenever a loop range exists; dim it to 0.15 when loop is off.

**Lanes:**
- Lane *i* starts at `y = 50 + i × 70`; each lane is 64 tall with a 6 px gap, aligned with the track headers.
- Lanes are bg/surface @ 0.5. The selected track's lane is the track colour @ 0.08.

**Grid:**
- Starts at y 40 and stops 20 px above the bottom.
- Bar lines: 1 px border/default @ 0.9.
- Beat lines: 1 px border/subtle @ 0.7.

**Clip** (inset 1 px horizontally and 2 px from the lane top; height 60, radius 6):
- Body: track colour @ 0.16. Outline: 1 px track colour @ 0.7.
- Selected: 1.5 px text/primary outline.
- Header strip: 16 tall, track colour @ 0.9. Name in Inter Semi Bold 10, bg/app colour, at (7, 2).
- Drum clip preview: three rows (kick, snare, hats) at y 22 / 34 / 46. Each hit is a cell `(barWidth/16 − 2) × 7`, radius 1.5. The hats row is drawn at 0.55 alpha.
- Synth clip preview: notes as 4 px bars, radius 2, in the full track colour. Map pitch into the 22–56 px band of the clip body.
- If the track is muted, the whole clip is drawn at 0.45 opacity.

**Playhead:**
- 2 px accent/purple line with glow, from y 30 to the bottom of the grid.
- Head: a 14 × 12 rounded down-pointing triangle at y 22.

**Horizontal scrollbar:** 5 px tall, 12 px from the bottom, inset 12 on each side, radius 3. Track bg/surface, thumb border/strong.

---

## 6. Editor panel shell

**Editor header** (height 52, bg/panel, bottom 1 px border/subtle, padding 0 16, gap 20; items are bottom-aligned so the tab underline sits on the divider):
- **Tabs:** gap 20. Each is label + 2 px underline (accent/purple when active, none when inactive), padding-top 10, gap 8.
  - Drums track → **Step Sequencer | Instrument**
  - Synth track → **Piano Roll | Instrument**
- **Context:** a 1 × 18 divider, a 4 × 16 track-colour bar (radius 2), the track name (Semi Bold 13), and the type (9 px caps, text/muted).
- *(flex spacer)*
- **Right-side tools** change with the tab (see sections 7–9). They are 9 px above the bottom edge.

**Body:** padding 16 (piano roll 12), filling the remaining space.

---

## 7. Instrument tab (synth tracks)

**Header right side:**
- "PRESET" label.
- **Nebula Poly** button: primary when it's the active preset.
- **Nebula Bass** button: secondary.
- Divider.
- **Reset** button: ghost.

The active preset uses the Primary style. Add a "· modified" suffix in text/muted when parameters differ from the preset. This is a suggestion; it is not in the mock.

**Body:** three equal modules in a row, gap 12, each 565 × 420.

**Module card:** radius 10, bg/surface, 1 px border/subtle, padding 16, vertical gap 14.
1. **Title row:**
   - 6 px dot in the module accent.
   - Title in 11 px caps Semi Bold, text/secondary, 10 % tracking.
   - Flex spacer.
   - Chip: bg/control, radius 5, padding 8 × 3, Mono 9 text.
2. **Display:**
   - Full width, radius 8, bg/inset, 1 px border/subtle, clipped.
   - Height 214 for the oscillator, 264 for the filter and envelope.
   - Grid: 3 horizontal lines (border/subtle @ 0.8) and 7 vertical lines (border/subtle @ 0.5).
3. **(Oscillator only) Waveform selector:**
   - Four equal segments, gap 8, height 34, radius 7.
   - Each segment: a 34 × 14 waveform icon plus a label (11 Semi Bold).
   - Selected: accent/purple-dim fill, accent/purple border and icon, text/primary label.
   - Unselected: bg/raised fill, border/subtle outline, text/secondary icon and label.
4. **Knob row:** fills the remaining height. Knobs are centred both ways, gap 28 (envelope 20).

| Module | Accent | Chip | Display content | Knobs (label – example value) |
|---|---|---|---|---|
| Oscillator | accent/purple | current wave, e.g. `SAW` | Two cycles of the selected waveform, 3 px stroke + glow, fill to bottom @ 0.12, zero line border/strong @ 0.8 | Level – `-10.0 dB` |
| Filter | accent/cyan | `LOW-PASS` | Magnitude response, 3 px stroke + glow, fill to bottom @ 0.10. A dashed vertical line at cutoff (1 px, dash 4/4). A 12 px handle at the peak (bg/inset fill, 2 px accent outline). A cutoff value chip next to the handle. Frequency labels `100 Hz`, `1 kHz`, `10 kHz` at the bottom (Mono 9, text/muted). | Cutoff – `2.80 kHz`, Resonance – `25%`, Env Amt – `35%` |
| Envelope | accent/pink | `AMP + FILTER` | ADSR curve: linear attack, exponential-looking decay and release, 3 px stroke + glow, fill @ 0.12. Handles (11 px) at start, peak, decay end, sustain end and release end. Dashed guides (border/strong, dash 3/4) under each node. Segment letters A/D/S/R at the bottom (Mono 10). | Attack – `5.0 ms`, Decay – `300 ms`, Sustain – `60%`, Release – `250 ms` |

**Graph mapping:** use log frequency on the x axis for the filter display (20 Hz–20 kHz) and linear dB on the y axis. For the envelope, give attack, decay and release widths proportional to time, with the sustain plateau a fixed width (about 28 % of the display).

---

## 8. Step Sequencer tab (drum tracks)

**Header right side:**
- Clip chip: bg/surface, border/subtle, radius 6, padding 10 × 6. It reads `Pattern A · Clip at bar 1 · 16 steps`, with the step count in Mono.
- Divider.
- **Clear** button: ghost.

**Body** (padding 16 / 14, row gap 10):
- **Step number row:**
  - A 316 px spacer containing the "SOUND" label.
  - Then 16 numbers centred over their columns (Mono Medium 10).
  - The first step of each group is text/secondary; the rest are text/muted.
  - The playing step's number is accent/purple.
- **Sound rows** (one per drum voice, row height 52, cells 44 tall). Each row is a 316 px left block followed by the step grid.
  - **Name chip:** 132 × 44, radius 8, bg/surface, border/subtle, with a 4 px colour bar on the left and the name in Semi Bold 12.
  - M and S toggles (component 9.3), gap 10.
  - **Level slider:** fills the remaining width (about 100 px) in the row colour (component 9.7).
  - **Step grid:** 4 groups of 4, gap 16 between groups and 6 within a group. Each cell flexes in width and is 44 tall (component 9.4).

| Row | Colour |
|---|---|
| Kick | accent/pink |
| Snare | accent/orange |
| Closed Hat | accent/cyan |
| Open Hat | accent/teal |
| Clap | accent/violet |

**Step cell states:**
- Off, groups 1 and 3: bg/raised.
- Off, groups 2 and 4: bg/control, so beats read at a glance.
- On: the row colour with a top shine.
- Playhead column, off: bg/control with a text/primary @ 0.6 outline.
- Playhead column, on: the row colour with a glow.

---

## 9. Piano Roll tab (synth tracks)

**Header right side:**
- **LENGTH** dropdown, 100 × 32: `4 bars`.
- **SNAP** dropdown, 86 × 32: `1/16`.
- Divider.
- **Quantize** button: secondary.
- The note count chip (`20 notes`, Mono 10 on bg/control) sits in the context group on the left.

Dropdown style: bg/raised, 1 px border/default, radius 6, padding 10 / 8, Mono Medium 12 value, 14 px chevron.

**Body** (padding 12 × 16, gap 8):
- **Note grid:** full width, height 310, radius 8, bg/inset, border/subtle.
  - Keyboard column: 64 wide.
    - White keys: text/primary @ 0.88 with a 0.5 px border/default line between keys.
    - Black keys: bg/app, 62 % of the column width.
    - C keys are labelled `C4`, `C5`, … (Mono 9, bg/app colour).
  - Ruler: 22 tall, bg/panel. Bar numbers are song bars (Mono 10).
  - Rows: 12 tall. Black-key rows are bg/app @ 0.9 and white-key rows are bg/surface @ 0.55, with a 1 px border/subtle row line. C rows get a border/strong line.
  - Vertical lines: the bar line is border/strong, the beat line border/default, and the 16th line border/subtle @ 0.5.
  - **Notes:** the row height minus 2, radius 3.
    - Fill: track colour, alpha `0.55 + 0.45 × velocity/127`.
    - Outline: 1 px accent/purple @ 0.9.
    - Selected notes: 1.5 px text/primary outline.
  - Playhead: 2 px accent/purple with glow, full height.
- **Velocity lane:** full width, height 104, same frame style as the note grid.
  - The left 64 px is bg/panel, with the "VELOCITY" label and the scale labels 127 / 64 / 0.
  - Each note gets a stem (2 px wide) from the baseline (y 84) up to `velocity/127 × 62`, topped by an 8 px dot.
  - Selected notes use text/primary; the others use the track colour @ 0.85.

---

## 10. Master panel (132 wide)

- Padding 14 / 12, vertical gap 10, centred.
- **"MASTER" label:** 10 px caps, text/muted, 14 % tracking.
- **Meter area:** 108 × (remaining height, 854 at 1080p).
  - dB scale on the left: right-aligned Mono 9 labels with 6 × 1 border/strong ticks at 0, −6, −12, −18, −24, −36, −48 and −60. The scale is linear in dB from the top (y 14) to the bottom (height − 26).
  - Two bars, L at x 40 and R at x 72, each 26 wide: bg/inset well, radius 4, border/subtle.
  - Level fill: 22 wide, radius 3. It uses `Colours::meterGradient` (cyan at the bottom → violet → pink at the top).
  - Peak hold: a 2 px text/primary @ 0.9 line.
  - Clip LED: 22 × 6 at the top of each bar. Off is bg/control; on is status/red.
  - "L" and "R" labels under the bars.
- **Peak readout:** full width, radius 8, bg/inset, border/subtle, padding 8. The value (`-3.8`) is Mono Medium 18, with "dB PEAK" (9 px caps) underneath. Click it to reset the peak hold. This is a suggestion; it is not in the mock.

---

## 11. Status bar (height 30, padding 0 16, gap 10)

- **Left:** a 7 px status/green dot, then **ALSA** (Semi Bold), then the device name, sample rate, buffer (samples and ms) and dropouts. Separators are `·` in text/muted. All text is 11, text/secondary.
- **Right:** a MIDI dot (text/muted when there's no input, status/green when a device is connected), `No MIDI input`, `Keys C4–G5, velocity 100`, then a purple dot and **MCP ready** (text/primary).
- Turn the left dot status/amber when dropouts > 0 and status/red when the device is lost.

---

## 12. Components (Figma "Components" board)

### 12.1 Knob — 48 × 48 (scalable; the track header uses 28)
- **Track:** an arc from 135° to 405° (clockwise from 3 o'clock, i.e. a 270° sweep with the gap at the bottom), bg/control, ring thickness 16 % of the radius.
- **Value arc:** the same ring in the accent colour, from the start angle to the value. For **bipolar** knobs such as pan, draw from 12 o'clock to the value instead.
- **Cap:** a circle at 64 % of the knob size, bg/raised, 1 px border/strong, with a soft drop shadow (black @ 0.5, y 2, blur 4).
- **Pointer:** a short wedge in text/primary on the cap edge at the value angle (inner radius 45 % of the cap).
- In JUCE, implement `drawRotarySlider` with `rotaryStartAngle = 1.25π` and `rotaryEndAngle = 2.75π`. JUCE measures angles clockwise from 12 o'clock, so these numbers differ from the Figma ones above but describe the same bottom-left to bottom-right sweep.

### 12.2 Knob Control (knob + label + value), width 72
- Vertical stack, gap 6, centred: Knob 48, then the label (Inter Semi Bold 10 caps, text/muted, 8 % tracking), then the value (Mono Regular 11, text/secondary).
- Double-click resets to default. Drag up and down to change the value; shift-drag for fine control.

### 12.3 Toggle Button (M / S) — 24 × 22, radius 5

| State | Fill | Border | Text |
|---|---|---|---|
| Off | bg/control | border/default | text/secondary, Bold 11 |
| Mute on | status/amber | status/amber | bg/app |
| Solo on | accent/cyan | accent/cyan | bg/app |

When a track is muted, its name in the header turns text/muted.

### 12.4 Step Cell — flexible width × 34 (44 in the sequencer), radius 5

| State | Fill | Border | Extra |
|---|---|---|---|
| Off | bg/raised | 1 px border/subtle | |
| Off (accent group) | bg/control | 1 px border/default | |
| On | row colour | none | 2 px white @ 0.35 shine bar inset 4 px at the top; inner shadow black @ 0.25, y −2 |
| Playhead (off) | bg/control | 1 px text/primary @ 0.6 | |
| Playhead (on) | row colour | none | glow, row colour @ 0.8, radius 14 |

### 12.5 Tab
- Label (13 Semi Bold) over a 2 px underline that spans the label width, gap 8, padding-top 10.
- Active: text/primary label with an accent/purple underline.
- Inactive: text/muted label with no underline.

### 12.6 Track Header — 248 × 64, radius 8
- **Fill:** bg/surface. **Outline:** 1 px border/subtle. Clip children to the rounded rect.
- **Selected:** bg/raised fill with a 1 px outline in the track colour.
- **Layout:** horizontal, gap 10, padding-right 12.
  1. **Colour strip:** 4 px wide, full height, in the track colour.
  2. **Info column** (flexes, gap 8):
     - Name row: name (Semi Bold 13), then the type tag (`DRUMS` / `SYNTH`, 9 px caps, text/muted, 10 % tracking).
     - Controls row (gap 6): M toggle, S toggle, volume slider 96 × 12.
  3. **Pan knob:** 28 px, bipolar, in the track colour.

### 12.7 Linear slider (volume / level) — height 12
- Layout left to right: a filled bar (4 px, radius 2, track colour) up to the value, then the thumb, then the remaining track (4 px, bg/control).
- **Thumb:** a 12 px circle, text/primary fill, 2 px track-colour outline.
- Implement as `LinearHorizontal` with a custom `drawLinearSlider`.

### 12.8 Button — radius 6, padding 14 × 7 (compact: 10 × 6 or 10 × 4), label Semi Bold 12

| Style | Fill | Border | Used for |
|---|---|---|---|
| Primary | accent/purple | none | Active preset |
| Secondary | bg/raised | 1 px border/default | + Drums, + Synth, Nebula Bass, Quantize |
| Ghost | transparent | 1 px border/subtle | Reset, Clear, zoom −/+/Fit |

---

## 13. Interaction states (proposed — not drawn in Figma)
The mock shows resting and selected states only. Use these for the rest so the whole app behaves the same way.

| State | Treatment |
|---|---|
| Hover (buttons, cells, clips) | Raise the fill one step (bg/surface → bg/raised → bg/control); for accent fills, `brighter(0.15f)` |
| Pressed | `darker(0.15f)` on the fill |
| Disabled | Draw the component at 40 % opacity and ignore the mouse |
| Keyboard focus | 1 px accent/purple @ 0.8 outline, offset 2 px |
| Drag over the timeline | A ghost clip at 0.35 opacity snapped to the grid |

---

## 14. JUCE implementation map

| Design element | Suggested JUCE implementation |
|---|---|
| All colours | `BeatForgeColours.h`; call `Colours::applyTo (lookAndFeel)` once at startup |
| Knob, Knob Control | `Slider` (RotaryHorizontalVerticalDrag) + custom `drawRotarySlider`; wrap in a `KnobControl` component with two `Label`s |
| Linear sliders | `Slider` (LinearHorizontal) + custom `drawLinearSlider` |
| M / S, loop, metronome, keyboard toggles | `TextButton` / `DrawableButton` with `setClickingTogglesState (true)` + custom `drawButtonBackground` |
| Tabs | A custom `EditorTabBar` component (two `TextButton`s with an underline), simpler than restyling `TabbedButtonBar` |
| Track header | `TrackHeaderComponent` (strip, labels, toggles, slider, pan knob) |
| Timeline, ruler, clips, playhead | `ArrangementView` painting lanes/grid/clips in `paint()`; drive the playhead from a 30–60 Hz `Timer` and repaint only its strip |
| Oscillator / Filter / Envelope displays | Components that draw a `juce::Path` from live parameter values; repaint on parameter change |
| Step sequencer | `StepSequencerView` with `StepCell` components (or one painted grid with hit-testing) |
| Piano roll + velocity | `PianoRollView` and `VelocityLane` sharing one horizontal zoom/scroll model |
| Master meter | `MasterMeter` with a `Timer`, peak-hold decay and `Colours::meterGradient` |
| Icons | Lucide SVG strings → `Drawable::createFromSVG`, recoloured with `replaceColour` |
| Fonts | Inter + JetBrains Mono via BinaryData, returned from `getTypefaceForFont` |

# AGENTS.md: Zoetrope

The why. `CLAUDE.md` has the commands; `README.md` the claims and the numbers.

## The idea

The spec (`~/Projects/resolume/specs/SPEC-zoetrope.md`, 2026-10-15) asked for "the clip's
frames printed on the cards of a spinning disc, seen only in the light of a strobe that
fires on the music". So the plugin is the whole apparatus, with nothing animated:

1. **The disc** (`Disc.cpp`). N cards at radius R, station k at room angle θ + k·2π/N,
   angles clockwise from twelve o'clock. The drive turns θ: a motor at a constant ω (Sync's,
   `Slip · (2π/N) / T_flash` from the host's tempo, or RPM's), or a hand that pushes it and
   a bearing that slows it, `dω/dt = −kω − c`, solved in closed form, never integrated.
2. **The print head** (`Disc::Crossings`). Card k passes the head when `θ(t) + k·pitch = h`
   (mod 2π). The crossings in a frame are the integers between `(θ − h)/pitch` at its two
   ends; each one's moment is solved by bisection on the closed form. A card that crosses
   during a host frame is printed with that frame of the clip (`Cards.cpp`, one layer of a
   texture array per station, linear light).
3. **The strobe** (`Strobe.cpp`). Flashes are rectangles Flash long with a fixed energy,
   placed at the beat (interpolated inside the frame from the bar phase), at Rate's own
   phase, or for an onset or a press of Fire to end at the frame's time.
4. **The camera** (`Strobe::Exposure`). The window `[t_n − S, t_n]`, `S = Shutter × T`.
   What it catches is a list of light segments: the room light over the whole window,
   weight `RoomLight · S / T`, and each flash's overlap `[a, b]` with it, weight
   `Strobe · (b − a) / Flash`. All solved in closed form on the CPU.
5. **The picture** (`kSceneFragment`). Per pixel: its angle (`clockAngle`), then for each
   light segment a power-of-two set of time taps at `(j + ½) / n` of the segment, n the
   smallest that gives a tap per pixel of the arc at this radius (up to 64), each the card
   under the pixel at the disc's angle then (the angle from a 17-entry table across the
   window, relative to a reference), averaged, times the weight, summed.
6. **The eye** (optional). Persistence is an exponential of the camera's pictures
   (`exp(−T / τ)`), in linear light, in a ping-ponged buffer.

The **Drum** view (`kDrumFragment`) is the same cards and drive inside a drum: for each
pixel a ray from an eye three radii off the axis meets the near wall and the far wall
(closed form); the slits that pass the near point during the exposure are solved from the
angle table (and its inverse, `fractionAt`); the far wall is averaged over each slit's
open turn with 16 taps; the camera is opened by the slits' duty cycle.

Neighbours: readout (a strobe against a rolling shutter), afterglow (a frame queue), gauge
(frames on a strip), photofinish (a slit). None samples motion with a strobe or turns the
clip into a rotating delay line.

## Decisions taken without asking (the brief says decide and write it down)

- **Name and id** are the spec's: `SW Zoetrope`, `ZT01`, `com.stoatworks.ffgl.zoetrope`,
  harness `zttest`. `ZT01` was free across `~/Projects/resolume` and `~/dev`.
- **Cards face outward.** A card's up is along its radius, so the figure at six o'clock is
  upside down. A rigid disc can't keep them upright; that's a carousel, and a different
  plugin.
- **A card is a print on card stock.** The print is the clip's shape, with a margin of 6%
  of its height all round in white stock (albedo 0.5, linear). A card not yet printed is
  bare stock, so a fresh disc (or a change of Stations, which is a new disc) starts as a
  ring of white cards and fills in as it turns: the printing is visible, as the spec wanted.
  The disc is near black (0.006), the print head a grey block (0.32) just off the rim.
- **Print at frame resolution.** A card crossing the head during a host frame gets that
  frame of the clip, tagged with the crossing's exact moment. The clip has no picture
  between frames; afterglow's ring would let the print take the frame before, which is no
  better, so it doesn't.
- **The default shutter is 360°.** At 120 BPM and 60 fps every beat falls on a frame
  boundary, and a flash starts on its beat; through a 180° shutter (open for the second half
  of each frame) every single beat flash fell between two exposures and the defaults showed
  nothing at all. A real camera can do this to you, which is why Shutter is there, but not
  out of the box.
- **Onsets and presses end on their frame.** They carry no moment of their own, so their
  flash is placed to end at the frame's time, which every shutter at least as long as the
  flash catches, and never to start before the frame did (see the traps).
- **Sync's flash period** is the trigger's subdivision of the host's beat, 1 / Rate for
  Rate, and one beat for Onset and Fire Only, which have no period.
- **Pushes** (Hand) come on every flash of a beat-type trigger, every onset and every press
  of Fire, and on the host's beat with Trigger Rate (a 200 Hz strobe pushing the disc 200
  times a second would only be a motor).
- **One Friction control** sets the viscous rate k, 0.05 to 4 per second, with the Coulomb
  part at `c = 0.25 k` rad/s², so the dry friction takes over below about 2.4 RPM and the
  disc stops in a finite time instead of slowing for ever.
- **Ranges.** Slip 0–2 linear (1 locked, 2 skips a card); RPM 0–600; Rate 0.25–200 Hz;
  Flash 10 µs to 20 ms; Push 1–120 RPM; Strobe 0–2; Radius 0.15–1 of half the shorter side;
  Card Size 0.2–1 of the widest card that can't touch a neighbour,
  `h_p = 2Rt / (a + 2m + (1 + 2m)t)`, `t = tan(π/N)`.
- **The room light is normalised to the frame**, the flashes to themselves: a shorter
  shutter dims the room light as a real exposure does, and a flash caught whole is the same
  however long the shutter.
- **At most eight flashes per exposure**; more are dropped and counted (200 Hz at 30 fps
  through 360° is seven).
- **Taps**: up to 64 per segment, one per pixel of arc; past 64 px of arc a per-pixel
  offset (a PCG hash) turns the leftover stepping into grain rather than copies.
- **Persistence restarts** from the current picture when its buffers reallocate (a
  resize): it's 200 ms of afterimage at most, and starting from black would flash.
- **The cards' layers** are sized from the print's width on screen on a half-octave ladder
  (8 to 2048 texels), so a dragged Radius reallocates a few times rather than every frame;
  the depth only grows. Each reallocation draws every print across, resampled.
- **No trig in the GLSL**, the fleet's rule since radar: `clockAngle` is an arctangent by
  a 30° reduction and its series (2.5e-9 worst against `atan` in double), `sinSeries` and
  `cosSeries` are Taylor series in Horner form for |x| ≤ π/4 (5e-13).
- **The drum's geometry** is fixed: the eye three radii from the axis, so the squeeze at
  the middle is (3 + 1) / 6 = ⅔; slits a sixteenth of a station; the camera opened by 16;
  the strip of cards on the far wall as tall as the slit band's view of it (the band is
  (d − 1)/(d + 1) of the strip's height); the drum twice the band's height. Cards sit at
  `k·pitch` and slits at `k·pitch + π`, so a slit in front shows a card's centre straight
  ahead. Radius sets the drum's apparent width (0.62 W × Radius), so a big Radius crops its
  sides and fills the frame with figures. The print head is behind the drum and isn't drawn.
- **The drum's light is steady**: the Strobe level as a lamp, plus the room light. Flash does
  nothing there; Trigger still sets Sync's speed and the hand's pushes.
- **No Accent.** schlieren's analyser can give an onset's level; the flash's energy could
  follow it. Left out to keep near 25 controls (23 now).
- `StoatworksAbout.h` and `ATTRIBUTIONS.md` are **provisional hand copies** adapted from
  schlieren's, with `guide = ""` (no guide button), as graticule's were before it was
  registered. Generated by stoatworks-backend once it is.
- **The workflows** (`ci.yml`, `release.yml`) assume the public `stoatworks-labs/zoetrope`
  the fleet's repos live in. Nothing here has a remote; they have never run.

## The traps actually hit

Ordered by how much they cost.

**A ring the frame cuts is not N-fold symmetric.** The angle instrument (the phase of the
ring's N-th angular harmonic) read held angles 1e-3 rad off, as a perfect sin(Nθ) and the
same at both rasters, which looked like a model error and was nothing of the sort. The test
disc was large enough that the cards at twelve and six o'clock ran off the frame, and the
cut-off ring has an N-th harmonic of its own. The checks' disc is now small enough that no
card leaves the frame at four stations.

**The angle instrument's error is not smooth.** Calibrated on twelve held angles it read to
7e-6 rad; the drift check then found single readings 6e-5 off. Where the cards' edges come
within a degree or so of a pixel row, every row an edge crosses samples its antialiased
ramp at the same phase, and their small first-moment errors add up instead of averaging out
(exactly zero at perfect alignment, by symmetry, then a spike either side). Dense
calibration (140 angles) found 9e-5; twelve had missed it. So `--drift` and `--hand` no
longer trust the instrument's worst case: each reading is corrected by the instrument's
reading of a **held twin** (a second plugin instance, white cards, disc held by a hook) at
the predicted angle, which cancels the grid's error wherever it is. The corrected errors are
3e-8 rad and 1.2e-7 rad, against 9e-5 uncorrected.

**Beat flashes fell between exposures.** See the 360° default above. Found by looking at
the first render, which was black.

**A press's long flash lost light.** A Fire flash placed to end on its frame started before
the frame did when Flash was longer than a frame: that part of it belonged to an exposure
already shown, and was simply gone. `--energy` at 20 ms (on a 16.7 ms frame) found it. A
flash now starts no earlier than its frame, and runs on into the next.

**A negative control clamped into silence.** The smear's first wrong model doubled a flash's
spread of angles; the flash ended on the window's end, the doubling was clamped there, and
the "wrong" model was the right one. `--negative` reported the smear check unfalsifiable.
It now halves the spread (dice's lesson again: a negative control can pass vacuously).

**A mutant the checks couldn't see.** The disc turning backwards inside an exposure
(`a + turned` for `a − turned`) passed `--drift` (its flashes sit at the window's start,
where the turn is nothing) and the smear's length (a backwards smear is as long). The
smear check now also holds the ramp's middle to the disc's angle at the flash's middle.

**Music that starts is an onset.** The audio check's printed rig heard silence while its
cards were printed, then the music: a rise from nothing, an onset, correctly. The hi-hat bed
now plays from the first frame. (A random hi-hat level every frame, as the sweep uses, fires
onsets at Threshold 0.4: the check uses a steady one, as schlieren's does.)

**Apple's GPU truncates a half-float store.** `--afterimage` found the eye seeing 0.998
of a flash over 91 frames on this GPU and 1.00000 on the software renderer: each store of
the RGBA16F afterimage loses up to a ULP the same way, and an exponential sums that bias
up. The tolerance carries it (`keep · ulp / (1 − keep)`).

**A copied line is a second mutation target.** The drum's `wallAt` started as a copy of
the disc's `cardAt`, card-dealing line and all, and `mutate.sh` refused the mutant whose
target it duplicated ("found 2 times"): a refusal, not a pass, which is what it's for. The
drum's line now says the same thing in another order.

**The drum's far angle wraps at the far point.** The harness's own geometry measured the
far wall's angle in [0, 2π), which jumps at x = 0, and bisection on it found nonsense to
the left of the middle. Wrapped to ±π.

**Apple's driver and texture 0.** A sampler left on texture 0 (the afterimage, when there
is none) is "unloadable", said once per process. The clip stands in (gauge's trap).

**Inherited from the fleet, and all still true here.** `ScopedFBOBinding` doesn't restore
the viewport; every `ffglex::Scoped*` clears to 0 on exit, so the cards and the afterimage
are allocated before anything is bound; `FFGLFBO::Release()` leaks the colour texture
(`PassBuffer::Destroy`); `SetParamInfo` clamps a STANDARD default into 0..1 (Stations is an
INTEGER, exempt); the core is an OBJECT library; `SetTextParameter` must return
`FF_SUCCESS` for the About block; the harness declares its clock unit and drives a
synthetic 60 fps clock and beat; an option reads back 0..1 whatever its count; Resolume's
clock overflows a float, so the disc's angle is reduced in double and nothing absolute
reaches the GPU; the onset detector is primed on its first frame; the clip is
premultiplied and the output alpha is decided (`--alpha`); `mutate.sh`'s summary `grep` is
wrapped `{ … || true; }` for pipefail (schlieren's fix).

## Verified, and assumed

Verified on this Mac (numbers in README Status): everything `tools/verify.sh` runs. In
words: a crawl advances by the wrapped turn per flash, a locked disc stands still, a smear
is ω τ R long and in the right place, a flash's light doesn't depend on its length, the
room light's blur keeps the ring's light, every card shows the frame it was printed with, a
hand-spun disc follows its bearing (to RK4) and stops when the closed form says, flashes
land on the beat and its subdivisions, each frame catches what its shutter window says,
onsets fire on their frame and not from a cold start, prints survive a resize, the drum's
figures stand where its geometry puts them and are squeezed by (d + a) / 2d, the alpha is
what it should be, the afterimage is an exponential that keeps the light, and the host's
GL state comes back.

Assumed, not measured:

- **That this is what a zoetrope looks like to a camera.** The kinematics are exact; the
  look of a real disc (its stock, its shadows, the head) is a model, and a dim one.
- **Printing at frame resolution**: a print is up to one host frame later than the moment
  it stands for (see the decisions).
- **The drum's hand drive** inverts the 16-piece angle table across the exposure to find
  when a slit opens; for a motor the table is exact, for the bearing's curve the error is
  its curvature over a sixteenth of a frame, ~1e-5 rad.
- **The drum's camera gain** (the slits' duty cycle) is a choice of exposure, not physics.
- **Persistence** is one exponential, the simplest eye there is.
- **Resolume's FFT bins** are unmeasured (fleet-wide), so Threshold's scale is a guess that
  worked on the harness's music.
- **More than eight flashes in one exposure** are dropped.

## Would this hold on another rasteriser, at another raster?

Every picture check runs at 1280×720 and 320×180 on this Mac's GPU and at 320×180 on
Apple's software renderer (`verify.sh`).

- `--drift`: each reading corrected by the same instrument's reading of a held twin at the
  predicted angle, on the same raster and renderer, so the pixel grid's error cancels
  wherever it is. Tolerance: four float ULPs of a 2π angle (the angle reaches the GPU as a
  float fraction of a station and a float table, twice) plus two flash moments placed from
  a float bar phase (1.2e-7 s each) at ω: 7e-6 rad. The instrument itself must read held
  angles to 0.05 px at the ring (it does to 0.02).
- `--lock`: phases at the same angle, so the grid's error is common; 0.01 px of arc,
  against ~1e-7 rad of float in the angle.
- `--smear`: the slope of a line fitted to the ramp's middle 60% (x from 0.2 to 0.8, on the
  print, clear of its edges). Tolerance from the discretisation, computed: n taps' staircase
  smoothed by the texture's bilinear tent (the narrowest smoothing the pipeline has),
  `staircaseRipple`, plus 1/512 for the GPU's 8-bit filter weights, through the fit's
  `Σ|φ − φ̄| / Σ(φ − φ̄)²`. Whole-pixel (40 px, 64 taps) and fractional (13.37 px, 16 taps)
  each get their own. The middle: the ripple over the slope.
- `--energy`: the print is 1.0 exactly in half float, a flash's weight is double; the
  encode and the harness's inverse are a few float ULPs: 1e-4. The blur's total must lie in
  the still ring's spread over 32 angles plus a quarter of it (with one tap set for every
  pixel the blurred total is an average of still ones); the spread is the grid's.
- `--delay`, `--resize`: frame numbers read as twelve thresholded stripes with a white and
  a black guard; integers. A print whose moment is within 1e-6 of a frame boundary is not
  decidable and is skipped.
- `--hand`: the twin's correction, as `--drift`; four float ULPs.
- `--alias`: the closed-form overlap of every flash with the window; 1e-4 (the encode). A
  frame with a flash within 1e-6 s of a window's edge is skipped (a float bar phase places
  a beat to ~1e-7 s).
- `--audio`: integer frames.
- `--drum`: each centre line read as the midpoint crossing of a ramp that is symmetric
  about it (16 taps over a slit's turn, a texel's tent): 0.05 px for the GPU's 8-bit
  weights (~0.02 px of a 7 px ramp) and a straight-line read across one pixel. The squeeze
  from two lines, so twice that over their spacing.
- `--alpha`: exact 0.5 and 1 (`mix` with 0 or 1).
- `--afterimage`: the afterimage is RGBA16F, so a frame's value is good to one half-float
  ULP (2^-10 of it) and a ratio of two to twice that; if every store rounds the same way
  (this GPU's do) the sum falls short by up to `keep · 2^-10 / (1 − keep)`, 5.5e-3 at
  100 ms. Measured: 0.998 here, 1.00000 on the software renderer, which rounds.
- `--bearing`, `--beat`, `--cues`, `--names`: no GL, double precision.
- `--state`: state queries.

## The negative controls and mutants

`zttest --negative` runs 17 wrong models, each a hook in the plugin (never the harness's
expectation, except the one the spec asked for), and requires each check to fail:

| wrong model | caught by |
| --- | --- |
| the expectation without the wrap: ω T itself (the spec's control) | `--drift` |
| Sync's speed worked out for N + 1 stations | `--drift`, `--lock` |
| a flash's light spread over half its overlap | `--smear` |
| a flash's light proportional to its length | `--energy` |
| a segment's taps averaged over n + 1 | `--energy` |
| the print head a station further round than Print At | `--delay` |
| the bearing without its dry friction | `--bearing`, `--hand` |
| a beat fired at the start of its frame | `--beat` |
| the window the whole frame, whatever the Shutter | `--alias` |
| the onset detector's first frame against zeroes (deaf for the first kick) | `--audio` |
| a new raster blanks the cards | `--resize` |
| the drum's far wall seen at the exposure's middle, not when each slit passed | `--drum` |
| the clip's alpha passed through everywhere | `--alpha` |
| the afterimage adding the last picture to this one instead of mixing them | `--afterimage` |
| every cue ramped | `--cues` |

`tools/mutate.sh` changes one character of the shipped code thirteen times, builds each
mutant from scratch and requires the named check to fail:

| mutant | caught by |
| --- | --- |
| GLSL the taps half a tap early, `( float( j ) + offset )` → `-` | `--hand` |
| GLSL the angle table read off its piece, `x - float( i )` → `+` | `--hand` |
| GLSL the disc turning backwards within the exposure, `a - turned` → `+` | `--smear` |
| GLSL the cards dealt to the wrong stations, `- StationOffset` → `+` | `--delay` |
| GLSL a print's average over the wrong count, `Taps * Taps` → `+` | `--energy` |
| GLSL the drum's far wall where the near wall is, `Eye + root` → `-` | `--drum` |
| C++ the viscous decay growing, `-k * s` → `+` | `--bearing` |
| C++ the head printing the wrong card, `( -m )` → `( +m )` | `--delay` |
| C++ a flash's overlap measured as nothing, `( b - a )` → `( b - b )` | `--energy` |
| C++ the beat's place in its frame from the sum of phases | `--beat` |
| C++ an onset's flash after its frame, `now - length` → `+` | `--audio` |
| C++ Sync's speed a station and a half per flash, `2.0 * kPi` → `3.0` | `--lock` |
| C++ the exposure the shutter over the frame's length, `* period` → `/` | `--alias` |

The GLSL mutants prove the harness drives the shaders the plugin ships, not a copy.

## The sweep's honest list

`tools/sweep.py --bare` (no per-control context): Rate (needs Trigger Rate), Threshold
(needs music, Trigger Onset, and Drive Hand so an onset moves something in the frame the
sweep reads), RPM (needs Sync off), Push and Friction (need Drive Hand) are dead. Flash is
alive bare only through a sub-pixel smear at the default speed; the sweep reads it on a disc
at 600 RPM. Everything else acts from the defaults. In the Drum view Flash does nothing and
the print head isn't drawn; the sweep reads the drum through View alone.

## Shape of the code

    source/
      Zoetrope.{h,cpp}   the FFGL plugin: clock, beat, audio, the passes, the drum's geometry
      Disc.{h,cpp}       the angle, the motor, the bearing's closed form, the print head's crossings
      Strobe.{h,cpp}     the flashes and the camera's exposure as light segments
      Cards.{h,cpp}      the prints: a texture array, one layer per station, carried across a resize
      Shaders.{h,cpp}    print, carry, scene (disc, drum, main) and show, assembled kVersion + kCommon + pass
      Controls.{h,cpp}   the parameter table and the units
      Audio.{h,cpp}      millpond's onset analyser, primed
      PassBuffer, GLState, Diag, PluginEntry, StoatworksAbout*
    tools/
      zttest/main.cpp    the harness: rig, clips, render, --pipe/--film/--script
      zttest/checks.inc  bookkeeping checks, --bench, --negative
      zttest/physics.inc every physics check and its instruments (the phase, the twin, the card reader)
      verify.sh, mutate.sh, sweep.py, glslc.sh

## The browser demo (2026-10-15)

`demo/` is <https://zoetrope-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules, fiche's and polyhedral's way: it **runs the plugin rather than
a port of it**. Built and tested locally on 2026-10-15, before the repository was public;
the DNS record, the deploy and a deploy workflow were left to the release.

- **`zoetrope-core.wasm` is the plugin's C++, unmodified**: Zoetrope.cpp, Disc, Strobe,
  Cards, Audio, Controls, Shaders, PassBuffer and Diag, with the SDK's
  CFFGLPluginManager / CFFGLPlugin, FFGLLog, FFGLShader, FFGLScreenQuad, FFGLFBO and the
  scoped bindings. Left out: PluginEntry.cpp (the registration and build stamp), so also
  FFGL.cpp (plugMain) and CFFGLPluginInfo. `-D__linux__` goes only to the files that
  include the SDK (Zoetrope, Cards, PassBuffer, the SDK's own, glue.cpp). Committed, with
  `demo/wasm/inputs.sha256` pinning every input; rebuild with `demo/tools/build-wasm.sh`.
- **The host** (`demo/wasm/glue.cpp`) constructs the plugin, reads its 27 declarations
  back through the SDK's host getters (the panel, Audio and the About block included, is
  built from them) and forwards SetFloatParameter, then each frame SetTime, SetBeatInfo
  and ProcessOpenGL in zttest's Rig order (the About line, the only text parameter, is
  display only and never sent). `ZOETROPE_LOG_DIR` must
  be set before the first InitGL: Diag opens its log there.
- **The transport is simulated, and is a bar phase, not only a BPM.** The page keeps a BPM
  clock (the Host BPM field, 120 at first; `?bpm=` too) and hands SetBeatInfo that BPM
  and a bar phase accumulated from the page's seconds, as the Rig does. So the plugin
  takes its host-beat path (`hostBeatSeen`), the one Resolume drives, rather than
  Strobe.cpp's own-clock fallback (which would also work, as honeydew's page does with a
  NaN phase). Accumulated, so a BPM change never jumps the phase; Restart zeroes it.
- **No audio.** Audio (FF_TYPE_BUFFER, FF_USAGE_FFT, 64 bins) is shown read-only where it
  is declared and never written, so the analyser hears silence: Trigger Onset never fires
  and Threshold does nothing on the page. Fire is a button that holds the press one frame,
  as Resolume sends an event. A press while paused lights the next Step, not the paused
  frame: an unannounced flash is placed to end on its frame, whose window has closed.
- **The shim** (`demo/wasm/gl_shim.cpp`), four entry points, polyhedral's: glShaderSource
  (the text must equal one of the page's assemblies of `shaders.js`, then the kit's
  `port()`) and glEnable / glDisable / glIsEnabled (GL_PROGRAM_POINT_SIZE, GLState.h, is
  not in WebGL2). Nothing else needed translating: the cards are a single-level RGBA16F
  array and FFGLFBO's RGBA16F with GL_FLOAT is legal in ES 3.0, so unlike fiche there is
  no texture-storage shim. Rendering into RGBA16F needs EXT_color_buffer_float (or
  _half_float); the page stops without.
- **Checked**: `demo/tools/check_shaders.py` (verify.sh) holds the 8 pieces, kVersion and
  Assemble()'s order for all 5 stages to the C++, and every input of the .wasm to its
  manifest. Negative controls, all caught: a comment added to Disc.cpp; one character of
  the GLSL (kDisc) in Shaders.cpp; a new header in source/; shaders.js edited by hand.
- **Headless, 2026-10-15** (M4 Max, Chrome 155 headless, ANGLE on Metal and on
  SwiftShader, cdpshot's CDP plumbing): every stage the plugin compiled matched the page's
  assembly; a paused frame redrawn is identical; Room Light 0.08 -> 0.6 by its slider
  changed 171k of 550k canvas pixels, View Drum 250k; Fire Only stays black until Fire,
  then one flash fires and lights the ring on the next Step; Host BPM 150 turns the disc
  at 12.5 RPM (Sync: a card per beat). No console errors or warnings on Metal. On
  SwiftShader Chrome logs four "GPU stall due to ReadPixels" performance warnings, which
  fiche's shipped page logs too: Chrome reading the WebGL canvas back to composite in
  software.
- **Not compared with zttest**, unlike fiche's page. The pre-vote wall clock and the
  synthetic beat would both have to be matched frame for frame first.
- **The clip** is the kit's generated ones (nothing to licence); Lights on black is the
  default because moving lights make the delay line round the ring visible.
- **Left to the release**: the DNS record, the deploy, `deploy.yml` (Actions only on a
  public repo), adding `zoetrope` to `resolume-demo/sync.sh`'s list, and the website's
  `demo` field.

## Not done

- **Never loaded into Resolume**, on macOS or on Windows. `oxbow` loads it, probes it and
  renders 120 frames through `plugMain`; nothing else has.
- **Windows never compiled.** CI would be the first place; it has no remote to run on.
- No user guide, OpenFX port, presets or video. No GitHub repository, tag or website
  entry: the release brief does those. The browser demo is above.

## Open design questions

- **An eye distance for the drum.** The squeeze is (d + a) / 2d: an Eye control would let
  the figures go from half width (far away) to nearly whole (close up).
- **A slit width** for the drum, trading brightness for sharpness as a real one does.
- **Accent**: the flash's energy from the onset's level (the analyser already has it).
- **Coloured strobes**: readout's gels.
- **A print from the past**: a short ring of frames (afterglow's) so a print could take
  the clip at its crossing's moment rather than the frame's.
- **Upright figures**: a carousel option, for a VJ who doesn't want half the ring upside
  down. Not a disc any more, but people may ask.

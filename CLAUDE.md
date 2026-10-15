# zoetrope

The clip's frames printed on the cards of a spinning disc, seen only in the light
of a strobe that fires on the music, for Resolume Arena/Avenue, as an FFGL
effect. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`.
MIT. ID `ZT01`, display name `SW Zoetrope`.

Read `AGENTS.md` before touching the exposure (the light segments and the time
taps), the disc's angle (it never reaches a float whole), the print head's
crossings, or the phase instrument the checks read angles with.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (writes into Arena's Extra Effects)
- Render offline: `./build/zttest --out /tmp/frame.png` (the defaults at 120 BPM on the harness's bouncing ball, 150 frames in)
- The frame a flash lands on: `--frames 152` (the beat at 2.5 s is caught by the frame after it)
- List parameters: `./build/zttest --list`
- Set anything by name: `./build/zttest --set "Trigger=Eighth Beat" --set "Slip=0.49"`
- Press something later: `./build/zttest --at 100 "Fire=1"`; music in the Audio buffer: `--kicks`
- The harness's clips: `--clip look|number|split|white` (number prints the frame number as bars)
- Film: `./build/zttest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/zttest --pipe --size WxH | ffmpeg …`

## Verify
- Everything: `tools/verify.sh` (about ten minutes: fresh universal build, every
  picture check at 1280x720 and 320x180, again on Apple's software renderer, the
  negative controls, 13 mutants, the sweep, lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest`, the pipe, the bench)
- **The disc**: `--drift` (the wrapped advance per flash), `--lock` (Slip 1 stands
  still for 100 flashes), `--delay` (frame numbers read off the cards),
  `--bearing` (no GL: the hand's closed form against RK4), `--hand` (the same, out
  of the picture, and the stop).
- **The light**: `--smear` (omega tau R, and where), `--energy` (a flash's light
  whatever its length; the room light's blur keeps the ring's light), `--alias`
  (what each frame's shutter catches), `--beat` (no GL: flashes on the beat and
  its subdivisions), `--audio`.
- **The drum**: `--drum` (the figures' centre lines against the ray geometry,
  and the squeeze against (d + a) / 2d).
- **The rest**: `--afterimage`, `--alpha`, `--resize`, `--state`, `--cues`, `--names`.
- The checks can fail: `--negative` (17 wrong models), `tools/mutate.sh` (13 mutants).
- No dead controls: `python3 tools/sweep.py` (22 swept, all live; the 23rd,
  Audio, is the input Threshold is swept through; `--bare` lists the five that
  need a context).
- Render cost: `./build/zttest --bench`.

## Notes
- **Nothing absolute reaches the GPU.** The disc's angle is kept reduced to
  [0, 2π) in double at each frame's start; the scene pass gets the reference
  angle as a whole number of stations plus a float fraction of one, and the turn
  across the exposure as a 17-entry table relative to it.
- **The exposure is a list of light segments**: the room light over the whole
  window, and each flash's overlap with it, solved in closed form on the CPU.
  The GPU averages the disc over a power-of-two set of time taps per segment,
  one per pixel of arc up to 64.
- **A card is printed in the host frame its centre crosses the print head**, with
  that frame of the clip; the crossing's moment is solved from the drive's closed
  form, not sampled. Prints live in a texture array, one layer per station, in
  linear light, and survive a resize.
- **The drum** (View Drum) is a branch of the scene pass (`kDrumFragment`): a ray
  per pixel meets the near and far walls, the slits passing the near point are
  solved from the angle table and its inverse, the far wall averaged over each
  slit's open turn. Steady light; the camera opened by the slits' duty cycle.
- **No trig in the GLSL.** A pixel's angle is `clockAngle` (arctangent by
  reduction and series), a card's frame `sinSeries`/`cosSeries` of an angle under
  π/4; `verify.sh` greps.
- All host parameters are 0..1 and mapped in `Controls.cpp`, except Stations
  (a real integer, 4–48) and Sync (a boolean); options hold the element index;
  Fire acts on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half
  layout flat packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh`
  greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no
  host can instantiate the plugin.
- `zoetrope_core` is an OBJECT library: the plugin registers itself from a
  file-scope constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `source/StoatworksAbout.h` and `ATTRIBUTIONS.md` are provisional hand copies
  (no guide button) until the project is registered with stoatworks-backend.
- Local only: no GitHub repository, no tag, no website entry yet. The workflows
  in `.github/` assume the public `stoatworks-labs/zoetrope` they are written for.

## Browser demo
- `demo/` is https://zoetrope-demo.stoatworks-labs.com (Cloudflare Worker
  `zoetrope-demo`, a ROUTE in `wrangler.toml`, which needs a proxied AAAA 100:: record
  made through the API before the first deploy -- the zone's custom domains are full,
  and without the record the page is dark with a green deploy). By hand:
  `cf-run npx wrangler deploy`.
- It runs the plugin's C++ as WebAssembly, COMMITTED (`demo/zoetrope-core.{js,wasm}`):
  after changing anything in `source/`, rerun `demo/tools/build-wasm.sh` (needs
  emscripten, `em++`); verify.sh fails until you do (`demo/wasm/inputs.sha256`).
- `demo/shaders.js` is GENERATED, pieces and `ASSEMBLY` order both: after changing
  `source/Shaders.cpp` run `python3 demo/tools/check_shaders.py --write`.
- `demo/wasm/glue.cpp` is the host (it sends SetBeatInfo from the page's BPM clock and
  never writes Audio); `demo/wasm/gl_shim.cpp` is what WebGL2 needs translated (see
  AGENTS.md). `demo/vendor/` is the shared kit: never edit it, re-vendor with
  `STOATWORKS_ROOTS=~/dev stoatworks-backend/resolume-demo/sync.sh zoetrope` (this
  repo is not on sync.sh's list yet).

## Not done yet
- Never loaded into Resolume. No host has played it any music. Windows never compiled.
- No OpenFX port, no user guide, no presets, no video.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/zoetrope/zoetrope.YYYY-MM-DD.log
    %LOCALAPPDATA%\zoetrope\logs\zoetrope.YYYY-MM-DD.log

`ZOETROPE_LOG_DIR` overrides the folder.

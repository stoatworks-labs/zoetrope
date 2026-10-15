# Attributions

Zoetrope is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The effect's shape, the harness and the tooling — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin class's shape and its table of controls, the host-clock vote, source/GLState.h, the harness's rig, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout exits 1), --negative with its Perturb, --state, --names, --cues and --offline, and tools/verify.sh, tools/mutate.sh (with its pipefail fix), tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh are schlieren's, which are fiche's and tinsel's before it, adapted. Firing a beat at the moment inside the frame where it fell (source/Strobe.cpp) is schlieren's, which is fiche's lesson.

### The audio analyser, the pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Audio.{h,cpp} is millpond's onset analyser (rosette's, adapted from macroblock's), with its primed first frame, by way of schlieren; source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's. All adapted.

### The held pictures in a texture array — Stoatworks gauge

<https://github.com/stoatworks-labs/gauge>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Cards.{h,cpp} is gauge's HeldFrames, adapted: one layer per station rather than a ring of frames, in linear light, and a reallocation that draws every print into the new array so a resize keeps them.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The classical Runge–Kutta method — Carl Runge and Martin Kutta

tools/zttest/physics.inc's Rk4Disc: an independent fourth-order solution of the bearing's equation, d omega / dt = -k omega - c, that zttest --bearing and --hand hold the hand-spun disc's closed form to (3e-11 rad over ten seconds and six pushes).

### Stroboscopic aliasing (the wagon-wheel effect)

zttest --drift: a disc turning omega T per flash appears to turn omega T modulo a station, wrapped to plus or minus half of one. Worked from the sampling, not from anyone's code.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The zoetrope, the phenakistiscope and the strobe-lit zoetrope — William George Horner, Joseph Plateau, and those after them

Horner's daedaleum (1834), the drum with slits that became the zoetrope, which the Drum view is; Plateau's phenakistiscope (1832), a disc of figures seen through slits, whose disc the Disc view keeps while trading the slits for a strobe; and the strobe-lit sculptural zoetropes of more recent years, Gregory Barsamian's and the one Pixar built in 2005, which spin a ring of figures under a strobe so they stand still and move. Built from how the things work; nothing is copied from anyone's software, images or design.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is printed onto the cards in linear light, so every average after it (a print's area average, a smear, an exposure, an afterimage) adds light, then encoded again.
- **Viscous and Coulomb friction** — d omega / dt = -k omega - c for a bearing, solved in closed form in source/Disc.cpp: omega( t ) = ( omega0 + c / k ) e^( -k t ) - c / k, stopping at ln( 1 + k omega0 / c ) / k.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.

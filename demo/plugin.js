/**
 * Zoetrope — browser demo.
 *
 * The clip's frames printed on the cards of a spinning disc, seen only in the
 * light of a strobe (SW Zoetrope, ZT01, an effect). The one idea, from
 * AGENTS.md: nothing is animated. A print head prints the clip onto each card
 * as the card passes it, the disc turns at whatever speed its drive gives it,
 * the strobe fires on the beat, and the camera sees whatever lands inside its
 * exposure. Figures that stand still and play the clip, the wagon wheel's
 * crawl, the smear of a long flash and the camera's own aliasing all fall out
 * of that; none of it is drawn.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `zoetrope-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Zoetrope.cpp` — the plugin class, with
 * its constructor and declarations, its clock and seconds-or-milliseconds
 * vote, its beat, its audio, every pass and every uniform and the drum's
 * geometry — and everything it calls: Disc (the angle, the motor, the
 * bearing's closed form, the print head's crossings), Strobe (the flashes and
 * the exposure as light segments), Cards, Audio, Controls, Shaders, PassBuffer
 * and Diag, with the FFGL SDK's CFFGLPlugin, its parameter bookkeeping,
 * FFGLShader, FFGLScreenQuad, FFGLFBO and the scoped bindings. Its GL calls go
 * to this page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin, reads its
 *     parameter declarations back through the SDK's host getters — the panel
 *     below is built from them, not from a list typed here — and forwards
 *     SetFloatParameter and, every frame, SetTime, SetBeatInfo and
 *     ProcessOpenGL, in the order tools/zttest's Rig makes them. (The one text
 *     parameter, the About line, is display only and never sent.)
 *   - **The transport.** A browser has no Resolume transport, so the page
 *     keeps a BPM clock: the Host BPM field under the picture (120 at first,
 *     the SDK's default and the harness's), and a bar phase advanced from the
 *     page's own seconds, handed to SetBeatInfo every frame as Resolume hands
 *     over its own. Simulated, and said so on the page.
 *   - **No audio.** Audio is an FFT buffer Resolume fills every frame; nothing
 *     here writes it, so the plugin's onset analyser hears silence and Trigger
 *     Onset never fires. Fire is a button.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp, Assemble() and InitGL by
 *     demo/tools/check_shaders.py), then applies the kit's `port()` — the
 *     version line and the ES precision defaults, nothing else.
 *     glEnable / glDisable / glIsEnabled: GL_PROGRAM_POINT_SIZE, which the
 *     plugin saves and restores every frame, does not exist in WebGL2.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's zt_convert pairs each control with
 *     the function ProcessOpenGL applies to it); the units printed after it,
 *     and every tooltip, are this page's words.
 *   - **The presets** are this page's, taken from the user guide's recipes and
 *     converted to the host's 0..1 by the plugin's own ParamFrom functions.
 *   - **The About block's links.** The plugin opens them with system(); the
 *     page opens the same addresses itself, read from the plugin's own table.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Kit types for FFGL types.** The kit has no event, integer or buffer
 * control. Fire and the About buttons (FF_TYPE_EVENT) are buttons: Fire sends
 * the press and holds it through one frame, releasing it on the next, as
 * Resolume sends an event; an About button sends the press and the release at
 * once, as fiche's page does. Stations (FF_TYPE_INTEGER) is a number field
 * clamped to the declared range. Audio (FF_TYPE_BUFFER, FF_USAGE_FFT) is shown
 * where the plugin declares it, read-only, saying there is no audio here —
 * rather than left out, because Trigger's Onset and Threshold refer to it.
 * The About line (FF_TYPE_TEXT) is shown read-only.
 *
 * **The transport is a bar phase, not only a BPM.** Honeydew's page hands its
 * plugin a BPM with no bar phase and lets the plugin run its own. Zoetrope
 * would do the same (Strobe.cpp's fallback is the same clock at the same BPM),
 * but Resolume sends a bar phase every frame and the plugin's beat path reads
 * it, so the page sends one, as zttest's Rig does: the plugin then takes the
 * same path it takes in Resolume. The phase is accumulated, so changing the
 * BPM changes how fast it runs from then on and never jumps it; Restart puts
 * it back to the top of the bar.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createZoetrope from './zoetrope-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_BUFFER = 12;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled stage by stage in the order
// the plugin joins the pieces (shaders.js's ASSEMBLY, read out of
// Zoetrope.cpp's InitGL and Shaders.cpp's Assemble() by check_shaders.py).
//===========================================================================

const ASSEMBLED = Object.fromEntries(
  Object.entries(S.ASSEMBLY).map(([stage, pieces]) => [stage, pieces.map((piece) => S[piece]).join('')]),
);
const FRAGMENT_STAGES = Object.keys(ASSEMBLED).filter((stage) => stage !== 'vertex');
const PAGE_SHADERS = {
  vertex: [ASSEMBLED.vertex],
  fragment: FRAGMENT_STAGES.map((stage) => ASSEMBLED[stage]),
};

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: [], refused: null };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and logs which pass) rather than
 * compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS[stage].indexOf(text);
  if (ours < 0) {
    shaderCheck.refused = { stage, length: text.length };
    return '';
  }
  shaderCheck.matched.push(stage === 'vertex' ? 'vertex' : FRAGMENT_STAGES[ours]);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createZoetrope({
    zoetropeShaderSource: shaderSource,
    print: (line) => { printed.push(line); console.log(line); },
    printErr: (line) => { printed.push(line); console.warn(line); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/zoetrope.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

/** A C string for one call: allocated, handed over, freed. */
function withCString(value, fn) {
  const pointer = core.stringToNewUTF8(value);
  try {
    return fn(pointer);
  } finally {
    core._free(pointer);
  }
}

// Diag.cpp opens its log in InitGL, so its directory must exist in the page's
// memory before the first instance is initialised.
core._zt_prepare_log();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._zt_new();
  const list = [];
  const n = core._zt_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._zt_param_type(h, i);
    const d = { index: i, name: text(core._zt_param_name(h, i)), type, group: text(core._zt_param_group(h, i)) };
    if (type === FF_TYPE_TEXT) d.default = text(core._zt_param_default_text(h, i));
    else d.default = core._zt_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._zt_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._zt_param_element_name(h, i, e)));
        d.values.push(core._zt_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_INTEGER) {
      d.min = core._zt_param_range_min(h, i);
      d.max = core._zt_param_range_max(h, i);
    }
    if (type === FF_TYPE_BUFFER) {
      d.bins = core._zt_param_element_count(h, i);
      d.usage = core._zt_param_usage(h, i);
    }
    list.push(d);
  }
  const maxInputs = core._zt_max_inputs(h);
  core._zt_delete(h);
  return { list, maxInputs };
}

const DECLARED = declarations();

// The About block: its first index, and the address each button opens, from
// the plugin's own table (StoatworksAboutLinks.h, compiled in). A label that
// does not match the declaration is a page bug, not something to paper over.
const ABOUT_FIRST = core._zt_about_first();
const ABOUT_LINKS = new Map();
for (const d of DECLARED.list) {
  if (d.index <= ABOUT_FIRST || d.type !== FF_TYPE_EVENT) continue;
  const button = d.index - ABOUT_FIRST - 1;
  const label = text(core._zt_about_button_label(button));
  if (label !== d.name) throw new Error(`About button ${button} is "${label}" in the plugin's table and "${d.name}" in its declaration`);
  ABOUT_LINKS.set(d.index, text(core._zt_about_button_url(button)));
}

const indexOf = (name) => {
  const d = DECLARED.list.find((x) => x.name === name);
  if (!d) throw new Error(`the plugin declares no "${name}"`);
  return d.index;
};

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const UNITS = {
  Rate: (x) => `${fixed(x, x < 10 ? 2 : 1)} Hz`,
  Threshold: (x) => `sens. ${fixed(x, 2)}`,
  Flash: (x) => (x < 0.9995e-3 ? `${fixed(1e6 * x, 0)} µs` : `${fixed(1e3 * x, 2)} ms`),
  Strobe: (x) => `${fixed(x, 2)}`,
  Slip: (x) => `${fixed(x, 3)} cards`,
  RPM: (x) => `${fixed(x, 1)} RPM`,
  Push: (x) => `${fixed(x, 1)} RPM`,
  Friction: (x) => `k ${fixed(x, 2)}/s`,
  'Print At': (x) => `${fixed(x, 1)}°`,
  Radius: (x) => `${fixed(x, 3)}`,
  'Card Size': (x) => `${fixed(x, 3)}`,
  Shutter: (x) => `${fixed(x, 0)}°`,
  'Room Light': (x) => `${fixed(100 * x, 1)}%`,
  Persistence: (x) => (x === 0 ? 'off' : `${fixed(1000 * x, 0)} ms`),
};

function readout(d) {
  return (v) => {
    const f = Math.fround(v);
    const x = core._zt_convert(d.index, f);
    if (Number.isNaN(x)) return fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the README's, AGENTS.md's and the user guide's account of each control. */
const HINTS = {
  Trigger: 'What fires the strobe. Beat, Half Beat, Quarter Beat, Eighth Beat and Bar follow the transport — on this page the Host BPM clock under the picture, in Resolume the composition’s tempo. Onset fires on hits in Audio, which a browser does not have, so here it never fires. Rate runs free at Rate. Fire Only fires only on Fire.',
  Rate: 'The free-running strobe, 0.25 to 200 Hz, geometrically (about 7 Hz at the middle). Trigger Rate only. Changing it changes how fast the strobe runs from then on and never jumps it.',
  Fire: 'A flash now, whatever Trigger is set to; with Drive on Hand, a push as well. FF_TYPE_EVENT: the button sends the press and releases it on the next frame, as Resolume sends an event. A press while paused lights the next Step.',
  Audio: 'Resolume’s audio input: an FFT buffer of 64 bins (FF_TYPE_BUFFER) the host fills every frame. A browser has no Resolume FFT, so this page never writes it: the plugin’s analyser hears silence and Onset never fires.',
  Threshold: 'How hard a hit must be to count as an onset (Trigger Onset only). The number beside it is the analyser’s sensitivity, 1 − Threshold. With no audio on this page it changes nothing.',
  Flash: 'How long each flash lasts, 10 µs to 20 ms, geometrically. Every flash carries the same light, so a longer one smears the cards further without making them brighter.',
  Strobe: 'The light one flash gives, 0 to 2. At 1 a flash caught whole shows a card at its print’s own brightness. In the Drum view, the lamp’s level.',
  Stations: 'How many cards, 4 to 48. FF_TYPE_INTEGER in the plugin. A change is a new disc: every card goes back to bare stock and fills in as it passes the head, and a hand-spun disc stops.',
  Drive: 'Motor: a steady speed, Sync’s or RPM’s. Hand: a push of Push on each flash of a beat-type Trigger, each onset and each Fire (on the beat with Trigger Rate), and the bearing slowing it in between.',
  Sync: 'On: the motor turns Slip cards per flash at the transport’s tempo (one over Rate on Rate, one beat on Onset and Fire Only). Off: RPM.',
  Slip: 'Cards turned per flash with Sync on, 0 to 2. 1 stands still and plays the clip; 0.98 crawls backwards and 1.02 forwards; 2 looks locked and skips a card; 0 stops the disc and freezes the ring.',
  RPM: 'The motor’s speed with Sync off, 0 to 600 RPM.',
  Push: 'What one push adds to the disc’s speed, 1 to 120 RPM, geometrically. Drive Hand only.',
  Friction: 'The bearing: k, the viscous rate, 0.05 to 4 per second geometrically, with dry friction c = 0.25 k rad/s², so the disc stops in a finite time rather than slowing for ever. Drive Hand only.',
  'Print At': 'The print head’s angle, clockwise from twelve o’clock. Each card is printed with the clip as it passes it.',
  Radius: 'The ring of card centres, 0.15 to 1 of half the frame’s shorter side (the number beside it). In the Drum view, how wide the drum looks.',
  'Card Size': 'The card’s width, 0.2 to 1 of the widest card that cannot touch its neighbours (the number beside it).',
  Shutter: 'The camera’s exposure, 0 to 360° of the frame. At 360° (the default) no flash falls between two exposures.',
  'Room Light': 'A dim steady light, 0 to 1 (1 lights the disc as fully as a flash over a 360° exposure): what shows the disc turning between flashes, as a blur.',
  Persistence: 'The eye’s afterimage, 0 to 200 ms. Off is the honest camera: one lit frame and a few dark ones.',
  Background: 'Black, or the clip behind the disc at its own brightness, with its alpha coming through outside the disc.',
  Mix: 'The effect against the clip. At 0 the clip is returned unchanged.',
  View: 'Disc: the cards face on, under the strobe. Drum: the same cards inside a drum, seen through slits in its near wall, under steady light.',
  About: 'The plugin’s own About line (FF_TYPE_TEXT, display only).',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_INTEGER]: 'text',
  [FF_TYPE_TEXT]: 'text',
  [FF_TYPE_BUFFER]: 'text',
};

/**
 * The kit reads `embed`, `size`, `clip` and `bg` from the query string itself,
 * and this page reads `bpm`; a parameter id equal to one would be set by it.
 */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'bpm']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_INTEGER: return String(Math.round(d.default));
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    case FF_TYPE_BUFFER: return '';
    default: return d.default;
  }
}

const PARAMS = DECLARED.list.map((d) => {
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (p.type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  if (d.type === FF_TYPE_BUFFER) p.placeholder = 'none: no audio in a browser';
  if (ABOUT_LINKS.has(d.index)) p.hint = `Opens ${ABOUT_LINKS.get(d.index)} — the address the plugin’s own button opens.`;
  return p;
});
const PARAM = Object.fromEntries(PARAMS.map((p) => [p.name, p]));

//===========================================================================
// The presets: the user guide's recipes, converted to the host's 0..1 by
// Controls.cpp's own inverses (zt_param_from). An option is its element's
// index; Strobe has no inverse in the plugin and is given as the slider
// position the guide names.
//===========================================================================

const from = (name, x) => {
  const v = core._zt_param_from(indexOf(name), x);
  if (Number.isNaN(v)) throw new Error(`no inverse for ${name}`);
  return Math.fround(v);
};
const element = (name, label) => {
  const i = PARAM[name].elements.indexOf(label);
  if (i < 0) throw new Error(`${name} has no element "${label}"`);
  return i;
};

const PRESETS = {
  'Locked, on every eighth of a beat': {
    trigger: element('Trigger', 'Eighth Beat'),
  },
  'Crawling backwards (Slip 0.98)': {
    trigger: element('Trigger', 'Eighth Beat'),
    slip: from('Slip', 0.98),
  },
  'Crawling forwards (Slip 1.02)': {
    trigger: element('Trigger', 'Eighth Beat'),
    slip: from('Slip', 1.02),
  },
  'Skipping a card (Slip 2)': {
    slip: from('Slip', 2.0),
  },
  'Smeared: a 20 ms flash at 300 RPM': {
    sync: 0,
    rpm: from('RPM', 300),
    flash: from('Flash', 20e-3),
  },
  'Hand-spun, pushed on the beat': {
    drive: element('Drive', 'Hand'),
  },
  'Hand-spun on Fire only': {
    drive: element('Drive', 'Hand'),
    trigger: element('Trigger', 'Fire Only'),
  },
  'Persistence, the eye in the room': {
    trigger: element('Trigger', 'Eighth Beat'),
    persistence: from('Persistence', 0.15),
    strobe: 0.85,
  },
  'The strobe against the camera: 61.7 Hz through 90°': {
    trigger: element('Trigger', 'Rate'),
    rate: from('Rate', 61.7),
    shutter: from('Shutter', 90),
  },
  'Two flashes a frame: 157 Hz through 200°': {
    trigger: element('Trigger', 'Rate'),
    rate: from('Rate', 157),
    shutter: from('Shutter', 200),
    slip: from('Slip', 0.3),
    cardsize: from('Card Size', 0.5),
  },
  'The drum': {
    view: element('View', 'Drum'),
    trigger: element('Trigger', 'Eighth Beat'),
    persistence: from('Persistence', 0.12),
  },
  'The disc over the clip': {
    background: element('Background', 'Clip'),
  },
};

const query = new URLSearchParams(window.location.search);

//===========================================================================
// The transport: the page's BPM clock, standing in for Resolume's.
//===========================================================================

const BPM_LOW = 20;
const BPM_HIGH = 300;
const queryBpm = Number(query.get('bpm'));
const transport = {
  bpm: Number.isFinite(queryBpm) && queryBpm >= BPM_LOW && queryBpm <= BPM_HIGH ? queryBpm : 120,
  bars: 0,///< bars since the top, accumulated from the page's seconds
  last: null,///< the page's seconds at the last frame
  /** The bar phase at the page's `seconds`: advanced at the BPM, four beats to the bar. */
  advance(seconds) {
    if (this.last === null || seconds < this.last) this.bars = 0;// a start, or Restart
    else this.bars += ((seconds - this.last) * this.bpm) / 240;
    this.last = seconds;
    return this.bars - Math.floor(this.bars);
  },
};

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v) {
  const d = p.ff;
  if (d.index === ABOUT_FIRST) return null;// display only
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_BUFFER: return null;// the page has no audio to write into it
    case FF_TYPE_INTEGER: {
      const n = Number.parseInt(String(v).trim(), 10);
      if (!Number.isFinite(n)) return null;
      return Math.min(d.max, Math.max(d.min, n));
    }
    case FF_TYPE_TEXT: return String(v);
    default: return Math.fround(v);
  }
}

function createRenderer(gl) {
  // The cards are held in linear light in an RGBA16F texture array the plugin
  // renders into, and so is the afterimage. WebGL2 renders to a float target
  // only with one of these.
  if (!gl.getExtension('EXT_color_buffer_float') && !gl.getExtension('EXT_color_buffer_half_float')) {
    throw new GLError('This browser cannot render to a half-float texture (neither EXT_color_buffer_float nor EXT_color_buffer_half_float), and the plugin prints its cards in linear light into RGBA16F. An 8-bit stand-in would be a plausible wrong picture, so the demo stops here.');
  }

  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  let instance = 0;
  let pushed = new Map();
  let releases = [];
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let barPhase = 0;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height) {
    instance = core._zt_new();
    pushed = new Map();
    releases = [];
    frames = 0;
    if (!core._zt_init_gl(instance, width, height)) {
      const why = shaderCheck.refused
        ? `the ${shaderCheck.refused.stage} shader text the compiled plugin handed to GL (${shaderCheck.refused.length} characters) is not this page’s checked copy of source/Shaders.cpp, so it was not compiled`
        : 'the plugin’s InitGL failed — its log line under the picture says which pass';
      throw new GLError(`SW Zoetrope could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      if (typeof value === 'string') withCString(value, (s) => core._zt_set_text(instance, p.ff.index, s));
      else core._zt_set_float(instance, p.ff.index, value);
    }
  }

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      if (!instance) start(width, height);
      push(params);
      barPhase = transport.advance(time);
      const clip = glName(input.texture);
      if (!core._zt_process(instance, time, Math.fround(transport.bpm), Math.fround(barPhase), clip, input.width, input.height)) {
        throw new GLError('The plugin’s ProcessOpenGL failed.');
      }
      // A press is held through exactly one frame: Resolume sends 1 on one
      // frame and 0 on the next.
      for (const index of releases) core._zt_set_float(instance, index, 0);
      releases = [];
      // For anything checking the page from outside: frames this instance drew.
      frames += 1;
      gl.canvas.dataset.frames = String(frames);
    },
    /** Fire: 1 now, 0 after the next frame. */
    press(index) {
      if (!instance) return false;
      core._zt_set_float(instance, index, 1);
      releases.push(index);
      return true;
    },
    /** An About button: the press and the release at once (the plugin acts on the press). */
    click(index) {
      if (!instance) return;
      core._zt_set_float(instance, index, 1);
      core._zt_set_float(instance, index, 0);
    },
    get barPhase() { return barPhase; },
    /** What the apparatus is doing, through the harness's accessors. */
    state() {
      if (!instance) return null;
      const s = (k) => core._zt_state(instance, k);
      return { angle: s(0), rpm: s(1), fired: s(2), dropped: s(3), frames: s(4), now: s(5), printed: s(6), stations: s(7), caught: s(8) };
    },
    log() {
      try {
        return core.FS.readFile(text(core._zt_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

let renderer = null;

const mounted = mountDemo({
  name: 'Zoetrope',
  pluginId: 'ZT01',
  kind: 'effect',
  tagline:
    'A spinning disc of your clip’s frames, for Resolume, seen only in the light of a strobe that fires on the beat. A print head prints the clip onto each card as it passes, the disc turns, and the camera sees whatever lands in its exposure. Turn exactly one card per flash and the figures stand still and play the clip, each a flash later than the one before it round the ring; a touch slow and they crawl backwards, a touch fast and forwards; a long flash smears them. None of it is drawn.',
  repo: 'https://github.com/stoatworks-labs/zoetrope',

  blurb:
    'It is the plugin’s own C++ — the plugin class, the disc, the strobe, the cards and the parameter table, with the FFGL SDK classes it uses — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: its tempo is a BPM clock the page keeps, there is no audio, the clock is the browser’s and the clip is generated in the page.',

  sources: ['spot', 'alpha', 'grid', 'scene', 'bars'],

  params: PARAMS,
  presets: PRESETS,
  showBackdrop: true,

  differences: [
    'This page runs the plugin rather than a port of it. zoetrope-core.wasm is source/Zoetrope.cpp — the plugin class: its constructor and declarations, its clock and unit vote, its beat and audio handling, every pass and every uniform, the drum’s geometry — and everything it calls (Disc, Strobe, Cards, Audio, Controls, Shaders, PassBuffer, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader, FFGLScreenQuad, FFGLFBO and scoped bindings, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). Of source/ only PluginEntry.cpp, the registration and build stamp, is left out, and with it the SDK’s plugMain (FFGL.cpp) and CFFGLPluginInfo: the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
    'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order Assemble() joins the pieces in, and the repository’s verify script fails if a character differs or if the WebAssembly was built from sources that have since changed. At start-up the page also requires each text the compiled plugin hands to glShaderSource to equal its own assembly of that copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2 (it reads as off here; the plugin draws no points).',
    'The plugin prints its cards into an RGBA16F texture array and keeps the afterimage in RGBA16F, rendering into both, which WebGL2 allows only with EXT_color_buffer_float or EXT_color_buffer_half_float. Without either the page stops rather than run on an 8-bit stand-in.',
    'The tempo is simulated. Resolume hands the plugin its BPM and bar phase every frame (SetBeatInfo); a browser has no transport, so this page keeps a BPM clock of its own — the Host BPM field under the picture, 120 at first — and hands the plugin that BPM and a bar phase advanced from the page’s own seconds, as the plugin’s harness does. The plugin cannot tell it from Resolume’s: Beat, Half Beat, Quarter Beat, Eighth Beat and Bar fire on its boundaries, Sync turns the disc at its tempo, and on Hand the disc is pushed on its beats. It is not the composition’s tempo and not anything you can hear: there is no tap tempo, no nudge and no music it follows. Pause stops it with the clock, a changed BPM runs from then on without a jump, and Restart puts it back to the top of a bar.',
    'There is no audio. The plugin declares Audio, an FFT buffer of 64 bins that Resolume fills every frame; it is shown on the panel where the plugin declares it, but nothing on this page writes it, so the plugin’s onset analyser hears silence. Trigger Onset therefore never fires, Threshold changes nothing, and on Hand no onset ever pushes the disc. Fire is the only unannounced flash here: a button that sends the press and releases it on the next frame, as Resolume sends an event. It fires in every Trigger mode, and on Hand it is a push as well. A press while paused is placed to end on the paused frame, which its exposure has already closed, so it lights the next Step instead.',
    'The clock is the browser’s: SetTime is handed seconds summed from the display’s frames (a frame capped at 0.1 s), and the plugin’s own vote settles on seconds within four frames, running on its wall clock (here performance.now()) until it does. The camera’s exposure is Shutter × the length of each frame, so what a frame catches — and the strobe-against-the-camera aliasing the Rate presets show — is against this browser’s frame rate and its jitter, not Resolume’s. Pause stops the host clock and the plugin sees no time pass: the disc holds still and the frame stays as it was. A control moved while paused redraws that instant. Step advances 1/60 s. Restart is a backward jump, which the plugin takes as no time passing: the disc carries on where it was.',
    'Stations is FF_TYPE_INTEGER in the plugin and a number field here, clamped to its declared 4–48. Fire and the About buttons are FF_TYPE_EVENT. The About block (a text line and three buttons) is the plugin’s, and is on the panel because Resolume shows it; pressing a button sends the press to the plugin, as Resolume would, and the plugin’s handler opens the link by asking the operating system (std::system), which a browser does not have — so there nothing happens, and the page opens the same address itself, read from the plugin’s own About table (StoatworksAboutLinks.h, compiled in).',
    'The clip is the kit’s generated one, premultiplied, or your own image or video, handed to the plugin unpadded (Width = HardwareWidth); a host may pad it. A card is printed with the frame of the clip the page hands over on the frame its centre passes the head, as in Resolume. The output is the page’s 8-bit canvas, and outside the disc with Background on Clip the clip’s alpha shows the backdrop chosen under the picture.',
    'Nothing on this page has been compared with the plugin’s own harness, zttest, pixel for pixel. WebAssembly has no fused multiply-add and WebGL2 is GLSL ES 3.00 on whatever GPU you have, so the disc’s angle and the flashes’ moments can differ from a Mac’s in the last bits; the models and every constant are the same code. What the render costs in a browser is not measured either: the scene pass takes up to 64 time taps per light segment, and a fast strobe of long flashes at 1920 × 1080 is a workstation GPU’s job.',
    'The plugin itself has never been loaded into Resolume (its README’s Status says so). Its harness, zttest, drives the same classes headlessly and measures each claim out of the picture — the wrapped advance per flash, a locked disc standing still, the smear’s ω τ R, a flash’s light whatever its length, the frame each card was printed with, the bearing against RK4, the drum’s squeeze — and that harness, not this page, is the reason to believe it.',
  ],

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// The controls the kit has no type for, the About block, the transport's
// BPM, and the plugin's state and log.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';

  // For anything checking the page from outside (a headless browser reading
  // the plugin's own accessors): the kit's params and state, and the renderer.
  window.zoetropeDemo = { params, state: mounted.state, redraw: mounted.redraw, transport, shaderCheck, get renderer() { return renderer; } };

  if (!embed) {
    const button = (label, title) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.textContent = label;
      if (title) b.title = title;
      return b;
    };
    const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
      ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

    for (const p of PARAMS) {
      const row = rowOf(p);
      if (!row) continue;

      // FF_TYPE_EVENT: Fire is a press held through one frame. An About
      // button sends its press and opens the address the plugin's own press
      // would have opened.
      if (p.ff.type === FF_TYPE_EVENT) {
        const toggle = row.querySelector('.prow__toggle');
        const url = ABOUT_LINKS.get(p.ff.index);
        let press;
        if (url) {
          press = document.createElement('a');
          press.href = url;
          press.target = '_blank';
          press.rel = 'noopener';
          press.textContent = 'Open';
          press.title = p.hint;
          press.addEventListener('click', () => renderer?.click(p.ff.index));
        } else {
          press = button(p.name, p.hint);
          press.addEventListener('click', () => {
            if (renderer?.press(p.ff.index)) mounted.redraw();
          });
        }
        press.className = 'prow__toggle';
        press.dataset.press = p.id;
        press.style.textAlign = 'center';
        press.style.textDecoration = 'none';
        toggle?.replaceWith(press);
        continue;
      }

      const field = row.querySelector('.prow__text');
      if (!field) continue;

      // The About line and Audio: the plugin's, display only.
      if (p.ff.index === ABOUT_FIRST || p.ff.type === FF_TYPE_BUFFER) {
        field.readOnly = true;
        field.tabIndex = -1;
        if (p.ff.type === FF_TYPE_BUFFER) field.title = p.hint;
        continue;
      }

      // FF_TYPE_INTEGER: a number field in the declared range.
      if (p.ff.type === FF_TYPE_INTEGER) {
        field.type = 'number';
        field.min = String(p.ff.min);
        field.max = String(p.ff.max);
        field.step = '1';
        field.inputMode = 'numeric';
      }
    }

    //-----------------------------------------------------------------------
    // The transport's BPM: what Resolume's would hand the plugin.
    //-----------------------------------------------------------------------
    const bar = document.querySelector('.transport');
    if (bar) {
      const label = document.createElement('label');
      label.className = 'transport__field';
      const span = document.createElement('span');
      span.className = 'transport__label';
      span.textContent = 'Host BPM';
      const bpm = document.createElement('input');
      bpm.type = 'number';
      bpm.className = 'prow__text';
      bpm.min = String(BPM_LOW);
      bpm.max = String(BPM_HIGH);
      bpm.step = '1';
      bpm.value = String(transport.bpm);
      bpm.inputMode = 'numeric';
      bpm.style.width = '6em';
      bpm.style.flex = '0 0 auto';
      bpm.id = 'zoetrope-bpm';
      label.title = 'A simulated transport: the BPM, and a bar phase from this page’s clock, handed to the plugin every frame (SetBeatInfo) as Resolume hands over the composition’s. The beat-type Triggers fire on it and Sync turns the disc at it. Not anything you can hear.';
      bpm.addEventListener('change', () => {
        const v = Number(bpm.value);
        if (Number.isFinite(v) && v >= BPM_LOW && v <= BPM_HIGH) {
          transport.bpm = v;
          if (!mounted.state.playing) mounted.redraw();
        } else {
          bpm.value = String(transport.bpm);
        }
      });
      label.append(span, bpm);
      bar.append(label);
    }

    //-----------------------------------------------------------------------
    // What the apparatus is doing (the harness's accessors), and the plugin's
    // own log (Diag.cpp). Both report; neither measures.
    //-----------------------------------------------------------------------
    const stage = document.querySelector('.stage');
    if (stage) {
      const status = document.createElement('p');
      status.className = 'stage__status';
      status.id = 'zoetrope-state';
      status.setAttribute('aria-live', 'off');
      const log = document.createElement('p');
      log.className = 'stage__status';
      log.id = 'zoetrope-log';
      log.setAttribute('aria-live', 'off');
      log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
      log.style.fontSize = '12px';
      log.style.whiteSpace = 'pre-wrap';
      stage.append(status, log);
      let lastLog = '';
      setInterval(() => {
        const s = renderer?.state();
        if (s && Number.isFinite(s.angle)) {
          const bars = transport.bars;
          const beat = Math.floor((bars - Math.floor(bars)) * 4) + 1;
          const dropped = s.dropped > 0 ? ` (${s.dropped} dropped over eight in one exposure)` : '';
          status.textContent =
            `The plugin reports: the disc at ${fixed(s.angle, 1)}°, turning at ${fixed(s.rpm, 1)} RPM · `
            + `${s.printed} of ${s.stations} cards printed · ${s.fired} flash${s.fired === 1 ? '' : 'es'} fired${dropped}, `
            + `${s.caught} in the last exposure · its clock at ${fixed(s.now, 2)} s. `
            + `The page’s transport: ${transport.bpm} BPM, bar ${Math.floor(bars) + 1} beat ${beat}.`;
        }
        const all = renderer?.log() ?? '';
        if (all === lastLog) return;
        lastLog = all;
        const lines = all.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) zoetrope: /, '$1 '));
        log.textContent = lines.length
          ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}`
          : '';
      }, 250);
    }
  }
}

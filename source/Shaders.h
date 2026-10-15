#pragma once

#include <string>

/**
	The passes.

	1. **print** -- the clip into one card's layer of the card array, as an
	   area average over each card texel's footprint, in linear light, alpha 1
	   (printed). Only on a frame a card passes the print head.
	2. **carry** -- one layer of the old card array into the new one, when the
	   cards' size changes (a resize keeps the prints).
	3. **scene** -- per output pixel, the camera's exposure: the disc at the
	   moments of each light segment (the room light's whole window, each
	   flash's overlap with it), the card under the pixel found by its angle,
	   averaged over a set of time taps, times the segment's weight, summed.
	   Straight to the host (encoded, mixed), or with Persistence on into the
	   afterimage buffer, blended with the last one.
	4. **show** -- with Persistence on, the afterimage to the host.

	The fragment shaders are assembled from pieces at InitGL -- kVersion +
	kCommon + the pass -- by `Assemble()`, the one place the order is written.
	tools/glslc.sh's ASSEMBLED table mirrors it, so the text it compiles is
	the text the plugin runs.

	**No sin, cos, tan or atan** (GLSL promises nothing about their accuracy,
	and verify.sh greps for them): a pixel's angle comes from `clockAngle`, an
	arctangent by argument reduction and its series; a card's frame from
	`sinSeries` and `cosSeries` of an angle never more than pi / 4.
*/
namespace zoetrope::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kPrintFragment;
extern const char* const kCarryFragment;
extern const char* const kSceneFragment;
extern const char* const kShowFragment;

enum class Pass
{
	Print,
	Carry,
	Scene,
	Show
};
std::string Assemble( Pass pass );

/// Entries in the angle table across the exposure: 16 pieces, linear in
/// between (exact for Motor; for Hand the bearing's curvature over a
/// sixteenth of a frame, ~1e-5 rad at its worst).
constexpr int kAngleTable = 17;
/// Light segments per exposure: the room light and up to eight flashes.
constexpr int kMaxSegments = 9;
/// Time taps per segment at most.
constexpr int kMaxTaps = 64;

/// Negative-control hooks: bits of the `Hooks` uniform. A shipped instance
/// sets 0, and every branch they guard is then the shipped model.
enum Hook : int
{
	kHookTapWeights = 1 << 0///< a segment's taps averaged over n + 1, not n
};

} // namespace zoetrope::shaders

#pragma once

#include "Controls.h"

#include <deque>
#include <vector>

/**
    The light: when the strobe fires, and what the camera's exposure catches
    of it and of the room light. Plain C++, no GL, so `zttest --offline`
    checks it.

    **A flash** is a rectangle of light Flash seconds long that starts at its
    moment, with the same energy whatever its length: the light per flash does
    not change with Flash, only the smear does.

    **When.** A beat-type trigger reads the host's bar phase every frame; a
    boundary crossed between two frames fires at the moment inside the frame
    where the phase crossed it, interpolated (schlieren's, which is fiche's
    lesson). Rate runs a phase of its own at Rate hertz, placed the same way.
    An onset and a press of Fire carry no moment of their own: their flash is
    placed to END at this frame's time, so the frame that heard it shows it
    (whenever the shutter is at least as long as the flash) -- but never to
    start before the frame did, so a flash longer than a frame runs on into
    the next and none of its light is lost.

    **The exposure** of a frame is the window [ now - S, now ], S = Shutter x
    the frame's length. What it catches is a list of light segments, each a
    stretch of the window and a weight:

        room light:  the whole window, weight RoomLight x S / T
        a flash:     its overlap [ a, b ] with the window,
                     weight Strobe x ( b - a ) / Flash

    so a pixel is the sum over segments of weight x the mean albedo it saw
    during that segment. The overlaps are solved in closed form; nothing about
    where a flash falls is sampled. A flash longer than the gap between two
    frames' windows is split between them and the two halves add up to one
    flash.
*/
namespace zoetrope
{
struct FlashEvent
{
	double start;   ///< host seconds
	double length;  ///< seconds
};

/// What one frame knows about the beat, the audio and the controls.
struct StrobeInputs
{
	Trigger trigger = Trigger::Beat;
	double rate     = 12.0;///< Hz, for Trigger Rate
	int presses     = 0;   ///< Fire, since the last frame
	bool onset      = false;
	double barPhase = 0.0; ///< 0..1
	double bpm      = 120.0;
	bool hostBeat   = false;///< the host has been moving barPhase
	double flash    = 1e-3; ///< seconds
};

/// One stretch of the exposure and the light it carries. Times are seconds
/// relative to the frame's time (so both are <= 0).
struct LightSegment
{
	double from;
	double to;
	double weight;
	bool flash;///< false for the room light
};

/// Hooks for the negative controls. A shipped instance sets 0.
enum StrobeHook : int
{
	kHookBeatAtFrameStart = 1 << 0,///< a beat fires at the start of its frame, not where it fell
	kHookLightPerFlashTau = 1 << 1,///< a flash's light proportional to its length (no 1 / Flash)
	kHookShutterIgnored   = 1 << 2,///< the window is the whole frame whatever the Shutter
	kHookSmearDoubled     = 1 << 3 ///< a flash's light spread over twice its overlap
};

class Strobe
{
public:
	/// The most flashes one exposure takes; any more are dropped (and
	/// counted). At 200 Hz and 30 fps a 360-degree window holds seven.
	static constexpr int kMaxFlashesPerWindow = 8;

	/// One host frame ending at `now`, `dt` long. Fires whatever is due and
	/// returns, in `pushes`, the moments (seconds after the frame's start) a
	/// hand-spun disc is pushed: each flash on a beat-type trigger, each
	/// onset's, each press's, and with Trigger Rate each beat of the host.
	void Advance( double now, double dt, const StrobeInputs& in, std::vector< double >& pushes );

	/// The window [ now - shutter, now ] as light segments. `period` is the
	/// frame's length, which the room light is normalised to.
	std::vector< LightSegment > Exposure( double now, double shutter, double period, double roomLight, double strobe ) const;

	const std::deque< FlashEvent >& Flashes() const
	{
		return flashes;
	}
	/// Flashes fired since the plugin loaded.
	unsigned long long Fired() const
	{
		return fired;
	}
	/// Flashes dropped from an exposure over kMaxFlashesPerWindow.
	unsigned long long Dropped() const
	{
		return dropped;
	}

	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	int Hooks() const
	{
		return hooks;
	}

private:
	void fire( double start, double length );
	static double unannounced( double now, double dt, double length );
	void crossings( double p0, double p1, double step, double frameStart, double dt, std::vector< double >& at ) const;

	std::deque< FlashEvent > flashes;
	double lastPhase = -1.0;///< the bar phase at the last frame
	double ratePhase = 0.0; ///< Rate's own, in flashes
	bool rateStarted = false;
	unsigned long long fired   = 0;
	mutable unsigned long long dropped = 0;
	int hooks = 0;
};

} // namespace zoetrope

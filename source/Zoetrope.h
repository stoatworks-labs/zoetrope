#pragma once

#include "Audio.h"
#include "Cards.h"
#include "Controls.h"
#include "Disc.h"
#include "PassBuffer.h"
#include "Shaders.h"
#include "Strobe.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <vector>

namespace zoetrope
{
/// The card's margin round its print, as a fraction of the print's height.
constexpr double kMargin = 0.06;

/// The drum's eye: its distance from the drum's axis, in drum radii.
constexpr double kEye = 3.0;
/// A slit's width, as a fraction of a station.
constexpr double kSlit = 1.0 / 16.0;

/// The drum as of the last frame: lengths in drum radii (an arc is its
/// angle), the focal length in pixels per unit of a ray's slope.
struct DrumGeometry
{
	double eye = kEye;
	double focal = 1.0;
	double cardHalfWidth = 0.0, cardHalfHeight = 0.0;
	double printHalfWidth = 0.0, printHalfHeight = 0.0;
	double stripHalf = 0.0, bandHalf = 0.0, drumHalf = 0.0;
	double slitHalf = 0.0;
	double gain = 1.0;
};

/// Where the disc sits on the output raster, in pixels, as of the last frame.
struct Geometry
{
	double centreX = 0.0, centreY = 0.0;///< from the bottom-left corner
	double ring    = 0.0;               ///< the card centres' radius
	double halfWidth = 0.0;             ///< half a card, across (tangential)
	double halfHeight = 0.0;            ///< half a card, up (radial)
	double printHalfWidth  = 0.0;       ///< half the print on it, across
	double printHalfHeight = 0.0;       ///< and up
	double discRadius = 0.0;
	double headSize   = 0.0;
	int stations      = 12;
	double pitch      = 0.0;            ///< 2 pi / N
};

/**
    The plugin.

    Three models in sequence, each in its own file: the light (Strobe.h:
    when the strobe fires and what the camera's window catches), the disc
    (Disc.h: its angle, its drive and the print head), and the picture
    (Shaders.h: the exposure integrated per pixel on the GPU). Cards.h holds
    the prints. See AGENTS.md for why each is built the way it is.
*/
class ZoetropePlugin : public CFFGLPlugin
{
public:
	ZoetropePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters on
	/// a fresh instance and deletes the instance if one fails, and
	/// CFFGLPlugin's SetTextParameter is a stub that returns exactly that
	/// failure -- so without this override no real host can load the plugin,
	/// while every offline harness here carries on passing.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these;
	// they exist so `zttest` can measure what the shipping passes produced,
	// rather than a copy of it computed for the test.
	//-------------------------------------------------------------------

	/// The offline harness DECLARES its clock unit (seconds) rather than
	/// letting the plugin vote on it against the wall clock -- a harness
	/// renders hundreds of frames a second, which the vote would read as
	/// milliseconds.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	void SetDiscHooksForTest( int bits )
	{
		disc.SetHooksForTest( bits );
	}
	void SetStrobeHooksForTest( int bits )
	{
		strobe.SetHooksForTest( bits );
	}
	void SetUnprimedForTest( bool unprimed )
	{
		analyser.SetUnprimedForTest( unprimed );
	}
	/// The --resize negative control: a new raster blanks the cards.
	void SetResizeForgetsForTest( bool forgets )
	{
		resizeForgets = forgets;
	}
	/// Hold the disc at an angle (stopped) from the next frame: the harness
	/// calibrates its angle instrument on renders of known angles.
	void SetDiscAngleForTest( double radians )
	{
		heldAngle = radians;
	}

	const Disc& DiscState() const
	{
		return disc;
	}
	const Strobe& StrobeState() const
	{
		return strobe;
	}
	const Cards& CardState() const
	{
		return cards;
	}
	const Geometry& LastGeometry() const
	{
		return geometry;
	}
	const DrumGeometry& LastDrum() const
	{
		return drum;
	}
	/// The light segments the last frame was exposed to.
	const std::vector< LightSegment >& LastSegments() const
	{
		return segments;
	}
	/// The cards that passed the head on the last frame.
	const std::vector< Disc::Print >& LastPrints() const
	{
		return prints;
	}
	/// Frames rendered since the plugin loaded (the first is 1).
	long long FrameCount() const
	{
		return frameCount;
	}
	/// The plugin's clock at the last frame, seconds.
	double Now() const
	{
		return now;
	}
	/// The exposure's length on the last frame, seconds.
	double LastShutter() const
	{
		return lastShutter;
	}

private:
	void UpdateClock();
	void UpdateAudio( double dt );
	bool ensureBuffers( GLsizei width, GLsizei height, int stations, double printWidth, double aspect );

	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader printShader;
	ffglex::FFGLShader carryShader;
	ffglex::FFGLShader sceneShader;
	ffglex::FFGLShader showShader;
	ffglex::FFGLScreenQuad quad;

	Cards cards;
	PassBuffer seen[ 2 ];///< the afterimage, ping-ponged
	int seenCurrent  = 0;
	bool seenStale   = true;

	Disc disc;
	Strobe strobe;
	audio::Analyser analyser;
	Geometry geometry;
	DrumGeometry drum;
	std::vector< LightSegment > segments;
	std::vector< Disc::Print > prints;
	std::vector< double > pushes;

	//-------------------------------------------------------------------
	// Time. See rosette: the host's clock unit is voted on against the wall
	// clock, because Resolume has sent both seconds and milliseconds.
	//-------------------------------------------------------------------
	double hostTime     = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	double clockScale   = 0.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockFrames     = 0;
	double now          = 0.0;
	double lastNow      = -1.0;
	bool settledJump    = false;
	long long frameCount = 0;
	double lastShutter  = 0.0;

	//-------------------------------------------------------------------
	// The beat, as SetBeatInfo last said.
	//-------------------------------------------------------------------
	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;

	//-------------------------------------------------------------------
	// Presses.
	//-------------------------------------------------------------------
	int firePresses = 0;
	bool fireHeld   = false;

	int hooks          = 0;
	bool resizeForgets = false;
	double heldAngle   = -1.0;
	int lastStations   = -1;
	GLsizei lastWidth  = 0;
	GLsizei lastHeight = 0;
};

} // namespace zoetrope

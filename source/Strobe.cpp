#include "Strobe.h"

#include <algorithm>
#include <cmath>

namespace zoetrope
{
namespace
{
/// The most boundaries one frame may fire. A frame that crossed more was a
/// seek or a stall, not music.
constexpr int kMaxPerFrame = 64;

/// How long a flash is kept after it ends: two of the longest frames the
/// plugin accepts, so one that straddles a frame boundary is still there for
/// the next window.
constexpr double kKeep = 0.5;

/// A bar is four beats (Resolume's bar phase runs 0..1 over a 4/4 bar).
constexpr double kBeatsPerBar = 4.0;
} // namespace

double Strobe::unannounced( double now, double dt, double length )
{
	//To end at this frame's time -- but never to start before the frame did:
	//a frame already shown cannot catch the light of a press it had not yet
	//heard, so a flash longer than the frame runs on into the next one, and
	//the two exposures still add up to the whole flash.
	return std::max( now - length, now - std::max( dt, 0.0 ) );
}

void Strobe::fire( double start, double length )
{
	flashes.push_back( { start, length } );
	++fired;
}

void Strobe::crossings( double p0, double p1, double step, double frameStart, double dt, std::vector< double >& at ) const
{
	if( !( step > 0.0 ) || !( p1 > p0 ) )
		return;
	double boundary = ( std::floor( p0 / step + 1e-9 ) + 1.0 ) * step;
	for( int n = 0; boundary <= p1 + 1e-12 && n < kMaxPerFrame; ++n, boundary += step )
	{
		const double inside = ( hooks & kHookBeatAtFrameStart ) != 0 ? 0.0 : ( boundary - p0 ) / ( p1 - p0 );
		at.push_back( frameStart + std::clamp( inside, 0.0, 1.0 ) * dt );
	}
}

void Strobe::Advance( double now, double dt, const StrobeInputs& in, std::vector< double >& pushes )
{
	pushes.clear();
	const double frameStart = now - dt;
	const double length     = std::max( in.flash, 1e-7 );

	//The bar phase: the host's, or a clock of the plugin's own at its BPM, so
	//a host that never moves it still has beats.
	const double tempo    = in.bpm > 1.0 ? in.bpm : 120.0;
	const double phaseNow = in.hostBeat ? std::clamp( in.barPhase, 0.0, 1.0 ) : std::fmod( now * tempo / 240.0, 1.0 );
	double p0 = lastPhase, p1 = phaseNow;
	const bool phaseValid = lastPhase >= 0.0 && dt > 0.0;
	if( phaseValid && p1 < p0 )
		p1 += 1.0;
	lastPhase = phaseNow;

	std::vector< double > beats;
	const double beatStep = BeatsPerFlash( in.trigger ) / kBeatsPerBar;
	switch( in.trigger )
	{
	case Trigger::Beat:
	case Trigger::HalfBeat:
	case Trigger::QuarterBeat:
	case Trigger::EighthBeat:
	case Trigger::Bar:
		if( phaseValid )
			crossings( p0, p1, beatStep, frameStart, dt, beats );
		for( double t : beats )
		{
			fire( t, length );
			pushes.push_back( t - frameStart );
		}
		break;

	case Trigger::Rate:
	{
		//The free-running strobe's own phase, in flashes. Changing Rate
		//changes how fast it runs from now on and never jumps it.
		const double r0 = ratePhase;
		if( rateStarted && dt > 0.0 )
			ratePhase += std::max( in.rate, 0.0 ) * dt;
		rateStarted = true;
		std::vector< double > ticks;
		crossings( r0, ratePhase, 1.0, frameStart, dt, ticks );
		for( double t : ticks )
			fire( t, length );
		//A hand-spun disc is pushed on the host's beat, not at Rate.
		if( phaseValid )
			crossings( p0, p1, 1.0 / kBeatsPerBar, frameStart, dt, beats );
		for( double t : beats )
			pushes.push_back( t - frameStart );
		//Keep the phase small: only its fraction matters.
		ratePhase -= std::floor( ratePhase );
		break;
	}

	case Trigger::Onset:
		if( in.onset )
		{
			const double t = unannounced( now, dt, length );
			fire( t, length );
			pushes.push_back( t - frameStart );
		}
		break;

	default: break;
	}
	if( in.trigger != Trigger::Rate )
		ratePhase = 0.0, rateStarted = false;

	//Fire, in every mode: a flash now, placed like an onset's.
	for( int i = 0; i < in.presses; ++i )
	{
		const double t = unannounced( now, dt, length );
		fire( t, length );
		pushes.push_back( t - frameStart );
	}
	std::sort( pushes.begin(), pushes.end() );

	while( !flashes.empty() && flashes.front().start + flashes.front().length < now - kKeep )
		flashes.pop_front();
}

std::vector< LightSegment > Strobe::Exposure( double now, double shutter, double period, double roomLight, double strobe ) const
{
	std::vector< LightSegment > out;
	if( ( hooks & kHookShutterIgnored ) != 0 )
		shutter = period;
	if( !( shutter > 0.0 ) || !( period > 0.0 ) )
		return out;
	const double open = now - shutter;

	if( roomLight > 0.0 )
		out.push_back( { -shutter, 0.0, roomLight * shutter / period, false } );

	int taken = 0;
	for( const FlashEvent& f : flashes )
	{
		const double a = std::max( f.start, open );
		const double b = std::min( f.start + f.length, now );
		if( !( b > a ) )
			continue;
		if( taken == kMaxFlashesPerWindow )
		{
			++dropped;
			continue;
		}
		++taken;
		//Constant energy: the light per unit time is Strobe / Flash.
		double weight = strobe * ( b - a ) / f.length;
		if( ( hooks & kHookLightPerFlashTau ) != 0 )
			weight = strobe * ( b - a ) / 1e-3;
		double to = b - now;
		if( ( hooks & kHookSmearHalved ) != 0 )
			to = ( a - now ) + 0.5 * ( b - a );
		out.push_back( { a - now, to, weight, true } );
	}
	return out;
}

} // namespace zoetrope

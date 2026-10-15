#pragma once

#include "Controls.h"

#include <vector>

/**
    The disc: its angle, what turns it, and which cards pass the print head.
    Plain C++, no GL, so `zttest --offline` checks it.

    **Angles.** Clockwise from twelve o'clock, in radians. Station k sits at
    room angle theta + k * pitch, pitch = 2 pi / N. The angle is kept reduced
    to [ 0, 2 pi ) in double at each frame's start, and within a frame as the
    advance A( s ) since that start, so nothing absolute ever reaches a float
    (BRIEF-ADDENDUM: Resolume's clock overflows one).

    **The drive.** Motor turns at a constant omega for the whole frame (Sync's
    or RPM's, set at the frame's start). Hand coasts on a bearing with viscous
    and Coulomb friction,

        d omega / dt = -k omega - c        (omega > 0; at 0 it stays put)

    whose solution is closed form and is used as such, never integrated:

        omega( t ) = ( omega0 + c / k ) e^( -k t ) - c / k
        A( t )     = ( omega0 + c / k ) ( 1 - e^( -k t ) ) / k - ( c / k ) t
        stops at     t* = ln( 1 + k omega0 / c ) / k

    A push adds Push to omega at its moment inside the frame, which starts a
    new piece of the same closed form.

    **The print head** is at room angle h. Card k passes it when
    theta( t ) + k pitch = h (mod 2 pi): in station units g( t ) =
    ( theta( t ) - h ) / pitch crosses an integer m, and the card is
    ( -m ) mod N. The crossing's moment is solved from the closed form (by
    bisection on a monotone function, which is root finding, not
    integration).
*/
namespace zoetrope
{

/// The bearing's closed form from omega0 after t seconds with no push: the
/// angle advanced and the speed. `stop` is t*, infinity when c is 0.
struct Coast
{
	double advance;
	double omega;
};
Coast CoastFor( double omega0, double t, const Bearing& bearing );
double StopTime( double omega0, const Bearing& bearing );

/// Hooks for the negative controls. A shipped instance sets 0.
enum DiscHook : int
{
	kHookNoCoulomb       = 1 << 0,///< the bearing without its dry friction: the disc never stops
	kHookStationMiscount = 1 << 1,///< Sync's speed worked out for N + 1 stations
	kHookHeadOneOn       = 1 << 2 ///< the print head a station further round than Print At says
};

class Disc
{
public:
	/// A card passing the head this frame: which, and when (seconds after the
	/// frame's start).
	struct Print
	{
		int card;
		double offset;
	};

	/// One host frame of `dt` seconds. `motorOmega` is the motor's speed
	/// (used in Motor; in Hand it is ignored), `pushes` the offsets of this
	/// frame's pushes in seconds after its start, ascending.
	void Advance( double dt, Drive drive, double motorOmega, const std::vector< double >& pushes, double push,
	              const Bearing& bearing );

	/// The angle advanced since this frame's start at `s` seconds into it
	/// (clamped to [ 0, dt ]).
	double AdvanceAt( double s ) const;
	double OmegaAt( double s ) const;

	/// The angle at this frame's start, reduced to [ 0, 2 pi ).
	double StartAngle() const
	{
		return startAngle;
	}
	/// The angle now (the frame's end), reduced.
	double Angle() const
	{
		return angle;
	}
	double Omega() const
	{
		return omega;
	}
	double FrameLength() const
	{
		return frameLength;
	}

	/// The cards that passed the head during this frame, in the order they
	/// passed it. More than N crossings in one frame keeps only the last N.
	std::vector< Print > Crossings( int stations, double head ) const;

	/// Stop the disc where it is.
	void Stop()
	{
		omega = 0.0;
	}

	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	int Hooks() const
	{
		return hooks;
	}
	/// Put the disc at an angle, stopped: the harness's instrument calibration.
	void SetAngleForTest( double radians );

private:
	struct Piece
	{
		double start;  ///< seconds after the frame's start
		double advance;///< A at its start
		double omega;  ///< omega at its start (after the push)
	};

	Bearing effective( const Bearing& bearing ) const;

	std::vector< Piece > pieces;///< Hand: one per push plus the first. Motor: one.
	Drive drive       = Drive::Motor;
	Bearing bearing   = { 0.4, 0.1 };
	double frameLength = 0.0;
	double startAngle = 0.0;
	double angle      = 0.0;
	double omega      = 0.0;
	int hooks         = 0;
};

/// Reduce an angle to [ 0, 2 pi ).
double Reduce( double radians );

} // namespace zoetrope

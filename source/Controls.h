#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Stations is a real integer (FF_TYPE_INTEGER is exempt from the clamp);
    Sync is a boolean; options hold the element index; Fire is a press; Audio
    is Resolume's 64-bin FFT buffer.

    Units: seconds, radians, radians per second (shown as RPM), hertz, and
    fractions of the frame for the disc's size.
*/
namespace zoetrope
{
constexpr double kPi = 3.14159265358979323846;

/// Every control, in the order Resolume shows them. The About block is
/// LAST, so the day a user guide adds a button no control moves.
enum ParamId : unsigned int
{
	// -- Strobe: when the light fires, and how ---------------------------------
	PT_TRIGGER = 0,
	PT_RATE,
	PT_FIRE,
	PT_AUDIO,
	PT_THRESHOLD,
	PT_FLASH,
	PT_STROBE,

	// -- Disc: the cards and what turns them ------------------------------------
	PT_STATIONS,
	PT_DRIVE,
	PT_SYNC,
	PT_SLIP,
	PT_RPM,
	PT_PUSH,
	PT_FRICTION,
	PT_PRINT_AT,
	PT_RADIUS,
	PT_CARD_SIZE,

	// -- Camera: what is integrated, and how it is shown -------------------------
	PT_SHUTTER,
	PT_ROOM_LIGHT,
	PT_PERSISTENCE,
	PT_BACKGROUND,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_FIRST,
	PT_COUNT = PT_ABOUT_FIRST + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,///< 0..1, converted in Controls.cpp
	Integer, ///< a real integer with a real range
	Boolean, ///< off or on
	Option,  ///< an element index
	Event,   ///< a press
	Buffer   ///< the FFT
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name; ///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< in the host's units: 0..1, the integer, or the index
	const char* const* options;
	int optionCount;
	float minimum;///< an Integer's range
	float maximum;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_FIRST.
const ControlInfo& InfoOf( unsigned int id );

//---------------------------------------------------------------------------
// Options.
//---------------------------------------------------------------------------
enum class Trigger
{
	Beat = 0,   ///< every beat of Resolume's clock
	HalfBeat,   ///< every quaver
	QuarterBeat,///< every semiquaver
	EighthBeat, ///< every demisemiquaver
	Bar,
	Onset,      ///< every onset the audio analyser finds
	Rate,       ///< free running at Rate hertz
	FireOnly,   ///< only the Fire button
	Count
};

enum class Drive
{
	Motor = 0,///< a constant angular velocity: Sync's, or RPM's
	Hand,     ///< pushed on the trigger, slowed by the bearing between pushes
	Count
};

enum class Background
{
	Black = 0,
	Clip,///< the clip, unlit, behind the disc
	Count
};

/// An option's index from the value the host stored (it reads back 0..1
/// whatever the count -- the fleet's trap -- so the table's own count is
/// what bounds it).
int OptionIndex( float value, int count );

/// The beats a flash period lasts for a beat-type trigger, and for Onset and
/// Fire Only, which Sync locks to one beat (AGENTS.md). 0 for Rate.
double BeatsPerFlash( Trigger trigger );

//---------------------------------------------------------------------------
// The mappings. Each says its range and its shape.
//---------------------------------------------------------------------------

/// The free-running strobe, 0.25 to 200 Hz, geometrically.
double RateFromParam( float value );
float ParamFromRate( double hertz );

/// The onset detector's bar: Threshold 0 fires on anything, 1 on very little.
/// Returned as millpond's analyser sensitivity (1 - Threshold).
double SensitivityFromThreshold( float value );

/// Flash: the length of one flash, 10 us to 20 ms, geometrically. A xenon
/// tube's flash is tens of microseconds; 20 ms is a lamp switched on and off.
double FlashFromParam( float value );
float ParamFromFlash( double seconds );

/// Strobe: the light one flash gives, 0 to 2 (1 exposes a card to its print).
double StrobeFromParam( float value );

/// Stations: the integer, clamped to the declared 4..48.
int StationsFromParam( float value );
constexpr int kMinStations = 4;
constexpr int kMaxStations = 48;

/// Slip: cards per flash with Sync on, 0 to 2, linearly. 1 is locked.
double SlipFromParam( float value );
float ParamFromSlip( double slip );

/// RPM: the motor's speed with Sync off, 0 to 600 RPM, linearly. In rad/s.
double MotorFromParam( float value );
float ParamFromMotor( double radiansPerSecond );

/// Push: what one push adds to the disc's speed, 1 to 120 RPM,
/// geometrically. In rad/s.
double PushFromParam( float value );
float ParamFromPush( double radiansPerSecond );

/// The bearing. Friction sets the viscous rate k (1/s, the time constant's
/// inverse), 0.05 to 4 per second geometrically, and the Coulomb part with
/// it: c = k * kCoulombSpeed, so below about 2.4 RPM the dry friction wins and
/// the disc stops in a finite time rather than for ever.
struct Bearing
{
	double viscous;///< k, 1/s
	double coulomb;///< c, rad/s^2
};
Bearing BearingFromParam( float value );
float ParamFromViscous( double perSecond );
constexpr double kCoulombSpeed = 0.25;///< rad/s: c / k

/// Print At: the print head's angle, 0 to 360 degrees clockwise from twelve
/// o'clock. In radians.
double PrintAtFromParam( float value );
float ParamFromPrintAt( double radians );

/// Radius: the ring of card centres, as a fraction of half the frame's
/// shorter side, 0.15 to 1.
double RadiusFromParam( float value );
float ParamFromRadius( double fraction );

/// Card Size: the card's width as a fraction of the widest card that cannot
/// touch its neighbours, 0.2 to 1.
double CardSizeFromParam( float value );
float ParamFromCardSize( double fraction );

/// Shutter: the camera's exposure as an angle of the frame, 0 to 360 degrees.
/// Returned as the fraction of a frame.
double ShutterFromParam( float value );
float ParamFromShutter( double fraction );

/// Room Light: a dim steady light, 0 to 1 (1 lights the disc as fully as a
/// flash, over a 360-degree exposure).
double RoomLightFromParam( float value );

/// Persistence: the eye's afterimage, an exponential of 0 to 200 ms,
/// linearly. 0 is off: the camera, honestly.
double PersistenceFromParam( float value );
float ParamFromPersistence( double seconds );

/// RPM to rad/s and back.
constexpr double kRpm = 2.0 * kPi / 60.0;

} // namespace zoetrope

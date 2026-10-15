#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace zoetrope
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kRateLow = 0.25, kRateHigh = 200.0;
constexpr double kFlashLow = 10e-6, kFlashHigh = 20e-3;
constexpr double kStrobeMost = 2.0;
constexpr double kSlipMost = 2.0;
constexpr double kMotorMost = 600.0 * kRpm;
constexpr double kPushLow = 1.0 * kRpm, kPushHigh = 120.0 * kRpm;
constexpr double kViscousLow = 0.05, kViscousHigh = 4.0;
constexpr double kRadiusLow = 0.15, kRadiusHigh = 1.0;
constexpr double kCardLow = 0.2, kCardHigh = 1.0;
constexpr double kPersistenceMost = 0.2;

const char* const kTriggerNames[]    = { "Beat", "Half Beat", "Quarter Beat", "Eighth Beat", "Bar", "Onset", "Rate", "Fire Only" };
const char* const kDriveNames[]      = { "Motor", "Hand" };
const char* const kBackgroundNames[] = { "Black", "Clip" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count, 0.0f, static_cast< float >( count - 1 ) };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo integer( unsigned int id, const char* name, const char* group, int value, int low, int high )
{
	return { id, name, group, Kind::Integer, static_cast< float >( value ), nullptr, 0, static_cast< float >( low ), static_cast< float >( high ) };
}
ControlInfo boolean( unsigned int id, const char* name, const char* group, bool value )
{
	return { id, name, group, Kind::Boolean, value ? 1.0f : 0.0f, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo buffer( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Buffer, 0.0f, nullptr, 0, 0.0f, 1.0f };
}

//---------------------------------------------------------------------------
// The defaults. Twelve cards on a disc locked to the beat: one card on per
// flash, so each figure stands still and plays the clip, a beat's delay more
// at each station round from the print head at twelve o'clock. A
// millisecond's flash on every beat, the camera's shutter open the whole
// frame (360 degrees, so no flash falls between two exposures), and a little
// room light to see the disc turning between flashes.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_TRIGGER, "Trigger", "Strobe", 0.0f, kTriggerNames, static_cast< int >( Trigger::Count ) ),
	standard( PT_RATE, "Rate", "Strobe", ParamFromRate( 12.0 ) ),
	event( PT_FIRE, "Fire", "Strobe" ),
	buffer( PT_AUDIO, "Audio", "Strobe" ),
	standard( PT_THRESHOLD, "Threshold", "Strobe", 0.4f ),
	standard( PT_FLASH, "Flash", "Strobe", ParamFromFlash( 1e-3 ) ),
	standard( PT_STROBE, "Strobe", "Strobe", 0.5f ),

	integer( PT_STATIONS, "Stations", "Disc", 12, kMinStations, kMaxStations ),
	option( PT_DRIVE, "Drive", "Disc", 0.0f, kDriveNames, static_cast< int >( Drive::Count ) ),
	boolean( PT_SYNC, "Sync", "Disc", true ),
	standard( PT_SLIP, "Slip", "Disc", ParamFromSlip( 1.0 ) ),
	standard( PT_RPM, "RPM", "Disc", ParamFromMotor( 30.0 * kRpm ) ),
	standard( PT_PUSH, "Push", "Disc", ParamFromPush( 30.0 * kRpm ) ),
	standard( PT_FRICTION, "Friction", "Disc", ParamFromViscous( 0.4 ) ),
	standard( PT_PRINT_AT, "Print At", "Disc", 0.0f ),
	standard( PT_RADIUS, "Radius", "Disc", ParamFromRadius( 0.62 ) ),
	standard( PT_CARD_SIZE, "Card Size", "Disc", ParamFromCardSize( 0.9 ) ),

	standard( PT_SHUTTER, "Shutter", "Camera", 1.0f ),
	standard( PT_ROOM_LIGHT, "Room Light", "Camera", 0.08f ),
	standard( PT_PERSISTENCE, "Persistence", "Camera", 0.0f ),
	option( PT_BACKGROUND, "Background", "Camera", 0.0f, kBackgroundNames, static_cast< int >( Background::Count ) ),
	standard( PT_MIX, "Mix", "Camera", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_FIRST, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_FIRST - 1 ) ];
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double BeatsPerFlash( Trigger trigger )
{
	switch( trigger )
	{
	case Trigger::Beat: return 1.0;
	case Trigger::HalfBeat: return 0.5;
	case Trigger::QuarterBeat: return 0.25;
	case Trigger::EighthBeat: return 0.125;
	case Trigger::Bar: return 4.0;
	case Trigger::Onset:
	case Trigger::FireOnly: return 1.0;
	default: return 0.0;
	}
}

double RateFromParam( float value )
{
	return geometric( value, kRateLow, kRateHigh );
}
float ParamFromRate( double hertz )
{
	return inverseGeometric( hertz, kRateLow, kRateHigh );
}

double SensitivityFromThreshold( float value )
{
	return 1.0 - clamp01( value );
}

double FlashFromParam( float value )
{
	return geometric( value, kFlashLow, kFlashHigh );
}
float ParamFromFlash( double seconds )
{
	return inverseGeometric( seconds, kFlashLow, kFlashHigh );
}

double StrobeFromParam( float value )
{
	return linear( value, 0.0, kStrobeMost );
}

int StationsFromParam( float value )
{
	if( !std::isfinite( value ) )
		return 12;
	return std::clamp( static_cast< int >( std::lround( value ) ), kMinStations, kMaxStations );
}

double SlipFromParam( float value )
{
	return linear( value, 0.0, kSlipMost );
}
float ParamFromSlip( double slip )
{
	return inverseLinear( slip, 0.0, kSlipMost );
}

double MotorFromParam( float value )
{
	return linear( value, 0.0, kMotorMost );
}
float ParamFromMotor( double radiansPerSecond )
{
	return inverseLinear( radiansPerSecond, 0.0, kMotorMost );
}

double PushFromParam( float value )
{
	return geometric( value, kPushLow, kPushHigh );
}
float ParamFromPush( double radiansPerSecond )
{
	return inverseGeometric( radiansPerSecond, kPushLow, kPushHigh );
}

Bearing BearingFromParam( float value )
{
	const double k = geometric( value, kViscousLow, kViscousHigh );
	return { k, k * kCoulombSpeed };
}
float ParamFromViscous( double perSecond )
{
	return inverseGeometric( perSecond, kViscousLow, kViscousHigh );
}

double PrintAtFromParam( float value )
{
	return linear( value, 0.0, 2.0 * kPi );
}
float ParamFromPrintAt( double radians )
{
	return inverseLinear( radians, 0.0, 2.0 * kPi );
}

double RadiusFromParam( float value )
{
	return linear( value, kRadiusLow, kRadiusHigh );
}
float ParamFromRadius( double fraction )
{
	return inverseLinear( fraction, kRadiusLow, kRadiusHigh );
}

double CardSizeFromParam( float value )
{
	return linear( value, kCardLow, kCardHigh );
}
float ParamFromCardSize( double fraction )
{
	return inverseLinear( fraction, kCardLow, kCardHigh );
}

double ShutterFromParam( float value )
{
	return clamp01( value );
}
float ParamFromShutter( double fraction )
{
	return static_cast< float >( std::clamp( fraction, 0.0, 1.0 ) );
}

double RoomLightFromParam( float value )
{
	return clamp01( value );
}

double PersistenceFromParam( float value )
{
	return linear( value, 0.0, kPersistenceMost );
}
float ParamFromPersistence( double seconds )
{
	return inverseLinear( seconds, 0.0, kPersistenceMost );
}

} // namespace zoetrope

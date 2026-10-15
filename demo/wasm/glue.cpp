/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Zoetrope.cpp` and everything it calls (Disc, Strobe,
    Cards, Audio, Controls, Shaders, PassBuffer, Diag), compiled UNMODIFIED
    into the same WebAssembly module as this file, with the FFGL SDK pieces it
    needs (see demo/tools/build-wasm.sh for the list). This file only plays
    the host:

      - it constructs a `ZoetropePlugin`, as PluginEntry.cpp's registration
        would through CFFGLPluginInfo, and calls InitGL / DeInitGL;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (GetParamName, GetParamType, GetParamGroup,
        GetParamDefault, the element, range and usage getters -- the SDK
        compiled unmodified too), so the page's panel is built from the
        plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, and each frame
        SetTime, SetBeatInfo and ProcessOpenGL with one input texture, in that
        order, as tools/zttest's Rig does. SetTextParameter is here too, but
        the plugin's one text parameter, the About line, is display only and
        the page never sends it.

    Two things are the page's and not the plugin's, and the page says so:

      - The transport. SetBeatInfo is handed a BPM and a bar phase from a
        clock the page keeps (a BPM field under the picture, 120 at first,
        and a bar phase advanced from the page's own seconds), because a
        browser has no Resolume transport. The plugin cannot tell it from
        one: it reads the bar phase moving and fires on its boundaries.
      - The audio. The plugin declares an FFT buffer (Audio) that Resolume
        fills every frame. This file never writes it, so the plugin's onset
        analyser hears silence and Trigger Onset never fires.

    The clock is the page's too: SetTime is handed the kit's seconds, and the
    plugin's own vote decides the unit (seconds, within four frames, as in
    Arena). Before the vote settles the plugin runs on its wall clock, which
    here is the browser's performance.now().

    The read-outs use the plugin's harness accessors (DiscState, StrobeState,
    CardState, LastGeometry, Now, FrameCount) or its own tables (Controls.cpp,
    the About block's buttons); they report and never change anything.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Zoetrope.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace zoetrope;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	ZoetropePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

constexpr double kNaN = std::numeric_limits< double >::quiet_NaN();
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// The plugin's log. Diag.cpp opens it in InitGL (diag::init), so this must run
// before the first zt_init_gl: a directory in the page's in-memory file
// system, named through the override Diag.cpp reads. Its own `mkdir -p` goes
// through system(), which a browser does not have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE void zt_prepare_log()
{
	setenv( "ZOETROPE_LOG_DIR", "/zoetrope/logs", 1 );
	mkdir( "/zoetrope", 0755 );
	mkdir( "/zoetrope/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE const char* zt_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* zt_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void zt_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int zt_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int zt_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* zt_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int zt_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int zt_param_usage( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamUsage( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* zt_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float zt_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a text parameter.
EMSCRIPTEN_KEEPALIVE const char* zt_param_default_text( Instance* instance, int index )
{
	const FFMixed mixed = instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) );
	return mixed.PointerValue ? static_cast< const char* >( mixed.PointerValue ) : "";
}

EMSCRIPTEN_KEEPALIVE int zt_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* zt_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float zt_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE float zt_param_range_min( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).min;
}

EMSCRIPTEN_KEEPALIVE float zt_param_range_max( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).max;
}

EMSCRIPTEN_KEEPALIVE int zt_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int zt_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float zt_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

EMSCRIPTEN_KEEPALIVE int zt_set_text( Instance* instance, int index, const char* value )
{
	return instance->plugin.SetTextParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* zt_get_text( Instance* instance, int index )
{
	const char* text = instance->plugin.GetTextParameter( static_cast< unsigned int >( index ) );
	return text ? text : "";
}

/// One frame, in the order tools/zttest's Rig makes the calls: SetTime,
/// SetBeatInfo with the page's transport, then ProcessOpenGL with one input of
/// clipWidth x clipHeight whose GL name is `clip` (a texture the page
/// registered with emscripten's GL tables), drawing into whatever framebuffer
/// and viewport the page has bound, as a host's. 1 on success.
EMSCRIPTEN_KEEPALIVE int zt_process( Instance* instance, double seconds, float bpm, float barPhase, int clip, int clipWidth,
                                     int clipHeight )
{
	instance->plugin.SetTime( seconds );
	instance->plugin.SetBeatInfo( bpm, barPhase );
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Zoetrope.cpp's ProcessOpenGL applies to it, in the unit the page prints
// (radians per second as RPM by the plugin's own kRpm, radians and the
// shutter's fraction of a frame as degrees). NaN where the plugin uses the
// host's 0..1 as it is (Mix), and for the controls that are not ranged.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double zt_convert( int index, float v )
{
	switch( index )
	{
	case PT_RATE: return RateFromParam( v );
	case PT_THRESHOLD: return SensitivityFromThreshold( v );
	case PT_FLASH: return FlashFromParam( v );
	case PT_STROBE: return StrobeFromParam( v );
	case PT_SLIP: return SlipFromParam( v );
	case PT_RPM: return MotorFromParam( v ) / kRpm;
	case PT_PUSH: return PushFromParam( v ) / kRpm;
	case PT_FRICTION: return BearingFromParam( v ).viscous;
	case PT_PRINT_AT: return PrintAtFromParam( v ) * 180.0 / kPi;
	case PT_RADIUS: return RadiusFromParam( v );
	case PT_CARD_SIZE: return CardSizeFromParam( v );
	case PT_SHUTTER: return ShutterFromParam( v ) * 360.0;
	case PT_ROOM_LIGHT: return RoomLightFromParam( v );
	case PT_PERSISTENCE: return PersistenceFromParam( v );
	default: return kNaN;
	}
}

/// The other way, for the page's presets: a setting in zt_convert's units to
/// the host's 0..1, by Controls.cpp's own inverse (ParamFromRate and the
/// rest). NaN for a control that has none.
EMSCRIPTEN_KEEPALIVE double zt_param_from( int index, double x )
{
	switch( index )
	{
	case PT_RATE: return ParamFromRate( x );
	case PT_FLASH: return ParamFromFlash( x );
	case PT_SLIP: return ParamFromSlip( x );
	case PT_RPM: return ParamFromMotor( x * kRpm );
	case PT_PUSH: return ParamFromPush( x * kRpm );
	case PT_FRICTION: return ParamFromViscous( x );
	case PT_PRINT_AT: return ParamFromPrintAt( x * kPi / 180.0 );
	case PT_RADIUS: return ParamFromRadius( x );
	case PT_CARD_SIZE: return ParamFromCardSize( x );
	case PT_SHUTTER: return ParamFromShutter( x / 360.0 );
	case PT_PERSISTENCE: return ParamFromPersistence( x );
	default: return kNaN;
	}
}

//---------------------------------------------------------------------------
// The About block's buttons: the URL each one opens, from the
// StoatworksAboutLinks.h the plugin is built with. The host getters give only
// a button's label; the page needs the address the plugin's own press would
// have opened, because that press calls system(), which a browser does not
// have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int zt_about_first()
{
	return static_cast< int >( PT_ABOUT_FIRST );
}

EMSCRIPTEN_KEEPALIVE const char* zt_about_button_label( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].label : "";
}

EMSCRIPTEN_KEEPALIVE const char* zt_about_button_url( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].url : "";
}

//---------------------------------------------------------------------------
// What the apparatus is doing, for the status line, through the harness's
// accessors. 0 = the disc's angle now, degrees clockwise from twelve; 1 = its
// speed, RPM; 2 = flashes fired since the instance began; 3 = flashes dropped
// from an exposure over its eight; 4 = frames rendered; 5 = the plugin's
// clock, seconds; 6 = cards printed of the disc's stations; 7 = stations;
// 8 = flashes the last exposure caught (whole or part). NaN before the first
// frame.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double zt_state( Instance* instance, int what )
{
	if( instance->plugin.FrameCount() == 0 )
		return kNaN;
	const Disc& disc        = instance->plugin.DiscState();
	const Strobe& strobe    = instance->plugin.StrobeState();
	const Geometry& geometry = instance->plugin.LastGeometry();
	switch( what )
	{
	case 0: return disc.Angle() * 180.0 / kPi;
	case 1: return disc.Omega() / kRpm;
	case 2: return static_cast< double >( strobe.Fired() );
	case 3: return static_cast< double >( strobe.Dropped() );
	case 4: return static_cast< double >( instance->plugin.FrameCount() );
	case 5: return instance->plugin.Now();
	case 6:
	{
		int printed = 0;
		for( int card = 0; card < geometry.stations; ++card )
			printed += instance->plugin.CardState().Printed( card ) ? 1 : 0;
		return printed;
	}
	case 7: return geometry.stations;
	case 8:
	{
		int flashes = 0;
		for( const LightSegment& segment : instance->plugin.LastSegments() )
			flashes += segment.flash ? 1 : 0;
		return flashes;
	}
	default: return kNaN;
	}
}

} // extern "C"

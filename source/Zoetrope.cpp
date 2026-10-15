#include "Zoetrope.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace zoetrope
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;

/// Seconds of host time a single frame may advance by. A stalled host must
/// not spin the disc for a minute in one frame.
constexpr double kMaxFrameDelta = 0.25;

/// The frame length assumed before there are two frames to measure one.
constexpr double kFirstFrame = 1.0 / 60.0;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

/// glGetString returns nullptr when there is no current context.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

void setInt2( FFGLShader& shader, const char* name, int x, int y )
{
	glUniform2i( shader.FindUniform( name ), x, y );
}
} // namespace

//---------------------------------------------------------------------------
// The buttons are declared one per link, so the run in the enum and the run the
// block actually has must agree.
static_assert( PT_COUNT - PT_ABOUT_FIRST == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
ZoetropePlugin::ZoetropePlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The disc turns at so many radians a second and the strobe fires on the
	//host's beat, so the plugin has to know what a second is.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Declaration, from the table in Controls.cpp. Every ranged STANDARD
	// parameter is a plain 0..1 float: SetParamInfo clamps its default into
	// 0..1 before a range could be attached (SDK b1afaf9). FF_TYPE_INTEGER
	// is exempt, so Stations is declared with its real range.
	//---------------------------------------------------------------------
	for( unsigned int id = 0; id < PT_ABOUT_FIRST; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		params[ id ]            = info.defaultValue;
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, info.defaultValue ); break;
		case Kind::Integer:
			SetParamInfo( id, info.name, FF_TYPE_INTEGER, info.defaultValue );
			SetParamRange( id, info.minimum, info.maximum );
			break;
		case Kind::Boolean: SetParamInfo( id, info.name, FF_TYPE_BOOLEAN, info.defaultValue >= 0.5f ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), info.defaultValue );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//An FFT buffer: Resolume shows it as an audio-source picker and
			//writes the spectrum into it every frame. With nothing routed,
			//Trigger Onset never fires.
			SetBufferParamInfo( id, info.name, audio::kBins, FF_USAGE_FFT );
			for( int i = 0; i < audio::kBins; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block. Inline rather than through a helper: SetParamInfo is
	// protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
FFResult ZoetropePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &printShader, shaders::Pass::Print, "print" },
		{ &carryShader, shaders::Pass::Carry, "carry" },
		{ &sceneShader, shaders::Pass::Scene, "scene" },
		{ &showShader, shaders::Pass::Show, "show" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Zoetrope: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool ZoetropePlugin::ensureBuffers( GLsizei width, GLsizei height, int stations, double printWidth, double aspect )
{
	//All of it up front, before anything is bound: every ffglex::Scoped*
	//binding clears to 0 on exit rather than restoring, and
	//FFGLFBO::Initialise binds under one.
	const int layerWidth  = LadderAtLeast( printWidth );
	const int layerHeight = std::max( 1, static_cast< int >( std::lround( layerWidth / std::max( aspect, 1e-3 ) ) ) );
	if( !cards.Ensure( layerWidth, layerHeight, stations, carryShader.GetGLID(), quad ) )
		return false;

	if( PersistenceFromParam( params[ PT_PERSISTENCE ] ) > 0.0 )
	{
		for( PassBuffer& buffer : seen )
		{
			const bool fresh = buffer.GetGLID() == 0 || static_cast< GLsizei >( buffer.GetWidth() ) != width
			                   || static_cast< GLsizei >( buffer.GetHeight() ) != height;
			if( !buffer.Ensure( width, height, GL_RGBA16F, PassBuffer::Sampling::Nearest ) )
				return false;
			//An eye that has just been handed a new raster starts from what
			//it sees now, rather than from black.
			if( fresh )
				seenStale = true;
		}
	}
	return true;
}

//---------------------------------------------------------------------------
void ZoetropePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;

	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking. (Rosette's code, by way of
	//millpond and schlieren.)
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;

			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale = millisVotes > secondsVotes ? 0.001 : 1.0;
				//The clock is about to jump from wall time to the host's own,
				//which can be hours in: no dt across the jump.
				settledJump = true;
			}
		}
	}

	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;

	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
void ZoetropePlugin::UpdateAudio( double dt )
{
	float bins[ audio::kBins ] = {};
	int binCount               = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}

	audio::Settings settings;
	settings.sensitivity = static_cast< float >( SensitivityFromThreshold( params[ PT_THRESHOLD ] ) );
	analyser.Update( bins, binCount, static_cast< float >( dt ), settings );
}

//---------------------------------------------------------------------------
FFResult ZoetropePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& source = *( pgl->inputTextures[ 0 ] );
	const GLsizei width             = static_cast< GLsizei >( source.Width );
	const GLsizei height            = static_cast< GLsizei >( source.Height );
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//The host's viewport and blend state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	//---------------------------------------------------------------------
	// Time, the audio and the beat.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	++frameCount;
	UpdateAudio( dt );

	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;

	//---------------------------------------------------------------------
	// The controls, in physical units.
	//---------------------------------------------------------------------
	const auto trigger    = static_cast< Trigger >( OptionIndex( params[ PT_TRIGGER ], static_cast< int >( Trigger::Count ) ) );
	const auto drive      = static_cast< Drive >( OptionIndex( params[ PT_DRIVE ], static_cast< int >( Drive::Count ) ) );
	const auto background = static_cast< Background >( OptionIndex( params[ PT_BACKGROUND ], static_cast< int >( Background::Count ) ) );
	const int stations    = StationsFromParam( params[ PT_STATIONS ] );
	const double pitch    = 2.0 * kPi / stations;
	const double rate     = RateFromParam( params[ PT_RATE ] );
	const double tempo    = bpm > 1.0f ? static_cast< double >( bpm ) : 120.0;

	//---------------------------------------------------------------------
	// The light: what fires this frame, and which moments a hand-spun disc
	// is pushed at.
	//---------------------------------------------------------------------
	StrobeInputs in;
	in.trigger  = trigger;
	in.rate     = rate;
	in.presses  = firePresses;
	in.onset    = analyser.Fired();
	in.barPhase = barPhase;
	in.bpm      = tempo;
	in.hostBeat = hostBeatSeen;
	in.flash    = FlashFromParam( params[ PT_FLASH ] );
	firePresses = 0;
	strobe.Advance( now, dt, in, pushes );

	//---------------------------------------------------------------------
	// The disc. With Sync, the motor turns Slip stations per flash period;
	// without, at RPM.
	//---------------------------------------------------------------------
	double motor = MotorFromParam( params[ PT_RPM ] );
	if( params[ PT_SYNC ] >= 0.5f )
	{
		const double period = trigger == Trigger::Rate ? 1.0 / rate : BeatsPerFlash( trigger ) * 60.0 / tempo;
		const int counted   = ( disc.Hooks() & kHookStationMiscount ) != 0 ? stations + 1 : stations;
		motor               = SlipFromParam( params[ PT_SLIP ] ) * ( 2.0 * kPi / counted ) / period;
	}
	if( heldAngle >= 0.0 )
	{
		disc.SetAngleForTest( heldAngle );
		motor = 0.0;
	}
	const bool newDisc = lastStations >= 0 && stations != lastStations;
	lastStations       = stations;
	if( newDisc )
		disc.Stop();
	disc.Advance( dt, heldAngle >= 0.0 ? Drive::Motor : drive, motor, pushes, PushFromParam( params[ PT_PUSH ] ),
	              BearingFromParam( params[ PT_FRICTION ] ) );
	prints = disc.Crossings( stations, PrintAtFromParam( params[ PT_PRINT_AT ] ) );

	//---------------------------------------------------------------------
	// Where the disc is on the raster. A print is the clip's shape, on a card
	// with a margin of kMargin of the print's height all round. The card is
	// Card Size of the widest that cannot touch a neighbour: its inner corner
	// at radius R must stay inside its own half of the station,
	//
	//     ( w_c / 2 ) / ( R - h_c / 2 ) <= tan( pi / N ),
	//     w_c = h_p ( a + 2 m ),  h_c = h_p ( 1 + 2 m ),
	//
	// which puts the tallest print at h_p = 2 R t / ( a + 2 m + ( 1 + 2 m ) t ).
	//---------------------------------------------------------------------
	const double aspect  = static_cast< double >( width ) / static_cast< double >( height );
	const double ring    = RadiusFromParam( params[ PT_RADIUS ] ) * 0.5 * std::min( width, height );
	const double tanHalf = std::tan( kPi / stations );
	double tallest       = 2.0 * ring * tanHalf / ( aspect + 2.0 * kMargin + ( 1.0 + 2.0 * kMargin ) * tanHalf );
	//...and no card reaches past the hub.
	tallest                  = std::min( tallest, 0.98 * ring / ( 0.5 + kMargin ) );
	const double printHeight = CardSizeFromParam( params[ PT_CARD_SIZE ] ) * tallest;
	const double printWidth  = printHeight * aspect;
	const double margin      = kMargin * printHeight;
	const double cardWidth   = printWidth + 2.0 * margin;
	const double cardHeight  = printHeight + 2.0 * margin;
	const double corner      = std::hypot( ring + 0.5 * cardHeight, 0.5 * cardWidth );
	geometry.centreX         = 0.5 * width;
	geometry.centreY         = 0.5 * height;
	geometry.ring            = ring;
	geometry.halfWidth       = 0.5 * cardWidth;
	geometry.halfHeight      = 0.5 * cardHeight;
	geometry.printHalfWidth  = 0.5 * printWidth;
	geometry.printHalfHeight = 0.5 * printHeight;
	geometry.discRadius      = corner + std::max( 2.0, 0.08 * cardHeight );
	geometry.headSize        = 0.3 * cardHeight;
	geometry.stations        = stations;
	geometry.pitch           = pitch;

	const bool resized = lastWidth > 0 && ( width != lastWidth || height != lastHeight );
	lastWidth          = width;
	lastHeight         = height;

	if( !ensureBuffers( width, height, stations, printWidth, aspect ) )
	{
		diag::error( "could not allocate the cards or the afterimage" );
		return FF_FAIL;
	}
	if( newDisc || ( resized && resizeForgets ) )
		cards.Blank();

	//---------------------------------------------------------------------
	// 1. Print the cards that passed the head, with this frame of the clip.
	//---------------------------------------------------------------------
	const FFGLTexCoords maxCoords = GetMaxGLTexCoords( source );
	if( !prints.empty() )
	{
		ScopedShaderBinding shader( printShader.GetGLID() );
		bindUnit( 0, source.Handle );
		printShader.Set( "InputTexture", 0 );
		printShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		setInt2( printShader, "Source", width, height );
		setInt2( printShader, "Target", cards.Width(), cards.Height() );
		const double scale = std::max( static_cast< double >( width ) / cards.Width(), static_cast< double >( height ) / cards.Height() );
		printShader.Set( "Taps", std::clamp( static_cast< int >( std::ceil( scale - 1e-6 ) ), 1, 8 ) );
		for( const Disc::Print& print : prints )
		{
			cards.Begin( print.card );
			quad.Draw();
			cards.Tag( print.card, frameCount, now - dt + print.offset );
		}
		cards.Unbind();
		unbindTextureUnits( 1 );
	}

	//---------------------------------------------------------------------
	// 2. The exposure: the window [ now - S, now ] as light segments, and
	// the disc's angle across it, from a reference at the window's start.
	//---------------------------------------------------------------------
	const double period  = dt > 0.0 ? dt : kFirstFrame;
	const double shutter = ShutterFromParam( params[ PT_SHUTTER ] ) * period;
	lastShutter          = shutter;
	segments             = strobe.Exposure( now, shutter, period, RoomLightFromParam( params[ PT_ROOM_LIGHT ] ),
                                StrobeFromParam( params[ PT_STROBE ] ) );

	const double open     = dt - shutter;//the window's start, seconds into the frame
	const double atOpen   = disc.AdvanceAt( open );
	const double reference = Reduce( disc.StartAngle() + atOpen );
	const double stationsIn = reference / pitch;
	int whole               = static_cast< int >( std::floor( stationsIn ) );
	double fraction         = stationsIn - whole;
	if( whole >= stations )
		whole -= stations;
	float angles[ shaders::kAngleTable ] = {};
	for( int i = 0; i < shaders::kAngleTable; ++i )
		angles[ i ] = static_cast< float >( disc.AdvanceAt( open + shutter * i / ( shaders::kAngleTable - 1.0 ) ) - atOpen );
	float rows[ shaders::kMaxSegments * 4 ] = {};
	int rowCount                            = 0;
	for( const LightSegment& segment : segments )
	{
		if( rowCount == shaders::kMaxSegments || !( shutter > 0.0 ) )
			break;
		rows[ rowCount * 4 + 0 ] = static_cast< float >( ( segment.from + shutter ) / shutter );
		rows[ rowCount * 4 + 1 ] = static_cast< float >( ( segment.to + shutter ) / shutter );
		rows[ rowCount * 4 + 2 ] = static_cast< float >( segment.weight );
		++rowCount;
	}

	//---------------------------------------------------------------------
	// 3. The scene, to the host -- or with Persistence on, into the
	// afterimage, then 4. the afterimage to the host.
	//---------------------------------------------------------------------
	const double persistence = PersistenceFromParam( params[ PT_PERSISTENCE ] );
	const bool afterimage    = persistence > 0.0 && seen[ 0 ].GetGLID() != 0 && seen[ 1 ].GetGLID() != 0;
	const int next           = 1 - seenCurrent;
	auto drawScene = [ & ]( int outputMode, float keep, int originX, int originY ) {
		ScopedShaderBinding shader( sceneShader.GetGLID() );
		bindUnit( 0, source.Handle );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, cards.Texture() );
		//Without the afterimage nothing reads Previous, but Apple's driver
		//calls a sampler on texture 0 "unloadable" and says so: the clip
		//stands in.
		bindUnit( 2, afterimage ? seen[ seenCurrent ].TextureID() : source.Handle );
		sceneShader.Set( "InputTexture", 0 );
		sceneShader.Set( "Cards", 1 );
		sceneShader.Set( "Previous", 2 );
		sceneShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		setInt2( sceneShader, "Size", width, height );
		setInt2( sceneShader, "ViewOrigin", originX, originY );
		sceneShader.Set( "Centre", static_cast< float >( geometry.centreX ), static_cast< float >( geometry.centreY ) );
		sceneShader.Set( "Ring", static_cast< float >( ring ) );
		sceneShader.Set( "CardHalf", static_cast< float >( geometry.halfWidth ), static_cast< float >( geometry.halfHeight ) );
		sceneShader.Set( "PrintHalf", static_cast< float >( geometry.printHalfWidth ), static_cast< float >( geometry.printHalfHeight ) );
		sceneShader.Set( "BandInner", static_cast< float >( ring - geometry.halfHeight - 1.0 ) );
		sceneShader.Set( "BandOuter", static_cast< float >( corner + 1.0 ) );
		sceneShader.Set( "DiscRadius", static_cast< float >( geometry.discRadius ) );
		sceneShader.Set( "Pitch", static_cast< float >( pitch ) );
		sceneShader.Set( "Stations", stations );
		sceneShader.Set( "StationOffset", whole );
		sceneShader.Set( "PhaseFrac", static_cast< float >( fraction ) );
		glUniform1fv( sceneShader.FindUniform( "Angles" ), shaders::kAngleTable, angles );
		glUniform4fv( sceneShader.FindUniform( "Segments" ), shaders::kMaxSegments, rows );
		sceneShader.Set( "SegmentCount", rowCount );
		sceneShader.Set( "Head", static_cast< float >( PrintAtFromParam( params[ PT_PRINT_AT ] ) ) );
		sceneShader.Set( "HeadSize", static_cast< float >( geometry.headSize ) );
		sceneShader.Set( "BackgroundMode", static_cast< int >( background ) );
		sceneShader.Set( "MixAmount", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
		sceneShader.Set( "OutputMode", outputMode );
		sceneShader.Set( "Afterimage", keep );
		sceneShader.Set( "Hooks", hooks );
		quad.Draw();
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
		unbindTextureUnits( 3 );
		glActiveTexture( GL_TEXTURE0 );
	};

	if( afterimage )
	{
		//The eye's afterimage: an exponential of the camera's pictures, the
		//last one kept by exp( -T / Persistence ).
		const float keep = seenStale ? 0.0f : static_cast< float >( std::exp( -period / persistence ) );
		{
			ScopedFBOBinding fbo( seen[ next ].GetGLID(), ScopedFBOBinding::RB_REVERT );
			glViewport( 0, 0, width, height );
			drawScene( 1, keep, 0, 0 );
		}
		seenCurrent = next;
		seenStale   = false;

		glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
		ScopedShaderBinding shader( showShader.GetGLID() );
		bindUnit( 0, source.Handle );
		bindUnit( 1, seen[ seenCurrent ].TextureID() );
		showShader.Set( "InputTexture", 0 );
		showShader.Set( "Seen", 1 );
		showShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		setInt2( showShader, "Size", width, height );
		setInt2( showShader, "ViewOrigin", hostViewport[ 0 ], hostViewport[ 1 ] );
		showShader.Set( "MixAmount", std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
		quad.Draw();
		unbindTextureUnits( 2 );
		glActiveTexture( GL_TEXTURE0 );
	}
	else
	{
		seenStale = true;
		glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
		drawScene( 0, 0.0f, hostViewport[ 0 ], hostViewport[ 1 ] );
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult ZoetropePlugin::DeInitGL()
{
	printShader.FreeGLResources();
	carryShader.FreeGLResources();
	sceneShader.FreeGLResources();
	showShader.FreeGLResources();
	quad.Release();
	cards.Release();
	seen[ 0 ].Destroy();
	seen[ 1 ].Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult ZoetropePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void ZoetropePlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* ZoetropePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult ZoetropePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult ZoetropePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	//Fire acts on the press, once: an event arrives as 1 on press and 0 on
	//release, and a host is free to restate either.
	if( index == PT_FIRE )
	{
		const bool down = value >= 0.5f;
		if( down && !fireHeld )
			++firePresses;
		fireHeld = down;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

float ZoetropePlugin::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;
	return params[ index ];
}

} // namespace zoetrope

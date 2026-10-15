//===========================================================================
// The checks. Included into main.cpp's anonymous namespace.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the stated law; `--negative` sets one field at a time to a
// deliberately wrong MODEL -- a hook in the plugin, so the shipped code
// computes the wrong thing -- and requires the check to FAIL.
//===========================================================================
struct Perturb
{
	int hooks        = 0;    ///< shaders::Hook bits
	int discHooks    = 0;    ///< DiscHook bits
	int strobeHooks  = 0;    ///< StrobeHook bits
	bool unprimed    = false;///< the onset detector's first frame against zeroes
	bool resizeForgets = false;///< a new raster blanks the cards
	bool cuesRamp    = false;///< --cues: every control ramps
	bool unwrapped   = false;///< --drift: the expectation without the wrap (the spec's control)
};

using CheckFn = int ( * )( const Perturb& );

struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

void hook( Rig& rig, const Perturb& perturb )
{
	rig.plugin.SetHooksForTest( perturb.hooks );
	rig.plugin.SetDiscHooksForTest( perturb.discHooks );
	rig.plugin.SetStrobeHooksForTest( perturb.strobeHooks );
	rig.plugin.SetUnprimedForTest( perturb.unprimed );
	rig.plugin.SetResizeForgetsForTest( perturb.resizeForgets );
}

#include "physics.inc"

//===========================================================================
// --names (no GL)
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too) and within FFGL's 16 characters\n" );
	ZoetropePlugin plugin;
	std::map< std::string, int > seen, address;
	int longNames = 0, dupes = 0, clashes = 0;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
		if( name.size() > 16 )
		{
			std::printf( "    too long: %s\n", name.c_str() );
			++longNames;
		}
		if( seen[ name ]++ > 0 )
			++dupes;
		std::string key;
		for( char c : name )
			if( c != ' ' )
				key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
		if( address[ key ]++ > 0 )
			++clashes;
	}
	const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
	const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
	Check( longNames == 0 && dupes == 0 && clashes == 0 && aboutLast,
	       fmt( "SW Zoetrope: %u parameters, %d too long, %d duplicated, %d clashing addresses; the About block is last (%s)",
	            plugin.GetNumParams(), longNames, dupes, clashes, aboutLast ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// --cues (no GL): options, switches, counts and events step; the rest ramp.
//===========================================================================
int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options, switches, counts and events, and ramps standard controls\n" );
	std::istringstream sheet( "0 Trigger 0\n60 Trigger 6\n0 Fire 0\n60 Fire 1\n0 Slip 0\n60 Slip 1\n0 Stations 4\n60 Stations 48\n0 Sync 0\n60 Sync 1\n" );
	std::string error;
	const auto tracks = loadScript( sheet, "cues", error );
	ZoetropePlugin plugin;
	int bad = 0;
	std::string what;
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		const auto found = tracks.find( p.name );
		if( found == tracks.end() )
			continue;
		const bool ramp = perturb.cuesRamp ? true : !stepsBetweenCues( p.type );
		const float mid = valueAt( found->second, 29, ramp );
		const float a = found->second.front().second, b = found->second.back().second;
		const bool expectRamp = p.type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( found->second, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", p.name.c_str(), expectRamp ? "ramps " : "steps ", mid );
	}
	Check( error.empty() && bad == 0 && tracks.size() == 5, fmt( "%d wrong:%s", bad, what.c_str() ) );
	return Verdict();
}

//===========================================================================
// --state: the GL state the host hands over is the state it gets back.
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back\n" );
	int problems = 0;
	std::string what;
	for( int persistence : { 0, 1 } )
	{
		Rig rig;
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.Set( PT_PERSISTENCE, persistence ? 0.5f : 0.0f );
		rig.Set( PT_TRIGGER, static_cast< float >( Trigger::Rate ) );
		rig.Set( PT_RATE, ParamFromRate( 30.0 ) );
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		Floats picture;
		for( int frame = 0; frame < 40; ++frame )
		{
			lookClip( frame, 320, 180, picture );
			rig.Upload( picture );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, 320, 180 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			rig.plugin.SetBeatInfo( 120.0f, static_cast< float >( std::fmod( frame / 60.0 * 0.5, 1.0 ) ) );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
			GLfloat clear[ 4 ]   = {};
			GLboolean mask[ 4 ]  = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			glGetBooleanv( GL_COLOR_WRITEMASK, mask );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0, boundArray = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				glGetIntegerv( GL_TEXTURE_BINDING_2D_ARRAY, &boundArray );
				expect( bound == 0 && boundArray == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		if( rig.plugin.CardState().Texture() == 0 || rig.plugin.LastPrints().size() > 1000 )
			++problems;
	}
	Check( problems == 0,
	       fmt( "40 frames each without and with Persistence, cards printing at 30 Hz: viewport, vertex array, array buffer, "
	            "program, active unit, framebuffer, blend, scissor, clear colour, colour mask, eight texture units (2D and "
	            "array) (%d wrong:%s)",
	            problems, what.empty() ? " none" : what.c_str() ) );
	return Verdict();
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		std::function< void( Rig& ) > set;
	};
	const Case cases[] = {
		{ "defaults at 120 BPM", []( Rig& ) {} },
		{ "a fast disc: Rate 150 Hz, 20 ms flashes, 600 RPM", []( Rig& r ) {
			 r.Set( PT_TRIGGER, static_cast< float >( Trigger::Rate ) );
			 r.Set( PT_RATE, ParamFromRate( 150.0 ) );
			 r.Set( PT_FLASH, ParamFromFlash( 20e-3 ) );
			 r.Set( PT_SYNC, 0.0f );
			 r.Set( PT_RPM, 1.0f );
			 r.Set( PT_ROOM_LIGHT, 0.3f );
			 r.Set( PT_PERSISTENCE, 0.5f );
		 } },
		{ "the drum at 600 RPM, Persistence 100 ms", []( Rig& r ) {
			 r.Set( PT_VIEW, static_cast< float >( View::Drum ) );
			 r.Set( PT_SYNC, 0.0f );
			 r.Set( PT_RPM, 1.0f );
			 r.Set( PT_PERSISTENCE, 0.5f );
		 } },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 120 frames of warm-up, median and worst of 120\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h, Clip() ) )
				return 1;
			Floats still;
			lookClip( 0, size.w, size.h, still );
			rig.Upload( still );
			c.set( rig );
			if( !rig.Render( 120 ) )
				return 1;
			constexpr int kTimed = 120;
			std::vector< double > times, cpu;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-50s %-6s GPU median %6.2f ms, worst %6.2f  (%4.1f%% of a 60 fps frame); wall %5.2f ms\n", c.what,
			             size.name, times[ kTimed / 2 ], times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ] );
		}
	glDeleteQueries( 1, &query );
	return 0;
}

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "drift", runDrift, false },
		{ "lock", runLock, false },
		{ "smear", runSmear, false },
		{ "energy", runEnergy, false },
		{ "delay", runDelay, false },
		{ "hand", runHand, false },
		{ "alias", runAlias, false },
		{ "audio", runAudio, false },
		{ "resize", runResize, false },
		{ "drum", runDrum, false },
		{ "alpha", runAlpha, false },
		{ "afterimage", runAfterimage, false },
		{ "state", runState, false },
		{ "bearing", runBearing, true },
		{ "beat", runBeat, true },
		{ "cues", runCues, true },
		{ "names", runNames, true },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "drift", runDrift, "the expectation without the wrap: omega T itself", []( Perturb& p ) { p.unwrapped = true; } );
	add( "drift", runDrift, "Sync's speed worked out for N + 1 stations", []( Perturb& p ) { p.discHooks = kHookStationMiscount; } );
	add( "lock", runLock, "Sync's speed worked out for N + 1 stations", []( Perturb& p ) { p.discHooks = kHookStationMiscount; } );
	add( "smear", runSmear, "a flash's light spread over half its overlap", []( Perturb& p ) { p.strobeHooks = kHookSmearHalved; } );
	add( "energy", runEnergy, "a flash's light proportional to its length", []( Perturb& p ) { p.strobeHooks = kHookLightPerFlashTau; } );
	add( "energy", runEnergy, "a segment's taps averaged over n + 1", []( Perturb& p ) { p.hooks = shaders::kHookTapWeights; } );
	add( "delay", runDelay, "the print head a station further round than Print At", []( Perturb& p ) { p.discHooks = kHookHeadOneOn; } );
	add( "bearing", runBearing, "the bearing without its dry friction", []( Perturb& p ) { p.discHooks = kHookNoCoulomb; } );
	add( "hand", runHand, "the bearing without its dry friction", []( Perturb& p ) { p.discHooks = kHookNoCoulomb; } );
	add( "beat", runBeat, "a beat fires at the start of its frame", []( Perturb& p ) { p.strobeHooks = kHookBeatAtFrameStart; } );
	add( "alias", runAlias, "the window the whole frame, whatever the Shutter", []( Perturb& p ) { p.strobeHooks = kHookShutterIgnored; } );
	add( "audio", runAudio, "the onset detector's first frame measured against zeroes (deaf for the first kick)", []( Perturb& p ) { p.unprimed = true; } );
	add( "resize", runResize, "a new raster blanks the cards", []( Perturb& p ) { p.resizeForgets = true; } );
	add( "drum", runDrum, "the far wall seen at the exposure's middle, not when each slit passed", []( Perturb& p ) { p.hooks = shaders::kHookDrumStill; } );
	add( "alpha", runAlpha, "the clip's alpha passed through everywhere", []( Perturb& p ) { p.hooks = shaders::kHookClipAlpha; } );
	add( "afterimage", runAfterimage, "the afterimage adds the last picture instead of mixing", []( Perturb& p ) { p.hooks = shaders::kHookAfterglow; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

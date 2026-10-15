//===========================================================================
// The physics checks: the instruments first, then one check per claim.
// Included into checks.inc after Perturb and hook().
//===========================================================================

/// The disc's quiet: nothing fires but Fire, a ten-microsecond flash at
/// Strobe 1, eight stations stopped, Sync off, no room light, a 360-degree
/// shutter, the print head off any station (0.37 rad), a black background,
/// and a disc small enough that no card leaves the frame at four stations
/// (a ring the frame cuts is not N-fold symmetric, and the phase instrument
/// reads the cut).
void quiet( Rig& rig )
{
	rig.Set( PT_TRIGGER, static_cast< float >( Trigger::FireOnly ) );
	rig.Set( PT_RATE, ParamFromRate( 12.0 ) );
	rig.Set( PT_THRESHOLD, 0.4f );
	rig.Set( PT_FLASH, ParamFromFlash( 10e-6 ) );
	rig.Set( PT_STROBE, 0.5f );
	rig.Set( PT_STATIONS, 8.0f );
	rig.Set( PT_DRIVE, static_cast< float >( Drive::Motor ) );
	rig.Set( PT_SYNC, 0.0f );
	rig.Set( PT_SLIP, ParamFromSlip( 1.0 ) );
	rig.Set( PT_RPM, 0.0f );
	rig.Set( PT_PUSH, ParamFromPush( 20.0 * kRpm ) );
	rig.Set( PT_FRICTION, ParamFromViscous( 1.0 ) );
	rig.Set( PT_PRINT_AT, ParamFromPrintAt( 0.37 ) );
	rig.Set( PT_RADIUS, ParamFromRadius( 0.62 ) );
	rig.Set( PT_CARD_SIZE, ParamFromCardSize( 0.9 ) );
	rig.Set( PT_SHUTTER, 1.0f );
	rig.Set( PT_ROOM_LIGHT, 0.0f );
	rig.Set( PT_PERSISTENCE, 0.0f );
	rig.Set( PT_BACKGROUND, static_cast< float >( Background::Black ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, const Raster& raster, Clip clip, const Perturb& perturb )
{
	if( !rig.Init( raster.w, raster.h, std::move( clip ) ) )
		return false;
	hook( rig, perturb );
	quiet( rig );
	return true;
}

/// Print every card: spin the disc at 600 RPM (ten turns a second, Sync
/// off) until each of the first `stations` has passed the head, then stop
/// it.
bool printAll( Rig& rig, int stations )
{
	rig.Set( PT_SYNC, 0.0f );
	rig.Set( PT_DRIVE, static_cast< float >( Drive::Motor ) );
	rig.Set( PT_RPM, 1.0f );
	bool all = false;
	for( int f = 0; f < 120 && !all; ++f )
	{
		if( !rig.Render( 1 ) )
			return false;
		all = true;
		for( int k = 0; k < stations; ++k )
			all = all && rig.plugin.CardState().Printed( k );
	}
	rig.Set( PT_RPM, 0.0f );
	return all;
}

/// The output as linear light (the green channel: every clip the checks use
/// is grey, and so are the disc and the card stock), rows bottom first.
struct Picture
{
	int w = 0, h = 0;
	std::vector< double > light;

	double at( int i, int j ) const
	{
		return light[ static_cast< size_t >( j ) * w + i ];
	}
	/// Bilinear between pixel centres, at ( x, y ) in pixels from the
	/// bottom-left corner.
	double sample( double x, double y ) const
	{
		const double fx = x - 0.5, fy = y - 0.5;
		const int i0 = std::clamp( static_cast< int >( std::floor( fx ) ), 0, w - 2 );
		const int j0 = std::clamp( static_cast< int >( std::floor( fy ) ), 0, h - 2 );
		const double u = std::clamp( fx - i0, 0.0, 1.0 ), v = std::clamp( fy - j0, 0.0, 1.0 );
		return ( 1 - u ) * ( 1 - v ) * at( i0, j0 ) + u * ( 1 - v ) * at( i0 + 1, j0 ) + ( 1 - u ) * v * at( i0, j0 + 1 ) + u * v * at( i0 + 1, j0 + 1 );
	}
};

Picture pictureOf( const Rig& rig )
{
	const Floats out = rig.Output();
	Picture p;
	p.w = rig.width;
	p.h = rig.height;
	p.light.resize( static_cast< size_t >( p.w ) * p.h );
	for( size_t i = 0; i < p.light.size(); ++i )
		p.light[ i ] = linearOf( out[ i * 4 + 1 ] );
	return p;
}

/// Clockwise from twelve o'clock, [ 0, 2 pi ).
double clockwise( double x, double y )
{
	const double a = std::atan2( x, y );
	return a < 0.0 ? a + kTwoPi : a;
}

/// Into [ -span / 2, span / 2 ).
double wrapTo( double x, double span )
{
	return x - span * std::floor( x / span + 0.5 );
}

/// The light the last exposure took from flashes, in flashes.
double flashLight( const ZoetropePlugin& plugin )
{
	double sum = 0.0;
	for( const LightSegment& s : plugin.LastSegments() )
		if( s.flash )
			sum += s.weight;
	return sum;
}

/// The annulus the cards sweep, from the plugin's geometry (where to look,
/// not what is measured).
struct Annulus
{
	double cx, cy, inner, outer;
	int stations;
	double pitch;
};
Annulus annulusOf( const ZoetropePlugin& plugin )
{
	const Geometry& g = plugin.LastGeometry();
	return { g.centreX, g.centreY, g.ring - g.halfHeight - 1.0, std::hypot( g.ring + g.halfHeight, g.halfWidth ) + 1.0, g.stations, g.pitch };
}

/// The N-fold phase of the ring's light: the angle, modulo one station, at
/// which its N-th angular harmonic peaks. For N identical cards, symmetric
/// about their centres, it is the disc's angle modulo a station.
double phaseOf( const Picture& p, const Annulus& a )
{
	std::complex< double > sum( 0.0, 0.0 );
	const int i0 = std::max( 0, static_cast< int >( a.cx - a.outer ) - 1 ), i1 = std::min( p.w, static_cast< int >( a.cx + a.outer ) + 2 );
	const int j0 = std::max( 0, static_cast< int >( a.cy - a.outer ) - 1 ), j1 = std::min( p.h, static_cast< int >( a.cy + a.outer ) + 2 );
	for( int j = j0; j < j1; ++j )
		for( int i = i0; i < i1; ++i )
		{
			const double x = i + 0.5 - a.cx, y = j + 0.5 - a.cy;
			const double r = std::hypot( x, y );
			if( r < a.inner || r > a.outer )
				continue;
			sum += p.at( i, j ) * std::polar( 1.0, a.stations * clockwise( x, y ) );
		}
	double psi = std::arg( sum ) / a.stations;
	if( psi < 0.0 )
		psi += a.pitch;
	return psi;
}

/// The phase instrument's own error, measured each run on renders of the
/// disc held at known angles (a hook stops it there) under steady light:
/// 128 angles evenly over one station and twelve more between them. Returns
/// the worst |measured - held|, radians. The cards must already be printed
/// alike.
///
/// The error is not smooth in the angle. Where the cards' edges come within
/// about one edge-length's worth of a pixel row (a degree or less), every row
/// an edge crosses samples its antialiased ramp at the same phase, and the
/// rows' small first-moment errors add up instead of averaging out: the
/// reading spikes to ~3e-5 rad there and is under 1e-6 elsewhere. That is
/// the pixel grid's, not the plugin's (it is exactly 0 at perfect alignment,
/// by symmetry), and why the calibration is dense.
double calibrate( Rig& rig )
{
	const double pitch = rig.plugin.LastGeometry().pitch;
	rig.Set( PT_ROOM_LIGHT, 1.0f );
	const float trigger = rig.plugin.GetFloatParameter( PT_TRIGGER );
	rig.Set( PT_TRIGGER, static_cast< float >( Trigger::FireOnly ) );
	double worst = 0.0;
	for( int k = 0; k < 140; ++k )
	{
		const double held = k < 128 ? 1e-4 + pitch * k / 128.0 : 0.1234 + pitch * std::fmod( k * 0.618034, 1.0 );
		rig.plugin.SetDiscAngleForTest( held );
		if( !rig.Render( 1 ) )
			return 1.0;
		const double psi = phaseOf( pictureOf( rig ), annulusOf( rig.plugin ) );
		worst            = std::max( worst, std::fabs( wrapTo( psi - held, pitch ) ) );
		if( std::getenv( "ZTTEST_DEBUG" ) )
			std::printf( "    calibrate: held %.6f measured %.6f error %+.3e (station fraction %.4f)\n", held, psi, wrapTo( psi - held, pitch ),
			             std::fmod( held / pitch, 1.0 ) );
	}
	rig.plugin.SetDiscAngleForTest( -1.0 );
	rig.Set( PT_ROOM_LIGHT, 0.0f );
	rig.Set( PT_TRIGGER, trigger );
	return worst;
}

/// One float32 ULP of an angle of 2 pi, in radians: the size of the angle
/// sent to the GPU, ~5e-7.
constexpr double kAngleUlp = 2.0 * kTwoPi * kFloatEps;

/// A twin of the rig under test: the same raster and disc, white cards
/// printed alike, steady light, and a disc held at whatever angle it is
/// asked. What the phase instrument reads off the twin is what it reads for a
/// disc truly at that angle, so a reading of the rig under test can be
/// corrected by the instrument's own error THERE -- which is not smooth (see
/// calibrate()) -- and held to the angle itself.
struct Twin
{
	Rig rig;
	double pitch = 0.0;

	bool Init( const Raster& raster, int stations, const Perturb& perturb )
	{
		if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) )
			return false;
		rig.Set( PT_STATIONS, static_cast< float >( stations ) );
		if( !printAll( rig, stations ) || !rig.Render( 1 ) )
			return false;
		pitch = kTwoPi / stations;
		return true;
	}
	/// The instrument's reading of a disc held at `theta`, less theta.
	double ErrorAt( double theta )
	{
		rig.Set( PT_ROOM_LIGHT, 1.0f );
		rig.plugin.SetDiscAngleForTest( theta );
		if( !rig.Render( 1 ) )
			return 1.0;
		return wrapTo( phaseOf( pictureOf( rig ), annulusOf( rig.plugin ) ) - theta, pitch );
	}
};

/// The disc's angle at the middle of the last frame's first flash, from the
/// plugin's own state: an anchor for where to evaluate the instrument's
/// error, never the thing compared.
double flashAngle( const ZoetropePlugin& plugin )
{
	for( const LightSegment& s : plugin.LastSegments() )
		if( s.flash )
		{
			const Disc& disc = plugin.DiscState();
			return Reduce( disc.StartAngle() + disc.AdvanceAt( disc.FrameLength() + 0.5 * ( s.from + s.to ) ) );
		}
	return 0.0;
}

//===========================================================================
// --drift: the apparent advance per flash is omega T, modulo a station,
// wrapped to +-half a station.
//===========================================================================
int runDrift( const Perturb& perturb )
{
	std::printf( "\n=== drift: the figures move by omega T per flash, modulo a station, wrapped to +-pi / N (the wagon wheel)\n" );
	const double slips[] = { 0.6, 0.9, 0.98, 1.02, 1.1, 1.3, 1.9, 2.0 };
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		Twin twin;
		rig.bpm = 120.0;
		if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) || !printAll( rig, 8 ) || !twin.Init( raster, 8, perturb ) )
			return 1;
		const double instrument = calibrate( twin.rig );
		const double pitch      = kTwoPi / 8.0;
		const double ring       = rig.plugin.LastGeometry().ring;
		rig.Set( PT_TRIGGER, static_cast< float >( Trigger::Beat ) );
		rig.Set( PT_SYNC, 1.0f );
		double worst = 0.0, worstRaw = 0.0, tolerance = 0.0;
		int advances = 0;
		std::string seen;
		for( double slip : slips )
		{
			const float param = static_cast< float >( slip / 2.0 );
			rig.Set( PT_SLIP, param );
			//Slip is linear on 0..2 (documented), read back through the float.
			const double s     = 2.0 * static_cast< double >( param );
			const double omega = s * pitch / 0.5;//a flash a beat, a beat 0.5 s at 120 BPM
			const double expect = perturb.unwrapped ? std::fmod( omega * 0.5, kTwoPi ) : wrapTo( omega * 0.5, pitch );
			//Each advance: two angles sent to the GPU in float (PhaseFrac,
			//the table), twice over with the twin's; and two flash moments
			//placed from a float bar phase (~1.2e-7 s each) at omega.
			tolerance = std::max( tolerance, 4.0 * kAngleUlp + 2.0 * 1.2e-7 * omega );
			std::vector< double > phases, anchors;
			for( int f = 0; f < 240 && phases.size() < 6; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				//A frame that caught a flash (a sliver of one at a window's
				//edge does not count: the frame that caught most of it does).
				if( flashLight( rig.plugin ) >= 0.5 )
				{
					phases.push_back( phaseOf( pictureOf( rig ), annulusOf( rig.plugin ) ) );
					anchors.push_back( flashAngle( rig.plugin ) );
				}
			}
			//The first flash after a change of Slip may be from the old speed.
			//From the second on, flash k is at anchor + ( k - 1 ) omega T.
			for( size_t k = 2; k < phases.size(); ++k )
			{
				const double raw     = wrapTo( phases[ k ] - phases[ k - 1 ], pitch );
				const double here    = anchors[ 1 ] + ( k - 1 ) * omega * 0.5;
				const double there   = anchors[ 1 ] + ( k - 2 ) * omega * 0.5;
				const double advance = raw - wrapTo( twin.ErrorAt( here ) - twin.ErrorAt( there ), pitch );
				worst    = std::max( worst, std::fabs( wrapTo( advance - expect, kTwoPi ) ) );
				worstRaw = std::max( worstRaw, std::fabs( wrapTo( raw - expect, kTwoPi ) ) );
				++advances;
			}
			seen += fmt( " %.2f:%+.4f", s, phases.size() >= 3 ? wrapTo( phases[ 2 ] - phases[ 1 ], pitch ) / pitch : 0.0 );
		}
		Check( worst <= tolerance && advances >= 30,
		       fmt( "%dx%d, 8 stations, Sync on the beat: %d advances over 8 Slips, each less the instrument's error at its two angles "
		            "(read off a held twin): worst %.2e rad (%.5f px at R = %.1f), tolerance %.2e; uncorrected, worst %.2e; advances in "
		            "stations:%s",
		            raster.w, raster.h, advances, worst, worst * ring, ring, tolerance, worstRaw, seen.c_str() ) );
		Check( instrument * ring < 0.05,
		       fmt( "%dx%d: the phase instrument reads a held angle to %.2e rad, %.4f px at R, at its worst over 140 angles (a usable "
		            "instrument is under 0.05 px)",
		            raster.w, raster.h, instrument, instrument * ring ) );
	}
	return Verdict();
}

//===========================================================================
// --lock: Slip 1 with Sync: a card stands still to the pixel for 100 flashes.
//===========================================================================
int runLock( const Perturb& perturb )
{
	std::printf( "\n=== lock: Slip 1 with Sync, a flash every eighth of a beat: the cards stand still for 100 flashes\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		rig.bpm = 120.0;
		if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) )
			return 1;
		rig.Set( PT_STATIONS, 12.0f );
		if( !printAll( rig, 12 ) )
			return 1;
		const double ring  = rig.plugin.LastGeometry().ring;
		const double pitch = kTwoPi / 12.0;
		rig.Set( PT_TRIGGER, static_cast< float >( Trigger::EighthBeat ) );
		rig.Set( PT_SYNC, 1.0f );
		rig.Set( PT_SLIP, ParamFromSlip( 1.0 ) );
		std::vector< double > phases;
		Floats first;
		int differ = 0;
		for( int f = 0; f < 600 && phases.size() < 102; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			if( flashLight( rig.plugin ) < 0.5 )
				continue;
			phases.push_back( phaseOf( pictureOf( rig ), annulusOf( rig.plugin ) ) );
			//The first flash may have caught the disc before Sync's speed.
			if( phases.size() == 2 )
				first = rig.Output();
			else if( phases.size() > 2 )
			{
				const Floats now = rig.Output();
				for( size_t i = 0; i < now.size(); ++i )
					if( byteOf( now[ i ] ) != byteOf( first[ i ] ) )
					{
						++differ;
						break;
					}
			}
		}
		double worst = 0.0;
		for( size_t k = 2; k < phases.size(); ++k )
			worst = std::max( worst, std::fabs( wrapTo( phases[ k ] - phases[ 1 ], pitch ) ) );
		//To the pixel, and then some: a hundredth of a pixel of arc. The
		//angle reaches the GPU as a station's fraction and a table, both
		//float, ~1e-7 rad: at R = 300 px, 3e-5 px.
		const double tolerance = 0.01 / ring;
		Check( phases.size() >= 102 && worst <= tolerance,
		       fmt( "%dx%d, 12 stations: %zu flashes after the first, the ring's phase within %.2e rad of the first's (%.5f px at R), "
		            "tolerance 0.01 px; %d of them not byte-identical to it",
		            raster.w, raster.h, phases.size() > 1 ? phases.size() - 2 : 0, worst, worst * ring, differ ) );
	}
	return Verdict();
}

//===========================================================================
// --smear: a radial edge smeared by a flash of length tau on a disc turning
// at omega spans the arc omega tau R.
//===========================================================================

/// The worst departure, in units of the edge's height, of n taps' staircase
/// smoothed by a bilinear tent of half-width h (both in the same units as the
/// spacing d), from the straight line through it, over its middle 60%.
/// The property of the discretisation (n taps, a texel grid), not of the
/// code under test: it is what the tolerance is built from.
double staircaseRipple( int n, double d, double h )
{
	const double L = n * d;
	auto tentStep  = [ h ]( double s ) {
		if( s <= -h )
			return 0.0;
		if( s >= h )
			return 1.0;
		return s < 0.0 ? 0.5 * ( s + h ) * ( s + h ) / ( h * h ) : 1.0 - 0.5 * ( h - s ) * ( h - s ) / ( h * h );
	};
	double worst = 0.0;
	for( int k = 0; k <= 4000; ++k )
	{
		const double s = -0.3 * L + 0.6 * L * k / 4000.0;
		double x       = 0.0;
		for( int j = 0; j < n; ++j )
			x += tentStep( s - ( -0.5 * L + ( j + 0.5 ) * d ) ) / n;
		worst = std::max( worst, std::fabs( x - ( 0.5 + s / L ) ) );
	}
	return worst;
}

int runSmear( const Perturb& perturb )
{
	std::printf( "\n=== smear: a radial edge smeared by one flash spans omega tau R, by the slope of its ramp\n" );
	struct Case
	{
		const char* name;
		double pixels;///< omega tau R wanted
		double tau;
	};
	//A whole number of pixels with 64 taps, and a fraction with 16: the
	//staircase each leaves is different, and so is the tolerance.
	const Case cases[] = { { "whole", 40.0, 16e-3 }, { "fractional", 13.37, 5e-3 } };
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			Rig rig;
			if( !startRig( rig, raster, splitClip, perturb ) )
				return 1;
			rig.Set( PT_STATIONS, 4.0f );
			if( !printAll( rig, 4 ) )
				return 1;
			const Geometry g     = rig.plugin.LastGeometry();
			const float flash    = ParamFromFlash( c.tau );
			const double tau     = FlashFromParam( flash );
			const float rpm      = ParamFromMotor( c.pixels / ( g.ring * tau ) );
			const double omega   = MotorFromParam( rpm );
			const double arc     = omega * tau;//radians
			const double expectPx = arc * g.ring;
			rig.Set( PT_FLASH, flash );
			rig.Set( PT_RPM, rpm );
			if( !rig.Render( 3 ) || !rig.Fire() )
				return 1;
			const Picture p = pictureOf( rig );
			//Where to look: the station nearest three o'clock at the
			//flash's middle, from the disc's state (locating, not measuring).
			const double mid = rig.plugin.DiscState().Angle() - 0.5 * arc;
			double centre    = 0.0, best = 1e9;
			for( int k = 0; k < 4; ++k )
			{
				const double a = mid + k * g.pitch;
				const double d = std::fabs( wrapTo( a - 0.5 * kPi, kTwoPi ) );
				if( d < best )
				{
					best   = d;
					centre = a;
				}
			}
			//The pixels on the print, away from its margins, and the ramp's
			//middle: x = light / ( Strobe x white ), 0.2 to 0.8.
			std::vector< std::pair< double, double > > ramp;
			int n = 0, mixedTaps = 0;
			double texelAngle = 1e9;
			const double texel = 2.0 * g.printHalfWidth / rig.plugin.CardState().Width();
			for( int j = 0; j < p.h; ++j )
				for( int i = 0; i < p.w; ++i )
				{
					const double x = i + 0.5 - g.centreX, y = j + 0.5 - g.centreY;
					const double r = std::hypot( x, y );
					const double phi = wrapTo( clockwise( x, y ) - centre, kTwoPi );
					const double t = r * std::sin( phi ), q = r * std::cos( phi ) - g.ring;
					//Clear of the print's own edges, which the flash smears too.
					if( std::fabs( q ) > 0.6 * g.printHalfHeight || std::fabs( t ) > g.printHalfWidth - 0.5 * arc * r - 3.0 )
						continue;
					const double level = p.at( i, j );
					if( level < 0.2 || level > 0.8 )
						continue;
					ramp.emplace_back( phi, level );
					int taps = 1;
					while( taps < 64 && taps < arc * r )
						taps *= 2;
					if( n == 0 )
						n = taps;
					mixedTaps += taps != n;
					texelAngle = std::min( texelAngle, texel / r );
				}
			double mp = 0.0, mx = 0.0;
			for( const auto& s : ramp )
				mp += s.first, mx += s.second;
			mp /= std::max< size_t >( ramp.size(), 1 );
			mx /= std::max< size_t >( ramp.size(), 1 );
			double spp = 0.0, spx = 0.0, sabs = 0.0;
			for( const auto& s : ramp )
			{
				spp += ( s.first - mp ) * ( s.first - mp );
				spx += ( s.first - mp ) * ( s.second - mx );
				sabs += std::fabs( s.first - mp );
			}
			const double slope    = spx / spp;//per radian
			const double measured = g.ring / slope;
			//The tolerance: the staircase's worst ripple r (n taps of
			//spacing arc / n, smoothed by the texel's tent, the narrowest
			//smoothing the pipeline has), plus 1/512 for the GPU's 8-bit
			//filter weights, bounds the slope's error by r sum| phi - mean |
			/// sum( phi - mean )^2.
			const double ripple    = staircaseRipple( std::max( n, 1 ), arc / std::max( n, 1 ), texelAngle ) + 1.0 / 512.0 + 1e-5;
			const double tolerance = ripple * sabs / spp / slope * measured;
			Check( ramp.size() > 50 && mixedTaps == 0 && std::fabs( measured - expectPx ) <= tolerance,
			       fmt( "%dx%d, %s: omega %.3f rad/s, tau %.3f ms, R %.2f px: the ramp spans %.4f px against omega tau R = %.4f "
			            "(%.4f%%), tolerance %.4f px from %d taps' staircase (ripple %.2e); %zu pixels",
			            raster.w, raster.h, c.name, omega, tau * 1e3, g.ring, measured, expectPx, 100.0 * ( measured / expectPx - 1.0 ),
			            tolerance, n, ripple, ramp.size() ) );
			//And where: the ramp's middle (x = 1/2) on the station's centre at
			//the flash's middle, as the disc's own angle has it. The fit's
			//value there is off by at most the ripple, so its place by the
			//ripple over the slope, and the angle reached the GPU in float.
			const double middle      = mp + ( 0.5 - mx ) / slope;
			const double placeLimit  = ripple / slope + 2.0 * kAngleUlp;
			Check( std::fabs( middle ) <= placeLimit,
			       fmt( "%dx%d, %s: the ramp's middle %.4f px from the station's centre at the flash's middle (tolerance %.4f px)", raster.w,
			            raster.h, c.name, middle * g.ring, placeLimit * g.ring ) );
		}
	return Verdict();
}

//===========================================================================
// --energy: a flash's light does not depend on its length; the room light's
// blur keeps the disc's mean.
//===========================================================================
int runEnergy( const Perturb& perturb )
{
	std::printf( "\n=== energy: the light of one flash, summed over the frames that caught it, is Strobe x the print whatever its length;\n"
	             "    the room light's blur of a turning disc keeps the ring's total light\n" );
	for( const Raster& raster : kRasters )
	{
		//(a) One flash, from 10 us to 20 ms, on a stopped disc of white prints.
		{
			Rig rig;
			if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) || !printAll( rig, 8 ) || !rig.Render( 2 ) )
				return 1;
			const Geometry g = rig.plugin.LastGeometry();
			const double angle = rig.plugin.DiscState().Angle();
			//The middle half of every print, from the stopped disc's angle.
			std::vector< size_t > inside;
			for( int j = 0; j < rig.height; ++j )
				for( int i = 0; i < rig.width; ++i )
				{
					const double x = i + 0.5 - g.centreX, y = j + 0.5 - g.centreY;
					const double r = std::hypot( x, y );
					const double phi = wrapTo( clockwise( x, y ) - angle, g.pitch );
					const double t = r * std::sin( phi ), q = r * std::cos( phi ) - g.ring;
					if( std::fabs( t ) < 0.5 * g.printHalfWidth && std::fabs( q ) < 0.5 * g.printHalfHeight )
						inside.push_back( static_cast< size_t >( j ) * rig.width + i );
				}
			double worst = 0.0;
			std::string seen;
			for( double tau : { 10e-6, 1e-3, 12e-3, 20e-3 } )
			{
				rig.Set( PT_FLASH, ParamFromFlash( tau ) );
				double total = 0.0;
				int frames   = 0;
				for( int f = 0; f < 4; ++f )
				{
					if( f == 0 ? !rig.Fire() : !rig.Render( 1 ) )
						return 1;
					const Picture p = pictureOf( rig );
					double mean     = 0.0;
					for( size_t index : inside )
						mean += p.light[ index ];
					mean /= std::max< size_t >( inside.size(), 1 );
					frames += mean > 1e-6;
					total += mean;
				}
				worst = std::max( worst, std::fabs( total - 1.0 ) );
				seen += fmt( " %.0f us: %.6f over %d frame%s;", tau * 1e6, total, frames, frames == 1 ? "" : "s" );
			}
			//The print is 1.0 exactly in half float; the encode and its
			//inverse are GLSL's pow against a double's, a few float ULPs.
			Check( worst < 1e-4 && inside.size() > 100,
			       fmt( "%dx%d, one flash at Strobe 1 on a white print:%s worst |sum - 1| %.2e (tolerance 1e-4)", raster.w, raster.h,
			            seen.c_str(), worst ) );
		}
		//(b) The room light's blur keeps the ring's light: the blurred ring's
		//total lies within the spread of the still ring's totals at every
		//angle, since with one tap set for every pixel it is their average.
		{
			Rig rig;
			if( !startRig( rig, raster, numberClip, perturb ) )
				return 1;
			rig.clip = Clip();//frozen: every card gets the same print
			Floats frozen;
			numberClip( 341, raster.w, raster.h, frozen );
			rig.Upload( frozen );
			if( !printAll( rig, 8 ) )
				return 1;
			const Geometry g = rig.plugin.LastGeometry();
			const Annulus a  = annulusOf( rig.plugin );
			auto total       = [ & ]( const Picture& p ) {
                double sum = 0.0;
                for( int j = 0; j < p.h; ++j )
                    for( int i = 0; i < p.w; ++i )
                    {
                        const double r = std::hypot( i + 0.5 - a.cx, j + 0.5 - a.cy );
                        if( r >= a.inner - 2.0 && r <= a.outer + 2.0 )
                            sum += p.at( i, j );
                    }
                return sum;
			};
			rig.Set( PT_ROOM_LIGHT, 0.5f );
			double lo = 1e300, hi = -1e300;
			for( int k = 0; k < 32; ++k )
			{
				rig.plugin.SetDiscAngleForTest( 0.05 + g.pitch * k / 32.0 );
				if( !rig.Render( 1 ) )
					return 1;
				const double s = total( pictureOf( rig ) );
				lo = std::min( lo, s );
				hi = std::max( hi, s );
			}
			rig.plugin.SetDiscAngleForTest( -1.0 );
			//A blur of 45 px at R: 64 taps for every pixel of the band.
			const double omega = 45.0 / g.ring * 60.0;
			rig.Set( PT_RPM, ParamFromMotor( omega ) );
			if( !rig.Render( 2 ) )
				return 1;
			const double blurred = total( pictureOf( rig ) );
			//The still totals' spread is the pixel grid's; a quarter of it
			//again for angles between the 32 sampled.
			const double slack = 0.25 * ( hi - lo ) + 1e-5 * hi;
			Check( blurred >= lo - slack && blurred <= hi + slack && hi > 0.0,
			       fmt( "%dx%d, Room Light 0.5, a 45 px blur: the ring's total %.4f, the still ring's %.4f to %.4f over 32 angles "
			            "(spread %.2e of it)",
			            raster.w, raster.h, blurred, lo, hi, ( hi - lo ) / hi ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --delay: the card at angle phi shows the frame printed ( phi - h ) / omega
// earlier, read off the cards in the picture.
//===========================================================================

/// Read a card's frame number off the picture: the twelve stripes of the
/// number clip at the print's middle height. -1 if the guards do not read.
int readCard( const Picture& p, const Geometry& g, double centre )
{
	const double sx = std::sin( centre ), cy = std::cos( centre );
	const double ex = std::cos( centre ), ey = -std::sin( centre );//clockwise tangent
	double level[ kStripes ];
	for( int s = 0; s < kStripes; ++s )
	{
		const double t = ( 2.0 * ( s + 0.5 ) / kStripes - 1.0 ) * g.printHalfWidth;
		const double x = g.centreX + g.ring * sx + t * ex, y = g.centreY + g.ring * cy + t * ey;
		if( x < 1.0 || y < 1.0 || x > p.w - 1.0 || y > p.h - 1.0 )
			return -1;
		level[ s ] = p.sample( x, y );
	}
	const double white = level[ 0 ], black = level[ kStripes - 1 ];
	if( white - black < 0.5 )
		return -1;
	int value = 0;
	for( int s = 1; s < kStripes - 1; ++s )
	{
		const double v = ( level[ s ] - black ) / ( white - black );
		if( v > 0.25 && v < 0.75 )
			return -1;//a stripe that is neither: not a clean read
		value = value * 2 + ( v >= 0.5 ? 1 : 0 );
	}
	return value;
}

int runDelay( const Perturb& perturb )
{
	std::printf( "\n=== delay: each card shows the clip's frame from when it passed the print head, ( phi - h ) / omega before the flash\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		rig.bpm = 120.0;
		if( !startRig( rig, raster, numberClip, perturb ) )
			return 1;
		//Locked on the beat from the first frame: theta( t ) = omega t.
		const int stations  = 8;
		const double pitch  = kTwoPi / stations;
		const double head   = PrintAtFromParam( ParamFromPrintAt( 0.37 ) );
		const double omega  = pitch / 0.5;
		rig.Set( PT_TRIGGER, static_cast< float >( Trigger::Beat ) );
		rig.Set( PT_SYNC, 1.0f );
		rig.Set( PT_SLIP, ParamFromSlip( 1.0 ) );
		const Geometry* g = nullptr;
		int read = 0, right = 0, flashes = 0;
		std::string wrong;
		for( int f = 0; f < 600; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			g = &rig.plugin.LastGeometry();
			//Read only after a full turn, when every card has been printed.
			if( flashLight( rig.plugin ) < 0.5 || rig.Seconds( f ) < 4.5 )
				continue;
			++flashes;
			const Picture p = pictureOf( rig );
			//The flash's middle: its start is on the beat (the window ends
			//on the frame), plus half its length.
			const double tm    = std::round( rig.Seconds( f ) * 2.0 ) / 2.0 + 0.5 * FlashFromParam( ParamFromFlash( 10e-6 ) );
			const double theta = std::fmod( omega * tm, kTwoPi );
			for( int k = 0; k < stations; ++k )
			{
				const double phi = theta + k * pitch;
				//It passed the head this long ago, within one turn.
				double ago = std::fmod( phi - head, kTwoPi );
				if( ago < 0.0 )
					ago += kTwoPi;
				const double tc = tm - ago / omega;
				//The frame it was printed on: the host frame whose interval
				//( ( n - 1 ) / 60, n / 60 ] holds that moment.
				const double where = tc * rig.fps;
				if( std::fabs( where - std::round( where ) ) < 1e-6 )
					continue;//on a frame boundary: not decidable
				const int expect = static_cast< int >( std::ceil( where ) ) & 1023;
				const int got    = readCard( p, *g, phi );
				++read;
				if( got == expect )
					++right;
				else if( wrong.size() < 200 )
					wrong += fmt( " (card at %.3f: read %d, want %d)", phi, got, expect );
			}
		}
		Check( flashes >= 3 && read >= 3 * stations && right == read,
		       fmt( "%dx%d, 8 cards locked on the beat, the head at 0.37 rad: %d of %d cards over %d flashes show the frame printed "
		            "( phi - h ) / omega before%s",
		            raster.w, raster.h, right, read, flashes, wrong.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --bearing (no GL) and --hand: a hand-spun disc slows as the bearing's
// closed form says, each push adds Push, and it stops when that says.
//===========================================================================

/// The bearing's equation, d omega / dt = -k omega - c while omega > 0 (at
/// 0 the disc stays put), solved by RK4 with pushes, independently of the
/// closed form: the reference both are held to.
struct Rk4Disc
{
	double k, c;
	double theta = 0.0, omega = 0.0, t = 0.0;
	double stoppedAt = -1.0;

	void step( double h )
	{
		if( omega <= 0.0 )
		{
			omega = 0.0;
			t += h;
			return;
		}
		auto f    = [ & ]( double w ) { return -k * w - c; };
		const double k1 = f( omega ), k2 = f( omega + 0.5 * h * k1 ), k3 = f( omega + 0.5 * h * k2 ), k4 = f( omega + h * k3 );
		const double w1 = omega + h / 6.0 * ( k1 + 2.0 * k2 + 2.0 * k3 + k4 );
		if( w1 > 0.0 )
		{
			//theta' = omega: the same stages, integrated once more.
			const double a1 = omega, a2 = omega + 0.5 * h * k1, a3 = omega + 0.5 * h * k2, a4 = omega + h * k3;
			theta += h / 6.0 * ( a1 + 2.0 * a2 + 2.0 * a3 + a4 );
			omega = w1;
			t += h;
			return;
		}
		//It stops inside this step: halve until the step ends at the stop.
		double lo = 0.0, hi = h;
		for( int i = 0; i < 60; ++i )
		{
			Rk4Disc probe = *this;
			const double mid = 0.5 * ( lo + hi );
			probe.k = k, probe.c = c;
			const double q1 = f( omega ), q2 = f( omega + 0.5 * mid * q1 ), q3 = f( omega + 0.5 * mid * q2 ), q4 = f( omega + mid * q3 );
			if( omega + mid / 6.0 * ( q1 + 2.0 * q2 + 2.0 * q3 + q4 ) > 0.0 )
				lo = mid;
			else
				hi = mid;
		}
		const double a1 = omega, q1 = f( omega ), q2 = f( omega + 0.5 * lo * q1 ), q3 = f( omega + 0.5 * lo * q2 );
		theta += lo / 6.0 * ( a1 + 2.0 * ( omega + 0.5 * lo * q1 ) + 2.0 * ( omega + 0.5 * lo * q2 ) + ( omega + lo * q3 ) );
		stoppedAt = t + lo;
		omega     = 0.0;
		t += h;
	}

	/// Run to time `to` in steps of at most `h`, landing on it exactly.
	void runTo( double to, double h )
	{
		while( t < to - 1e-15 )
			step( std::min( h, to - t ) );
	}
};

int runBearing( const Perturb& perturb )
{
	std::printf( "\n=== bearing: a hand-spun disc against an RK4 solution of d omega / dt = -k omega - c, pushes and all\n" );
	const Bearing bearing = BearingFromParam( ParamFromViscous( 1.0 ) );
	const double push     = PushFromParam( ParamFromPush( 30.0 * kRpm ) );
	const double dt       = 1.0 / 60.0;
	//Six pushes half a second apart, then a coast to a stop.
	std::vector< double > pushTimes;
	for( int j = 1; j <= 6; ++j )
		pushTimes.push_back( 0.5 * j + 0.0037 );
	Disc disc;
	disc.SetHooksForTest( perturb.discHooks );
	Rk4Disc rk { bearing.viscous, bearing.coulomb };
	double total = 0.0, worstTheta = 0.0, worstOmega = 0.0, worstJump = 0.0, discStop = -1.0, afterLast = 0.0;
	int samples = 0;
	for( int f = 1; f <= 600; ++f )
	{
		const double t0 = ( f - 1 ) * dt;
		std::vector< double > offsets;
		for( double p : pushTimes )
			if( p > t0 && p <= t0 + dt )
				offsets.push_back( p - t0 );
		disc.Advance( dt, Drive::Hand, 0.0, offsets, push, bearing );
		//This frame's moments in time order: two samples and its pushes.
		std::vector< std::pair< double, bool > > events = { { dt / 3.0, false }, { dt, false } };
		for( double o : offsets )
			events.emplace_back( o, true );
		std::sort( events.begin(), events.end() );
		for( const auto& e : events )
		{
			rk.runTo( t0 + e.first, 1e-5 );
			if( e.second )
			{
				rk.omega += push;
				afterLast = rk.omega;
				//Just after the push less just before it: Push, to the
				//bearing's slope over a nanosecond.
				worstJump = std::max( worstJump, std::fabs( disc.OmegaAt( e.first ) - disc.OmegaAt( e.first - 1e-9 ) - push ) );
				continue;
			}
			worstTheta = std::max( worstTheta, std::fabs( total + disc.AdvanceAt( e.first ) - rk.theta ) );
			worstOmega = std::max( worstOmega, std::fabs( disc.OmegaAt( e.first ) - rk.omega ) );
			++samples;
		}
		total += disc.AdvanceAt( dt );
		if( discStop < 0.0 && t0 > pushTimes.back() && disc.OmegaAt( dt ) == 0.0 )
		{
			double lo = 0.0, hi = dt;
			for( int i = 0; i < 80; ++i )
			{
				const double mid = 0.5 * ( lo + hi );
				( disc.OmegaAt( mid ) > 0.0 ? lo : hi ) = mid;
			}
			discStop = t0 + hi;
		}
	}
	const double k = bearing.viscous, c = bearing.coulomb;
	//The closed form's stop: t* = ln( 1 + k omega0 / c ) / k after the last
	//push, omega0 taken from the RK4 run.
	const double closed = pushTimes.back() + std::log( 1.0 + k * afterLast / c ) / k;
	//RK4 with 10 us steps: a global error far under 1e-12; the closed form
	//in double, ~1e-15 of the angle. 1e-8 is loose for both and tight for
	//any wrong term (the Coulomb part alone is 0.25 rad/s^2).
	Check( samples > 1000 && worstTheta < 1e-8 && worstOmega < 1e-8,
	       fmt( "k = %.2f /s, c = %.3f rad/s^2, six pushes of %.3f rad/s: over %d samples the closed form is within %.1e rad and "
	            "%.1e rad/s of RK4 (tolerance 1e-8)",
	            k, c, push, samples, worstTheta, worstOmega ) );
	Check( worstJump < 1e-7,
	       fmt( "each push adds Push to omega: worst |jump - Push| %.1e rad/s (tolerance 1e-7: the bearing's slope, ~10 rad/s^2, over the "
	            "nanosecond the jump is read across)",
	            worstJump ) );
	Check( rk.stoppedAt > 0.0 && discStop > 0.0 && std::fabs( discStop - closed ) < 1e-6 && std::fabs( rk.stoppedAt - closed ) < 1e-6,
	       fmt( "from %.3f rad/s after the last push the disc stops at %.6f s, RK4 at %.6f s, the closed form ln( 1 + k omega / c ) / k "
	            "at %.6f s (tolerance 1 us)",
	            afterLast, discStop, rk.stoppedAt, closed ) );
	return Verdict();
}

int runHand( const Perturb& perturb )
{
	std::printf( "\n=== hand: the disc's angle out of the picture follows the bearing's equation (RK4), push by push, and stops when it should\n" );
	if( runBearing( perturb ) != 0 )
		return 1;
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		Twin twin;
		if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) || !printAll( rig, 8 ) || !rig.Render( 1 ) || !twin.Init( raster, 8, perturb ) )
			return 1;
		const double ring  = rig.plugin.LastGeometry().ring;
		const double pitch = kTwoPi / 8.0;
		//Seen by room light through a 1-degree shutter: the angle at
		//t_n - S / 2 (the decelerating disc's blur over 46 us is one tap,
		//at the window's middle). Fire pushes, and lights nothing at Strobe 0.
		rig.Set( PT_DRIVE, static_cast< float >( Drive::Hand ) );
		rig.Set( PT_STROBE, 0.0f );
		rig.Set( PT_ROOM_LIGHT, 1.0f );
		const float shutterParam = 1.0f / 360.0f;
		rig.Set( PT_SHUTTER, shutterParam );
		rig.Set( PT_PUSH, ParamFromPush( 30.0 * kRpm ) );
		rig.Set( PT_FRICTION, ParamFromViscous( 1.0 ) );
		const Bearing bearing = BearingFromParam( ParamFromViscous( 1.0 ) );
		const double push     = PushFromParam( ParamFromPush( 30.0 * kRpm ) );
		const double shutter  = ShutterFromParam( shutterParam ) / rig.fps;
		const double tau      = FlashFromParam( ParamFromFlash( 10e-6 ) );
		//At rest: the plugin's angle is the anchor, RK4 the rest.
		const double anchor = rig.plugin.DiscState().Angle();
		Rk4Disc rk { bearing.viscous, bearing.coulomb };
		rk.t = rig.Seconds( rig.frame - 1 );
		std::vector< double > pushAt;
		std::vector< std::pair< double, double > > track;//( seen at, angle ) for the stop
		double worst = 0.0, afterLast = 0.0;
		int frames = 0;
		for( int f = 0; f < 480; ++f )
		{
			const bool press = f < 180 && f % 30 == 5;
			rig.Set( PT_FIRE, press ? 1.0f : 0.0f );
			if( !rig.Render( 1 ) )
				return 1;
			const double now = rig.Seconds( rig.frame - 1 );
			if( press )
				pushAt.push_back( now - tau );//a press's flash, and its push, end on the frame
			const double seenAt = now - 0.5 * shutter;
			for( double p : pushAt )
				if( p > rk.t && p <= seenAt )
				{
					rk.runTo( p, 1e-5 );
					rk.omega += push;
					afterLast = rk.omega;
				}
			rk.runTo( seenAt, 1e-5 );
			const double expect = anchor + rk.theta;
			const double psi    = phaseOf( pictureOf( rig ), annulusOf( rig.plugin ) );
			const double error  = wrapTo( psi - expect - twin.ErrorAt( expect ), pitch );
			worst               = std::max( worst, std::fabs( error ) );
			track.emplace_back( seenAt, psi );
			++frames;
		}
		const double k = bearing.viscous, c = bearing.coulomb;
		const double closed = pushAt.back() + std::log( 1.0 + k * afterLast / c ) / k;
		//The angle to the GPU in float, here and in the twin (2 ulp each);
		//RK4 against the closed form, 3e-11.
		const double tolerance = 4.0 * kAngleUlp;
		Check( frames > 400 && worst <= tolerance,
		       fmt( "%dx%d: six presses of Fire half a second apart, then a coast: over %d frames the disc's angle out of the picture, "
		            "less the instrument's error there, is within %.2e rad (%.5f px at R) of RK4, tolerance %.2e",
		            raster.w, raster.h, frames, worst, worst * ring, tolerance ) );
		//After the closed form's stop (and a frame for the one that holds
		//it) the picture must not move at all; over the quarter-second
		//before it, the disc must still have turned.
		double still = 0.0, turned = 0.0;
		int stillFrames = 0;
		for( size_t i = 1; i < track.size(); ++i )
		{
			const double step = std::fabs( wrapTo( track[ i ].second - track[ i - 1 ].second, pitch ) );
			if( track[ i - 1 ].first > closed + 1.0 / rig.fps )
			{
				still = std::max( still, step );
				++stillFrames;
			}
			else if( track[ i ].first > closed - 0.25 )
				turned += step;
		}
		Check( stillFrames > 30 && still == 0.0 && turned > 1e-3,
		       fmt( "%dx%d: the closed form stops the disc at %.4f s; the %d frames after it read the same angle to the last bit (largest "
		            "step %.1e rad), and it turned %.2e rad in the quarter-second before",
		            raster.w, raster.h, closed, stillFrames, still, turned ) );
	}
	return Verdict();
}

//===========================================================================
// --beat (no GL): every flash starts on its beat or subdivision, placed
// inside its frame, on the host's clock and on the plugin's own.
//===========================================================================
int runBeat( const Perturb& perturb )
{
	std::printf( "\n=== beat: every flash starts on the host's beat or subdivision, placed inside its frame, on both clocks\n" );
	struct Case
	{
		Trigger trigger;
		const char* name;
	};
	const Case cases[] = { { Trigger::Beat, "Beat" },
		                   { Trigger::HalfBeat, "Half Beat" },
		                   { Trigger::QuarterBeat, "Quarter Beat" },
		                   { Trigger::EighthBeat, "Eighth Beat" },
		                   { Trigger::Bar, "Bar" } };
	const double bpm = 128.0, fps = 60.0;
	const int frames = 600;
	for( bool host : { true, false } )
		for( const Case& c : cases )
		{
			Strobe strobe;
			strobe.SetHooksForTest( perturb.strobeHooks );
			StrobeInputs in;
			in.trigger  = c.trigger;
			in.hostBeat = host;
			in.bpm      = bpm;
			in.flash    = 10e-6;
			std::vector< double > starts, pushes;
			for( int f = 0; f < frames; ++f )
			{
				const double now = f / fps;
				in.barPhase      = std::fmod( now * bpm / 240.0, 1.0 );
				const unsigned long long before = strobe.Fired();
				strobe.Advance( now, f == 0 ? 0.0 : 1.0 / fps, in, pushes );
				const auto& list = strobe.Flashes();
				for( size_t i = list.size() - static_cast< size_t >( strobe.Fired() - before ); i < list.size(); ++i )
					starts.push_back( list[ i ].start );
			}
			//Subdivision k is at k * 60 / bpm * beats-per-flash seconds.
			const double period = 60.0 / bpm * BeatsPerFlash( c.trigger );
			const int expected  = static_cast< int >( std::floor( ( frames - 1 ) / fps / period + 1e-9 ) );
			double worst        = 0.0;
			for( double s : starts )
				worst = std::max( worst, std::fabs( s - std::round( s / period ) * period ) );
			Check( static_cast< int >( starts.size() ) == expected && worst < 1e-9,
			       fmt( "%-12s (%s clock): %zu flashes, %d subdivisions in %.2f s; worst departure from its moment %.2g s", c.name,
			            host ? "host's" : "own", starts.size(), expected, ( frames - 1 ) / fps, worst ) );
		}
	//Rate: free running, m / Rate from the moment it was chosen.
	{
		Strobe strobe;
		strobe.SetHooksForTest( perturb.strobeHooks );
		StrobeInputs in;
		in.trigger = Trigger::Rate;
		in.rate    = 37.3;
		std::vector< double > starts, pushes;
		for( int f = 0; f < 300; ++f )
		{
			const unsigned long long before = strobe.Fired();
			strobe.Advance( f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, in, pushes );
			const auto& list = strobe.Flashes();
			for( size_t i = list.size() - static_cast< size_t >( strobe.Fired() - before ); i < list.size(); ++i )
				starts.push_back( list[ i ].start );
		}
		double worst = 0.0;
		for( size_t m = 0; m < starts.size(); ++m )
			worst = std::max( worst, std::fabs( starts[ m ] - ( m + 1 ) / 37.3 ) );
		const size_t expected = static_cast< size_t >( std::floor( 299.0 / 60.0 * 37.3 ) );
		Check( starts.size() == expected && worst < 1e-9,
		       fmt( "Rate 37.3 Hz: %zu flashes in %.2f s (want %zu), the m-th at m / 37.3 s to %.2g s", starts.size(), 299.0 / 60.0, expected, worst ) );
	}
	return Verdict();
}

//===========================================================================
// --audio: an onset fires the strobe on its own frame; the detector is
// primed, so the cold start fires nothing.
//===========================================================================
int runAudio( const Perturb& perturb )
{
	std::printf( "\n=== audio: an onset fires a flash the frame it is heard, caught by that frame's exposure; nothing fires from the cold start\n" );
	const Raster raster = kRasters.back();
	//A bed of hi-hat, a kick every half second from frame 20; for the cold
	//start, the first frames already loud (a clip triggered mid-song).
	std::vector< int > kicks;
	int origin = 1 << 20;
	auto music = [ & ]( ZoetropePlugin& plugin, int f, bool loudStart ) {
		const int since = ( f - 20 ) % 30;
		const bool hard = ( ( f - 20 ) / 30 ) % 2 == 1;
		for( int bin = 0; bin < audio::kBins; ++bin )
		{
			double v = bin > 40 ? 0.05 : 0.02;
			if( f >= 20 && since < 4 && bin < 10 )
				v += ( hard ? 0.9 : 0.25 ) * std::exp( -since * 0.8 );
			if( loudStart && f < 2 )
				v += 0.3;
			plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( bin ), static_cast< float >( v ) );
		}
	};
	Rig rig;
	if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) )
		return 1;
	//The bed plays from the first frame (music that starts from silence is
	//an onset, rightly); the kicks start 20 frames after `origin`.
	rig.beforeFrame = [ & ]( int f ) {
		const int g = f - origin;
		music( rig.plugin, g, false );
		if( g >= 20 && ( g - 20 ) % 30 == 0 )
			kicks.push_back( g );
	};
	if( !printAll( rig, 8 ) || !rig.Render( 30 ) )
		return 1;
	//A fresh instance for the cold start: the printed one above is only
	//for what the camera sees.
	Rig cold;
	if( !startRig( cold, raster, flatClip( 1.0f ), perturb ) )
		return 1;
	for( Rig* r : { &rig, &cold } )
	{
		r->Set( PT_TRIGGER, static_cast< float >( Trigger::Onset ) );
		r->Set( PT_THRESHOLD, 0.4f );
	}
	std::vector< int > fired, lit;
	origin = rig.frame;
	for( int g = 0; g < 200; ++g )
	{
		const unsigned long long before = rig.plugin.StrobeState().Fired();
		if( !rig.Render( 1 ) )
			return 1;
		if( rig.plugin.StrobeState().Fired() > before )
			fired.push_back( g );
		if( flashLight( rig.plugin ) >= 0.99 )
			lit.push_back( g );
	}
	cold.beforeFrame = [ & ]( int f ) { music( cold.plugin, f, true ); };
	int coldFlashes  = 0;
	for( int g = 0; g < 20; ++g )
	{
		const unsigned long long before = cold.plugin.StrobeState().Fired();
		if( !cold.Render( 1 ) )
			return 1;
		coldFlashes += static_cast< int >( cold.plugin.StrobeState().Fired() - before );
	}
	int matched = 0;
	for( int k : kicks )
		matched += std::count( fired.begin(), fired.end(), k ) == 1 && std::count( lit.begin(), lit.end(), k ) == 1;
	Check( matched == static_cast< int >( kicks.size() ) && fired.size() == kicks.size() && lit.size() == kicks.size(),
	       fmt( "%zu kicks, %zu flashes fired, %zu frames lit by a whole flash, %d of the kicks fired and caught on their own frame", kicks.size(),
	            fired.size(), lit.size(), matched ) );
	Check( coldFlashes == 0, fmt( "a fresh instance with music already loud on its first frame: %d flash%s in the first 20 frames (none before "
	                              "the first kick)",
	                              coldFlashes, coldFlashes == 1 ? "" : "es" ) );
	return Verdict();
}

//===========================================================================
// --alias: what each frame's exposure catches of a strobe, against the
// closed form of the window [ t_n - S, t_n ]: a Rate strobe faster than the
// frame rate, and beats that fall anywhere in a frame.
//===========================================================================
int runAlias( const Perturb& perturb )
{
	std::printf( "\n=== alias: the flashes each frame catches, from the picture, against the shutter window's closed form\n" );
	struct Case
	{
		const char* name;
		Trigger trigger;
		double rate;   ///< Hz, for Rate
		double bpm;
		double shutter;///< degrees
	};
	const Case cases[] = {
		{ "Rate 157 Hz, 200-degree shutter", Trigger::Rate, 157.0, 120.0, 200.0 },
		{ "Rate 61.7 Hz, 90-degree shutter", Trigger::Rate, 61.7, 120.0, 90.0 },
		{ "Beat at 117 BPM, 180-degree shutter", Trigger::Beat, 0.0, 117.0, 180.0 },
		{ "Eighth Beat at 117 BPM, 180-degree shutter", Trigger::EighthBeat, 0.0, 117.0, 180.0 },
	};
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			Rig rig;
			rig.bpm = c.bpm;
			if( !startRig( rig, raster, flatClip( 1.0f ), perturb ) || !printAll( rig, 8 ) || !rig.Render( 1 ) )
				return 1;
			const Geometry g   = rig.plugin.LastGeometry();
			const double angle = rig.plugin.DiscState().Angle();
			std::vector< size_t > inside;
			for( int j = 0; j < rig.height; ++j )
				for( int i = 0; i < rig.width; ++i )
				{
					const double x = i + 0.5 - g.centreX, y = j + 0.5 - g.centreY;
					const double r = std::hypot( x, y );
					const double phi = wrapTo( clockwise( x, y ) - angle, g.pitch );
					if( std::fabs( r * std::sin( phi ) ) < 0.5 * g.printHalfWidth && std::fabs( r * std::cos( phi ) - g.ring ) < 0.5 * g.printHalfHeight )
						inside.push_back( static_cast< size_t >( j ) * rig.width + i );
				}
			const float shutterParam = static_cast< float >( c.shutter / 360.0 );
			const double window      = ShutterFromParam( shutterParam ) / rig.fps;
			const float rateParam    = ParamFromRate( c.rate > 0.0 ? c.rate : 12.0 );
			const double rate        = RateFromParam( rateParam );
			const double tau         = FlashFromParam( ParamFromFlash( 10e-6 ) );
			rig.Set( PT_SHUTTER, shutterParam );
			rig.Set( PT_RATE, rateParam );
			rig.Set( PT_TRIGGER, static_cast< float >( c.trigger ) );
			//The strobe's moments, from the harness's own clock: Rate's from
			//the frame it was chosen on, the beats from the host's grid.
			const double origin = rig.Seconds( rig.frame );
			const double period = c.trigger == Trigger::Rate ? 1.0 / rate : 60.0 / c.bpm * BeatsPerFlash( c.trigger );
			auto flashAt = [ & ]( long long m ) { return c.trigger == Trigger::Rate ? origin + m * period : m * period; };
			double worst = 0.0;
			int frames = 0, skipped = 0;
			std::map< int, int > histogram;
			for( int f = 0; f < 240; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const double now = rig.Seconds( rig.frame - 1 );
				if( f < 2 )
					continue;
				//The closed form: sum over flashes of | [ t_m, t_m + tau ] n
				//[ now - S, now ] | / tau.
				double expect = 0.0;
				bool edge     = false;
				const long long first = static_cast< long long >( std::floor( ( c.trigger == Trigger::Rate ? now - window - origin : now - window ) / period ) ) - 1;
				for( long long m = std::max( first, 1LL ); m <= first + 2 + static_cast< long long >( window / period ); ++m )
				{
					const double t = flashAt( m );
					expect += std::max( 0.0, std::min( t + tau, now ) - std::max( t, now - window ) ) / tau;
					//A float bar phase places a beat to ~1e-7 s: a flash that
					//close to a window's edge cannot be called.
					edge = edge || std::fabs( t - now ) < 1e-6 || std::fabs( t - ( now - window ) ) < 1e-6
					       || std::fabs( t + tau - now ) < 1e-6 || std::fabs( t + tau - ( now - window ) ) < 1e-6;
				}
				if( edge )
				{
					++skipped;
					continue;
				}
				const Picture p = pictureOf( rig );
				double mean     = 0.0;
				for( size_t index : inside )
					mean += p.light[ index ];
				mean /= std::max< size_t >( inside.size(), 1 );
				worst = std::max( worst, std::fabs( mean - expect ) );
				++histogram[ static_cast< int >( std::lround( mean ) ) ];
				++frames;
			}
			std::string counts;
			for( const auto& h : histogram )
				counts += fmt( " %d:%d", h.first, h.second );
			//The print is 1.0 in half float; a flash's light is Strobe x its
			//caught fraction; the encode and its inverse a few float ULPs.
			Check( frames > 200 && worst < 1e-4,
			       fmt( "%dx%d, %s: %d frames (%d on an edge, skipped), worst |caught - closed form| %.1e flashes (tolerance 1e-4); "
			            "flashes caught per frame:%s",
			            raster.w, raster.h, c.name, frames, skipped, worst, counts.c_str() ) );
		}
	return Verdict();
}

//===========================================================================
// --resize: the printed cards survive a change of the host's raster.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes, every card still shows the frame it was printed with\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h };
	Rig rig;
	if( !startRig( rig, big, numberClip, perturb ) || !printAll( rig, 8 ) || !rig.Render( 1 ) || !rig.Fire() )
		return 1;
	auto readAll = [ & ]() {
		std::vector< int > out;
		const Geometry g = rig.plugin.LastGeometry();
		const Picture p  = pictureOf( rig );
		for( int k = 0; k < 8; ++k )
			out.push_back( readCard( p, g, rig.plugin.DiscState().Angle() + k * g.pitch ) );
		return out;
	};
	const std::vector< int > before = readAll();
	if( !rig.Resize( small.w, small.h ) || !rig.Fire() )
		return 1;
	const std::vector< int > after = readAll();
	int good = 0, same = 0;
	std::string seen;
	for( int k = 0; k < 8; ++k )
	{
		good += before[ k ] >= 0;
		same += before[ k ] >= 0 && after[ k ] == before[ k ];
		seen += fmt( " %d/%d", before[ k ], after[ k ] );
	}
	Check( good == 8 && same == 8,
	       fmt( "%dx%d, then %dx%d: %d of 8 cards read the same frame number before and after (before/after:%s)", big.w, big.h, small.w,
	            small.h, same, seen.c_str() ) );
	return Verdict();
}

//===========================================================================
// --drum: the figures inside a turning drum, seen through its slits, are
// squeezed sideways by ( d + a ) / ( 2 d ) at the middle of the view.
//===========================================================================

/// The drum's map from a screen column to room angles, in double, from the
/// harness's own ray-cylinder geometry: the eye d radii off the axis, focal
/// length f pixels. G is the far wall's drum angle seen through a slit at its
/// middle (less the slit's own), gammaFar the far wall's room angle.
struct DrumRay
{
	double d, f;
	double near( double x ) const { return hit( x, false ); }
	double far( double x ) const { return hit( x, true ); }
	double G( double x ) const { return wrapTo( far( x ), kTwoPi ) - near( x ) + kPi; }
	double hit( double x, bool farSide ) const
	{
		const double X = x / f, A = X * X + 1.0;
		const double root = std::sqrt( d * d - A * ( d * d - 1.0 ) );
		const double t    = ( d + ( farSide ? root : -root ) ) / A;
		return clockwise( X * t, t - d );
	}
	/// The column where `map` reaches `target`, by bisection about `guess`.
	template < typename Map >
	double solve( Map map, double target, double lo, double hi ) const
	{
		for( int i = 0; i < 200; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( map( mid ) < target ? lo : hi ) = mid;
		}
		return 0.5 * ( lo + hi );
	}
};

int runDrum( const Perturb& perturb )
{
	std::printf( "\n=== drum: through the moving slits the figures stand where the geometry puts them, squeezed by ( d + a ) / 2d\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster, splitClip, perturb ) )
			return 1;
		rig.Set( PT_VIEW, static_cast< float >( View::Drum ) );
		rig.Set( PT_STATIONS, 12.0f );
		if( !printAll( rig, 12 ) )
			return 1;
		//600 RPM at 60 fps with 12 stations: exactly two stations a frame,
		//so every column sees two whole passes of a slit, whatever its phase.
		rig.Set( PT_RPM, 1.0f );
		if( !rig.Render( 3 ) )
			return 1;
		const Picture p     = pictureOf( rig );
		const DrumGeometry& dg = rig.plugin.LastDrum();
		const Geometry& g   = rig.plugin.LastGeometry();
		const DrumRay ray { dg.eye, dg.focal };
		const double pitch  = kTwoPi / 12.0;
		//Where each figure's black-to-white centre line must be: G( x ) =
		//m pitch; and where it would be on a drum standing still, seen
		//directly: gammaFar( x ) = m pitch.
		double predicted[ 3 ], still[ 3 ];
		const double span = 0.8 * dg.focal / std::sqrt( dg.eye * dg.eye - 1.0 );
		for( int m = -1; m <= 1; ++m )
		{
			predicted[ m + 1 ] = ray.solve( [ & ]( double x ) { return ray.G( x ); }, m * pitch, -span, span );
			still[ m + 1 ] = ray.solve( [ & ]( double x ) { return wrapTo( ray.far( x ), kTwoPi ); }, m * pitch, -span, span );
		}
		//The rows across the band's middle half.
		const double bandPx = dg.bandHalf * dg.focal / ( dg.eye - 1.0 );
		double measured[ 3 ] = { 0.0, 0.0, 0.0 };
		int rows = 0, missing = 0;
		for( int j = 0; j < p.h; ++j )
		{
			const double y = j + 0.5 - g.centreY;
			if( std::fabs( y ) > 0.5 * bandPx )
				continue;
			++rows;
			for( int m = 0; m < 3; ++m )
			{
				const double x0 = g.centreX + predicted[ m ];
				//The levels either side, clear of the slit's blur
				//( pitch / 16 of turn, ~7 px at 1280 ).
				const double wide = 3.0 + 1.5 * dg.slitHalf * 2.0 / ( 2.0 * dg.eye / dg.focal );
				const double dark = p.sample( x0 - wide, j + 0.5 ), bright = p.sample( x0 + wide, j + 0.5 );
				const double half = 0.5 * ( dark + bright );
				double found = 1e9;
				for( int i = static_cast< int >( x0 - wide ); i < static_cast< int >( x0 + wide ); ++i )
				{
					const double a = p.sample( i + 0.5, j + 0.5 ), b = p.sample( i + 1.5, j + 0.5 );
					if( a < half && b >= half && bright - dark > 0.3 )
					{
						const double at = i + 0.5 + ( half - a ) / ( b - a );
						if( std::fabs( at - x0 ) < std::fabs( found - x0 ) )
							found = at;
					}
				}
				if( found > 1e8 )
					++missing;
				else
					measured[ m ] += found - g.centreX;
			}
		}
		for( double& m : measured )
			m /= std::max( rows, 1 );
		double worst = 0.0;
		for( int m = 0; m < 3; ++m )
			worst = std::max( worst, std::fabs( measured[ m ] - predicted[ m ] ) );
		//The edge is the average of 16 taps over each slit's pass, smoothed
		//by the texel's tent: symmetric about the line, so its middle is
		//the line's place up to the GPU's 8-bit filter weights (1/512 of
		//the ramp, ~0.02 px) and a straight-line read between two pixels
		//of a ramp that is not quite straight there.
		const double tolerance = 0.05;
		Check( rows > 4 && missing == 0 && worst <= tolerance,
		       fmt( "%dx%d, 12 stations at 600 RPM, eye %.1f radii out: the figures' centre lines at %.3f, %.3f, %.3f px from the middle, "
		            "the geometry's %.3f, %.3f, %.3f (worst %.4f px, tolerance %.2f; %d rows)",
		            raster.w, raster.h, dg.eye, measured[ 0 ], measured[ 1 ], measured[ 2 ], predicted[ 0 ], predicted[ 1 ], predicted[ 2 ],
		            worst, tolerance, rows ) );
		//The squeeze: figures' spacing against the still drum's. In the
		//closed form it is the slopes' ratio at the middle of the view,
		//d gammaFar / dx over d G / dx = ( d + a ) / ( d + a + d - a ); the
		//harness's own geometry gives that slope (a central difference over
		//a hundredth of a pixel) and the ratio across a station either
		//side, which the picture is held to.
		const double closed  = ( dg.eye + 1.0 ) / ( 2.0 * dg.eye );
		const double h       = 0.01;
		const double slope   = ( wrapTo( ray.far( h ), kTwoPi ) - wrapTo( ray.far( -h ), kTwoPi ) ) / ( ray.G( h ) - ray.G( -h ) );
		const double exact   = ( predicted[ 2 ] - predicted[ 0 ] ) / ( still[ 2 ] - still[ 0 ] );
		const double squeeze = ( measured[ 2 ] - measured[ 0 ] ) / ( still[ 2 ] - still[ 0 ] );
		const double slack   = 2.0 * tolerance / ( still[ 2 ] - still[ 0 ] );
		Check( std::fabs( squeeze - exact ) <= slack && std::fabs( slope - closed ) < 1e-6,
		       fmt( "%dx%d: the figures squeezed to %.5f of the still drum's width across a station either side, the geometry's %.5f "
		            "(tolerance %.5f, two edges' error); at the middle the geometry's slope is %.7f and ( d + a ) / 2d = %.7f",
		            raster.w, raster.h, squeeze, exact, slack, slope, closed ) );
	}
	return Verdict();
}

#include "Disc.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace zoetrope
{
namespace
{
constexpr double kTwoPi = 2.0 * kPi;
} // namespace

double Reduce( double radians )
{
	double r = std::fmod( radians, kTwoPi );
	if( r < 0.0 )
		r += kTwoPi;
	//fmod can land on 2 pi itself for an input a hair under a multiple.
	return r >= kTwoPi ? 0.0 : r;
}

double StopTime( double omega0, const Bearing& bearing )
{
	if( omega0 <= 0.0 )
		return 0.0;
	if( bearing.coulomb <= 0.0 )
		return std::numeric_limits< double >::infinity();
	if( bearing.viscous <= 0.0 )
		return omega0 / bearing.coulomb;
	return std::log1p( bearing.viscous * omega0 / bearing.coulomb ) / bearing.viscous;
}

Coast CoastFor( double omega0, double t, const Bearing& bearing )
{
	if( omega0 <= 0.0 || t <= 0.0 )
		return { 0.0, std::max( omega0, 0.0 ) };
	const double k = bearing.viscous, c = bearing.coulomb;
	const double stop = StopTime( omega0, bearing );
	const double s    = std::min( t, stop );
	if( k <= 0.0 )
	{
		//The limit k -> 0: constant deceleration c.
		const double w = omega0 - c * s;
		return { omega0 * s - 0.5 * c * s * s, t >= stop ? 0.0 : std::max( w, 0.0 ) };
	}
	//( 1 - e^-ks ) / k without cancellation for a small k s.
	const double decay  = -std::expm1( -k * s ) / k;
	const double floorW = c / k;
	const double advance = ( omega0 + floorW ) * decay - floorW * s;
	const double w       = t >= stop ? 0.0 : ( omega0 + floorW ) * std::exp( -k * s ) - floorW;
	return { advance, std::max( w, 0.0 ) };
}

Bearing Disc::effective( const Bearing& b ) const
{
	Bearing out = b;
	if( ( hooks & kHookNoCoulomb ) != 0 )
		out.coulomb = 0.0;
	return out;
}

void Disc::SetAngleForTest( double radians )
{
	angle = startAngle = Reduce( radians );
	omega              = 0.0;
	pieces.assign( 1, Piece { 0.0, 0.0, 0.0 } );
	frameLength = 0.0;
}

void Disc::Advance( double dt, Drive newDrive, double motorOmega, const std::vector< double >& pushes, double push,
                    const Bearing& newBearing )
{
	drive       = newDrive;
	bearing     = effective( newBearing );
	frameLength = std::max( dt, 0.0 );
	startAngle  = angle;
	pieces.clear();

	if( drive == Drive::Motor )
	{
		omega = std::max( motorOmega, 0.0 );
		pieces.push_back( { 0.0, 0.0, omega } );
	}
	else
	{
		//A piece from the frame's start, then a new one at every push.
		pieces.push_back( { 0.0, 0.0, omega } );
		for( double at : pushes )
		{
			const double s    = std::clamp( at, 0.0, frameLength );
			const Piece& last = pieces.back();
			const Coast coast = CoastFor( last.omega, s - last.start, bearing );
			pieces.push_back( { s, last.advance + coast.advance, coast.omega + push } );
		}
		omega = OmegaAt( frameLength );
	}
	angle = Reduce( startAngle + AdvanceAt( frameLength ) );
}

double Disc::AdvanceAt( double s ) const
{
	if( pieces.empty() )
		return 0.0;
	s = std::clamp( s, 0.0, frameLength );
	if( drive == Drive::Motor )
		return pieces.front().omega * s;
	//The last piece that has started by s. Two pushes at one moment make two
	//pieces at one start; the later (faster) one wins.
	size_t i = 0;
	while( i + 1 < pieces.size() && pieces[ i + 1 ].start <= s )
		++i;
	const Piece& p = pieces[ i ];
	return p.advance + CoastFor( p.omega, s - p.start, bearing ).advance;
}

double Disc::OmegaAt( double s ) const
{
	if( pieces.empty() )
		return 0.0;
	s = std::clamp( s, 0.0, frameLength );
	if( drive == Drive::Motor )
		return pieces.front().omega;
	size_t i = 0;
	while( i + 1 < pieces.size() && pieces[ i + 1 ].start <= s )
		++i;
	const Piece& p = pieces[ i ];
	return CoastFor( p.omega, s - p.start, bearing ).omega;
}

std::vector< Disc::Print > Disc::Crossings( int stations, double head ) const
{
	std::vector< Print > out;
	if( stations < 1 || frameLength <= 0.0 )
		return out;
	const double pitch = kTwoPi / stations;
	if( ( hooks & kHookHeadOneOn ) != 0 )
		head += pitch;
	const double total = AdvanceAt( frameLength );
	if( !( total > 0.0 ) )
		return out;

	//g( s ) = ( start + A( s ) - head ) / pitch, unreduced over the frame. The
	//start is reduced to [ 0, 2 pi ), so g stays small.
	const double g0 = ( startAngle - head ) / pitch;
	const double g1 = ( startAngle + total - head ) / pitch;
	long long first = static_cast< long long >( std::floor( g0 ) ) + 1;
	const long long last = static_cast< long long >( std::floor( g1 ) );
	if( last < first )
		return out;
	//A frame that turned more than a whole revolution prints every card; only
	//the last pass of each matters.
	first = std::max( first, last - stations + 1 );
	for( long long m = first; m <= last; ++m )
	{
		//The moment A( s ) = m pitch + head - start, by bisection: A is
		//continuous and never decreases.
		const double target = static_cast< double >( m ) * pitch + head - startAngle;
		double lo = 0.0, hi = frameLength;
		for( int i = 0; i < 100 && hi - lo > 1e-15 * std::max( 1.0, frameLength ); ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			if( AdvanceAt( mid ) < target )
				lo = mid;
			else
				hi = mid;
		}
		const long long n = stations;
		const int card    = static_cast< int >( ( ( -m ) % n + n ) % n );
		out.push_back( { card, hi } );
	}
	return out;
}

} // namespace zoetrope

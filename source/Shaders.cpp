#include "Shaders.h"

namespace zoetrope::shaders
{

const char* const kVersion = "#version 410 core\n";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// Shared by every pass.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi    = 3.14159265358979;
const float kTwoPi = 6.28318530717959;

//sRGB, both ways. The cards hold light, and light adds: every average here
//(a print's area average, a smear, an exposure) is taken in linear light.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}

//The arctangent of z in [ 0, 1 ]. Above tan 15 degrees it is 30 degrees
//plus the arctangent of ( z sqrt 3 - 1 ) / ( z + sqrt 3 ), which is back
//under tan 15; there the series to z^15 leaves out less than
//0.268^17 / 17 = 3e-11, far below a float.
float arcTanUnit( float z )
{
	float offset = 0.0;
	if( z > 0.26794919 )
	{
		z      = ( z * 1.7320508 - 1.0 ) / ( z + 1.7320508 );
		offset = 0.52359878;
	}
	float z2   = z * z;
	float term = z;
	float sum  = 0.0;
	for( int k = 0; k < 8; ++k )
	{
		sum += ( ( k & 1 ) == 0 ? term : -term ) / float( 2 * k + 1 );
		term *= z2;
	}
	return offset + sum;
}

//The angle of p clockwise from twelve o'clock (+y), in [ 0, 2 pi ).
float clockAngle( vec2 p )
{
	float ax = abs( p.x );
	float ay = abs( p.y );
	if( ax == 0.0 && ay == 0.0 )
		return 0.0;
	float a = ax <= ay ? arcTanUnit( ax / ay ) : 0.5 * kPi - arcTanUnit( ay / ax );
	if( p.y < 0.0 )
		a = kPi - a;
	if( p.x < 0.0 )
		a = kTwoPi - a;
	return a;
}

//sin and cos for |x| <= pi / 4 (a card is never more than half a station
//from its centre, and there are at least four), by their series in Horner
//form. The first terms left out at pi / 4 are 3e-12 and 4e-11.
float sinSeries( float x )
{
	float x2 = x * x;
	return x * ( 1.0 - x2 / 6.0 * ( 1.0 - x2 / 20.0 * ( 1.0 - x2 / 42.0 * ( 1.0 - x2 / 72.0 * ( 1.0 - x2 / 110.0 * ( 1.0 - x2 / 156.0 ) ) ) ) ) );
}
float cosSeries( float x )
{
	float x2 = x * x;
	return 1.0 - x2 / 2.0 * ( 1.0 - x2 / 12.0 * ( 1.0 - x2 / 30.0 * ( 1.0 - x2 / 56.0 * ( 1.0 - x2 / 90.0 * ( 1.0 - x2 / 132.0 ) ) ) ) );
}

//PCG, integer, the same everywhere.
uint pcgHash( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float hash01( ivec2 p )
{
	return float( pcgHash( uint( p.x ) * 1973u + pcgHash( uint( p.y ) ) ) ) * ( 1.0 / 4294967296.0 );
}
)";

//---------------------------------------------------------------------------
// 1. print: the clip into a card's layer.
//---------------------------------------------------------------------------
const char* const kPrintFragment = R"(
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform ivec2 Source;//the clip's raster
uniform ivec2 Target;//the layer's
uniform int Taps;    //per axis

in vec2 uv;
out vec4 fragColor;

void main()
{
	//The layer texel's footprint on the clip, averaged by Taps x Taps reads
	//at the centres of equal sub-boxes of it: a box average, in light.
	vec2 scale = vec2( Source ) / vec2( Target );
	vec2 base  = floor( gl_FragCoord.xy );
	vec3 sum   = vec3( 0.0 );
	for( int j = 0; j < Taps; ++j )
		for( int i = 0; i < Taps; ++i )
		{
			vec2 at = ( base + ( vec2( float( i ), float( j ) ) + 0.5 ) / float( Taps ) ) * scale / vec2( Source );
			sum += toLinear( texture( InputTexture, at * MaxUV ).rgb );
		}
	//Alpha 1: this card has been printed.
	fragColor = vec4( sum / float( Taps * Taps ), 1.0 );
}
)";

//---------------------------------------------------------------------------
// 2. carry: one layer of the old card array into the new one.
//---------------------------------------------------------------------------
const char* const kCarryFragment = R"(
uniform sampler2DArray Old;
uniform int Layer;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( Old, vec3( uv, float( Layer ) ) );
}
)";

//---------------------------------------------------------------------------
// 3. scene: what the camera integrated.
//---------------------------------------------------------------------------
const char* const kSceneFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2DArray Cards;
uniform sampler2D Previous;
uniform vec2 MaxUV;
uniform ivec2 Size;
uniform ivec2 ViewOrigin;

uniform vec2 Centre;      //the disc's centre, pixels from the frame's bottom-left corner
uniform float Ring;       //the card centres' radius, pixels
uniform vec2 CardHalf;    //half a card, across ( tangential ) and up ( radial ), pixels
uniform vec2 PrintHalf;   //half the print on it
uniform float BandInner;  //the radii a card can reach
uniform float BandOuter;
uniform float DiscRadius;
uniform float Pitch;      //2 pi / N
uniform int Stations;
uniform int StationOffset;//the reference angle's whole stations
uniform float PhaseFrac;  //and its fraction of one
uniform float Angles[ 17 ];//the disc's angle across the exposure, from the reference
uniform vec4 Segments[ 9 ];//( from, to ) as fractions of the exposure, weight
uniform int SegmentCount;
uniform float Head;       //the print head's angle
uniform float HeadSize;   //pixels
uniform int BackgroundMode;
uniform float MixAmount;
uniform int OutputMode;   //0: to the host. 1: into the afterimage, blended with Previous
uniform float Afterimage; //how much of Previous the afterimage keeps
uniform int Hooks;

//What the matte things give back of the light that falls on them, in
//linear light: the disc, the card stock round a print (and all of a card
//not yet printed), and the print head.
const float kDisc       = 0.006;
const float kStock      = 0.62;
const float kHeadAlbedo = 0.32;

in vec2 uv;
out vec4 fragColor;

vec4 clipAt( ivec2 pixel )
{
	vec2 at = ( vec2( pixel ) + 0.5 ) / vec2( Size );
	return texture( InputTexture, at * MaxUV );
}

//The disc's angle at fraction u of the exposure, past the reference.
float angleAt( float u )
{
	float x = clamp( u, 0.0, 1.0 ) * 16.0;
	int i   = min( int( x ), 15 );
	return mix( Angles[ i ], Angles[ i + 1 ], x - float( i ) );
}

//What a card gives back at a pixel ( r, a ) with the disc turned `turned`
//past the reference, less what the bare disc would have: the card's light
//(its print, or the stock round it) times its coverage of the pixel. A card
//not yet printed has alpha 0 in its layer and is bare stock.
vec3 cardAt( float r, float a, float turned )
{
	float x     = ( a - turned ) / Pitch - PhaseFrac;
	float kf    = floor( x + 0.5 );
	float delta = ( x - kf ) * Pitch;
	float t     = r * sinSeries( delta );
	float q     = r * cosSeries( delta ) - Ring;
	float cover = clamp( CardHalf.x - abs( t ) + 0.5, 0.0, 1.0 ) * clamp( CardHalf.y - abs( q ) + 0.5, 0.0, 1.0 );
	if( cover <= 0.0 )
		return vec3( 0.0 );
	int card     = ( int( kf ) - StationOffset + 64 * Stations ) % Stations;
	vec4 print_  = texture( Cards, vec3( 0.5 + 0.5 * t / PrintHalf.x, 0.5 + 0.5 * q / PrintHalf.y, float( card ) ) );
	float inside = clamp( PrintHalf.x - abs( t ) + 0.5, 0.0, 1.0 ) * clamp( PrintHalf.y - abs( q ) + 0.5, 0.0, 1.0 ) * print_.a;
	return cover * ( mix( vec3( kStock ), print_.rgb, inside ) - vec3( kDisc ) );
}

void main()
{
	ivec2 pixel   = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 original = clipAt( pixel );
	vec2 p        = vec2( pixel ) + 0.5 - Centre;
	float r       = length( p );
	float a       = clockAngle( p );

	//Everything the camera's exposure caught, whatever was turning.
	float light = 0.0;
	for( int s = 0; s < SegmentCount; ++s )
		light += Segments[ s ].z;

	vec3 lit = vec3( kDisc * light );
	if( r > BandInner && r < BandOuter )
	{
		for( int s = 0; s < SegmentCount; ++s )
		{
			vec4 seg  = Segments[ s ];
			float arc = abs( angleAt( seg.y ) - angleAt( seg.x ) ) * r;
			//A power of two, enough for a tap per pixel of the arc up to
			//kMaxTaps; past that a per-pixel offset turns the remaining
			//steps into grain rather than copies.
			int n = 1;
			while( n < 64 && float( n ) < arc )
				n *= 2;
			float offset = arc > 64.0 ? hash01( pixel ) : 0.5;
			vec3 sum     = vec3( 0.0 );
			for( int j = 0; j < n; ++j )
				sum += cardAt( r, a, angleAt( mix( seg.x, seg.y, ( float( j ) + offset ) / float( n ) ) ) );
			float count = ( Hooks & 1 ) != 0 ? float( n + 1 ) : float( n );
			lit += seg.z * sum / count;
		}
	}

	float discCover = clamp( DiscRadius - r + 0.5, 0.0, 1.0 );
	vec3 behind     = BackgroundMode == 1 ? toLinear( original.rgb ) : vec3( 0.0 );
	float behindA   = BackgroundMode == 1 ? original.a : 1.0;
	vec3 scene      = mix( behind, lit, discCover );
	float sceneA    = mix( behindA, 1.0, discCover );

	//The print head: a block just off the rim at its angle, standing still
	//in the same light.
	if( HeadSize > 0.0 )
	{
		float d = abs( a - Head );
		d       = min( d, kTwoPi - d ) * r;
		float s = r - DiscRadius - 0.25 * HeadSize;
		float c = clamp( 0.3 * HeadSize - d + 0.5, 0.0, 1.0 ) * clamp( 0.5 * HeadSize - abs( s - 0.5 * HeadSize ) + 0.5, 0.0, 1.0 );
		scene   = mix( scene, vec3( kHeadAlbedo * light ), c );
		sceneA  = mix( sceneA, 1.0, c );
	}

	if( OutputMode == 1 )
	{
		vec4 before = texelFetch( Previous, pixel, 0 );
		fragColor   = vec4( mix( scene, before.rgb, Afterimage ), mix( sceneA, before.a, Afterimage ) );
		return;
	}
	fragColor = mix( original, vec4( toCode( scene ), sceneA ), MixAmount );
}
)";

//---------------------------------------------------------------------------
// 4. show: the afterimage to the host.
//---------------------------------------------------------------------------
const char* const kShowFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2D Seen;
uniform vec2 MaxUV;
uniform ivec2 Size;
uniform ivec2 ViewOrigin;
uniform float MixAmount;

in vec2 uv;
out vec4 fragColor;

void main()
{
	ivec2 pixel   = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec2 at       = ( vec2( pixel ) + 0.5 ) / vec2( Size );
	vec4 original = texture( InputTexture, at * MaxUV );
	vec4 seen     = texelFetch( Seen, pixel, 0 );
	fragColor     = mix( original, vec4( toCode( seen.rgb ), seen.a ), MixAmount );
}
)";

std::string Assemble( Pass pass )
{
	std::string out = kVersion;
	out += kCommon;
	switch( pass )
	{
	case Pass::Print: out += kPrintFragment; break;
	case Pass::Carry: out += kCarryFragment; break;
	case Pass::Scene: out += kSceneFragment; break;
	case Pass::Show: out += kShowFragment; break;
	}
	return out;
}

} // namespace zoetrope::shaders

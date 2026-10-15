#include "Cards.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace zoetrope
{
namespace
{
constexpr int64_t kBlank = std::numeric_limits< int64_t >::min();

GLuint makeArray( int width, int height, int layers )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
	glTexImage3D( GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, width, height, layers, 0, GL_RGBA, GL_HALF_FLOAT, nullptr );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
	return texture;
}

/// Clear every layer of `texture` to transparent black through `fbo`: a blank
/// card (alpha 0 is how the scene pass knows it is blank).
void clearLayers( GLuint fbo, GLuint texture, int width, int height, int layers )
{
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	for( int layer = 0; layer < layers; ++layer )
	{
		glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0, layer );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
}
} // namespace

int LadderAtLeast( double pixels )
{
	int best = 2048;
	for( int k = 6; k <= 22; ++k )
	{
		const int size = static_cast< int >( std::lround( std::pow( 2.0, k / 2.0 ) ) );
		if( size >= pixels )
		{
			best = size;
			break;
		}
	}
	return std::clamp( best, 8, 2048 );
}

bool Cards::Ensure( int newWidth, int newHeight, int newLayers, GLuint copyProgram, ffglex::FFGLScreenQuad& quad )
{
	newWidth  = std::max( newWidth, 1 );
	newHeight = std::max( newHeight, 1 );
	newLayers = std::max( newLayers, 1 );
	if( texture != 0 && newWidth == width && newHeight == height && newLayers <= layers )
		return true;
	//Grow the depth, never shrink it: a disc taken from 48 stations to 4 and
	//back would otherwise reallocate twice for nothing.
	newLayers = std::max( newLayers, layers );

	const GLuint fresh = makeArray( newWidth, newHeight, newLayers );
	if( fresh == 0 )
		return false;
	if( fbo == 0 )
		glGenFramebuffers( 1, &fbo );

	GLint previousFBO = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFBO );
	GLint previousViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, previousViewport );
	GLfloat previousClear[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glGetFloatv( GL_COLOR_CLEAR_VALUE, previousClear );

	//Undefined texture memory is not "noise on the first frame", it is
	//whatever the driver handed back: every new layer starts blank.
	clearLayers( fbo, fresh, newWidth, newHeight, newLayers );

	std::vector< int64_t > freshFrames( static_cast< size_t >( newLayers ), kBlank );
	std::vector< double > freshWhens( static_cast< size_t >( newLayers ), 0.0 );

	//Carry every print across, resampled, to the same station.
	if( texture != 0 && copyProgram != 0 )
	{
		glUseProgram( copyProgram );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, texture );
		glUniform1i( glGetUniformLocation( copyProgram, "Old" ), 0 );
		const GLint layerUniform = glGetUniformLocation( copyProgram, "Layer" );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, newWidth, newHeight );
		for( int layer = 0; layer < layers; ++layer )
		{
			if( frames[ static_cast< size_t >( layer ) ] == kBlank )
				continue;
			glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fresh, 0, layer );
			glUniform1i( layerUniform, layer );
			quad.Draw();
			freshFrames[ static_cast< size_t >( layer ) ] = frames[ static_cast< size_t >( layer ) ];
			freshWhens[ static_cast< size_t >( layer ) ]  = whens[ static_cast< size_t >( layer ) ];
		}
		glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
		glBindTexture( GL_TEXTURE_2D_ARRAY, 0 );
		glUseProgram( 0 );
	}

	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFBO ) );
	glViewport( previousViewport[ 0 ], previousViewport[ 1 ], previousViewport[ 2 ], previousViewport[ 3 ] );
	glClearColor( previousClear[ 0 ], previousClear[ 1 ], previousClear[ 2 ], previousClear[ 3 ] );

	if( texture != 0 )
		glDeleteTextures( 1, &texture );
	texture = fresh;
	width   = newWidth;
	height  = newHeight;
	layers  = newLayers;
	frames  = std::move( freshFrames );
	whens   = std::move( freshWhens );
	return true;
}

void Cards::Begin( int card )
{
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0, std::clamp( card, 0, layers - 1 ) );
	glViewport( 0, 0, width, height );
}

void Cards::Unbind()
{
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void Cards::Tag( int card, int64_t frame, double when )
{
	if( card < 0 || card >= layers )
		return;
	frames[ static_cast< size_t >( card ) ] = frame;
	whens[ static_cast< size_t >( card ) ]  = when;
}

void Cards::Blank()
{
	if( texture == 0 )
		return;
	GLint previousFBO = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFBO );
	GLint previousViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, previousViewport );
	GLfloat previousClear[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glGetFloatv( GL_COLOR_CLEAR_VALUE, previousClear );
	clearLayers( fbo, texture, width, height, layers );
	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFBO ) );
	glViewport( previousViewport[ 0 ], previousViewport[ 1 ], previousViewport[ 2 ], previousViewport[ 3 ] );
	glClearColor( previousClear[ 0 ], previousClear[ 1 ], previousClear[ 2 ], previousClear[ 3 ] );
	std::fill( frames.begin(), frames.end(), kBlank );
	std::fill( whens.begin(), whens.end(), 0.0 );
}

bool Cards::Printed( int card ) const
{
	return card >= 0 && card < layers && frames[ static_cast< size_t >( card ) ] != kBlank;
}

int64_t Cards::FrameOf( int card ) const
{
	return Printed( card ) ? frames[ static_cast< size_t >( card ) ] : -1;
}

double Cards::WhenOf( int card ) const
{
	return Printed( card ) ? whens[ static_cast< size_t >( card ) ] : 0.0;
}

void Cards::Release()
{
	if( texture != 0 )
		glDeleteTextures( 1, &texture );
	if( fbo != 0 )
		glDeleteFramebuffers( 1, &fbo );
	texture = fbo = 0;
	width = height = layers = 0;
	frames.clear();
	whens.clear();
}

} // namespace zoetrope

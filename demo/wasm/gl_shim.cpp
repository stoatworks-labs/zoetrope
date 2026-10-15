/*
    The four GL entry points emscripten's WebGL2 library cannot take from the
    plugin as they are: glShaderSource, glEnable, glDisable and glIsEnabled.
    Every other GL call the plugin and the FFGL SDK make goes straight to
    emscripten's implementation, on the page's own WebGL2 context. polyhedral's
    shim, with Zoetrope's names.

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `zoetropeShaderSource` first
        REQUIRES the text to be byte for byte one of the page's own assemblies
        of the plugin's shaders (demo/shaders.js, which
        demo/tools/check_shaders.py holds to source/Shaders.cpp, and its
        ASSEMBLY table to Assemble() and InitGL), then applies the demo kit's
        `port()`: the version line and the ES precision defaults, nothing
        else. A shader the plugin assembled differently from the page's copy
        is refused (an empty source, so the plugin's InitGL fails and logs
        which pass), never compiled quietly.

    glEnable / glDisable / glIsEnabled
        GL_PROGRAM_POINT_SIZE is desktop-only state (in ES a point is always
        sized by the shader, and WebGL2 rejects the enum with INVALID_ENUM).
        The plugin's GLState.h saves and restores it on every frame, as a
        plugin inside Resolume must. Here it reads as off and enabling it is
        ignored; the plugin draws no points. Every other capability is passed
        through unchanged.

    Nothing else needed one. The textures the plugin renders into -- the
    cards (a GL_TEXTURE_2D_ARRAY of RGBA16F, one layer per station) and the
    afterimage's two RGBA16F buffers (the SDK's FFGLFBO) -- are single-level,
    allocated with formats and types WebGL2 accepts as they are, and
    rendering into them needs EXT_color_buffer_float, which the page asks for
    and stops without.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstring>
#include <string>

namespace
{
constexpr GLenum kProgramPointSize = 0x8642;///< GL_PROGRAM_POINT_SIZE, desktop GL 3.2

// clang-format off
EM_JS( void, ztShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'zoetropeShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
EM_JS( void, ztEnable, ( GLenum cap ), { GLctx.enable( cap ); } );
EM_JS( void, ztDisable, ( GLenum cap ), { GLctx.disable( cap ); } );
EM_JS( int, ztIsEnabled, ( GLenum cap ), { return GLctx.isEnabled( cap ) ? 1 : 0; } );
// clang-format on
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	ztShaderSource( shader, source.c_str() );
}

void glEnable( GLenum cap )
{
	if( cap != kProgramPointSize )
		ztEnable( cap );
}

void glDisable( GLenum cap )
{
	if( cap != kProgramPointSize )
		ztDisable( cap );
}

GLboolean glIsEnabled( GLenum cap )
{
	if( cap == kProgramPointSize )
		return GL_FALSE;
	return ztIsEnabled( cap ) ? GL_TRUE : GL_FALSE;
}
} // extern "C"

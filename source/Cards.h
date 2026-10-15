#pragma once

#include <FFGLSDK.h>

#include <cstdint>
#include <vector>

namespace zoetrope
{
/**
	The printed cards: one layer of a GL_TEXTURE_2D_ARRAY per station, in
	linear light (RGBA16F), and for each the moment it was printed.

	**A card is printed once per pass of the head** and holds that print until
	it comes round again; a card that has never passed the head is blank,
	which is the disc's own black.

	**A resize keeps the prints.** The layer size follows the card's size on
	screen (on a ladder of half-octaves, so a dragged Radius does not
	reallocate at every step), so a host raster change, a Radius or a Card
	Size reallocates -- and reallocation draws every layer into the new array,
	resampled, under the same station. A disc that lost its prints whenever
	the composition changed size would be the photofinish trap; `zttest
	--resize` checks it. A change of Stations is a new disc, and its cards are
	blank.
*/
class Cards
{
public:
	/// Make sure there are at least `layers` layers of `width` x `height`.
	/// `copyProgram` draws one layer of the old array (uniforms `Old` on unit
	/// 0 and `Layer`) across the viewport; `quad` is the screen quad.
	bool Ensure( int width, int height, int layers, GLuint copyProgram, ffglex::FFGLScreenQuad& quad );

	/// Bind the framebuffer to station `card`'s layer, viewport to it. Draw,
	/// then Unbind.
	void Begin( int card );
	void Unbind();

	/// Record a print: the host frame it was made on and its moment.
	void Tag( int card, int64_t frame, double when );
	/// Blank every card (a new disc), in GL as well as in the tags.
	void Blank();

	bool Printed( int card ) const;
	int64_t FrameOf( int card ) const;
	double WhenOf( int card ) const;

	GLuint Texture() const
	{
		return texture;
	}
	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	int Layers() const
	{
		return layers;
	}

	void Release();

private:
	GLuint texture = 0;
	GLuint fbo     = 0;
	int width      = 0;
	int height     = 0;
	int layers     = 0;
	std::vector< int64_t > frames;
	std::vector< double > whens;
};

/// The ladder the layers are sized on: 2^( k / 2 ), rounded, the smallest at
/// or above `pixels`, between 8 and 2048.
int LadderAtLeast( double pixels );

} // namespace zoetrope

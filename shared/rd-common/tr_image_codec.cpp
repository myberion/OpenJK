/*
===========================================================================
Copyright (C) 2026, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// tr_image_codec.cpp -- the image files the renderer reads and writes, and the only file that knows the libraries
// that do it: Wuffs (lib/wuffs) decodes every format the game loads (JPEG, PNG, TGA), stb_image_write (lib/stb)
// writes the screenshots and the video capture (JPEG, PNG). One file for the multiplayer and the single-player
// renderers, each compiling it with its own rd-common/tr_common.h. The rest of a renderer calls R_DecodeImage and
// the RE_Save functions of that header: replacing a library changes this file only
// (docs_rewrite/modernization/surveys/image-libraries.md).

// the libraries first: they include the standard headers they need before the engine's own
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__TARGA
#define WUFFS_CONFIG__MODULE__ZLIB
#include "wuffs-v0.4.c"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include "rd-common/tr_common.h"

/*
================================================================================
 Decoding
================================================================================
*/

// the most pixels an image may have: four bytes each must fit the int of the renderer's allocator
#define MAX_IMAGE_PIXELS 0x1FFFFFFF

// the deleter of memory the engine frees itself
static void KeepPixels( void * ) noexcept {}

namespace {

// Wuffs decodes straight into the engine's memory: RGBA, 8 bits per channel, rows from the top
class DecodeCallbacks : public wuffs_aux::DecodeImageCallbacks {
public:
	byte *pixels = nullptr;		// allocated here, owned by the caller of R_DecodeImage

private:
	wuffs_base__pixel_format SelectPixfmt( const wuffs_base__image_config & ) override {
		return wuffs_base__make_pixel_format( WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL );
	}

	AllocPixbufResult AllocPixbuf( const wuffs_base__image_config &config, bool allowUninitialized ) override {
		const uint64_t count = static_cast<uint64_t>( config.pixcfg.width() ) * config.pixcfg.height();

		if ( count == 0 || count > MAX_IMAGE_PIXELS ) {
			return AllocPixbufResult( std::string( "the image has no pixel or too many" ) );
		}
		pixels = static_cast<byte *>( R_ImageMalloc( static_cast<int>( count * 4 ), allowUninitialized ? qfalse : qtrue ) );

		wuffs_base__pixel_buffer pixbuf;
		const wuffs_base__status status = pixbuf.set_from_slice( &config.pixcfg,
			wuffs_base__make_slice_u8( pixels, static_cast<size_t>( count * 4 ) ) );
		if ( !status.is_ok() ) {
			return AllocPixbufResult( status.message() );
		}
		return AllocPixbufResult( wuffs_aux::MemOwner( pixels, &KeepPixels ), pixbuf );
	}
};

} // namespace

/*
=================
R_DecodeImage

Decodes an image file held in memory, of any format the game loads: the format is found from the file's content, not
from its name. Returns width * height * 4 bytes of RGBA from R_ImageMalloc, rows from the top, or NULL after a
warning.
=================
*/
byte *R_DecodeImage( const char *name, const byte *file, int length, int *width, int *height ) {
	DecodeCallbacks callbacks;
	wuffs_aux::sync_io::MemoryInput input( file, static_cast<size_t>( length ) );
	const wuffs_aux::DecodeImageResult result = wuffs_aux::DecodeImage( callbacks, input );

	if ( !result.error_message.empty() ) {
		if ( callbacks.pixels ) {
			R_ImageFree( callbacks.pixels );
		}
		ri.Printf( PRINT_WARNING, "R_DecodeImage: %s: %s\n", name, result.error_message.c_str() );
		return NULL;
	}
	*width = static_cast<int>( result.pixbuf.pixcfg.width() );
	*height = static_cast<int>( result.pixbuf.pixcfg.height() );
	return callbacks.pixels;
}

#ifdef JK2_MODE
// the screenshot kept in a saved game
void LoadJPGFromBuffer( byte *inputBuffer, size_t len, byte **pic, int *width, int *height ) {
	if ( !inputBuffer ) {
		return;
	}
	*pic = R_DecodeImage( "saved game screenshot", inputBuffer, static_cast<int>( len ), width, height );
}
#endif

/*
================================================================================
 Encoding
================================================================================
*/

typedef struct encodeBuffer_s {
	byte	*out;
	size_t	size, used;
} encodeBuffer_t;

static void EncodeToBuffer( void *context, void *data, int size ) {
	encodeBuffer_t *buffer = static_cast<encodeBuffer_t *>( context );

	if ( buffer->used + size > buffer->size ) {
		Com_Error( ERR_FATAL, "Output buffer for encoded JPEG image has insufficient size of %d bytes", static_cast<int>( buffer->size ) );
	}
	memcpy( buffer->out + buffer->used, data, size );
	buffer->used += size;
}

/*
=================
RE_SaveJPGToBuffer

Encodes JPEG from the RGB image in image_buffer and writes it to buffer; returns its size. The rows are from the
bottom, as the frame buffer's, unless flip_vertical; each is followed by padding bytes, which are removed in place
(image_buffer is changed).
=================
*/
size_t RE_SaveJPGToBuffer( byte *buffer, size_t bufSize, int quality, int image_width, int image_height, byte *image_buffer, int padding, bool flip_vertical ) {
	const int rowBytes = image_width * 3;
	encodeBuffer_t out = { buffer, bufSize, 0 };

	if ( padding ) {
		for ( int y = 1; y < image_height; y++ ) {
			memmove( image_buffer + y * rowBytes, image_buffer + y * ( rowBytes + padding ), rowBytes );
		}
	}
	stbi_flip_vertically_on_write( flip_vertical ? 0 : 1 );
	stbi_write_jpg_to_func( EncodeToBuffer, &out, image_width, image_height, 3, image_buffer, quality );
	return out.used;
}

void RE_SaveJPG( const char *filename, int quality, int image_width, int image_height, byte *image_buffer, int padding ) {
	byte *out;
	size_t bufSize;

	bufSize = image_width * image_height * 3;
	out = R_ImageMalloc( static_cast<int>( bufSize ), qfalse );

	bufSize = RE_SaveJPGToBuffer( out, bufSize, quality, image_width, image_height, image_buffer, padding, false );
	ri.FS_WriteFile( filename, out, bufSize );

	R_ImageFree( out );
}

static void WriteImageFile( void *context, void *data, int size ) {
	ri.FS_WriteFile( static_cast<const char *>( context ), data, size );
}

/*
=================
RE_SavePNG

Writes the image in buf (byteDepth bytes per pixel, rows from the bottom) as a PNG file; returns 0, or -1 on failure.
=================
*/
int RE_SavePNG( const char *filename, byte *buf, size_t width, size_t height, int byteDepth ) {
	stbi_flip_vertically_on_write( 1 );
	return stbi_write_png_to_func( WriteImageFile, const_cast<char *>( filename ), static_cast<int>( width ), static_cast<int>( height ),
		byteDepth, buf, static_cast<int>( width ) * byteDepth ) ? 0 : -1;
}

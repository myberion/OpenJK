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

// q_pk3.cpp -- reading the pk3 files, and the only file that knows the library that inflates them: zlib-ng
// (lib/zlib-ng). A pk3 is a ZIP archive: this file reads its directory, which is a list of fixed records at the end
// of the archive, and the files it names, stored or deflated. One file for the multiplayer and the single-player
// engines, each compiling it with its own qcommon/qcommon.h; their file systems (qcommon/files.cpp) call the
// functions of q_pk3.h: replacing the library changes this file only
// (docs_rewrite/modernization/surveys/compression-libraries.md).
//
// The archives come from anyone (a server sends them): no offset, size or count read in one is trusted before it is
// checked against the archive's length. ZIP64 archives (4 GB or more, or 65,535 files or more) are read. Not read:
// encrypted files, compression methods other than deflate, a file of 2 GB or more (sizes are ints in the file system).

// the library first: it includes the standard headers it needs before the engine's own
#include <zlib-ng.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "qcommon/q_pk3.h"

#define ZIP_END_SIGNATURE		0x06054b50u		// the record that ends the archive and locates the directory
#define ZIP_END_SIZE			22
#define ZIP_END_COMMENT			65535			// the longest comment that may follow it
#define ZIP_ENTRY_SIGNATURE		0x02014b50u		// one record of the directory
#define ZIP_ENTRY_SIZE			46
#define ZIP_LOCAL_SIGNATURE		0x04034b50u		// the header before a file's data
#define ZIP_LOCAL_SIZE			30
#define ZIP64_LOCATOR_SIGNATURE	0x07064b50u		// before the end record: where the ZIP64 end record is
#define ZIP64_LOCATOR_SIZE		20
#define ZIP64_END_SIGNATURE		0x06064b50u		// the end record with 64-bit fields
#define ZIP64_END_SIZE			56
#define ZIP64_EXTRA_ID			1				// the field of an entry's extra data that holds its 64-bit sizes
#define ZIP_ALL_ONES			0xffffffffu		// a 32-bit field whose value is in its ZIP64 form
#define ZIP_FLAG_ENCRYPTED		1
#define ZIP_STORED				0
#define ZIP_DEFLATED			8

#define PK3_INPUT_SIZE			65536			// the bytes of a deflated file read from the archive at a time
#define PK3_NOWHERE				UINT64_MAX		// the archive's read position, when it is not known
#define PK3_MAX_SIZE			0x7fffffffu		// sizes are ints in the file system

typedef struct pk3Slot_s {
	const char	*name;
	uint64_t	local;		// where the file's local header is in the archive
	uint32_t	csize;		// the file's size in the archive
	uint32_t	usize;		// the file's size once inflated
	uint32_t	crc;
	uint16_t	method;
	uint16_t	flags;
} pk3Slot_t;

struct pk3Archive_s {
	FILE		*file;
	uint64_t	length;
	uint64_t	at;			// where the next read of the file would be
	int			count;
	int			users;		// who opened it and the files open on it: it is closed when the last of them closes
	pk3Slot_t	*entries;	// count of them, then their names, in the archive's own allocation
	char		path[MAX_OSPATH];
};

struct pk3File_s {
	pk3Archive_t	*archive;
	const pk3Slot_t	*entry;
	uint64_t		data;		// where the file's data starts in the archive
	uint32_t		inLeft;		// bytes of a deflated file not read from the archive yet
	uint32_t		out;		// the position: the bytes delivered
	uint32_t		crc;		// of the bytes delivered
	qboolean		whole;		// every byte up to the position was delivered, in order: the CRC-32 can be checked
	qboolean		failed;
	zng_stream		z;
	byte			in[PK3_INPUT_SIZE];
};

static uint32_t PK3_Get16( const byte *p ) {
	return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 );
}

static uint32_t PK3_Get32( const byte *p ) {
	return PK3_Get16( p ) | ( PK3_Get16( p + 2 ) << 16 );
}

static uint64_t PK3_Get64( const byte *p ) {
	return (uint64_t)PK3_Get32( p ) | ( (uint64_t)PK3_Get32( p + 4 ) << 32 );
}

static qboolean PK3_FileLength( FILE *f, uint64_t *length ) {
#ifdef _WIN32
	const long long n = _fseeki64( f, 0, SEEK_END ) ? -1 : _ftelli64( f );
#else
	const off_t n = fseeko( f, 0, SEEK_END ) ? -1 : ftello( f );
#endif
	if ( n < 0 ) {
		return qfalse;
	}
	*length = (uint64_t)n;
	return qtrue;
}

// reads len bytes of the archive from the offset at; every file open on the archive reads through here, so the
// position is set whenever the last read did not end there
static qboolean PK3_ReadAt( pk3Archive_t *archive, uint64_t at, void *buffer, size_t len ) {
	if ( at > archive->length || len > archive->length - at ) {
		return qfalse;
	}
	if ( archive->at != at ) {
		archive->at = PK3_NOWHERE;
#ifdef _WIN32
		if ( _fseeki64( archive->file, (long long)at, SEEK_SET ) ) {
#else
		if ( fseeko( archive->file, (off_t)at, SEEK_SET ) ) {
#endif
			return qfalse;
		}
	}
	if ( fread( buffer, 1, len, archive->file ) != len ) {
		archive->at = PK3_NOWHERE;
		return qfalse;
	}
	archive->at = at + len;
	return qtrue;
}

/*
================================================================================
 The archive and its directory
================================================================================
*/

// The ZIP64 form of the end record, which an archive of 4 GB or more, or of 65,535 files or more, has: a locator
// just before the end record (at endAt) tells where it is.
static qboolean PK3_FindDirectory64( pk3Archive_t *archive, uint64_t endAt, uint64_t *at, uint64_t *size, uint64_t *count ) {
	byte	locator[ZIP64_LOCATOR_SIZE], end[ZIP64_END_SIZE];

	if ( endAt < ZIP64_LOCATOR_SIZE || !PK3_ReadAt( archive, endAt - ZIP64_LOCATOR_SIZE, locator, sizeof( locator ) ) ||
		PK3_Get32( locator ) != ZIP64_LOCATOR_SIGNATURE || !PK3_ReadAt( archive, PK3_Get64( locator + 8 ), end, sizeof( end ) ) ||
		PK3_Get32( end ) != ZIP64_END_SIGNATURE ) {
		return qfalse;
	}
	*count = PK3_Get64( end + 32 );
	*size = PK3_Get64( end + 40 );
	*at = PK3_Get64( end + 48 );
	return qtrue;
}

// where the directory is: the end record, found from the archive's last bytes. NULL, or why it was not found.
static const char *PK3_FindDirectory( pk3Archive_t *archive, uint64_t *at, uint64_t *size, uint64_t *count ) {
	const size_t	tailLen = archive->length < ZIP_END_SIZE + ZIP_END_COMMENT ? (size_t)archive->length : ZIP_END_SIZE + ZIP_END_COMMENT;
	const char		*why = "it has no ZIP directory";
	byte			*tail;

	if ( tailLen < ZIP_END_SIZE ) {
		return why;
	}
	tail = (byte *)Z_Malloc( (int)tailLen, TAG_FILESYS, qfalse );
	if ( !PK3_ReadAt( archive, archive->length - tailLen, tail, tailLen ) ) {
		why = "it cannot be read";
	} else {
		for ( size_t i = tailLen - ZIP_END_SIZE + 1; i-- > 0; ) {
			const byte *end = tail + i;

			if ( PK3_Get32( end ) != ZIP_END_SIGNATURE ) {
				continue;
			}
			*count = PK3_Get16( end + 10 );
			*size = PK3_Get32( end + 12 );
			*at = PK3_Get32( end + 16 );
			if ( ( *count == 0xffff || *size == ZIP_ALL_ONES || *at == ZIP_ALL_ONES ) &&
				!PK3_FindDirectory64( archive, archive->length - tailLen + i, at, size, count ) ) {
				why = "its ZIP64 directory is damaged";
			} else if ( *size > PK3_MAX_SIZE || *at > archive->length || *size > archive->length - *at || *count > *size / ZIP_ENTRY_SIZE ) {
				why = "its directory is not valid";		// outside the file, too large to hold, or too small for its entries
			} else {
				why = NULL;
			}
			break;
		}
	}
	Z_Free( tail );
	return why;
}

// One 64-bit value of an entry's ZIP64 field, when there is one left to take.
static qboolean PK3_Take64( const byte **p, uint32_t *left, uint64_t *value ) {
	if ( *left < 8 ) {
		return qfalse;
	}
	*value = PK3_Get64( *p );
	*p += 8;
	*left -= 8;
	return qtrue;
}

// The sizes and the place of a file that do not fit 32 bits are in a field of the entry's extra data, in this order,
// each of them there only when its 32-bit form is all ones. A size no int can hold stays all ones: PK3_OpenFile
// refuses the file.
static void PK3_Zip64Sizes( const byte *extra, uint32_t extraLen, pk3Slot_t *e ) {
	while ( extraLen >= 4 ) {
		const uint32_t	id = PK3_Get16( extra ), len = PK3_Get16( extra + 2 );
		const byte		*p = extra + 4;
		uint32_t		left = len;
		uint64_t		value;

		if ( len > extraLen - 4 ) {
			return;
		}
		if ( id == ZIP64_EXTRA_ID ) {
			if ( e->usize == ZIP_ALL_ONES ) {
				if ( !PK3_Take64( &p, &left, &value ) ) {
					return;
				}
				e->usize = value > PK3_MAX_SIZE ? ZIP_ALL_ONES : (uint32_t)value;
			}
			if ( e->csize == ZIP_ALL_ONES ) {
				if ( !PK3_Take64( &p, &left, &value ) ) {
					return;
				}
				e->csize = value > PK3_MAX_SIZE ? ZIP_ALL_ONES : (uint32_t)value;
			}
			if ( e->local == ZIP_ALL_ONES ) {
				PK3_Take64( &p, &left, &e->local );
			}
			return;
		}
		extra += 4 + len;
		extraLen -= 4 + len;
	}
}

// the archive's entries, from its directory held in memory (cd, size bytes): the first pass (entries NULL) checks
// every record and returns the bytes their names need, the second fills the entries and the names. -1 on a
// directory that is not what its records say.
static int64_t PK3_ParseDirectory( const byte *cd, uint32_t size, int count, pk3Slot_t *entries, char *names ) {
	uint32_t	at = 0;
	int64_t		namesLen = 0;

	for ( int i = 0; i < count; i++ ) {
		const byte	*record = cd + at;
		uint32_t	nameLen, recordLen;

		if ( size - at < ZIP_ENTRY_SIZE || PK3_Get32( record ) != ZIP_ENTRY_SIGNATURE ) {
			return -1;
		}
		nameLen = PK3_Get16( record + 28 );
		recordLen = ZIP_ENTRY_SIZE + nameLen + PK3_Get16( record + 30 ) + PK3_Get16( record + 32 );
		if ( size - at < recordLen ) {
			return -1;
		}
		if ( entries ) {
			pk3Slot_t *e = &entries[i];

			e->flags = (uint16_t)PK3_Get16( record + 8 );
			e->method = (uint16_t)PK3_Get16( record + 10 );
			e->crc = PK3_Get32( record + 16 );
			e->csize = PK3_Get32( record + 20 );
			e->usize = PK3_Get32( record + 24 );
			e->local = PK3_Get32( record + 42 );
			if ( e->csize == ZIP_ALL_ONES || e->usize == ZIP_ALL_ONES || e->local == ZIP_ALL_ONES ) {
				PK3_Zip64Sizes( record + ZIP_ENTRY_SIZE + nameLen, PK3_Get16( record + 30 ), e );
			}
			e->name = names + namesLen;
			memcpy( names + namesLen, record + ZIP_ENTRY_SIZE, nameLen );
			names[namesLen + nameLen] = 0;
		}
		namesLen += nameLen + 1;
		at += recordLen;
	}
	return namesLen;
}

// the archive with its directory read. NULL, with the reason in *why.
static pk3Archive_t *PK3_Load( pk3Archive_t *opened, const char **why ) {
	pk3Archive_t	*archive;
	uint64_t		at = 0, size = 0, count = 0, bytes = 0;
	int64_t			namesLen = 0;
	byte			*cd = NULL;

	*why = PK3_FindDirectory( opened, &at, &size, &count );		// size fits an int, count is at most size / 46
	if ( *why ) {
		return NULL;
	}
	if ( size ) {
		cd = (byte *)Z_Malloc( (int)size, TAG_FILESYS, qfalse );
		if ( !PK3_ReadAt( opened, at, cd, (size_t)size ) ) {
			namesLen = -1;
		}
	}
	if ( namesLen == 0 ) {
		namesLen = PK3_ParseDirectory( cd, (uint32_t)size, (int)count, NULL, NULL );
	}
	if ( namesLen >= 0 ) {
		bytes = sizeof( *archive ) + count * sizeof( pk3Slot_t ) + (uint64_t)namesLen;
	}
	if ( namesLen < 0 || bytes > PK3_MAX_SIZE ) {
		if ( cd ) {
			Z_Free( cd );
		}
		*why = namesLen < 0 ? "its directory is damaged" : "its directory is too large";
		return NULL;
	}

	archive = (pk3Archive_t *)Z_Malloc( (int)bytes, TAG_FILESYS, qtrue );
	*archive = *opened;
	archive->count = (int)count;
	archive->users = 1;
	archive->entries = (pk3Slot_t *)( archive + 1 );
	PK3_ParseDirectory( cd, (uint32_t)size, (int)count, archive->entries, (char *)( archive->entries + count ) );
	if ( cd ) {
		Z_Free( cd );
	}
	return archive;
}

pk3Archive_t *PK3_Open( const char *ospath ) {
	pk3Archive_t	opened;
	pk3Archive_t	*archive = NULL;
	const char		*why = "it cannot be opened";

	memset( &opened, 0, sizeof( opened ) );
	Q_strncpyz( opened.path, ospath, sizeof( opened.path ) );
	opened.at = PK3_NOWHERE;
	opened.file = fopen( ospath, "rb" );
	if ( opened.file ) {
		if ( PK3_FileLength( opened.file, &opened.length ) ) {
			archive = PK3_Load( &opened, &why );
		}
		if ( !archive ) {
			fclose( opened.file );
		}
	}
	if ( !archive ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s is not read: %s\n", ospath, why );
	}
	return archive;
}

void PK3_Close( pk3Archive_t *archive ) {
	if ( --archive->users > 0 ) {
		return;		// a file is still open on it
	}
	fclose( archive->file );
	Z_Free( archive );
}

int PK3_NumEntries( const pk3Archive_t *archive ) {
	return archive->count;
}

void PK3_GetEntry( const pk3Archive_t *archive, int index, pk3Entry_t *entry ) {
	const pk3Slot_t *e = &archive->entries[index];

	entry->name = e->name;
	entry->crc = e->crc;
	entry->size = e->usize;
}

/*
================================================================================
 A file of an archive
================================================================================
*/

pk3File_t *PK3_OpenFile( pk3Archive_t *archive, int index ) {
	const pk3Slot_t	*e;
	pk3File_t		*file;
	const char		*why = NULL;
	byte			local[ZIP_LOCAL_SIZE];
	uint64_t		data = 0;

	if ( index < 0 || index >= archive->count ) {
		return NULL;
	}
	e = &archive->entries[index];
	if ( e->flags & ZIP_FLAG_ENCRYPTED ) {
		why = "it is encrypted";
	} else if ( e->method != ZIP_STORED && e->method != ZIP_DEFLATED ) {
		why = "its compression method is not deflate";
	} else if ( e->csize > PK3_MAX_SIZE || e->usize > PK3_MAX_SIZE || ( e->method == ZIP_STORED && e->csize != e->usize ) ) {
		why = "its sizes are not valid";
	} else if ( !PK3_ReadAt( archive, e->local, local, sizeof( local ) ) || PK3_Get32( local ) != ZIP_LOCAL_SIGNATURE ) {
		why = "its header is damaged";
	} else {
		data = e->local + ZIP_LOCAL_SIZE + PK3_Get16( local + 26 ) + PK3_Get16( local + 28 );
		if ( data > archive->length || e->csize > archive->length - data ) {
			why = "its data is outside the archive";
		}
	}
	if ( why ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: %s in %s is not read: %s\n", e->name, archive->path, why );
		return NULL;
	}

	file = (pk3File_t *)Z_Malloc( sizeof( *file ), TAG_FILESYS, qfalse );
	memset( file, 0, offsetof( pk3File_t, in ) );
	file->archive = archive;
	file->entry = e;
	file->data = data;
	file->inLeft = e->csize;
	file->whole = qtrue;
	if ( e->method == ZIP_DEFLATED && zng_inflateInit2( &file->z, -15 ) != Z_OK ) {	// -15: deflate with no header
		Z_Free( file );
		return NULL;
	}
	archive->users++;
	return file;
}

void PK3_CloseFile( pk3File_t *file ) {
	if ( file->entry->method == ZIP_DEFLATED ) {
		zng_inflateEnd( &file->z );
	}
	PK3_Close( file->archive );
	Z_Free( file );
}

// inflates into buffer until it holds want bytes, reading the archive as the decoder runs out of input
static qboolean PK3_Inflate( pk3File_t *file, void *buffer, uint32_t want ) {
	const pk3Slot_t	*e = file->entry;
	zng_stream		*z = &file->z;

	z->next_out = (uint8_t *)buffer;
	z->avail_out = want;
	while ( z->avail_out ) {
		int result;

		if ( !z->avail_in && file->inLeft ) {
			const uint32_t n = file->inLeft < PK3_INPUT_SIZE ? file->inLeft : PK3_INPUT_SIZE;

			if ( !PK3_ReadAt( file->archive, file->data + ( e->csize - file->inLeft ), file->in, n ) ) {
				return qfalse;
			}
			file->inLeft -= n;
			z->next_in = file->in;
			z->avail_in = n;
		}
		// the decoder is told when this call ends the file (all of its input is here and all of its rest is asked
		// for): it then keeps no window of what it wrote, which a file read whole in one call never needs
		result = zng_inflate( z, ( !file->inLeft && want == e->usize - file->out ) ? Z_FINISH : Z_SYNC_FLUSH );
		if ( result == Z_STREAM_END ) {
			break;
		}
		if ( result != Z_OK ) {		// damaged data, or input that ends before the file does
			return qfalse;
		}
	}
	return (qboolean)( z->avail_out == 0 );		// the data ended before the size the directory gives
}

int PK3_Read( pk3File_t *file, void *buffer, int len ) {
	const pk3Slot_t	*e;
	uint32_t		want;

	if ( !file || file->failed || len < 0 ) {
		return -1;
	}
	e = file->entry;
	want = e->usize - file->out;
	if ( (uint32_t)len < want ) {
		want = (uint32_t)len;
	}
	if ( !want ) {
		return 0;
	}

	if ( e->method == ZIP_STORED ? !PK3_ReadAt( file->archive, file->data + file->out, buffer, want ) : !PK3_Inflate( file, buffer, want ) ) {
		file->failed = qtrue;
		return -1;
	}
	file->out += want;
	if ( file->whole ) {
		file->crc = zng_crc32( file->crc, (const uint8_t *)buffer, want );
		if ( file->out == e->usize && file->crc != e->crc ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s in %s is damaged: its CRC-32 is wrong\n", e->name, file->archive->path );
			file->failed = qtrue;
			return -1;
		}
	}
	return (int)want;
}

int PK3_Tell( const pk3File_t *file ) {
	return (int)file->out;
}

// A deflated file can only be inflated from its start: a seek backwards starts it again, a seek forwards inflates
// and drops what it skips. A stored file is read where it is asked.
int PK3_Seek( pk3File_t *file, long offset, int origin ) {
	const pk3Slot_t	*e = file->entry;
	int64_t			target;
	byte			skipped[PK3_INPUT_SIZE];

	switch ( origin ) {
		case FS_SEEK_SET:
			target = offset;
			break;
		case FS_SEEK_CUR:
			target = (int64_t)file->out + offset;
			break;
		case FS_SEEK_END:
			target = (int64_t)e->usize + offset;
			break;
		default:
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
			return -1;
	}
	if ( target < 0 ) {
		target = 0;
	} else if ( target > e->usize ) {
		target = e->usize;
	}
	if ( target == file->out ) {
		return offset;
	}

	if ( e->method == ZIP_STORED ) {
		file->out = (uint32_t)target;
		file->crc = 0;
		file->whole = (qboolean)( target == 0 );
		return offset;
	}
	if ( target < file->out ) {
		zng_inflateReset( &file->z );
		file->z.avail_in = 0;
		file->inLeft = e->csize;
		file->out = 0;
		file->crc = 0;
		file->failed = qfalse;
	}
	while ( file->out < target ) {
		const int n = target - file->out < (int64_t)sizeof( skipped ) ? (int)( target - file->out ) : (int)sizeof( skipped );

		if ( PK3_Read( file, skipped, n ) != n ) {
			break;
		}
	}
	return offset;
}

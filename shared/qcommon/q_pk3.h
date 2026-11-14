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

#pragma once

// q_pk3.h -- the pk3 files: ZIP archives whose files are stored or deflated. The file systems of the multiplayer
// and the single-player engines (qcommon/files.cpp) read their archives through these functions; q_pk3.cpp is the
// only file that knows the library that inflates them.

typedef struct pk3Archive_s pk3Archive_t;	// an archive open for reading, with its directory
typedef struct pk3File_s pk3File_t;		// one file of an archive, being read

typedef struct pk3Entry_s {
	const char		*name;		// as the archive stores it; valid until the archive is closed
	unsigned int	crc;		// the CRC-32 of the file's data
	unsigned int	size;		// of the file's data once inflated
} pk3Entry_t;

// NULL, with a warning, when the file is not an archive these functions read. A closed archive stays open for the
// files still open on it (the music and the videos are read across a restart of the file system) and goes with
// the last of them.
pk3Archive_t	*PK3_Open( const char *ospath );
void			PK3_Close( pk3Archive_t *archive );

// the entries of the directory, in its order, folders included
int				PK3_NumEntries( const pk3Archive_t *archive );
void			PK3_GetEntry( const pk3Archive_t *archive, int index, pk3Entry_t *entry );

// Any number of files of one archive may be open at once. A file is inflated as it is read; the CRC-32 of a file
// read from its start to its end is checked, and the read that ends it fails when it is wrong.
pk3File_t		*PK3_OpenFile( pk3Archive_t *archive, int index );		// NULL when the entry cannot be read
void			PK3_CloseFile( pk3File_t *file );
int				PK3_Read( pk3File_t *file, void *buffer, int len );		// the bytes delivered, fewer than len at the file's end; -1 on an error
int				PK3_Tell( const pk3File_t *file );
int				PK3_Seek( pk3File_t *file, long offset, int origin );	// origin: a fsOrigin_t; returns offset, as FS_Seek does

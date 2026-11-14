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

// files_engine.h -- what the file system (shared/qcommon/files.cpp, one file for the multiplayer and the
// single-player engines) takes from the engine it is built in: here the multiplayer one. A difference between the
// two engines goes in this file and in code/qcommon/files_engine.h, not in the shared one.

#define FS_DEFAULT_CFG			"mpdefault.cfg"		// the configuration a game folder must hold
#define FS_AUTOEXEC_CFG			"autoexec.cfg"		// read from outside the pk3 files only, as Q3CONFIG_CFG is
#define FS_MODULES_FROM_PAKS	qtrue				// a module found in a pk3 is written to a temporary file and loaded

// before a file is read whole, and before a saved game is renamed: nothing to do in this engine
static inline void FS_QuietSound( void ) {}

// the memory of a file read whole has no label in this engine
static inline void FS_LabelFile( const void *, const char * ) {}

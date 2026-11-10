/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

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

#include "client.h"

// TTimo: unused, commenting out to make gcc happy
#if 1
/*
==============
CL_Netchan_Encode

Removed with the client packets coded by netcodec (plan step 8, owner 9 November 2026): the client-to-server payload was
XORed with a key made of the challenge, the serverId, the acknowledge and the last command texts, read in clear at
the start of the packet, which the codec now codes. Whether to restore an equivalent is decided in plan step 12.
==============
*/

/*
==============
CL_Netchan_Decode

Removed with the message codec (plan step 7, owner 8 November 2026): the server's payload was XORed with a key
chosen from the reliable acknowledge read in clear at the start of the message, which the codec now codes (see
SV_Netchan_Encode). Whether to restore an equivalent is decided in plan step 12.
==============
*/
#endif

/*
=================
CL_Netchan_TransmitNextFragment
=================
*/
void CL_Netchan_TransmitNextFragment( netchan_t *chan ) {
	Netchan_TransmitNextFragment( chan );
}

//byte chksum[65536];

/*
===============
CL_Netchan_Transmit
================
*/
void CL_Netchan_Transmit( netchan_t *chan, msg_t* msg ) {
	// the message codec's bytes, as they are: no clc_EOF and no XOR (see CL_Netchan_Encode above)
	Netchan_Transmit( chan, msg->cursize, msg->data );
}

extern 	int oldsize;
int newsize = 0;

/*
=================
CL_Netchan_Process
=================
*/
qboolean CL_Netchan_Process( netchan_t *chan, msg_t *msg ) {
	int ret;
//	int i;
//	static		int newsize = 0;

	ret = Netchan_Process( chan, msg );
	if (!ret)
		return qfalse;
//	Huff_Decompress( msg, CL_DECODE_START );
//	for(i=CL_DECODE_START+msg->readcount;i<msg->cursize;i++) {
//		if (msg->data[i] != chksum[i-(CL_DECODE_START+msg->readcount)]) {
//			Com_Error(ERR_DROP,"bad %d v %d\n", msg->data[i], chksum[i-(CL_DECODE_START+msg->readcount)]);
//		}
//	}
	newsize += msg->cursize;
//	Com_Printf("saved %d to %d (%d%%)\n", (oldsize>>3), newsize, 100-(newsize*100/(oldsize>>3)));
	return qtrue;
}

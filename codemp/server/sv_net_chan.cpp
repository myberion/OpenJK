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

#include "server.h"

// TTimo: unused, commenting out to make gcc happy
#if 1
/*
==============
SV_Netchan_Encode

Removed with the message codec (plan step 7, owner 8 November 2026): the server-to-client payload was XORed with a
key made of the challenge, the sequence and the last command texts, chosen on the client from the reliable
acknowledge read in clear at the start of the message, which the codec now codes. Whether to restore an equivalent
is decided in plan step 12.
==============
*/

/*
==============
SV_Netchan_Decode

Removed with the client packets coded by netcodec (plan step 8, owner 9 November 2026): the client-to-server payload was
XORed with a key made of the challenge, the serverId, the acknowledge and the last command texts, read in clear at
the start of the packet, which the codec now codes. Whether to restore an equivalent is decided in plan step 12.
==============
*/
#endif

/*
=================
SV_Netchan_TransmitNextFragment
=================
*/
void SV_Netchan_TransmitNextFragment( netchan_t *chan ) {
	Netchan_TransmitNextFragment( chan );
}


/*
===============
SV_Netchan_Transmit
================
*/

void SV_Netchan_Transmit( client_t *client, msg_t *msg) {	//int length, const byte *data ) {
//	int i;
	// the message codec's bytes, as they are: no svc_EOF and no XOR (see SV_Netchan_Encode above)
	Netchan_Transmit( &client->netchan, msg->cursize, msg->data );
}

/*
=================
Netchan_SV_Process
=================
*/
qboolean SV_Netchan_Process( client_t *client, msg_t *msg ) {
	int ret;
//	int i;
	ret = Netchan_Process( &client->netchan, msg );
	if (!ret)
		return qfalse;
	// the message codec's bytes, as they are: no XOR (see SV_Netchan_Decode above)
	return qtrue;
}


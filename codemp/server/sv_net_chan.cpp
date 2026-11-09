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
is decided in plan step 12. The client-to-server direction keeps it (SV_Netchan_Decode).
==============
*/

/*
==============
SV_Netchan_Decode

	// first 12 bytes of the data are always:
	long serverId;
	long messageAcknowledge;
	long reliableAcknowledge;

==============
*/
static void SV_Netchan_Decode( client_t *client, msg_t *msg ) {
	int serverId, messageAcknowledge, reliableAcknowledge;
	int i, index, srdc, sbit;
	qboolean soob;
	byte key, *string;

        srdc = msg->readcount;
        sbit = msg->bit;
        soob = msg->oob;

        msg->oob = qfalse;

        serverId = MSG_ReadLong(msg);
	messageAcknowledge = MSG_ReadLong(msg);
	reliableAcknowledge = MSG_ReadLong(msg);

        msg->oob = soob;
        msg->bit = sbit;
        msg->readcount = srdc;

	string = (byte *)client->reliableCommands[ reliableAcknowledge & (MAX_RELIABLE_COMMANDS-1) ];
	index = 0;
	//
	key = client->challenge ^ serverId ^ messageAcknowledge;
	for (i = msg->readcount + SV_DECODE_START; i < msg->cursize; i++) {
		// modify the key with the last sent and acknowledged server command
		if (!string[index])
			index = 0;
		if (/*string[index] > 127 || */	// eurofix: remove this so we can chat in european languages...	-ste
			string[index] == '%')
		{
			key ^= '.' << (i & 1);
		}
		else {
			key ^= string[index] << (i & 1);
		}
		index++;
		// decode the data with this key
		*(msg->data + i) = *(msg->data + i) ^ key;
	}
}
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
	SV_Netchan_Decode( client, msg );
//	Huff_Decompress( msg, SV_DECODE_START );
//	for(i=SV_DECODE_START+msg->readcount;i<msg->cursize;i++) {
//		if (msg->data[i] != chksum[i-(SV_DECODE_START+msg->readcount)]) {
//			Com_Error(ERR_DROP,"bad\n");
//		}
//	}
	return qtrue;
}


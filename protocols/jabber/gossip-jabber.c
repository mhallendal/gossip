/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004 Imendio HB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "gossip-jabber.h"

const gchar * 
gossip_jabber_presence_type_to_string (GossipPresence *presence)
{
	switch (gossip_presence_get_type (presence)) {
	case GOSSIP_PRESENCE_TYPE_BUSY:
		return "dnd";
	case GOSSIP_PRESENCE_TYPE_AWAY:
		return "away";
	case GOSSIP_PRESENCE_TYPE_EXT_AWAY:
		return "xa";
	default:
		return NULL;
	}

	return NULL;
}



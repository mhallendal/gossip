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
#include "gossip-presence.h"

struct _GossipPresence {
	GossipPresenceState  state;
	GossipPresenceType   type;
	gchar               *status;

	gint                 priority;

	guint                ref_count;
};

GossipPresence *
gossip_presence_new ()
{
	GossipPresence *presence;

	presence = g_new0 (GossipPresence, 1);
	presence->ref_count = 1;
	presence->state = GOSSIP_PRESENCE_STATE_OFFLINE;
	presence->status = NULL;

	return presence;
}

GossipPresenceState 
gossip_presence_get_state (GossipPresence *presence)
{
	g_return_val_if_fail (presence != NULL, GOSSIP_PRESENCE_STATE_OFFLINE);
	
	return presence->state;
}

void
gossip_presence_set_state (GossipPresence *presence, GossipPresenceState state)
{
	g_return_if_fail (presence != NULL);

	presence->state = state;
}

GossipPresenceType
gossip_presence_get_type (GossipPresence *presence)
{
	g_return_val_if_fail (presence != NULL, 
			      GOSSIP_PRESENCE_TYPE_AVAILABLE);

	return presence->type;
}

void
gossip_presence_set_type (GossipPresence *presence, GossipPresenceType type)
{
	g_return_if_fail (presence != NULL);

	presence->type = type;
}

const gchar *
gossip_presence_get_status (GossipPresence *presence)
{
	g_return_val_if_fail (presence != NULL, NULL);

	if (presence->status) {
		return presence->status;
	}

	return "";
}

void
gossip_presence_set_status (GossipPresence *presence, const gchar *status)
{
	g_return_if_fail (presence != NULL);

	g_free (presence->status);
	if (status) {
		presence->status = g_strdup (status);
	} else {
		presence->status = NULL;
	}
}

gint
gossip_presence_get_priority (GossipPresence *presence)
{
	g_return_val_if_fail (presence != NULL, 0);

	return presence->priority;
}

void
gossip_presence_set_priority (GossipPresence *presence, gint priority)
{
	g_return_if_fail (presence != NULL);

	presence->priority = priority;
}

GossipPresence *
gossip_presence_ref (GossipPresence *presence)
{
	g_return_val_if_fail (presence != NULL, NULL);

	presence->ref_count++;

	return presence;
}

void
gossip_presence_unref (GossipPresence *presence)
{
	g_return_if_fail (presence != NULL);

	presence->ref_count--;
	
	if (presence->ref_count > 0) {
		return;
	}

	g_free (presence->status);
	g_free (presence);
}

GossipPresence *
gossip_presence_new_available (void)
{
	GossipPresence *presence;

	presence = gossip_presence_new ();
	gossip_presence_set_state (presence, GOSSIP_PRESENCE_STATE_ONLINE);
	gossip_presence_set_type (presence, GOSSIP_PRESENCE_TYPE_AVAILABLE);

	return presence;
}




/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004-2007 Imendio AB
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
 * 
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#ifndef __GOSSIP_JABBER_UTILS_H__
#define __GOSSIP_JABBER_UTILS_H__

#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

/* utils */
const gchar *       gossip_jabber_presence_state_to_str    (GossipPresence  *presence);
GossipPresenceState gossip_jabber_presence_state_from_str  (const gchar     *str);
GossipTime          gossip_jabber_get_message_timestamp    (LmMessage       *m);
GossipChatroomInvite *
		    gossip_jabber_get_message_conference   (GossipJabber    *jabber,
							    LmMessage       *m);
gboolean            gossip_jabber_get_message_is_event     (LmMessage       *m);
gboolean            gossip_jabber_get_message_is_composing (LmMessage       *m);
gchar *             gossip_jabber_get_name_to_use          (const gchar     *jid_str,
							    const gchar     *nickname,
							    const gchar     *full_name);

G_END_DECLS

#endif /* __GOSSIP_JABBER_UTILS_H__ */
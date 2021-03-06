/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2005 Imendio AB
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

#ifndef __GOSSIP_JABBER_PRIVATE_H__
#define __GOSSIP_JABBER_PRIVATE_H__

#include <loudmouth/loudmouth.h>

#include "gossip-session.h"
#include "gossip-jabber.h"
#include "gossip-jabber-ft.h"

G_BEGIN_DECLS

LmConnection *   _gossip_jabber_new_connection (GossipJabber  *jabber,
                                                GossipAccount *account);
gboolean         _gossip_jabber_set_connection (LmConnection  *connection,
                                                GossipJabber  *jabber,
                                                GossipAccount *account);
LmConnection *   _gossip_jabber_get_connection (GossipJabber  *jabber);
GossipSession *  _gossip_jabber_get_session    (GossipJabber  *jabber);
GossipJabberFTs *_gossip_jabber_get_fts        (GossipJabber  *jabber);

G_END_DECLS

#endif /* __GOSSIP_JABBER_PRIVATE_H__ */


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Martyn Russell <mr@gnome.org>
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

#ifndef __GOSSIP_STATUS_PRESETS_H__
#define __GOSSIP_STATUS_PRESETS_H__

G_BEGIN_DECLS

#include <libgossip/gossip-presence.h>

void   gossip_status_presets_get_all    (void);


GList *gossip_status_presets_get        (GossipPresenceState  presence);

void   gossip_status_presets_set_last   (const gchar         *name,
					 const gchar         *status,
					 GossipPresenceState  presence);

G_END_DECLS

#endif /* __GOSSIP_STATUS_PRESETS_H__ */

/* -*- mode: C; c-file-style: "gnu" -*- */
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
 * Authors: Martyn Russell <martyn@imendio.com>
 */

#ifndef __GOSSIP_DBUS_H__
#define __GOSSIP_DBUS_H__

#include <dbus/dbus-glib.h>

#include <libgossip/gossip.h>

G_BEGIN_DECLS

#define GOSSIP_DBUS_ERROR gnome_dbus_error_quark()

GQuark   gossip_dbus_error_quark          (void) G_GNUC_CONST;

/* Session */
gboolean gossip_dbus_init_for_session     (GossipSession *session,
                                           gboolean       multiple_instances);
void     gossip_dbus_finalize_for_session (void);

/* GNOME Network Manager */
gboolean gossip_dbus_nm_setup             (void);
gboolean gossip_dbus_nm_get_state         (gboolean      *connected);

G_END_DECLS

#endif /* __GOSSIP_DBUS_H__ */

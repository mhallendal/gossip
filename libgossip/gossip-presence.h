/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2004 Imendio AB
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

#ifndef __GOSSIP_PRESENCE_H__
#define __GOSSIP_PRESENCE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GOSSIP_TYPE_PRESENCE         (gossip_presence_get_type ())
#define GOSSIP_PRESENCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_PRESENCE, GossipPresence))
#define GOSSIP_PRESENCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOSSIP_TYPE_PRESENCE, GossipPresenceClass))
#define GOSSIP_IS_PRESENCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_PRESENCE))
#define GOSSIP_IS_PRESENCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_PRESENCE))
#define GOSSIP_PRESENCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_PRESENCE, GossipPresenceClass))

typedef struct _GossipPresence      GossipPresence;
typedef struct _GossipPresenceClass GossipPresenceClass;

struct _GossipPresence {
    GObject parent;
};

struct _GossipPresenceClass {
    GObjectClass parent_class;
};

typedef enum {
    GOSSIP_PRESENCE_STATE_AVAILABLE,
    GOSSIP_PRESENCE_STATE_BUSY,
    GOSSIP_PRESENCE_STATE_AWAY,
    GOSSIP_PRESENCE_STATE_EXT_AWAY,
    GOSSIP_PRESENCE_STATE_HIDDEN,      /* When you appear offline to others */
    GOSSIP_PRESENCE_STATE_UNAVAILABLE,
} GossipPresenceState;

GType               gossip_presence_get_type                 (void) G_GNUC_CONST;

GossipPresence *    gossip_presence_new                      (void);
GossipPresence *    gossip_presence_new_full                 (GossipPresenceState  state,
                                                              const gchar         *status);

const gchar *       gossip_presence_get_resource             (GossipPresence      *presence);
GossipPresenceState gossip_presence_get_state                (GossipPresence      *presence);
const gchar *       gossip_presence_get_status               (GossipPresence      *presence);
gint                gossip_presence_get_priority             (GossipPresence      *presence);

void                gossip_presence_set_resource             (GossipPresence      *presence,
                                                              const gchar         *resource);
void                gossip_presence_set_state                (GossipPresence      *presence,
                                                              GossipPresenceState  state);
void                gossip_presence_set_status               (GossipPresence      *presence,
                                                              const gchar         *status);
void                gossip_presence_set_priority             (GossipPresence      *presence,
                                                              gint                 priority);
gboolean            gossip_presence_resource_equal           (gconstpointer        a,
                                                              gconstpointer        b);
gint                gossip_presence_sort_func                (gconstpointer        a,
                                                              gconstpointer        b);

const gchar *       gossip_presence_state_get_default_status (GossipPresenceState  state);

G_END_DECLS

#endif /* __GOSSIP_PRESENCE_H__ */


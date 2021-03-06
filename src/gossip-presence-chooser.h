/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
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

#ifndef __GOSSIP_PRESENCE_CHOOSER_H__
#define __GOSSIP_PRESENCE_CHOOSER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOSSIP_TYPE_PRESENCE_CHOOSER         (gossip_presence_chooser_get_type ())
#define GOSSIP_PRESENCE_CHOOSER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_PRESENCE_CHOOSER, GossipPresenceChooser))
#define GOSSIP_PRESENCE_CHOOSER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOSSIP_TYPE_PRESENCE_CHOOSER, GossipPresenceChooserClass))
#define GOSSIP_IS_PRESENCE_CHOOSER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_PRESENCE_CHOOSER))
#define GOSSIP_IS_PRESENCE_CHOOSER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_PRESENCE_CHOOSER))
#define GOSSIP_PRESENCE_CHOOSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_PRESENCE_CHOOSER, GossipPresenceChooserClass))

typedef struct _GossipPresenceChooser      GossipPresenceChooser;
typedef struct _GossipPresenceChooserClass GossipPresenceChooserClass;

struct _GossipPresenceChooser {
    GtkToggleButton parent;
};

struct _GossipPresenceChooserClass {
    GtkToggleButtonClass parent_class;
};

GType      gossip_presence_chooser_get_type    (void) G_GNUC_CONST;
GtkWidget *gossip_presence_chooser_new         (void);
GtkWidget *gossip_presence_chooser_create_menu (GossipPresenceChooser *chooser,
                                                gint                   position,
                                                gboolean               sensitive,
                                                gboolean               include_clear);
void       gossip_presence_chooser_insert_menu (GossipPresenceChooser *chooser,
                                                GtkWidget             *menu,
                                                gint                   position,
                                                gboolean               sensitive,
                                                gboolean               include_clear);
void       gossip_presence_chooser_set_state   (GossipPresenceChooser *chooser,
                                                GossipPresenceState    state);
void       gossip_presence_chooser_set_status  (GossipPresenceChooser *chooser,
                                                const gchar           *status);

G_END_DECLS

#endif /* __GOSSIP_PRESENCE_CHOOSER_H__ */


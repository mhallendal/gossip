/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Imendio AB
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

#ifndef __GOSSIP_THEME_FANCY_H__
#define __GOSSIP_THEME_FANCY_H__

#include <glib-object.h>

#include "gossip-theme.h"

G_BEGIN_DECLS

#define GOSSIP_TYPE_THEME_FANCY            (gossip_theme_fancy_get_type ())
#define GOSSIP_THEME_FANCY(o)              (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_THEME_FANCY, GossipThemeFancy))
#define GOSSIP_THEME_FANCY_CLASS(k)        (G_TYPE_CHECK_CLASS_CAST ((k), GOSSIP_TYPE_THEME_FANCY, GossipThemeFancyClass))
#define GOSSIP_IS_THEME_FANCY(o)           (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_THEME_FANCY))
#define GOSSIP_IS_THEME_FANCY_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_THEME_FANCY))
#define GOSSIP_THEME_FANCY_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_THEME_FANCY, GossipThemeFancyClass))

typedef struct _GossipThemeFancy      GossipThemeFancy;
typedef struct _GossipThemeFancyClass GossipThemeFancyClass;

struct _GossipThemeFancy {
	GossipTheme parent;
};

struct _GossipThemeFancyClass {
	GossipThemeClass parent_class;
};

GType         gossip_theme_fancy_get_type      (void) G_GNUC_CONST;

GossipTheme * gossip_theme_fancy_new           (const gchar *name);

G_END_DECLS

#endif /* __GOSSIP_THEME_FANCY_H__ */

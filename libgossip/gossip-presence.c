/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <config.h>

#include <glib/gi18n.h>

#include "gossip-stock.h"
#include "gossip-utils.h"
#include "gossip-presence.h"

#define OFFLINE_MESSAGE "Offline"

#define AVAILABLE_MESSAGE "Available"
#define AWAY_MESSAGE "Away"
#define BUSY_MESSAGE "Busy"

#define GOSSIP_PRESENCE_GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_PRESENCE, GossipPresencePriv))

typedef struct _GossipPresencePriv GossipPresencePriv;
struct _GossipPresencePriv {
	GossipPresenceState  state;
	gchar               *status;

	gchar               *resource;

	gint                 priority;
};

static void presence_finalize          (GObject *object);
static void presence_get_property      (GObject              *object,
					guint                 param_id,
					GValue               *value,
					GParamSpec           *pspec);
static void presence_set_property      (GObject              *object,
					guint                 param_id,
					const GValue         *value,
					GParamSpec           *pspec);
static const gchar *
presence_get_default_status            (GossipPresenceState   state);

/* -- Properties -- */
enum {
	PROP_0,
	PROP_STATE,
	PROP_STATUS,
	PROP_RESOURCE,
	PROP_PRIORITY
};

G_DEFINE_TYPE (GossipPresence, gossip_presence, G_TYPE_OBJECT);

static gpointer parent_class = NULL;

static void
gossip_presence_class_init (GossipPresenceClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	parent_class = g_type_class_peek_parent (class);

	object_class->finalize     = presence_finalize;
	object_class->get_property = presence_get_property;
	object_class->set_property = presence_set_property;

	g_object_class_install_property (object_class,
					 PROP_STATE,
					 g_param_spec_int ("state",
							   "Presence State",
							   "The current state of the presence",
							   GOSSIP_PRESENCE_STATE_AVAILABLE,
							   GOSSIP_PRESENCE_STATE_EXT_AWAY,
							   GOSSIP_PRESENCE_STATE_AVAILABLE,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STATUS,
					 g_param_spec_string ("status",
							      "Presence Status",
							      "Status string set on presence",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_RESOURCE,
					 g_param_spec_string ("resource",
							      "Presence Resource",
							      "Resource that this presence is for",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PRIORITY,
					 g_param_spec_int ("priority",
							   "Presence Priority",
							   "Priority value of presence",
							   G_MININT,
							   G_MAXINT,
							   0,
							   G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GossipPresencePriv));
}

static void
gossip_presence_init (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	priv->state    = GOSSIP_PRESENCE_STATE_AVAILABLE;
	priv->status   = NULL;
	priv->resource = NULL;
	priv->priority = 0;
}

static void
presence_finalize (GObject *object)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (object);

	g_free (priv->status);
	g_free (priv->resource);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
presence_get_property (GObject    *object,
		       guint       param_id,
		       GValue     *value,
		       GParamSpec *pspec)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (object);

	switch (param_id) {
	case PROP_STATE:
		g_value_set_int (value, priv->state);
		break;
	case PROP_STATUS:
		g_value_set_string (value, 
				    gossip_presence_get_status (GOSSIP_PRESENCE (object)));
		break;
	case PROP_RESOURCE:
		g_value_set_string (value,
				    gossip_presence_get_resource (GOSSIP_PRESENCE (object)));
		break;
	case PROP_PRIORITY:
		g_value_set_int (value, priv->priority);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}
static void
presence_set_property (GObject      *object,
		       guint         param_id,
		       const GValue *value,
		       GParamSpec   *pspec)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (object);

	switch (param_id) {
	case PROP_STATE:
		priv->state = g_value_get_int (value);
		break;
	case PROP_STATUS:
		gossip_presence_set_status (GOSSIP_PRESENCE (object),
					    g_value_get_string (value));
		break;
	case PROP_RESOURCE:
		gossip_presence_set_resource (GOSSIP_PRESENCE (object),
					      g_value_get_string (value));
		break;
	case PROP_PRIORITY:
		priv->priority = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static const gchar *
presence_get_default_status (GossipPresenceState state)
{
	switch (state) {
	case GOSSIP_PRESENCE_STATE_AVAILABLE:
		return _(AVAILABLE_MESSAGE);
		break;
	case GOSSIP_PRESENCE_STATE_BUSY:
		return _(BUSY_MESSAGE);
		break;
	case GOSSIP_PRESENCE_STATE_AWAY:
	case GOSSIP_PRESENCE_STATE_EXT_AWAY:
		return _(AWAY_MESSAGE);
		break;
	}

	return _(AVAILABLE_MESSAGE);
}

GossipPresence *
gossip_presence_new (void)
{
	return g_object_new (GOSSIP_TYPE_PRESENCE, NULL);
}

GossipPresence *
gossip_presence_new_full (GossipPresenceState state, const gchar *status)
{
	return g_object_new (GOSSIP_TYPE_PRESENCE, 
			     "state", state,
			     "status", status,
			     NULL);
}

const gchar *
gossip_presence_get_resource (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence), "");

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);
	
	if (priv->resource) {
		return priv->resource;
	}

	return "";
}

void
gossip_presence_set_resource (GossipPresence *presence, const gchar *resource)
{
	GossipPresencePriv *priv;

	g_return_if_fail (GOSSIP_IS_PRESENCE (presence));
	g_return_if_fail (resource != NULL);

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	g_free (priv->resource);
	priv->resource = g_strdup (resource);
}

GossipPresenceState
gossip_presence_get_state (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence),
			      GOSSIP_PRESENCE_STATE_AVAILABLE);
	
	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	return priv->state;
}

void
gossip_presence_set_state (GossipPresence *presence, GossipPresenceState state)
{
	GossipPresencePriv *priv;

	g_return_if_fail (GOSSIP_IS_PRESENCE (presence));

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	priv->state = state;
}

const gchar *
gossip_presence_get_status (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence), 
			      _(OFFLINE_MESSAGE));

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	if (priv->status) {
		return priv->status;
	} else {
		return presence_get_default_status (priv->state);
	}

	return _(AVAILABLE_MESSAGE);
}

void
gossip_presence_set_status (GossipPresence *presence, const gchar *status)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);
	g_return_if_fail (GOSSIP_IS_PRESENCE (presence));

	g_free (priv->status);
	
	if (status) {
		priv->status = g_strdup (status);
	} else {
		priv->status = NULL;
	}
}

gint
gossip_presence_get_priority (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);
	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence), 0);

	return priv->priority;
}

void
gossip_presence_set_priority (GossipPresence *presence,
			      gint            priority)
{
	GossipPresencePriv *priv;

	g_return_if_fail (GOSSIP_IS_PRESENCE (presence));

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	priv->priority = priority;
}

GdkPixbuf *
gossip_presence_get_pixbuf (GossipPresence *presence)
{
	const gchar        *stock = NULL;

	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence), 
			      gossip_utils_get_pixbuf_offline ());

	switch (gossip_presence_get_state (presence)) {
	case GOSSIP_PRESENCE_STATE_AVAILABLE:
		stock = GOSSIP_STOCK_AVAILABLE;
		break;
	case GOSSIP_PRESENCE_STATE_BUSY:
		stock = GOSSIP_STOCK_BUSY;
		break;
	case GOSSIP_PRESENCE_STATE_AWAY:
		stock = GOSSIP_STOCK_AWAY;
		break;
	case GOSSIP_PRESENCE_STATE_EXT_AWAY:
		stock = GOSSIP_STOCK_EXT_AWAY;
		break;
	}

	return gossip_utils_get_pixbuf_from_stock (stock);
}

const gchar *
gossip_presence_get_default_status (GossipPresence *presence)
{
	GossipPresencePriv *priv;

	g_return_val_if_fail (GOSSIP_IS_PRESENCE (presence), 
			      _(OFFLINE_MESSAGE));

	priv = GOSSIP_PRESENCE_GET_PRIV (presence);

	return presence_get_default_status (priv->state);
}


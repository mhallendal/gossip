/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2005 Imendio AB
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

#ifndef __GOSSIP_UTILS_H__
#define __GOSSIP_UTILS_H__

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "gossip-account.h"

#define G_STR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

typedef enum {
	GOSSIP_REGEX_AS_IS,
	GOSSIP_REGEX_BROWSER,
	GOSSIP_REGEX_EMAIL,
	GOSSIP_REGEX_OTHER,
	GOSSIP_REGEX_ALL,
} GossipRegExType;

/* Regular expressions */
gchar *       gossip_substring           (const gchar     *str,
					  gint             start,
					  gint             end);
gint          gossip_regex_match         (GossipRegExType  type,
					  const gchar     *msg,
					  GArray          *start,
					  GArray          *end);

/* Strings */
gint          gossip_strcasecmp          (const gchar     *s1,
					  const gchar     *s2);
gint          gossip_strncasecmp         (const gchar     *s1,
					  const gchar     *s2,
					  gsize            n);

/* XML */
gboolean      gossip_xml_validate        (xmlDoc          *doc,
					  const gchar     *dtd_filename);

/* GValue/GType */
GType         gossip_dbus_type_to_g_type (const gchar     *dbus_type_string);
const gchar * gossip_g_type_to_dbus_type (GType            g_type);
gchar *       gossip_g_value_to_string   (const GValue    *value);
GValue *      gossip_string_to_g_value   (const gchar     *str,
					  GType            type);

#endif /*  __GOSSIP_UTILS_H__ */

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

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "gossip-status-presets.h"

#define STATUS_PRESETS_XML_FILENAME "status-presets.xml"
#define STATUS_PRESETS_DTD_FILENAME "gossip-status-presets.dtd"

/* this is per type of presence */
#define STATUS_PRESETS_MAX_EACH     5  

#define d(x) x


typedef struct {
	gchar               *name;
	gchar               *status;
	GossipPresenceState  presence;
} StatusPreset;


static void          status_presets_file_parse    (const gchar         *filename);
static gboolean      status_presets_file_save     (void);
static StatusPreset *status_preset_new            (const gchar         *name,
						   const gchar         *status,
						   GossipPresenceState  presence);
static void          status_preset_free           (StatusPreset        *status);


static GList *presets = NULL; 


void
gossip_status_presets_get_all (void)
{
	gchar    *dir;
	gchar    *file_with_path;

	/* if already set up clean up first */
	if (presets) {
		g_list_foreach (presets, (GFunc)status_preset_free, NULL);
		g_list_free (presets);
		presets = NULL;
	}

	dir = g_build_filename (g_get_home_dir (), ".gnome2", PACKAGE_NAME, NULL);
	if (!g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		mkdir (dir, S_IRUSR | S_IWUSR | S_IXUSR);
	}

	file_with_path = g_build_filename (dir, STATUS_PRESETS_XML_FILENAME, NULL);
	g_free (dir);

	if (g_file_test(file_with_path, G_FILE_TEST_EXISTS)) {
		status_presets_file_parse (file_with_path);
	}
	
	g_free (file_with_path);
}

static void
status_presets_file_parse (const gchar *filename) 
{
	xmlParserCtxtPtr  ctxt;
	xmlDocPtr         doc;
	xmlNodePtr        presets_node;
	xmlNodePtr        node;
	gint              count[4] = { 0, 0, 0, 0};
	
	g_return_if_fail (filename != NULL);

	d(g_print ("Attempting to parse file:'%s'...\n", filename));

 	ctxt = xmlNewParserCtxt ();

	/* Parse and validate the file. */
	doc = xmlCtxtReadFile (ctxt, filename, NULL, XML_PARSE_DTDVALID);	
	if (!doc) {
		g_warning ("Failed to parse file:'%s'", filename);
		xmlFreeParserCtxt (ctxt);
		return;
	}

	if (!ctxt->valid) {
		g_warning ("Failed to validate file:'%s'",  filename);
		xmlFreeDoc(doc);
		xmlFreeParserCtxt (ctxt);
		return;
	}
	
	/* The root node, presets. */
	presets_node = xmlDocGetRootElement (doc);

	node = presets_node->children;
	while (node) {
		if (strcmp ((gchar *) node->name, "status") == 0) {
			gchar               *status;
			gchar               *name;
			gchar               *presence_str;
			GossipPresenceState  presence;
			StatusPreset        *preset;

			status = (gchar *) xmlNodeGetContent (node);
			name = (gchar *) xmlGetProp (node, BAD_CAST ("name"));

			presence_str = (gchar *) xmlGetProp (node, BAD_CAST ("presence"));
			if (presence_str) {
				if (strcmp (presence_str, "available") == 0) {
					presence = GOSSIP_PRESENCE_STATE_AVAILABLE;
				}
				else if (strcmp (presence_str, "busy") == 0) {
					presence = GOSSIP_PRESENCE_STATE_BUSY;
				}
				else if (strcmp (presence_str, "away") == 0) {
					presence = GOSSIP_PRESENCE_STATE_AWAY;
				}
				else if (strcmp (presence_str, "ext_away") == 0) {
					presence = GOSSIP_PRESENCE_STATE_EXT_AWAY;
				} else {
					presence = GOSSIP_PRESENCE_STATE_AVAILABLE;
				}
				
				count[presence]++;
				if (count[presence] <= STATUS_PRESETS_MAX_EACH) {
					preset = status_preset_new (name, 
								    "koko", //status,
								    presence);
					
					presets = g_list_append (presets, preset);
				}
			}

			xmlFree (status);
			xmlFree (name);
			xmlFree (presence_str);
		}

		node = node->next;
	}
	
	d(g_print ("Parsed %d status presets\n", g_list_length (presets)));

	xmlFreeDoc (doc);
	xmlFreeParserCtxt (ctxt);
}

static StatusPreset *
status_preset_new (const gchar         *name,
		   const gchar         *status,
		   GossipPresenceState  presence)
{
	StatusPreset *preset;

	preset = g_new0 (StatusPreset, 1);
	
	preset->name = g_strdup (name);
	preset->status = g_strdup (status);;
	preset->presence = presence;

	return preset;
}

static void
status_preset_free (StatusPreset *preset)
{
	g_return_if_fail (preset != NULL);
	
	g_free (preset->name);
	g_free (preset->status);
	
	g_free (preset);
}

static gboolean
status_presets_file_save (void)
{
	xmlDocPtr   doc;  
	xmlDtdPtr   dtd;  
	xmlNodePtr  root;
	GList      *l;
	gchar      *dtd_file;
	gchar      *xml_dir;
	gchar      *xml_file;
	gint        count[4] = { 0, 0, 0, 0};

	xml_dir = g_build_filename (g_get_home_dir (), ".gnome2", PACKAGE_NAME, NULL);
	if (!g_file_test (xml_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		mkdir (xml_dir, S_IRUSR | S_IWUSR | S_IXUSR);
	}

	xml_file = g_build_filename (xml_dir, STATUS_PRESETS_XML_FILENAME, NULL);
	g_free (xml_dir);

	dtd_file = g_build_filename (DTDDIR, STATUS_PRESETS_DTD_FILENAME, NULL);

	doc = xmlNewDoc (BAD_CAST "1.0");
	root = xmlNewNode (NULL, BAD_CAST "presets");
	xmlDocSetRootElement (doc, root);

	dtd = xmlCreateIntSubset (doc, BAD_CAST "presets", NULL, BAD_CAST dtd_file);

	for (l = presets; l; l = l->next) {
		StatusPreset *sp;
		xmlNodePtr    subnode;
		xmlChar      *state;

		sp = l->data;

		switch (sp->presence) {
		case GOSSIP_PRESENCE_STATE_AVAILABLE:
			state = BAD_CAST "available";
			break;
		case GOSSIP_PRESENCE_STATE_BUSY:
			state = BAD_CAST "busy";
			break;
		case GOSSIP_PRESENCE_STATE_AWAY:
			state = BAD_CAST "away";
			break;
		case GOSSIP_PRESENCE_STATE_EXT_AWAY:
			state = BAD_CAST "ext_away";
			break;
		default:
			continue;
		}

		count[sp->presence]++;
		if (count[sp->presence] > STATUS_PRESETS_MAX_EACH) {
			continue;
		}
		
		subnode = xmlNewChild (root, NULL, BAD_CAST "status", BAD_CAST sp->status);
		xmlNewProp (subnode, BAD_CAST "name", BAD_CAST sp->name);
		xmlNewProp (subnode, BAD_CAST "presence", state);	
	}

	d(g_print ("Saving file:'%s'\n", xml_file));
	xmlSaveFormatFileEnc (xml_file, doc, "utf-8", 1);
	xmlFreeDoc (doc);

	xmlCleanupParser ();

	xmlMemoryDump ();
	
	g_free (xml_file);

	return TRUE;
}

GList *
gossip_status_presets_get (GossipPresenceState presence)
{
	GList *list = NULL;
	GList *l;
	
	for (l = presets; l; l = l->next) {
		StatusPreset *sp;

		sp = l->data;

		if (sp->presence != presence) {
			continue;
		}

		list = g_list_append (list, sp->status);
	}

	return list;
}

void
gossip_status_presets_set_last (const gchar         *name,
				const gchar         *status,
				GossipPresenceState  presence)
{
	GList        *l;
	gboolean      found = FALSE;

	g_return_if_fail (status != NULL);

	/* make sure this is not a duplicate*/
	for (l = presets; l; l = l->next) {
		StatusPreset *sp = l->data;

		if (!sp) {
			continue;
		}

		if (sp->name && name && strcmp (sp->name, name) == 0) {
			found = TRUE;
			break;
		}

		if (sp->status && status && strcmp (sp->status, status) == 0) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		StatusPreset *sp;

		sp = status_preset_new (name, status, presence);
		presets = g_list_prepend (presets, sp);

		status_presets_file_save ();
	}
}

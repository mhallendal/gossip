/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * 
 * Authors: Martyn Russell <martyn@imendio.com>
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <libloudermouth/loudermouth.h>

#ifdef HAVE_GNOME
#include <libgnomevfs/gnome-vfs.h>
#endif

#include <libgossip/gossip-contact.h>
#include <libgossip/gossip-debug.h>
#include <libgossip/gossip-presence.h>
#include <libgossip/gossip-ft.h>

#include "gossip-jabber-ft.h"
#include "gossip-jabber-private.h"
#include "gossip-jid.h"

#define DEBUG_DOMAIN "JabberFT"

#define XMPP_FILE_TRANSFER_XMLNS        "http://jabber.org/protocol/si"
#define XMPP_FILE_TRANSFER_PROFILE      "http://jabber.org/protocol/si/profile/file-transfer"

#define XMPP_FILE_XMLNS                 "http://jabber.org/protocol/si/profile/file-transfer"
#define XMPP_FEATURE_XMLNS              "http://jabber.org/protocol/feature-neg"

#define XMPP_BYTESTREAMS_PROTOCOL       "http://jabber.org/protocol/bytestreams"
#define XMPP_IBB_PROTOCOL               "http://jabber.org/protocol/ibb"

#define XMPP_AMP_XMLNS                  "http://jabber.org/protocol/amp"

#define IMPP_ERROR_XMLNS                "urn:ietf:params:xml:ns:xmpp-stanzas"

struct _GossipJabberFTs {
	GossipJabber   *jabber;
	LmConnection   *connection;
	LmBsSession    *bs_session;

	GHashTable     *str_ids;
	GHashTable     *ft_ids;
	GHashTable     *remote_ids;
	GHashTable     *jid_sids;
};
static void            jabber_ft_transfer_complete    (GossipJabber      *jabber,
						       guint              id);
static void            jabber_ft_transfer_failure     (GossipJabber      *jabber,
						       guint              id,
						       gchar             *reason);
static LmHandlerResult jabber_ft_iq_si_handler        (LmMessageHandler  *handler,
						       LmConnection      *conn,
						       LmMessage         *message,
						       GossipJabber      *jabber);
static void            jabber_ft_handle_request       (GossipJabber      *jabber,
						       LmMessage         *m);
static void            jabber_ft_handle_error         (GossipJabber      *jabber,
						       LmMessage         *m);
static void            jabber_ft_error                (GossipJabber      *jabber,
						       const gchar       *signal,
						       GossipFT          *ft,
						       GossipFTError      code,
						       const gchar       *reason);
static gboolean        jabber_ft_get_file_details     (const gchar       *uri,
						       gchar            **file_name,
						       gchar            **file_size,
						       gchar            **mime_type);
static gchar *         jabber_ft_get_contact_last_jid (GossipContact     *contact);

static gchar *         jabber_ft_get_unique_sid       (void);

GossipJabberFTs *
gossip_jabber_ft_init (GossipJabber *jabber)
{
	GossipJabberFTs  *fts;
	LmConnection     *connection;
	LmMessageHandler *handler;

	g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);

	connection = gossip_jabber_get_connection (jabber);

	fts = g_new0 (GossipJabberFTs, 1);
	fts->jabber     = g_object_ref (jabber);
	fts->connection = lm_connection_ref (connection);
	fts->bs_session = lm_bs_session_new (NULL);
	lm_bs_session_set_failure_function (fts->bs_session, 
					    (LmBsFailureFunction) jabber_ft_transfer_failure,
					    jabber,
					    NULL);

	lm_bs_session_set_complete_function (fts->bs_session, 
					    (LmBsCompleteFunction) jabber_ft_transfer_complete,
					    jabber,
					    NULL);

	fts->str_ids = g_hash_table_new_full (g_direct_hash,
					      g_direct_equal,
					      NULL,
					      g_free);

	fts->ft_ids = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     g_free,
					     g_object_unref);

	fts->remote_ids = g_hash_table_new_full (g_direct_hash,
						 g_direct_equal,
						 NULL,
						 g_free);

	fts->jid_sids = g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       NULL,
					       (GDestroyNotify) g_hash_table_destroy);

	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_ft_iq_si_handler,
					  jabber, NULL);
	lm_connection_register_message_handler (connection,
						handler,
						LM_MESSAGE_TYPE_IQ,
						LM_HANDLER_PRIORITY_NORMAL);
	lm_message_handler_unref (handler);


	return fts;
}

void
gossip_jabber_ft_finalize (GossipJabberFTs *fts)
{
	if (!fts) {
		/* We don't error here, because if no connection is
		made, then we can clean up a GossipJabber object
		without any fts ever existing */
		return;
	}

	g_hash_table_destroy (fts->str_ids);
	g_hash_table_destroy (fts->ft_ids);
	g_hash_table_destroy (fts->remote_ids);
	g_hash_table_destroy (fts->jid_sids);

	lm_connection_unref (fts->connection);
	g_object_unref (fts->jabber);

	g_free (fts);
}

static guint
guess_id_by_sid_and_sender (GossipJabber *jabber,
			    const gchar  *sid,
			    const gchar  *jid)
{
	GossipJabberFTs *fts;
	GHashTable      *jids;
	gpointer         id_ptr;
	
	fts = gossip_jabber_get_fts (jabber);
	g_return_val_if_fail (fts, 0);
	jids = g_hash_table_lookup (fts->jid_sids, jid);
	if (!jids) {
		return 0;
	}
	id_ptr = g_hash_table_lookup (jids, sid);
	if (!id_ptr) {
		return 0;
	}
	return GPOINTER_TO_UINT(id_ptr);
}

static void
jabber_ft_transfer_complete (GossipJabber *jabber, guint id)
{
	gossip_debug (DEBUG_DOMAIN, "transfer is complete");
}

static void
jabber_ft_transfer_failure (GossipJabber *jabber, guint id, gchar *reason)
{
	gossip_debug (DEBUG_DOMAIN, "transfer failed");
}

static void
add_jid_sid_to_table (GossipJabberFTs *fts,
		      const gchar     *jid,
		      const gchar     *sid,
		      guint            id)
{
	GHashTable *jid_sids_table;

	jid_sids_table = g_hash_table_lookup(fts->jid_sids, jid);
	if (!jid_sids_table) {
		jid_sids_table = g_hash_table_new_full (g_str_hash,
							g_str_equal,
							g_free,
							NULL);
		g_hash_table_insert (fts->jid_sids,
				     g_strdup (jid),
				     jid_sids_table);
	} 
	g_hash_table_insert (jid_sids_table,
			     g_strdup (sid),
			     GUINT_TO_POINTER (id));
}

static LmHandlerResult
jabber_ft_iq_streamhost (LmConnection *conn,
			 LmMessage    *m,
			 GossipJabber *jabber)
{
	GossipJabberFTs *fts;
	LmMessageNode   *node;

	const gchar   *attr;
	const gchar   *iq_id;

	node = lm_message_node_get_child (m->node, "query");
	if(!node) {
		/* child node is not a query */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_attribute (node, "xmlns");
	if(!attr || strcmp (attr, XMPP_BYTESTREAMS_PROTOCOL) != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	node = lm_message_node_get_child (node, "streamhost-used");
	if(!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_attribute (node, "jid");
	if(!attr) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	iq_id = lm_message_node_get_attribute (m->node, "id");
	fts = gossip_jabber_get_fts (jabber);
	lm_bs_session_activate_streamhost (fts->bs_session, iq_id, attr);
	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult
jabber_ft_iq_query (LmConnection   *conn,
		    LmMessage      *m,
		    GossipJabber   *jabber)
{
	LmMessageNode   *child;
	LmMessageNode   *node;
	GossipJabberFTs *fts;
	const gchar     *xmlns;
	const gchar     *sid;
	const gchar     *iq_id;
	const gchar     *host;
	const gchar     *port;
	const gchar     *jid;
	guint            id;

	node = lm_message_node_get_child (m->node, "query");
	if(!node) {
		/* child node is not a query */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	xmlns = lm_message_node_get_attribute (node, "xmlns");
	if(!xmlns || strcmp (xmlns, XMPP_BYTESTREAMS_PROTOCOL) != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	sid = lm_message_node_get_attribute (node, "sid");
	if(!sid) {
		/* cannot identify transfer */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	jid = lm_message_node_get_attribute (m->node, "from");
	if (!jid) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	id = guess_id_by_sid_and_sender (jabber, sid, jid);
	if (id == 0) {
		/* previos records for transfer doesn't exist */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	iq_id = lm_message_node_get_attribute (m->node, "id");
	fts = gossip_jabber_get_fts (jabber);
	lm_bs_session_set_iq_id (fts->bs_session, id, iq_id);

	/* get all children named "streamhost" */
	for (child = node->children; child; child = child->next) {
		if (strcmp (child->name, "streamhost") == 0) {
		/* this id is needed for the reply stanza 'streamhost-used' */
			host = lm_message_node_get_attribute (child, 
							      "host");
			port = lm_message_node_get_attribute (child, 
							      "port");
			jid = lm_message_node_get_attribute (child,
							     "jid");
			if(!host || !port || !jid) {
				continue;
			}
			lm_bs_session_add_streamhost (fts->bs_session,
						      id,
						      host,
						      port,
						      jid);
		}
	}
	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void 
jabber_ft_send_streamhosts (LmConnection *conn,
			    GossipJabber *jabber,
			    GossipFT     *ft)
{
	GossipJabberFTs *fts;
	LmMessage       *m;
	LmMessageNode   *node;
	gint             file_id;
	const gchar     *to_str;
	const gchar     *iq_id;
	const gchar     *sid;
	gchar           *local_host;
	guint            port;
	gchar           *port_str;
	guint            id;
	gchar           *jid_str;
	
	fts = gossip_jabber_get_fts (jabber);
	g_return_if_fail (fts != NULL);
	file_id = gossip_ft_get_id (ft);
	sid = gossip_ft_get_sid (ft);
	to_str = g_hash_table_lookup (fts->remote_ids,
				      GUINT_TO_POINTER(file_id));
	m = lm_message_new_with_sub_type (to_str,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);
	id = guess_id_by_sid_and_sender (jabber, sid, to_str);
	if (id == 0) {
		/* previos records for transfer doesn't exist */
		return;
	}

#ifdef HAVE_LM_WITH_FT
	jid_str = lm_connection_get_full_jid (conn);
#else
	return;
#endif

	port = lm_bs_session_start_listener (fts->bs_session);
	port_str = g_strdup_printf ("%d", port);
	node = lm_message_node_add_child (m->node, "query", NULL);
	lm_message_node_set_attributes (node,
					"xmlns",
					XMPP_BYTESTREAMS_PROTOCOL,
					"mode",
					"tcp",
					"sid",
					sid,
					NULL);
#ifdef HAVE_LM_WITH_FT
	local_host = lm_connection_get_client_host (conn);
#else 
	return;
#endif
	node = lm_message_node_add_child (node, "streamhost", NULL);
	lm_message_node_set_attributes (node,
					"host",
					local_host,
					"jid",
					jid_str,
					"port",
					port_str);
	
	lm_bs_session_add_streamhost (fts->bs_session,
				      id,
				      local_host,
				      port_str,
				      jid_str);
	
	lm_connection_send (conn, m, NULL);

	iq_id = lm_message_node_get_attribute (m->node, "id");
	lm_bs_session_set_iq_id (fts->bs_session, id, iq_id);

	g_free (jid_str);
	g_free (port_str);
	g_free (local_host);
	lm_message_unref (m);
}


static LmHandlerResult
jabber_ft_feature_result (LmConnection   *conn,
			  LmMessage      *m,
			  GossipJabber   *jabber)
{
	GossipJabberFTs *fts;
	GossipFT        *ft;
	const gchar     *id_str;
	LmMessageNode   *node;
	const gchar     *attr;

	fts = gossip_jabber_get_fts (jabber);
	g_return_val_if_fail (fts != NULL,
			      LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS);

	node = lm_message_node_get_child (m->node, "si");
	/* already tested in jabber_ft_iq_si_handler */
	g_return_val_if_fail (node != NULL, 
			      LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS);
	node = lm_message_node_get_child (node, "feature");
	if (!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_attribute (node, "xmlns");
	if (!attr || strcmp (attr, XMPP_FEATURE_XMLNS) != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	node = lm_message_node_get_child (node, "x");
	if (!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_attribute (node, "type");
	if (!attr || strcmp (attr, "submit") != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	node = lm_message_node_get_child (node, "field");
	if (!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_attribute (node, "var");
	if (!attr || strcmp (attr, "stream-method") != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	node = lm_message_node_get_child (node, "value");
	if (!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	attr = lm_message_node_get_value (node);
	if (!attr || strcmp (attr, XMPP_BYTESTREAMS_PROTOCOL) != 0) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	id_str = lm_message_node_get_attribute (m->node, "id");
	ft = g_hash_table_lookup (fts->ft_ids, id_str);
	if (!ft) {
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}
	jabber_ft_send_streamhosts (conn, jabber, ft);
	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult
jabber_ft_iq_si_handler (LmMessageHandler *handler,
			 LmConnection     *conn,
			 LmMessage        *m,
			 GossipJabber     *jabber)
{
	LmMessageSubType  subtype;
	LmMessageNode    *node;
	const gchar      *xmlns;

	subtype = lm_message_get_sub_type (m);

	if (subtype != LM_MESSAGE_SUB_TYPE_GET &&
	    subtype != LM_MESSAGE_SUB_TYPE_SET &&
	    subtype != LM_MESSAGE_SUB_TYPE_RESULT &&
	    subtype != LM_MESSAGE_SUB_TYPE_ERROR) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	if (subtype == LM_MESSAGE_SUB_TYPE_ERROR) {
		/* handle it */
		jabber_ft_handle_error (jabber, m);
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	node = lm_message_node_get_child (m->node, "si");
	if (!node) {
		if (subtype == LM_MESSAGE_SUB_TYPE_SET) {
			return jabber_ft_iq_query (conn, m, jabber);
		} 
		else if (subtype == LM_MESSAGE_SUB_TYPE_RESULT) {
			return jabber_ft_iq_streamhost (conn, m, jabber);
		} else {
			return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
		}
	}

	xmlns = lm_message_node_get_attribute (node, "xmlns");

	if (xmlns && strcmp (xmlns, XMPP_FILE_TRANSFER_XMLNS) == 0) {
		const gchar *profile;

		profile = lm_message_node_get_attribute (node, "profile");
		if (profile && strcmp (profile, XMPP_FILE_TRANSFER_PROFILE) == 0) {
			jabber_ft_handle_request (jabber, m);
			return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
		}
		if (subtype == LM_MESSAGE_SUB_TYPE_RESULT) {
			return jabber_ft_feature_result (conn, m, jabber);
		}
	}

	/* if a get, return error for unsupported IQ */
	if (subtype == LM_MESSAGE_SUB_TYPE_GET ||
	    subtype == LM_MESSAGE_SUB_TYPE_SET) {
		/* do something:
		   no Jabber spec for this that I could see (mjr) */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/*
 * receiving
 */
static void
jabber_ft_handle_request (GossipJabber *jabber,
			  LmMessage    *m)
{
	GossipJabberFTs *fts;
	const gchar     *id_str;
	const gchar     *from_str;
	GossipContact   *from;
	GossipFT        *ft;
	GossipFTId       id;
	LmMessageNode   *node;
	const gchar     *file_name;
	const gchar     *file_size;
	const gchar     *file_mime;
	const gchar     *sid;

	fts = gossip_jabber_get_fts (jabber);
	g_return_if_fail (fts != NULL);

	from_str = lm_message_node_get_attribute (m->node, "from");
	id_str = lm_message_node_get_attribute (m->node, "id");
	from = gossip_jabber_get_contact_from_jid (jabber, from_str, NULL, TRUE);

	node = lm_message_node_find_child (m->node, "si");
	sid = lm_message_node_get_attribute (node, "id");
	file_mime = lm_message_node_get_attribute (node, "mime-type");

	node = lm_message_node_find_child (m->node, "file");
	file_name = lm_message_node_get_attribute (node, "name");
	file_size = lm_message_node_get_attribute (node, "size");
	
	ft = g_object_new (GOSSIP_TYPE_FT,
			   "type", GOSSIP_FT_TYPE_RECEIVING,
			   "contact", from,
			   "file-name", file_name,
			   "file-size", g_ascii_strtoull (file_size, NULL, 10),
			   "file-mime-type", file_mime,
			   "sid", sid,
			   NULL);

	g_object_get (ft, "id", &id, NULL);

	gossip_debug (DEBUG_DOMAIN,
		      "ID[%d] File transfer request from:'%s' with file:'%s', size:'%s'",
		      id,
		      from_str,
		      file_name,
		      file_size);

	g_hash_table_insert (fts->str_ids, GUINT_TO_POINTER (id), g_strdup (id_str));
	g_hash_table_insert (fts->ft_ids, g_strdup (id_str), ft);
	g_hash_table_insert (fts->remote_ids, GUINT_TO_POINTER (id), g_strdup (from_str));
	add_jid_sid_to_table (fts, from_str, sid, id);

	/* signal */
	g_signal_emit_by_name (fts->jabber,
			       "file-transfer-request",
			       ft);
}

static void
jabber_ft_handle_error (GossipJabber *jabber,
			LmMessage    *m)
{
	GossipJabberFTs *fts;
	GossipFT        *ft;
	GossipFTError    error_code;
	const gchar     *error_code_str;
	const gchar     *error_reason;
	const gchar     *id_str;
	const gchar     *from_str;
	GossipContact   *from;
	LmMessageNode   *node;

	fts = gossip_jabber_get_fts (jabber);
	g_return_if_fail (fts != NULL);

	id_str = lm_message_node_get_attribute (m->node, "id");

	ft = g_hash_table_lookup (fts->ft_ids, id_str);
	if (!ft) {
		gossip_debug (DEBUG_DOMAIN,
			      "Could not find GossipFT* id:'%s'",
			      id_str);
		return;
	}

	from_str = lm_message_node_get_attribute (m->node, "from");
	from = gossip_jabber_get_contact_from_jid (jabber, from_str, NULL, TRUE);

	node = lm_message_node_find_child (m->node, "error");
	error_reason = lm_message_node_get_value (node);
	error_code_str = lm_message_node_get_attribute (node, "code");

	gossip_debug (DEBUG_DOMAIN,
		      "File transfer error from:'%s' with code:'%s', reason:'%s'",
		      from_str,
		      error_code_str,
		      error_reason);

	switch (atoi (error_code_str)) {
	case 400:
		error_code = GOSSIP_FT_ERROR_UNSUPPORTED;
		break;
	case 403:
		error_code = GOSSIP_FT_ERROR_DECLINED;
		break;
	default:
		error_code = GOSSIP_FT_ERROR_UNKNOWN;
		break;
	}

	/* signal */
	jabber_ft_error (fts->jabber,
			 "file-transfer-error",
			 ft,
			 error_code,
			 error_reason ? error_reason : "");
}

static void
jabber_ft_error (GossipJabber  *jabber,
		 const gchar   *signal,
		 GossipFT      *ft,
		 GossipFTError  code,
		 const gchar   *reason)
{
	GError *error;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));
	g_return_if_fail (GOSSIP_IS_FT (ft));
	g_return_if_fail (signal != NULL);
	g_return_if_fail (reason != NULL);

	error = g_error_new_literal (gossip_jabber_error_quark(), code, reason);
	g_signal_emit_by_name (jabber, signal, ft, error);
	g_error_free (error);
}

/*
 * utils
 */
static gboolean
jabber_ft_get_file_details (const gchar  *uri,
			    gchar       **file_name,
			    gchar       **file_size,
			    gchar       **mime_type)
{
#ifdef HAVE_GNOME
	GnomeVFSFileInfo *file_info;
	GnomeVFSResult    result;

	gossip_debug (DEBUG_DOMAIN, "Getting file info for URI:'%s'", uri);

	file_info = gnome_vfs_file_info_new ();

	result = gnome_vfs_get_file_info (uri,
					  file_info,
					  GNOME_VFS_FILE_INFO_DEFAULT);

	if (result != GNOME_VFS_OK) {
		g_warning ("ProtocolFT: Could not get file info for URI:'%s'", uri);
		return FALSE;
	}

	if (file_name) {
		*file_name = g_strdup (file_info->name);
	}

	if (file_size) {
		*file_size = g_strdup_printf ("%d", (guint) file_info->size);
	}

	if (mime_type) {
		gchar *s;

		s = gnome_vfs_get_mime_type (uri);
		*mime_type = s;

		if (!s) {
			*mime_type = g_strdup (GNOME_VFS_MIME_TYPE_UNKNOWN);
		}
	}

	gnome_vfs_file_info_unref (file_info);

	return TRUE;
#endif
	return FALSE;
}

static gchar *
jabber_ft_get_contact_last_jid (GossipContact *contact)
{
	GossipPresence *presence;
	const gchar    *resource;
	gchar          *to;

	/* note: this function gets the JID for the contact based on
	   the last resource they sent a message to us from, so it
	   makes sure we send the request or do things with the JID at
	   the last location they spoke to us from */

	presence = gossip_contact_get_active_presence (contact);
	resource = gossip_presence_get_resource (presence);

	to = g_strdup_printf ("%s/%s",
			      gossip_contact_get_id (contact),
			      resource ? resource : "");

	return to;
}

static gchar *         jabber_ft_get_unique_sid       () {
	static guint id = 0;
	id++;
	return g_strdup_printf("GOSSIPSID%d", id);
}


/*
 * sending
 */

GossipFTId
gossip_jabber_ft_send (GossipJabberFTs *fts,
		       GossipContact   *contact,
		       const gchar     *file)
{
	GossipFT      *ft;
	GossipFTId     id;
	const gchar   *id_str;
	GossipContact *own_contact;
	LmConnection  *connection;
	LmMessage     *m;
	LmMessageNode *node, *si_node, *field_node;
	gchar         *to;
	gchar         *file_size;
	gchar         *file_name;
	gchar         *mime_type;
	gboolean       ok;
	const gchar   *sid;
	gchar         *file_path;

	g_return_val_if_fail (fts != NULL, 0);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), 0);
	g_return_val_if_fail (file != NULL, 0);

	connection = gossip_jabber_get_connection (fts->jabber);
	own_contact = gossip_jabber_get_own_contact (fts->jabber);

	to = jabber_ft_get_contact_last_jid (contact);

	ok = jabber_ft_get_file_details (file,
					 &file_name,
					 &file_size,
					 &mime_type);
	if (!ok) {
		return 0;
	}

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);
	node = m->node;

	id_str = lm_message_node_get_attribute (m->node, "id");

	/* bare jids are not allowed in FT */
	/* lm_message_node_set_attribute (node, "from",
				       gossip_contact_get_id (own_contact)); */

	node = lm_message_node_add_child (node, "si", NULL);
	sid = jabber_ft_get_unique_sid();
	lm_message_node_set_attributes (node,
					"profile", XMPP_FILE_TRANSFER_PROFILE,
					"mime-type", mime_type,
					"id", sid,
					"xmlns", XMPP_FILE_TRANSFER_XMLNS,
					NULL);

	si_node = node;

	node = lm_message_node_add_child (si_node, "file", NULL);
	lm_message_node_set_attributes (node,
					"size", file_size,
					"name", file_name,
					"xmlns", XMPP_FILE_XMLNS,
					NULL);

	node = lm_message_node_add_child (si_node, "feature", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_FEATURE_XMLNS,
					NULL);

	node = lm_message_node_add_child (node, "x", NULL);
	lm_message_node_set_attributes (node,
					"type", "form",
					"xmlns", "jabber:x:data",
					NULL);

	node = lm_message_node_add_child (node, "field", NULL);
	lm_message_node_set_attributes (node,
					"type", "list-single",
					"var", "stream-method",
					NULL);

	field_node = node;

	node = lm_message_node_add_child (field_node, "option", NULL);
	lm_message_node_add_child (node, "value", XMPP_BYTESTREAMS_PROTOCOL);

	/* create object */
	ft = g_object_new (GOSSIP_TYPE_FT,
			   "type", GOSSIP_FT_TYPE_SENDING,
			   "contact", contact,
			   "file-name", file_name,
			   "file-size", g_ascii_strtoull (file_size, NULL, 10),
			   "file-mime-type", mime_type,
			   "sid", sid,
			   NULL);

	g_object_get (ft, "id", &id, NULL);

	gossip_debug (DEBUG_DOMAIN,
		      "ID[%d] Sending file transfer request for URI:'%s'",
		      id, file);

	g_hash_table_insert (fts->str_ids, GUINT_TO_POINTER (id), g_strdup (id_str));
	g_hash_table_insert (fts->ft_ids, g_strdup (id_str), ft);
	g_hash_table_insert (fts->remote_ids, GUINT_TO_POINTER (id), to);
	add_jid_sid_to_table (fts, to, sid, id);

	file_path = g_filename_from_uri (file, NULL, NULL);

	lm_bs_session_send_file (fts->bs_session,
				 connection,
				 id,
				 sid,
				 to,
				 file_path,
				 gossip_ft_get_file_size(ft));
	g_free (file_path);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);

	/* clean up */
	g_free (file_name);
	g_free (file_size);
	g_free (mime_type);

	return id;
}

void
gossip_jabber_ft_accept (GossipJabberFTs *fts,
			 GossipFTId       id)
{
	GossipContact *own_contact;
	const gchar   *id_str;
	const gchar   *to_str;
	const gchar   *file_uri;
	gchar         *file_path;
	LmConnection  *connection;
	LmMessage     *m;
	LmMessageNode *node;
	GossipFT      *ft;

	g_return_if_fail (fts != NULL);

	gossip_debug (DEBUG_DOMAIN, "ID[%d] Accepting file transfer", id);

	connection = gossip_jabber_get_connection (fts->jabber);
	own_contact = gossip_jabber_get_own_contact (fts->jabber);
	to_str = g_hash_table_lookup (fts->remote_ids, GUINT_TO_POINTER (id));
	id_str = g_hash_table_lookup (fts->str_ids, GUINT_TO_POINTER (id));
	ft = g_hash_table_lookup (fts->ft_ids, id_str);
	if (!to_str) {
		g_warning ("ProtocolFT: ID[%d] Could not get remote JID", id);
		return;
	}

	if (!id_str) {
		g_warning ("ProtocolFT: ID[%d] Could not get original message id", id);
		return;
	}

	file_uri = gossip_ft_get_location (ft);
	file_path = g_filename_from_uri (file_uri, NULL, NULL);

	lm_bs_session_receive_file (fts->bs_session,
				    connection,
				    id,
				    gossip_ft_get_sid (ft),
				    to_str,
				    file_path,
				    gossip_ft_get_file_size (ft));
	g_free (file_path);
	m = lm_message_new_with_sub_type (to_str,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_RESULT);
	node = m->node;
	lm_message_node_set_attributes (node,
					"id", id_str,
					/* don't set a bare JID  */
					/* "from", gossip_contact_get_id (own_contact), */
					NULL);

	node = lm_message_node_add_child (node, "si", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_FILE_TRANSFER_XMLNS,
					NULL);

	node = lm_message_node_add_child (node, "feature", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_FEATURE_XMLNS,
					NULL);

	node = lm_message_node_add_child (node, "x", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", "jabber:x:data",
					"type", "submit",
					NULL);

	node = lm_message_node_add_child (node, "field", NULL);
	lm_message_node_set_attributes (node,
					"var", "stream-method",
					NULL);

	/* choose method here, currently we only support one */
	lm_message_node_add_child (node, "value", XMPP_BYTESTREAMS_PROTOCOL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_ft_decline (GossipJabberFTs *fts,
			  GossipFTId       id)
{
	GossipContact *own_contact;
	const gchar   *id_str;
	const gchar   *to_str;
	LmConnection  *connection;
	LmMessage     *m;
	LmMessageNode *node;

	g_return_if_fail (fts != NULL);

	gossip_debug (DEBUG_DOMAIN, "ID[%d] Declining file transfer", id);

	connection = gossip_jabber_get_connection (fts->jabber);
	own_contact = gossip_jabber_get_own_contact (fts->jabber);

	to_str = g_hash_table_lookup (fts->remote_ids, GUINT_TO_POINTER (id));
	id_str = g_hash_table_lookup (fts->str_ids, GUINT_TO_POINTER (id));

	if (!to_str) {
		g_warning ("ProtocolFT: ID[%d] Could not get remote JID", id);
		return;
	}

	if (!id_str) {
		g_warning ("ProtocolFT: ID[%d] Could not get original message id", id);
		return;
	}

	m = lm_message_new_with_sub_type (to_str,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_ERROR);
	node = m->node;

	lm_message_node_set_attributes (node,
					"id", id_str,
					"from", gossip_contact_get_id (own_contact),
					NULL);

	node = lm_message_node_add_child (node, "error", "Declined");
	lm_message_node_set_attributes (node,
					"code", "403",
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);

	/* clean up hash tables */
	g_hash_table_remove (fts->str_ids, &id);
	g_hash_table_remove (fts->ft_ids, id_str);
	g_hash_table_remove (fts->remote_ids, &id);
	
	/* remove it from bt session */
	lm_bs_session_remove_transfer (fts->bs_session, id);
}

void
gossip_jabber_ft_cancel (GossipJabberFTs *fts,
			 GossipFTId       id)
{
	g_return_if_fail (fts != NULL);
	lm_bs_session_remove_transfer (fts->bs_session, id);
/* 	room = (JabberChatroom*) g_hash_table_lookup (chatrooms->room_id_hash,  */
/* 						       GINT_TO_POINTER (id)); */
}


/*
 * stream stuff
 */

void
gossip_jabber_ft_iib_start (GossipJabber *jabber,
			    const gchar  *to)
{
	LmConnection *connection;
	LmMessage     *m;
	LmMessageNode *node;

	const gchar   *sid = "test";
	const gchar   *block_size = "4096";

	g_return_if_fail (to != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);
	node = m->node;

	node = lm_message_node_add_child (node, "open", NULL);
	lm_message_node_set_attributes (node,
					"sid", sid,
					"block-size", block_size,
					"xmlns", XMPP_IBB_PROTOCOL,
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_ft_iib_start_response (GossipJabber *jabber,
				     const gchar  *to,
				     const gchar  *id)
{
	LmConnection *connection;
	LmMessage    *m;

	g_return_if_fail (to != NULL);
	g_return_if_fail (id != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_RESULT);
	lm_message_node_set_attributes (m->node,
					"id", id,
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}


void
gossip_jabber_ft_iib_error (GossipJabber *jabber,
			    const gchar  *to,
			    const gchar  *id,
			    const gchar  *error_code,
			    const gchar  *error_type)
{
	LmConnection *connection;
	LmMessage     *m;
	LmMessageNode *node;

	g_return_if_fail (to != NULL);
	g_return_if_fail (id != NULL);
	g_return_if_fail (error_code != NULL);
	g_return_if_fail (error_type != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_ERROR);

	node = m->node;

	lm_message_node_set_attributes (node,
					"id", id,
					NULL);

	node = lm_message_node_add_child (node, "error", NULL);
	lm_message_node_set_attributes (node,
					"code", error_code,
					"type", error_type,
					NULL);

	node = lm_message_node_add_child (node, "feature-not-implemented", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", IMPP_ERROR_XMLNS,
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_ft_iib_send (GossipJabber *jabber,
			   const gchar  *to,
			   const gchar  *sid,
			   const gchar  *data,
			   const gchar  *seq)
{
	LmConnection *connection;
	LmMessage     *m;
	LmMessageNode *node, *amp_node;

	g_return_if_fail (to != NULL);
	g_return_if_fail (sid != NULL);
	g_return_if_fail (data != NULL);
	g_return_if_fail (seq != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new (to, LM_MESSAGE_TYPE_MESSAGE);

	node = m->node;

	node = lm_message_node_add_child (node, "data", data);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_IBB_PROTOCOL,
					"sid", sid,
					"seq", seq,
					NULL);

	node = lm_message_node_add_child (node, "amp", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_AMP_XMLNS,
					NULL);

	amp_node = node;

	node = lm_message_node_add_child (amp_node, "rule", NULL);
	lm_message_node_set_attributes (node,
					"condition", "deliver-at",
					"value", "stored",
					"action", "error",
					NULL);

	node = lm_message_node_add_child (amp_node, "rule", NULL);
	lm_message_node_set_attributes (node,
					"condition", "match-resource",
					"value", "exact",
					"action", "error",
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_ft_iib_finish (GossipJabber *jabber,
			     const gchar  *to,
			     const gchar  *sid)
{
	LmConnection *connection;
	LmMessage     *m;
	LmMessageNode *node;

	g_return_if_fail (to != NULL);
	g_return_if_fail (sid != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);

	node = m->node;

	node = lm_message_node_add_child (node, "close", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_IBB_PROTOCOL,
					"sid", sid,
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_ft_iib_finish_response (GossipJabber *jabber,
				      const gchar  *to,
				      const gchar  *id)
{
	LmConnection *connection;
	LmMessage    *m;

	g_return_if_fail (to != NULL);
	g_return_if_fail (id != NULL);

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (to,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_RESULT);
	lm_message_node_set_attributes (m->node,
					"id", id,
					NULL);

	/* send */
	lm_connection_send (connection, m, NULL);
	lm_message_unref (m);
}




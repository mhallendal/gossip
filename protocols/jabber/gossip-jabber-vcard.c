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

#include <stdlib.h> 

#include <loudmouth/loudmouth.h>

#include <libgossip/gossip-utils.h>

#include "gossip-jabber-vcard.h"
#include "gossip-jabber-private.h"
#include "gossip-jid.h"

#define DEBUG_MSG(x)
/* #define DEBUG_MSG(args) g_printerr args ; g_printerr ("\n"); */

static LmHandlerResult 
jabber_vcard_get_cb (LmMessageHandler   *handler,
		     LmConnection       *connection,
		     LmMessage          *m,
		     GossipCallbackData *data)
{
	GossipVCard         *vcard;
	GossipVCardCallback  callback;
	LmMessageNode       *vcard_node, *photo_node, *node;
	LmMessageSubType     type;

	if (!data || !data->callback) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	callback = data->callback;

	/* check for error */
	type = lm_message_get_sub_type (m);

	if (type == LM_MESSAGE_SUB_TYPE_ERROR) {
		GossipResult result = GOSSIP_RESULT_ERROR_INVALID_REPLY;

		node = lm_message_node_get_child (m->node, "error");
		if (node) {
			const gchar *str;
			gint         code;

			str = lm_message_node_get_attribute (node, "code");
			code = str ? atoi (str) : 0;
			
			switch (code) {
			case 404: {
				/* receipient unavailable */
				DEBUG_MSG (("VCard: Receipient is unavailable"));
				result = GOSSIP_RESULT_ERROR_UNAVAILABLE;
				break;
			}

			default:
				DEBUG_MSG (("VCard: Unhandled presence error:%d", code));
				result = GOSSIP_RESULT_ERROR_INVALID_REPLY;
				break;
			}
		}

		(callback) (result, 
			    NULL,
			    data->user_data);

		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	} 

	/* no vcard node */
	vcard_node = lm_message_node_get_child (m->node, "vCard");
	if (!vcard_node) {
		(callback) (GOSSIP_RESULT_ERROR_INVALID_REPLY, 
			    NULL,
			    data->user_data);

		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	/* everything else must be OK */
	vcard = gossip_vcard_new ();
	
	node = lm_message_node_get_child (vcard_node, "FN");
	if (node) {
		gossip_vcard_set_name (vcard, node->value);
	}

	node = lm_message_node_get_child (vcard_node, "NICKNAME");
	if (node) {
		gossip_vcard_set_nickname (vcard, node->value);
	}

	node = lm_message_node_get_child (vcard_node, "EMAIL");
	if (node) {
		gchar *email = NULL;

		if (node->value) {
			email = node->value;
		} else {
			node = lm_message_node_get_child (node, "USERID");
			if (node) {
				email = node->value;
			}
		}
			
		gossip_vcard_set_email (vcard, email);
	}

	node = lm_message_node_get_child (vcard_node, "URL");
	if (node) {
		gossip_vcard_set_url (vcard, node->value);
	}

	node = lm_message_node_get_child (vcard_node, "DESC");
	if (node) {
		gossip_vcard_set_description (vcard, node->value);
	}

	photo_node = lm_message_node_get_child (vcard_node, "PHOTO");
	if (photo_node) {
		node = lm_message_node_get_child (photo_node, "BINVAL");
		if (node) {
			guchar *decoded_avatar;
			gsize   len;

			decoded_avatar = gossip_base64_decode (node->value, &len);
			gossip_vcard_set_avatar (vcard, decoded_avatar, len);
			g_free (decoded_avatar);
		}
	}

	(callback) (GOSSIP_RESULT_OK, vcard, data->user_data);

	g_object_unref (vcard);

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

gboolean
gossip_jabber_vcard_get (GossipJabber         *jabber,
			 const gchar          *jid_str, 
			 GossipVCardCallback   callback,
			 gpointer              user_data,
			 GError              **error)
{
	LmConnection       *connection;
	LmMessage          *m;
	LmMessageNode      *node;
	LmMessageHandler   *handler;
	GossipJID          *jid;
	GossipCallbackData *data;
	const gchar        *jid_without_resource;

	connection = gossip_jabber_get_connection (jabber);

	jid = gossip_jid_new (jid_str);
	jid_without_resource = gossip_jid_get_without_resource (jid);

	DEBUG_MSG (("Protocol: Requesting VCard, JID:'%s'", jid_without_resource));

	m = lm_message_new (jid_without_resource, 
			    LM_MESSAGE_TYPE_IQ);

	gossip_jid_unref (jid);

	node = lm_message_node_add_child (m->node, "vCard", NULL);
	lm_message_node_set_attribute (node, "xmlns", "vcard-temp");

	data = g_new0 (GossipCallbackData, 1);
	data->callback = callback;
	data->user_data = user_data;
	
	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_vcard_get_cb,
					  data, 
					  g_free);
	
	if (!lm_connection_send_with_reply (connection, m, handler, error)) {
		lm_message_unref (m);
		lm_message_handler_unref (handler);
		return FALSE;
	}

	/* FIXME: Set a timeout */
	
	lm_message_unref (m);
	lm_message_handler_unref (handler);

	return TRUE;
}

static LmHandlerResult 
jabber_vcard_set_cb (LmMessageHandler   *handler,
		     LmConnection       *connection,
		     LmMessage          *m,
		     GossipCallbackData *data)
{
	GossipResultCallback callback;

	if (!data || !data->callback) {
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}

	callback = data->callback;
	(callback) (GOSSIP_RESULT_OK, data->user_data);

	DEBUG_MSG (("Protocol: Setting presence after vcard"));
	
	/* Send our current presence to indicate the avatar has changed */
	gossip_jabber_send_presence (GOSSIP_JABBER (data->internal_data), NULL);
	
	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

gboolean
gossip_jabber_vcard_set (GossipJabber          *jabber,
			 GossipVCard           *vcard,
			 GossipResultCallback   callback,
			 gpointer               user_data,
			 GError               **error)
{
	LmConnection       *connection;
	LmMessage          *m;
	LmMessageNode      *node;
	LmMessageHandler   *handler;
	GossipCallbackData *data;
	gboolean            result;
	const guchar       *avatar;
	gsize		    avatar_size;

	connection = gossip_jabber_get_connection (jabber);

	m = lm_message_new_with_sub_type (NULL,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);

	node = lm_message_node_add_child (m->node, "vCard", NULL);
	lm_message_node_set_attribute (node, "xmlns", "vcard-temp");

	lm_message_node_add_child (node, "FN", gossip_vcard_get_name (vcard));
	lm_message_node_add_child (node, "NICKNAME", 
				   gossip_vcard_get_nickname (vcard));
	lm_message_node_add_child (node, "URL", gossip_vcard_get_url (vcard));
	lm_message_node_add_child (node, "EMAIL",
				   gossip_vcard_get_email (vcard));
	lm_message_node_add_child (node, "DESC", 
				   gossip_vcard_get_description (vcard)); 
				   
	avatar = gossip_vcard_get_avatar (vcard, &avatar_size);
	if (avatar != NULL) {
		gchar *avatar_encoded;
		
		node = lm_message_node_add_child (node, "PHOTO", NULL);
		lm_message_node_add_child (node, "TYPE", "image/png"); 

		avatar_encoded = gossip_base64_encode (avatar, avatar_size);
		lm_message_node_add_child (node, "BINVAL", avatar_encoded);
		g_free (avatar_encoded);
	}

	data = g_new0 (GossipCallbackData, 1);

	data->callback = callback;
	data->user_data = user_data;
	data->internal_data = jabber;
	
	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_vcard_set_cb,
					  data, 
					  g_free);
 
	result = lm_connection_send_with_reply (connection, m, handler, error);
	
	lm_message_unref (m);
	lm_message_handler_unref (handler);

	return result;
}

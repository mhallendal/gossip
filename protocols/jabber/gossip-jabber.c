/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004-2005 Imendio AB
 * Copyright (C) 2004 Martyn Russell <mr@gnome.org>
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
#include <string.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>

#include <libgossip/gossip-account.h>
#include <libgossip/gossip-contact.h>
#include <libgossip/gossip-vcard.h>
#include <libgossip/gossip-utils.h>
#include <libgossip/gossip-chatroom-provider.h>
#include <libgossip/gossip-ft-provider.h>

#include "gossip-jabber-chatrooms.h"
#include "gossip-jabber-ft.h"
#include "gossip-jabber-vcard.h"
#include "gossip-jabber-services.h"
#include "gossip-jabber-utils.h"
#include "gossip-transport-accounts.h"
#include "gossip-jabber.h"
#include "gossip-jabber-private.h"

#define d(x)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
						    GOSSIP_TYPE_JABBER, \
						    GossipJabberPriv))

#define XMPP_VERSION_XMLNS  "jabber:iq:version"
#define XMPP_ROSTER_XMLNS   "jabber:iq:roster"
#define XMPP_REGISTER_XMLNS "jabber:iq:register"

/* 3.5 minutes, we use this because if the port is just wrong then it
   will timeout before then with that error */
#define CONNECT_TIMEOUT     210 


struct _GossipJabberPriv {
	LmConnection          *connection;
	
	GossipContact         *contact;
	GossipAccount         *account;
	GossipPresence        *presence;
	GossipVCard           *vcard;

	GHashTable            *contacts;

	/* extended parts */
	GossipJabberChatrooms *chatrooms;
	GossipJabberFTs       *fts;

	/* used to hold a list of composing message ids, this is so we
	   can send the cancelation to the last message id */
	GHashTable            *composing_ids;

	/* transport stuff... is this in the right place? */
	GossipTransportAccountList *account_list;

	LmMessageHandler      *subscription_handler;

	guint                  connection_timeout_id;
	gboolean               disconnect_request;
};


typedef struct {
	GossipJabber     *jabber;
	LmMessageHandler *message_handler;

	GossipRegisterCallback callback;
	gpointer          user_data;
} RegisterAccountData;


typedef struct {
	GossipJabber     *jabber;
	gchar            *group;
	gchar            *new_name;
} RenameGroupData;


typedef struct {
	GossipJabber     *jabber;
	GossipContact    *contact;
} VCardData;


static void             gossip_jabber_class_init            (GossipJabberClass            *klass);
static void             gossip_jabber_init                  (GossipJabber                 *jabber);
static void             jabber_finalize                     (GObject                      *obj);
static void             jabber_setup                        (GossipProtocol               *protocol,
							     GossipAccount                *account);
static void             jabber_setup_connection             (GossipJabber                 *jabber);
static void             jabber_login                        (GossipProtocol               *protocol);
static gboolean         jabber_login_timeout_cb             (GossipJabber                 *jabber);
static void             jabber_logout                       (GossipProtocol               *protocol);
static gboolean         jabber_logout_contact_foreach       (gpointer                      key,
							     GossipContact                *contact,
							     GossipJabber                 *jabber);
static void             jabber_error                        (GossipProtocol               *protocol,
							     GossipProtocolError           code,
							     const gchar                  *reason);
static void             jabber_connection_open_cb           (LmConnection                 *connection,
							     gboolean                      result,
							     GossipJabber                 *jabber);
static void             jabber_connection_auth_cb           (LmConnection                 *connection,
							     gboolean                      result,
							     GossipJabber                 *jabber);
static void             jabber_disconnect_cb                (LmConnection                 *connection,
							     LmDisconnectReason            reason,
							     GossipJabber                 *jabber);
static LmSSLResponse    jabber_ssl_status_cb                (LmConnection                 *connection,
							     LmSSLStatus                   status,
							     GossipJabber                 *jabber);
static RegisterAccountData *
                        jabber_register_account_new         (GossipJabber                 *jabber,
							     GossipRegisterCallback        callback,
							     gpointer                      user_data);
static void             jabber_register_account_free        (RegisterAccountData          *ra);
static gboolean         jabber_register_account             (GossipProtocol               *protocol,
							     GossipAccount                *account,
							     GossipVCard                  *vcard,
							     GossipRegisterCallback        callback,
							     gpointer                      user_data,
							     GError                      **error);
static void             jabber_register_connection_open_cb  (LmConnection                 *connection,
							     gboolean                      result,
							     RegisterAccountData          *ra);
static const gchar *    jabber_register_error_to_str        (gint                          error_code);
static LmHandlerResult  jabber_register_message_handler     (LmMessageHandler             *handler,
							     LmConnection                 *conn,
							     LmMessage                    *m,
							     RegisterAccountData          *ra);
static gboolean         jabber_is_connected                 (GossipProtocol               *protocol);
static gboolean         jabber_is_valid_username            (GossipProtocol               *protocol,
							     const gchar                  *username);
static gboolean         jabber_is_ssl_supported             (GossipProtocol               *protocol);
static const gchar *    jabber_get_example_username         (GossipProtocol               *protocol);
static gchar *          jabber_get_default_server           (GossipProtocol               *protocol,
							     const gchar                  *username);
static guint16          jabber_get_default_port             (GossipProtocol               *protocol,
							     gboolean                      use_ssl);
static void             jabber_send_message                 (GossipProtocol               *protocol,
							     GossipMessage                *message);
static void             jabber_send_composing               (GossipProtocol               *protocol,
							     GossipContact                *contact,
							     gboolean                      typing);
static void             jabber_set_presence                 (GossipProtocol               *protocol,
							     GossipPresence               *presence);
static void             jabber_set_subscription             (GossipProtocol               *protocol,
							     GossipContact                *contact,
							     gboolean                      subscribed);
static gboolean         jabber_set_vcard                    (GossipProtocol               *protocol,
							     GossipVCard                  *vcard,
							     GossipResultCallback          callback,
							     gpointer                      user_data,
							     GError                      **error);
static GossipContact *  jabber_contact_find                 (GossipProtocol               *protocol,
							     const gchar                  *id);
static void             jabber_contact_add                  (GossipProtocol               *protocol,
							     const gchar                  *id,
							     const gchar                  *name,
							     const gchar                  *group,
							     const gchar                  *message);
static void             jabber_contact_rename               (GossipProtocol               *protocol,
							     GossipContact                *contact,
							     const gchar                  *new_name);
static void             jabber_contact_remove               (GossipProtocol               *protocol,
							     GossipContact                *contact);
static void             jabber_contact_update               (GossipProtocol               *protocol,
							     GossipContact                *contact);
static void             jabber_contact_vcard                (GossipJabber                 *jabber,
							     GossipContact                *contact);
static void             jabber_contact_vcard_cb             (GossipResult                  result,
							     GossipVCard                  *vcard,
							     VCardData                    *vd);
static void             jabber_group_rename                 (GossipProtocol               *protocol,
							     const gchar                  *group,
							     const gchar                  *new_name);
static void             jabber_group_rename_foreach_cb      (const gchar                  *jid,
							     GossipContact                *contact,
							     RenameGroupData              *rg);
static GossipPresence * jabber_get_presence                 (LmMessage                    *message);
static const GList *    jabber_get_contacts                 (GossipProtocol               *protocol);
static GossipContact *  jabber_get_own_contact              (GossipProtocol               *protocol);
static const gchar *    jabber_get_active_resource          (GossipProtocol               *protocol,
							     GossipContact                *contact);
static GList *          jabber_get_groups                   (GossipProtocol               *protocol);
static void             jabber_get_groups_foreach_cb        (const gchar                  *jid,
							     GossipContact                *contact,
							     GList                       **list);
static gboolean         jabber_get_vcard                    (GossipProtocol               *protocol,
							     GossipContact                *contact,
							     GossipVCardCallback           callback,
							     gpointer                      user_data,
							     GError                      **error);
static gboolean         jabber_get_version                  (GossipProtocol               *protocol,
							     GossipContact                *contact,
							     GossipVersionCallback         callback,
							     gpointer                      user_data,
							     GError                      **error);
static void             jabber_set_proxy                    (LmConnection                 *conn);
static LmHandlerResult  jabber_message_handler              (LmMessageHandler             *handler,
							     LmConnection                 *conn,
							     LmMessage                    *message,
							     GossipJabber                 *jabber);
static LmHandlerResult  jabber_presence_handler             (LmMessageHandler             *handler,
							     LmConnection                 *conn,
							     LmMessage                    *message,
							     GossipJabber                 *jabber);
static LmHandlerResult  jabber_iq_query_handler             (LmMessageHandler             *handler,
							     LmConnection                 *conn,
							     LmMessage                    *message,
							     GossipJabber                 *jabber);
static LmHandlerResult  jabber_subscription_message_handler (LmMessageHandler             *handler,
							     LmConnection                 *connection,
							     LmMessage                    *m,
							     GossipJabber                 *jabber);
static void             jabber_request_version              (GossipJabber                 *jabber,
							     LmMessage                    *m);
static void             jabber_request_roster               (GossipJabber                 *jabber,
							     LmMessage                    *m);
static void             jabber_request_unknown              (GossipJabber                 *jabber,
							     LmMessage                    *m);




/* chatrooms */
static void             jabber_chatroom_init                (GossipChatroomProviderIface  *iface);
static GossipChatroomId jabber_chatroom_join                (GossipChatroomProvider       *provider,
							     GossipChatroom               *chatroom,
							     GossipChatroomJoinCb          callback,
							     gpointer                      user_data);
static void             jabber_chatroom_cancel              (GossipChatroomProvider       *provider,
							     GossipChatroomId              id);
static void             jabber_chatroom_send                (GossipChatroomProvider       *provider,
							     GossipChatroomId              id,
							     const gchar                  *message);
static void             jabber_chatroom_change_topic        (GossipChatroomProvider       *provider,
							     GossipChatroomId              id,
							     const gchar                  *new_topic);
static void             jabber_chatroom_change_nick         (GossipChatroomProvider       *provider,
							     GossipChatroomId              id,
							     const gchar                  *new_nick);
static void             jabber_chatroom_leave               (GossipChatroomProvider       *provider,
							     GossipChatroomId              id);
static const gchar *    jabber_chatroom_get_room_name       (GossipChatroomProvider       *provider,
							     GossipChatroomId              id);
static void             jabber_chatroom_invite              (GossipChatroomProvider       *provider,
							     GossipChatroomId              id,
							     const gchar                  *contact_id,
							     const gchar                  *invite);
static void             jabber_chatroom_invite_accept       (GossipChatroomProvider       *provider,
							     GossipChatroomJoinCb          callback,
							     const gchar                  *nickname,
							     const gchar                  *invite_id);
static GList *          jabber_chatroom_get_rooms           (GossipChatroomProvider       *provider);


/* fts */
static void             jabber_ft_init                      (GossipFTProviderIface        *iface);
static GossipFTId       jabber_ft_send                      (GossipFTProvider             *provider,
							     GossipContact                *contact,
							     const gchar                  *file);
static void             jabber_ft_cancel                    (GossipFTProvider             *provider,
							     GossipFTId                    id);
static void             jabber_ft_accept                    (GossipFTProvider             *provider,
							     GossipFTId                    id);
static void             jabber_ft_decline                   (GossipFTProvider             *provider,
							     GossipFTId                    id);


extern GConfClient *gconf_client;


G_DEFINE_TYPE_WITH_CODE (GossipJabber, gossip_jabber, GOSSIP_TYPE_PROTOCOL,
			 G_IMPLEMENT_INTERFACE (GOSSIP_TYPE_CHATROOM_PROVIDER,
						jabber_chatroom_init);
			 G_IMPLEMENT_INTERFACE (GOSSIP_TYPE_FT_PROVIDER,
						jabber_ft_init));


static void
gossip_jabber_class_init (GossipJabberClass *klass)
{
	GObjectClass        *object_class = G_OBJECT_CLASS (klass);
	GossipProtocolClass *protocol_class = GOSSIP_PROTOCOL_CLASS (klass);
	
	object_class->finalize = jabber_finalize;

	protocol_class->setup                = jabber_setup;
	protocol_class->login                = jabber_login;
	protocol_class->logout               = jabber_logout;
	protocol_class->is_connected         = jabber_is_connected;
	protocol_class->is_valid_username    = jabber_is_valid_username;
	protocol_class->is_ssl_supported     = jabber_is_ssl_supported;
	protocol_class->get_example_username = jabber_get_example_username;
	protocol_class->get_default_server   = jabber_get_default_server;
	protocol_class->get_default_port     = jabber_get_default_port;
	protocol_class->set_presence         = jabber_set_presence;
	protocol_class->set_subscription     = jabber_set_subscription;
	protocol_class->set_vcard            = jabber_set_vcard;
	protocol_class->send_message         = jabber_send_message;
	protocol_class->send_composing       = jabber_send_composing;
        protocol_class->find_contact         = jabber_contact_find;
        protocol_class->add_contact          = jabber_contact_add;
        protocol_class->rename_contact       = jabber_contact_rename;
        protocol_class->remove_contact       = jabber_contact_remove;
	protocol_class->update_contact       = jabber_contact_update;
 	protocol_class->rename_group         = jabber_group_rename;
	protocol_class->get_contacts         = jabber_get_contacts;
	protocol_class->get_own_contact      = jabber_get_own_contact;
	protocol_class->get_active_resource  = jabber_get_active_resource;
 	protocol_class->get_groups           = jabber_get_groups;
	protocol_class->get_vcard            = jabber_get_vcard;
	protocol_class->get_version          = jabber_get_version;
	protocol_class->register_account     = jabber_register_account;

	g_type_class_add_private (object_class, sizeof (GossipJabberPriv));
}

static void
gossip_jabber_init (GossipJabber *jabber)
{
 	GossipJabberPriv *priv; 

	priv = GET_PRIV (jabber);

	priv->contacts = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       (GDestroyNotify) g_free,
				       (GDestroyNotify) g_object_unref);

	priv->composing_ids = 
		g_hash_table_new_full (gossip_contact_hash,
				       gossip_contact_equal,
				       (GDestroyNotify) g_object_unref,
				       (GDestroyNotify) g_free);
}
	
static void
jabber_finalize (GObject *obj)
{
 	GossipJabber     *jabber;
 	GossipJabberPriv *priv;

 	jabber = GOSSIP_JABBER (obj);
 	priv = GET_PRIV (jabber);

	if (priv->account) {
		g_object_unref (priv->account);
	}

	if (priv->vcard) {
		g_object_unref (priv->vcard);
	}
	
	g_hash_table_destroy (priv->contacts);

	/* finalize extended modules */
 	gossip_jabber_chatrooms_finalize (priv->chatrooms);
	gossip_jabber_ft_finalize (priv->fts);

 	g_hash_table_destroy (priv->composing_ids);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}
	
#ifdef USE_TRANSPORTS
 	gossip_transport_account_list_free (priv->account_list);
#endif	
/*  	g_free (priv); */
}

static void
jabber_setup (GossipProtocol *protocol,
 	      GossipAccount  *account)
{
 	GossipJabber     *jabber;
 	GossipJabberPriv *priv;
 	LmMessageHandler *handler;
	const gchar      *id;
	GossipJID        *jid;
	
 	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
  	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));
	
 	jabber = GOSSIP_JABBER (protocol);
 	priv = GET_PRIV (jabber);
 	
 	priv->account = g_object_ref (account);
 
 	priv->contact = gossip_contact_new (GOSSIP_CONTACT_TYPE_USER,
					    priv->account);
 
	id = gossip_account_get_id (account);
	jid = gossip_jid_new (id);

	g_object_set (priv->contact,
		      "id", id,          /* we need FULL JID here */
		      "name", id,
		      NULL);

	priv->connection = lm_connection_new (gossip_account_get_server (account));

	/* setup the connection to send keep alive messages every 30 seconds */
        lm_connection_set_keep_alive_rate (priv->connection, 30);

	lm_connection_set_disconnect_function (priv->connection,
					       (LmDisconnectFunction) jabber_disconnect_cb,
					       jabber, NULL);

	lm_connection_set_port (priv->connection, gossip_account_get_port (account));
	lm_connection_set_jid (priv->connection, gossip_jid_get_without_resource (jid));

	gossip_jid_unref (jid);

	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_message_handler,
					  jabber, NULL);
	lm_connection_register_message_handler (priv->connection,
						handler,
						LM_MESSAGE_TYPE_MESSAGE,
						LM_HANDLER_PRIORITY_NORMAL);
	lm_message_handler_unref (handler);

	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_presence_handler,
					  jabber, NULL);
	lm_connection_register_message_handler (priv->connection,
						handler,
						LM_MESSAGE_TYPE_PRESENCE,
						LM_HANDLER_PRIORITY_NORMAL);
	lm_message_handler_unref (handler);

	handler = lm_message_handler_new ((LmHandleMessageFunction) jabber_iq_query_handler,
					  jabber, NULL);
	lm_connection_register_message_handler (priv->connection,
						handler,
						LM_MESSAGE_TYPE_IQ,
						LM_HANDLER_PRIORITY_NORMAL);
	lm_message_handler_unref (handler);

	/* initiate extended modules */
	priv->chatrooms = gossip_jabber_chatrooms_init (jabber);
	priv->fts = gossip_jabber_ft_init (jabber);

#ifdef USE_TRANSPORTS
	/* initialise the jabber accounts module which is necessary to
	   watch roster changes to know which services are set up */
	priv->account_list = gossip_transport_account_list_new (jabber);
#endif
}

static void
jabber_setup_connection (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	priv = GET_PRIV (jabber);

	priv->disconnect_request = FALSE;

	if (gossip_account_get_use_ssl (priv->account)) {
		LmSSL *ssl;

		d(g_print ("Protocol: Using SSL\n"));

		ssl = lm_ssl_new (NULL,
				  (LmSSLFunction) jabber_ssl_status_cb,
				  jabber, NULL);

		lm_connection_set_ssl (priv->connection, ssl);
		lm_connection_set_port (priv->connection,
					gossip_account_get_port (priv->account));

		lm_ssl_unref (ssl);
	}

	if (gossip_account_get_use_proxy (priv->account)) {
		d(g_print ("Protocol: Using proxy\n"));

		jabber_set_proxy (priv->connection);
	} else {
		/* FIXME: Just pass NULL when Loudmouth > 0.17.1 */
		LmProxy *proxy;

		proxy = lm_proxy_new (LM_PROXY_TYPE_NONE);
		lm_connection_set_proxy (priv->connection, proxy);
		lm_proxy_unref (proxy);
	}
}

static void
jabber_login (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GError           *error = NULL;
	gboolean          result;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	
	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	d(g_print ("Protocol: Logging in Jabber\n"));

	priv->presence = gossip_presence_new ();
	gossip_presence_set_state (priv->presence, 
				   GOSSIP_PRESENCE_STATE_AVAILABLE);
	gossip_jabber_chatrooms_set_presence (priv->chatrooms, priv->presence);

	jabber_setup_connection (jabber);

	if (!priv->connection) {
		jabber_error (GOSSIP_PROTOCOL (jabber), 
			      GossipProtocolErrorNoConnection,
			      _("Could not open connection"));
		return;
	}

	result = lm_connection_open (priv->connection, 
				     (LmResultFunction) jabber_connection_open_cb,
				     jabber, NULL, &error);


	if (result && !error) {
		/* FIXME: add timeout incase we get nothing back from
		   Loudmouth, this happens with current CVS loudmouth
		   1.01 where you connect to port 5222 using SSL, the
		   error is not reflecting back into Gossip so we just
		   hang around waiting. */
		priv->connection_timeout_id = g_timeout_add (CONNECT_TIMEOUT * 1000,
							     (GSourceFunc) jabber_login_timeout_cb,
							     jabber);
		
		return;
	}

	if (error->code == 1 && 
	    strcmp (error->message, "getaddrinfo() failed") == 0) {
		/* host lookup failed */
		jabber_error (GOSSIP_PROTOCOL (jabber), 
			      GossipProtocolErrorNoSuchHost,
			      _("Could not find the server you wanted to use"));
	}

	g_error_free (error);
}

static gboolean 
jabber_login_timeout_cb (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	priv = GET_PRIV (jabber);

	priv->connection_timeout_id = 0;

	jabber_error (GOSSIP_PROTOCOL (jabber), 
		      GossipProtocolErrorTimedOut,
		      _("Connection to the server failed."));

	return FALSE;
}

static void
jabber_logout (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	priv->disconnect_request = TRUE;

	if (priv->connection && 
	    lm_connection_is_open (priv->connection)) {
		lm_connection_close (priv->connection, NULL);
	}
}

static gboolean
jabber_logout_contact_foreach (gpointer       key,
			       GossipContact *contact,
			       GossipJabber  *jabber)
{
	g_signal_emit_by_name (jabber, "contact-removed", contact);
	return TRUE;
}

static void
jabber_error (GossipProtocol      *protocol, 
	      GossipProtocolError  code,
	      const gchar         *reason)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GError           *error;
	static GQuark     quark = 0;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (reason != NULL);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (!quark) {
		quark = g_quark_from_static_string ("gossip-jabber");
	}
	
	error = g_error_new_literal (quark, code, reason);
	g_signal_emit_by_name (protocol, "error", priv->account, error);
 	g_error_free (error); 
}

static void
jabber_connection_open_cb (LmConnection *connection,
			   gboolean      result,
			   GossipJabber *jabber)
{
	GossipJabberPriv *priv;
	GossipAccount    *account;
	const gchar      *account_password;
	const gchar      *jid_str;
	gchar            *id;
	const gchar      *resource;
	gchar            *password = NULL;
	
	priv = GET_PRIV (jabber);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	if (priv->disconnect_request) {
		/* this is so we go no further that way we don't issue
		   warnings for connections we stopped ourselves */
		return;
	}

	if (result == FALSE) {
		jabber_error (GOSSIP_PROTOCOL (jabber), 
			      GossipProtocolErrorNoConnection,
			      _("Could not open connection"));
		return;
	}

	d(g_print ("Protocol: Connection open!\n"));

	account = priv->account;
	account_password = gossip_account_get_password (account);

	if (!account_password || strlen (account_password) < 1) {
		/* FIXME: Ask the user for the password */
		g_signal_emit_by_name (jabber, "get-password", 
				       account, &password);
	} else {
		password = g_strdup (account_password);
	}
	 
	/* FIXME: Decide on Resource */
	jid_str = gossip_account_get_id (account);

	id = gossip_jid_string_get_part_name (jid_str);
	resource = gossip_jid_string_get_part_resource (jid_str);

	d(g_print ("Protocol: Using JabberID:'%s'\n", jid_str));
	
	lm_connection_authenticate (priv->connection,
				    id,
				    password, 
				    resource,
				    (LmResultFunction) jabber_connection_auth_cb,
				    jabber, NULL, NULL);

	g_free (id);
	g_free (password);
}

static void
jabber_connection_auth_cb (LmConnection *connection,
			   gboolean      result,
			   GossipJabber *jabber)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
	LmMessageNode    *node;
	
	priv = GET_PRIV (jabber);
	
	if (result == FALSE) {
		jabber_error (GOSSIP_PROTOCOL (jabber), 
			      GossipProtocolErrorAuthFailed,
			      _("Authentication failed"));
		return;
	}

	d(g_print ("Protocol: Connection logged in!\n"));

	/* Request roster */
	m = lm_message_new_with_sub_type (NULL,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_GET);
	node = lm_message_node_add_child (m->node, "query", NULL);
	lm_message_node_set_attributes (node, 
					"xmlns", XMPP_ROSTER_XMLNS,
					NULL);
	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	/* Notify others that we are online */
	m = lm_message_new_with_sub_type (NULL,
					  LM_MESSAGE_TYPE_PRESENCE,
					  LM_MESSAGE_SUB_TYPE_AVAILABLE);
	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	g_signal_emit_by_name (jabber, "logged-in", priv->account);

	if (priv->vcard) {
		const gchar *name;

		name = gossip_vcard_get_name (priv->vcard);
		if (name && strlen (name) > 0) {
			gossip_contact_set_name (priv->contact, name);
			g_signal_emit_by_name (jabber, 
					       "contact-updated", 
					       priv->contact);
		}

		/* Set the vcard waiting to be sent to our jabber server once
		 * connected. 
		 */
		gossip_jabber_vcard_set (priv->connection,
					 priv->vcard,
					 NULL, NULL, NULL);
		
		g_object_unref (priv->vcard);
		priv->vcard = NULL;
	} else {
		/* Request our vcard so we know what our nick name is to use
		 * in chats windows, etc.
		 */
		jabber_contact_vcard (jabber, priv->contact);
	}

}

static void 
jabber_disconnect_cb (LmConnection       *connection,
		      LmDisconnectReason  reason,
		      GossipJabber       *jabber)
{
	GossipJabberPriv *priv;

	priv = GET_PRIV (jabber);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	g_signal_emit_by_name (jabber, "logged-out", priv->account);

	/* signal removal of each contact */
	if (priv->contacts) {
		g_hash_table_foreach_remove (priv->contacts,
					     (GHRFunc) jabber_logout_contact_foreach,
					     jabber);
	}
}

static LmSSLResponse 
jabber_ssl_status_cb (LmConnection *connection,
		      LmSSLStatus   status,
		      GossipJabber *jabber)
{
	const gchar *str = "";

	switch (status) {
	case LM_SSL_STATUS_NO_CERT_FOUND: 
		str = "No certificate found";
		break;
	case LM_SSL_STATUS_UNTRUSTED_CERT: 
		str = "Untrusted certificate";
		break;
	case LM_SSL_STATUS_CERT_EXPIRED: 
		str = "Certificate expired";
		break;
	case LM_SSL_STATUS_CERT_NOT_ACTIVATED: 
		str = "Certificate not activated";
		break;
	case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH: 
		str = "Certificate host mismatch";
		break;
	case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH: 
		str = "Certificate fingerprint mismatch";
		break;
	case LM_SSL_STATUS_GENERIC_ERROR:
		str = "Generic error";
		break;
	default: 
		str = "Unknown error:";
		break;
	}

	d(g_print ("Protocol: %s\n", str));

	return LM_SSL_RESPONSE_CONTINUE;
}

static RegisterAccountData *
jabber_register_account_new (GossipJabber           *jabber,
			     GossipRegisterCallback  callback,
			     gpointer                user_data)
{
	RegisterAccountData *ra;

	ra = g_new0 (RegisterAccountData, 1);
	
	ra->jabber = g_object_ref (jabber);

	ra->callback = callback;
	ra->user_data = user_data;

	return ra;
}

static void
jabber_register_account_free (RegisterAccountData *ra)
{
	if (ra->jabber) {
		g_object_unref (ra->jabber);
	}

	/* FIXME: should we clean up the message handler or does
	 * closing the connection do this for us? 
	 */

	g_free (ra);
}

static gboolean            
jabber_register_account (GossipProtocol               *protocol,
			 GossipAccount                *account,
			 GossipVCard                  *vcard,
			 GossipRegisterCallback        callback,
			 gpointer                      user_data,
			 GError                      **error)
{
	GossipJabber         *jabber;
	GossipJabberPriv     *priv;
	RegisterAccountData  *ra;
	gboolean              result;
	const gchar          *error_message = NULL; 

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

	d(g_print ("Protocol: Registering with Jabber server...\n"));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	jabber_setup_connection (jabber);

	if (priv->connection) {
		ra = jabber_register_account_new (jabber, callback, user_data); 
		
		result = lm_connection_open (priv->connection, 
					     (LmResultFunction) jabber_register_connection_open_cb,
					     ra, NULL, NULL);

		if (!result) {
			error_message = _("Connection could not be opened");
			jabber_register_account_free (ra); 
		} else {
			/* Save vcard information so the next time we connect we can
			 * set it, this could be done better */  
			priv->vcard = g_object_ref (vcard);
		}
	} else {
		error_message = _("Connection could not be created");
	}
	
	if (error_message) {
		d(g_print ("Protocol: %s\n", error_message));

		if (callback) {
			(callback) (GOSSIP_RESULT_ERROR_REGISTRATION, 
				    error_message, 
				    user_data);
		}
	}

	return TRUE;
}

static void
jabber_register_connection_open_cb (LmConnection        *connection,
				    gboolean             result,
				    RegisterAccountData *ra)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
	LmMessageNode    *node;
	const gchar      *str = NULL;
	gboolean          ok = FALSE;

	priv = GET_PRIV (ra->jabber);
	
	if (result == FALSE) {
		str = _("Connection could not be opened");
		d(g_print ("Protocol: %s\n", str));

		if (ra->callback) {
			(ra->callback) (GOSSIP_RESULT_ERROR_REGISTRATION, 
					str, ra->user_data);
		}

		jabber_register_account_free (ra);
		return;
	} else {
		d(g_print ("Protocol: Connection open!\n"));
	}

	ra->message_handler = lm_message_handler_new ((LmHandleMessageFunction) 
						      jabber_register_message_handler,
						      ra, NULL);

        m = lm_message_new_with_sub_type (gossip_account_get_server (priv->account),
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_MESSAGE_SUB_TYPE_SET);

	lm_message_node_add_child (m->node, "query", NULL);
	node = lm_message_node_get_child (m->node, "query");

        lm_message_node_set_attribute (node, "xmlns", XMPP_REGISTER_XMLNS);
	
	lm_message_node_add_child (node, "username", gossip_account_get_id (priv->account));
	lm_message_node_add_child (node, "password", gossip_account_get_password (priv->account));

        ok = lm_connection_send_with_reply (connection, m, 
					    ra->message_handler, NULL);
        lm_message_unref (m);

	if (!ok) {
		str = _("Couldn't send message!");
		d(g_print ("Protocol: %s\n", str));

		if (ra->callback) {
			(ra->callback) (GOSSIP_RESULT_ERROR_REGISTRATION, 
					str, ra->user_data);
		}

		jabber_register_account_free (ra);
		return;
	}

	d(g_print ("Protocol: Sent registration details\n"));
}

static const gchar *
jabber_register_error_to_str (gint error_code) 
{
	switch (error_code) {
	case 302: return _("Redirect");
	case 400: return _("Bad Request");
	case 401: return _("Not Authorized");
	case 402: return _("Payment Required");
	case 403: return _("Forbidden");
	case 404: return _("Not Found");
	case 405: return _("Not Allowed");
	case 406: return _("Not Acceptable");
	case 407: return _("Registration Required");
	case 408: return _("Request Timeout");
	case 409: return _("Conflict");
	case 500: return _("Internal Server Error");
	case 501: return _("Not Implemented");
	case 502: return _("Remote Server Error");
	case 503: return _("Service Unavailable");
	case 504: return _("Remote Server Timeout");
	case 510: return _("Disconnected");
	};

	return _("Unknown");
}

static LmHandlerResult
jabber_register_message_handler (LmMessageHandler     *handler,
				 LmConnection         *connection,
				 LmMessage            *m,
				 RegisterAccountData  *ra)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	const gchar      *error_code = NULL;
	const gchar      *error_reason = NULL;
	GossipResult      result = GOSSIP_RESULT_OK;

	priv = GET_PRIV (ra->jabber);

	node = lm_message_node_get_child (m->node, "error");	
	if (node) {
		result = GOSSIP_RESULT_ERROR_REGISTRATION;

		error_code = lm_message_node_get_attribute (node, "code");
		error_reason = jabber_register_error_to_str (atoi (error_code));

		d(g_print ("Protocol: Registration failed with error:%s->'%s'\n",
			   error_code, error_reason));	
	} else {
		d(g_print ("Protocol: Registration success\n"));
	}

	if (ra->callback) {
		(ra->callback) (result, 
				error_reason, 
				ra->user_data);
	}
	
	if (result != GOSSIP_RESULT_OK) {
		jabber_register_account_free (ra);
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}

	/* Clean up */
	jabber_register_account_free (ra);

	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
jabber_is_connected (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), FALSE);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (priv->connection == NULL) {
		return FALSE;
	}

	return lm_connection_is_authenticated (priv->connection);
}

static gboolean
jabber_is_valid_username (GossipProtocol *protocol, 
			  const gchar    *username)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), TRUE);
	g_return_val_if_fail (username != NULL, FALSE);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	return gossip_jid_string_is_valid (username, TRUE);
}

static gboolean
jabber_is_ssl_supported (GossipProtocol *protocol)
{
	return lm_ssl_is_supported ();
}

static const gchar *
jabber_get_example_username (GossipProtocol *protocol)
{
	return "user@jabber.org/laptop";
}

static gchar *
jabber_get_default_server (GossipProtocol *protocol,
			   const gchar    *username)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv; 
	GossipJID        *jid;
	const gchar      *str;
	gchar            *server;

	g_return_val_if_fail (username != NULL, NULL);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	jid = gossip_jid_new (username);
	str = gossip_jid_get_part_host (jid);
	server = g_strdup (str);
	gossip_jid_unref (jid);

	return server;
}

static guint16
jabber_get_default_port (GossipProtocol *protocol,
			 gboolean        use_ssl)
{
	if (use_ssl) {
		return LM_CONNECTION_DEFAULT_PORT_SSL;
	}

	return LM_CONNECTION_DEFAULT_PORT;
}

static void
jabber_send_message (GossipProtocol *protocol, 
		     GossipMessage  *message)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GossipContact    *recipient;
	LmMessage        *m;
	const gchar      *recipient_id;
	const gchar      *resource;
	gchar            *jid_str;
	gboolean          new_contact;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	recipient = gossip_message_get_recipient (message);

	/* FIXME: Create a full JID (with resource) and send to that */
	recipient_id = gossip_contact_get_id (recipient);
	resource = gossip_message_get_explicit_resource (message);

	/* Getting contact from JID, this will add them to our
	 * contacts hash table if they don't exist too. 
	 */
	recipient = gossip_jabber_get_contact_from_jid (jabber, recipient_id, &new_contact);
	if (new_contact) {
		g_object_unref (recipient);
	}

	if (resource) {
		jid_str = g_strdup_printf ("%s/%s", recipient_id, resource);
	} else {
		jid_str = g_strdup (recipient_id);
	}

	d(g_print ("Protocol: Sending message to: '%s'\n", jid_str));
	
	m = lm_message_new_with_sub_type (jid_str,
					  LM_MESSAGE_TYPE_MESSAGE,
					  LM_MESSAGE_SUB_TYPE_CHAT);

	lm_message_node_add_child (m->node, "body",
				   gossip_message_get_body (message));

	/* if we have had a request for composing then we send the
	   other side composing details with every message */
	if (gossip_message_is_requesting_composing (message)) {
		LmMessageNode *node;

		node = lm_message_node_add_child (m->node, "x", NULL);
		lm_message_node_set_attribute (node, "xmlns", "jabber:x:event");
		lm_message_node_add_child (node, "composing", NULL);
	}

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	g_free (jid_str);
}

static void
jabber_send_composing (GossipProtocol *protocol,
		       GossipContact  *contact,
		       gboolean        typing)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	LmMessage        *m;
	LmMessageNode    *node;
	const gchar      *id = NULL;
	const gchar      *contact_id;
	const gchar      *resource = NULL;
	gchar            *jid_str;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	contact_id = gossip_contact_get_id (contact);

	d(g_print ("Protocol: Sending %s to contact:'%s'\n", 
		   typing ? "composing" : "not composing",
		   contact_id));
	
	/* FIXME: Create a full JID (with resource) and send to that */
/* 	resource = gossip_message_get_explicit_resource (message); */

	if (resource) {
		jid_str = g_strdup_printf ("%s/%s", contact_id, resource);
	} else {
		jid_str = g_strdup (contact_id);
	}

	m = lm_message_new_with_sub_type (jid_str,
					  LM_MESSAGE_TYPE_MESSAGE,
					  LM_MESSAGE_SUB_TYPE_CHAT);
	node = lm_message_node_add_child (m->node, "x", NULL);
	lm_message_node_set_attribute (node, "xmlns", "jabber:x:event");
	
	if (typing) {
		id = lm_message_node_get_attribute (m->node, "id"); 

		g_hash_table_insert (priv->composing_ids, 
				     g_object_ref (contact),
				     g_strdup (id));

		lm_message_node_add_child (node, "composing", NULL);
		lm_message_node_add_child (node, "id", id); 
	} else {
		id = g_hash_table_lookup (priv->composing_ids, contact);
		lm_message_node_add_child (node, "id", id); 

		if (id) {
			g_hash_table_remove (priv->composing_ids, contact);
		}
	}

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	g_free (jid_str);
}

static void
jabber_set_presence (GossipProtocol *protocol, 
		     GossipPresence *presence)
{
	GossipJabber        *jabber;
	GossipJabberPriv    *priv;
	LmMessage           *m;
	GossipPresenceState  state;
	const gchar         *show = NULL;
	const gchar         *priority;
	
	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (priv->presence) {
		g_object_unref (priv->presence);
	}

	priv->presence = g_object_ref (presence);
	
	m = lm_message_new_with_sub_type (NULL, 
					  LM_MESSAGE_TYPE_PRESENCE,
					  LM_MESSAGE_SUB_TYPE_AVAILABLE);

	state = gossip_presence_get_state (presence);
	show = gossip_jabber_presence_state_to_str (presence);
	
	switch (state) {
	case GOSSIP_PRESENCE_STATE_BUSY:
		priority = "40";
		break;
	case GOSSIP_PRESENCE_STATE_AWAY:
		priority = "30";
		break;
	case GOSSIP_PRESENCE_STATE_EXT_AWAY:
		priority = "0";
		break;
	default:
		priority = "50";
		break;
	}

	d(g_print ("Protocol: Settting presence to:'%s', status:'%s'\n", 
		   show, gossip_presence_get_status (presence)));

	if (show) {
		lm_message_node_add_child (m->node, "show", show);
	}
	lm_message_node_add_child (m->node, "priority", priority);
	
	if (gossip_presence_get_status (presence)) {
		lm_message_node_add_child (m->node, "status",
					   gossip_presence_get_status (presence));
	}
	
	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	gossip_jabber_chatrooms_set_presence (priv->chatrooms, presence);
}

static void
jabber_set_subscription (GossipProtocol *protocol,
			 GossipContact  *contact,
			 gboolean        subscribed)
{
	GossipJabber *jabber;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	jabber = GOSSIP_JABBER (protocol);

	d(g_print ("Protocol: Settting subscription for contact:'%s' as %s\n", 
		   gossip_contact_get_id (contact),
		   subscribed ? "subscribed" : "unsubscribed"));

	if (subscribed) {
		gossip_jabber_send_subscribed (jabber, contact);
	} else {
		gossip_jabber_send_unsubscribed (jabber, contact);
	}
}

static gboolean
jabber_set_vcard (GossipProtocol        *protocol,
		  GossipVCard           *vcard,
		  GossipResultCallback   callback,
		  gpointer               user_data,
		  GError               **error)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv; 

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	return gossip_jabber_vcard_set (priv->connection,
					vcard,
					callback, user_data,
					error);
}

static GossipContact *
jabber_contact_find (GossipProtocol *protocol,
		     const gchar    *id)
{
        GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GossipJID        *jid;
	GossipContact    *contact;

        jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	jid = gossip_jid_new (id);
	contact = g_hash_table_lookup (priv->contacts, 
				       gossip_jid_get_without_resource (jid));

	gossip_jid_unref (jid);

	return contact;
}

static void
jabber_contact_add (GossipProtocol *protocol,
                    const gchar    *id,
                    const gchar    *name,
                    const gchar    *group,
                    const gchar    *message)
{
        GossipJabber     *jabber;
        GossipJabberPriv *priv;
	LmMessage        *m;
        LmMessageNode    *node;

        jabber = GOSSIP_JABBER (protocol);
        priv = GET_PRIV (jabber);

        /* Request subscription */
        m = lm_message_new_with_sub_type (id, LM_MESSAGE_TYPE_PRESENCE,
                                          LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
        lm_message_node_add_child (m->node, "status", message);
        lm_connection_send (priv->connection, m, NULL);
        lm_message_unref (m);

        /* Add to roster */
        m = lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_IQ,
                                          LM_MESSAGE_SUB_TYPE_SET);
        node = lm_message_node_add_child (m->node, "query", NULL);
        lm_message_node_set_attributes (node,
                                        "xmlns", XMPP_ROSTER_XMLNS, NULL);
        node = lm_message_node_add_child (node, "item", NULL);
        lm_message_node_set_attributes (node,
                                        "jid", id,
                                        "subscription", "none",
                                        "ask", "subscribe",
                                        "name", name,
                                        NULL);
        if (group && strcmp (group, "") != 0) {
                lm_message_node_add_child (node, "group", group);
        }

        lm_connection_send (priv->connection, m, NULL);
        lm_message_unref (m);
}

static void
jabber_contact_rename (GossipProtocol *protocol,
                       GossipContact  *contact,
                       const gchar    *new_name)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	LmMessage        *m;
        LmMessageNode    *node;
	gchar            *escaped;
	GList            *l; 
	
	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	m = lm_message_new_with_sub_type (NULL, 
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);
	node = lm_message_node_add_child (m->node, "query", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_ROSTER_XMLNS,
					NULL);
	
	escaped = g_markup_escape_text (new_name, -1);

	node = lm_message_node_add_child (node, "item", NULL);
	lm_message_node_set_attributes (node, 
					"jid", gossip_contact_get_id (contact),
					"name", escaped,
					NULL);
        g_free (escaped);
	
        for (l = gossip_contact_get_groups (contact); l; l = l->next) {
                escaped = g_markup_escape_text ((const gchar *) l->data, -1);
                lm_message_node_add_child (node, "group", escaped);
                g_free (escaped);
	}	

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);
}

static void
jabber_contact_remove (GossipProtocol *protocol, 
		       GossipContact  *contact)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	LmMessage        *m;
        LmMessageNode    *node;
	
	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	m = lm_message_new_with_sub_type (NULL,
 					  LM_MESSAGE_TYPE_IQ,
 					  LM_MESSAGE_SUB_TYPE_SET);
	
 	node = lm_message_node_add_child (m->node, "query", NULL);
 	lm_message_node_set_attribute (node,
				       "xmlns",
				       XMPP_ROSTER_XMLNS);
	
 	node = lm_message_node_add_child (node, "item", NULL);
 	lm_message_node_set_attributes (node,
					"jid", gossip_contact_get_id (contact),
					"subscription", "remove",
					NULL);
 	
 	lm_connection_send (priv->connection, m, NULL);
 	lm_message_unref (m);

	m = lm_message_new_with_sub_type (gossip_contact_get_id (contact),
					  LM_MESSAGE_TYPE_PRESENCE,
					  LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);
}

static void
jabber_contact_update (GossipProtocol *protocol, 
		       GossipContact  *contact)
{
	/* we set the groups _and_ the name here, the rename function
	   will do exactly what we want to do so just call that */
	jabber_contact_rename (protocol, 
			       contact, 
			       gossip_contact_get_name (contact));
}

static void
jabber_contact_vcard (GossipJabber  *jabber,
		      GossipContact *contact)
{
	GossipJabberPriv *priv;
	VCardData        *vd;
	
	priv = GET_PRIV (jabber);

	vd = g_new0 (VCardData, 1);
	
	vd->jabber = g_object_ref (jabber);
	vd->contact = g_object_ref (contact);
	
	gossip_jabber_vcard_get (priv->connection,
				 gossip_contact_get_id (contact),
				 (GossipVCardCallback) jabber_contact_vcard_cb, 
				 vd, 
				 NULL);
}

static void
jabber_contact_vcard_cb (GossipResult  result,
			 GossipVCard  *vcard,
			 VCardData    *vd)
{
	if (result == GOSSIP_RESULT_OK) {
		const gchar *name;

		name = gossip_vcard_get_name (vcard);
		if (name && strlen (name) > 0) {
			gossip_contact_set_name (vd->contact, name);
			g_signal_emit_by_name (vd->jabber, 
					       "contact-updated", 
					       vd->contact);
		}
	}

	g_object_unref (vd->jabber);
	g_object_unref (vd->contact);
	g_free (vd);
}

static void
jabber_group_rename (GossipProtocol *protocol,
		     const gchar    *group,
		     const gchar    *new_name)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	RenameGroupData  *rg;

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	rg = g_new0 (RenameGroupData, 1);
	
	rg->jabber = jabber;
	rg->group = g_strdup (group);
	rg->new_name = g_strdup (new_name);

	g_hash_table_foreach (priv->contacts, 
			      (GHFunc)jabber_group_rename_foreach_cb, 
			      rg);

	g_free (rg->group);
	g_free (rg->new_name);
	g_free (rg);
}

static void
jabber_group_rename_foreach_cb (const gchar     *jid,
				GossipContact   *contact,
				RenameGroupData *rg)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
        LmMessageNode    *node;
	gchar            *escaped;
	GList            *l; 
	gboolean          found = FALSE;

	priv = GET_PRIV (rg->jabber);

        for (l = gossip_contact_get_groups (contact); l && !found; l = l->next) {
		gchar *group = (gchar*)l->data;

		if (group && strcmp (group, rg->group) == 0) {
			found = TRUE;
		}
	}
	
	if (!found) {
		return;
	}

	m = lm_message_new_with_sub_type (NULL, 
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);
	node = lm_message_node_add_child (m->node, "query", NULL);
	lm_message_node_set_attributes (node,
					"xmlns", XMPP_ROSTER_XMLNS,
					NULL);
	
	escaped = g_markup_escape_text (gossip_contact_get_name (contact), -1);
	
	node = lm_message_node_add_child (node, "item", NULL);
	lm_message_node_set_attributes (node, 
					"jid", gossip_contact_get_id (contact),
					"name", escaped,
					NULL);
	g_free (escaped);
	
	for (l = gossip_contact_get_groups (contact); l; l = l->next) {
		const gchar *group = (const gchar*) l->data;
		
		/* do not include the group we are renaming */
		if (group && strcmp (group, rg->group) == 0) {
			continue;
		}

		escaped = g_markup_escape_text (group, -1);
		lm_message_node_add_child (node, "group", escaped);
		g_free (escaped);
	}	

	/* add the new group name */
	escaped = g_markup_escape_text (rg->new_name, -1);
	lm_message_node_add_child (node, "group", escaped);
	g_free (escaped);
	
	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);
}

static GossipPresence *
jabber_get_presence (LmMessage *m)
{
	GossipPresence      *presence;
	LmMessageNode       *node;
	GossipPresenceState  state;
	
	if (lm_message_node_find_child (m->node, "error")) {
		/* FIXME: Handle this (send offline?) */
	}

	if (lm_message_get_sub_type (m) != LM_MESSAGE_SUB_TYPE_AVAILABLE) {
		return NULL;
	}

	presence = gossip_presence_new ();

	node = lm_message_node_get_child (m->node, "show");
	if (node) {
		state = gossip_jabber_presence_state_from_str (node->value);
	} else {
		state = GOSSIP_PRESENCE_STATE_AVAILABLE;
	}

	gossip_presence_set_state (presence, state);
	
	node = lm_message_node_get_child (m->node, "status");
	if (node) {
		gossip_presence_set_status (presence, node->value);
	}
	
	node = lm_message_node_get_child (m->node, "priority");
	if (node) {
		gossip_presence_set_priority (presence, atoi (node->value));
	}

	return presence;
}

static const GList *
jabber_get_contacts (GossipProtocol *protocol)
{
	/* FIXME: Keep track of own contacts */
	return NULL;
}

static GossipContact *
jabber_get_own_contact (GossipProtocol *protocol)
{
	GossipJabber *jabber;

	jabber = GOSSIP_JABBER (protocol);

	return gossip_jabber_get_own_contact (jabber);
}

static const gchar *
jabber_get_active_resource (GossipProtocol *protocol,
			    GossipContact  *contact)
{
	return NULL;
}

static GList *
jabber_get_groups (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GList            *list = NULL;

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	g_hash_table_foreach (priv->contacts, 
			      (GHFunc)jabber_get_groups_foreach_cb, 
			      &list);

	list = g_list_sort (list, (GCompareFunc)strcmp);

	return list;
}

static void
jabber_get_groups_foreach_cb (const gchar    *jid,
			      GossipContact  *contact,
			      GList         **list)
{
	GList *l;

	if (!gossip_contact_get_groups (contact)) {
		return;
	}

	for (l = gossip_contact_get_groups (contact); l; l = l->next) {
		gchar *group;
		GList *found;

		group = (gchar*) l->data;
		found = g_list_find_custom (*list, 
					    group, 
					    (GCompareFunc)strcmp);
		
		if (!found) {
			*list = g_list_prepend (*list, g_strdup (group));
		}
	}
}

static gboolean
jabber_get_vcard (GossipProtocol       *protocol,
		  GossipContact        *contact,
		  GossipVCardCallback   callback,
		  gpointer              user_data,
		  GError              **error)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv; 
	const gchar      *jid_str;

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	if (contact) {
		jid_str = gossip_contact_get_id (contact);
	} else {
		jid_str = NULL;
	}
	
	return gossip_jabber_vcard_get (priv->connection,
					jid_str,
					callback, user_data, error);
}

static gboolean
jabber_get_version (GossipProtocol         *protocol,
		    GossipContact          *contact,
		    GossipVersionCallback   callback,
		    gpointer                user_data,
		    GError                **error)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv; 

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);
	
	return gossip_jabber_services_get_version (priv->connection,
						   contact,
						   callback, user_data,
						   error);
}

#define CONF_HTTP_PROXY_PREFIX "/system/http_proxy"

void
jabber_set_proxy (LmConnection *conn)
{
	gboolean     use_http_proxy;
	GConfClient *gconf_client;

	gconf_client = gconf_client_get_default ();
	
	use_http_proxy = gconf_client_get_bool (gconf_client,
						CONF_HTTP_PROXY_PREFIX "/use_http_proxy",
						NULL);
	if (use_http_proxy) {
		LmProxy  *proxy;
		gchar    *host;
		gint      port;
		gboolean  use_auth;
		
		host = gconf_client_get_string (gconf_client,
						CONF_HTTP_PROXY_PREFIX "/host",
						NULL);

		port = gconf_client_get_int (gconf_client,
					     CONF_HTTP_PROXY_PREFIX "/port",
					     NULL);

		proxy = lm_proxy_new_with_server (LM_PROXY_TYPE_HTTP,
						  host, (guint) port);
		g_free (host);

		lm_connection_set_proxy (conn, proxy);

		use_auth = gconf_client_get_bool (gconf_client,
						  CONF_HTTP_PROXY_PREFIX "/use_authentication",
						  NULL);
		if (use_auth) {
			gchar *username;
			gchar *password;

			username = gconf_client_get_string (gconf_client,
							    CONF_HTTP_PROXY_PREFIX "/authentication_user",
							    NULL);
			password = gconf_client_get_string (gconf_client,
							    CONF_HTTP_PROXY_PREFIX "/authentication_password",
							    NULL);
			lm_proxy_set_username (proxy, username);
			lm_proxy_set_password (proxy, password);

			g_free (username);
			g_free (password);
		}
		
		lm_proxy_unref (proxy);
	}

	g_object_unref (gconf_client);
}

static LmHandlerResult
jabber_message_handler (LmMessageHandler *handler,
			LmConnection     *conn,
			LmMessage        *m,
			GossipJabber     *jabber)
{
	GossipJabberPriv *priv;
	GossipMessage    *message;
	const gchar      *from_str;
	GossipContact    *from;
	const gchar      *thread = "";
	const gchar      *body = "";
	LmMessageNode    *node;

	priv = GET_PRIV (jabber);
	
	d(g_print ("Protocol: New message from: %s\n", 
		   lm_message_node_get_attribute (m->node, "from")));

	switch (lm_message_get_sub_type (m)) {
	case LM_MESSAGE_SUB_TYPE_NOT_SET:
	case LM_MESSAGE_SUB_TYPE_NORMAL:
	case LM_MESSAGE_SUB_TYPE_CHAT:
	case LM_MESSAGE_SUB_TYPE_HEADLINE: /* For now, fixes #120009 */
		from_str = lm_message_node_get_attribute (m->node, "from");
		from = gossip_jabber_get_contact_from_jid (jabber, from_str, NULL);
		
		message = gossip_message_new (GOSSIP_MESSAGE_TYPE_NORMAL,
					      priv->contact);
		
		gossip_message_set_sender (message, from);

		/* if event, then we can ignore the rest */
		if (gossip_jabber_get_message_is_event (m)) {
			gboolean composing;

			composing = gossip_jabber_get_message_is_composing (m);

			g_signal_emit_by_name (jabber, "composing-event", 
					       from, composing);
			break;
		}

		node = lm_message_node_get_child (m->node, "body");
		if (node) {
			body = node->value;
		}

		node = lm_message_node_get_child (m->node, "thread");
		if (node) {
			thread = node->value;
		}
			
		gossip_message_set_body (message, body);
		gossip_message_set_thread (message, thread);

		gossip_message_set_timestamp (message,
					      gossip_jabber_get_message_timestamp (m));
		
		gossip_message_set_invite (message,
					   gossip_jabber_get_message_conference (m));
		
		g_signal_emit_by_name (jabber, "new-message", message);
		g_signal_emit_by_name (jabber, "composing-event", 
				       from, FALSE);
		g_object_unref (message);

		break;
	case LM_MESSAGE_SUB_TYPE_ERROR:
		/* FIXME: Emit error */
		break;
	default:
		break;
	}

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static LmHandlerResult
jabber_presence_handler (LmMessageHandler *handler,
			 LmConnection     *conn,
			 LmMessage        *m,
			 GossipJabber     *jabber)
{
	GossipJabberPriv *priv;
	GossipContact    *contact;
	const gchar      *from;
	const gchar      *type;
	gboolean          new_item = FALSE;

	priv = GET_PRIV (jabber);

	from = lm_message_node_get_attribute (m->node, "from");
        d(g_print ("Protocol: New presence from: %s\n", 
		   lm_message_node_get_attribute (m->node, "from")));

	if (gossip_jabber_chatrooms_get_jid_is_chatroom (priv->chatrooms,
							 from)) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	
	contact = gossip_jabber_get_contact_from_jid (jabber, from, &new_item); 

	type = lm_message_node_get_attribute (m->node, "type");
	if (!type) {
		type = "available";
	}

	if (strcmp (type, "subscribe") == 0) {
		g_signal_emit_by_name (jabber, "subscription-request", 
				       contact, NULL);

		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	} else if (strcmp (type, "subscribed") == 0) {
		/* Handle this? */
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}
	
	if (contact) {
		GossipPresence *presence;
		GossipJID      *jid;
		const gchar    *resource;
		
		jid = gossip_jid_new (from);
		resource = gossip_jid_get_resource (jid);
		if (!resource) {
			resource = "";
		}

		presence = jabber_get_presence (m);
		if (!presence) {
			presence = gossip_contact_get_presence_for_resource (contact, resource);
			if (presence) {
				gossip_contact_remove_presence (contact,
								presence);
			}
		} else {
			gossip_presence_set_resource (presence, resource);
			gossip_contact_add_presence (contact, presence);

			g_object_unref (presence);
		}
		
		gossip_jid_unref (jid);
	
		g_signal_emit_by_name (jabber,
				       "contact-presence-updated", 
				       contact);
	}
	
	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static LmHandlerResult
jabber_iq_query_handler (LmMessageHandler *handler,
			 LmConnection     *conn,
			 LmMessage        *m,
			 GossipJabber     *jabber)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	const gchar      *xmlns;
	
	priv = jabber->priv;
	
	if (lm_message_get_sub_type (m) != LM_MESSAGE_SUB_TYPE_GET &&
	    lm_message_get_sub_type (m) != LM_MESSAGE_SUB_TYPE_SET &&
	    lm_message_get_sub_type (m) != LM_MESSAGE_SUB_TYPE_RESULT) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	node = lm_message_node_get_child (m->node, "query");
	if (!node) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	xmlns = lm_message_node_get_attribute (node, "xmlns");
	if (xmlns && strcmp (xmlns, XMPP_ROSTER_XMLNS) == 0) {
		jabber_request_roster (jabber, m);
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	if (xmlns && strcmp (xmlns, XMPP_REGISTER_XMLNS) == 0) {
		/* do nothing at this point */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}
	
	if (xmlns && strcmp (xmlns, XMPP_VERSION_XMLNS) == 0) {
		jabber_request_version (jabber, m);
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	/* if a get, return error for unsupported IQ */
	if (lm_message_get_sub_type (m) == LM_MESSAGE_SUB_TYPE_GET ||
	    lm_message_get_sub_type (m) == LM_MESSAGE_SUB_TYPE_SET) {
		jabber_request_unknown (jabber, m);
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static LmHandlerResult
jabber_subscription_message_handler (LmMessageHandler  *handler,
				     LmConnection      *connection,
				     LmMessage         *m,
				     GossipJabber      *jabber)
{
	LmMessage     *new_message;
	GossipContact *own_contact;
	const gchar   *id;
	const gchar   *from;
	gchar         *to;

	if (lm_message_get_sub_type (m) != LM_MESSAGE_SUB_TYPE_SUBSCRIBE) {
                return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	from = lm_message_node_get_attribute (m->node, "from");
	to = g_strdup (from);

	/* clean up */
	lm_message_handler_invalidate (handler);
	
	/* send subscribed to them */
	d(g_print ("Protocol: Sending subscribed message to new service:'%s'\n", to));
	new_message = lm_message_new_with_sub_type (to,
						    LM_MESSAGE_TYPE_PRESENCE,
						    LM_MESSAGE_SUB_TYPE_SUBSCRIBED);

	own_contact = gossip_jabber_get_own_contact (jabber);
	id = gossip_contact_get_id (own_contact);
	lm_message_node_set_attribute (new_message->node, "from", id);

	lm_connection_send (connection, new_message, NULL);
	lm_message_unref (new_message);

	/* send our presence */
	new_message = lm_message_new_with_sub_type (to, 
						    LM_MESSAGE_TYPE_PRESENCE,
						    LM_MESSAGE_SUB_TYPE_AVAILABLE);

	lm_message_node_set_attribute (new_message->node, "from", id);

	lm_connection_send (connection, new_message, NULL);
	lm_message_unref (new_message);

	/* clean up */
	g_free (to);

 	return LM_HANDLER_RESULT_REMOVE_MESSAGE;	 
}


/*
 * requests
 */

static void
jabber_request_version (GossipJabber *jabber,
			LmMessage    *m)
{
	GossipJabberPriv  *priv;
	LmMessage         *r;
	const gchar       *from, *id;
	LmMessageNode     *node;
	GossipVersionInfo *info;

	priv = GET_PRIV (jabber);

	from = lm_message_node_get_attribute (m->node, "from");
	id = lm_message_node_get_attribute (m->node, "id");

	d(g_print ("Protocol: Version request from:'%s'\n", from));

	r = lm_message_new_with_sub_type (from,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_RESULT);
	lm_message_node_set_attributes (r->node,
					"id", id,
					NULL);
	info = gossip_version_info_get_own ();
	node = lm_message_node_add_child (r->node, "query", NULL);
	lm_message_node_set_attributes (node, 
					"xmlns", XMPP_VERSION_XMLNS,
					NULL);

	if (gossip_version_info_get_name (info)) {
		lm_message_node_add_child (node, 
					   "name", 
					   gossip_version_info_get_name (info));
	}

	if (gossip_version_info_get_version (info)) {
		lm_message_node_add_child (node,
					   "version",
					   gossip_version_info_get_version (info));
	}

	if (gossip_version_info_get_os (info)) {
		lm_message_node_add_child (node,
					   "os",
					   gossip_version_info_get_os (info));
	}

	lm_connection_send (priv->connection, r, NULL);
	lm_message_unref (r);
}

static void
jabber_request_roster (GossipJabber *jabber, 
		       LmMessage    *m)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	
	priv = GET_PRIV (jabber);
	
	node = lm_message_node_get_child (m->node, "query");
	if (!node) {
		return;
	}

	for (node = node->children; node; node = node->next) {
		GossipContact *contact;
		const gchar   *jid_str;
		const gchar   *subscription;
		gboolean       new_item = FALSE;
		gboolean       updated;
		LmMessageNode *subnode;
		LmMessageNode *child;
		GList         *groups = NULL;

		const gchar   *name;
		GList         *new_groups = NULL;
		gboolean       name_updated, groups_updated;
		

		if (strcmp (node->name, "item") != 0) {
			continue;
		}

		jid_str = lm_message_node_get_attribute (node, "jid");
		if (!jid_str) {
			continue;
		}

		contact = gossip_jabber_get_contact_from_jid (jabber, jid_str, &new_item);
		g_object_set (contact, "type", GOSSIP_CONTACT_TYPE_CONTACTLIST, NULL);

		/* groups */
		for (subnode = node->children; subnode; subnode = subnode->next) {
			if (strcmp (subnode->name, "group") != 0) {
				continue;
			}
			
			if (subnode->value) {
				groups = g_list_append (groups, subnode->value);
			}
		}
		
		/* FIXME: why is this here if we set the groups below */
		if (groups) {
			gossip_contact_set_groups (contact, groups);
		}

		/* subscription */
		subscription = lm_message_node_get_attribute (node, "subscription");
		if (contact && subscription) {
			GossipSubscription type;

			if (strcmp (subscription, "remove") == 0) {
				g_object_ref (contact);
				g_hash_table_remove (priv->contacts, 
						     gossip_contact_get_id (contact));
				
				g_signal_emit_by_name (jabber,
						       "contact-removed", contact);
				g_object_unref (contact);
				continue;
			} else if (strcmp (subscription, "both") == 0) {
				type = GOSSIP_SUBSCRIPTION_BOTH;
			} else if (strcmp (subscription, "to") == 0) {
				type = GOSSIP_SUBSCRIPTION_TO;
			} else if (strcmp (subscription, "from") == 0) {
				type = GOSSIP_SUBSCRIPTION_FROM;
			} else {
				type = GOSSIP_SUBSCRIPTION_NONE;
			}

			gossip_contact_set_subscription (contact, type);
		} 
		
		/* find out if any thing has updated */
		name_updated = groups_updated = FALSE;

		name = lm_message_node_get_attribute (node, "name");
		if (name) {
			name_updated = TRUE;
			gossip_contact_set_name (contact, name);
		}

		for (child = node->children; child; child = child->next) {
			if (strcmp (child->name, "group") == 0 && child->value) {
				new_groups = g_list_append (new_groups, child->value);
			}
		}

		if (new_groups) {
			/* does not get free'd */
			groups_updated = gossip_contact_set_groups (contact, new_groups);
		}

		updated = (name_updated || groups_updated);

		
		if (new_item) {
			g_signal_emit_by_name (jabber, 
					       "contact-added", contact);
		}
		else if (updated) {
			g_signal_emit_by_name (jabber, 
					       "contact-updated", contact);
		}
	}
}

static void
jabber_request_unknown (GossipJabber *jabber, 
			LmMessage    *m)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	
	LmMessage         *new_m;
	const gchar       *from_str, *xmlns;

	priv = GET_PRIV (jabber);

	/* get details */
	from_str = lm_message_node_get_attribute (m->node, "from");

	node = lm_message_node_get_child (m->node, "query");
	if (!node) {
		return;
	}

	xmlns = lm_message_node_get_attribute (node, "xmlns");

	/* construct response */
	new_m = lm_message_new_with_sub_type (from_str, 
					      LM_MESSAGE_TYPE_IQ,
					      LM_MESSAGE_SUB_TYPE_ERROR);

	node = lm_message_node_add_child (new_m->node, "query", NULL);
	lm_message_node_set_attribute (node, "xmlns", xmlns);
	
	node = lm_message_node_add_child (new_m->node, "error", NULL);
	lm_message_node_set_attribute (node, "type", "cancel");
	lm_message_node_set_attribute (node, "code", "503");

	node = lm_message_node_add_child (node, "service-unavailable", NULL);
	lm_message_node_set_attribute (node, "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas");

	lm_connection_send (priv->connection, new_m, NULL);

	lm_message_unref (new_m);
}

/*
 * chatrooms
 */

static void
jabber_chatroom_init (GossipChatroomProviderIface *iface)
{
	iface->join            = jabber_chatroom_join;
	iface->cancel          = jabber_chatroom_cancel;
	iface->send            = jabber_chatroom_send;
	iface->change_topic    = jabber_chatroom_change_topic;
	iface->change_nick     = jabber_chatroom_change_nick;
	iface->leave           = jabber_chatroom_leave;
	iface->get_room_name   = jabber_chatroom_get_room_name;
	iface->invite          = jabber_chatroom_invite;
	iface->invite_accept   = jabber_chatroom_invite_accept;
	iface->get_rooms       = jabber_chatroom_get_rooms;
}

static GossipChatroomId
jabber_chatroom_join (GossipChatroomProvider      *provider,
		      GossipChatroom              *chatroom,
		      GossipChatroomJoinCb         callback,
		      gpointer                     user_data)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), 0);
	g_return_val_if_fail (GOSSIP_IS_CHATROOM (chatroom), 0);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_join (priv->chatrooms, chatroom, 
					     callback, user_data);
}

static void
jabber_chatroom_cancel (GossipChatroomProvider *provider,
			GossipChatroomId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (id >= 1);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_cancel (priv->chatrooms, id);
}

static void
jabber_chatroom_send (GossipChatroomProvider *provider,
		      GossipChatroomId        id,
		      const gchar            *message)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_send (priv->chatrooms, id, message);
}

static void
jabber_chatroom_change_topic (GossipChatroomProvider *provider,
			      GossipChatroomId        id,
			      const gchar            *new_topic)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_change_topic (priv->chatrooms, id, new_topic);
}

static void
jabber_chatroom_change_nick (GossipChatroomProvider *provider,
			     GossipChatroomId        id,
			     const gchar            *new_nick)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_change_nick (priv->chatrooms, id, new_nick);
}

static void
jabber_chatroom_leave (GossipChatroomProvider *provider,
		       GossipChatroomId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_leave (priv->chatrooms, id);
}

static const gchar *
jabber_chatroom_get_room_name (GossipChatroomProvider *provider,
			       GossipChatroomId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_get_room_name (priv->chatrooms, id);
}

static void
jabber_chatroom_invite (GossipChatroomProvider *provider,
			GossipChatroomId        id,
			const gchar            *contact_id,
			const gchar            *invite)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_invite (priv->chatrooms, id, contact_id, invite);
}

static void
jabber_chatroom_invite_accept (GossipChatroomProvider *provider,
			       GossipChatroomJoinCb    callback,
			       const gchar            *nickname,
			       const gchar            *invite_id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_invite_accept (priv->chatrooms, 
					       callback, 
					       nickname, 
					       invite_id);
}

static GList *
jabber_chatroom_get_rooms (GossipChatroomProvider *provider)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

/* 	g_hash_table_foreach (priv->chatrooms->room_id_hash,  */
/* 			      (GHFunc) jabber_chatrooms_foreach_set_presence, */
/* 			      chatrooms); */


	return gossip_jabber_chatrooms_get_rooms (priv->chatrooms);
}

/*
 * ft
 */

static void
jabber_ft_init (GossipFTProviderIface *iface)
{
	iface->send    = jabber_ft_send;
	iface->cancel  = jabber_ft_cancel;
	iface->accept  = jabber_ft_accept;
	iface->decline = jabber_ft_decline;
}

static GossipFTId
jabber_ft_send (GossipFTProvider *provider,
		GossipContact    *contact,
		const gchar      *file)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), 0);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), 0);
	g_return_val_if_fail (file != NULL, 0);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_ft_send (priv->fts, contact, file);
}

static void
jabber_ft_cancel (GossipFTProvider *provider,
		  GossipFTId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (id >= 1);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_ft_cancel (priv->fts, id);
}

static void
jabber_ft_accept (GossipFTProvider *provider,
		  GossipFTId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (id >= 1);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_ft_accept (priv->fts, id);
}

static void
jabber_ft_decline (GossipFTProvider *provider,
		   GossipFTId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (id >= 1);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_ft_decline (priv->fts, id);
}

/* 
 * external functions 
 */ 

GossipAccount *
gossip_jabber_get_account (GossipJabber *jabber)
{
	GossipJabberPriv *priv;
	
        g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);
	
	priv = GET_PRIV (jabber);
	
	return priv->account;
}

GossipContact *
gossip_jabber_get_own_contact (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);

	priv = GET_PRIV (jabber);

	return priv->contact;
}

GossipContact *
gossip_jabber_get_contact_from_jid (GossipJabber *jabber, 
				    const gchar  *jid_str,
				    gboolean     *new_item)
{
	GossipJabberPriv *priv;
	GossipContact    *contact;
	GossipJID        *jid;
	gboolean          tmp_new_item = FALSE;

	priv = GET_PRIV (jabber);

	jid = gossip_jid_new (jid_str);
	d(g_print ("Protocol: Get contact: %s\n", gossip_jid_get_full (jid)));

	contact = g_hash_table_lookup (priv->contacts, 
				       gossip_jid_get_without_resource (jid));

	if (!contact) {
		d(g_print ("Protocol: New contact\n"));
		contact = gossip_contact_new (GOSSIP_CONTACT_TYPE_TEMPORARY,
					      priv->account);
		gossip_contact_set_id (contact, 
				       gossip_jid_get_full (jid));
		gossip_contact_set_name (contact, 
					 gossip_jid_get_without_resource (jid));

		tmp_new_item = TRUE;

		g_hash_table_insert (priv->contacts, 
				     g_strdup (gossip_contact_get_id (contact)),
				     g_object_ref (contact));
		
		/* Request contacts VCard details so we can get the
		 * real name for them for chat windows, etc 
		 */
		jabber_contact_vcard (jabber, contact);
	}

	gossip_jid_unref (jid);

	if (new_item) {
		*new_item = tmp_new_item;
	}

	return contact;
}

void
gossip_jabber_subscription_allow_all (GossipJabber *jabber)
{
	GossipJabberPriv *priv;
	LmMessageHandler *handler;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));
	
	priv = GET_PRIV (jabber);

	handler = priv->subscription_handler;
	if (handler) {
		lm_connection_unregister_message_handler (priv->connection, 
							  handler,
							  LM_MESSAGE_TYPE_PRESENCE);
		priv->subscription_handler = NULL;
	}

	/* set up handler to sliently catch the subscription request */
	handler = lm_message_handler_new ((LmHandleMessageFunction)jabber_subscription_message_handler, 
					  jabber, NULL);

	lm_connection_register_message_handler (priv->connection,
						handler,
						LM_MESSAGE_TYPE_PRESENCE,
						LM_HANDLER_PRIORITY_NORMAL);

	priv->subscription_handler = handler;
}

void
gossip_jabber_subscription_disallow_all (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));

	priv = GET_PRIV (jabber);

	if (priv->subscription_handler) {
		lm_connection_unregister_message_handler (priv->connection, 
							  priv->subscription_handler,
							  LM_MESSAGE_TYPE_PRESENCE);
		priv->subscription_handler = NULL;
	}
}

void
gossip_jabber_send_subscribed (GossipJabber  *jabber,
			       GossipContact *contact)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
	const gchar      *id;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	priv = GET_PRIV (jabber);

	id = gossip_contact_get_id (contact);

	m = lm_message_new_with_sub_type (id, 
					  LM_MESSAGE_TYPE_PRESENCE,
					  LM_MESSAGE_SUB_TYPE_SUBSCRIBED);

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);
}

void
gossip_jabber_send_unsubscribed (GossipJabber  *jabber,
				 GossipContact *contact)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
	const gchar      *id;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	priv = GET_PRIV (jabber);
	
	id = gossip_contact_get_id (contact);

	m = lm_message_new_with_sub_type (id, 
					  LM_MESSAGE_TYPE_PRESENCE,
					  LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);
}

/*
 * private functions 
 */

LmConnection *
gossip_jabber_get_connection (GossipJabber *jabber)
{
	GossipJabberPriv *priv;
	
        g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);
	
	priv = GET_PRIV (jabber);
	
	return priv->connection;
}

GossipJabberFTs *
gossip_jabber_get_fts (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);

	priv = GET_PRIV (jabber);
	
	return priv->fts;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>

#include <libgossip/gossip-account.h>
#include <libgossip/gossip-async.h>
#include <libgossip/gossip-avatar.h>
#include <libgossip/gossip-contact.h>
#include <libgossip/gossip-contact-manager.h>
#include <libgossip/gossip-conf.h>
#include <libgossip/gossip-chatroom.h>
#include <libgossip/gossip-debug.h>
#include <libgossip/gossip-chatroom-provider.h>
#include <libgossip/gossip-ft.h>
#include <libgossip/gossip-ft-provider.h>
#include <libgossip/gossip-message.h>
#include <libgossip/gossip-session.h>
#include <libgossip/gossip-utils.h>
#include <libgossip/gossip-vcard.h>
#include <libgossip/gossip-version-info.h>

#include "gossip-jid.h"
#include "gossip-jabber-chatrooms.h"
#include "gossip-jabber-ft.h"
#include "gossip-jabber-vcard.h"
#include "gossip-jabber-disco.h"
#include "gossip-jabber-services.h"
#include "gossip-jabber-utils.h"

#include "gossip-jabber.h"
#include "gossip-jabber-private.h"
#include "gossip-sha.h"

#ifdef USE_TRANSPORTS
#include "gossip-transport-accounts.h"
#endif

#define DEBUG_DOMAIN "Jabber"

#define GOSSIP_JABBER_ERROR_DOMAIN "GossipJabber"

/* Common XMPP XML namespaces we use. */
#define XMPP_VERSION_XMLNS         "jabber:iq:version"
#define XMPP_ROSTER_XMLNS          "jabber:iq:roster"
#define XMPP_REGISTER_XMLNS        "jabber:iq:register"

/* We use 3.5 minutes because if the port is just wrong then it
 * will timeout before then with that error.
 */
#define CONNECT_TIMEOUT            210

/* This is the timeout we will accept the user to be composing for
 * before we assume it is stuck and the server has failed to tell us
 * the user has stopped composing.
 */
#define COMPOSING_TIMEOUT          45

/* How many rand char should be happend to the resource */
#define N_RAND_CHAR                6

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_JABBER, GossipJabberPriv))

struct _GossipJabberPriv {
	LmConnection          *connection;

	GossipContact         *contact;
	GossipAccount         *account;
	GossipPresence        *presence;
	GossipVCard           *vcard;

	GHashTable            *contacts;

	/* Cancel registration attempt */
	gboolean               register_cancel;

	/* Cancel password change attempt */
	gboolean               change_password_cancel;

	/* Connection details */
	guint                  connection_timeout_id;
	gboolean               disconnect_request;

	/* Extended parts */
	GossipJabberChatrooms *chatrooms;
	GossipJabberFTs       *fts;

	/* Used to hold a list of composing message ids, this is so we
	 * can send the cancelation to the last message id.
	 */
	GHashTable            *composing_ids;
	GHashTable            *composing_timeouts;
	GHashTable            *composing_requests;

	/* Transport stuff... is this in the right place? */
#ifdef USE_TRANSPORTS
	GossipTransportAccountList *account_list;
#endif
	LmMessageHandler      *subscription_handler;
};

typedef struct {
	GossipJabber       *jabber;
	LmMessageHandler   *message_handler;

	GossipErrorCallback callback;
	gpointer            user_data;
} AsyncData;

typedef struct {
	GossipJabber     *jabber;
	gchar            *group;
	gchar            *new_name;
} RenameGroupData;

typedef struct {
	GossipJabber     *jabber;
	GossipContact    *contact;
	gpointer          user_data;
} JabberData;

static void             gossip_jabber_class_init            (GossipJabberClass          *klass);
static void             gossip_jabber_init                  (GossipJabber               *jabber);
static void             jabber_finalize                     (GObject                    *object);
static GossipAccount *  jabber_new_account                  (GossipProtocol             *protocol,
							     GossipAccountType           type);
static GossipContact *  jabber_new_contact                  (GossipProtocol             *protocol,
							     const gchar                *id,
							     const gchar                *name);
static void             jabber_setup                        (GossipProtocol             *protocol,
							     GossipAccount              *account);
static void             jabber_setup_connection             (GossipJabber               *jabber);
static void             jabber_login                        (GossipProtocol             *protocol);
static gboolean         jabber_login_timeout_cb             (GossipJabber               *jabber);
static void             jabber_logout                       (GossipProtocol             *protocol);
static gboolean         jabber_logout_contact_foreach       (gpointer                    key,
							     GossipContact              *contact,
							     GossipJabber               *jabber);
static GError *         jabber_error_create                 (GossipProtocolError         code,
							     const gchar                *reason);
static void             jabber_error                        (GossipProtocol             *protocol,
							     GossipProtocolError         code);
static void             jabber_connection_open_cb           (LmConnection               *connection,
							     gboolean                    result,
							     GossipJabber               *jabber);
static void             jabber_connection_auth_cb           (LmConnection               *connection,
							     gboolean                    result,
							     GossipJabber               *jabber);
static void             jabber_disconnect_cb                (LmConnection               *connection,
							     LmDisconnectReason          reason,
							     GossipJabber               *jabber);
static LmSSLResponse    jabber_ssl_status_cb                (LmConnection               *connection,
							     LmSSLStatus                 status,
							     GossipJabber               *jabber);
static AsyncData *      jabber_async_data_new               (GossipJabber               *jabber,
							     GossipErrorCallback         callback,
							     gpointer                    user_data);
static void             jabber_async_data_free              (AsyncData                  *ad);
static void             jabber_register_account             (GossipProtocol             *protocol,
							     GossipAccount              *account,
							     GossipVCard                *vcard,
							     GossipErrorCallback         callback,
							     gpointer                    user_data);
static void             jabber_register_cancel              (GossipProtocol             *protocol);
static void             jabber_change_password              (GossipProtocol             *protocol,
							     const gchar                *new_password,
							     GossipErrorCallback         callback,
							     gpointer                    user_data);
static void             jabber_change_password_cancel       (GossipProtocol             *protocol);
static void             jabber_get_avatar_requirements      (GossipProtocol             *protocol,
							     guint                      *min_width,
							     guint                      *min_height,
							     guint                      *max_width,
							     guint                      *max_height,
							     gsize                      *max_size,
							     gchar                     **format);
static void             jabber_register_connection_open_cb  (LmConnection               *connection,
							     gboolean                    result,
							     AsyncData                  *ad);
static gboolean         jabber_composing_timeout_cb         (JabberData                 *data);
static LmHandlerResult  jabber_register_message_handler     (LmMessageHandler           *handler,
							     LmConnection               *conn,
							     LmMessage                  *m,
							     AsyncData                  *ad);
static gboolean         jabber_is_connected                 (GossipProtocol             *protocol);
static gboolean         jabber_is_connecting                (GossipProtocol             *protocol);
static gboolean         jabber_is_valid_username            (GossipProtocol             *protocol,
							     const gchar                *username);
static gboolean         jabber_is_ssl_supported             (GossipProtocol             *protocol);
static const gchar *    jabber_get_example_username         (GossipProtocol             *protocol);
static gchar *          jabber_get_default_server           (GossipProtocol             *protocol,
							     const gchar                *username);
static guint            jabber_get_default_port             (GossipProtocol             *protocol,
							     gboolean                    use_ssl);
static void             jabber_send_message                 (GossipProtocol             *protocol,
							     GossipMessage              *message);
static void             jabber_send_composing               (GossipProtocol             *protocol,
							     GossipContact              *contact,
							     gboolean                    typing);
static void             jabber_set_presence                 (GossipProtocol             *protocol,
							     GossipPresence             *presence);
static void             jabber_set_subscription             (GossipProtocol             *protocol,
							     GossipContact              *contact,
							     gboolean                    subscribed);
static gboolean         jabber_set_vcard                    (GossipProtocol             *protocol,
							     GossipVCard                *vcard,
							     GossipCallback              callback,
							     gpointer                    user_data,
							     GError                    **error);
static GossipContact *  jabber_contact_find                 (GossipProtocol             *protocol,
							     const gchar                *id);
static void             jabber_contact_add                  (GossipProtocol             *protocol,
							     const gchar                *id,
							     const gchar                *name,
							     const gchar                *group,
							     const gchar                *message);
static void             jabber_contact_rename               (GossipProtocol             *protocol,
							     GossipContact              *contact,
							     const gchar                *new_name);
static void             jabber_contact_remove               (GossipProtocol             *protocol,
							     GossipContact              *contact);
static void             jabber_contact_update               (GossipProtocol             *protocol,
							     GossipContact              *contact);
static void             jabber_contact_is_avatar_latest_cb  (GossipResult                result,
							     GossipVCard                *vcard,
							     JabberData                 *data);
static void		jabber_contact_is_avatar_latest	    (GossipJabber	        *jabber,
							     GossipContact	        *contact,
							     LmMessageNode   	        *m,
							     gboolean                    force_update);
static void             jabber_contact_vcard                (GossipJabber               *jabber,
							     GossipContact              *contact);
static void             jabber_contact_vcard_cb             (GossipResult                result,
							     GossipVCard                *vcard,
							     JabberData                 *data);
static void             jabber_group_rename                 (GossipProtocol             *protocol,
							     const gchar                *group,
							     const gchar                *new_name);
static void             jabber_group_rename_foreach_cb      (const gchar                *jid,
							     GossipContact              *contact,
							     RenameGroupData            *rg);
static GossipPresence * jabber_get_presence                 (LmMessage                  *message);
static const GList *    jabber_get_contacts                 (GossipProtocol             *protocol);
static GossipContact *  jabber_get_own_contact              (GossipProtocol             *protocol);
static const gchar *    jabber_get_active_resource          (GossipProtocol             *protocol,
							     GossipContact              *contact);
static GList *          jabber_get_groups                   (GossipProtocol             *protocol);
static void             jabber_get_groups_foreach_cb        (const gchar                *jid,
							     GossipContact              *contact,
							     GList                     **list);
static gboolean         jabber_get_vcard                    (GossipProtocol             *protocol,
							     GossipContact              *contact,
							     GossipVCardCallback         callback,
							     gpointer                    user_data,
							     GError                    **error);
static gboolean         jabber_get_version                  (GossipProtocol             *protocol,
							     GossipContact              *contact,
							     GossipVersionCallback       callback,
							     gpointer                    user_data,
							     GError                    **error);
static void             jabber_set_proxy                    (LmConnection               *conn);
static LmHandlerResult  jabber_message_handler              (LmMessageHandler           *handler,
							     LmConnection               *conn,
							     LmMessage                  *message,
							     GossipJabber               *jabber);
static LmHandlerResult  jabber_presence_handler             (LmMessageHandler           *handler,
							     LmConnection               *conn,
							     LmMessage                  *message,
							     GossipJabber               *jabber);
static LmHandlerResult  jabber_iq_query_handler             (LmMessageHandler           *handler,
							     LmConnection               *conn,
							     LmMessage                  *message,
							     GossipJabber               *jabber);
static LmHandlerResult  jabber_subscription_message_handler (LmMessageHandler           *handler,
							     LmConnection               *connection,
							     LmMessage                  *m,
							     GossipJabber               *jabber);
static void             jabber_request_version              (GossipJabber               *jabber,
							     LmMessage                  *m);
static void             jabber_request_roster               (GossipJabber               *jabber,
							     LmMessage                  *m);
static void             jabber_request_unknown              (GossipJabber               *jabber,
							     LmMessage                  *m);

/* Chatrooms */
static void             jabber_chatroom_init                (GossipChatroomProviderIface *iface);
static GossipChatroomId jabber_chatroom_join                (GossipChatroomProvider     *provider,
							     GossipChatroom             *chatroom,
							     GossipChatroomJoinCb        callback,
							     gpointer                    user_data);
static void             jabber_chatroom_cancel              (GossipChatroomProvider     *provider,
							     GossipChatroomId            id);
static void             jabber_chatroom_send                (GossipChatroomProvider     *provider,
							     GossipChatroomId            id,
							     const gchar                *message);
static void             jabber_chatroom_change_topic        (GossipChatroomProvider     *provider,
							     GossipChatroomId            id,
							     const gchar                *new_topic);
static void             jabber_chatroom_change_nick         (GossipChatroomProvider     *provider,
							     GossipChatroomId            id,
							     const gchar                *new_nick);
static void             jabber_chatroom_leave               (GossipChatroomProvider     *provider,
							     GossipChatroomId            id);
static GossipChatroom * jabber_chatroom_find_by_id          (GossipChatroomProvider     *provider,
							     GossipChatroomId            id);
static GossipChatroom * jabber_chatroom_find                (GossipChatroomProvider     *provider,
							     GossipChatroom             *chatroom);
static void             jabber_chatroom_invite              (GossipChatroomProvider     *provider,
							     GossipChatroomId            id,
							     GossipContact              *contact,
							     const gchar                *reason);
static void             jabber_chatroom_invite_accept       (GossipChatroomProvider     *provider,
							     GossipChatroomJoinCb        callback,
							     GossipChatroomInvite       *invite,
							     const gchar                *nickname);
static void             jabber_chatroom_invite_decline      (GossipChatroomProvider     *provider,
							     GossipChatroomInvite       *invite,
							     const gchar                *reason);
static GList *          jabber_chatroom_get_rooms           (GossipChatroomProvider     *provider);
static void             jabber_chatroom_browse_rooms        (GossipChatroomProvider     *provider,
							     const gchar                *server,
							     GossipChatroomBrowseCb      callback,
							     gpointer                    user_data);

/* File Transfers */
static void             jabber_ft_init                      (GossipFTProviderIface      *iface);
static GossipFTId       jabber_ft_send                      (GossipFTProvider           *provider,
							     GossipContact              *contact,
							     const gchar                *file);
static void             jabber_ft_cancel                    (GossipFTProvider           *provider,
							     GossipFTId                  id);
static void             jabber_ft_accept                    (GossipFTProvider           *provider,
							     GossipFTId                  id);
static void             jabber_ft_decline                   (GossipFTProvider           *provider,
							     GossipFTId                  id);

/* Misc */
static JabberData *     jabber_data_new                     (GossipJabber               *jabber,
							     GossipContact              *contact,
							     gpointer                    user_data);
static void             jabber_data_free                    (JabberData                 *data);

static const gchar *server_conversions[] = {
	"gmail.com", "talk.google.com",
	"googlemail.com", "talk.google.com",
	NULL
};

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

	protocol_class->new_account             = jabber_new_account;
	protocol_class->new_contact             = jabber_new_contact;
	protocol_class->setup                   = jabber_setup;
	protocol_class->login                   = jabber_login;
	protocol_class->logout                  = jabber_logout;
	protocol_class->is_connected            = jabber_is_connected;
	protocol_class->is_connecting           = jabber_is_connecting;
	protocol_class->is_valid_username       = jabber_is_valid_username;
	protocol_class->is_ssl_supported        = jabber_is_ssl_supported;
	protocol_class->get_example_username    = jabber_get_example_username;
	protocol_class->get_default_server      = jabber_get_default_server;
	protocol_class->get_default_port        = jabber_get_default_port;
	protocol_class->set_presence            = jabber_set_presence;
	protocol_class->set_subscription        = jabber_set_subscription;
	protocol_class->set_vcard               = jabber_set_vcard;
	protocol_class->send_message            = jabber_send_message;
	protocol_class->send_composing          = jabber_send_composing;
	protocol_class->find_contact            = jabber_contact_find;
	protocol_class->add_contact             = jabber_contact_add;
	protocol_class->rename_contact          = jabber_contact_rename;
	protocol_class->remove_contact          = jabber_contact_remove;
	protocol_class->update_contact          = jabber_contact_update;
	protocol_class->rename_group            = jabber_group_rename;
	protocol_class->get_contacts            = jabber_get_contacts;
	protocol_class->get_own_contact         = jabber_get_own_contact;
	protocol_class->get_active_resource     = jabber_get_active_resource;
	protocol_class->get_groups              = jabber_get_groups;
	protocol_class->get_vcard               = jabber_get_vcard;
	protocol_class->get_version             = jabber_get_version;
	protocol_class->register_account        = jabber_register_account;
	protocol_class->register_cancel         = jabber_register_cancel;
	protocol_class->change_password         = jabber_change_password;
	protocol_class->change_password_cancel  = jabber_change_password_cancel;
	protocol_class->get_avatar_requirements = jabber_get_avatar_requirements;

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

	priv->composing_timeouts =
		g_hash_table_new_full (gossip_contact_hash,
				       gossip_contact_equal,
				       (GDestroyNotify) g_object_unref,
				       (GDestroyNotify) jabber_data_free);

	priv->composing_requests =
		g_hash_table_new_full (gossip_contact_hash,
				       gossip_contact_equal,
				       (GDestroyNotify) g_object_unref,
				       NULL);
}

static void
jabber_finalize (GObject *object)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	jabber = GOSSIP_JABBER (object);
	priv = GET_PRIV (jabber);

	if (priv->account) {
		g_object_unref (priv->account);
	}

	if (priv->contact) {
		g_object_unref (priv->contact);
	}

	if (priv->vcard) {
		g_object_unref (priv->vcard);
	}

	if (priv->presence) {
		g_object_unref (priv->presence);
	}

	g_hash_table_destroy (priv->contacts);

	/* finalize extended modules */
	gossip_jabber_chatrooms_finalize (priv->chatrooms);
	gossip_jabber_ft_finalize (priv->fts);

	g_hash_table_destroy (priv->composing_ids);
	g_hash_table_destroy (priv->composing_timeouts);
	g_hash_table_destroy (priv->composing_requests);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
	}

#ifdef USE_TRANSPORTS
	gossip_transport_account_list_free (priv->account_list);
#endif

	(G_OBJECT_CLASS (gossip_jabber_parent_class)->finalize) (object);
}

static GossipAccount *
jabber_new_account (GossipProtocol    *protocol,
		    GossipAccountType  type)
{
	GossipAccount *account;
	const gchar   *id;
	gchar         *server;
	gboolean       ssl;
	guint          port;

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), NULL);

	id = jabber_get_example_username (protocol);
	server = jabber_get_default_server (protocol, id);
	ssl = jabber_is_ssl_supported (protocol);
	port = jabber_get_default_port (protocol, ssl);
	
	/* Set a default value for each account parameter */
	account = g_object_new (GOSSIP_TYPE_ACCOUNT,
				"type", GOSSIP_ACCOUNT_TYPE_JABBER_LEGACY,
				"name", _("new account"),
				"auto_connect", TRUE,
				"use_proxy", FALSE,
				NULL);
 
	gossip_account_new_param (account,
				  "password", G_TYPE_STRING, "", 0,
				  "resource", G_TYPE_STRING, _("Home"), 0,
				  "server", G_TYPE_STRING, server, 0,
				  "port", G_TYPE_UINT, port, 0,
				  "use_ssl", G_TYPE_BOOLEAN, ssl, 0,
				  "account", G_TYPE_STRING, id, 0,
				  NULL);

	g_free (server);

	return account;
}

static GossipContact *
jabber_new_contact (GossipProtocol *protocol,
		    const gchar    *id,
		    const gchar    *name)
{
	GossipJabber  *jabber;
	GossipContact *contact;
	gboolean       new_contact;

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), NULL);

	jabber = GOSSIP_JABBER (protocol);

	contact = gossip_jabber_get_contact_from_jid (jabber,
						      id,
						      &new_contact,
						      FALSE);
	if (new_contact && !G_STR_EMPTY (name)) {
		gossip_contact_set_name (contact, name);
	}

	return contact;
}

static void
jabber_setup (GossipProtocol *protocol,
	      GossipAccount  *account)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	LmMessageHandler *handler;
	GossipJID        *jid;
	const gchar      *account_param;
	const gchar      *server;
	guint             port;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	priv->account = g_object_ref (account);

	priv->contact = gossip_contact_new (GOSSIP_CONTACT_TYPE_USER,
					    priv->account);

	gossip_account_get_param (account,
				  "server", &server,
				  "port", &port,
				  "account", &account_param,
				  NULL);

	g_object_set (priv->contact, "id", account_param, NULL);

	priv->connection = lm_connection_new (server);

	/* setup the connection to send keep alive messages every 30 seconds */
	lm_connection_set_keep_alive_rate (priv->connection, 30);

	lm_connection_set_disconnect_function (priv->connection,
					       (LmDisconnectFunction) jabber_disconnect_cb,
					       jabber, NULL);

	lm_connection_set_port (priv->connection, port);

	jid = gossip_jid_new (account_param);
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
	gossip_jabber_disco_init (jabber);

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
	gboolean          use_ssl;
	guint             port;

	priv = GET_PRIV (jabber);

	priv->disconnect_request = FALSE;

	gossip_account_get_param (priv->account,
				  "use_ssl", &use_ssl,
				  "port", &port,
				  NULL);

	if (use_ssl) {
		LmSSL *ssl;

		gossip_debug (DEBUG_DOMAIN, "Using SSL");

		ssl = lm_ssl_new (NULL,
				  (LmSSLFunction) jabber_ssl_status_cb,
				  jabber, NULL);

		lm_connection_set_ssl (priv->connection, ssl);
		lm_connection_set_port (priv->connection, port);

		lm_ssl_unref (ssl);
	}

	if (gossip_account_get_use_proxy (priv->account)) {
		gossip_debug (DEBUG_DOMAIN, "Using proxy");
		jabber_set_proxy (priv->connection);
	} else {
		lm_connection_set_proxy (priv->connection, NULL);
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

	gossip_debug (DEBUG_DOMAIN, "Connecting...");
	g_signal_emit_by_name (jabber, "connecting", priv->account);

	if (priv->presence) {
		g_object_unref (priv->presence);
	}

	priv->presence = gossip_presence_new ();
	gossip_presence_set_state (priv->presence,
				   GOSSIP_PRESENCE_STATE_AVAILABLE);
	gossip_jabber_chatrooms_set_presence (priv->chatrooms, priv->presence);

	jabber_setup_connection (jabber);

	if (!priv->connection) {
		/* Should we emit these or just call jabber_logout()
		 * with the risk of not getting the disconnected
		 * signal from Loudmouth? 
		 */
		g_signal_emit_by_name (jabber, "disconnecting", priv->account);
		g_signal_emit_by_name (jabber, "disconnected", priv->account);

		jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_NO_CONNECTION);
		return;
	}

	result = lm_connection_open (priv->connection,
				     (LmResultFunction) jabber_connection_open_cb,
				     jabber, NULL, &error);

	if (result && !error) {
		/* FIXME: add timeout incase we get nothing back from
		 * Loudmouth, this happens with current CVS loudmouth
		 * 1.01 where you connect to port 5222 using SSL, the
		 * error is not reflecting back into Gossip so we just
		 * hang around waiting.
		 */
		priv->connection_timeout_id = g_timeout_add (CONNECT_TIMEOUT * 1000,
							     (GSourceFunc) jabber_login_timeout_cb,
							     jabber);

		return;
	}

	if (error->code == 1 &&
	    strcmp (error->message, "getaddrinfo() failed") == 0) {
		jabber_logout (GOSSIP_PROTOCOL (jabber));
		jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_NO_SUCH_HOST);
	}

	g_error_free (error);
}

static gboolean
jabber_login_timeout_cb (GossipJabber *jabber)
{
	GossipJabberPriv *priv;

	priv = GET_PRIV (jabber);

	priv->connection_timeout_id = 0;

	jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_TIMED_OUT);

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

	gossip_debug (DEBUG_DOMAIN, 
		      "Disconnecting for account:'%s'",
		      gossip_account_get_name (priv->account));

	g_signal_emit_by_name (jabber, "disconnecting", priv->account);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	priv->disconnect_request = TRUE;

	if (priv->connection) {
		lm_connection_close (priv->connection, NULL);
	}
}

static gboolean
jabber_logout_contact_foreach (gpointer       key,
			       GossipContact *contact,
			       GossipJabber  *jabber)
{
	GList          *presences;
	GList          *l;
	GossipPresence *presence;

	/* Set each contact to be offline, since they effectively are
	 * now we don't know.
	 */
	presences = g_list_copy (gossip_contact_get_presence_list (contact));

	/* Copy the list since it will be modified during traversal
	 * otherwise.
	 */
	for (l = presences; l; l = l->next) {
		presence = l->data;

		if (!presence) {
			continue;
		}

		gossip_contact_remove_presence (contact, presence);
	}

	g_list_free (presences);

	g_signal_emit_by_name (jabber, "contact-removed", contact);

	return TRUE;
}

static GError *
jabber_error_create (GossipProtocolError  code,
		     const gchar         *reason)
{
	static GQuark  quark = 0;
	GError        *error;

	if (!quark) {
		quark = g_quark_from_static_string ("gossip-jabber");
	}

	error = g_error_new_literal (quark, code, reason);
	return error;
}

static void
jabber_error (GossipProtocol      *protocol,
	      GossipProtocolError  code)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;
	GError           *error;
	const gchar      *message;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	message = gossip_protocol_error_to_string (code);
	gossip_debug (DEBUG_DOMAIN, "Error:%d->'%s'", code, message);

	error = jabber_error_create (code, message);
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
	const gchar      *account_param;
	const gchar      *account_resource;
	const gchar      *jid_str;
	gchar            *id;
	gchar            *password = NULL;
#ifdef USE_RAND_RESOURCE
	gchar            *resource_rand;
	gchar             rand_str[N_RAND_CHAR + 1];
	gint              i;
#endif

	priv = GET_PRIV (jabber);

	if (priv->disconnect_request) {
		/* This is so we go no further that way we don't issue
		 * warnings for connections we stopped ourselves.
		 */
		jabber_logout (GOSSIP_PROTOCOL (jabber));
		return;
	}

	if (result == FALSE) {
		jabber_logout (GOSSIP_PROTOCOL (jabber));
		jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_NO_CONNECTION);
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Connection open!");

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	account = priv->account;
	gossip_account_get_param (account,
				  "password", &account_password,
				  "resource", &account_resource,
				  "account", &account_param,
				  NULL);

	if (!account_password || g_utf8_strlen (account_password, -1) < 1) {
		gossip_debug (DEBUG_DOMAIN, "Requesting password for:'%s'",
			      account_param);

		g_signal_emit_by_name (jabber, "get-password",
				       account, &password);

		if (!password) {
			gossip_debug (DEBUG_DOMAIN, "Cancelled password request for:'%s'",
				      account_param);

			jabber_logout (GOSSIP_PROTOCOL (jabber));
			return;
		}
	} else {
		password = g_strdup (account_password);
	}

	/* FIXME: Decide on Resource */
	jid_str = account_param;
	gossip_debug (DEBUG_DOMAIN, "Attempting to use JabberID:'%s'", jid_str);

	id = gossip_jid_string_get_part_name (jid_str);

	if (!account_resource) {
		gossip_debug (DEBUG_DOMAIN, "JabberID:'%s' is invalid, there is no resource.", jid_str);

		jabber_logout (GOSSIP_PROTOCOL (jabber));
		jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_INVALID_USER);
		return;
	}

#ifdef USE_RAND_RESOURCE
	/* appens a random string to the resource to avoid conflicts */
	for (i = 0; i < N_RAND_CHAR; i++) {
		if (g_random_boolean()) {
			rand_str[i] = g_random_int_range('A', 'Z' + 1);
		} else {
			rand_str[i] = g_random_int_range('0', '9' + 1);
		}
	}
	rand_str[N_RAND_CHAR] = '\0';
	resource_rand = g_strdup_printf ("%s.%s", account_resource, rand_str);

	lm_connection_authenticate (priv->connection,
				    id,
				    password,
				    resource_rand,
				    (LmResultFunction) jabber_connection_auth_cb,
				    jabber, NULL, NULL);

	g_free (resource_rand);
#else
	lm_connection_authenticate (priv->connection,
				    id,
				    password,
				    account_resource,
				    (LmResultFunction) jabber_connection_auth_cb,
				    jabber, NULL, NULL);
#endif

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
		jabber_logout (GOSSIP_PROTOCOL (jabber));
		jabber_error (GOSSIP_PROTOCOL (jabber), GOSSIP_PROTOCOL_AUTH_FAILED);
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Connection logged in!");

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

	g_signal_emit_by_name (jabber, "connected", priv->account);

	if (priv->vcard) {
		gchar *name;

		name = gossip_jabber_get_name_to_use
			(gossip_contact_get_id (priv->contact),
			 gossip_vcard_get_nickname (priv->vcard),
			 gossip_vcard_get_name (priv->vcard));

		gossip_contact_set_name (priv->contact, name);
		g_free (name);

		/* Set the vcard waiting to be sent to our jabber server once
		 * connected.
		 */
		gossip_jabber_vcard_set (jabber,
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
	GossipJabberPriv               *priv;
	GossipProtocolDisconnectReason  gossip_reason;

	priv = GET_PRIV (jabber);

	if (priv->connection_timeout_id != 0) {
		g_source_remove (priv->connection_timeout_id);
		priv->connection_timeout_id = 0;
	}

	/* Signal removal of each contact */
	if (priv->contacts) {
		g_hash_table_foreach_remove (priv->contacts,
					     (GHRFunc) jabber_logout_contact_foreach,
					     jabber);
	}

	switch (reason) {
	case LM_DISCONNECT_REASON_OK:
	case LM_DISCONNECT_REASON_RESOURCE_CONFLICT:
		gossip_reason = GOSSIP_PROTOCOL_DISCONNECT_ASKED;
		break;

	case LM_DISCONNECT_REASON_INVALID_XML:
	case LM_DISCONNECT_REASON_PING_TIME_OUT:
	case LM_DISCONNECT_REASON_HUP:
	case LM_DISCONNECT_REASON_ERROR:
	case LM_DISCONNECT_REASON_UNKNOWN:
		gossip_reason = GOSSIP_PROTOCOL_DISCONNECT_ERROR;
		break;
	}

	g_signal_emit_by_name (jabber, "disconnected", priv->account, gossip_reason);
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

	gossip_debug (DEBUG_DOMAIN, "%s", str);

	return LM_SSL_RESPONSE_CONTINUE;
}

static AsyncData *
jabber_async_data_new (GossipJabber        *jabber,
		       GossipErrorCallback  callback,
		       gpointer             user_data)
{
	AsyncData *ad;

	ad = g_new0 (AsyncData, 1);

	ad->jabber = g_object_ref (jabber);

	ad->callback = callback;
	ad->user_data = user_data;

	return ad;
}

static void
jabber_async_data_free (AsyncData *ad)
{
	if (!ad) {
		return;
	}

	if (ad->jabber) {
		g_object_unref (ad->jabber);
	}

	/* FIXME: should we clean up the message handler or does
	 * closing the connection do this for us?
	 */

	g_free (ad);
}

static void
jabber_register_account (GossipProtocol      *protocol,
			 GossipAccount       *account,
			 GossipVCard         *vcard,
			 GossipErrorCallback  callback,
			 gpointer             user_data)
{
	GossipJabber        *jabber;
	GossipJabberPriv    *priv;
	AsyncData           *ad = NULL;
	gboolean             result;
	GossipProtocolError  error_code;
	const gchar         *error_message;
	GError              *error;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (callback != NULL);

	gossip_debug (DEBUG_DOMAIN, "Registering with Jabber server...");

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	/* Save vcard information so the next time we connect we can
	 * set it, this could be done better */
	priv->vcard = g_object_ref (vcard);

	priv->register_cancel = FALSE;

	/* Make sure we connect using the account settings */
	jabber_setup_connection (jabber);
	if (!priv->connection) {
		goto error;
	}

	ad = jabber_async_data_new (jabber, callback, user_data);
	result = lm_connection_open (priv->connection,
				     (LmResultFunction) jabber_register_connection_open_cb,
				     ad, NULL, NULL);
	if (result) {
		return;
	}

error:
	error_code = GOSSIP_PROTOCOL_NO_CONNECTION;
	error_message = gossip_protocol_error_to_string (error_code);
	error = jabber_error_create (error_code, error_message);

	gossip_debug (DEBUG_DOMAIN, "%s", error_message);

	if (callback) {
		(callback) (GOSSIP_RESULT_ERROR_FAILED,
			    error,
			    user_data);
	}

	g_error_free (error);
	jabber_async_data_free (ad);
}

static void
jabber_register_cancel (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));

	gossip_debug (DEBUG_DOMAIN, "Canceling registration with Jabber server...");

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	priv->register_cancel = TRUE;

	jabber_logout (protocol);
}

static LmHandlerResult
jabber_change_password_message_handler (LmMessageHandler *handler,
					LmConnection     *connection,
					LmMessage        *m,
					AsyncData        *ad)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	GossipResult      result = GOSSIP_RESULT_OK;
	GError           *error = NULL;

	priv = GET_PRIV (ad->jabber);

	if (priv->change_password_cancel) {
		jabber_async_data_free (ad);
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}

	node = lm_message_node_get_child (m->node, "error");
	if (node) {
		GossipProtocolError  error_code;
		const gchar         *error_code_str;
		const gchar         *error_message;

		result = GOSSIP_RESULT_ERROR_FAILED;
		error_code_str = lm_message_node_get_attribute (node, "code");

		switch (atoi (error_code_str)) {
		case 401: /* Not Authorized */
		case 407: /* Registration Required */
			error_code = GOSSIP_PROTOCOL_UNAUTHORIZED;
			break;

		case 501: /* Not Implemented */
		case 503: /* Service Unavailable */
			error_code = GOSSIP_PROTOCOL_UNAVAILABLE;
			break;

		case 409: /* Conflict */
			error_code = GOSSIP_PROTOCOL_DUPLICATE_USER;
			break;

		case 408: /* Request Timeout */
		case 504: /* Remote Server Timeout */
			error_code = GOSSIP_PROTOCOL_TIMED_OUT;
			break;

		case 302: /* Redirect */
		case 400: /* Bad Request */
		case 402: /* Payment Required */
		case 403: /* Forbidden */
		case 404: /* Not Found */
		case 405: /* Not Allowed */
		case 406: /* Not Acceptable */
		case 500: /* Internal Server Error */
		case 502: /* Remote Server Error */
		case 510: /* Disconnected */
		default:
			error_code = GOSSIP_PROTOCOL_SPECIFIC_ERROR;
			break;
		};

		error_message = gossip_protocol_error_to_string (error_code);
		error = jabber_error_create (error_code,
					     error_message);

		gossip_debug (DEBUG_DOMAIN, "Registration failed with error:%s->'%s'",
			      error_code_str, error_message);
	} else {
		gossip_debug (DEBUG_DOMAIN, "Registration success");
	}

	if (ad->callback) {
		(ad->callback) (result,
				error,
				ad->user_data);
	}

	/* Clean up */
	if (error) {
		g_error_free (error);
	}

	jabber_async_data_free (ad);

	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
jabber_change_password (GossipProtocol      *protocol,
			const gchar         *new_password,
			GossipErrorCallback  callback,
			gpointer             user_data)
{
	GossipJabber        *jabber;
	GossipJabberPriv    *priv;
	GossipJID           *jid;
	const gchar         *account_param;
	const gchar         *server;
	const gchar         *error_message;
	gboolean             ok;
	AsyncData           *ad = NULL;
	LmMessage           *m;
	LmMessageNode       *node;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (new_password != NULL);

	gossip_debug (DEBUG_DOMAIN, "Changing password to '%s'...", new_password);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	/* Set flags up */
	priv->change_password_cancel = FALSE;

	/* Get credentials */
	gossip_account_get_param (priv->account,
				  "account", &account_param,
				  "server", &server,
				  NULL);
	jid = gossip_jid_new (account_param);

	/* Create & send message */
        m = lm_message_new_with_sub_type (server, 
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);

        node = lm_message_node_add_child (m->node, "query", NULL);
        lm_message_node_set_attributes (node, "xmlns", XMPP_REGISTER_XMLNS, NULL);
        lm_message_node_add_child (node, "username", gossip_jid_get_part_name (jid));
        lm_message_node_add_child (node, "password", new_password);

	gossip_jid_unref (jid);

	ad = jabber_async_data_new (jabber, callback, user_data);
	ad->message_handler = lm_message_handler_new ((LmHandleMessageFunction)
						      jabber_change_password_message_handler,
						      ad,
						      NULL);

	ok = lm_connection_send_with_reply (priv->connection, m,
					    ad->message_handler,
					    NULL);
	lm_message_unref (m);

	if (!ok) {
		GError *error;

		error_message = _("Couldn't send message!");
		error = jabber_error_create (GOSSIP_PROTOCOL_SPECIFIC_ERROR,
					     error_message);

		gossip_debug (DEBUG_DOMAIN, "%s", error_message);

		if (ad->callback) {
			(ad->callback) (GOSSIP_RESULT_ERROR_FAILED,
					error,
					ad->user_data);
		}

		g_error_free (error);
		jabber_async_data_free (ad);
		return;
	}
}

static void
jabber_change_password_cancel (GossipProtocol *protocol)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));

	gossip_debug (DEBUG_DOMAIN, "Changing password canceled");

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	priv->change_password_cancel = TRUE;
}

static void
jabber_get_avatar_requirements (GossipProtocol  *protocol,
				guint           *min_width,
				guint           *min_height,
				guint           *max_width,
				guint           *max_height,
				gsize           *max_size,
				gchar          **format)
{
	if (min_width) {
		*min_width = 32;
	}
	if (min_height) {
		*min_height = 32;
	}
	if (max_width) {
		*max_width = 96;
	}
	if (max_height) {
		*max_height = 96;
	}
	if (max_size) {
		*max_size = 8*1024; /* 8kb */
	}
	if (format) {
		*format = g_strdup ("image/png");
	}
}

static void
jabber_register_connection_open_cb (LmConnection *connection,
				    gboolean      result,
				    AsyncData    *ad)
{
	GossipJabberPriv *priv;
	LmMessage        *m;
	LmMessageNode    *node;
	const gchar      *error_message = NULL;
	const gchar      *account_param;
	const gchar      *server;
	const gchar      *password;
	gchar            *username;
	gboolean          ok = FALSE;

	priv = GET_PRIV (ad->jabber);

	if (priv->register_cancel) {
		jabber_async_data_free (ad);
		return;
	}

	if (result == FALSE) {
		GError *error;

		error_message = _("Connection could not be opened");
		error = jabber_error_create (GOSSIP_PROTOCOL_NO_CONNECTION,
					     error_message);

		gossip_debug (DEBUG_DOMAIN, "%s", error_message);

		if (ad->callback) {
			(ad->callback) (GOSSIP_RESULT_ERROR_FAILED,
					error,
					ad->user_data);
		}

		g_error_free (error);
		jabber_async_data_free (ad);
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Connection open!");

	ad->message_handler = lm_message_handler_new ((LmHandleMessageFunction)
						      jabber_register_message_handler,
						      ad,
						      NULL);

	gossip_account_get_param (priv->account,
				  "server", &server,
				  "password", &password,
				  "account", &account_param,
				  NULL);

	m = lm_message_new_with_sub_type (server,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_SET);

	lm_message_node_add_child (m->node, "query", NULL);
	node = lm_message_node_get_child (m->node, "query");

	lm_message_node_set_attribute (node, "xmlns", XMPP_REGISTER_XMLNS);

	username = gossip_jid_string_get_part_name (account_param);
	lm_message_node_add_child (node, "username", username);
	g_free (username);

	lm_message_node_add_child (node, "password", password);

	ok = lm_connection_send_with_reply (connection, m,
					    ad->message_handler,
					    NULL);
	lm_message_unref (m);

	if (!ok) {
		GError *error;

		error_message = _("Couldn't send message!");
		error = jabber_error_create (GOSSIP_PROTOCOL_SPECIFIC_ERROR,
					     error_message);

		gossip_debug (DEBUG_DOMAIN, "%s", error_message);

		if (ad->callback) {
			(ad->callback) (GOSSIP_RESULT_ERROR_FAILED,
					error,
					ad->user_data);
		}

		g_error_free (error);
		jabber_async_data_free (ad);
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Sent registration details");
}

static LmHandlerResult
jabber_register_message_handler (LmMessageHandler *handler,
				 LmConnection     *connection,
				 LmMessage        *m,
				 AsyncData        *ad)
{
	GossipJabberPriv *priv;
	LmMessageNode    *node;
	GossipResult      result = GOSSIP_RESULT_OK;
	GError           *error = NULL;

	priv = GET_PRIV (ad->jabber);

	if (priv->register_cancel) {
		jabber_async_data_free (ad);
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	}

	node = lm_message_node_get_child (m->node, "error");
	if (node) {
		GossipProtocolError  error_code;
		const gchar         *error_code_str;
		const gchar         *error_message;

		result = GOSSIP_RESULT_ERROR_FAILED;
		error_code_str = lm_message_node_get_attribute (node, "code");

		switch (atoi (error_code_str)) {
		case 401: /* Not Authorized */
		case 407: /* Registration Required */
			error_code = GOSSIP_PROTOCOL_UNAUTHORIZED;
			break;

		case 501: /* Not Implemented */
		case 503: /* Service Unavailable */
			error_code = GOSSIP_PROTOCOL_UNAVAILABLE;
			break;

		case 409: /* Conflict */
			error_code = GOSSIP_PROTOCOL_DUPLICATE_USER;
			break;

		case 408: /* Request Timeout */
		case 504: /* Remote Server Timeout */
			error_code = GOSSIP_PROTOCOL_TIMED_OUT;
			break;

		case 302: /* Redirect */
		case 400: /* Bad Request */
		case 402: /* Payment Required */
		case 403: /* Forbidden */
		case 404: /* Not Found */
		case 405: /* Not Allowed */
		case 406: /* Not Acceptable */
		case 500: /* Internal Server Error */
		case 502: /* Remote Server Error */
		case 510: /* Disconnected */
		default:
			error_code = GOSSIP_PROTOCOL_SPECIFIC_ERROR;
			break;
		};

		error_message = gossip_protocol_error_to_string (error_code);
		error = jabber_error_create (error_code,
					     error_message);

		gossip_debug (DEBUG_DOMAIN, "Registration failed with error:%s->'%s'",
			      error_code_str, error_message);
	} else {
		gossip_debug (DEBUG_DOMAIN, "Registration success");
	}

	if (ad->callback) {
		(ad->callback) (result,
				error,
				ad->user_data);
	}

	/* Clean up */
	if (error) {
		g_error_free (error);
	}

	jabber_async_data_free (ad);

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
jabber_is_connecting (GossipProtocol *protocol)
{
	GossipJabber      *jabber;
	GossipJabberPriv  *priv;
	LmConnectionState  state;

	g_return_val_if_fail (GOSSIP_IS_JABBER (protocol), FALSE);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (priv->connection == NULL) {
		return FALSE;
	}

	state = lm_connection_get_state (priv->connection);
	if (state == LM_CONNECTION_STATE_OPENING) {
		return TRUE;
	}

	return FALSE;
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

	return gossip_jid_string_is_valid (username, FALSE);
}

static gboolean
jabber_is_ssl_supported (GossipProtocol *protocol)
{
	return lm_ssl_is_supported ();
}

static const gchar *
jabber_get_example_username (GossipProtocol *protocol)
{
	return "user@jabber.org";
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
	gint              i = 0;

	g_return_val_if_fail (username != NULL, NULL);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	jid = gossip_jid_new (username);
	str = gossip_jid_get_part_host (jid);

	while (server_conversions[i] != NULL) {
		if (g_ascii_strncasecmp (str, server_conversions[i], -1) == 0) {
			str = server_conversions[i + 1];
			break;
		}

		i += 2;
	}

	server = g_strdup (str);
	gossip_jid_unref (jid);

	return server;
}

static guint
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

	recipient_id = gossip_contact_get_id (recipient);
	resource = gossip_message_get_explicit_resource (message);

	/* Getting contact from JID, this will add them to our
	 * contacts hash table if they don't exist too.
	 */
	recipient = gossip_jabber_get_contact_from_jid (jabber,
							recipient_id,
							&new_contact,
							TRUE);
	if (new_contact) {
		g_object_unref (recipient);
	}

	if (resource && g_utf8_strlen (resource, -1) > 0) {
		jid_str = g_strdup_printf ("%s/%s", recipient_id, resource);
	} else {
		jid_str = g_strdup (recipient_id);
	}

	gossip_debug (DEBUG_DOMAIN, "Sending message to:'%s'", jid_str);

	m = lm_message_new_with_sub_type (jid_str,
					  LM_MESSAGE_TYPE_MESSAGE,
					  LM_MESSAGE_SUB_TYPE_CHAT);

	lm_message_node_add_child (m->node, "body",
				   gossip_message_get_body (message));

	/* If we have had a request for composing then we send the
	 * other side composing details with every message
	 */
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
	gchar            *jid_str;

	g_return_if_fail (GOSSIP_IS_JABBER (protocol));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	if (!g_hash_table_lookup (priv->composing_requests, contact)) {
		return;
	}

	contact_id = gossip_contact_get_id (contact);

	gossip_debug (DEBUG_DOMAIN, "Sending %s to contact:'%s'",
		      typing ? "composing" : "not composing",
		      contact_id);

	jid_str = g_strdup (contact_id);

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
	LmMessageNode       *node;
	GossipContact	    *contact;
	GossipPresenceState  state;
	const gchar         *show;
	const gchar         *status;
	const gchar         *priority;
	gchar               *sha1;
	GossipAvatar        *avatar;

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
	status = gossip_presence_get_status (presence);

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

	contact = gossip_jabber_get_own_contact (jabber);
	avatar = gossip_contact_get_avatar (contact);
	if (avatar) {
		sha1 = gossip_sha_hash (avatar->data, avatar->len);
	} else {
		sha1 = gossip_sha_hash (NULL, 0);
	}

	gossip_debug (DEBUG_DOMAIN, "Setting presence to:'%s', status:'%s', "
		      "priority:'%s' sha1:'%s'",
		      show ? show : "available",
		      status ? status : "",
		      priority, sha1);

	if (show) {
		lm_message_node_add_child (m->node, "show", show);
	}

	lm_message_node_add_child (m->node, "priority", priority);

	if (status) {
		lm_message_node_add_child (m->node, "status", status);
	}

	node = lm_message_node_add_child (m->node, "x", "");
	lm_message_node_set_attribute (node, "xmlns", "vcard-temp:x:update");

	/* I can't think of an elegant way of doing this correctly so
	 * I won't. We need to send an empty "x" element before we
	 * download the user's vcard. And an empty "photo" element
	 * when the user chooses not to use an avatar.
	 *
	 * See:
	 * http://www.jabber.org/jeps/jep-0153.html#bizrules-presence
	 */

	lm_message_node_add_child (node, "photo", sha1);
	g_free (sha1);

	lm_connection_send (priv->connection, m, NULL);
	lm_message_unref (m);

	/* Don't forget to set any chatroom status too */
	gossip_jabber_chatrooms_set_presence (priv->chatrooms, priv->presence);
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

	gossip_debug (DEBUG_DOMAIN, "Setting subscription for contact:'%s' as %s",
		      gossip_contact_get_id (contact),
		      subscribed ? "subscribed" : "unsubscribed");

	if (subscribed) {
		gossip_jabber_send_subscribed (jabber, contact);
	} else {
		gossip_jabber_send_unsubscribed (jabber, contact);
	}
}

static gboolean
jabber_set_vcard (GossipProtocol  *protocol,
		  GossipVCard     *vcard,
		  GossipCallback   callback,
		  gpointer         user_data,
		  GError         **error)
{
	GossipJabber *jabber;

	jabber = GOSSIP_JABBER (protocol);

	gossip_debug (DEBUG_DOMAIN, "Setting vcard for '%s'",
		      gossip_vcard_get_name (vcard));

	return gossip_jabber_vcard_set (jabber,
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
	LmMessage          *m;
	LmMessageNode      *node;
	GossipJabber       *jabber;
	GossipJabberPriv   *priv;
	GossipJID          *jid;
	GossipContact      *contact;
	GossipSubscription  subscription;
	gchar              *escaped;
	gboolean            add_to_roster;

	gossip_debug (DEBUG_DOMAIN, "Adding contact:'%s' with name:'%s' and group:'%s'",
		      id, name, group);

	jabber = GOSSIP_JABBER (protocol);
	priv = GET_PRIV (jabber);

	jid = gossip_jid_new (id);
	contact = g_hash_table_lookup (priv->contacts, gossip_jid_get_without_resource (jid));

	if (contact) {
		subscription = gossip_contact_get_subscription (contact);
	} else {
		subscription = GOSSIP_SUBSCRIPTION_NONE;
	}

	/* We would normally only add to roster IF not on it but we
	 * always do this because the server will inforce it otherwise
	 * and it makes sense to use our provided name/group, etc
	 */
	add_to_roster = TRUE;
/* 	add_to_roster = gossip_contact_get_type (contact) == GOSSIP_CONTACT_TYPE_TEMPORARY; */

	if (add_to_roster) {
		gossip_debug (DEBUG_DOMAIN, "Adding contact:'%s' to roster...", id);

		m = lm_message_new_with_sub_type (NULL,
						  LM_MESSAGE_TYPE_IQ,
						  LM_MESSAGE_SUB_TYPE_SET);

		node = lm_message_node_add_child (m->node, "query", NULL);
		lm_message_node_set_attributes (node,
						"xmlns", XMPP_ROSTER_XMLNS, NULL);

		node = lm_message_node_add_child (node, "item", NULL);

		escaped = g_markup_escape_text (name, -1);
		lm_message_node_set_attributes (node,
						"jid", gossip_jid_get_without_resource (jid),
						"name", escaped,
						NULL);
		g_free (escaped);

		if (group && g_utf8_strlen (group, -1) > 0) {
			escaped = g_markup_escape_text (group, -1);
			lm_message_node_add_child (node, "group", escaped);
			g_free (escaped);
		}

		lm_connection_send (priv->connection, m, NULL);
		lm_message_unref (m);
	}

	/* Request subscription */
	if (subscription == GOSSIP_SUBSCRIPTION_NONE ||
	    subscription == GOSSIP_SUBSCRIPTION_FROM) {
		gossip_debug (DEBUG_DOMAIN, "Sending subscribe request with message:'%s'...",
			      message);

		m = lm_message_new_with_sub_type (gossip_jid_get_without_resource (jid),
						  LM_MESSAGE_TYPE_PRESENCE,
						  LM_MESSAGE_SUB_TYPE_SUBSCRIBE);

		escaped = g_markup_escape_text (message, -1);
		lm_message_node_add_child (m->node, "status", escaped);
		g_free (escaped);

		lm_connection_send (priv->connection, m, NULL);
		lm_message_unref (m);
	} else {
		gossip_debug (DEBUG_DOMAIN, "NOT Sending subscribe request, "
			      "subscription is either TO or BOTH");
	}

	gossip_jid_unref (jid);
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
		escaped = g_markup_escape_text (l->data, -1);
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

	/* Next remove the contact from the roster */
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

	/* Remove current subscription */
	gossip_protocol_set_subscription (protocol, contact, FALSE);

	/* Remove from internal hash table */
	gossip_debug (DEBUG_DOMAIN,
		      "Contact:'%s' being removed with current ref count:%d",
		      gossip_contact_get_id (contact), G_OBJECT (contact)->ref_count);

	g_hash_table_remove (priv->contacts, contact);
}

static void
jabber_contact_update (GossipProtocol *protocol,
		       GossipContact  *contact)
{
	/* We set the groups _and_ the name here, the rename function
	 * will do exactly what we want to do so just call that.
	 */
	jabber_contact_rename (protocol,
			       contact,
			       gossip_contact_get_name (contact));
}

static void
jabber_contact_is_avatar_latest_cb (GossipResult  result,
				    GossipVCard  *vcard,
				    JabberData   *data)
{
	if (result == GOSSIP_RESULT_OK) {
		GossipAvatar *avatar;

		avatar = gossip_vcard_get_avatar (vcard);
		gossip_contact_set_avatar (data->contact, avatar);
		gossip_contact_set_vcard (data->contact, vcard);
	}

	jabber_data_free (data);
}

static void
jabber_contact_is_avatar_latest (GossipJabber  *jabber,
				 GossipContact *contact,
				 LmMessageNode *m,
				 gboolean       force_update)
{
	JabberData    *data;
	LmMessageNode *avatar_node;
	GossipAvatar  *avatar;
	gchar         *sha1;
	gboolean       same;

	if (!force_update) {
		avatar_node = lm_message_node_find_child (m, "photo");
		if (!avatar_node || !avatar_node->value) {
			gossip_contact_set_avatar (contact, NULL);
			return;
		}

		avatar = gossip_contact_get_avatar (contact);
		if (avatar) {
			sha1 = gossip_sha_hash (avatar->data, avatar->len);
		} else {
			sha1 = gossip_sha_hash (NULL, 0);
		}

		same = g_ascii_strcasecmp (sha1, avatar_node->value) == 0;
		g_free (sha1);

		if (same) {
			return;
		}
	}

	data = jabber_data_new (jabber, contact, NULL);
	gossip_jabber_vcard_get (jabber,
				 gossip_contact_get_id (contact),
				 (GossipVCardCallback) jabber_contact_is_avatar_latest_cb,
				 data,
				 NULL);
}

static void
jabber_contact_vcard_cb (GossipResult  result,
			 GossipVCard  *vcard,
			 JabberData   *data)
{
	if (result == GOSSIP_RESULT_OK) {
		GossipContact *own_contact;
		gchar         *name;
		GossipAvatar  *avatar;

		avatar = gossip_vcard_get_avatar (vcard);
		gossip_contact_set_avatar (data->contact, avatar);
		gossip_contact_set_vcard (data->contact, vcard);

		name = gossip_jabber_get_name_to_use
			(gossip_contact_get_id (data->contact),
			 gossip_vcard_get_nickname (vcard),
			 gossip_vcard_get_name (vcard));

		gossip_contact_set_name (data->contact, name);
		g_free (name);

		/* Send presence if this is the user's VCard
		 * (Avatar support, JEP-0153)
		 */
		own_contact = jabber_get_own_contact (GOSSIP_PROTOCOL (data->jabber));
		if (gossip_contact_equal (own_contact, data->contact)) {
			gossip_jabber_send_presence (data->jabber, NULL);
		}
	}

	jabber_data_free (data);
}

static void
jabber_contact_vcard (GossipJabber  *jabber,
		      GossipContact *contact)
{
	JabberData *data;

	data = jabber_data_new (jabber, contact, NULL);

	gossip_jabber_vcard_get (jabber,
				 gossip_contact_get_id (contact),
				 (GossipVCardCallback) jabber_contact_vcard_cb,
				 data,
				 NULL);
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

		/* Do not include the group we are renaming */
		if (group && strcmp (group, rg->group) == 0) {
			continue;
		}

		escaped = g_markup_escape_text (group, -1);
		lm_message_node_add_child (node, "group", escaped);
		g_free (escaped);
	}

	/* Add the new group name */
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
	GList          *list;
	GossipPresence *presence;

	list = gossip_contact_get_presence_list (contact);
	if (!list) {
		return NULL;
	}

	/* The first one is the active one. */
	presence = list->data;

	return gossip_presence_get_resource (presence);
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
		jid_str = gossip_contact_get_id (priv->contact);
	}

	return gossip_jabber_vcard_get (jabber,
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

//koko
void
jabber_set_proxy (LmConnection *conn)
{
	gboolean  use_http_proxy;
	gchar    *host;
	gint      port;
	gboolean  use_auth;
	gchar    *username;
	gchar    *password;

	gossip_conf_get_http_proxy (gossip_conf_get (),
				    &use_http_proxy,
				    &host,
				    &port,
				    &use_auth,
				    &username,
				    &password);

	if (use_http_proxy) {
		LmProxy  *proxy;

		proxy = lm_proxy_new_with_server (LM_PROXY_TYPE_HTTP,
						  host, (guint) port);
		lm_connection_set_proxy (conn, proxy);

		if (use_auth) {
			lm_proxy_set_username (proxy, username);
			lm_proxy_set_password (proxy, password);
		}

		lm_proxy_unref (proxy);
	} else {
		lm_connection_set_proxy (conn, NULL);
	}

	g_free (host);
	g_free (username);
	g_free (password);
}

static gboolean
jabber_composing_timeout_cb (JabberData *data)
{
	GossipJabberPriv *priv;

	priv = GET_PRIV (data->jabber);

	gossip_debug (DEBUG_DOMAIN, "Contact:'%s' is NOT composing (timed out)",
		      gossip_contact_get_id (data->contact));

	g_signal_emit_by_name (data->jabber, "composing", data->contact, FALSE);

	g_hash_table_remove (priv->composing_timeouts, data->contact);

	return FALSE;
}

static LmHandlerResult
jabber_message_handler (LmMessageHandler *handler,
			LmConnection     *conn,
			LmMessage        *m,
			GossipJabber     *jabber)
{
	LmMessageNode        *node;
	LmMessageSubType      sub_type;
	GossipJabberPriv     *priv;
	GossipMessage        *message;
	const gchar          *from_str;
	GossipContact        *from;
	const gchar          *thread = NULL;
	const gchar          *subject = NULL;
	const gchar          *body = NULL;
	GossipChatroomInvite *invite;
	const gchar          *resource;

	priv = GET_PRIV (jabber);

	gossip_debug (DEBUG_DOMAIN, "New message from:'%s'",
		      lm_message_node_get_attribute (m->node, "from"));

	sub_type = lm_message_get_sub_type (m);
	if (sub_type != LM_MESSAGE_SUB_TYPE_NOT_SET &&
	    sub_type != LM_MESSAGE_SUB_TYPE_NORMAL &&
	    sub_type != LM_MESSAGE_SUB_TYPE_CHAT &&
	    sub_type != LM_MESSAGE_SUB_TYPE_HEADLINE) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	from_str = lm_message_node_get_attribute (m->node, "from");
	from = gossip_jabber_get_contact_from_jid (jabber,
						   from_str,
						   NULL,
						   TRUE);

	if (gossip_jabber_get_message_is_event (m)) {
		gboolean composing;

		composing = gossip_jabber_get_message_is_composing (m);

		gossip_debug (DEBUG_DOMAIN, "Contact:'%s' %s",
			      gossip_contact_get_id (from),
			      composing ? "is composing" : "is NOT composing");

		g_signal_emit_by_name (jabber, "composing", from, composing);

		if (composing) {
			JabberData *data;
			guint       id;

			data = g_hash_table_lookup (priv->composing_timeouts, from);
			if (data) {
				g_source_remove (GPOINTER_TO_UINT (data->user_data));
				g_hash_table_remove (priv->composing_timeouts, from);
			}

			data = jabber_data_new (jabber, from, NULL);
			id = g_timeout_add (COMPOSING_TIMEOUT * 1000,
					    (GSourceFunc) jabber_composing_timeout_cb,
					    data);
			data->user_data = GUINT_TO_POINTER (id);
			g_hash_table_insert (priv->composing_timeouts, g_object_ref (from), data);
		} else {
			JabberData *data;

			data = g_hash_table_lookup (priv->composing_timeouts, from);
			if (data) {
				g_source_remove (GPOINTER_TO_UINT (data->user_data));
				g_hash_table_remove (priv->composing_timeouts, from);
			}
		}

		/* If event, then we can ignore the rest */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	} else {
		gboolean wants_composing;

		wants_composing = gossip_jabber_get_message_is_composing (m);

		if (wants_composing) {
			g_hash_table_insert (priv->composing_requests,
					     g_object_ref (from),
					     GINT_TO_POINTER (TRUE));
		} else {
			g_hash_table_remove (priv->composing_requests, from);
		}

		gossip_debug (DEBUG_DOMAIN, "Contact:'%s' %s composing info...",
			      gossip_contact_get_id (from),
			      wants_composing ? "wants" : "does NOT want");
	}

	node = lm_message_node_get_child (m->node, "subject");
	if (node) {
		subject = node->value;
	}

	invite = gossip_jabber_get_message_conference (jabber, m);
	if (invite) {
		GossipContact *invitor;

		invitor = gossip_chatroom_invite_get_invitor (invite);

		gossip_debug (DEBUG_DOMAIN, "Chat room invitiation from:'%s' for room:'%s', reason:'%s'",
			      gossip_contact_get_id (invitor),
			      gossip_chatroom_invite_get_id (invite),
			      gossip_chatroom_invite_get_reason (invite));

		/* We set the from to be the person that invited you,
		 * not the chatroom that sent the request on their
		 * behalf.
		 */
		from = invitor;

		/* Make sure we have some sort of body for
		 * invitations, since it is not necessary but should
		 * really exist.
		 */
		body = "";
	}

	node = lm_message_node_get_child (m->node, "body");
	if (node) {
		body = node->value;
	}

	if (!body && !invite) {
		/* If no body to the message, we ignore it since it
		 * has no purpose now (see fixes #309912).
		 */

		gossip_debug (DEBUG_DOMAIN, "Dropping new message, no <body> element");
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	node = lm_message_node_get_child (m->node, "thread");
	if (node) {
		thread = node->value;
	}

	message = gossip_message_new (GOSSIP_MESSAGE_TYPE_NORMAL,
				      priv->contact);

	/* To make the sender right in private chat messages sent from
	 * groupchats, we take the name from the resource, which carries the
	 * nick for those messages.
	 */
	if (gossip_jabber_chatrooms_get_jid_is_chatroom (priv->chatrooms,
							 from_str)) {
		GossipJID   *jid;
		const gchar *resource;

		jid = gossip_jid_new (from_str);
		resource = gossip_jid_get_resource (jid);
		if (!resource) {
			resource = "";
		}

		gossip_contact_set_name (from, resource);

		gossip_jid_unref (jid);
	}

	gossip_message_set_sender (message, from);
	gossip_message_set_body (message, body);

	resource = gossip_jid_string_get_part_resource (from_str);
	gossip_message_set_explicit_resource (message, resource);

	if (subject) {
		gossip_message_set_subject (message, subject);
	}

	if (thread) {
		gossip_message_set_thread (message, thread);
	}

	if (invite) {
		gossip_message_set_invite (message, invite);
		gossip_chatroom_invite_unref (invite);
	}

	gossip_message_set_timestamp (message,
				      gossip_jabber_get_message_timestamp (m));

	g_signal_emit_by_name (jabber, "new-message", message);
	g_signal_emit_by_name (jabber, "composing", from, FALSE);

	g_object_unref (message);

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
	gossip_debug (DEBUG_DOMAIN, "New presence from:'%s'",
		      lm_message_node_get_attribute (m->node, "from"));

	if (gossip_jabber_chatrooms_get_jid_is_chatroom (priv->chatrooms,
							 from)) {
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	contact = gossip_jabber_get_contact_from_jid (jabber, from, &new_item, TRUE);

	/* Get the type */
	type = lm_message_node_get_attribute (m->node, "type");
	if (!type) {
		type = "available";
	}

	if (strcmp (type, "subscribe") == 0) {
		g_signal_emit_by_name (jabber, "subscription-request", contact, NULL);

		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	} else if (strcmp (type, "subscribed") == 0) {
		/* Handled in the roster handling code */
		return LM_HANDLER_RESULT_REMOVE_MESSAGE;
	} else if (strcmp (type, "unsubscribed") == 0) {
		/* Handled in the roster handling code */
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
			if (gossip_contact_get_presence_list (contact)) {
				/* Check avatar xml tags to see if we
				 * have the latest.
				 */
				jabber_contact_is_avatar_latest (jabber, contact, m->node, FALSE);
			} else {
				/* Force retrieval of the latest avatar for
				 * the contact because no presence information
				 * should mean they were offline.
				 *
				 * The reason we do this is because
				 * some clients don't support the
				 * avatar xml tags (JEP 0153).
				 */
				jabber_contact_is_avatar_latest (jabber, contact, m->node, TRUE);
			}

			gossip_presence_set_resource (presence, resource);
			gossip_contact_add_presence (contact, presence);

			g_object_unref (presence);
		}

		gossip_jid_unref (jid);
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

	priv = GET_PRIV (jabber);

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
		/* Do nothing at this point */
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	if (xmlns && strcmp (xmlns, XMPP_VERSION_XMLNS) == 0) {
		jabber_request_version (jabber, m);
		return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
	}

	/* If a get, return error for unsupported IQ */
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

	/* Clean up */
	lm_message_handler_invalidate (handler);

	/* Send subscribed to them */
	gossip_debug (DEBUG_DOMAIN, "Sending subscribed message to new service:'%s'", to);
	new_message = lm_message_new_with_sub_type (to,
						    LM_MESSAGE_TYPE_PRESENCE,
						    LM_MESSAGE_SUB_TYPE_SUBSCRIBED);

	own_contact = gossip_jabber_get_own_contact (jabber);
	id = gossip_contact_get_id (own_contact);
	lm_message_node_set_attribute (new_message->node, "from", id);

	lm_connection_send (connection, new_message, NULL);
	lm_message_unref (new_message);

	/* Send our presence */
	new_message = lm_message_new_with_sub_type (to,
						    LM_MESSAGE_TYPE_PRESENCE,
						    LM_MESSAGE_SUB_TYPE_AVAILABLE);

	lm_message_node_set_attribute (new_message->node, "from", id);

	lm_connection_send (connection, new_message, NULL);
	lm_message_unref (new_message);

	g_free (to);

	return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


/*
 * Requests
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

	gossip_debug (DEBUG_DOMAIN, "Version request from:'%s'", from);

	r = lm_message_new_with_sub_type (from,
					  LM_MESSAGE_TYPE_IQ,
					  LM_MESSAGE_SUB_TYPE_RESULT);
	if (id) {
		lm_message_node_set_attributes (r->node,
						"id", id,
						NULL);
	}

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
		GossipContact     *contact;
		GossipContactType  type;
		const gchar       *jid_str;
		const gchar       *subscription;
		gboolean           added_item = FALSE;
		gboolean           removed_item = FALSE;
		LmMessageNode     *subnode;
		LmMessageNode     *child;
		GList             *groups;
		const gchar       *name;
		GList             *new_groups;

		if (strcmp (node->name, "item") != 0) {
			continue;
		}

		jid_str = lm_message_node_get_attribute (node, "jid");
		if (!jid_str) {
			continue;
		}

		contact = gossip_jabber_get_contact_from_jid (jabber,
							      jid_str,
							      &added_item,
							      FALSE);

		type = gossip_contact_get_type (contact);

		/* Groups */
		groups = NULL;
		for (subnode = node->children; subnode; subnode = subnode->next) {
			if (strcmp (subnode->name, "group") != 0) {
				continue;
			}

			if (subnode->value) {
				/* FIXME: unescape the markup here: #342927 */
				groups = g_list_append (groups, subnode->value);
			}
		}

		/* FIXME: why is this here if we set the groups below */
		if (groups) {
			gossip_contact_set_groups (contact, groups);
		}

		g_list_free (groups);

		/* Subscription */
		subscription = lm_message_node_get_attribute (node, "subscription");
		if (contact && subscription) {
			GossipContactType  contact_type;
			GossipSubscription subscription_type;

			if (strcmp (subscription, "remove") == 0) {
				g_signal_emit_by_name (jabber, "contact-removed", contact);
				g_hash_table_remove (priv->contacts, gossip_contact_get_id (contact));
				continue;
			} else if (strcmp (subscription, "both") == 0) {
				subscription_type = GOSSIP_SUBSCRIPTION_BOTH;
			} else if (strcmp (subscription, "to") == 0) {
				subscription_type = GOSSIP_SUBSCRIPTION_TO;
			} else if (strcmp (subscription, "from") == 0) {
				subscription_type = GOSSIP_SUBSCRIPTION_FROM;
			} else {
				subscription_type = GOSSIP_SUBSCRIPTION_NONE;
			}

			/* In the rare cases where we have this state,
			 * NONE means that we are in the process of
			 * setting up subscription so the contact is
			 * still temporary, any other state and we
			 * assume they must be a proper contact list
			 * contact.
			 *
			 * Also, later when we present the
			 * subscription dialog to the user, we need to
			 * know if user is temporary contact or an old
			 * contact so we can silently accept
			 * subscription requests for people already on
			 * the roster with "to" or "from" conditions.
			 */
			if (subscription_type != GOSSIP_SUBSCRIPTION_NONE) {
				g_object_get (contact, "type", &contact_type, NULL);
				if (contact_type != GOSSIP_CONTACT_TYPE_CONTACTLIST) {
					added_item = TRUE;
				}
				
				contact_type = GOSSIP_CONTACT_TYPE_CONTACTLIST;
				g_object_set (contact, "type", contact_type, NULL);
			} else {
				g_object_get (contact, "type", &contact_type, NULL);
				if (contact_type == GOSSIP_CONTACT_TYPE_CONTACTLIST) {
					removed_item = TRUE;
				}

				contact_type = GOSSIP_CONTACT_TYPE_TEMPORARY;
				g_object_set (contact, "type", contact_type, NULL);
			}

			gossip_contact_set_subscription (contact, subscription_type);
		}

		name = lm_message_node_get_attribute (node, "name");
		if (name) {
			gossip_contact_set_name (contact, name);
		}

		new_groups = NULL;
		for (child = node->children; child; child = child->next) {
			if (strcmp (child->name, "group") == 0 && child->value) {
				new_groups = g_list_append (new_groups, child->value);
			}
		}

		if (new_groups) {
			gossip_contact_set_groups (contact, new_groups);
		}

		g_list_free (new_groups);

		if (added_item) {
			g_signal_emit_by_name (jabber, "contact-added", contact);
		}

		if (removed_item) {
			g_signal_emit_by_name (jabber, "contact-removed", contact);
			g_hash_table_remove (priv->contacts, gossip_contact_get_id (contact));
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
	iface->find_by_id      = jabber_chatroom_find_by_id;
	iface->find            = jabber_chatroom_find;
	iface->invite          = jabber_chatroom_invite;
	iface->invite_accept   = jabber_chatroom_invite_accept;
	iface->invite_decline  = jabber_chatroom_invite_decline;
	iface->get_rooms       = jabber_chatroom_get_rooms;
	iface->browse_rooms    = jabber_chatroom_browse_rooms;
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

	gossip_jabber_chatrooms_cancel (priv->chatrooms, id);
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

static GossipChatroom *
jabber_chatroom_find_by_id (GossipChatroomProvider *provider,
			    GossipChatroomId        id)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_find_by_id (priv->chatrooms, id);
}

static GossipChatroom *
jabber_chatroom_find (GossipChatroomProvider *provider,
		      GossipChatroom         *chatroom)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), NULL);
	g_return_val_if_fail (GOSSIP_IS_CHATROOM (chatroom), NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_find (priv->chatrooms, chatroom);
}

static void
jabber_chatroom_invite (GossipChatroomProvider *provider,
			GossipChatroomId        id,
			GossipContact          *contact,
			const gchar            *reason)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (GOSSIP_IS_CONTACT (contact));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_invite (priv->chatrooms, id, contact, reason);
}

static void
jabber_chatroom_invite_accept (GossipChatroomProvider *provider,
			       GossipChatroomJoinCb    callback,
			       GossipChatroomInvite   *invite,
			       const gchar            *nickname)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_invite_accept (priv->chatrooms,
					       callback,
					       invite,
					       nickname);
}

static void
jabber_chatroom_invite_decline (GossipChatroomProvider *provider,
				GossipChatroomInvite   *invite,
				const gchar            *reason)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_invite_decline (priv->chatrooms,
						invite,
						reason);
}

static GList *
jabber_chatroom_get_rooms (GossipChatroomProvider *provider)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_JABBER (provider), NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	return gossip_jabber_chatrooms_get_rooms (priv->chatrooms);
}

static void
jabber_chatroom_browse_rooms (GossipChatroomProvider *provider,
			      const gchar            *server,
			      GossipChatroomBrowseCb  callback,
			      gpointer                user_data)
{
	GossipJabber     *jabber;
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (provider));
	g_return_if_fail (server != NULL);
	g_return_if_fail (callback != NULL);

	jabber = GOSSIP_JABBER (provider);
	priv = GET_PRIV (jabber);

	gossip_jabber_chatrooms_browse_rooms (priv->chatrooms, server, 
					      callback, user_data);
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

	gossip_jabber_ft_cancel (priv->fts, id);
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

	gossip_jabber_ft_accept (priv->fts, id);
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

	gossip_jabber_ft_decline (priv->fts, id);
}

/*
 * Misc
 */

static JabberData *
jabber_data_new (GossipJabber  *jabber,
		 GossipContact *contact,
		 gpointer       user_data)
{
	JabberData *data;

	g_return_val_if_fail (GOSSIP_IS_JABBER (jabber), NULL);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), NULL);

	data = g_slice_new0 (JabberData);

	data->jabber = g_object_ref (jabber);
	data->contact = g_object_ref (contact);

	data->user_data = user_data;

	return data;
}

static void
jabber_data_free (JabberData *data)
{
	if (!data) {
		return;
	}

	if (data->jabber) {
		g_object_unref (data->jabber);
	}

	if (data->contact) {
		g_object_unref (data->contact);
	}

	g_slice_free (JabberData, data);
}

/*
 * External functions
 */

GQuark
gossip_jabber_error_quark (void)
{
	return g_quark_from_static_string (GOSSIP_JABBER_ERROR_DOMAIN);
}

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
				    gboolean     *new_item,
				    gboolean      get_vcard)
{
	GossipJabberPriv *priv;
	GossipContact    *contact;
	GossipJID        *jid;
	gboolean          tmp_new_item = FALSE;

	priv = GET_PRIV (jabber);

	jid = gossip_jid_new (jid_str);

	contact = g_hash_table_lookup (priv->contacts,
				       gossip_jid_get_without_resource (jid));

	if (!contact) {
		gossip_debug (DEBUG_DOMAIN,
			      "New contact:'%s' (GOSSIP_CONTACT_TYPE_TEMPORARY)",
			      gossip_jid_get_full (jid));
		contact = gossip_contact_new (GOSSIP_CONTACT_TYPE_TEMPORARY,
					      priv->account);
		gossip_contact_set_id (contact,
				       gossip_jid_get_full (jid));
		gossip_contact_set_name (contact,
					 gossip_jid_get_without_resource (jid));

		tmp_new_item = TRUE;

		g_hash_table_insert (priv->contacts,
				     g_strdup (gossip_jid_get_without_resource (jid)),
				     contact);

		if (get_vcard) {
			/* Request contacts VCard details so we can get the
			 * real name for them for chat windows, etc
			 */
			jabber_contact_vcard (jabber, contact);
		}
	} else {
		GossipContactType  type;
		const gchar       *type_str;

		type = gossip_contact_get_type (contact);

		switch (type) {
		case GOSSIP_CONTACT_TYPE_TEMPORARY:
			type_str = "GOSSIP_CONTACT_TYPE_TEMPORARY";
			break;
		case GOSSIP_CONTACT_TYPE_CONTACTLIST:
			type_str = "GOSSIP_CONTACT_TYPE_CONTACTLIST";
			break;
		case GOSSIP_CONTACT_TYPE_CHATROOM:
			type_str = "GOSSIP_CONTACT_TYPE_CHATROOM";
			break;
		case GOSSIP_CONTACT_TYPE_USER:
			type_str = "GOSSIP_CONTACT_TYPE_USER";
			break;
		default:
			type_str = "unknown";
			break;
		}

		gossip_debug (DEBUG_DOMAIN,
			      "Get contact:'%s', type:%d-->'%s'",
			      gossip_jid_get_full (jid), type, type_str);
	}

	gossip_jid_unref (jid);

	if (new_item) {
		*new_item = tmp_new_item;
	}

	return contact;
}

void
gossip_jabber_send_presence (GossipJabber   *jabber,
			     GossipPresence *presence)
{
	GossipJabberPriv *priv;

	g_return_if_fail (GOSSIP_IS_JABBER (jabber));

	priv = GET_PRIV (jabber);

	jabber_set_presence (GOSSIP_PROTOCOL (jabber),
			     presence ? presence : priv->presence);
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

	/* Set up handler to sliently catch the subscription request */
	handler = lm_message_handler_new
		((LmHandleMessageFunction) jabber_subscription_message_handler,
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

/*
 * Private functions
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

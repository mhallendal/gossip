/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004-2005 Imendio AB
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

#include "libgossip-marshal.h"
#include "gossip-session.h"

#define DEBUG_MSG(x) 
/* #define DEBUG_MSG(args) g_printerr args ; g_printerr ("\n"); */

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_SESSION, GossipSessionPriv))

typedef struct _GossipSessionPriv  GossipSessionPriv;

struct _GossipSessionPriv {
 	GossipAccountManager *account_manager; 

	GHashTable           *accounts;
	GList                *protocols;

	GossipPresence       *presence;

	GList                *contacts;

	guint                 connected_counter;
	guint                 connecting_counter;

	GHashTable           *timers; /* connected time */
};

struct FindAccount {
	const gchar   *contact_id;
	GossipAccount *account;
};

struct CountAccounts {
	GossipSession *session;
	guint          connected;
	guint          disconnected;
};

struct GetAccounts {
	GossipSession *session;
	GList         *accounts;
};

struct ConnectData {
	GossipSession *session;
	gboolean       startup;
};

static void            gossip_session_class_init                 (GossipSessionClass   *klass);
static void            gossip_session_init                       (GossipSession        *session);
static void            session_finalize                          (GObject              *object);
static void            session_protocol_signals_setup            (GossipSession        *session,
								  GossipProtocol       *protocol);
static void            session_protocol_logged_in                (GossipProtocol       *protocol,
								  GossipAccount        *account,
								  GossipSession        *session);
static void            session_protocol_logged_out               (GossipProtocol       *protocol,
								  GossipAccount        *account,
								  GossipSession        *session);
static void            session_protocol_new_message              (GossipProtocol       *protocol,
								  GossipMessage        *message,
								  GossipSession        *session);
static void            session_protocol_contact_added            (GossipProtocol       *protocol,
								  GossipContact        *contact,
								  GossipSession        *session);
static void            session_protocol_contact_updated          (GossipProtocol       *protocol,
								  GossipContact        *contact,
								  GossipSession        *session);
static void            session_protocol_contact_presence_updated (GossipProtocol       *protocol,
								  GossipContact        *contact,
								  GossipSession        *session);
static void            session_protocol_contact_removed          (GossipProtocol       *protocol,
								  GossipContact        *contact,
								  GossipSession        *session);
static void            session_protocol_composing                (GossipProtocol       *protocol,
								  GossipContact        *contact,
								  gboolean              composing,
								  GossipSession        *session);
static gchar *         session_protocol_get_password             (GossipProtocol       *protocol,
								  GossipAccount        *account,
								  GossipSession        *session);
static void            session_protocol_error                    (GossipProtocol       *protocol,
								  GossipAccount        *account,
								  GError               *error,
								  GossipSession        *session);
static GossipProtocol *session_get_protocol                      (GossipSession        *session,
								  GossipContact        *contact);
static void            session_get_accounts_foreach_cb           (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  struct GetAccounts   *ga);
static void            session_count_accounts_foreach_cb         (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  struct CountAccounts *ca);
static void            session_find_account_foreach_cb           (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  struct FindAccount   *fa);
static void            session_account_added_cb                  (GossipAccountManager *manager,
								  GossipAccount        *account,
								  GossipSession        *session);
static void            session_account_removed_cb                (GossipAccountManager *manager,
								  GossipAccount        *account,
								  GossipSession        *session);
static void            session_connect                           (GossipSession        *session,
								  GossipAccount        *account);
static void            session_connect_foreach_cb                (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  struct ConnectData   *cd);
static void            session_disconnect                        (GossipSession        *session,
								  GossipAccount        *account);
static void            session_disconnect_foreach_cb             (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  GossipSession        *session);
	
enum {
	CONNECTING,
	CONNECTED,
	DISCONNECTING,
	DISCONNECTED,
	PROTOCOL_CONNECTING,
	PROTOCOL_CONNECTED,
	PROTOCOL_DISCONNECTED,
	PROTOCOL_DISCONNECTING,
	PROTOCOL_ERROR,
	NEW_MESSAGE,
	PRESENCE_CHANGED,
	CONTACT_ADDED,
	CONTACT_UPDATED,
	CONTACT_PRESENCE_UPDATED,
	CONTACT_REMOVED,
	COMPOSING,

	/* Used for protocols to retreive information from UI */
	GET_PASSWORD,
	LAST_SIGNAL
};

static guint    signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (GossipSession, gossip_session, G_TYPE_OBJECT);

static void
gossip_session_class_init (GossipSessionClass *klass)
{
	GObjectClass *object_class;
	
	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = session_finalize;

	signals[CONNECTING] = 
		g_signal_new ("connecting",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, 
			      NULL, NULL,
			      libgossip_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[CONNECTED] = 
		g_signal_new ("connected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, 
			      NULL, NULL,
			      libgossip_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[DISCONNECTING] = 
		g_signal_new ("disconnecting",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, 
			      NULL, NULL,
			      libgossip_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[DISCONNECTED] = 
		g_signal_new ("disconnected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, 
			      NULL, NULL,
			      libgossip_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[PROTOCOL_CONNECTING] = 
		g_signal_new ("protocol-connecting",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 
			      1, GOSSIP_TYPE_ACCOUNT);

	signals[PROTOCOL_CONNECTED] = 
		g_signal_new ("protocol-connected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT_OBJECT,
			      G_TYPE_NONE, 
			      2, GOSSIP_TYPE_ACCOUNT, GOSSIP_TYPE_PROTOCOL);
	
	signals[PROTOCOL_DISCONNECTED] = 
		g_signal_new ("protocol-disconnected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT_OBJECT,
			      G_TYPE_NONE, 
			      2, GOSSIP_TYPE_ACCOUNT, GOSSIP_TYPE_PROTOCOL);

	signals[PROTOCOL_DISCONNECTING] = 
		g_signal_new ("protocol-disconnecting",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 
			      1, GOSSIP_TYPE_ACCOUNT);
	
	signals[PROTOCOL_ERROR] = 
		g_signal_new ("protocol-error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT_OBJECT_POINTER,
			      G_TYPE_NONE, 
			      3, GOSSIP_TYPE_PROTOCOL, GOSSIP_TYPE_ACCOUNT, G_TYPE_POINTER);

	signals[NEW_MESSAGE] = 
		g_signal_new ("new-message",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_MESSAGE);

	signals[PRESENCE_CHANGED] = 
		g_signal_new ("presence-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_PRESENCE);

	signals[CONTACT_ADDED] = 
		g_signal_new ("contact-added",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_CONTACT);
	signals[CONTACT_UPDATED] = 
		g_signal_new ("contact-updated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_CONTACT);

	signals[CONTACT_PRESENCE_UPDATED] = 
		g_signal_new ("contact-presence-updated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_CONTACT);

	signals[CONTACT_REMOVED] = 
		g_signal_new ("contact-removed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_CONTACT);
	signals[COMPOSING] =
		g_signal_new ("composing",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__OBJECT_BOOLEAN,
			      G_TYPE_NONE,
			      2, GOSSIP_TYPE_CONTACT, G_TYPE_BOOLEAN);
	signals[GET_PASSWORD] =
		g_signal_new ("get-password",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_STRING__OBJECT,
			      G_TYPE_STRING,
			      1, GOSSIP_TYPE_ACCOUNT);
	
	g_type_class_add_private (object_class, sizeof (GossipSessionPriv));
}

static void
gossip_session_init (GossipSession *session)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (session);
	
	priv->accounts = g_hash_table_new_full (gossip_account_hash, 
						gossip_account_equal,
						g_object_unref,
						g_object_unref);

	priv->protocols = NULL;

	priv->connected_counter = 0;

	priv->timers = g_hash_table_new_full (gossip_account_hash, 
					      gossip_account_equal,
					      g_object_unref,
					      (GDestroyNotify)g_timer_destroy);

}

static void
session_finalize (GObject *object)
{
	GossipSessionPriv *priv;
	GList             *l;

	priv = GET_PRIV (object);
	
	if (priv->accounts) {
		g_hash_table_destroy (priv->accounts);
	}

	if (priv->protocols) {
		g_list_foreach (priv->protocols, 
				(GFunc)g_object_unref,
				NULL);
		g_list_free (priv->protocols);
	}

	if (priv->contacts) {
		for (l = priv->contacts; l; l = l->next) {
			g_object_unref (l->data);
		}

		g_list_free (priv->contacts);
	}

	if (priv->presence) {
		g_object_unref (priv->presence);
	}

	if (priv->timers) {
		g_hash_table_destroy (priv->timers);
	}

	g_signal_handlers_disconnect_by_func (priv->account_manager,
					      session_account_added_cb, 
					      GOSSIP_SESSION (object));

	g_signal_handlers_disconnect_by_func (priv->account_manager,
					      session_account_removed_cb, 
					      GOSSIP_SESSION (object));

 	if (priv->account_manager) { 
 		g_object_unref (priv->account_manager); 
 	} 

	(G_OBJECT_CLASS (gossip_session_parent_class)->finalize) (object);
}

static void
session_protocol_signals_setup (GossipSession  *session, 
				GossipProtocol *protocol)
{
	g_signal_connect (protocol, "logged-in", 
			  G_CALLBACK (session_protocol_logged_in),
			  session);
	g_signal_connect (protocol, "logged-out",
			  G_CALLBACK (session_protocol_logged_out),
			  session);
	g_signal_connect (protocol, "new_message",
			  G_CALLBACK (session_protocol_new_message),
			  session);
	g_signal_connect (protocol, "contact-added",
			  G_CALLBACK (session_protocol_contact_added),
			  session);
	g_signal_connect (protocol, "contact-updated",
			  G_CALLBACK (session_protocol_contact_updated),
			  session);
	g_signal_connect (protocol, "contact-presence-updated",
			  G_CALLBACK (session_protocol_contact_presence_updated),
			  session);
	g_signal_connect (protocol, "contact-removed",
			  G_CALLBACK (session_protocol_contact_removed),
			  session);
	g_signal_connect (protocol, "composing",
			  G_CALLBACK (session_protocol_composing),
			  session);
	g_signal_connect (protocol, "get-password",
			  G_CALLBACK (session_protocol_get_password),
			  session);
	g_signal_connect (protocol, "error", 
			  G_CALLBACK (session_protocol_error),
			  session);
}

static void
session_protocol_logged_in (GossipProtocol *protocol,
			    GossipAccount  *account,
			    GossipSession  *session)
{
	GossipSessionPriv *priv;
	GTimer            *timer;

	DEBUG_MSG (("Session: Protocol logged in"));

	priv = GET_PRIV (session);

	/* Setup timer */
	timer = g_timer_new ();
	g_timer_start (timer);

	g_hash_table_insert (priv->timers, 
			     g_object_ref (account), 
			     timer);
	
	/* Update some status? */
	priv->connected_counter++;

	if (priv->connecting_counter > 0) {
		priv->connecting_counter--;
	}

	g_signal_emit (session, signals[PROTOCOL_CONNECTED], 0, account, protocol);

	if (priv->connected_counter == 1) {
		/* Before this connect the session was set to be DISCONNECTED */
		g_signal_emit (session, signals[CONNECTED], 0);
	}
}

static void
session_protocol_logged_out (GossipProtocol *protocol, 
			     GossipAccount  *account,
			     GossipSession  *session) 
{
	GossipSessionPriv *priv;
	gdouble            seconds;

	seconds = gossip_session_get_connected_time (session, account);
	DEBUG_MSG (("Session: Protocol logged out (after %.2f seconds)", seconds));

	priv = GET_PRIV (session);

	/* Remove timer */
	g_hash_table_remove (priv->timers, account);

	/* Update some status? */
	if (priv->connected_counter < 0) {
		g_warning ("We have some issues in the connection counting");
		return;
	} 

	/* Don't go lower than 0 */
	if (priv->connected_counter > 0) {
		priv->connected_counter--;
	}

	if (priv->connecting_counter > 0) {
		priv->connecting_counter--;
	}

	g_signal_emit (session, signals[PROTOCOL_DISCONNECTED], 0, account, protocol);
	
	if (priv->connected_counter == 0) {
		/* Last connected protocol was disconnected */
		g_signal_emit (session, signals[DISCONNECTED], 0);
	}
}

static void
session_protocol_new_message (GossipProtocol *protocol,
			      GossipMessage  *message,
			      GossipSession  *session)
{
	/* Just relay the signal */
	g_signal_emit (session, signals[NEW_MESSAGE], 0, message);
}

static void
session_protocol_contact_added (GossipProtocol *protocol,
				GossipContact  *contact,
				GossipSession  *session)
{
	GossipSessionPriv *priv;

	DEBUG_MSG (("Session: Contact added '%s'",
		   gossip_contact_get_name (contact)));

	priv = GET_PRIV (session);
	
	priv->contacts = g_list_prepend (priv->contacts, 
					 g_object_ref (contact));
	
	g_signal_emit (session, signals[CONTACT_ADDED], 0, contact);
}

static void
session_protocol_contact_updated (GossipProtocol *protocol,
				  GossipContact  *contact,
				  GossipSession  *session)
{
	DEBUG_MSG (("Session: Contact updated '%s'",
		   gossip_contact_get_name (contact)));

	g_signal_emit (session, signals[CONTACT_UPDATED], 0, contact);
}

static void     
session_protocol_contact_presence_updated (GossipProtocol *protocol,
					   GossipContact  *contact,
					   GossipSession  *session)
{
	DEBUG_MSG (("Session: Contact presence updated '%s'",
		   gossip_contact_get_name (contact)));
	g_signal_emit (session, signals[CONTACT_PRESENCE_UPDATED], 0, contact);
}

static void 
session_protocol_contact_removed (GossipProtocol *protocol,
				  GossipContact  *contact,
				  GossipSession  *session)
{
	GossipSessionPriv *priv;
	
	DEBUG_MSG (("Session: Contact removed '%s'",
		   gossip_contact_get_name (contact)));

	priv = GET_PRIV (session);
	
	g_signal_emit (session, signals[CONTACT_REMOVED], 0, contact);

	priv->contacts = g_list_remove (priv->contacts, contact);
	g_object_unref (contact);
}

static void 
session_protocol_composing (GossipProtocol *protocol,
			    GossipContact  *contact,
			    gboolean        composing,
			    GossipSession  *session)
{
	GossipSessionPriv *priv;
	
	DEBUG_MSG (("Session: Contact %s composing:'%s'",
		   composing ? "is" : "is not",
		   gossip_contact_get_name (contact)));

	priv = GET_PRIV (session);
	
	g_signal_emit (session, signals[COMPOSING], 0, contact, composing);
}

static gchar *
session_protocol_get_password (GossipProtocol *protocol,
			       GossipAccount  *account,
			       GossipSession  *session)
{
	gchar *password = NULL;

	DEBUG_MSG (("Session: Get password"));

	g_signal_emit (session, signals[GET_PASSWORD], 0, account, &password);
	
	return password;
}

static void
session_protocol_error (GossipProtocol *protocol,
			GossipAccount  *account,
			GError         *error,
			GossipSession  *session)
{
	GossipSessionPriv *priv;

	DEBUG_MSG (("Session: Error:%d->'%s'", 
		   error->code, error->message));

	priv = GET_PRIV (session);

	if (priv->connecting_counter > 0) {
		priv->connecting_counter--;
	}

	g_signal_emit (session, signals[PROTOCOL_ERROR], 0, protocol, account, error); 
}

static GossipProtocol *
session_get_protocol (GossipSession *session, 
		      GossipContact *contact)
{
	GossipSessionPriv *priv;
	GList             *l;
	const gchar       *id;
	
	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), NULL);

	priv = GET_PRIV (session);

	id = gossip_contact_get_id (contact);

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		GossipContact  *this_contact;
		
		protocol = l->data;

		this_contact = gossip_protocol_find_contact (protocol, id);
		if (!this_contact) {
			continue;
		}
	     
		if (gossip_contact_equal (this_contact, contact)) {
			return protocol;
		}
	}

	return NULL;
}

GossipSession *
gossip_session_new (GossipAccountManager *manager)
{
	GossipSession     *session;
	GossipSessionPriv *priv;
	GList             *accounts, *l;
 
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT_MANAGER (manager), NULL);

	session = g_object_new (GOSSIP_TYPE_SESSION, NULL);

	priv = GET_PRIV (session);

	priv->account_manager = g_object_ref (manager);

	g_signal_connect (priv->account_manager, "account_added",
			  G_CALLBACK (session_account_added_cb), 
			  session);

	g_signal_connect (priv->account_manager, "account_removed",
			  G_CALLBACK (session_account_removed_cb), 
			  session);

	accounts = gossip_account_manager_get_accounts (manager);
	
	for (l = accounts; l; l = l->next) {
		GossipAccount *account = l->data;
		
		gossip_session_add_account (session, account);
	}

	g_list_free (accounts);

	return session;
}

GossipProtocol *
gossip_session_get_protocol (GossipSession *session,
			     GossipAccount *account)
{
	GossipSessionPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), NULL);

	priv = GET_PRIV (session);

	return g_hash_table_lookup (priv->accounts, account);
}

GossipAccountManager *
gossip_session_get_account_manager (GossipSession *session) 
{
	GossipSessionPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);

	priv = GET_PRIV (session);

	return priv->account_manager;
}

GList *
gossip_session_get_accounts (GossipSession *session)
{
	GossipSessionPriv  *priv;
	struct GetAccounts  ga;
	GList              *list;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	
	priv = GET_PRIV (session);

	ga.session = session;
	ga.accounts = NULL;

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_get_accounts_foreach_cb,
			      &ga);

	list = ga.accounts;

	return list;
}

static void
session_get_accounts_foreach_cb (GossipAccount      *account,
				 GossipProtocol     *protocol,
				 struct GetAccounts *ga)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (ga->session);

	ga->accounts = g_list_append (ga->accounts, g_object_ref (account));
}

gdouble
gossip_session_get_connected_time (GossipSession *session,
				   GossipAccount *account)
{
	GossipSessionPriv *priv;
	GTimer            *timer;
	gulong             ms;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), 0);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), 0);

	priv = GET_PRIV (session);

	timer = g_hash_table_lookup (priv->timers, account);
	if (!timer) {
		return 0;
	}

	return g_timer_elapsed (timer, &ms);
}

void 
gossip_session_count_accounts (GossipSession *session,
			       guint         *connected,
			       guint         *connecting,
			       guint         *disconnected)
{
	GossipSessionPriv    *priv;
	struct CountAccounts  ca;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));
	
	priv = GET_PRIV (session);

	ca.session = session;

	ca.connected = 0;
	ca.disconnected = 0;

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_count_accounts_foreach_cb,
			      &ca);

	if (connected) {
		*connected = ca.connected;
	}

	if (disconnected) {
		*disconnected = ca.disconnected;
	}

	if (connecting) {
		*connecting = priv->connecting_counter;
	}
}

static void
session_count_accounts_foreach_cb (GossipAccount        *account,
				   GossipProtocol       *protocol,
				   struct CountAccounts *ca)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (ca->session);

	if (gossip_protocol_is_connected (protocol)) {
		ca->connected++;
	} else {
		ca->disconnected++;
	}
}

gboolean 
gossip_session_add_account (GossipSession *session,
			    GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;
	GossipAccountType  type;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);

	priv = GET_PRIV (session);

	/* check this account is not already set up */
	protocol = g_hash_table_lookup (priv->accounts, account);
	
	if (protocol) {
		/* already added */
		return TRUE;
	}

	type = gossip_account_get_type (account);
	protocol = gossip_protocol_new_from_account_type (type);
	g_return_val_if_fail (GOSSIP_IS_PROTOCOL (protocol), FALSE);

	/* add to list */
	priv->protocols = g_list_append (priv->protocols, 
					 g_object_ref (protocol));

	/* add to hash table */
	g_hash_table_insert (priv->accounts, 
			     g_object_ref (account), 
			     g_object_ref (protocol));

	/* connect up all signals */ 
	session_protocol_signals_setup (session, protocol);

	/* clean up */
	g_object_unref (protocol);
			
	return TRUE;
}

gboolean
gossip_session_remove_account (GossipSession *session,
			       GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);

	priv = GET_PRIV (session);

	/* get protocol details for this account */
	protocol = g_hash_table_lookup (priv->accounts, account);
	
	if (!protocol) {
		/* not added in the first place */
		return TRUE;
	}

	/* remove from list */
	priv->protocols = g_list_remove (priv->protocols, protocol);
	g_object_unref (protocol);

	/* remove from hash table */
	return g_hash_table_remove (priv->accounts, account);
}

GossipAccount * 
gossip_session_find_account (GossipSession *session,
			     GossipContact *contact)
{
	GossipSessionPriv  *priv;
	struct FindAccount  fa;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), NULL);
	
	priv = GET_PRIV (session);

	fa.contact_id = gossip_contact_get_id (contact);
	fa.account = NULL;

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_find_account_foreach_cb,
			      &fa);

	return fa.account;
}

static void
session_find_account_foreach_cb (GossipAccount      *account,
				 GossipProtocol     *protocol,
				 struct FindAccount *fa)
{
	if (gossip_protocol_find_contact (protocol, fa->contact_id)) {
		fa->account = g_object_ref (account);
	}
}

static void
session_account_added_cb (GossipAccountManager *manager,
			  GossipAccount        *account,
			  GossipSession        *session)
{
	gossip_session_add_account (session, account);
}

static void
session_account_removed_cb (GossipAccountManager *manager,
			    GossipAccount        *account,
			    GossipSession        *session)
{
	gossip_session_remove_account (session, account);
}

void
gossip_session_connect (GossipSession *session,
			GossipAccount *account,
			gboolean       startup)
{
	GossipSessionPriv  *priv;
	struct ConnectData  cd;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	g_return_if_fail (priv->accounts != NULL);

	g_signal_emit (session, signals[CONNECTING], 0);

	/* Temporary */
	if (!priv->presence) {
		priv->presence = gossip_presence_new_full (GOSSIP_PRESENCE_STATE_AVAILABLE, 
							   NULL);
	}

	/* Connect one account if provided */
	if (account) {
		session_connect (session, account);
		return;
	}

	cd.session = session;
	cd.startup = startup;

	/* Connect ALL accounts if no one account is specified */
	g_hash_table_foreach (priv->accounts,
			      (GHFunc) session_connect_foreach_cb,
			      &cd);
}

static void
session_connect (GossipSession *session,
		 GossipAccount *account) 
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);
	
	if (gossip_protocol_is_connected (protocol)) {
		return;
	}
	
	g_signal_emit (session, signals[PROTOCOL_CONNECTING], 0, account);
	priv->connecting_counter++;

	/* can we not just pass the GossipAccount on the GObject init? */
	gossip_protocol_setup (protocol, account);
	
	/* setup the network connection */
	gossip_protocol_login (protocol);
}

static void
session_connect_foreach_cb (GossipAccount      *account,
			    GossipProtocol     *protocol,
			    struct ConnectData *cd)
{
 	if (cd->startup && 
	    !gossip_account_get_auto_connect (account)) { 
 		return; 
 	} 

	session_connect (cd->session, account);
}

void
gossip_session_disconnect (GossipSession *session,
			   GossipAccount *account)
{
	GossipSessionPriv *priv;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	g_signal_emit (session, signals[DISCONNECTING], 0);

	/* disconnect one account if provided */
	if (account) {
		session_disconnect (session, account);
		return;
	}

	/* disconnect ALL accounts if no one account is specified */
	g_hash_table_foreach (priv->accounts,
			      (GHFunc)session_disconnect_foreach_cb,
			      session);
}


static void
session_disconnect (GossipSession *session,
		    GossipAccount *account) 
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	priv = GET_PRIV (session);
	
	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));
	
	protocol = g_hash_table_lookup (priv->accounts, account);
	
	g_signal_emit (session, signals[PROTOCOL_DISCONNECTING], 0, account);
	
	gossip_protocol_logout (protocol);
}

static void
session_disconnect_foreach_cb (GossipAccount  *account,
			       GossipProtocol *protocol,
			       GossipSession  *session)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (session);
	
	session_disconnect (session, account);
}

void 
gossip_session_send_message (GossipSession *session, 
			     GossipMessage *message)
{
	GossipSessionPriv *priv;
	GossipContact     *contact;
	GossipAccount     *account;
	GList             *l;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
	g_return_if_fail (message != NULL);

	priv = GET_PRIV (session);
	
	contact = gossip_message_get_recipient (message);
	account = gossip_session_find_account (session, contact);

	if (account) {
		GossipProtocol *protocol;

		protocol = g_hash_table_lookup (priv->accounts, account);
		
		gossip_protocol_send_message (protocol, message);

		g_object_unref (account);

		return;
	}

	/* NOTE: is this right? look for the account based on the
	   recipient, if not known then send to ALL?? */
	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol = GOSSIP_PROTOCOL (l->data);

		if (!gossip_protocol_is_connected (protocol)) {
			continue;
		}

		gossip_protocol_send_message (protocol, message);
	}
}

void
gossip_session_send_composing (GossipSession  *session,
			       GossipContact  *contact,
			       gboolean        composing)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	protocol = session_get_protocol (session, contact);
	if (!protocol) {
		/* We don't warn here because if no protocol is found,
		 * it is likely that it is because the contact is a
		 * temporary contact, instead we just silently ignore
		 * it and don't send composing. 
		 */
		return;
	}

	if (!gossip_protocol_is_connected (protocol)) {
		return;
	}

	gossip_protocol_send_composing (protocol,
					contact,
					composing);
}

GossipPresence *
gossip_session_get_presence (GossipSession *session)
{
	GossipSessionPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);

	priv = GET_PRIV (session);
	
	return priv->presence;
}

void 
gossip_session_set_presence (GossipSession  *session,
			     GossipPresence *presence)
{
	GossipSessionPriv *priv;
	GList            *l;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);
	priv->presence = presence;

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;

		protocol = GOSSIP_PROTOCOL (l->data);

		if (!gossip_protocol_is_connected (protocol)) {
			continue;
		}

		gossip_protocol_set_presence (protocol, presence);
	}

	g_signal_emit (session, signals[PRESENCE_CHANGED], 0, presence);
}

gboolean
gossip_session_is_connected (GossipSession *session,
			     GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	
	priv = GET_PRIV (session);

	if (account) {
		protocol = g_hash_table_lookup (priv->accounts, account);
		
		return gossip_protocol_is_connected (protocol);
	} 

	/* Fall back to counter if no account is provided */
	return (priv->connected_counter > 0);
}

const gchar *
gossip_session_get_active_resource (GossipSession *session,
				    GossipContact *contact)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	
	/* Get the activate resource, needed to be able to lock the
	 * chat against a certain resource.
	 */

	priv = GET_PRIV (session);

	protocol = session_get_protocol (session, contact);
	if (!protocol) {
		return NULL;
	}
	
	return gossip_protocol_get_active_resource (protocol, contact);
}

GossipChatroomProvider *
gossip_session_get_chatroom_provider (GossipSession *session,
				      GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), NULL);

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);

	g_return_val_if_fail (GOSSIP_IS_PROTOCOL (protocol), NULL);

	return GOSSIP_CHATROOM_PROVIDER (protocol);
}

GossipFTProvider *
gossip_session_get_ft_provider (GossipSession *session,
				GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), NULL);

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, 
					gossip_account_get_name (account));

	g_return_val_if_fail (GOSSIP_IS_PROTOCOL (protocol), NULL);

	return GOSSIP_FT_PROVIDER (protocol);
}

GossipContact *
gossip_session_find_contact (GossipSession *session, 
			     const gchar   *id)
{
	GossipSessionPriv *priv;
	GList             *l;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
        g_return_val_if_fail (id != NULL, NULL);

	priv = GET_PRIV (session);

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		GossipContact  *contact;
		
		protocol = l->data;

		contact = gossip_protocol_find_contact (protocol, id);
		if (contact) {
			return contact;
		}
	}

	return NULL;
}

void
gossip_session_add_contact (GossipSession *session,
			    GossipAccount *account,
                            const gchar   *id,
                            const gchar   *name,
                            const gchar   *group,
                            const gchar   *message)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));
        g_return_if_fail (id != NULL);
        g_return_if_fail (name != NULL);
        g_return_if_fail (message != NULL);

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);

        gossip_protocol_add_contact (protocol,
                                     id, name, group, message);
}

void
gossip_session_rename_contact (GossipSession *session,
                               GossipContact *contact,
                               const gchar   *new_name)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
        g_return_if_fail (GOSSIP_IS_CONTACT (contact));
        g_return_if_fail (new_name != NULL);
	
	/* get the activate resource, needed to be able to lock the
	   chat against a certain resource */

	priv = GET_PRIV (session);

	protocol = session_get_protocol (session, contact);
	if (!protocol) {
		return;
	}
	
        gossip_protocol_rename_contact (protocol, contact, new_name);
}

void
gossip_session_remove_contact (GossipSession *session,
                               GossipContact *contact)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
        g_return_if_fail (GOSSIP_IS_CONTACT (contact));
	
	/* Get the activate resource, needed to be able to lock the
	 * chat against a certain resource.
	 */

	priv = GET_PRIV (session);

	protocol = session_get_protocol (session, contact);
	if (!protocol) {
		return;
	}
	
        gossip_protocol_remove_contact (protocol, contact);
}

void
gossip_session_update_contact (GossipSession *session,
                               GossipContact *contact)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
        g_return_if_fail (contact != NULL);
	
	priv = GET_PRIV (session);

	protocol = session_get_protocol (session, contact);
	if (!protocol) {
		return;
	}
	
        gossip_protocol_update_contact (protocol, contact);
}

void
gossip_session_rename_group (GossipSession *session,
			     const gchar   *group,
			     const gchar   *new_name)
{
	GossipSessionPriv *priv;
	GList             *l;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
        g_return_if_fail (group != NULL);
        g_return_if_fail (new_name != NULL);
	
	priv = GET_PRIV (session);

	/* 	protocol = session_get_protocol (session, NULL); */

	/* FIXME: don't just blindly do this across all protocols
	   actually pass the protocol in some how from the contact
	   list? - what if we have the same group name in 2 different
	   accounts */
	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol = GOSSIP_PROTOCOL (l->data);
	
		gossip_protocol_rename_group (protocol, group, new_name);
	}
}

const GList *
gossip_session_get_contacts (GossipSession *session)
{
	GossipSessionPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);

	priv = GET_PRIV (session);

	return priv->contacts;
}

GList *
gossip_session_get_contacts_by_account (GossipSession *session,
					GossipAccount *account)
{
	GossipSessionPriv *priv;
	GList             *l, *list = NULL;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), NULL);

	priv = GET_PRIV (session);

	for (l = priv->contacts; l; l = l->next) {
		GossipContact *contact;
		GossipAccount *this_account;
		
		contact = l->data;

		this_account = gossip_contact_get_account (contact);

		if (!gossip_account_equal (this_account, account)) {
			continue;
		}

		list = g_list_append (list, contact);
	}
	
	return list;
}

GossipContact * 
gossip_session_get_own_contact (GossipSession *session,
				GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), NULL);

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);
	g_return_val_if_fail (GOSSIP_IS_PROTOCOL (protocol), NULL);

	return gossip_protocol_get_own_contact (protocol);
}

GList *
gossip_session_get_groups (GossipSession *session)
{
	GossipSessionPriv *priv;
	GList             *l, *j;
	GList             *all_groups = NULL;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);

	priv = GET_PRIV (session);

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		GList          *groups;
		
		protocol = l->data;

		groups = gossip_protocol_get_groups (protocol);
		for (j = groups; j; j = j->next) {
			if (!g_list_find_custom (all_groups, 
						 j->data, 
						 (GCompareFunc) strcmp)) {
				all_groups = g_list_append (all_groups, j->data);
			}
		}
	}

	return all_groups;
}

const gchar *
gossip_session_get_nickname (GossipSession *session,
			     GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;
	GossipContact     *contact;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), "");
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), "");

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);
	g_return_val_if_fail (GOSSIP_IS_PROTOCOL (protocol), "");

	contact = gossip_protocol_get_own_contact (protocol);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), "");

	return gossip_contact_get_name (contact);
}

void
gossip_session_register_account (GossipSession           *session,
				 GossipAccount           *account,
				 GossipVCard             *vcard,
				 GossipRegisterCallback   callback,
				 gpointer                 user_data)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));
	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));
	g_return_if_fail (callback != NULL);

	priv = GET_PRIV (session);

	/* make sure we have added the account to our list */
	gossip_session_add_account (session, account);
	
	protocol = g_hash_table_lookup (priv->accounts, account);
	gossip_protocol_setup (protocol, account);

        gossip_protocol_register_account (protocol, account, vcard,
					  callback, user_data);
}

void
gossip_session_register_cancel (GossipSession *session,
				GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));
	g_return_if_fail (GOSSIP_IS_ACCOUNT (account));

	priv = GET_PRIV (session);

	protocol = g_hash_table_lookup (priv->accounts, account);

        gossip_protocol_register_cancel (protocol);
}

gboolean
gossip_session_get_vcard (GossipSession        *session,
			  GossipAccount        *account,
			  GossipContact        *contact,
			  GossipVCardCallback   callback,
			  gpointer              user_data,
			  GError              **error)
{
	GossipSessionPriv *priv;
	
	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

	priv = GET_PRIV (session);
	
	if (!account && !contact) {
		g_warning ("No GossipAccount and no GossipContact to use for vcard request");
		return FALSE;
	}

	/* find contact and get vcard */
	if (contact) {
		GossipProtocol *protocol;
		
		protocol = session_get_protocol (session, contact);
		
		/* use account, must be temp contact, use account protocol */
		if (!protocol && GOSSIP_IS_ACCOUNT (account)) {
			protocol = g_hash_table_lookup (priv->accounts, account);	
		}
		
		return gossip_protocol_get_vcard (protocol, contact,
						  callback, user_data,
						  error);
	}

	/* get my vcard */
	if (account) {
		GossipProtocol *protocol;

		g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);

		protocol = g_hash_table_lookup (priv->accounts, account);

		return gossip_protocol_get_vcard (protocol, NULL,
						  callback, user_data,
						  error);
	}

	return FALSE;
}

gboolean
gossip_session_set_vcard (GossipSession         *session,
			  GossipAccount         *account,
			  GossipVCard           *vcard,
			  GossipResultCallback   callback,
			  gpointer               user_data,
			  GError               **error)
{
	GossipSessionPriv *priv;
	GList             *l;
	gboolean           ok = TRUE;
	
	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_VCARD (vcard), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);
	
	priv = GET_PRIV (session);
	
	/* if account is supplied set the vcard for that account only! */
	if (account) {
		GossipProtocol *protocol;

		g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);

		protocol = g_hash_table_lookup (priv->accounts, account);
		
		return gossip_protocol_set_vcard (protocol,
						  vcard,
						  callback,
						  user_data,
						  error);
	}

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		
		protocol = l->data;

		/* FIXME: error is pointless here... since if this is
		   the 5th protocol, it may have already been
		   written. */
		ok &= gossip_protocol_set_vcard (protocol,
						vcard,
						callback,
						user_data,
						error);
	}
	
	return ok;
}

gboolean
gossip_session_get_version (GossipSession          *session,
			    GossipContact          *contact,
			    GossipVersionCallback   callback,
			    gpointer                user_data,
			    GError                **error)
{
	GossipProtocol *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);
	
	protocol = session_get_protocol (session, contact);

	if (!protocol) {
		return FALSE;
	}
	
	return gossip_protocol_get_version (protocol, contact, 
					    callback, user_data,
					    error);
}

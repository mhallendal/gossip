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

#include "libgossip-marshal.h"
#include "gossip-account.h"
#include "gossip-protocol.h"

/* Temporary */
#include <gossip-jabber.h>

#include "gossip-session.h"

#define d(x)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_SESSION, GossipSessionPriv))


typedef struct _GossipSessionPriv  GossipSessionPriv;


struct _GossipSessionPriv {
 	GossipAccountManager *account_manager; 

	GHashTable           *accounts;
	GList                *protocols;

	GossipPresence       *presence;

	GList                *contacts;

	guint                 connected_counter;

	GHashTable           *timers; /* connected time */
};


typedef struct {
	gchar         *contact_id;
	GossipAccount *account;
} FindAccount;


struct CountAccounts {
	GossipSession *session;
	guint          connected;
	guint          connected_total;
	guint          disconnected;
};


typedef struct {
	GossipSession *session;
	GList         *accounts;
} GetAccounts;


typedef struct {
	GossipSession *session;
	gboolean       startup;
} ConnectAccounts;


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
static void            session_protocol_composing_event          (GossipProtocol       *protocol,
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
								  GetAccounts          *data);
static void            session_count_accounts_foreach_cb         (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  struct CountAccounts *ca);
static void            session_find_account_foreach_cb           (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  FindAccount          *fa);
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
								  ConnectAccounts      *data);
static void            session_disconnect                        (GossipSession        *session,
								  GossipAccount        *account);
static void            session_disconnect_foreach_cb             (GossipAccount        *account,
								  GossipProtocol       *protocol,
								  GossipSession        *session);
	

/* signals */
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
	COMPOSING_EVENT,

	/* Used for protocols to retreive information from UI */
	GET_PASSWORD,
	LAST_SIGNAL
};


G_DEFINE_TYPE (GossipSession, gossip_session, G_TYPE_OBJECT);


static guint    signals[LAST_SIGNAL] = {0};
static gpointer parent_class;


static void
gossip_session_class_init (GossipSessionClass *klass)
{
	GObjectClass *object_class;
	
	object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

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
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE, 
			      1, G_TYPE_POINTER);

	signals[PROTOCOL_CONNECTED] = 
		g_signal_new ("protocol-connected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE, 
			      2, G_TYPE_POINTER, G_TYPE_POINTER);
	
	signals[PROTOCOL_DISCONNECTED] = 
		g_signal_new ("protocol-disconnected",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER_POINTER,
			      G_TYPE_NONE, 
			      2, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[PROTOCOL_DISCONNECTING] = 
		g_signal_new ("protocol-disconnecting",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE, 
			      1, G_TYPE_POINTER);
	
	signals[PROTOCOL_ERROR] = 
		g_signal_new ("protocol-error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER_POINTER_POINTER,
			      G_TYPE_NONE, 
			      3, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[NEW_MESSAGE] = 
		g_signal_new ("new-message",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	signals[PRESENCE_CHANGED] = 
		g_signal_new ("presence-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	signals[CONTACT_ADDED] = 
		g_signal_new ("contact-added",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
	signals[CONTACT_UPDATED] = 
		g_signal_new ("contact-updated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	signals[CONTACT_PRESENCE_UPDATED] = 
		g_signal_new ("contact-presence-updated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	signals[CONTACT_REMOVED] = 
		g_signal_new ("contact-removed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
	signals[COMPOSING_EVENT] =
		g_signal_new ("composing-event",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_VOID__POINTER_BOOLEAN,
			      G_TYPE_NONE,
			      2, G_TYPE_POINTER, G_TYPE_BOOLEAN);
	signals[GET_PASSWORD] =
		g_signal_new ("get-password",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      libgossip_marshal_STRING__POINTER,
			      G_TYPE_STRING,
			      1, G_TYPE_POINTER);
	
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

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
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
	g_signal_connect (protocol, "composing-event",
			  G_CALLBACK (session_protocol_composing_event),
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

	d(g_print ("Session: Protocol logged in\n"));

	priv = GET_PRIV (session);

	/* setup timer */
	timer = g_timer_new ();
	g_timer_start (timer);

	g_hash_table_insert (priv->timers, 
			     g_object_ref (account), 
			     timer);
	
	/* update some status? */
	priv->connected_counter++;

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
	d(g_print ("Session: Protocol logged out (after %.2f seconds)\n", seconds));

	priv = GET_PRIV (session);

	/* remove timer */
	g_hash_table_remove (priv->timers, account);

	/* update some status? */
	if (priv->connected_counter < 0) {
		g_warning ("We have some issues in the connection counting");
		return;
	}

	priv->connected_counter--;
	
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

	d(g_print ("Session: Contact added '%s'\n",
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
	d(g_print ("Session: Contact updated '%s'\n",
		   gossip_contact_get_name (contact)));

	g_signal_emit (session, signals[CONTACT_UPDATED], 0, contact);
}

static void     
session_protocol_contact_presence_updated (GossipProtocol *protocol,
					   GossipContact  *contact,
					   GossipSession  *session)
{
	d(g_print ("Session: Contact presence updated '%s'\n",
		   gossip_contact_get_name (contact)));
	g_signal_emit (session, signals[CONTACT_PRESENCE_UPDATED], 0, contact);
}

static void 
session_protocol_contact_removed (GossipProtocol *protocol,
				  GossipContact  *contact,
				  GossipSession  *session)
{
	GossipSessionPriv *priv;
	
	d(g_print ("Session: Contact removed '%s'\n",
		   gossip_contact_get_name (contact)));

	priv = GET_PRIV (session);
	
	g_signal_emit (session, signals[CONTACT_REMOVED], 0, contact);

	priv->contacts = g_list_remove (priv->contacts, contact);
	g_object_unref (contact);
}

static void 
session_protocol_composing_event (GossipProtocol *protocol,
				  GossipContact  *contact,
				  gboolean        composing,
				  GossipSession  *session)
{
	GossipSessionPriv *priv;
	
	d(g_print ("Session: Contact %s composing:'%s'\n",
		   composing ? "is" : "is not",
		   gossip_contact_get_name (contact)));

	priv = GET_PRIV (session);
	
	g_signal_emit (session, signals[COMPOSING_EVENT], 0, contact, composing);
}

static gchar *
session_protocol_get_password (GossipProtocol *protocol,
			       GossipAccount  *account,
			       GossipSession  *session)
{
	gchar *password;

	d(g_print ("Session: Get password\n"));

	g_signal_emit (session, signals[GET_PASSWORD], 0, account, &password);
	
	return password;
}

static void
session_protocol_error (GossipProtocol *protocol,
			GossipAccount  *account,
			GError         *error,
			GossipSession  *session)
{

	d(g_print ("Session: Error:%d->'%s'\n", 
		   error->code, error->message));

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
	GossipSessionPriv *priv;
	GetAccounts       *data;
	GList             *list;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	
	priv = GET_PRIV (session);

	data = g_new0 (GetAccounts, 1);
	data->session = g_object_ref (session);

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_get_accounts_foreach_cb,
			      data);

	list = data->accounts;

	g_object_unref (data->session);
	g_free (data);

	return list;
}

static void
session_get_accounts_foreach_cb (GossipAccount   *account,
				 GossipProtocol  *protocol,
				 GetAccounts     *data)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (data->session);

	data->accounts = g_list_append (data->accounts, g_object_ref (account));
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
			       guint         *connected_total,
			       guint         *disconnected)
{
	GossipSessionPriv    *priv;
	struct CountAccounts  ca;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));
	
	priv = GET_PRIV (session);

	ca.session = session;

	ca.connected = 0;
	ca.connected_total = 0;
	ca.disconnected = 0;

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_count_accounts_foreach_cb,
			      &ca);

	if (connected) {
		*connected = ca.connected;
	}

	if (connected_total) {
		*connected_total = ca.connected_total;
	}

	if (disconnected) {
		*disconnected = ca.disconnected;
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
		if (gossip_account_get_enabled (account)) {
			ca->connected++;
		}

		ca->connected_total++;
	} else {
		if (gossip_account_get_enabled (account)) {
			ca->disconnected++;
		}
	}
}

gboolean 
gossip_session_add_account (GossipSession *session,
			    GossipAccount *account)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);

	priv = GET_PRIV (session);

	/* check this account is not already set up */
	protocol = g_hash_table_lookup (priv->accounts, account);
	
	if (protocol) {
		/* already added */
		return TRUE;
	}

	/* create protocol for account type */
	switch (gossip_account_get_type (account)) {
	case GOSSIP_ACCOUNT_TYPE_JABBER:
		protocol = g_object_new (GOSSIP_TYPE_JABBER, NULL);
		break;
	default:
		g_assert_not_reached();
	}
	
	/* add to list */
	priv->protocols = g_list_append (priv->protocols, 
					 g_object_ref (protocol));

	/* add to hash table */
	g_hash_table_insert (priv->accounts, 
			     g_object_ref (account), 
			     g_object_ref (protocol));

	/* connect up all signals */ 
	session_protocol_signals_setup (session, protocol);
			
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
	GossipSessionPriv *priv;
	GossipAccount     *account = NULL;
	FindAccount       *fa;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), NULL);
	
	priv = GET_PRIV (session);

	fa = g_new0 (FindAccount, 1);
	
	fa->contact_id = g_strdup (gossip_contact_get_id (contact));

	g_hash_table_foreach (priv->accounts, 
			      (GHFunc)session_find_account_foreach_cb,
			      fa);

	if (fa->account) {
		account = fa->account;
	}

	g_free (fa->contact_id);
	g_free (fa);

	return account;
}

static void
session_find_account_foreach_cb (GossipAccount  *account,
				 GossipProtocol *protocol,
				 FindAccount    *fa)
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
	GossipSessionPriv *priv;
	ConnectAccounts   *data;
	
	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	g_return_if_fail (priv->accounts != NULL);

	g_signal_emit (session, signals[CONNECTING], 0);

	/* temporary */
	if (!priv->presence) {
		priv->presence = gossip_presence_new_full (GOSSIP_PRESENCE_STATE_AVAILABLE, 
							   NULL);
	}

	/* connect one account if provided */
	if (account) {
		g_return_if_fail (GOSSIP_IS_ACCOUNT (account));
		
		session_connect (session, account);
		return;
	}

	/* connect ALL accounts if no one account is specified */
	data = g_new0 (ConnectAccounts, 1);

	data->session = g_object_ref (session);
	data->startup = startup;

	g_hash_table_foreach (priv->accounts,
			      (GHFunc)session_connect_foreach_cb,
			      data);

	g_object_unref (data->session);
	g_free (data);
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

	/* can we not just pass the GossipAccount on the GObject init? */
	gossip_protocol_setup (protocol, account);
	
	/* setup the network connection */
	gossip_protocol_login (protocol);
}

static void
session_connect_foreach_cb (GossipAccount   *account,
			    GossipProtocol  *protocol,
			    ConnectAccounts *data)
{
	GossipSessionPriv *priv;

	priv = GET_PRIV (data->session);

	if (!gossip_account_get_enabled (account)) {
		return;
	}
	
	if (data->startup && 
	    !gossip_account_get_auto_connect (account)) {
		return;
	}

	session_connect (data->session, account);
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
	
/* 	if (!gossip_protocol_is_connected (protocol)) { */
/* 		return; */
/* 	} */

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

	/* don't disconnect accounts which are disabled since
	   they might have been started manually for other
	   reasons and we only want to disconnect all accounts
	   we would normall connect */
	if (!gossip_account_get_enabled (account)) {
		return;
	}
	
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
	g_return_if_fail (GOSSIP_IS_PROTOCOL (protocol));

	if (!gossip_protocol_is_connected (protocol)) {
		return;
	}

	gossip_protocol_send_composing (protocol,
					contact,
					composing);
}

#if 0
void
gossip_session_ft_start (GossipSession  *session,
			 GossipContact  *contact,
			 const gchar    *file)
{
	GossipSessionPriv *priv;
	GossipAccount     *account;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	account = gossip_session_find_account (session, contact);
	protocol = g_hash_table_lookup (priv->accounts, 
					gossip_account_get_name (account));
	
	gossip_protocol_ft_start (protocol, contact, file);
}

void            gossip_session_ft_start                (GossipSession           *session,
							GossipContact           *contact,
							const gchar             *file);
void            gossip_session_ft_accept               (GossipSession           *session,
							GossipContact           *contact,
							const gchar             *file);
void            gossip_session_ft_decline              (GossipSession           *session,
							GossipContact           *contact,
							const gchar             *file);

void
gossip_session_ft_accept (GossipSession  *session,
			  GossipContact  *contact,
			  const gchar    *file)
{
	GossipSessionPriv *priv;
	GossipAccount     *account;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	account = gossip_session_find_account (session, contact);
	protocol = g_hash_table_lookup (priv->accounts, 
					gossip_account_get_name (account));
	
	gossip_protocol_ft_accept (protocol, contact, file);
}

void
gossip_session_ft_decline (GossipSession  *session,
			   GossipContact  *contact,
			   const gchar    *file)
{
	GossipSessionPriv *priv;
	GossipAccount     *account;
	GossipProtocol    *protocol;

	g_return_if_fail (GOSSIP_IS_SESSION (session));

	priv = GET_PRIV (session);

	account = gossip_session_find_account (session, contact);
	protocol = g_hash_table_lookup (priv->accounts, 
					gossip_account_get_name (account));
	
	gossip_protocol_ft_accept (protocol, contact, file);
}
#endif

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

	/* fall back to counter if no account is provided */
	return (priv->connected_counter > 0);
}

const gchar *
gossip_session_get_active_resource (GossipSession *session,
				    GossipContact *contact)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);
	
	/* get the activate resource, needed to be able to lock the
	   chat against a certain resource */

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
	
	/* get the activate resource, needed to be able to lock the
	   chat against a certain resource */

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

GList *
gossip_session_get_groups (GossipSession *session)
{
	GossipSessionPriv *priv;
	GList             *l, *all_groups = NULL;
	
	g_return_val_if_fail (GOSSIP_IS_SESSION (session), NULL);

	priv = GET_PRIV (session);

	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		GList          *groups;
		
		protocol = l->data;

		groups = gossip_protocol_get_groups (protocol);
		if (groups) {
			all_groups = g_list_concat (all_groups, groups);
		}
	}

	return all_groups;
}

const gchar *
gossip_session_get_nickname (GossipSession *session)
{
	GossipSessionPriv *priv;
	const GList       *l;

	g_return_val_if_fail (GOSSIP_IS_SESSION (session), "");

	priv = GET_PRIV (session);

	/* FIXME: this needs thinking about (mr) */
	for (l = priv->protocols; l; l = l->next) {
		GossipProtocol *protocol;
		GossipContact  *contact;

		protocol = l->data;

		if (!gossip_protocol_is_connected (protocol)) {
			continue;
		}

#if 1
		/* FIXME: for now, use the first jabber account */
		if (!GOSSIP_IS_JABBER (protocol)) {
			continue;
		}
#endif

		contact = gossip_jabber_get_own_contact (GOSSIP_JABBER (protocol));
		return gossip_contact_get_name (contact);
	}

	return "";
}

gboolean
gossip_session_register_account (GossipSession           *session,
				 GossipAccountType        type,
				 GossipAccount           *account,
				 GossipRegisterCallback   callback,
				 gpointer                 user_data,
				 GError                 **error)
{
	GossipSessionPriv *priv;
	GossipProtocol    *protocol;
	
	g_return_val_if_fail (GOSSIP_IS_SESSION (session), FALSE);
	g_return_val_if_fail (GOSSIP_IS_ACCOUNT (account), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

	priv = GET_PRIV (session);

	/* make sure we have added the account to our list */
	gossip_session_add_account (session, account);
	
	protocol = g_hash_table_lookup (priv->accounts, account);
	
        return gossip_protocol_register_account (protocol, account, 
						 callback, user_data,
						 error);
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

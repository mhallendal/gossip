/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 Imendio AB
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
#include <glib/gi18n.h>
#include <libnotify/notify.h>

#include "gossip-app.h"
#include "gossip-contact-info-dialog.h"
#include "gossip-notify.h"
#include "gossip-preferences.h"
#include "gossip-stock.h"

#define DEBUG_MSG(x)
/* #define DEBUG_MSG(args) g_printerr args ; g_printerr ("\n"); */

#define NOTIFY_MESSAGE_TIME 20000

/* Max length of the body part of a message we show in the
 * notification. 
 */
#define NOTIFY_MESSAGE_MAX_LEN 64 

/* Time to wait before we use notification for an account after it has
 * gone online/offline, so we don't spam the sound with online's, etc
 */ 
#define NOTIFY_WAIT_TIME 10000

static const gchar *       notify_get_status_from_presence    (GossipPresence     *presence);
static gboolean            notify_get_is_busy                 (void);
static void                notify_contact_online              (GossipContact      *contact);
static void                notify_online_action_cb            (NotifyNotification *notify,
							       gchar              *label,
							       GossipContact      *contact);
static void                notify_new_message_contact_cb      (NotifyNotification *notify,
							       gchar              *label,
							       GossipEventManager *event_manager);
static NotifyNotification *notify_new_message                 (GossipEventManager *event_manager,
							       GossipMessage      *message);
static gboolean            notify_protocol_timeout_cb         (GossipAccount      *account);
static void                notify_protocol_connected_cb       (GossipSession      *session,
							       GossipAccount      *account,
							       GossipProtocol     *protocol,
							       gpointer            user_data);
static void                notify_contact_presence_updated_cb (GossipSession      *session,
							       GossipContact      *contact,
							       gpointer            user_data);
static void                notify_event_added_cb              (GossipEventManager *event_manager,
							       GossipEvent        *event,
							       gpointer            user_data);
static gboolean            notify_event_remove_foreach        (gpointer            key,
							       GossipEvent        *event,
							       GossipEvent        *event_to_compare);
static void                notify_event_removed_cb            (GossipEventManager *event_manager,
							       GossipEvent        *event,
							       gpointer            user_data);
static void                notify_event_destroy_cb            (NotifyNotification *notify);

enum {
	NOTIFY_SHOW_MESSAGE,
	NOTIFY_SHOW_ROSTER,
};

static GHashTable *account_states = NULL;
static GHashTable *contact_states = NULL;
static GHashTable *message_notifications = NULL;
static GHashTable *event_notifications = NULL;
static GtkWidget  *attach_widget = NULL;

static const gchar *
notify_get_status_from_presence (GossipPresence *presence)
{
	const gchar *status;

	status = gossip_presence_get_status (presence);
	if (!status) {
		GossipPresenceState state;

		state = gossip_presence_get_state (presence);
		status = gossip_presence_state_get_default_status (state);
	}
	
	return status;
}

static void
notify_online_action_cb (NotifyNotification *notify,
			 gchar              *label,
			 GossipContact      *contact)
{
	if (label && strcmp (label, "chat") == 0) {
		GossipSession     *session;
		GossipChatManager *chat_manager;

		session = gossip_app_get_session ();
		chat_manager = gossip_app_get_chat_manager ();
		gossip_chat_manager_show_chat (chat_manager, contact);
	}

	g_object_unref (contact);
}

static gboolean
notify_get_is_busy (void)
{
	GossipPresence      *presence;
	GossipPresenceState  state;
	
	presence = gossip_session_get_presence (gossip_app_get_session ());
        state = gossip_presence_get_state (presence);

        if (state == GOSSIP_PRESENCE_STATE_BUSY) {
		return TRUE;
	}

	return FALSE;
}

static void
notify_contact_online (GossipContact *contact)
{
	GossipPresence     *presence;
	NotifyNotification *notify;
	GdkPixbuf          *pixbuf;
	gchar              *title;
	const gchar        *status;
	GError             *error = NULL;
	gboolean            show_popup;

	show_popup = gconf_client_get_bool (gossip_app_get_gconf_client (),
					    GCONF_POPUPS_WHEN_AVAILABLE,
					    NULL);

	if (!show_popup) {
		return;
	}

	if (notify_get_is_busy ()) {
		return;
	}
	
	DEBUG_MSG (("Notify: Setting up notification for online contact:'%s'", 
		   gossip_contact_get_id (contact)));

	presence = gossip_contact_get_active_presence (contact);
	pixbuf = gossip_pixbuf_for_presence (presence);

	title = g_markup_printf_escaped (_("%s has come online"), 
					 gossip_contact_get_name (contact));
	status = notify_get_status_from_presence (presence);

	notify = notify_notification_new (title, status, NULL, NULL);
	notify_notification_set_urgency (notify, NOTIFY_URGENCY_LOW);
	notify_notification_set_icon_from_pixbuf (notify, pixbuf);

	if (attach_widget) {
		notify_notification_attach_to_widget (notify, attach_widget);
	}

	notify_notification_add_action (notify, "default", _("Default"),
					(NotifyActionCallback) notify_online_action_cb,
					g_object_ref (contact), NULL);
	notify_notification_add_action (notify, "chat", _("Chat!"),
					(NotifyActionCallback) notify_online_action_cb,
					g_object_ref (contact), NULL);

	if (!notify_notification_show (notify, &error)) {
		g_warning ("Failed to send notification: %s",
			   error->message);
		g_error_free (error);
	}
	
	g_free (title);

	if (pixbuf) {
		g_object_unref (pixbuf);
	}
}

static void
notify_new_message_default_cb (NotifyNotification *notify,
			       gchar              *label,
			       GossipEventManager *event_manager)
{
	GossipEvent   *event;
	GossipMessage *message;
	GossipContact *contact = NULL;

	event = g_hash_table_lookup (event_notifications, notify);
	if (event) {
		message = GOSSIP_MESSAGE (gossip_event_get_data (event));
		contact = gossip_message_get_sender (message);
	
		gossip_event_manager_activate (event_manager, event);
	} else {
		g_warning ("No event found for NotifyNotification: %p", notify);
	}

	g_hash_table_remove (event_notifications, notify);
	g_hash_table_remove (message_notifications, contact);

	g_object_unref (event_manager);
}

static void
notify_new_message_contact_cb (NotifyNotification *notify,
			       gchar              *label,
			       GossipEventManager *event_manager)
{
	GossipEvent   *event;
	GossipMessage *message;
	GossipContact *contact = NULL;

	event = g_hash_table_lookup (event_notifications, notify);
	if (event) {
		message = GOSSIP_MESSAGE (gossip_event_get_data (event));
		contact = gossip_message_get_sender (message);

		gossip_contact_info_dialog_show (contact);
	} else {
		g_warning ("No event found for Notification: %p", notify);
	}

	g_hash_table_remove (event_notifications, notify);
	g_hash_table_remove (message_notifications, contact);

	g_object_unref (event_manager);
}

static NotifyNotification *
notify_new_message (GossipEventManager *event_manager,
		    GossipMessage      *message)
{ 
	GossipContact      *contact;
	NotifyNotification *notify;
	GdkPixbuf          *pixbuf;
	gchar              *title;
	gchar              *body_copy;
	const gchar        *body_stripped;
	gchar              *str;
	gint                len;
	GError             *error = NULL;

	contact = gossip_message_get_sender (message);

	body_copy = g_strdup (gossip_message_get_body (message));
	body_stripped = g_strstrip (body_copy);
	
	len = g_utf8_strlen (body_stripped, -1);
	if (len < 1) {
		DEBUG_MSG (("Notify: Ignoring new message from:'%s', no message content", 
			    gossip_contact_get_id (contact)));
		g_free (body_copy);
		return NULL;
	}

	DEBUG_MSG (("Notify: Setting up notification for new message from:'%s'", 
		   gossip_contact_get_id (contact)));

	pixbuf = gossip_pixbuf_from_stock (GOSSIP_STOCK_MESSAGE, GTK_ICON_SIZE_MENU);

	title = g_strdup_printf (_("New message from %s"), 
				 gossip_contact_get_name (contact));

	if (len >= NOTIFY_MESSAGE_MAX_LEN) {
		gchar *end;

		end = g_utf8_offset_to_pointer (body_stripped, NOTIFY_MESSAGE_MAX_LEN);
		end[0] = '\0';
		
		str = g_markup_printf_escaped ("%s...", body_stripped);
	} else {
		str = g_markup_printf_escaped ("%s", body_stripped);
	}

	notify = notify_notification_new (title, str, NULL, NULL);
	notify_notification_set_urgency (notify, NOTIFY_URGENCY_NORMAL);
	notify_notification_set_icon_from_pixbuf (notify, pixbuf);
	notify_notification_set_timeout (notify, NOTIFY_MESSAGE_TIME);

	if (attach_widget) {
		notify_notification_attach_to_widget (notify, attach_widget);
	}
	
	notify_notification_add_action (notify, "default", _("Default"),
					(NotifyActionCallback) notify_new_message_default_cb,
					g_object_ref (event_manager), NULL);
	notify_notification_add_action (notify, "respond", _("Respond"),
					(NotifyActionCallback) notify_new_message_default_cb,
					g_object_ref (event_manager), NULL);
	notify_notification_add_action (notify, "contact", _("Contact Information"),
					(NotifyActionCallback) notify_new_message_contact_cb,
					g_object_ref (event_manager), NULL);
	
	if (!notify_notification_show (notify, &error)) {
		g_warning ("Failed to send notification: %s",
			   error->message);
		g_error_free (error);
	}

	g_free (str);
	g_free (title);
	g_free (body_copy);

	if (pixbuf) {
		g_object_unref (pixbuf);
	}

	return notify;
}

static gboolean
notify_protocol_timeout_cb (GossipAccount *account)
{
	g_hash_table_remove (account_states, account);
	return FALSE;
}

static void
notify_protocol_connected_cb (GossipSession  *session,
			      GossipAccount  *account,
			      GossipProtocol *protocol,
			      gpointer        user_data)
{
	guint id;

	if (g_hash_table_lookup (account_states, account)) {
		return;
	}

	DEBUG_MSG (("Notify: Account update, account:'%s' is now online",
		    gossip_account_get_id (account)));

	id = g_timeout_add (NOTIFY_WAIT_TIME,
			    (GSourceFunc) notify_protocol_timeout_cb, 
			    account);
	g_hash_table_insert (account_states, account, GUINT_TO_POINTER (id));
}

static void
notify_contact_presence_updated_cb (GossipSession *session,
				    GossipContact *contact,
				    gpointer       user_data)
{
	GossipPresence *presence;

	if (gossip_contact_get_type (contact) != GOSSIP_CONTACT_TYPE_CONTACTLIST) {
		return;
	}

	presence = gossip_contact_get_active_presence (contact);
	if (!presence) {
		if (g_hash_table_lookup (contact_states, contact)) {
			DEBUG_MSG (("Notify: Presence update, contact:'%s' is now offline",
				    gossip_contact_get_id (contact)));
		}
			
		g_hash_table_remove (contact_states, contact);
	} else {
		GossipAccount *account;
		
		account = gossip_contact_get_account (contact);

		/* Only show notifications after being online for some
		 * time instead of spamming notifications each time we
		 * connect.
		 */
		if (!g_hash_table_lookup (account_states, account) && 
		    !g_hash_table_lookup (contact_states, contact)) {
			DEBUG_MSG (("Notify: Presence update, contact:'%s' is now online",
				    gossip_contact_get_id (contact)));
			notify_contact_online (contact);
		}
		
		g_hash_table_insert (contact_states, 
				     g_object_ref (contact), 
				     g_object_ref (presence));
	}
}

static void
notify_event_added_cb (GossipEventManager *event_manager,
		       GossipEvent        *event,
		       gpointer            user_data)
{
	GossipEventType type;
	
	type = gossip_event_get_type (event);

	if (type == GOSSIP_EVENT_NEW_MESSAGE) {
		NotifyNotification  *notify;
		GossipMessage       *message;
		GossipContact       *contact;
		
		message = GOSSIP_MESSAGE (gossip_event_get_data (event));
		contact = gossip_message_get_sender (message);

		/* Find out if there are any other messages waiting,
		 * if not, show a notification.
		 */
		if (! g_hash_table_lookup (message_notifications, contact)) {
			notify = notify_new_message (event_manager, message);
			
			if (notify) {
				g_hash_table_insert (message_notifications,
						     contact,
						     g_object_ref (event));
				g_hash_table_insert (event_notifications,   
						     notify,
						     g_object_ref (event));  
			}
		}
	}
}

static gboolean
notify_event_remove_foreach (gpointer key,
			     GossipEvent *event,
			     GossipEvent *event_to_compare)
{
	if (gossip_event_equal (event, event_to_compare)) {
		return TRUE;
	}

	return FALSE;
}

static void
notify_event_removed_cb (GossipEventManager *event_manager,
			 GossipEvent        *event,
			 gpointer            user_data)
{
	g_hash_table_foreach_remove (message_notifications, 
				     (GHRFunc) notify_event_remove_foreach,
				     event);
	g_hash_table_foreach_remove (event_notifications, 
				     (GHRFunc) notify_event_remove_foreach,
				     event);
}

static void
notify_event_destroy_cb (NotifyNotification *notify)
{
	notify_notification_close (notify, NULL);
}

void
gossip_notify_set_attach_widget (GtkWidget *new_attach_widget)
{
	if (attach_widget) {
		g_object_remove_weak_pointer (G_OBJECT (attach_widget),
					      (gpointer) &attach_widget);
	}
	
	attach_widget = new_attach_widget;
	if (attach_widget) {
		g_object_add_weak_pointer (G_OBJECT (new_attach_widget),
					   (gpointer) &attach_widget);
	}
}

void
gossip_notify_init (GossipSession      *session,
		    GossipEventManager *event_manager)
{
	static gboolean inited = FALSE;

	g_return_if_fail (GOSSIP_IS_SESSION (session));
	g_return_if_fail (GOSSIP_IS_EVENT_MANAGER (event_manager));
	
	DEBUG_MSG (("Notify: Initiating..."));

	if (inited) {
		return;
	}
	
	if (!notify_init (PACKAGE_NAME)) {
		g_warning ("Cannot initialize Notify integration");
		return;
	}

	message_notifications = g_hash_table_new_full (gossip_contact_hash,
						       gossip_contact_equal,
						       (GDestroyNotify) g_object_unref,
						       (GDestroyNotify) g_object_unref);

	event_notifications = g_hash_table_new_full (g_direct_hash,
						     g_direct_equal,
						     (GDestroyNotify) notify_event_destroy_cb,
						     (GDestroyNotify) g_object_unref);

	account_states = g_hash_table_new_full (gossip_account_hash,
						gossip_account_equal,
						(GDestroyNotify) g_object_unref,
						(GDestroyNotify) g_source_remove);

	contact_states = g_hash_table_new_full (gossip_contact_hash,
						gossip_contact_equal,
						(GDestroyNotify) g_object_unref,
						(GDestroyNotify) g_object_unref);

	g_signal_connect (session, "protocol-connected",
			  G_CALLBACK (notify_protocol_connected_cb),
			  NULL);
	g_signal_connect (session, "contact-presence-updated",
			  G_CALLBACK (notify_contact_presence_updated_cb),
			  NULL);

	g_signal_connect (event_manager, "event-added",
			  G_CALLBACK (notify_event_added_cb),
			  NULL);
	g_signal_connect (event_manager, "event-removed",
			  G_CALLBACK (notify_event_removed_cb),
			  NULL);

 	inited = TRUE;
}

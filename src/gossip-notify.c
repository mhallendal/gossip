/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libnotify/notify.h>

#include "gossip-chat-manager.h"
#include "gossip-app.h"
#include "gossip-contact-info-dialog.h"
#include "gossip-notify.h"
#include "gossip-preferences.h"
#include "gossip-ui-utils.h"

#define DEBUG_DOMAIN "Notify"

#define NOTIFY_MESSAGE_TIME      20000
#define NOTIFY_SUBSCRIPTION_TIME 20000

/* Max length of the body part of a message we show in the
 * notification.
 */
#define NOTIFY_MESSAGE_MAX_LEN   64

/* Time to wait before we use notification for an account after it has
 * gone online/offline, so we don't spam the sound with online's, etc
 */
#define NOTIFY_WAIT_TIME         10000

static const gchar *       notify_get_default_status_from_presence (GossipPresence     *presence);
static gboolean            notify_get_is_busy                      (void);
static void                notify_contact_online                   (GossipContact      *contact);
static void                notify_online_action_cb                 (NotifyNotification *notify,
                                                                    gchar              *label,
                                                                    GossipContact      *contact);
static void                notify_subscription_request_default_cb  (NotifyNotification *notify,
                                                                    gchar              *label,
                                                                    gpointer            user_data);
static void                notify_subscription_request_show        (GossipContact      *contact);
static void                notify_new_message_contact_cb           (NotifyNotification *notify,
                                                                    gchar              *label,
                                                                    GossipEventManager *event_manager);
static NotifyNotification *notify_new_message                      (GossipEventManager *event_manager,
                                                                    GossipMessage      *message);
static gboolean            notify_protocol_timeout_cb              (GossipAccount      *account);
static void                notify_protocol_connected_cb            (GossipSession      *session,
                                                                    GossipAccount      *account,
                                                                    GossipJabber       *jabber,
                                                                    gpointer            user_data);
static void                notify_protocol_disconnected_cb         (GossipSession      *session,
                                                                    GossipAccount      *account,
                                                                    GossipJabber       *jabber,
                                                                    gint                reason,
                                                                    gpointer            user_data);
static void                notify_contact_presence_updated_cb      (GossipContact      *contact,
                                                                    GParamSpec         *param,
                                                                    gpointer            user_data);
static void                notify_contact_added_cb                 (GossipSession      *session,
                                                                    GossipContact      *contact,
                                                                    gpointer            user_data);
static void                notify_contact_removed_cb               (GossipSession      *session,
                                                                    GossipContact      *contact,
                                                                    gpointer            user_data);
static gboolean            notify_subscription_request_show_cb     (GossipContact      *contact);
static void                notify_event_added_cb                   (GossipEventManager *event_manager,
                                                                    GossipEvent        *event,
                                                                    gpointer            user_data);
static gboolean            notify_event_remove_foreach             (gpointer            key,
                                                                    GossipEvent        *event,
                                                                    GossipEvent        *event_to_compare);
static void                notify_event_removed_cb                 (GossipEventManager *event_manager,
                                                                    GossipEvent        *event,
                                                                    gpointer            user_data);

enum {
    NOTIFY_SHOW_MESSAGE,
    NOTIFY_SHOW_ROSTER
};

static gboolean       inited = FALSE;
static GHashTable    *account_states = NULL;
static GHashTable    *contact_states = NULL;
static GHashTable    *message_notifications = NULL;
static GHashTable    *event_notifications = NULL;
static GtkWidget     *attach_widget = NULL;
static GtkStatusIcon *attach_status_icon = NULL;

static const gchar *
notify_get_default_status_from_presence (GossipPresence *presence)
{
    GossipPresenceState  state;
    const gchar         *status;

    state = gossip_presence_get_state (presence);
    status = gossip_presence_state_get_default_status (state);
        
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

static void
notify_closed_cb (NotifyNotification *notify,
                  gpointer            user_data)
{
    /* NOTE: We only do this because libnotify<=0.4.3 breaks here
     * otherwise, this is all detailed in bug #395588.
     */
#ifndef LIBNOTIFY_BUGGY_CLOSE
    g_object_unref(notify);
#endif
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
    GdkPixbuf          *pixbuf = NULL;
    gchar              *body;
    GError             *error = NULL;
    gboolean            show_popup;
    gboolean            show_avatars;

    if (!gossip_conf_get_bool (gossip_conf_get (),
                               GOSSIP_PREFS_POPUPS_WHEN_AVAILABLE,
                               &show_popup)) {
        return;
    }

    if (!show_popup) {
        return;
    }

    if (notify_get_is_busy ()) {
        return;
    }

    gossip_debug (DEBUG_DOMAIN, 
                  "Setting up notification for online contact:'%s'",
                  gossip_contact_get_id (contact));

    presence = gossip_contact_get_active_presence (contact);

    show_avatars = FALSE;
    gossip_conf_get_bool (gossip_conf_get (),
                          GOSSIP_PREFS_UI_SHOW_AVATARS,
                          &show_avatars);

    if (show_avatars) {
        pixbuf = gossip_contact_get_avatar_pixbuf (contact);
    }

    if (pixbuf) {
        g_object_ref (pixbuf);
    } else {
        /* Fall back to presence state */
        pixbuf = gossip_pixbuf_for_presence (presence);
    }
        
    body = g_markup_printf_escaped (_("%s is %s"),
                                    gossip_contact_get_name (contact),
                                    notify_get_default_status_from_presence (presence));

    notify = notify_notification_new (_("Contact Online"), body, NULL, NULL);
    notify_notification_set_urgency (notify, NOTIFY_URGENCY_LOW);
    notify_notification_set_icon_from_pixbuf (notify, pixbuf);

    g_signal_connect (notify,
                      "closed",
                      G_CALLBACK (notify_closed_cb),
                      NULL);

    if (attach_status_icon) {
        notify_notification_attach_to_status_icon (notify, attach_status_icon);
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

    g_free (body);

    if (pixbuf) {
        g_object_unref (pixbuf);
    }
}

static void
notify_subscription_request_default_cb (NotifyNotification *notify,
                                        gchar              *label,
                                        gpointer            user_data)
{
    /* FIXME: Show subscription dialog */
}

static void
notify_subscription_request_show (GossipContact *contact)
{
    GossipSession        *session;
    GossipAccountManager *account_manager;
    const gchar          *contact_name;
    guint                 n;
    NotifyNotification   *notify;
    GdkPixbuf            *pixbuf = NULL;
    GError               *error = NULL;
    gchar                *message;
    gboolean              show_avatars = FALSE;

    session = gossip_app_get_session ();
    account_manager = gossip_session_get_account_manager (session);
    n = gossip_account_manager_get_count (account_manager);
    contact_name = gossip_contact_get_name (contact);

    if (n > 1) {
        GossipAccount *account;
        const gchar   *account_name;

        account = gossip_contact_get_account (contact);
        account_name = gossip_account_get_name (account);

        if (contact_name) {
            message = g_markup_printf_escaped (
                _("%s wants to be added to your contact list for your “%s” account"), 
                contact_name, account_name);
        } else {
            message = g_markup_printf_escaped (
                _("Someone wants to be added to your contact list for your “%s” account"),
                account_name);
        }
    } else { 
        if (contact_name) {
            message = g_markup_printf_escaped (
                _("%s wants to be added to your contact list"), 
                contact_name);
        } else {
            message = g_markup_printf_escaped (
                _("Someone wants to be added to your contact list"));
        }
    }

    gossip_conf_get_bool (gossip_conf_get (),
                          GOSSIP_PREFS_UI_SHOW_AVATARS,
                          &show_avatars);

    if (show_avatars) {
        pixbuf = gossip_contact_get_avatar_pixbuf (contact);
    }

    if (pixbuf) {
        g_object_ref (pixbuf);
    } else {
        /* Fall back to presence state */
        pixbuf = gossip_stock_create_pixbuf (gossip_app_get_window (),
                                             GTK_STOCK_DIALOG_QUESTION,
                                             GTK_ICON_SIZE_MENU);
    }

    notify = notify_notification_new (_("Subscription Request"),
                                      message,
                                      NULL,
                                      NULL);
    g_free (message);

    if (attach_status_icon) {
        notify_notification_attach_to_status_icon (notify, attach_status_icon);
    }

    notify_notification_set_urgency (notify, NOTIFY_URGENCY_NORMAL);
    notify_notification_set_timeout (notify, NOTIFY_SUBSCRIPTION_TIME);

    if (pixbuf) {
        notify_notification_set_icon_from_pixbuf (notify, pixbuf);
        g_object_unref (pixbuf);
    }

    notify_notification_add_action (notify, "default", _("Default"),
                                    (NotifyActionCallback) notify_subscription_request_default_cb,
                                    NULL, NULL);

    g_signal_connect (notify,
                      "closed",
                      G_CALLBACK (notify_closed_cb),
                      NULL);

    if (!notify_notification_show (notify, &error)) {
        g_warning ("Failed to send notification: %s",
                   error->message);
        g_error_free (error);
    }

    return;
}

static void
notify_file_transfer_request (GossipFT *ft)
{
    NotifyNotification *notification;
    gchar *notification_body;
    GError *error;
        
    /* These messages copy (more-or-less) the ones found in the file transfer dialogue. */
    notification_body = g_markup_printf_escaped (_("%s would like to send you “%s”"),
                                                 gossip_contact_get_name (gossip_ft_get_contact (ft)),
                                                 gossip_ft_get_file_name (ft));
    notification = notify_notification_new (_("File Transfer"),
                                            notification_body,
                                            NULL,
                                            NULL);

    g_free (notification_body);
        
    if (attach_status_icon) {
        notify_notification_attach_to_status_icon (notification, attach_status_icon);
    }

    notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);

    error = NULL;
    if (!notify_notification_show (notification, &error)) {
        g_warning ("Failed to send notification: %s",
                   error->message);
        g_error_free (error);
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
                
        gossip_chat_manager_remove_events (gossip_app_get_chat_manager(), contact);
                
        g_hash_table_remove (event_notifications, notify);
        g_hash_table_remove (message_notifications, contact);
    } else {
        g_warning ("No event found for NotifyNotification: %p", notify);
    }

    g_object_unref (event_manager);
}

static void
notify_new_message_respond_cb (NotifyNotification *notify,
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

        g_hash_table_remove (event_notifications, notify);
        g_hash_table_remove (message_notifications, contact);
    } else {
        g_warning ("No event found for NotifyNotification: %p", notify);
    }

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
        GtkWindow *parent;

        message = GOSSIP_MESSAGE (gossip_event_get_data (event));
        contact = gossip_message_get_sender (message);
                
        parent = GTK_WINDOW (gossip_app_get_window ());
        if (!gossip_window_get_is_visible (parent)) {
            parent = NULL;
        }

        gossip_contact_info_dialog_show (contact, parent);

        g_hash_table_remove (event_notifications, notify);
        g_hash_table_remove (message_notifications, contact);
    } else {
        g_warning ("No event found for Notification: %p", notify);
    }

    g_object_unref (event_manager);
}

static NotifyNotification *
notify_new_message (GossipEventManager *event_manager,
                    GossipMessage      *message)
{
    GossipContact      *contact;
    NotifyNotification *notify;
    GdkPixbuf          *pixbuf = NULL;
    GError             *error = NULL;
    gchar              *title;
    gchar              *body_copy;
    const gchar        *body_stripped;
    gchar              *str;
    gint                len;
    gboolean            show_avatars;

    contact = gossip_message_get_sender (message);

    if (gossip_message_is_action (message)) {
        body_copy = gossip_message_get_action_string (message);
    } else {
        body_copy = g_strdup (gossip_message_get_body (message));
    }

    body_stripped = g_strstrip (body_copy);

    len = g_utf8_strlen (body_stripped, -1);
    if (len < 1) {
        gossip_debug (DEBUG_DOMAIN, 
                      "Ignoring new message from:'%s', no message content",
                      gossip_contact_get_id (contact));
        g_free (body_copy);
        return NULL;
    }

    gossip_debug (DEBUG_DOMAIN, 
                  "Setting up notification for new message from:'%s'",
                  gossip_contact_get_id (contact));

    show_avatars = FALSE;
    gossip_conf_get_bool (gossip_conf_get (),
                          GOSSIP_PREFS_UI_SHOW_AVATARS,
                          &show_avatars);

    if (show_avatars) {
        pixbuf = gossip_contact_get_avatar_pixbuf (contact);
    }

    if (pixbuf) {
        g_object_ref (pixbuf);
    } else {
        /* Fall back to message icon */
        pixbuf = gossip_stock_create_pixbuf (gossip_app_get_window (),
                                             GOSSIP_STOCK_MESSAGE, 
                                             GTK_ICON_SIZE_MENU);
    }

    title = g_markup_printf_escaped (_("New Message from %s"),
                                     gossip_contact_get_name (contact));

    if (len >= NOTIFY_MESSAGE_MAX_LEN) {
        gchar *end;

        end = g_utf8_offset_to_pointer (body_stripped, NOTIFY_MESSAGE_MAX_LEN);
        end[0] = '\0';

        str = g_markup_printf_escaped (_("“%s...”"), body_stripped);
    } else {
        str = g_markup_printf_escaped (_("“%s”"), body_stripped);
    }

    notify = notify_notification_new (title, str, NULL, NULL);
    notify_notification_set_urgency (notify, NOTIFY_URGENCY_NORMAL);
    notify_notification_set_icon_from_pixbuf (notify, pixbuf);
    notify_notification_set_timeout (notify, NOTIFY_MESSAGE_TIME);

    g_signal_connect (notify,
                      "closed",
                      G_CALLBACK (notify_closed_cb),
                      NULL);

    if (attach_status_icon) {
        notify_notification_attach_to_status_icon (notify, attach_status_icon);
    }

    notify_notification_add_action (notify, "default", _("Default"),
                                    (NotifyActionCallback) notify_new_message_default_cb,
                                    g_object_ref (event_manager), NULL);
    notify_notification_add_action (notify, "respond", _("Show"),
                                    (NotifyActionCallback) notify_new_message_respond_cb,
                                    g_object_ref (event_manager), NULL);

    if (gossip_contact_get_type (contact) == GOSSIP_CONTACT_TYPE_TEMPORARY) {
        notify_notification_add_action (
            notify, "contact", _("Contact Information"),
            (NotifyActionCallback) notify_new_message_contact_cb,
            g_object_ref (event_manager), NULL);
    }

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


static gboolean
notify_disconnected_message_foreach (GossipContact  *contact,
                                     GossipEvent    *event,
                                     GossipAccount  *account)
{
    GossipAccount *contact_account;

    contact_account = gossip_contact_get_account (contact);

    if (gossip_account_equal (contact_account, account)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
notify_disconnected_contact_foreach (GossipContact  *contact,
                                     GossipPresence *presence,
                                     GossipAccount  *account)
{
    GossipAccount *contact_account;

    contact_account = gossip_contact_get_account (contact);

    if (gossip_account_equal (contact_account, account)) {
        return TRUE;
    }

    return FALSE;
}

static void
notify_protocol_disconnected_cb (GossipSession *session,
                                 GossipAccount *account,
                                 GossipJabber  *jabber,
                                 gint           reason,
                                 gpointer       user_data)
{
    g_hash_table_remove (account_states, account);

    g_hash_table_foreach_remove (message_notifications,
                                 (GHRFunc) notify_disconnected_message_foreach,
                                 account);

    g_hash_table_foreach_remove (contact_states,
                                 (GHRFunc) notify_disconnected_contact_foreach,
                                 account);
}

static void
notify_protocol_connected_cb (GossipSession *session,
                              GossipAccount *account,
                              GossipJabber  *jabber,
                              gpointer       user_data)
{
    guint        id;
    const gchar *account_name;

    if (g_hash_table_lookup (account_states, account)) {
        return;
    }

    account_name = gossip_account_get_name (account);
    gossip_debug (DEBUG_DOMAIN, "Account update, account:'%s' is now online",
                  account_name);

    id = g_timeout_add (NOTIFY_WAIT_TIME,
                        (GSourceFunc) notify_protocol_timeout_cb,
                        account);
    g_hash_table_insert (account_states, g_object_ref (account),
                         GUINT_TO_POINTER (id));
}

static void
notify_contact_presence_updated_cb (GossipContact *contact,
                                    GParamSpec    *param,
                                    gpointer       user_data)
{
    GossipPresence *presence;

    if (gossip_contact_get_type (contact) != GOSSIP_CONTACT_TYPE_CONTACTLIST) {
        return;
    }

    presence = gossip_contact_get_active_presence (contact);
    if (!presence) {
        if (g_hash_table_lookup (contact_states, contact)) {
            gossip_debug (DEBUG_DOMAIN, 
                          "Presence update, contact:'%s' is now offline",
                          gossip_contact_get_id (contact));
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
            gossip_debug (DEBUG_DOMAIN, "Presence update, contact:'%s' is now online",
                          gossip_contact_get_id (contact));
            notify_contact_online (contact);
        }

        g_hash_table_insert (contact_states,
                             g_object_ref (contact),
                             g_object_ref (presence));
    }
}

static void
notify_contact_added_cb (GossipSession *session,
                         GossipContact *contact,
                         gpointer       user_data)
{
    g_signal_connect (contact, "notify::presences",
                      G_CALLBACK (notify_contact_presence_updated_cb),
                      NULL);
    g_signal_connect (contact, "notify::type",
                      G_CALLBACK (notify_contact_presence_updated_cb),
                      NULL);
}

static void
notify_contact_removed_cb (GossipSession *session,
                           GossipContact *contact,
                           gpointer       user_data)
{
    g_signal_handlers_disconnect_by_func (contact,
                                          notify_contact_presence_updated_cb,
                                          NULL);

    g_hash_table_remove (contact_states, contact);
}

static gboolean
notify_subscription_request_show_cb (GossipContact *contact)
{
    notify_subscription_request_show (contact);

    return FALSE;
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
        if (!g_hash_table_lookup (message_notifications, contact)) {
            notify = notify_new_message (event_manager, message);

            if (notify) {
                g_hash_table_insert (message_notifications,
                                     contact,
                                     g_object_ref (event));
                g_hash_table_insert (event_notifications,
                                     g_object_ref (notify),
                                     g_object_ref (event));
            }
        }
    }
    else if (type == GOSSIP_EVENT_SUBSCRIPTION_REQUEST) {
        /* Give a chance to the server to have time to give us infos */
        g_timeout_add (1000,
                       (GSourceFunc) notify_subscription_request_show_cb,
                       gossip_event_get_data (event));
    }
    else if (type == GOSSIP_EVENT_FILE_TRANSFER_REQUEST) {
        notify_file_transfer_request (GOSSIP_FT (gossip_event_get_data (event)));
    }
}

static gboolean
notify_message_remove_foreach (gpointer     key,
                               GossipEvent *event,
                               GossipEvent *event_to_compare)
{
    if (gossip_event_equal (event, event_to_compare)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean
notify_event_remove_foreach (gpointer     key,
                             GossipEvent *event,
                             GossipEvent *event_to_compare)
{
    if (gossip_event_equal (event, event_to_compare)) {
        notify_notification_close (key, NULL);
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
                                 (GHRFunc) notify_message_remove_foreach,
                                 event);
    g_hash_table_foreach_remove (event_notifications,
                                 (GHRFunc) notify_event_remove_foreach,
                                 event);
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
gossip_notify_set_attach_status_icon (GtkStatusIcon *new_attach)
{
    if (attach_status_icon) {
        g_object_remove_weak_pointer (G_OBJECT (attach_status_icon),
                                      (gpointer) &attach_status_icon);
    }

    attach_status_icon = new_attach;
    if (attach_status_icon) {
        g_object_add_weak_pointer (G_OBJECT (attach_status_icon),
                                   (gpointer) &attach_status_icon);
    }
}

static void
notify_hint_closed_cb (NotifyNotification *notification,
                       gpointer            user_data)
{
    GFunc        func;
    const gchar *conf_path;

    conf_path = g_object_get_data (G_OBJECT (notification), "conf_path");
    func = g_object_get_data (G_OBJECT (notification), "func");

    gossip_conf_set_bool (gossip_conf_get (), conf_path, FALSE);

    g_object_unref (notification);

    if (func) {
        (func) ((gpointer) conf_path, user_data);
    }
}

gboolean
gossip_notify_hint_show (const gchar        *conf_path,
                         const gchar        *summary,
                         const gchar        *message,
                         GFunc               func,
                         gpointer            user_data)
{
    gboolean            ok;
    gboolean            show_hint = TRUE;
    NotifyNotification *notify;
    GError             *error = NULL;

    g_return_val_if_fail (conf_path != NULL, FALSE);
    g_return_val_if_fail (summary != NULL, FALSE);

    ok = gossip_conf_get_bool (gossip_conf_get (),
                               conf_path,
                               &show_hint);

    if (ok && !show_hint) {
        return FALSE;
    }

    notify = notify_notification_new (summary, message, NULL, NULL);
    g_object_set_data_full (G_OBJECT (notify), "conf_path", g_strdup (conf_path), g_free);
    g_object_set_data (G_OBJECT (notify), "func", func);
    g_signal_connect (notify,
                      "closed",
                      G_CALLBACK (notify_hint_closed_cb),
                      user_data);

    if (attach_status_icon) {
        notify_notification_attach_to_status_icon (notify, attach_status_icon);
    }

    if (!notify_notification_show (notify, &error)) {
        g_warning ("Failed to send notification: %s",
                   error->message);
        g_error_free (error);
    }

    return TRUE;
}

void
gossip_notify_init (GossipSession      *session,
                    GossipEventManager *event_manager)
{
    g_return_if_fail (GOSSIP_IS_SESSION (session));
    g_return_if_fail (GOSSIP_IS_EVENT_MANAGER (event_manager));

    gossip_debug (DEBUG_DOMAIN, "Initiating...");

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
                                                 (GDestroyNotify) g_object_unref,
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
    g_signal_connect (session, "protocol-disconnected",
                      G_CALLBACK (notify_protocol_disconnected_cb),
                      NULL);
    g_signal_connect (session, "contact-added",
                      G_CALLBACK (notify_contact_added_cb),
                      NULL);
    g_signal_connect (session, "contact-removed",
                      G_CALLBACK (notify_contact_removed_cb),
                      NULL);

    g_signal_connect (event_manager, "event-added",
                      G_CALLBACK (notify_event_added_cb),
                      NULL);
    g_signal_connect (event_manager, "event-removed",
                      G_CALLBACK (notify_event_removed_cb),
                      NULL);

    inited = TRUE;
}

void
gossip_notify_finalize (void)
{
    if (!inited) {
        return;
    }

    g_hash_table_destroy (account_states);
    g_hash_table_destroy (contact_states);
    g_hash_table_destroy (message_notifications);
    g_hash_table_destroy (event_notifications);

    notify_uninit ();
}

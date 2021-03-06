/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
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

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include "gossip-app.h"
#include "gossip-cell-renderer-expander.h"
#include "gossip-cell-renderer-text.h"
#include "gossip-chat.h"
#include "gossip-chat-invite.h"
#include "gossip-chat-view.h"
#include "gossip-contact-list-iface.h"
#include "gossip-contact-info-dialog.h"
#include "gossip-glade.h"
#include "gossip-group-chat.h"
#include "gossip-private-chat.h"
#include "gossip-sound.h"
#include "gossip-ui-utils.h"

#define DEBUG_DOMAIN "GroupChat"

#define IS_ENTER(v) (v == GDK_Return || v == GDK_ISO_Enter || v == GDK_KP_Enter)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_GROUP_CHAT, GossipGroupChatPriv))

struct _GossipGroupChatPriv {
    GossipChatroomProvider *chatroom_provider;
    GossipChatroom         *chatroom;
    GossipChatroomStatus    last_status;

    GtkWidget              *widget;
    GtkWidget              *hpaned;
    GtkWidget              *vbox_left;

    GtkWidget              *scrolled_window_chat;
    GtkWidget              *scrolled_window_input;
    GtkWidget              *scrolled_window_contacts;

    GtkWidget              *hbox_subject;
    GtkWidget              *label_subject;
    gchar                  *subject;

    GtkWidget              *treeview;

    GCompletion            *completion;

    GList                  *private_chats;

    guint                   scroll_idle_id;

    gint                    contacts_width;
    gboolean                contacts_visible;

    GossipTime              time_joined;
};

typedef struct {
    GossipGroupChat *chat;
    GossipContact   *contact;
    gboolean         found;
    GtkTreeIter      found_iter;
} ContactFindData;

typedef struct {
    GossipGroupChat *chat;
    GossipContact   *contact;
    GtkWidget       *entry;
} ChatInviteData;

typedef struct {
    GtkTreeIter        iter;
    GossipChatroomRole role;
    gboolean           found;
} FindRoleIterData;

static void            group_chat_contact_list_iface_init     (GossipContactListIfaceClass  *iface);
static void            group_chat_finalize                    (GObject                      *object);
static void            group_chat_retry_connection_clicked_cb (GtkWidget                    *button,
                                                               GossipGroupChat              *chat);
static void            group_chat_join                        (GossipGroupChat              *chat);
static void            group_chat_join_cb                     (GossipChatroomProvider       *provider,
                                                               GossipChatroomId              id,
                                                               GossipChatroomError           error,
                                                               gpointer                      user_data);
static void            group_chat_protocol_connected_cb       (GossipSession                *session,
                                                               GossipAccount                *account,
                                                               GossipJabber                 *jabber,
                                                               GossipGroupChat              *chat);
static gboolean        group_chat_key_press_event_cb          (GtkWidget                    *widget,
                                                               GdkEventKey                  *event,
                                                               GossipGroupChat              *chat);
static gboolean        group_chat_focus_in_event_cb           (GtkWidget                    *widget,
                                                               GdkEvent                     *event,
                                                               GossipGroupChat              *chat);
static void            group_chat_drag_data_received          (GtkWidget                    *widget,
                                                               GdkDragContext               *context,
                                                               int                           x,
                                                               int                           y,
                                                               GtkSelectionData             *selection,
                                                               guint                         info,
                                                               guint                         time,
                                                               GossipGroupChat              *chat);
static void            group_chat_widget_destroy_cb           (GtkWidget                    *widget,
                                                               GossipGroupChat              *chat);
static gint            group_chat_contacts_completion_func    (const gchar                  *s1,
                                                               const gchar                  *s2,
                                                               gsize                         n);
static void            group_chat_create_ui                   (GossipGroupChat              *chat);
static void            group_chat_chatroom_name_cb            (GossipChatroom               *chatroom,
                                                               GParamSpec                   *spec,
                                                               GossipGroupChat              *chat);
static void            group_chat_chatroom_status_cb          (GossipChatroom               *chatroom,
                                                               GParamSpec                   *spec,
                                                               GossipGroupChat              *chat);
static void            group_chat_kicked_cb                   (GossipChatroomProvider       *provider,
                                                               gint                          id,
                                                               GossipGroupChat              *chat);
static void            group_chat_nick_changed_cb             (GossipChatroomProvider       *provider,
                                                               gint                          id,
                                                               GossipContact                *contact,
                                                               const gchar                  *old_nick,
                                                               GossipGroupChat              *chat);
static void            group_chat_new_message_cb              (GossipChatroomProvider       *provider,
                                                               gint                          id,
                                                               GossipMessage                *message,
                                                               GossipGroupChat              *chat);
static void            group_chat_new_event_cb                (GossipChatroomProvider       *provider,
                                                               gint                          id,
                                                               const gchar                  *event,
                                                               GossipGroupChat              *chat);
static void            group_chat_subject_changed_cb          (GossipChatroomProvider       *provider,
                                                               gint                          id,
                                                               GossipContact                *who,
                                                               const gchar                  *new_subject,
                                                               GossipGroupChat              *chat);
static void            group_chat_error_cb                    (GossipChatroomProvider       *provider,
                                                               GossipChatroomId              id,
                                                               GossipChatroomError           error,
                                                               GossipGroupChat              *chat);
static void            group_chat_dialog_entry_activate_cb    (GtkWidget                    *entry,
                                                               GtkDialog                    *dialog);
static void            group_chat_dialog_response_cb          (GtkWidget                    *dialog,
                                                               gint                          response,
                                                               GossipGroupChat              *chat);
static void            group_chat_dialog_new                  (GossipGroupChat              *chat,
                                                               const gchar                  *action,
                                                               const gchar                  *message,
                                                               const gchar                  *pretext);
static void            group_chat_contact_joined_cb           (GossipChatroom               *chatroom,
                                                               GossipContact                *contact,
                                                               GossipGroupChat              *chat);
static void            group_chat_contact_left_cb             (GossipChatroom               *chatroom,
                                                               GossipContact                *contact,
                                                               GossipGroupChat              *chat);
static void            group_chat_contact_info_changed_cb     (GossipChatroom               *chatroom,
                                                               GossipContact                *contact,
                                                               GossipGroupChat              *chat);
static void            group_chat_contact_add                 (GossipGroupChat              *chat,
                                                               GossipContact                *contact);
static void            group_chat_contact_remove              (GossipGroupChat              *chat,
                                                               GossipContact                *contact);
static void            group_chat_contact_presence_updated_cb (GossipContact                *contact,
                                                               GParamSpec                   *param,
                                                               GossipGroupChat              *chat);
static void            group_chat_contact_updated_cb          (GossipContact                *contact,
                                                               GParamSpec                   *param,
                                                               GossipGroupChat              *chat);
static void            group_chat_private_chat_new            (GossipGroupChat              *chat,
                                                               GossipContact                *contact);
static void            group_chat_private_chat_removed        (GossipGroupChat              *chat,
                                                               GossipChat                   *private_chat);
static void            group_chat_private_chat_stop_foreach   (GossipChat                   *private_chat,
                                                               GossipGroupChat              *chat);
static void            group_chat_send                        (GossipGroupChat              *chat);
static gboolean        group_chat_get_nick_list_foreach       (GtkTreeModel                 *model,
                                                               GtkTreePath                  *path,
                                                               GtkTreeIter                  *iter,
                                                               GList                       **list);
static GList *         group_chat_get_nick_list               (GossipGroupChat              *chat);
static GtkWidget *     group_chat_get_widget                  (GossipChat                   *chat);
static const gchar *   group_chat_get_name                    (GossipChat                   *chat);
static gchar *         group_chat_get_tooltip                 (GossipChat                   *chat);
static GdkPixbuf *     group_chat_get_status_pixbuf           (GossipChat                   *chat);
static GossipContact * group_chat_get_own_contact             (GossipChat                   *chat);
static GossipChatroom *group_chat_get_chatroom                (GossipChat                   *chat);
static gboolean        group_chat_get_show_contacts           (GossipChat                   *chat);
static void            group_chat_set_show_contacts           (GossipChat                   *chat,
                                                               gboolean                      show);
static gboolean        group_chat_is_group_chat               (GossipChat                   *chat);
static gboolean        group_chat_is_connected                (GossipChat                   *chat);
static void            group_chat_get_role_iter               (GossipGroupChat              *chat,
                                                               GossipChatroomRole            role,
                                                               GtkTreeIter                  *iter);
static void            group_chat_cl_row_activated_cb         (GtkTreeView                  *view,
                                                               GtkTreePath                  *path,
                                                               GtkTreeViewColumn            *col,
                                                               GossipGroupChat              *chat);
static gboolean        group_chat_cl_button_press_event_cb    (GtkTreeView                  *view,
                                                               GdkEventButton               *event,
                                                               GossipGroupChat              *chat);
static gint            group_chat_cl_sort_func                (GtkTreeModel                 *model,
                                                               GtkTreeIter                  *iter_a,
                                                               GtkTreeIter                  *iter_b,
                                                               gpointer                      user_data);
static void            group_chat_cl_setup                    (GossipGroupChat              *chat);
static gboolean        group_chat_cl_find_foreach             (GtkTreeModel                 *model,
                                                               GtkTreePath                  *path,
                                                               GtkTreeIter                  *iter,
                                                               ContactFindData              *data);
static gboolean        group_chat_cl_find                     (GossipGroupChat              *chat,
                                                               GossipContact                *contact,
                                                               GtkTreeIter                  *iter);
static void            group_chat_cl_pixbuf_cell_data_func    (GtkTreeViewColumn            *tree_column,
                                                               GtkCellRenderer              *cell,
                                                               GtkTreeModel                 *model,
                                                               GtkTreeIter                  *iter,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_text_cell_data_func      (GtkTreeViewColumn            *tree_column,
                                                               GtkCellRenderer              *cell,
                                                               GtkTreeModel                 *model,
                                                               GtkTreeIter                  *iter,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_expander_cell_data_func  (GtkTreeViewColumn            *tree_column,
                                                               GtkCellRenderer              *cell,
                                                               GtkTreeModel                 *model,
                                                               GtkTreeIter                  *iter,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_set_background           (GossipGroupChat              *chat,
                                                               GtkCellRenderer              *cell,
                                                               gboolean                      is_header);
static GtkWidget *     group_chat_cl_menu_create              (GossipGroupChat              *chat,
                                                               GossipContact                *contact);
static void            group_chat_cl_menu_destroy             (GtkWidget                    *menu,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_menu_info_activate_cb    (GtkMenuItem                  *menuitem,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_menu_chat_activate_cb    (GtkMenuItem                  *menuitem,
                                                               GossipGroupChat              *chat);
static void            group_chat_cl_menu_kick_activate_cb    (GtkMenuItem                  *menuitem,
                                                               GossipGroupChat              *chat);
static void            group_chat_set_scrolling_for_events    (GossipGroupChat              *chat,
                                                               gboolean                      disable);
static gboolean        group_chat_scroll_down_when_idle_func  (GossipGroupChat              *chat);
static void            group_chat_scroll_down_when_idle       (GossipGroupChat              *chat);

enum {
    COL_STATUS,
    COL_NAME,
    COL_CONTACT,
    COL_IS_HEADER,
    COL_HEADER_ROLE,
    NUMBER_OF_COLS
};

typedef enum {
    DND_DRAG_TYPE_CONTACT_ID,
} DndDragType;

static const GtkTargetEntry drag_types_dest[] = {
    { "text/contact-id", GTK_TARGET_SAME_APP, DND_DRAG_TYPE_CONTACT_ID },
};

static GHashTable *group_chats = NULL;

G_DEFINE_TYPE_WITH_CODE (GossipGroupChat, gossip_group_chat,
                         GOSSIP_TYPE_CHAT,
                         G_IMPLEMENT_INTERFACE (GOSSIP_TYPE_CONTACT_LIST_IFACE,
                                                group_chat_contact_list_iface_init));

static void
gossip_group_chat_class_init (GossipGroupChatClass *klass)
{
    GObjectClass    *object_class;
    GossipChatClass *chat_class;

    object_class = G_OBJECT_CLASS (klass);
    chat_class = GOSSIP_CHAT_CLASS (klass);

    object_class->finalize        = group_chat_finalize;

    chat_class->get_name          = group_chat_get_name;
    chat_class->get_tooltip       = group_chat_get_tooltip;
    chat_class->get_status_pixbuf = group_chat_get_status_pixbuf;
    chat_class->get_contact       = NULL;
    chat_class->get_own_contact   = group_chat_get_own_contact;
    chat_class->get_chatroom      = group_chat_get_chatroom;
    chat_class->get_widget        = group_chat_get_widget;
    chat_class->get_show_contacts = group_chat_get_show_contacts;
    chat_class->set_show_contacts = group_chat_set_show_contacts;
    chat_class->is_group_chat     = group_chat_is_group_chat;
    chat_class->is_connected      = group_chat_is_connected;

    g_type_class_add_private (object_class, sizeof (GossipGroupChatPriv));
}

static void
gossip_group_chat_init (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipChatView      *chatview;

    priv = GET_PRIV (chat);

    priv->contacts_visible = TRUE;

    g_signal_connect_object (gossip_app_get_session (),
                             "protocol-connected",
                             G_CALLBACK (group_chat_protocol_connected_cb),
                             chat, 0);

    chatview = GOSSIP_CHAT_VIEW (GOSSIP_CHAT (chat)->view);
    gossip_chat_view_set_is_group_chat (chatview, TRUE);

    group_chat_create_ui (chat);
}

static void
group_chat_contact_list_iface_init (GossipContactListIfaceClass *iface)
{
    /* No functions for now */
}

static void
group_chat_finalize (GObject *object)
{
    GossipGroupChat      *chat;
    GossipGroupChatPriv  *priv;
    GossipChatroomId      id;
    GossipChatroomStatus  status;
    GList                *l;
    GList                *contacts;

    gossip_debug (DEBUG_DOMAIN, "Finalized:%p", object);

    chat = GOSSIP_GROUP_CHAT (object);
    priv = GET_PRIV (chat);
        
    /* Make absolutely sure we don't still have the chatroom ID in
     * the hash table, this can happen when we have called
     * g_object_unref (chat) too many times. 
     *
     * This shouldn't happen.
     */
    id = gossip_chatroom_get_id (priv->chatroom);
    g_hash_table_steal (group_chats, GINT_TO_POINTER (id));

    contacts = gossip_chatroom_get_contacts (priv->chatroom);
    for (l = contacts; l; l = l->next) {
        g_signal_handlers_disconnect_by_func (l->data,
                                              group_chat_contact_updated_cb,
                                              chat);
        g_signal_handlers_disconnect_by_func (l->data,
                                              group_chat_contact_presence_updated_cb,
                                              chat);
    }

    g_list_free (contacts);

    g_signal_handlers_disconnect_by_func (priv->chatroom, 
                                          group_chat_chatroom_name_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom, 
                                          group_chat_chatroom_status_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_kicked_cb, 
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_nick_changed_cb, 
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_new_message_cb, 
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_new_event_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_subject_changed_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom_provider,
                                          group_chat_error_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom,
                                          group_chat_contact_joined_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom,
                                          group_chat_contact_left_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (priv->chatroom,
                                          group_chat_contact_info_changed_cb,
                                          chat);

    /* Make sure we do this AFTER removing signal handlers
     * because when we update the status, we get called back and
     * the widget is destroyed. 
     */
    status = gossip_chatroom_get_status (priv->chatroom);
    if (status == GOSSIP_CHATROOM_STATUS_ACTIVE) {
        gossip_chatroom_provider_leave (priv->chatroom_provider,
                                        gossip_chatroom_get_id (priv->chatroom));
    } else if (status == GOSSIP_CHATROOM_STATUS_JOINING) {
        gossip_chatroom_provider_cancel (priv->chatroom_provider,
                                         gossip_chatroom_get_id (priv->chatroom));
    }
        
    g_object_unref (priv->chatroom);
        
    g_object_unref (priv->chatroom_provider);

    g_free (priv->subject);

    g_list_foreach (priv->private_chats,
                    (GFunc) group_chat_private_chat_stop_foreach,
                    chat);
    g_list_free (priv->private_chats);

    if (priv->scroll_idle_id) {
        g_source_remove (priv->scroll_idle_id);
    }

    G_OBJECT_CLASS (gossip_group_chat_parent_class)->finalize (object);
}

static void
group_chat_retry_connection_clicked_cb (GtkWidget       *button,
                                        GossipGroupChat *chat)
{
    group_chat_join (chat);
    gtk_widget_set_sensitive (button, FALSE);
}

static void
group_chat_set_scrolling_for_events (GossipGroupChat *chat,
                                     gboolean         disable)
{
    GossipGroupChatPriv *priv;
    gboolean             recently_joined;

    priv = GET_PRIV (chat);

    recently_joined = priv->time_joined + 5 > gossip_time_get_current ();

    if (!recently_joined) {
        return;
    }

    if (disable) {
        /* If we joined the group chat in the last 5 seconds, don't
         * scroll on new presence messages. We need to do this because
         * unlike normal messages, we don't have a timestamp all the
         * time.
         */
        gossip_chat_view_allow_scroll (GOSSIP_CHAT (chat)->view, FALSE);
    } else {
        /* Enable scrolling again */
        gossip_chat_view_allow_scroll (GOSSIP_CHAT (chat)->view, TRUE);
    }
}

static void
group_chat_chatroom_status_update (GossipGroupChat     *chat,
                                   GossipChatroomError  error)
{
    GossipGroupChatPriv  *priv;
    GossipChatroomStatus  status;
    const gchar          *event_str;

    priv = GET_PRIV (chat);

    status = gossip_chatroom_get_status (priv->chatroom);

    /* If the state is different, then do something */
    if (status == priv->last_status) {
        return;
    }

    priv->last_status = status;

    /* Make widgets available based on state */
    gtk_widget_set_sensitive (priv->hbox_subject, 
                              status == GOSSIP_CHATROOM_STATUS_ACTIVE);
    gtk_widget_set_sensitive (priv->scrolled_window_contacts, 
                              status == GOSSIP_CHATROOM_STATUS_ACTIVE);
    gtk_widget_set_sensitive (priv->scrolled_window_input, 
                              status == GOSSIP_CHATROOM_STATUS_ACTIVE);

    /* Clear previous roster if re-connecting */
    if (status == GOSSIP_CHATROOM_STATUS_ACTIVE && 
        error != GOSSIP_CHATROOM_ERROR_ALREADY_OPEN) {
        GtkTreeView  *view;
        GtkTreeModel *model;
        GtkTreeStore *store;
                
        view = GTK_TREE_VIEW (priv->treeview);
        model = gtk_tree_view_get_model (view);
        store = GTK_TREE_STORE (model);
        gtk_tree_store_clear (store);
    }
        
    /* Either print state or error */
    if (error == GOSSIP_CHATROOM_ERROR_NONE ||
        error == GOSSIP_CHATROOM_ERROR_ALREADY_OPEN) {
        event_str = gossip_chatroom_status_to_string (status);
    } else {
        event_str = gossip_chatroom_error_to_string (error);
    }
        
    /* Add status event message */
    group_chat_set_scrolling_for_events (chat, TRUE);
    gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, event_str);
    group_chat_set_scrolling_for_events (chat, FALSE);
        
    /* If we have a situation where we can retry, add a button to
     * do so. 
     */
    if (error == GOSSIP_CHATROOM_ERROR_NICK_IN_USE ||
        error == GOSSIP_CHATROOM_ERROR_MAXIMUM_USERS_REACHED ||
        error == GOSSIP_CHATROOM_ERROR_TIMED_OUT ||
        error == GOSSIP_CHATROOM_ERROR_UNKNOWN ||
        error == GOSSIP_CHATROOM_ERROR_CANCELED) {
        GtkWidget *button;

        /* FIXME: Need special case for nickname to put an
         * entry in the chat view and to request a new nick.
         */
        button = gtk_button_new_with_label (_("Retry connection"));
        g_signal_connect (button, "clicked", 
                          G_CALLBACK (group_chat_retry_connection_clicked_cb),
                          chat);

        group_chat_set_scrolling_for_events (chat, TRUE);
        gossip_chat_view_append_button (GOSSIP_CHAT (chat)->view,
                                        NULL,
                                        button,
                                        NULL);
        group_chat_set_scrolling_for_events (chat, FALSE);
    }

    /* Signal to the chat object to update */
    g_signal_emit_by_name (chat, "status-changed");

    gtk_widget_grab_focus (GOSSIP_CHAT (chat)->input_text_view);
}

static void
group_chat_join (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;

    priv = GET_PRIV (chat);

    gossip_chatroom_provider_join (priv->chatroom_provider,
                                   priv->chatroom,
                                   (GossipChatroomJoinCb) group_chat_join_cb,
                                   NULL);
}

static void     
group_chat_join_cb (GossipChatroomProvider *provider,
                    GossipChatroomId        id,
                    GossipChatroomError     error,
                    gpointer                user_data)
{
    GossipGroupChatPriv *priv;
    GossipChat          *chat;

    chat = g_hash_table_lookup (group_chats, GINT_TO_POINTER (id));

    if (!chat) {
        return;
    }

    priv = GET_PRIV (chat);

    /* Set the time we joined this chatroom so we know for new
     * messages if they are backlog or not.
     */
    priv->time_joined = gossip_time_get_current ();

    gossip_debug (DEBUG_DOMAIN, 
                  "Join callback for id:%d, error:%d->'%s'", 
                  id, 
                  error, 
                  gossip_chatroom_error_to_string (error));

    group_chat_chatroom_status_update (GOSSIP_GROUP_CHAT (chat), error);
}

static GtkWidget*
group_chat_cl_menu_create (GossipGroupChat *chat,
                           GossipContact   *contact)
{
    GladeXML      *glade;
    GtkWidget     *menu;
    GtkWidget     *menu_item_kick;
    GossipContact *own_contact;
    gboolean       can_kick = TRUE;
        
    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);
        
    glade = gossip_glade_get_file ("group-chat.glade",
                                   "contact_menu",
                                   NULL,
                                   "contact_menu", &menu,
                                   "kick", &menu_item_kick,
                                   NULL);
        
    gossip_glade_connect (glade,
                          chat,
                          "contact_menu", "selection-done", group_chat_cl_menu_destroy,
                          "private_chat", "activate", group_chat_cl_menu_chat_activate_cb,
                          "contact_information", "activate", group_chat_cl_menu_info_activate_cb,
                          "kick", "activate", group_chat_cl_menu_kick_activate_cb,
                          NULL);
        
    own_contact = gossip_chat_get_own_contact (GOSSIP_CHAT (chat));
    can_kick &= gossip_chat_is_connected (GOSSIP_CHAT (chat));
    can_kick &= gossip_group_chat_contact_can_kick (chat, own_contact);

    gtk_widget_set_sensitive (menu_item_kick, can_kick);
        
    g_object_unref (glade); 
        
    return menu;    
}

static void
group_chat_cl_menu_destroy (GtkWidget       *menu,
                            GossipGroupChat *chat)
{
    gtk_widget_destroy (menu);
    g_object_unref (menu);
}

static void
group_chat_cl_menu_info_activate_cb (GtkMenuItem     *menuitem,
                                     GossipGroupChat *chat)
{
    GossipContact *contact;
        
    contact = gossip_group_chat_get_selected_contact (chat);
        
    gossip_contact_info_dialog_show (contact,
                                     NULL);
    g_object_unref (contact);
        
}
static void
group_chat_cl_menu_chat_activate_cb (GtkMenuItem     *menuitem,
                                     GossipGroupChat *chat)
{
    GossipContact *contact;
        
    contact = gossip_group_chat_get_selected_contact(chat);
        
    group_chat_private_chat_new (chat, contact);
        
    g_object_unref (contact);
        
}

static void
group_chat_cl_menu_kick_activate_cb (GtkMenuItem     *menuitem,
                                     GossipGroupChat *chat)
{
    gossip_group_chat_contact_kick (chat, NULL);
}
 
static void
group_chat_protocol_connected_cb (GossipSession   *session,
                                  GossipAccount   *account,
                                  GossipJabber    *jabber,
                                  GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipAccount       *this_account;
    GossipContact       *own_contact;

    priv = GET_PRIV (chat);

    own_contact = gossip_chatroom_get_own_contact (priv->chatroom);
    this_account = gossip_contact_get_account (own_contact);

    if (!gossip_account_equal (this_account, account)) {
        return;
    }

    /* Here we attempt to reconnect if we are not already connected */
    if (gossip_chatroom_get_status (priv->chatroom) == GOSSIP_CHATROOM_STATUS_ACTIVE) {
        return;
    }

    group_chat_join (chat);
}

static gboolean
group_chat_key_press_event_cb (GtkWidget       *widget,
                               GdkEventKey     *event,
                               GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkAdjustment       *adj;
    gdouble              val;
    GtkTextBuffer       *buffer;
    GtkTextIter          start, current;
    gchar               *nick, *completed;
    gint                 len;
    GList               *list, *l, *completed_list;
    gboolean  is_start_of_buffer;

    priv = GET_PRIV (chat);

    /* Catch ctrl+up/down so we can traverse messages we sent */
    if ((event->state & GDK_CONTROL_MASK) && 
        (event->keyval == GDK_Up || 
         event->keyval == GDK_Down)) {
        GtkTextBuffer *buffer;
        const gchar   *str;

        buffer = gtk_text_view_get_buffer 
            (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view));

        if (event->keyval == GDK_Up) {
            str = gossip_chat_sent_message_get_next (GOSSIP_CHAT (chat));
        } else {
            str = gossip_chat_sent_message_get_last (GOSSIP_CHAT (chat));
        }

        gtk_text_buffer_set_text (buffer, str ? str : "", -1);

        return TRUE;    
    }

    /* Catch enter but not ctrl/shift-enter */
    if (IS_ENTER (event->keyval) && !(event->state & GDK_SHIFT_MASK)) {
        GossipChat   *parent;
        GtkTextView  *view;
        GtkIMContext *context;

        /* This is to make sure that kinput2 gets the enter. And if
         * it's handled there we shouldn't send on it. This is because
         * kinput2 uses Enter to commit letters. See:
         * http://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=104299
         */
        parent = GOSSIP_CHAT (chat);
        view = GTK_TEXT_VIEW (parent->input_text_view);
        context = view->im_context;

        if (gtk_im_context_filter_keypress (context, event)) {
            GTK_TEXT_VIEW (view)->need_im_reset = TRUE;
            return TRUE;
        }

        group_chat_send (chat);
        return TRUE;
    }

    if (IS_ENTER (event->keyval) && (event->state & GDK_SHIFT_MASK)) {
        /* Newline for shift-enter. */
        return FALSE;
    }
    else if ((event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK &&
             event->keyval == GDK_Page_Up) {
        adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolled_window_chat));
        gtk_adjustment_set_value (adj, adj->value - adj->page_size);

        return TRUE;
    }
    else if ((event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK &&
             event->keyval == GDK_Page_Down) {
        adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolled_window_chat));
        val = MIN (adj->value + adj->page_size, adj->upper - adj->page_size);
        gtk_adjustment_set_value (adj, val);

        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK &&
        (event->state & GDK_SHIFT_MASK) != GDK_SHIFT_MASK &&
        event->keyval == GDK_Tab) {
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view));
        gtk_text_buffer_get_iter_at_mark (buffer, &current, gtk_text_buffer_get_insert (buffer));

        /* Get the start of the nick to complete. */
        gtk_text_buffer_get_iter_at_mark (buffer, &start, gtk_text_buffer_get_insert (buffer));
        gtk_text_iter_backward_word_start (&start);
        is_start_of_buffer = gtk_text_iter_is_start (&start);

        nick = gtk_text_buffer_get_text (buffer, &start, &current, FALSE);

        g_completion_clear_items (priv->completion);

        len = strlen (nick);

        list = group_chat_get_nick_list (chat);

        g_completion_add_items (priv->completion, list);

        completed_list = g_completion_complete (priv->completion,
                                                nick,
                                                &completed);

        g_free (nick);

        if (completed) {
            int       len;
            gchar    *text;

            gtk_text_buffer_delete (buffer, &start, &current);

            len = g_list_length (completed_list);

            if (len == 1) {
                /* If we only have one hit, use that text
                 * instead of the text in completed since the
                 * completed text will use the typed string
                 * which might be cased all wrong.
                 * Fixes #120876
                 * */
                text = (gchar *) completed_list->data;
            } else {
                text = completed;
            }

            gtk_text_buffer_insert_at_cursor (buffer, text, strlen (text));

            if (len == 1) {
                if (is_start_of_buffer) {
                    gtk_text_buffer_insert_at_cursor (buffer, ", ", 2);
                }
            }

            g_free (completed);
        }

        g_completion_clear_items (priv->completion);

        for (l = list; l; l = l->next) {
            g_free (l->data);
        }

        g_list_free (list);

        return TRUE;
    }

    return FALSE;
}

static gboolean
group_chat_focus_in_event_cb (GtkWidget       *widget,
                              GdkEvent        *event,
                              GossipGroupChat *chat)
{
    gtk_widget_grab_focus (GOSSIP_CHAT (chat)->input_text_view);

    return TRUE;
}

static void
group_chat_drag_data_received (GtkWidget        *widget,
                               GdkDragContext   *context,
                               int               x,
                               int               y,
                               GtkSelectionData *selection,
                               guint             info,
                               guint             time,
                               GossipGroupChat  *chat)
{
    if (info == DND_DRAG_TYPE_CONTACT_ID) {
        GossipGroupChatPriv  *priv;
        GossipContactManager *contact_manager;
        GossipContact        *contact;
        const gchar          *id;
        gchar                *str;

        priv = GET_PRIV (chat);
                
        id = (const gchar*) selection->data;
                
        contact_manager = gossip_session_get_contact_manager (gossip_app_get_session ());
        contact = gossip_contact_manager_find (contact_manager, NULL, id);

        if (!contact) {
            gossip_debug (DEBUG_DOMAIN, 
                          "Drag data received, but no contact found by the id:'%s'", 
                          id);
            return;
        }
                
        gossip_debug (DEBUG_DOMAIN, 
                      "Drag data received, for contact:'%s'", 
                      id);
                
        /* Send event to chat window */
        str = g_strdup_printf (_("Invited %s to join this chat conference."),
                               gossip_contact_get_name (contact));

        group_chat_set_scrolling_for_events (chat, TRUE);
        gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, str);
        group_chat_set_scrolling_for_events (chat, FALSE);

        g_free (str);
                
        gossip_chat_invite_dialog_show (contact, gossip_chatroom_get_id (priv->chatroom));
                
        gtk_drag_finish (context, TRUE, FALSE, time);
    } else {
        gossip_debug (DEBUG_DOMAIN, "Received drag & drop from unknown source");
        gtk_drag_finish (context, FALSE, FALSE, time);
    }
}

static void
group_chat_widget_destroy_cb (GtkWidget       *widget,
                              GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipChatroomId     id;

    gossip_debug (DEBUG_DOMAIN, "Destroyed, removing from hash table");

    priv = GET_PRIV (chat);
    id = gossip_chatroom_get_id (priv->chatroom);

    g_hash_table_remove (group_chats, GINT_TO_POINTER (id));
}

static gint
group_chat_contacts_completion_func (const gchar *s1,
                                     const gchar *s2,
                                     gsize        n)
{
    gchar *tmp, *nick1, *nick2;
    gint   ret;

    tmp = g_utf8_normalize (s1, -1, G_NORMALIZE_DEFAULT);
    nick1 = g_utf8_casefold (tmp, -1);
    g_free (tmp);

    tmp = g_utf8_normalize (s2, -1, G_NORMALIZE_DEFAULT);
    nick2 = g_utf8_casefold (tmp, -1);
    g_free (tmp);

    ret = strncmp (nick1, nick2, n);

    g_free (nick1);
    g_free (nick2);

    return ret;
}

static void
group_chat_create_ui (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GList               *list = NULL; 
    GtkWidget           *temp_widget;

    priv = GET_PRIV (chat);

    priv->widget = g_object_new (GTK_TYPE_VBOX,
                                 "homogeneous", FALSE,
                                 "spacing", 6,
                                 "border-width", 4,
                                 NULL);

    priv->hbox_subject = gtk_hbox_new (FALSE, 6);

    gtk_box_pack_start (GTK_BOX (priv->widget), priv->hbox_subject,
                        FALSE, FALSE, 2);

    temp_widget = g_object_new (GTK_TYPE_LABEL,
                                "label", _("<b>Subject:</b>"),
                                "use-markup", TRUE,
                                NULL);

    gtk_box_pack_start (GTK_BOX (priv->hbox_subject), temp_widget,
                        FALSE, FALSE, 0);

    priv->label_subject = g_object_new (GTK_TYPE_LABEL,
                                        "use-markup", TRUE,
                                        "single-line-mode", TRUE,
                                        "ellipsize", PANGO_ELLIPSIZE_END,
                                        "selectable", TRUE,
                                        "xalign", 0.0,
                                        NULL);

    gtk_box_pack_start (GTK_BOX (priv->hbox_subject), priv->label_subject,
                        TRUE, TRUE, 0);

    priv->hpaned = gtk_hpaned_new ();

    gtk_box_pack_start (GTK_BOX (priv->widget), priv->hpaned,
                        TRUE, TRUE, 0);

        
    priv->vbox_left = gtk_vbox_new (FALSE, 6);

    gtk_paned_pack1 (GTK_PANED (priv->hpaned), priv->vbox_left,
                     TRUE, TRUE);
        
    priv->scrolled_window_contacts = 
        g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                      "hscrollbar-policy", GTK_POLICY_NEVER,
                      "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                      "shadow-type", GTK_SHADOW_IN,
                      NULL);

    gtk_paned_pack2 (GTK_PANED (priv->hpaned), 
                     priv->scrolled_window_contacts,
                     FALSE, FALSE);

    priv->scrolled_window_chat =
        g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                      "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                      "vscrollbar-policy", GTK_POLICY_ALWAYS,
                      "shadow-type", GTK_SHADOW_IN,
                      NULL);

    gtk_box_pack_start (GTK_BOX (priv->vbox_left), 
                        priv->scrolled_window_chat,
                        TRUE, TRUE, 0);

    priv->scrolled_window_input =
        g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                      "hscrollbar-policy", GTK_POLICY_NEVER,
                      "vscrollbar-policy", GTK_POLICY_NEVER,
                      "shadow-type", GTK_SHADOW_IN,
                      NULL);

    gtk_box_pack_start (GTK_BOX (priv->vbox_left),
                        priv->scrolled_window_input,
                        FALSE, TRUE, 0);

    priv->treeview = g_object_new (GTK_TYPE_TREE_VIEW,
                                   "headers-visible", FALSE,
                                   "enable-search", TRUE,
                                   "width-request", 115,
                                   NULL);

    gtk_container_add (GTK_CONTAINER (priv->scrolled_window_contacts),
                       priv->treeview);

    gtk_widget_show_all (priv->widget);

    g_signal_connect (priv->widget, "destroy", 
                      G_CALLBACK (group_chat_widget_destroy_cb),
                      chat);

    g_object_set_data (G_OBJECT (priv->widget), "chat", chat);

    /* Add room GtkTextView. */
    gtk_container_add (GTK_CONTAINER (priv->scrolled_window_chat),
                       GTK_WIDGET (GOSSIP_CHAT (chat)->view));
    gtk_widget_show (GTK_WIDGET (GOSSIP_CHAT (chat)->view));

    g_signal_connect (GOSSIP_CHAT (chat)->view, "focus_in_event",
                      G_CALLBACK (group_chat_focus_in_event_cb),
                      chat);

    /* Add input GtkTextView */
    gtk_container_add (GTK_CONTAINER (priv->scrolled_window_input),
                       GOSSIP_CHAT (chat)->input_text_view);
    gtk_widget_show (GOSSIP_CHAT (chat)->input_text_view);

    g_signal_connect (GOSSIP_CHAT (chat)->input_text_view, "key_press_event",
                      G_CALLBACK (group_chat_key_press_event_cb),
                      chat);

    /* Drag & drop */
    gtk_drag_dest_set (GTK_WIDGET (priv->treeview),
                       GTK_DEST_DEFAULT_ALL,
                       drag_types_dest,
                       G_N_ELEMENTS (drag_types_dest),
                       GDK_ACTION_MOVE);

    g_signal_connect (priv->treeview, "drag-data-received",
                      G_CALLBACK (group_chat_drag_data_received),
                      chat);

    /* Add nick name completion */
    priv->completion = g_completion_new (NULL);
    g_completion_set_compare (priv->completion,
                              group_chat_contacts_completion_func);

    group_chat_cl_setup (chat);

    /* Set widget focus order */
    list = g_list_append (NULL, priv->scrolled_window_input);
    gtk_container_set_focus_chain (GTK_CONTAINER (priv->vbox_left), list);
    g_list_free (list);

    list = g_list_append (NULL, priv->vbox_left);
    list = g_list_append (list, priv->scrolled_window_contacts);
    gtk_container_set_focus_chain (GTK_CONTAINER (priv->hpaned), list);
    g_list_free (list);

    list = g_list_append (NULL, priv->hpaned);
    list = g_list_append (list, priv->hbox_subject);
    gtk_container_set_focus_chain (GTK_CONTAINER (priv->widget), list);
    g_list_free (list);
}

/* Scroll down after the back-log has been received. */
static gboolean
group_chat_scroll_down_when_idle_func (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;

    priv = GET_PRIV (chat);

    gossip_chat_scroll_down (GOSSIP_CHAT (chat));

    priv->scroll_idle_id = 0;

    return FALSE;
}

static void
group_chat_scroll_down_when_idle (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
        
    priv = GET_PRIV (chat);
        
    /* Add an idle timeout to scroll to the bottom */
    if (priv->scroll_idle_id) {
        g_source_remove (priv->scroll_idle_id);
        priv->scroll_idle_id = 0;
    }
        
    priv->scroll_idle_id = g_idle_add ((GSourceFunc) 
                                       group_chat_scroll_down_when_idle_func, 
                                       chat);
}

static void
group_chat_chatroom_name_cb (GossipChatroom  *chatroom,
                             GParamSpec      *spec,
                             GossipGroupChat *chat)
{
    g_signal_emit_by_name (chat, "name-changed", gossip_chatroom_get_name (chatroom));
}

static void
group_chat_chatroom_status_cb (GossipChatroom  *chatroom,
                               GParamSpec      *spec,
                               GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipChatroomError  last_error;

    priv = GET_PRIV (chat);

    last_error = gossip_chatroom_get_last_error (priv->chatroom);
    group_chat_chatroom_status_update (chat, last_error);
}

static void
group_chat_kicked_cb (GossipChatroomProvider *provider,
                      gint                    id,
                      GossipGroupChat        *chat)
{
    GossipGroupChatPriv *priv;
    GossipChatView      *chatview;
    GList               *contacts;
    GList               *l;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    gossip_debug (DEBUG_DOMAIN, "[%d] Kicked from room", id);

    chatview = GOSSIP_CHAT (chat)->view;

    contacts = gossip_chatroom_get_contacts (priv->chatroom);
    for (l = contacts; l; l = l->next) {
        g_signal_handlers_disconnect_by_func (l->data,
                                              group_chat_contact_updated_cb,
                                              chat);
        g_signal_handlers_disconnect_by_func (l->data,
                                              group_chat_contact_presence_updated_cb,
                                              chat);
    }
    g_list_free (contacts);

    gtk_widget_set_sensitive (priv->hbox_subject, FALSE);
    gtk_widget_set_sensitive (priv->scrolled_window_contacts, FALSE);
    gtk_widget_set_sensitive (priv->scrolled_window_input, FALSE);
        
    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), TRUE);
    gossip_chat_view_append_event (chatview, _("You have been kicked from this room"));
    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), FALSE);

    g_signal_emit_by_name (chat, "status-changed");
}

static void
group_chat_nick_changed_cb (GossipChatroomProvider *provider,
                            gint                    id,
                            GossipContact          *contact,
                            const gchar            *old_nick,
                            GossipGroupChat        *chat)
{
    GossipGroupChatPriv *priv;
    GossipChatView      *chatview;
    gchar               *str;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    gossip_debug (DEBUG_DOMAIN,
                  "[%d] Nick changed for contact:'%s', old nick:'%s', new nick:'%s'", 
                  id, 
                  gossip_contact_get_id (contact),
                  old_nick,
                  gossip_contact_get_name (contact));

    chatview = GOSSIP_CHAT (chat)->view;

    str = g_strdup_printf (_("%s is now known as %s"),
                           old_nick,
                           gossip_contact_get_name (contact));
        
    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), TRUE);
    gossip_chat_view_append_event (chatview, str);
    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), FALSE);

    g_free (str);
}

static void
group_chat_new_message_cb (GossipChatroomProvider *provider,
                           gint                    id,
                           GossipMessage          *message,
                           GossipGroupChat        *chat)
{
    GossipGroupChatPriv  *priv;
    GossipChatroomInvite *invite;
    GossipContact        *sender;
    GossipContact        *own_contact;
    GossipLogManager     *log_manager;
    GossipTime            timestamp;
    gboolean              is_incoming;
    gboolean              is_backlog;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    timestamp = gossip_message_get_timestamp (message);
    is_backlog = timestamp < priv->time_joined;

    sender = gossip_message_get_sender (message);
    own_contact = gossip_chatroom_get_own_contact (priv->chatroom);

    is_incoming = !gossip_contact_equal (sender, own_contact);

    gossip_debug (DEBUG_DOMAIN, 
                  "[%d] New message with timestamp:%d, message %s backlog, %s incoming", 
                  id, timestamp, 
                  is_backlog ? "IS" : "IS NOT", 
                  is_incoming ? "IS" : "IS NOT");

    if (is_backlog) {
        gossip_chat_view_allow_scroll (GOSSIP_CHAT (chat)->view, FALSE);
    }

    invite = gossip_message_get_invite (message);
    if (invite) {
        gossip_chat_view_append_invite (GOSSIP_CHAT (chat)->view,
                                        message);
    } else {
        if (is_incoming) {
            gossip_chat_view_append_message_from_other (
                GOSSIP_CHAT (chat)->view,
                message,
                own_contact,
                NULL);
        } else {
            gossip_chat_view_append_message_from_self (
                GOSSIP_CHAT (chat)->view,
                message,
                own_contact,
                NULL);
        }
    }

    /* Play sound? */
    if (!is_backlog && 
        gossip_chat_should_play_sound (GOSSIP_CHAT (chat)) &&
        gossip_chat_should_highlight_nick (message, own_contact)) {
        gossip_sound_play (GOSSIP_SOUND_CHAT);
    }

    /* Log message? */
    if (!is_backlog) {
        log_manager = gossip_session_get_log_manager (gossip_app_get_session ());
        gossip_log_message_for_chatroom (log_manager, priv->chatroom, message, is_incoming);
    }

    /* Re-enable scrolling? */
    if (is_backlog) {
        gossip_chat_view_allow_scroll (GOSSIP_CHAT (chat)->view, TRUE);
        group_chat_scroll_down_when_idle (chat);
    }

    g_signal_emit_by_name (chat, "new-message", message, is_backlog);
}

static void
group_chat_new_event_cb (GossipChatroomProvider *provider,
                         gint                    id,
                         const gchar            *event,
                         GossipGroupChat        *chat)
{
    GossipGroupChatPriv *priv;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    gossip_debug (DEBUG_DOMAIN, "[%d] New event:'%s'", id, event);

    group_chat_set_scrolling_for_events (chat, TRUE);
    gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, event);
    group_chat_set_scrolling_for_events (chat, FALSE);
}

static void
group_chat_subject_changed_cb (GossipChatroomProvider *provider,
                               gint                    id,
                               GossipContact          *who,
                               const gchar            *new_subject,
                               GossipGroupChat        *chat)
{
    GossipGroupChatPriv *priv;
    gchar               *event;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    if (who) {
        gossip_debug (DEBUG_DOMAIN, 
                      "[%d] Subject changed by:'%s' to:'%s'",
                      id, 
                      gossip_contact_get_id (who),
                      new_subject);

        event = g_strdup_printf (_("%s has set the subject: %s"),
                                 gossip_contact_get_name (who),
                                 new_subject);
    } else {
        gossip_debug (DEBUG_DOMAIN, 
                      "[%d] Subject set as:'%s'",
                      id, 
                      new_subject);

        event = g_strdup_printf (_("Subject is set as: %s"),
                                 new_subject);
    }

    g_free (priv->subject);
    priv->subject = g_strdup (new_subject);
        
    gtk_label_set_text (GTK_LABEL (priv->label_subject), new_subject);

    group_chat_set_scrolling_for_events (chat, TRUE);
    gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, event);
    group_chat_set_scrolling_for_events (chat, FALSE);

    g_free (event);

    g_signal_emit_by_name (chat, "status-changed");
}

static void     
group_chat_error_cb (GossipChatroomProvider *provider,
                     GossipChatroomId        id,
                     GossipChatroomError     error,
                     GossipGroupChat        *chat)
{
    GossipGroupChatPriv *priv;
    const gchar         *error_str;

    priv = GET_PRIV (chat);

    if (id != gossip_chatroom_get_id (priv->chatroom)) {
        return;
    }

    error_str = gossip_chatroom_error_to_string (error);

    gossip_debug (DEBUG_DOMAIN, "[%d] Error:%d->'%s'", 
                  id, error, error_str);

    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), TRUE);
    gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, error_str);
    group_chat_set_scrolling_for_events (GOSSIP_GROUP_CHAT (chat), FALSE);
}

static void
group_chat_dialog_entry_activate_cb (GtkWidget *entry,
                                     GtkDialog *dialog)
{
    gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static void
group_chat_dialog_response_cb (GtkWidget       *dialog,
                               gint             response,                             
                               GossipGroupChat *chat)
{
    if (response == GTK_RESPONSE_OK) {
        GtkWidget   *entry;
        const gchar *str;

        entry = g_object_get_data (G_OBJECT (dialog), "entry");
        str = gtk_entry_get_text (GTK_ENTRY (entry));
                
        if (!G_STR_EMPTY (str)) {
            const gchar *purpose;
        
            purpose = g_object_get_data (G_OBJECT (dialog), "action");
            if (purpose) { 
                GossipGroupChatPriv *priv;

                priv = GET_PRIV (chat);

                if (strcmp (purpose, "change-subject") == 0) {
                    gossip_chatroom_provider_change_subject (priv->chatroom_provider,
                                                             gossip_chatroom_get_id (priv->chatroom),
                                                             str);
                }
                else if (strcmp (purpose, "change-nick") == 0) {
                    gossip_chatroom_provider_change_nick (priv->chatroom_provider,
                                                          gossip_chatroom_get_id (priv->chatroom),
                                                          str);
                }
            } else {
                g_warning ("Action for group chat dialog unknown, doing nothing");
            }
        }
    }

    gtk_widget_destroy (dialog);
}

static void
group_chat_dialog_new (GossipGroupChat *chat,
                       const gchar     *action,
                       const gchar     *message, 
                       const gchar     *pretext)
{
    GossipGroupChatPriv *priv;
    GossipChatWindow    *chat_window;
    GossipContact       *contact;
    GtkWidget           *chat_dialog;
    GtkWidget           *dialog;
    GtkWidget           *entry;
    GtkWidget           *hbox;

    g_return_if_fail (!G_STR_EMPTY (action));
    g_return_if_fail (!G_STR_EMPTY (message));

    priv = GET_PRIV (chat);

    contact = gossip_group_chat_get_selected_contact (chat);

    chat_window = gossip_chat_get_window (GOSSIP_CHAT (chat));
    chat_dialog = gossip_chat_window_get_dialog (chat_window);

    dialog = gtk_message_dialog_new (GTK_WINDOW (chat_dialog),
                                     0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_OK_CANCEL,
                                     NULL);

    gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog),
                                   message);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                        hbox, FALSE, TRUE, 4);

    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry), pretext ? pretext : ""); 
    gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
                    
    gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 4);

    g_object_set_data (G_OBJECT (dialog), "entry", entry);
    g_object_set_data_full (G_OBJECT (dialog), "action", g_strdup (action), g_free);

    if (contact) {
        g_object_set_data_full (G_OBJECT (dialog), "contact", 
                                g_object_ref (contact), 
                                g_object_unref);
    }

    g_signal_connect (entry, "activate",
                      G_CALLBACK (group_chat_dialog_entry_activate_cb),
                      dialog);
    g_signal_connect (dialog, "response",
                      G_CALLBACK (group_chat_dialog_response_cb),
                      chat);

    gtk_widget_show_all (dialog);
}

GossipContact *
gossip_group_chat_get_selected_contact (GossipGroupChat *group_chat)
{
    GossipGroupChatPriv *priv;
    GtkTreeSelection    *selection;
    GtkTreeIter          iter;
    GtkTreeModel        *model;
    GossipContact       *contact;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), NULL);

    priv = GET_PRIV (group_chat);

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
    if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
        return NULL;
    }

    gtk_tree_model_get (model, &iter, COL_CONTACT, &contact, -1);

    return contact;
}

static void
group_chat_cl_row_activated_cb (GtkTreeView       *view,
                                GtkTreePath       *path,
                                GtkTreeViewColumn *col,
                                GossipGroupChat   *chat)
{
    GossipGroupChatPriv *priv;
    GossipContact       *own_contact;
    GossipContact       *contact;
    GtkTreeModel        *model;
    GtkTreeIter          iter;

    if (gtk_tree_path_get_depth (path) == 1) {
        /* Do nothing for role groups */
        return;
    }

    model = gtk_tree_view_get_model (view);

    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model,
                        &iter,
                        COL_CONTACT, &contact,
                        -1);

    priv = GET_PRIV (chat);
    own_contact = gossip_chatroom_get_own_contact (priv->chatroom);

    if (!gossip_contact_equal (own_contact, contact)) {
        group_chat_private_chat_new (chat, contact);
    }

    g_object_unref (contact);
}

static gint
group_chat_cl_sort_func (GtkTreeModel *model,
                         GtkTreeIter  *iter_a,
                         GtkTreeIter  *iter_b,
                         gpointer      user_data)
{
    gboolean is_header;

    gtk_tree_model_get (model, iter_a,
                        COL_IS_HEADER, &is_header,
                        -1);

    if (is_header) {
        GossipChatroomRole role_a, role_b;

        gtk_tree_model_get (model, iter_a,
                            COL_HEADER_ROLE, &role_a,
                            -1);
        gtk_tree_model_get (model, iter_b,
                            COL_HEADER_ROLE, &role_b,
                            -1);

        return role_a - role_b;
    } else {
        GossipContact *contact_a, *contact_b;
        const gchar   *name_a, *name_b;
        gint           ret_val;

        gtk_tree_model_get (model, iter_a,
                            COL_CONTACT, &contact_a,
                            -1);

        gtk_tree_model_get (model, iter_b,
                            COL_CONTACT, &contact_b,
                            -1);

        name_a = gossip_contact_get_name (contact_a);
        name_b = gossip_contact_get_name (contact_b);

        ret_val = g_ascii_strcasecmp (name_a, name_b);

        g_object_unref (contact_a);
        g_object_unref (contact_b);

        return ret_val;
    }
}

static gboolean
group_chat_cl_button_press_event_cb (GtkTreeView     *view,
                                     GdkEventButton  *event,
                                     GossipGroupChat *chat)
{
    GtkWidget        *menu;
    GtkTreePath      *path;
    GtkTreeSelection *selection;
    gboolean          row_exists;
        
    if (event->button != 3) {
        return FALSE;
    }
        
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
        
    gtk_widget_grab_focus (GTK_WIDGET (view));
        
    row_exists = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (view),
                                                event->x, event->y,
                                                &path,
                                                NULL, NULL, NULL);
    if (!row_exists) {
        return FALSE;
    }
        
    gtk_tree_selection_unselect_all (selection);
    gtk_tree_selection_select_path (selection, path);
        
    menu = gossip_group_chat_contact_menu (chat);
                
    if (menu) {
        g_object_ref (menu);
        g_object_ref_sink (menu);
        g_object_unref (menu);
        gtk_widget_show (menu);
        gtk_menu_popup (GTK_MENU (menu),
                        NULL, NULL, NULL, NULL,
                        event->button, event->time);
    }
        
    return TRUE;
}

static void
group_chat_cl_setup (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkTreeView         *treeview;
    GtkTreeStore        *store;
    GtkCellRenderer     *cell;
    GtkTreeViewColumn   *col;

    priv = GET_PRIV (chat);

    treeview = GTK_TREE_VIEW (priv->treeview);

    g_object_set (treeview,
                  "show-expanders", FALSE,
                  "reorderable", FALSE,
                  "headers-visible", FALSE,
                  NULL);

    store = gtk_tree_store_new (NUMBER_OF_COLS,
                                GDK_TYPE_PIXBUF,
                                G_TYPE_STRING,
                                G_TYPE_OBJECT,
                                G_TYPE_BOOLEAN,
                                G_TYPE_INT);

    gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (store),
                                             group_chat_cl_sort_func,
                                             chat,
                                             NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                          GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                          GTK_SORT_ASCENDING);

    gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (store));

    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

    cell = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_column_pack_start (col, cell, FALSE);

    gtk_tree_view_column_set_cell_data_func (col, cell,
                                             (GtkTreeCellDataFunc) 
                                             group_chat_cl_pixbuf_cell_data_func,
                                             chat, NULL);

    g_object_set (cell,
                  "xpad", 5,
                  "ypad", 1,
                  "visible", FALSE,
                  NULL);

    cell = gossip_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, cell, TRUE);
    gtk_tree_view_column_set_cell_data_func (col, cell,
                                             (GtkTreeCellDataFunc) 
                                             group_chat_cl_text_cell_data_func,
                                             chat, NULL);
    gtk_tree_view_column_add_attribute (col, cell,
                                        "name", COL_NAME);

    gtk_tree_view_column_add_attribute (col, cell,
                                        "is_group", COL_IS_HEADER);

    cell = gossip_cell_renderer_expander_new ();
    gtk_tree_view_column_pack_end (col, cell, FALSE);
    gtk_tree_view_column_set_cell_data_func (col, cell,
                                             (GtkTreeCellDataFunc) 
                                             group_chat_cl_expander_cell_data_func,
                                             chat, NULL);

    gtk_tree_view_append_column (treeview, col);

    g_signal_connect (treeview, "row-activated",
                      G_CALLBACK (group_chat_cl_row_activated_cb),
                      chat);
    g_signal_connect (treeview, "button-press-event",
                      G_CALLBACK (group_chat_cl_button_press_event_cb),
                      chat);
}

static gboolean
group_chat_cl_find_foreach (GtkTreeModel    *model,
                            GtkTreePath     *path,
                            GtkTreeIter     *iter,
                            ContactFindData *data)
{
    GossipContact *contact;
    gboolean       equal;

    if (gtk_tree_path_get_depth (path) == 1) {
        /* No contacts on depth 1 */
        return FALSE;
    }

    gtk_tree_model_get (model, iter, COL_CONTACT, &contact, -1);

    equal = gossip_contact_equal (data->contact, contact);
    g_object_unref (contact);

    if (equal) {
        data->found = TRUE;
        data->found_iter = *iter;
    }

    return equal;
}

static gboolean
group_chat_cl_find (GossipGroupChat *chat,
                    GossipContact   *contact,
                    GtkTreeIter     *iter)
{
    GossipGroupChatPriv *priv;
    ContactFindData      data;
    GtkTreeModel        *model;

    priv = GET_PRIV (chat);

    data.found = FALSE;
    data.contact = contact;
    data.chat = chat;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

    gtk_tree_model_foreach (model,
                            (GtkTreeModelForeachFunc) group_chat_cl_find_foreach,
                            &data);

    if (data.found) {
        *iter = data.found_iter;
    }

    return data.found;
}

static void
group_chat_cl_pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
                                     GtkCellRenderer   *cell,
                                     GtkTreeModel      *model,
                                     GtkTreeIter       *iter,
                                     GossipGroupChat   *chat)
{
    GdkPixbuf *pixbuf;
    gboolean   is_header;

    gtk_tree_model_get (model, iter,
                        COL_IS_HEADER, &is_header,
                        COL_STATUS, &pixbuf,
                        -1);

    g_object_set (cell,
                  "visible", !is_header,
                  "pixbuf", pixbuf,
                  NULL);

    if (pixbuf) {
        g_object_unref (pixbuf);
    }

    group_chat_cl_set_background (chat, cell, is_header);
}

static void
group_chat_cl_text_cell_data_func (GtkTreeViewColumn *tree_column,
                                   GtkCellRenderer   *cell,
                                   GtkTreeModel      *model,
                                   GtkTreeIter       *iter,
                                   GossipGroupChat   *chat)
{
    gboolean is_header;

    gtk_tree_model_get (model, iter,
                        COL_IS_HEADER, &is_header,
                        -1);

    if (is_header) {
        GossipChatroomRole role;
        gint               nr;

        gtk_tree_model_get (model, iter,
                            COL_HEADER_ROLE, &role,
                            -1);

        nr = gtk_tree_model_iter_n_children (model, iter);

        g_object_set (cell,
                      "name", gossip_chatroom_role_to_string (role, nr),
                      NULL);
    } else {
        gchar *name;

        gtk_tree_model_get (model, iter,
                            COL_NAME, &name,
                            -1);
        g_object_set (cell,
                      "name", name,
                      NULL);

        g_free (name);
    }

    group_chat_cl_set_background (chat, cell, is_header);
}

static void
group_chat_cl_expander_cell_data_func (GtkTreeViewColumn      *column,
                                       GtkCellRenderer        *cell,
                                       GtkTreeModel           *model,
                                       GtkTreeIter            *iter,
                                       GossipGroupChat        *chat)
{
    gboolean is_header;

    gtk_tree_model_get (model, iter,
                        COL_IS_HEADER, &is_header,
                        -1);

    if (is_header) {
        GtkTreePath *path;
        gboolean     row_expanded;

        path = gtk_tree_model_get_path (model, iter);
        row_expanded = gtk_tree_view_row_expanded (GTK_TREE_VIEW (column->tree_view), path);
        gtk_tree_path_free (path);

        g_object_set (cell,
                      "visible", TRUE,
                      "expander-style", row_expanded ? GTK_EXPANDER_EXPANDED : GTK_EXPANDER_COLLAPSED,
                      NULL);
    } else {
        g_object_set (cell, "visible", FALSE, NULL);
    }

    group_chat_cl_set_background (chat, cell, is_header);
}

static void
group_chat_cl_set_background (GossipGroupChat *chat,
                              GtkCellRenderer *cell,
                              gboolean         is_header)
{
    GossipGroupChatPriv *priv;

    priv = GET_PRIV (chat);

    if (is_header) {
        GdkColor  color;
        GtkStyle *style;

        style = gtk_widget_get_style (GTK_WIDGET (priv->treeview));
        color = style->text_aa[GTK_STATE_INSENSITIVE];

        color.red = (color.red + (style->white).red) / 2;
        color.green = (color.green + (style->white).green) / 2;
        color.blue = (color.blue + (style->white).blue) / 2;

        g_object_set (cell,
                      "cell-background-gdk", &color,
                      NULL);
    } else {
        g_object_set (cell,
                      "cell-background-gdk", NULL,
                      NULL);
    }
}

static void
group_chat_contact_joined_cb (GossipChatroom  *chatroom,
                              GossipContact   *contact,
                              GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipContact       *own_contact;

    priv = GET_PRIV (chat);

    gossip_debug (DEBUG_DOMAIN, 
                  "Contact joined:'%s' (refs:%d)",
                  gossip_contact_get_id (contact),
                  G_OBJECT (contact)->ref_count);

    group_chat_contact_add (chat, contact);

    g_signal_connect (contact, "notify::presences",
                      G_CALLBACK (group_chat_contact_presence_updated_cb),
                      chat);
    g_signal_connect (contact, "notify::name",
                      G_CALLBACK (group_chat_contact_updated_cb),
                      chat);

    g_signal_emit_by_name (chat, "contact-added", contact);

    /* Add event to chatroom */
    own_contact = gossip_chatroom_get_own_contact (chatroom);

    if (!gossip_contact_equal (own_contact, contact)) {
        gchar *str;

        str = g_strdup_printf (_("%s has joined the room"),
                               gossip_contact_get_name (contact));
                
        group_chat_set_scrolling_for_events (chat, TRUE);
        gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, str);
        group_chat_set_scrolling_for_events (chat, FALSE);

        g_free (str);
    }
}

static void
group_chat_contact_left_cb (GossipChatroom  *chatroom,
                            GossipContact   *contact,
                            GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GossipContact       *own_contact;

    priv = GET_PRIV (chat);

    gossip_debug (DEBUG_DOMAIN, 
                  "Contact left:'%s' (refs:%d)",
                  gossip_contact_get_id (contact),
                  G_OBJECT (contact)->ref_count);

    g_signal_handlers_disconnect_by_func (contact,
                                          group_chat_contact_updated_cb,
                                          chat);
    g_signal_handlers_disconnect_by_func (contact,
                                          group_chat_contact_presence_updated_cb,
                                          chat);

    group_chat_contact_remove (chat, contact);

    g_signal_emit_by_name (chat, "contact_removed", contact);

    /* Add event to chatroom */
    own_contact = gossip_chatroom_get_own_contact (chatroom);

    if (!gossip_contact_equal (own_contact, contact)) {
        gchar *str;

        str = g_strdup_printf (_("%s has left the room"),
                               gossip_contact_get_name (contact));

        group_chat_set_scrolling_for_events (chat, TRUE);
        gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, str);
        group_chat_set_scrolling_for_events (chat, FALSE);

        g_free (str);
    }
}

static void
group_chat_contact_info_changed_cb (GossipChatroom  *chatroom,
                                    GossipContact   *contact,
                                    GossipGroupChat *chat)
{
    group_chat_contact_remove (chat, contact);
    group_chat_contact_add (chat, contact);
}

static void
group_chat_contact_add (GossipGroupChat *chat,
                        GossipContact   *contact)
{
    GossipGroupChatPriv       *priv;
    GossipChatroomContactInfo *info;
    GossipChatroomRole         role = GOSSIP_CHATROOM_ROLE_NONE;
    GtkTreeModel              *model;
    GtkTreeIter                iter;
    GtkTreeIter                parent;
    GdkPixbuf                 *pixbuf;
    GtkTreePath               *path;

    priv = GET_PRIV (chat);

    pixbuf = gossip_pixbuf_for_contact (contact);

    info = gossip_chatroom_get_contact_info (priv->chatroom, contact);
    if (info) {
        role = info->role;
    }
    group_chat_get_role_iter (chat, role, &parent);

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
    gtk_tree_store_append (GTK_TREE_STORE (model), &iter, &parent);

    gtk_tree_store_set (GTK_TREE_STORE (model),
                        &iter,
                        COL_CONTACT, contact,
                        COL_NAME, gossip_contact_get_name (contact),
                        COL_STATUS, pixbuf,
                        COL_IS_HEADER, FALSE,
                        -1);

    path = gtk_tree_model_get_path (model, &parent);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->treeview), path, TRUE);
    gtk_tree_path_free (path);

    g_object_unref (pixbuf);
}

static void
group_chat_contact_remove (GossipGroupChat *chat,
                           GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GtkTreeIter          iter;

    priv = GET_PRIV (chat);

    if (group_chat_cl_find (chat, contact, &iter)) {
        GtkTreeModel *model;
        GtkTreeIter   parent;

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
        gtk_tree_model_iter_parent (model, &parent, &iter);

        gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);

        if (!gtk_tree_model_iter_has_child (model, &parent)) {
            gtk_tree_store_remove (GTK_TREE_STORE (model), &parent);
        }
    }
}

static void
group_chat_contact_presence_updated_cb (GossipContact   *contact,
                                        GParamSpec      *param,
                                        GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkTreeIter          iter;

    priv = GET_PRIV (chat);

    gossip_debug (DEBUG_DOMAIN, 
                  "Contact Presence Updated:'%s'",
                  gossip_contact_get_id (contact));

    if (group_chat_cl_find (chat, contact, &iter)) {
        GdkPixbuf    *pixbuf;
        GtkTreeModel *model;

        pixbuf = gossip_pixbuf_for_contact (contact);

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
        gtk_tree_store_set (GTK_TREE_STORE (model),
                            &iter,
                            COL_STATUS, pixbuf,
                            -1);

        gdk_pixbuf_unref (pixbuf);
    } else {
        g_signal_handlers_disconnect_by_func (contact,
                                              group_chat_contact_presence_updated_cb,
                                              chat);
    }
}

static void
group_chat_contact_updated_cb (GossipContact   *contact,
                               GParamSpec      *param,
                               GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkTreeIter          iter;

    priv = GET_PRIV (chat);

    if (group_chat_cl_find (chat, contact, &iter)) {
        GtkTreeModel *model;

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
        gtk_tree_store_set (GTK_TREE_STORE (model),
                            &iter,
                            COL_NAME, gossip_contact_get_name (contact),
                            -1);
    } else {
        g_signal_handlers_disconnect_by_func (contact,
                                              group_chat_contact_updated_cb,
                                              chat);
    }
}

static void
group_chat_private_chat_new (GossipGroupChat *chat,
                             GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipPrivateChat   *private_chat;
    GossipChatManager   *chat_manager;
    GtkWidget           *widget;

    priv = GET_PRIV (chat);

    chat_manager = gossip_app_get_chat_manager ();
    gossip_chat_manager_show_chat (chat_manager, contact);

    private_chat = gossip_chat_manager_get_chat (chat_manager, contact);
    priv->private_chats = g_list_prepend (priv->private_chats, private_chat);

    g_object_weak_ref (G_OBJECT (private_chat),
                       (GWeakNotify) group_chat_private_chat_removed,
                       chat);

    /* Set private chat sensitive, since previously we might have
     * already had a private chat with this JID and in this room,
     * and they would have been made insensitive when the last
     * room was closed.
     */
    widget = gossip_chat_get_widget (GOSSIP_CHAT (private_chat));
    gtk_widget_set_sensitive (widget, TRUE);
}

static void
group_chat_private_chat_removed (GossipGroupChat *chat,
                                 GossipChat      *private_chat)
{
    GossipGroupChatPriv *priv;

    priv = GET_PRIV (chat);

    priv->private_chats = g_list_remove (priv->private_chats, private_chat);
}

static void
group_chat_private_chat_stop_foreach (GossipChat      *private_chat,
                                      GossipGroupChat *chat)
{
    GtkWidget *widget;

    widget = gossip_chat_get_widget (private_chat);
    gtk_widget_set_sensitive (widget, FALSE);

    g_object_weak_unref (G_OBJECT (private_chat),
                         (GWeakNotify) group_chat_private_chat_removed,
                         chat);
}

static void
group_chat_send (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkTextBuffer       *buffer;
    GtkTextIter          start, end;
    gchar                   *msg;
    gboolean             handled_command = FALSE;

    priv = GET_PRIV (chat);

    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view));

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    msg = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

    if (G_STR_EMPTY (msg)) {
        g_free (msg);
        return;
    }

    gossip_app_set_not_away ();

    /* Clear the input field. */
    gtk_text_buffer_set_text (buffer, "", -1);

    /* Check for commands */
    if (g_ascii_strncasecmp (msg, "/nick ", 6) == 0 && strlen (msg) > 6) {
        const gchar *nick;

        nick = msg + 6;
        gossip_chatroom_provider_change_nick (priv->chatroom_provider,
                                              gossip_chatroom_get_id (priv->chatroom),
                                              nick);
        handled_command = TRUE;
    }
    else if (g_ascii_strncasecmp (msg, "/kick ", 6) == 0 && strlen (msg) > 6) {
        GList         *contacts, *l;
        GossipContact *contact = NULL;
        const gchar   *nick;

        nick = msg + 6;

        contacts = gossip_chatroom_get_contacts (priv->chatroom);

        for (l = contacts; l && !contact; l = l->next) {
            const gchar *name;
            gchar       *name_caseless;
            gchar       *nick_caseless;
                        
            name = gossip_contact_get_name (l->data);
            name_caseless = g_utf8_casefold (name, -1);
            nick_caseless = g_utf8_casefold (nick, -1);

            if (strcmp (name_caseless, nick_caseless) == 0) {
                contact = l->data;
            }

            g_free (nick_caseless);
            g_free (name_caseless);
        }

        g_list_free (contacts);
                
        if (contact) {
            gossip_group_chat_contact_kick (chat, contact);
        } else {
            gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, 
                                           _("Nick was not recogized"));
        }

        handled_command = TRUE;
    }
    else if (g_ascii_strncasecmp (msg, "/subject ", 9) == 0 && strlen (msg) > 9) {
        const gchar *subject;
                
        subject = msg + 9;
        gossip_chatroom_provider_change_subject (priv->chatroom_provider,
                                                 gossip_chatroom_get_id (priv->chatroom),
                                                 subject);
        handled_command = TRUE;
    }
    else if (g_ascii_strncasecmp (msg, "/topic ", 7) == 0 && strlen (msg) > 7) {
        const gchar *subject;
                
        subject = msg + 7;
        gossip_chatroom_provider_change_subject (priv->chatroom_provider,
                                                 gossip_chatroom_get_id (priv->chatroom),
                                                 subject);
        handled_command = TRUE;
    }
    else if (g_ascii_strcasecmp (msg, "/clear") == 0) {
        GtkTextBuffer *buffer;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->view));
        gtk_text_buffer_set_text (buffer, "", -1);

        handled_command = TRUE;
    }

    if (!handled_command) {
        gossip_chatroom_provider_send (priv->chatroom_provider, 
                                       gossip_chatroom_get_id (priv->chatroom), 
                                       msg);
                
        gossip_chat_sent_message_add (GOSSIP_CHAT (chat), msg);

        gossip_app_set_not_away ();
    }

    g_free (msg);

    GOSSIP_CHAT (chat)->is_first_char = TRUE;
}

static gboolean
group_chat_get_nick_list_foreach (GtkTreeModel  *model,
                                  GtkTreePath   *path,
                                  GtkTreeIter   *iter,
                                  GList        **list)
{
    gchar *name;

    if (gtk_tree_path_get_depth (path) == 1) {
        /* No contacts on depth 1 */
        return FALSE;
    }

    gtk_tree_model_get (model,
                        iter,
                        COL_NAME, &name,
                        -1);

    *list = g_list_prepend (*list, name);

    return FALSE;
}

static GList *
group_chat_get_nick_list (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;
    GtkTreeModel        *model;
    GList               *list = NULL;

    priv = GET_PRIV (chat);

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

    gtk_tree_model_foreach (model,
                            (GtkTreeModelForeachFunc) group_chat_get_nick_list_foreach,
                            &list);

    return list;
}

static GtkWidget *
group_chat_get_widget (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    return priv->widget;
}

static const gchar *
group_chat_get_name (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    return gossip_chatroom_get_name (priv->chatroom);
}

static gchar *
group_chat_get_tooltip (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    if (priv->subject) {
        gchar *subject;
        gchar *tooltip;

        subject = g_strdup_printf (_("Subject: %s"), priv->subject);
        tooltip = g_strdup_printf ("%s\n%s", 
                                   gossip_chatroom_get_name (priv->chatroom), 
                                   subject);
        g_free (subject);

        return tooltip;
    }

    return g_strdup (gossip_chatroom_get_name (priv->chatroom));
}

static GdkPixbuf *
group_chat_get_status_pixbuf (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;
    GdkPixbuf           *pixbuf = NULL;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    if (priv->chatroom) {
        pixbuf = gossip_pixbuf_for_chatroom_status (priv->chatroom, GTK_ICON_SIZE_MENU);
    }

    if (!pixbuf) {
        pixbuf = gossip_stock_create_pixbuf (gossip_app_get_window (),
                                             GOSSIP_STOCK_GROUP_MESSAGE,
                                             GTK_ICON_SIZE_MENU);
    }

    return pixbuf;
}

static GossipContact *
group_chat_get_own_contact (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    return gossip_chatroom_get_own_contact (priv->chatroom);
}

static GossipChatroom *
group_chat_get_chatroom (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    return priv->chatroom;
}

static gboolean
group_chat_get_show_contacts (GossipChat *chat)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), FALSE);

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    return priv->contacts_visible;
}

static void
group_chat_set_show_contacts (GossipChat *chat,
                              gboolean    show)
{
    GossipGroupChat     *group_chat;
    GossipGroupChatPriv *priv;

    g_return_if_fail (GOSSIP_IS_GROUP_CHAT (chat));

    group_chat = GOSSIP_GROUP_CHAT (chat);
    priv = GET_PRIV (group_chat);

    priv->contacts_visible = show;

    if (show) {
        gtk_widget_show (priv->scrolled_window_contacts);
        gtk_paned_set_position (GTK_PANED (priv->hpaned),
                                priv->contacts_width);
    } else {
        priv->contacts_width = gtk_paned_get_position (GTK_PANED (priv->hpaned));
        gtk_widget_hide (priv->scrolled_window_contacts);
    }
}

static gboolean
group_chat_is_group_chat (GossipChat *chat)
{
    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), FALSE);

    return TRUE;
}

static gboolean
group_chat_is_connected (GossipChat *chat)
{
    GossipGroupChatPriv  *priv;
    GossipChatroomStatus  status;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), FALSE);
        
    priv = GET_PRIV (chat);
        
    if (!priv->chatroom) {
        return FALSE;
    }

    status = gossip_chatroom_get_status (priv->chatroom);

    return status == GOSSIP_CHATROOM_STATUS_ACTIVE;
}

static gboolean
group_chat_get_role_iter_foreach (GtkTreeModel     *model,
                                  GtkTreePath      *path,
                                  GtkTreeIter      *iter,
                                  FindRoleIterData *fr)
{
    GossipChatroomRole role;
    gboolean           is_header;

    /* Headers are only at the top level. */
    if (gtk_tree_path_get_depth (path) != 1) {
        return FALSE;
    }

    gtk_tree_model_get (model, iter,
                        COL_IS_HEADER, &is_header,
                        COL_HEADER_ROLE, &role,
                        -1);

    if (is_header && role == fr->role) {
        fr->iter = *iter;
        fr->found = TRUE;
    }

    return fr->found;
}

static void
group_chat_get_role_iter (GossipGroupChat    *chat,
                          GossipChatroomRole  role,
                          GtkTreeIter        *iter)
{
    GossipGroupChatPriv *priv;
    GtkTreeModel        *model;
    FindRoleIterData     fr;

    priv = GET_PRIV (chat);

    fr.found = FALSE;
    fr.role = role;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
    gtk_tree_model_foreach (model,
                            (GtkTreeModelForeachFunc) group_chat_get_role_iter_foreach,
                            &fr);

    if (!fr.found) {
        gtk_tree_store_append (GTK_TREE_STORE (model), iter, NULL);
        gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                            COL_IS_HEADER, TRUE,
                            COL_HEADER_ROLE, role,
                            -1);
    } else {
        *iter = fr.iter;
    }
}

GossipGroupChat *
gossip_group_chat_new (GossipChatroomProvider *provider,
                       GossipChatroom         *chatroom)
{
    GossipGroupChat     *chat;
    GossipGroupChatPriv *priv;
    GossipChatroomId     id;

    g_return_val_if_fail (GOSSIP_IS_CHATROOM_PROVIDER (provider), NULL);
    g_return_val_if_fail (GOSSIP_IS_CHATROOM (chatroom), NULL);

    if (!group_chats) {
        group_chats = g_hash_table_new_full (NULL,
                                             NULL,
                                             NULL,
                                             (GDestroyNotify) g_object_unref);
    }

    /* Check this group chat is not already shown */
    id = gossip_chatroom_get_id (chatroom);
    chat = g_hash_table_lookup (group_chats, GINT_TO_POINTER (id));
    if (chat) {
        gossip_chat_present (GOSSIP_CHAT (chat));
        return chat;
    }

    /* Create new group chat object */
    chat = g_object_new (GOSSIP_TYPE_GROUP_CHAT, NULL);

    priv = GET_PRIV (chat);

    priv->chatroom = g_object_ref (chatroom);
    priv->chatroom_provider = g_object_ref (provider);
    priv->last_status = gossip_chatroom_get_status (chatroom);

    priv->private_chats = NULL;

    g_hash_table_insert (group_chats, GINT_TO_POINTER (id), chat);

    g_signal_connect (chatroom, "notify::name",
                      G_CALLBACK (group_chat_chatroom_name_cb),
                      chat);
    g_signal_connect (chatroom, "notify::status",
                      G_CALLBACK (group_chat_chatroom_status_cb),
                      chat);
    g_signal_connect (provider, "chatroom-kicked",
                      G_CALLBACK (group_chat_kicked_cb),
                      chat);
    g_signal_connect (provider, "chatroom-nick-changed",
                      G_CALLBACK (group_chat_nick_changed_cb),
                      chat);
    g_signal_connect (provider, "chatroom-new-message",
                      G_CALLBACK (group_chat_new_message_cb),
                      chat);
    g_signal_connect (provider, "chatroom-new-event",
                      G_CALLBACK (group_chat_new_event_cb),
                      chat);
    g_signal_connect (provider, "chatroom-subject-changed",
                      G_CALLBACK (group_chat_subject_changed_cb),
                      chat);
    g_signal_connect (provider, "chatroom-error",
                      G_CALLBACK (group_chat_error_cb),
                      chat);
    g_signal_connect (chatroom, "contact-joined",
                      G_CALLBACK (group_chat_contact_joined_cb),
                      chat);
    g_signal_connect (chatroom, "contact-left",
                      G_CALLBACK (group_chat_contact_left_cb),
                      chat);
    g_signal_connect (chatroom, "contact-info-changed",
                      G_CALLBACK (group_chat_contact_info_changed_cb),
                      chat);

    /* Actually join the chat room */
    group_chat_join (chat);

    /* NOTE: We must start the join before here otherwise the
     * chatroom doesn't exist for the provider to find it, and
     * this is needed in the function below, since it calls
     * gossip_chat_get_chatroom() which tries to find the chatroom
     * from the provider by id and if we haven't started the join,
     * it officially doesn't exist according to the backend.
     */
        
    gossip_chat_present (GOSSIP_CHAT (chat)); 

    group_chat_scroll_down_when_idle (chat);

    return chat;
}

GossipChatroomId
gossip_group_chat_get_chatroom_id (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), 0);

    priv = GET_PRIV (chat);
        
    if (!priv->chatroom) {
        return 0;
    }

    return gossip_chatroom_get_id (priv->chatroom);
}

GossipChatroomProvider *
gossip_group_chat_get_chatroom_provider (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    priv = GET_PRIV (chat);

    return priv->chatroom_provider;
}

GossipChatroom * 
gossip_group_chat_get_chatroom (GossipGroupChat *chat)
{
    GossipGroupChatPriv *priv;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

    priv = GET_PRIV (chat);

    return priv->chatroom;
}


GtkWidget *
gossip_group_chat_contact_menu (GossipGroupChat *group_chat)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact;
    GossipContact       *own_contact;
    GtkWidget           *menu = NULL;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), NULL);

    priv = GET_PRIV (group_chat);

    if (!gossip_chat_is_connected (GOSSIP_CHAT (group_chat))) {
        return NULL;
    }

    contact = gossip_group_chat_get_selected_contact (group_chat);
    if (!contact) {
        return NULL;
    }
        
    own_contact = gossip_chatroom_get_own_contact (priv->chatroom);
    if (gossip_contact_equal (contact, own_contact)) {
        g_object_unref (contact);
        return NULL;
    }

    menu = group_chat_cl_menu_create (group_chat, contact);
    g_object_unref (contact);

    return menu;
}

void
gossip_group_chat_change_subject (GossipGroupChat *group_chat)
{
    GossipGroupChatPriv *priv;

    g_return_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat));

    priv = GET_PRIV (group_chat);

    group_chat_dialog_new (group_chat, 
                           "change-subject", 
                           _("Enter the new subject you want to set for this room:"),
                           gtk_label_get_text (GTK_LABEL (priv->label_subject)));

}

void
gossip_group_chat_change_nick (GossipGroupChat *group_chat)
{
    GossipGroupChatPriv *priv;

    g_return_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat));

    priv = GET_PRIV (group_chat);

    group_chat_dialog_new (group_chat, 
                           "change-nick", 
                           _("Enter the new nickname you want to be known by:"),
                           gossip_chatroom_get_nick (priv->chatroom));
}

void
gossip_group_chat_contact_kick (GossipGroupChat *group_chat,
                                GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact_to_kick;
    gchar               *str;

    g_return_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat));

    priv = GET_PRIV (group_chat);

    if (contact) {
        contact_to_kick = contact;
    } else {
        contact_to_kick = gossip_group_chat_get_selected_contact (group_chat);
    }

    g_return_if_fail (contact_to_kick != NULL);

    str = g_strdup_printf (_("Kicking %s"), 
                           gossip_contact_get_name (contact_to_kick)); 
    gossip_chat_view_append_event (GOSSIP_CHAT (group_chat)->view, str);
    g_free (str);

    gossip_chatroom_provider_kick (priv->chatroom_provider,
                                   gossip_chatroom_get_id (priv->chatroom),
                                   contact_to_kick,
                                   _("You have been removed from the "
                                     "room by an administrator."));
}

gboolean
gossip_group_chat_contact_can_message_all (GossipGroupChat *group_chat,
                                           GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact_to_use;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), FALSE);

    priv = GET_PRIV (group_chat);

    if (contact) {
        contact_to_use = contact;
    } else {
        contact_to_use = gossip_group_chat_get_selected_contact (group_chat);
    }

    return gossip_chatroom_contact_can_message_all (priv->chatroom, contact_to_use);
}

gboolean
gossip_group_chat_contact_can_change_subject (GossipGroupChat *group_chat,
                                              GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact_to_use;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), FALSE);

    priv = GET_PRIV (group_chat);

    if (contact) {
        contact_to_use = contact;
    } else {
        contact_to_use = gossip_group_chat_get_selected_contact (group_chat);
    }

    return gossip_chatroom_contact_can_change_subject (priv->chatroom, contact_to_use);
}

gboolean
gossip_group_chat_contact_can_kick (GossipGroupChat *group_chat,
                                    GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact_to_use;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), FALSE);

    priv = GET_PRIV (group_chat);

    if (contact) {
        contact_to_use = contact;
    } else {
        contact_to_use = gossip_group_chat_get_selected_contact (group_chat);
    }

    return gossip_chatroom_contact_can_kick (priv->chatroom, contact_to_use);
}

gboolean
gossip_group_chat_contact_can_change_role (GossipGroupChat *group_chat,
                                           GossipContact   *contact)
{
    GossipGroupChatPriv *priv;
    GossipContact       *contact_to_use;

    g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (group_chat), FALSE);

    priv = GET_PRIV (group_chat);

    if (contact) {
        contact_to_use = contact;
    } else {
        contact_to_use = gossip_group_chat_get_selected_contact (group_chat);
    }

    return gossip_chatroom_contact_can_change_role (priv->chatroom, contact_to_use);
}


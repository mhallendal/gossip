/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2005 Imendio AB
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
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>

#include <libgossip/gossip-chatroom-provider.h>
#include <libgossip/gossip-message.h>

#include "gossip-app.h"
#include "gossip-cell-renderer-text.h"
#include "gossip-chat.h"
#include "gossip-chat-invite.h"
#include "gossip-chat-view.h"
#include "gossip-contact-list-iface.h"
#include "gossip-group-chat.h"
#include "gossip-private-chat.h"
#include "gossip-sound.h"
#include "gossip-stock.h"
#include "gossip-ui-utils.h"

#define IS_ENTER(v) (v == GDK_Return || v == GDK_ISO_Enter || v == GDK_KP_Enter)


struct _GossipGroupChatPriv {
	GossipChatroomProvider *provider;
	GossipChatroomId        room_id;

	GtkWidget              *widget;
	GtkWidget              *textview_sw;
	GtkWidget              *topic_entry;
	GtkWidget              *treeview;
	GtkWidget              *contacts_sw;
	GtkWidget              *hpaned;

	gchar                  *name;

	GCompletion            *completion;

	GossipContact          *own_contact;
	GHashTable             *contacts;
	GList                  *private_chats;

	gint                    contacts_width;
	gboolean                contacts_visible;
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


static void           group_chat_contact_list_iface_init     (GossipContactListIfaceClass  *iface);
static void           group_chat_finalize                    (GObject                      *object);
static void           group_chats_init                       (void);
static gboolean       group_chat_key_press_event_cb          (GtkWidget                    *widget,
							      GdkEventKey                  *event,
							      GossipGroupChat              *chat);
static gboolean       group_chat_focus_in_event_cb           (GtkWidget                    *widget,
							      GdkEvent                     *event,
							      GossipGroupChat              *chat);
static void           group_chat_drag_data_received          (GtkWidget                    *widget,
							      GdkDragContext               *context,
							      int                           x,
							      int                           y,
							      GtkSelectionData             *selection,
							      guint                         info,
							      guint                         time,
							      GossipGroupChat              *chat);
static void           group_chat_row_activated_cb            (GtkTreeView                  *view,
							      GtkTreePath                  *path,
							      GtkTreeViewColumn            *col,
							      GossipGroupChat              *chat);
static void           group_chat_widget_destroy_cb           (GtkWidget                    *widget,
							      GossipGroupChat              *chat);
static gint           group_chat_contacts_sort_func          (GtkTreeModel                 *model,
							      GtkTreeIter                  *iter_a,
							      GtkTreeIter                  *iter_b,
							      gpointer                      user_data);
static void           group_chat_contacts_setup              (GossipGroupChat              *chat);
static gboolean       group_chat_contact_find_foreach        (GtkTreeModel                 *model,
							      GtkTreePath                  *path,
							      GtkTreeIter                  *iter,
							      ContactFindData              *data);
static gboolean       group_chat_contact_find                (GossipGroupChat              *chat,
							      GossipContact                *contact,
							      GtkTreeIter                  *iter);
static gint           group_chat_contact_completion_func     (const gchar                  *s1,
							      const gchar                  *s2,
							      gsize                         n);
static void           group_chat_connected_cb                (GossipSession                *session,
							      GossipGroupChat              *chat);
static void           group_chat_disconnected_cb             (GossipSession                *session,
							      GossipGroupChat              *chat);
static void           group_chat_create_gui                  (GossipGroupChat              *chat);
static void           group_chat_new_message_cb              (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipMessage                *message,
							      GossipGroupChat              *chat);
static void           group_chat_new_room_event_cb           (GossipChatroomProvider       *provider,
							      gint                          id,
							      const gchar                  *event,
							      GossipGroupChat              *chat);
static void           group_chat_topic_changed_cb            (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipContact                *who,
							      const gchar                  *new_topic,
							      GossipGroupChat              *chat);
static void           group_chat_contact_joined_cb           (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipContact                *contact,
							      GossipGroupChat              *chat);
static void           group_chat_contact_left_cb             (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipContact                *contact,
							      GossipGroupChat              *chat);
static void           group_chat_contact_presence_updated_cb (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipContact                *contact,
							      GossipGroupChat              *chat);
static void           group_chat_contact_updated_cb          (GossipChatroomProvider       *provider,
							      gint                          id,
							      GossipContact                *contact,
							      GossipGroupChat              *chat);
static void           group_chat_topic_activate_cb           (GtkEntry                     *entry,
							      GossipGroupChat              *chat);
static void           group_chat_private_chat_new            (GossipGroupChat              *chat,
							      GossipContact                *contact);
static void           group_chat_private_chat_removed        (GossipGroupChat              *chat,
							      GossipChat                   *private_chat);
static void           group_chat_private_chat_stop_foreach   (GossipChat                   *private_chat,
							      GossipGroupChat              *chat);
static void           group_chat_send                        (GossipGroupChat              *chat);
static gboolean       group_chat_get_nick_list_foreach       (GtkTreeModel                 *model,
							      GtkTreePath                  *path,
							      GtkTreeIter                  *iter,
							      GList                       **list);
static GList *        group_chat_get_nick_list               (GossipGroupChat              *chat);
static GtkWidget *    group_chat_get_widget                  (GossipChat                   *chat);
static const gchar *  group_chat_get_name                    (GossipChat                   *chat);
static gchar *        group_chat_get_tooltip                 (GossipChat                   *chat);
static GdkPixbuf *    group_chat_get_status_pixbuf           (GossipChat                   *chat);
static GossipContact *group_chat_get_contact                 (GossipChat                   *chat);
static GossipContact *group_chat_get_own_contact             (GossipChat                   *chat);
static void           group_chat_get_geometry                (GossipChat                   *chat,
							      gint                         *width,
							      gint                         *height);
static gboolean       group_chat_get_group_chat              (GossipChat                   *chat);
static gboolean       group_chat_get_show_contacts           (GossipChat                   *chat);
static void           group_chat_set_show_contacts           (GossipChat                   *chat,
							      gboolean                      show);



enum {
	COL_STATUS,
	COL_NAME,
	COL_CONTACT,
	NUMBER_OF_COLS
};


enum DndDragType {
	DND_DRAG_TYPE_CONTACT_ID,
};


static GtkTargetEntry drop_types[] = {
	{ "text/contact-id", 0, DND_DRAG_TYPE_CONTACT_ID },
};


G_DEFINE_TYPE_WITH_CODE (GossipGroupChat, gossip_group_chat, 
			 GOSSIP_TYPE_CHAT,
			 G_IMPLEMENT_INTERFACE (GOSSIP_TYPE_CONTACT_LIST_IFACE,
						group_chat_contact_list_iface_init));


static GHashTable *group_chats = NULL;


static void
gossip_group_chat_class_init (GossipGroupChatClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
	GossipChatClass *chat_class   = GOSSIP_CHAT_CLASS (klass);
                                                                                
        object_class->finalize = group_chat_finalize;

	chat_class->get_name          = group_chat_get_name;
	chat_class->get_tooltip       = group_chat_get_tooltip;
	chat_class->get_status_pixbuf = group_chat_get_status_pixbuf;
	chat_class->get_contact       = group_chat_get_contact;
	chat_class->get_own_contact   = group_chat_get_own_contact;
	chat_class->get_geometry      = group_chat_get_geometry;
	chat_class->get_widget        = group_chat_get_widget;
	chat_class->get_group_chat    = group_chat_get_group_chat;
	chat_class->get_show_contacts = group_chat_get_show_contacts;
	chat_class->set_show_contacts = group_chat_set_show_contacts;
}

static void
gossip_group_chat_init (GossipGroupChat *chat)
{
        GossipGroupChatPriv *priv;
                                                                                
        priv = g_new0 (GossipGroupChatPriv, 1);
                                                                                
        chat->priv = priv;

	priv->contacts_visible = TRUE;
}

static void
group_chat_contact_list_iface_init (GossipContactListIfaceClass *iface)
{
	/* No functions for now */
}

static void
group_chat_finalize (GObject *object)
{
	GossipGroupChat     *chat;
	GossipGroupChatPriv *priv;

	chat = GOSSIP_GROUP_CHAT (object);
	priv = chat->priv;
	
	g_signal_handlers_disconnect_by_func (priv->provider, 
					      group_chat_new_message_cb, chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_new_room_event_cb, 
					      chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_topic_changed_cb, 
					      chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_contact_joined_cb,
					      chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_contact_left_cb,
					      chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_contact_presence_updated_cb,
					      chat);
	g_signal_handlers_disconnect_by_func (priv->provider,
					      group_chat_contact_updated_cb,
					      chat);
	
	g_free (priv->name);

	g_list_foreach (priv->private_chats, 
			(GFunc)group_chat_private_chat_stop_foreach, 
			chat);
	g_list_free (priv->private_chats);

	if (priv->own_contact) {
		g_object_unref (priv->own_contact);
	}
	
	g_free (priv);
}

static void
group_chats_init (void)
{
	static gboolean inited = FALSE;

	if (inited) {
		return;
	}
	
	inited = TRUE;
	
	group_chats = g_hash_table_new_full (NULL, 
					     NULL,
					     NULL,
					     (GDestroyNotify) g_object_unref);
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

	priv = chat->priv;
	
	/* Catch enter but not ctrl/shift-enter */
	if (IS_ENTER (event->keyval) && !(event->state & GDK_SHIFT_MASK)) {
		/* This is to make sure that kinput2 gets the enter. And if
                 * it's handled there we shouldn't send on it. This is because
                 * kinput2 uses Enter to commit letters. See:
                 * http://bugzilla.redhat.com/bugzilla/show_bug.cgi?id=104299
                 */
		if (gtk_im_context_filter_keypress (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view)->im_context, event)) {
			GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view)->need_im_reset = TRUE;
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
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->textview_sw));
		gtk_adjustment_set_value (adj, adj->value - adj->page_size);

		return TRUE;
	}
	else if ((event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK &&
		 event->keyval == GDK_Page_Down) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->textview_sw));
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
	GossipGroupChatPriv *priv;

	priv = chat->priv;
	
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
	GossipGroupChatPriv *priv;
	GossipContact       *contact;
	const gchar         *id;
	gchar               *str;

	priv = chat->priv;

	id = (const gchar*) selection->data;
	g_print ("Received drag & drop contact from roster with id:'%s'\n", id);

	contact = gossip_session_find_contact (gossip_app_get_session (), id);
	
	if (!contact) {
		g_print ("No contact found associated with drag & drop\n");
		return;
	}

	/* send event to chat window */
	str = g_strdup_printf (_("Invited %s to join this chat conference."),
			       gossip_contact_get_id (contact));
 	gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, str);
	g_free (str);

	gossip_chat_invite_dialog (contact, priv->room_id);

	/* clean up dnd */
	gtk_drag_finish (context, TRUE, FALSE, GDK_CURRENT_TIME);
}

static void
group_chat_row_activated_cb (GtkTreeView       *view,
			     GtkTreePath       *path,
			     GtkTreeViewColumn *col,
			     GossipGroupChat   *chat)
{
	GtkTreeModel  *model;
	GtkTreeIter    iter;
	GossipContact *contact;

	model = gtk_tree_view_get_model (view);

	gtk_tree_model_get_iter (model, &iter, path);
	
	gtk_tree_model_get (model,
			    &iter,
			    COL_CONTACT, &contact,
			    -1);

	group_chat_private_chat_new (chat, contact);

	g_object_unref (contact);
}

static void
group_chat_widget_destroy_cb (GtkWidget       *widget,
			      GossipGroupChat *chat)
{
	GossipGroupChatPriv *priv;

	priv = chat->priv;
	
	/* disconnect signals */
	g_signal_handlers_disconnect_by_func (gossip_app_get_session (),
					      group_chat_connected_cb,
					      chat);

	g_signal_handlers_disconnect_by_func (gossip_app_get_session (),
					      group_chat_disconnected_cb,
					      chat);

	gossip_chatroom_provider_leave (priv->provider, priv->room_id);
	
	g_hash_table_remove (group_chats, GINT_TO_POINTER (priv->room_id));
}

static gint
group_chat_contacts_sort_func (GtkTreeModel *model,
			       GtkTreeIter  *iter_a,
			       GtkTreeIter  *iter_b,
			       gpointer      user_data)
{
	gchar *name_a, *name_b;
	gint   ret_val;
	
	gtk_tree_model_get (model, iter_a, COL_NAME, &name_a, -1);
	gtk_tree_model_get (model, iter_b, COL_NAME, &name_b, -1);
	
	ret_val = g_ascii_strcasecmp (name_a, name_b);

	g_free (name_a);
	g_free (name_b);

	return ret_val;
}

static void
group_chat_contacts_setup (GossipGroupChat *chat)
{
	GossipGroupChatPriv *priv;
	GtkTreeView         *tree;
	GtkTextView         *tv;
	GtkListStore        *store;
	GtkCellRenderer     *cell;	
	GtkTreeViewColumn   *col;

	priv = chat->priv;
	tree = GTK_TREE_VIEW (priv->treeview);
	tv = GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->view);

	store = gtk_list_store_new (NUMBER_OF_COLS,
				    GDK_TYPE_PIXBUF,
				    G_TYPE_STRING,
				    G_TYPE_OBJECT);

	gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (store),
						 group_chat_contacts_sort_func,
						 chat,
						 NULL);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
					      GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
					      GTK_SORT_ASCENDING);
	
	gtk_tree_view_set_model (tree, GTK_TREE_MODEL (store));
	
	col = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_add_attribute (col,
					    cell,
					    "pixbuf", COL_STATUS);

	cell = gossip_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, TRUE);
	gtk_tree_view_column_add_attribute (col,
					    cell,
					    "name", COL_NAME);

	gtk_tree_view_append_column (tree, col);

	g_signal_connect (tree,
			  "row_activated",
			  G_CALLBACK (group_chat_row_activated_cb),
			  chat);
}

static gboolean
group_chat_contact_find_foreach (GtkTreeModel    *model,
				 GtkTreePath     *path,
				 GtkTreeIter     *iter,
				 ContactFindData *data)
{
	GossipContact *contact;
	gboolean       equal;

	gtk_tree_model_get (model,
			    iter,
			    COL_CONTACT, &contact,
			    -1);

	equal = gossip_contact_equal (data->contact, contact);
	g_object_unref (contact);

	if (equal) {
		data->found = TRUE;
		data->found_iter = *iter;
	}

	return equal;
}

static gboolean
group_chat_contact_find (GossipGroupChat *chat,
			 GossipContact   *contact, 
			 GtkTreeIter     *iter)
{
	GossipGroupChatPriv *priv;
	ContactFindData      data;
	GtkTreeModel        *model;

	priv = chat->priv;
	
	data.found = FALSE;
	data.contact = contact;
	data.chat = chat;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) group_chat_contact_find_foreach,
				&data);

	if (data.found) {
		*iter = data.found_iter;
	}

	return data.found;
}

static gint
group_chat_contact_completion_func (const gchar *s1, 
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
group_chat_connected_cb (GossipSession   *session, 
			 GossipGroupChat *chat)
{
 	g_return_if_fail (GOSSIP_IS_GROUP_CHAT (chat)); 

 	gtk_widget_set_sensitive (GOSSIP_CHAT (chat)->input_text_view, TRUE); 

 	gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, _("Connected")); 
}

static void
group_chat_disconnected_cb (GossipSession   *session, 
			    GossipGroupChat *chat)
{
 	g_return_if_fail (GOSSIP_IS_GROUP_CHAT (chat)); 

 	gtk_widget_set_sensitive (GOSSIP_CHAT (chat)->input_text_view, FALSE); 

 	gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, _("Disconnected")); 
}

static void
group_chat_create_gui (GossipGroupChat *chat)
{
	GossipGroupChatPriv *priv;
	GladeXML            *glade;
	GtkWidget           *focus_vbox;
	GtkWidget           *input_textview_sw;
	GList               *list;

	priv = chat->priv;
	
	glade = gossip_glade_get_file (GLADEDIR "/group-chat.glade",
				       "group_chat_widget",
				       NULL,
				       "group_chat_widget", &priv->widget,
				       "chat_view_sw", &priv->textview_sw,
				       "input_text_view_sw", &input_textview_sw,
				       "topic_entry", &priv->topic_entry,
				       "treeview", &priv->treeview,
				       "contacts_sw", &priv->contacts_sw,
				       "left_vbox", &focus_vbox,
				       "hpaned", &priv->hpaned,
				       NULL);

	gossip_glade_connect (glade,
			      chat,
			      "group_chat_widget", "destroy", group_chat_widget_destroy_cb,
			      "topic_entry", "activate", group_chat_topic_activate_cb,
			      NULL);

	g_object_set_data (G_OBJECT (priv->widget), "chat", chat);

	gtk_container_add (GTK_CONTAINER (priv->textview_sw), 
			   GTK_WIDGET (GOSSIP_CHAT (chat)->view));
	gtk_widget_show (GTK_WIDGET (GOSSIP_CHAT (chat)->view));

	gtk_container_add (GTK_CONTAINER (input_textview_sw),
			   GOSSIP_CHAT (chat)->input_text_view);
	gtk_widget_show (GOSSIP_CHAT (chat)->input_text_view);

	g_signal_connect (GOSSIP_CHAT (chat)->input_text_view,
			  "key_press_event",
			  G_CALLBACK (group_chat_key_press_event_cb),
			  chat);

	g_signal_connect (GOSSIP_CHAT (chat)->view,
			  "focus_in_event",
			  G_CALLBACK (group_chat_focus_in_event_cb),
			  chat);

	/* drag & drop */ 
	gtk_drag_dest_set (GTK_WIDGET (priv->treeview), 
			   GTK_DEST_DEFAULT_ALL, 
			   drop_types, 
			   G_N_ELEMENTS (drop_types),
			   GDK_ACTION_COPY);
		  
	g_signal_connect (priv->treeview,
			  "drag-data-received",
			  G_CALLBACK (group_chat_drag_data_received),
			  chat);

	g_object_unref (glade);

	list = NULL;
	list = g_list_append (list, GOSSIP_CHAT (chat)->input_text_view);
	list = g_list_append (list, priv->topic_entry);
	
	gtk_container_set_focus_chain (GTK_CONTAINER (focus_vbox), list);
	
	priv->completion = g_completion_new (NULL);
	g_completion_set_compare (priv->completion,
				  group_chat_contact_completion_func);
		
	gtk_widget_grab_focus (GOSSIP_CHAT (chat)->input_text_view);
	group_chat_contacts_setup (chat);
}

static void 
group_chat_new_message_cb (GossipChatroomProvider *provider,
			   gint                    id,
			   GossipMessage          *message,
			   GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	const gchar         *invite;

	priv = chat->priv;

	if (priv->room_id != id) {
		return;
	}

	invite = gossip_message_get_invite (message);
	if (invite) {
		gossip_chat_view_append_invite (GOSSIP_CHAT (chat)->view,
						message);
	} else {
		GossipContact *sender;

		sender = gossip_message_get_sender (message);
		if (gossip_contact_equal (sender, priv->own_contact)) {
			gossip_chat_view_append_message_from_self (
				GOSSIP_CHAT (chat)->view,
				message,
				priv->own_contact);
		} else {
			gossip_chat_view_append_message_from_other (
				GOSSIP_CHAT (chat)->view,
				message,
				priv->own_contact);
		}
	}
	
	if (gossip_chat_should_play_sound (GOSSIP_CHAT (chat)) &&
	    gossip_chat_should_highlight_nick (message, priv->own_contact)) {
		gossip_sound_play (GOSSIP_SOUND_CHAT);
	}

	g_signal_emit_by_name (chat, "new-message");
}

static void 
group_chat_new_room_event_cb (GossipChatroomProvider *provider,
			      gint                    id,
			      const gchar            *event,
			      GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;

	priv = chat->priv;

	if (priv->room_id != id) {
		return;
	}

	gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, event);
}

static void
group_chat_topic_changed_cb (GossipChatroomProvider *provider,
			     gint                    id,
			     GossipContact          *who,
			     const gchar            *new_topic,
			     GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	gchar               *event;
	gchar               *str;

	priv = chat->priv;

	if (priv->room_id != id) {
		return;
	}
	
	gtk_entry_set_text (GTK_ENTRY (priv->topic_entry), new_topic);
	
	str = g_strdup_printf (_("%s has set the topic"),
				 gossip_contact_get_name (who));
	event = g_strconcat (str, ": ", new_topic, NULL);
	g_free (str);

	gossip_chat_view_append_event (GOSSIP_CHAT (chat)->view, event);
	g_free (event);
}

static void
group_chat_contact_joined_cb (GossipChatroomProvider *provider,
			      gint                    id,
			      GossipContact          *contact,
			      GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	GtkTreeModel        *model;
	GtkTreeIter          iter;
	GdkPixbuf           *pixbuf;
		
	priv = chat->priv;
	
	if (priv->room_id != id) {
		return;
	}

	if (gossip_contact_get_type (contact) == GOSSIP_CONTACT_TYPE_USER) {
		if (priv->own_contact) {
			g_object_unref (priv->own_contact);
		}

		priv->own_contact = g_object_ref (contact);
	}
	
	pixbuf = gossip_pixbuf_for_contact (contact);
	
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (model),
			    &iter,
			    COL_CONTACT, contact,
			    COL_NAME, gossip_contact_get_name (contact),
			    COL_STATUS, pixbuf,
			    -1);

	g_object_unref (pixbuf);

	g_signal_emit_by_name (chat, "contact_added", contact);
}

static void
group_chat_contact_left_cb (GossipChatroomProvider *provider,
			    gint                    id,
			    GossipContact          *contact,
			    GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	GtkTreeIter          iter;

	priv = chat->priv;
	
	if (priv->room_id != id) {
		return;
	}

	
	if (group_chat_contact_find (chat, contact, &iter)) {
		GtkTreeModel *model;
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

		g_signal_emit_by_name (chat, "contact_removed", contact);
	}
}

static void
group_chat_contact_presence_updated_cb (GossipChatroomProvider *provider,
					gint                    id,
					GossipContact          *contact,
					GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	GtkTreeIter          iter;

	priv = chat->priv;
	
	if (priv->room_id != id) {
		return;
	}

	if (group_chat_contact_find (chat, contact, &iter)) {
		GdkPixbuf    *pixbuf;
		GtkTreeModel *model;
		
		pixbuf = gossip_pixbuf_for_contact (contact);

		model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
		gtk_list_store_set (GTK_LIST_STORE (model),
				    &iter,
				    COL_STATUS, pixbuf,
				    -1);

		gdk_pixbuf_unref (pixbuf);
		
		g_signal_emit_by_name (chat, "contact_presence_updated",
				       contact);
	}
}

static void
group_chat_contact_updated_cb (GossipChatroomProvider *provider,
			       gint                    id,
			       GossipContact          *contact,
			       GossipGroupChat        *chat)
{
	GossipGroupChatPriv *priv;
	GtkTreeIter          iter;

	priv = chat->priv;
	
	if (priv->room_id != id) {
		return;
	}

	if (group_chat_contact_find (chat, contact, &iter)) {
		GtkTreeModel *model;
		
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
		gtk_list_store_set (GTK_LIST_STORE (model),
				    &iter,
				    COL_NAME, gossip_contact_get_name (contact),
				    -1);

		g_signal_emit_by_name (chat, "contact_updated", contact);
	}
}

static void
group_chat_topic_activate_cb (GtkEntry *entry, 
			      GossipGroupChat *chat)
{
	GossipGroupChatPriv *priv;
	
	priv = chat->priv;
	
	gossip_chatroom_provider_change_topic (priv->provider, 
					       priv->room_id,
					       gtk_entry_get_text (entry));
	
	gtk_widget_grab_focus (GOSSIP_CHAT (chat)->input_text_view);
}

static void
group_chat_private_chat_new (GossipGroupChat *chat,
			     GossipContact   *contact)
{
	GossipGroupChatPriv *priv;
	GossipPrivateChat   *private_chat;
	GossipChatManager   *chat_manager;
	GtkWidget           *widget;

 	priv = chat->priv; 

	chat_manager = gossip_app_get_chat_manager ();
	gossip_chat_manager_show_chat (chat_manager, contact);

	private_chat = gossip_chat_manager_get_chat (chat_manager, contact);
	priv->private_chats = g_list_prepend (priv->private_chats, private_chat);

	g_object_weak_ref (G_OBJECT (private_chat),
			   (GWeakNotify) group_chat_private_chat_removed,
			   chat);

	/* Set private chat sensitive, since previously we might have
	   already had a private chat with this JID and in this room,
	   and they would have been made insensitive when the last
	   room was closed */
	widget = gossip_chat_get_widget (GOSSIP_CHAT (private_chat));
	gtk_widget_set_sensitive (widget, TRUE);
}

static void
group_chat_private_chat_removed (GossipGroupChat *chat, 
				 GossipChat      *private_chat)
{
	GossipGroupChatPriv *priv;

	priv = chat->priv;
	
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
	gchar		    *msg;
	gboolean             handled_command = FALSE;

	priv = chat->priv;
	
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->input_text_view));

	gtk_text_buffer_get_bounds (buffer, &start, &end);
	msg = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	
	/* Clear the input field. */
	gtk_text_buffer_set_text (buffer, "", -1);

	/* Check for commands */
	if (g_ascii_strncasecmp (msg, "/nick ", 6) == 0 && strlen (msg) > 6) {
		const gchar *nick;

		nick = msg + 6;
		gossip_chatroom_provider_change_nick (priv->provider,
						      priv->room_id,
						      nick);
		handled_command = TRUE;
	}
	else if (g_ascii_strncasecmp (msg, "/topic ", 7) == 0 && strlen (msg) > 7) {
		const gchar *topic;

		topic = msg + 7;
		gossip_chatroom_provider_change_topic (priv->provider,
						       priv->room_id,
						       topic);
		handled_command = TRUE;
	}
	else if (g_ascii_strcasecmp (msg, "/clear") == 0) {
		GtkTextBuffer *buffer;

		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (GOSSIP_CHAT (chat)->view));
		gtk_text_buffer_set_text (buffer, "", -1);
		
		handled_command = TRUE;
	}

	if (!handled_command) {
		gossip_chatroom_provider_send (priv->provider, priv->room_id, msg);
		
		gossip_app_force_non_away ();
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
	GList               *list;

	priv = chat->priv;
	list = NULL;
	
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
	
	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) group_chat_get_nick_list_foreach,
				&list);

	return list;
}

static GtkWidget *
group_chat_get_widget (GossipChat *chat)
{
	GossipGroupChat     *g_chat;
	GossipGroupChatPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

	g_chat = GOSSIP_GROUP_CHAT (chat);
	priv   = g_chat->priv;

	return priv->widget;
}

static const gchar *
group_chat_get_name (GossipChat *chat)
{
	GossipGroupChat     *g_chat;
	GossipGroupChatPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

	g_chat = GOSSIP_GROUP_CHAT (chat);
	priv   = g_chat->priv;

	return priv->name;
}

static gchar *
group_chat_get_tooltip (GossipChat *chat)
{
	GossipGroupChat     *g_chat;
	GossipGroupChatPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

	g_chat = GOSSIP_GROUP_CHAT (chat);
	priv   = g_chat->priv;

	return g_strdup (priv->name);
}

static GdkPixbuf *
group_chat_get_status_pixbuf (GossipChat *chat)
{
	static GdkPixbuf *pixbuf = NULL;

	if (pixbuf == NULL) {
		pixbuf = gossip_pixbuf_from_stock (GOSSIP_STOCK_GROUP_MESSAGE,
						   GTK_ICON_SIZE_MENU);
	}

	return pixbuf;
}

static GossipContact *
group_chat_get_contact (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

	return NULL;
}

static GossipContact *
group_chat_get_own_contact (GossipChat *chat)
{
	GossipGroupChat     *g_chat;
	GossipGroupChatPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), NULL);

	g_chat = GOSSIP_GROUP_CHAT (chat);
	priv   = g_chat->priv;

	return priv->own_contact;
}

static void
group_chat_get_geometry (GossipChat *chat,
		         gint       *width,
		 	 gint       *height)
{
	*width  = 600;
	*height = 400;
}

static gboolean
group_chat_get_group_chat (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), FALSE);

	return TRUE;
}

static gboolean
group_chat_get_show_contacts (GossipChat *chat)
{
	GossipGroupChatPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), FALSE);

	priv = GOSSIP_GROUP_CHAT (chat)->priv;
	
	return priv->contacts_visible;
}

static void
group_chat_set_show_contacts (GossipChat *chat, 
			      gboolean    show)
{
	GossipGroupChatPriv *priv;

	g_return_if_fail (GOSSIP_IS_GROUP_CHAT (chat));

	priv = GOSSIP_GROUP_CHAT (chat)->priv;
	
	priv->contacts_visible = show;

	if (show) {
		gtk_widget_show (priv->contacts_sw);
		gtk_paned_set_position (GTK_PANED (priv->hpaned),
					priv->contacts_width);
	} else {
		priv->contacts_width = gtk_paned_get_position (GTK_PANED (priv->hpaned));
		gtk_widget_hide (priv->contacts_sw);
	}
}

/* Scroll down after the back-log has been received. */
static gboolean
group_chat_scroll_down_func (GossipChat *chat)
{
	gossip_chat_scroll_down (chat);
 	g_object_unref (chat);

	return FALSE;
}

GossipGroupChat *
gossip_group_chat_show (GossipChatroomProvider *provider,
			GossipChatroomId        id)
{
	GossipGroupChat     *chat;
	GossipGroupChatPriv *priv;
	
	group_chats_init ();

	chat = g_hash_table_lookup (group_chats, GINT_TO_POINTER (id));
	if (chat) {
		gossip_chat_present (GOSSIP_CHAT (chat));
		return chat;
	}

	chat = g_object_new (GOSSIP_TYPE_GROUP_CHAT, NULL);
	priv = chat->priv;

	priv->room_id = id;
	
	priv->name = g_strdup (gossip_chatroom_provider_get_room_name (provider, id));

	priv->private_chats = NULL;
	
	group_chat_create_gui (chat);

	g_hash_table_insert (group_chats, GINT_TO_POINTER (id), chat);

	g_signal_connect (provider, "chatroom-new-message",
			  G_CALLBACK (group_chat_new_message_cb),
			  chat);
	g_signal_connect (provider, "chatroom-new-room-event",
			  G_CALLBACK (group_chat_new_room_event_cb),
			  chat);
	g_signal_connect (provider, "chatroom-topic-changed",
			  G_CALLBACK (group_chat_topic_changed_cb),
			  chat);
	g_signal_connect (provider, "chatroom-contact-joined",
			  G_CALLBACK (group_chat_contact_joined_cb),
			  chat);
	g_signal_connect (provider, "chatroom-contact-left",
			  G_CALLBACK (group_chat_contact_left_cb),
			  chat);
	g_signal_connect (provider, "chatroom-contact-presence-updated",
			  G_CALLBACK (group_chat_contact_presence_updated_cb),
			  chat);
	g_signal_connect (provider, "chatroom-contact-updated",
			  G_CALLBACK (group_chat_contact_updated_cb),
			  chat);

	g_signal_connect_object (gossip_app_get_session (),
				 "connected",
				 G_CALLBACK (group_chat_connected_cb),
				 chat, 0);

	g_signal_connect_object (gossip_app_get_session (),
				 "disconnected",
				 G_CALLBACK (group_chat_disconnected_cb),
				 chat, 0);

	priv->provider = provider;
	
	gossip_chat_present (GOSSIP_CHAT (chat));

 	g_object_ref (chat);
	g_idle_add ((GSourceFunc) group_chat_scroll_down_func, chat);
	
	return chat;
}

GossipChatroomId 
gossip_group_chat_get_room_id (GossipGroupChat *chat)
{
	GossipGroupChatPriv *priv;

 	g_return_val_if_fail (GOSSIP_IS_GROUP_CHAT (chat), 0); 

	priv = chat->priv;	

	return priv->room_id;
}

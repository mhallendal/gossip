/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2005 Imendio AB
 * Copyright (C) 2003-2004 Geert-Jan Van den Bogaerde <geertjan@gnome.org>
 * Copyright (C) 2004      Martyn Russell <mr@gnome.org>
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
#include <stdlib.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glib/gi18n.h>

#include "gossip-chat-window.h"
#include "gossip-marshal.h"
#include "gossip-spell.h"
#include "gossip-spell-dialog.h"
#include "gossip-ui-utils.h"
#include "gossip-preferences.h"
#include "gossip-chat.h"
#include "gossip-app.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_CHAT, GossipChatPriv))

#define DEBUG_MSG(x)
/* #define DEBUG_MSG(args) g_printerr args ; g_printerr ("\n");   */

#define CHAT_DIR_CREATE_MODE    (S_IRUSR | S_IWUSR | S_IXUSR)
#define CHAT_FILE_CREATE_MODE   (S_IRUSR | S_IWUSR)

#define CHAT_KEY_FILENAME       "geometry.ini"
#define CHAT_KEY_GROUP_CONTACTS "Chat"

#define CHAT_GEOMETRY_FORMAT    "%d,%d,%d,%d"

struct _GossipChatPriv {
	GossipChatWindow *window;

	GossipSpell      *spell;

	/* Used to automatically shrink a window that has temporarily
	   grown due to long input */
	gint              padding_height;
	gint              default_window_height;
	gint              last_input_height;
};

typedef struct {
	GossipChat  *chat;
       	gchar       *word;

       	GtkTextIter  start;
       	GtkTextIter  end;
} GossipChatSpell;

static void      gossip_chat_class_init            (GossipChatClass *klass);
static void      gossip_chat_init                  (GossipChat      *chat);
static void      chat_finalize                     (GObject         *object);
static void      chat_input_text_buffer_changed_cb (GtkTextBuffer   *buffer,
						    GossipChat      *chat);
static void      chat_text_view_size_allocate_cb   (GtkWidget       *widget,
						    GtkAllocation   *allocation,
						    GossipChat      *chat);
static void      chat_text_populate_popup_cb       (GtkTextView     *view,
						    GtkMenu         *menu,
						    GossipChat      *chat);
static void      chat_text_check_word_spelling_cb  (GtkMenuItem     *menuitem,
						    GossipChatSpell *chat_spell);
GossipChatSpell *chat_spell_new                    (GossipChat      *chat,
						    const gchar     *word,
						    GtkTextIter      start,
						    GtkTextIter      end);
static void      chat_spell_free                   (GossipChatSpell *chat_spell);
static gchar *   chat_window_get_geometry_filename (void);

enum {
	COMPOSING,
	NEW_MESSAGE,
	NAME_CHANGED,
	STATUS_CHANGED,
	LAST_SIGNAL
};

static guint chat_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GossipChat, gossip_chat, G_TYPE_OBJECT);

static void
gossip_chat_class_init (GossipChatClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = chat_finalize;

        chat_signals[COMPOSING] =
                g_signal_new ("composing",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GossipChatClass, composing),
                              NULL, NULL,
			      gossip_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

	chat_signals[NEW_MESSAGE] =
		g_signal_new ("new-message",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GossipChatClass, new_message),
			      NULL, NULL,
			      gossip_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, GOSSIP_TYPE_MESSAGE);

	chat_signals[NAME_CHANGED] =
		g_signal_new ("name-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GossipChatClass, name_changed),
			      NULL, NULL,
			      gossip_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	chat_signals[STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GossipChatClass, status_changed),
			      NULL, NULL,
			      gossip_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (GossipChatPriv));
}

static void
gossip_chat_init (GossipChat *chat)
{
	GossipChatPriv *priv;
	GtkTextBuffer  *buffer;

	gchar          *value;
	GList          *languages = NULL;

	chat->view = gossip_chat_view_new ();
	chat->input_text_view = gtk_text_view_new ();
	chat->is_first_char = TRUE;

	g_object_set (chat->input_text_view,
		      "pixels-above-lines", 2,
		      "pixels-below-lines", 2,
		      "pixels-inside-wrap", 1,
		      "right-margin", 2,
		      "left-margin", 2,
		      "wrap-mode", GTK_WRAP_WORD_CHAR,
		      NULL);

	priv = GET_PRIV (chat);

	priv->default_window_height = -1;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	g_signal_connect (buffer,
			  "changed",
			  G_CALLBACK (chat_input_text_buffer_changed_cb),
			  chat);

	g_signal_connect (chat->input_text_view,
			  "size_allocate",
			  G_CALLBACK (chat_text_view_size_allocate_cb),
			  chat);

	g_signal_connect (GTK_TEXT_VIEW (chat->input_text_view),
			  "populate_popup",
			  G_CALLBACK (chat_text_populate_popup_cb),
			  chat);

	/* create misspelt words identification tag */
	gtk_text_buffer_create_tag (buffer,
				    "misspelled",
				    "underline", PANGO_UNDERLINE_ERROR,
				    NULL);

	/* get spelling languages */
	value = gconf_client_get_string (gossip_app_get_gconf_client (),
					 GCONF_PATH "/conversation/spell_checker_languages", 
					 NULL);
	
	if (value) {
		gchar **vlanguages;
		gchar  *lang;
		gint    i;

		vlanguages = g_strsplit (value, ",", -1);
		
		for (i = 0, lang = vlanguages[i]; lang; lang = vlanguages[++i]) {
			languages = g_list_append (languages, g_strdup (lang));
		}

		g_strfreev (vlanguages);
	}
	
	g_free (value);
	
	priv->spell = gossip_spell_new (languages);
	
	if (languages) {
		g_list_foreach (languages, (GFunc)g_free, NULL);
		g_list_free (languages);
	}
}

static void
chat_finalize (GObject *object)
{
	GossipChat     *chat;
	GossipChatPriv *priv;

	g_return_if_fail (GOSSIP_IS_CHAT (object));

	chat = GOSSIP_CHAT (object);
	priv = GET_PRIV (chat);

	if (priv->spell) {
		gossip_spell_unref (priv->spell);
		g_free (priv);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
chat_input_text_buffer_changed_cb (GtkTextBuffer *buffer, GossipChat *chat)
{
	GossipChatPriv *priv;

	GtkTextIter     start, end;
	gchar          *str;
	gboolean        spell_checker;

	priv = GET_PRIV (chat);

 	spell_checker = gconf_client_get_bool (gossip_app_get_gconf_client (), 
					       "/apps/gossip/conversation/enable_spell_checker", 
					       NULL); 

	if (chat->is_first_char) {
		GtkRequisition  req;
		gint            window_height;
		GtkWidget      *dialog;
		GtkAllocation  *allocation;

		/* Save the window's size */
		dialog = gossip_chat_window_get_dialog (priv->window);
		gtk_window_get_size (GTK_WINDOW (dialog),
				     NULL, &window_height);

		gtk_widget_size_request (chat->input_text_view, &req);

		allocation = &GTK_WIDGET (chat->view)->allocation;

		priv->default_window_height = window_height;
		priv->last_input_height = req.height;
		priv->padding_height = window_height - req.height - allocation->height;

		chat->is_first_char = FALSE;
	}

	gtk_text_buffer_get_start_iter (buffer, &start);

	if (!spell_checker) {
		gtk_text_buffer_get_end_iter (buffer, &end);
		gtk_text_buffer_remove_tag_by_name (buffer, "misspelled", &start, &end);
		return;
	}

	if (!gossip_spell_supported () || !gossip_spell_has_backend (priv->spell)) {
		return;
	}
	
	/* NOTE: this is really inefficient, we shouldn't have to
	   reiterate the whole buffer each time and check each work
	   every time. */
	while (TRUE) {
		gboolean correct = FALSE;

		/* if at start */
		if (gtk_text_iter_is_start (&start)) {
			end = start;

			if (!gtk_text_iter_forward_word_end (&end)) {
				/* no whole word yet */
				break;
			}
		} else {
			if (!gtk_text_iter_forward_word_end (&end)) {
				/* must be the end of the buffer */
				break;
			}

			start = end; 
			gtk_text_iter_backward_word_start (&start);
		}
	
		str = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

		/* spell check string */
  		if (!gossip_chat_get_is_command (str)) { 
			correct = gossip_spell_check (priv->spell, str);
 		} else { 
 			correct = TRUE; 
 		} 

		if (!correct) {
			gtk_text_buffer_apply_tag_by_name (buffer, "misspelled", &start, &end);
		} else {
			gtk_text_buffer_remove_tag_by_name (buffer, "misspelled", &start, &end);
		}

		g_free (str);

		/* set start iter to the end iters position */
		start = end; 
	}
}

typedef struct {
	GtkWidget *window;
	gint       width;
	gint       height;
} ChangeSizeData;

static gboolean
chat_change_size_in_idle_cb (ChangeSizeData *data)
{
	gtk_window_resize (GTK_WINDOW (data->window), 
			   data->width, data->height);

	return FALSE;
}

static void
chat_text_view_size_allocate_cb (GtkWidget     *widget,
		                 GtkAllocation *allocation,
				 GossipChat    *chat)
{
	GossipChatPriv   *priv;
	gint              width;
	GtkWidget        *dialog;
	ChangeSizeData   *data;
	gint              window_height;
	gint              new_height;
	GtkAllocation    *view_allocation;
	gint              current_height;
	gint              diff;

	priv = GET_PRIV (chat);

	if (priv->default_window_height <= 0) {
		return;
	}

	if (priv->last_input_height <= allocation->height) {
		priv->last_input_height = allocation->height;
		return;
	}

	diff = priv->last_input_height - allocation->height;
	priv->last_input_height = allocation->height;
	
	view_allocation = &GTK_WIDGET (chat->view)->allocation;

	dialog = gossip_chat_window_get_dialog (priv->window);
	gtk_window_get_size (GTK_WINDOW (dialog), NULL, &current_height);

	new_height = view_allocation->height + priv->padding_height + allocation->height - diff;

	if (new_height <= priv->default_window_height) {
		window_height = priv->default_window_height;
	} else {
		window_height = new_height;
	}

	if (current_height <= window_height) {
		return;
	}

	/* Restore the window's size */
	gtk_window_get_size (GTK_WINDOW (dialog), &width, NULL);

	data = g_new0 (ChangeSizeData, 1);
	data->window = dialog;
	data->width  = width;
	data->height = window_height;

	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
			 (GSourceFunc) chat_change_size_in_idle_cb,
			 data, g_free);
}

static void
chat_insert_smiley_activate_cb (GtkWidget  *menuitem, 
				GossipChat *chat)
{
	GtkTextBuffer *buffer;
	GtkTextIter    iter;
	const gchar   *smiley;

	smiley = g_object_get_data (G_OBJECT (menuitem), "smiley_text");

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, 
				smiley, 
				g_utf8_strlen (smiley, -1));
}

static void
chat_text_populate_popup_cb (GtkTextView *view,
			     GtkMenu     *menu,
			     GossipChat  *chat)
{
	GtkTextBuffer   *buffer;
	GtkTextTagTable *table;
	GtkTextTag      *tag;
	gint             x, y;
	GtkTextIter      iter, start, end;
	GtkWidget       *item;
	gchar           *str = NULL;
	GossipChatSpell *chat_spell;
	GtkWidget       *smiley_menu;

	/*
	 * Add the emoticon menu.
	 */

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
	
	item = gtk_menu_item_new_with_mnemonic (_("Insert Smiley"));
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	smiley_menu = gossip_chat_view_get_smiley_menu (
		G_CALLBACK (chat_insert_smiley_activate_cb), 
		chat);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), smiley_menu);

	/*
	 * Add the spell check menu item.
	 */

	buffer = gtk_text_view_get_buffer (view);
	table = gtk_text_buffer_get_tag_table (buffer);

	tag = gtk_text_tag_table_lookup (table, "misspelled");
	
	gtk_widget_get_pointer (GTK_WIDGET (view), &x, &y);
	
	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view), 
					       GTK_TEXT_WINDOW_WIDGET,
					       x, y,
					       &x, &y);
	
	gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view), &iter, x, y);

	start = end = iter;
	
	if (gtk_text_iter_backward_to_tag_toggle (&start, tag) &&
	    gtk_text_iter_forward_to_tag_toggle (&end, tag)) {
					
		str = gtk_text_buffer_get_text (buffer, 
						&start, &end, FALSE);
	}

	if (!str || strlen (str) == 0) {
		return;
	}

	chat_spell = chat_spell_new (chat, str, start, end);

	g_object_set_data_full (G_OBJECT (menu), 
				"chat_spell", chat_spell, 
				(GDestroyNotify) chat_spell_free);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
	
	item = gtk_menu_item_new_with_mnemonic (_("_Check Word Spelling..."));
	g_signal_connect (item,
			  "activate",
			  G_CALLBACK (chat_text_check_word_spelling_cb),
			  chat_spell);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
}

static void     
chat_text_check_word_spelling_cb (GtkMenuItem     *menuitem, 
				  GossipChatSpell *chat_spell)
{
	GossipChatPriv *priv;

	priv = GET_PRIV (chat_spell->chat);

	gossip_spell_dialog_show (chat_spell->chat,
				  priv->spell,
				  chat_spell->start,
				  chat_spell->end,
				  chat_spell->word);
}

GossipChatSpell *
chat_spell_new (GossipChat  *chat, 
		const gchar *word,
		GtkTextIter  start,
		GtkTextIter  end)
{
	GossipChatSpell *chat_spell;

	g_return_val_if_fail (chat != NULL, NULL);
	g_return_val_if_fail (word != NULL, NULL);

	chat_spell = g_new0 (GossipChatSpell, 1);

	chat_spell->chat = g_object_ref (chat);


	chat_spell->word = g_strdup (word);

	chat_spell->start = start;
	chat_spell->end = end;
	
	return chat_spell;
}

static void
chat_spell_free (GossipChatSpell *chat_spell)
{
	g_object_unref (chat_spell->chat);
	g_free (chat_spell->word);
	g_free (chat_spell);
}

static gchar *
chat_window_get_geometry_filename (void)
{
	gchar *dir;
	gchar *filename;

	dir = g_build_filename (g_get_home_dir (), ".gnome2", PACKAGE_NAME, NULL);
	if (!g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
		DEBUG_MSG (("ChatWindow: Creating directory:'%s'", dir));
		g_mkdir_with_parents (dir, CHAT_DIR_CREATE_MODE);
	}

	filename = g_build_filename (dir, CHAT_KEY_FILENAME, NULL);
	g_free (dir);

	return filename;
}

gboolean
gossip_chat_get_is_command (const gchar *str)
{
	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (strlen (str) > 0, FALSE);
	
	if (g_str_has_prefix (str, "/me")) {
		return TRUE;
	}
	else if (g_str_has_prefix (str, "/nick")) {
		return TRUE;
	}
	else if (g_str_has_prefix (str, "/topic")) {
		return TRUE;
	}

	return FALSE;
}

void
gossip_chat_correct_word (GossipChat  *chat,
			  GtkTextIter  start,
			  GtkTextIter  end,
			  const gchar *new_word)
{
	GtkTextBuffer *buffer;

	g_return_if_fail (chat != NULL);
	g_return_if_fail (new_word != NULL);
	g_return_if_fail (strlen (new_word) > 0);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));

	gtk_text_buffer_delete (buffer, &start, &end);
	gtk_text_buffer_insert (buffer, &start, 
				new_word, 
				g_utf8_strlen (new_word, -1));
}

const gchar *
gossip_chat_get_name (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_name) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_name (chat);
	}

	return NULL;
}

gchar *
gossip_chat_get_tooltip (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_tooltip) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_tooltip (chat);
	}

	return NULL;
}

GdkPixbuf *
gossip_chat_get_status_pixbuf (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_status_pixbuf) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_status_pixbuf (chat);
	}

	return NULL;
}

GossipContact *
gossip_chat_get_contact (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_contact) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_contact (chat);
	}

	return NULL;
}

GossipContact *
gossip_chat_get_own_contact (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_own_contact) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_own_contact (chat);
	}

	return NULL;
}

GossipChatroom *
gossip_chat_get_chatroom (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_chatroom) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_chatroom (chat);
	}

	return NULL;
}

GtkWidget *
gossip_chat_get_widget (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), NULL);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_widget) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_widget (chat);
	}

	return NULL;
}

gboolean
gossip_chat_is_group_chat (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), FALSE);

	if (GOSSIP_CHAT_GET_CLASS (chat)->is_group_chat) {
		return GOSSIP_CHAT_GET_CLASS (chat)->is_group_chat (chat);
	}

	return FALSE;
}

gboolean 
gossip_chat_get_show_contacts (GossipChat *chat)
{
	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), FALSE);

	if (GOSSIP_CHAT_GET_CLASS (chat)->get_show_contacts) {
		return GOSSIP_CHAT_GET_CLASS (chat)->get_show_contacts (chat);
	}

	return FALSE;
}

void 
gossip_chat_set_show_contacts (GossipChat *chat, 
			       gboolean    show)
{
	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	if (GOSSIP_CHAT_GET_CLASS (chat)->set_show_contacts) {
		GOSSIP_CHAT_GET_CLASS (chat)->set_show_contacts (chat, show);
	}
}

void 
gossip_chat_save_geometry (GossipChat *chat, 
			   gint        x,
			   gint        y,
			   gint        w,
			   gint        h)
{
	GError      *error = NULL;
	GKeyFile    *key_file;
	const gchar *id;

	gchar       *filename;

	GdkScreen   *screen;
	gint         max_width;
	gint         max_height;

	gchar       *content;
	gsize        length;
	gchar       *str;

	DEBUG_MSG (("Chat: Saving geometry: x:%d, y:%d, w:%d, h:%d\n", 
		    x, y, w, h));

	if (gossip_chat_is_group_chat (chat)) {
		GossipChatroom *chatroom;

		chatroom = gossip_chat_get_chatroom (chat);
		id = gossip_chatroom_get_id_str (chatroom);
	} else {
		GossipContact *contact;

		contact = gossip_chat_get_contact (chat);
		id = gossip_contact_get_id (contact);
	}

	screen = gdk_screen_get_default ();
	max_width = gdk_screen_get_width (screen);
	max_height = gdk_screen_get_height (screen);

	x = CLAMP (x, 0, max_width);
	y = CLAMP (y, 0, max_height);

	w = CLAMP (w, 100, max_width);
	h = CLAMP (h, 100, max_height);

	str = g_strdup_printf (CHAT_GEOMETRY_FORMAT, x, y, w, h);
	
	key_file = g_key_file_new ();

	filename = chat_window_get_geometry_filename ();

	g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, NULL);
	g_key_file_set_comment (key_file, CHAT_KEY_GROUP_CONTACTS, NULL, 
				"Note: The format below is: X, Y, Width, Height", 
				NULL);
	g_key_file_set_string (key_file, CHAT_KEY_GROUP_CONTACTS, id, str);

	g_free (str);

	content = g_key_file_to_data (key_file, &length, NULL);
	if (!g_file_set_contents (filename, content, length, &error)) { 
		g_warning ("Couldn't save chat window geometry, error:%d->'%s'", 
			   error->code, error->message);
		g_error_free (error);
	}

	g_free (content);
	g_free (filename);
	g_key_file_free (key_file);
}

void 
gossip_chat_load_geometry (GossipChat *chat,
			   gint       *x,
			   gint       *y,
			   gint       *w,
			   gint       *h)
{
	GKeyFile    *key_file;
	const gchar *id;
	gchar       *filename;
	gchar       *str = NULL;

	if (x) {
		*x = 0;
	}

	if (y) {
		*y = 0;
	}

	if (w) {
		*w = 0;
	}

	if (h) {
		*h = 0;
	}

	if (gossip_chat_is_group_chat (chat)) {
		GossipChatroom *chatroom;

		chatroom = gossip_chat_get_chatroom (chat);
		id = gossip_chatroom_get_id_str (chatroom);
	} else {
		GossipContact *contact;

		contact = gossip_chat_get_contact (chat);
		id = gossip_contact_get_id (contact);
	}

	key_file = g_key_file_new ();

	filename = chat_window_get_geometry_filename ();

	if (g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, NULL)) {
		str = g_key_file_get_string (key_file, CHAT_KEY_GROUP_CONTACTS, id, NULL);
	}

	if (str) {
		gint tmp_x, tmp_y, tmp_w, tmp_h;

		sscanf (str, CHAT_GEOMETRY_FORMAT, &tmp_x, &tmp_y, &tmp_w, &tmp_h);

		if (x) {
			*x = tmp_x;
		}
		
		if (y) {
			*y = tmp_y;
		}
		
		if (w) {
			*w = tmp_w;
		}
		
		if (h) {
			*h = tmp_h;
		}

		g_free (str);
	}

	DEBUG_MSG (("Chat: Loading geometry: x:%d, y:%d, w:%d, h:%d\n", 
		    x ? *x : -1, 
		    y ? *y : -1, 
		    w ? *w : -1, 
		    h ? *h : -1));

	g_free (filename);
	g_key_file_free (key_file);
}

void
gossip_chat_clear (GossipChat *chat)
{
	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	gossip_chat_view_clear (chat->view);
}

void
gossip_chat_set_window (GossipChat       *chat, 
			GossipChatWindow *window)
{
	GossipChatPriv *priv;

	priv = GET_PRIV (chat);
	priv->window = window;
}

GossipChatWindow *
gossip_chat_get_window (GossipChat *chat)
{
	GossipChatPriv *priv;

	priv = GET_PRIV (chat);

	return priv->window;
}

void
gossip_chat_scroll_down (GossipChat *chat)
{
	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	gossip_chat_view_scroll_down (chat->view);
}

void
gossip_chat_cut (GossipChat *chat)
{
	GtkTextBuffer *buffer;
	
	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	if (gtk_text_buffer_get_selection_bounds (buffer, NULL, NULL)) {
		GtkClipboard *clipboard;

		clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

		gtk_text_buffer_cut_clipboard (buffer, clipboard, TRUE);
	}
}

void
gossip_chat_copy (GossipChat *chat)
{
	GtkTextBuffer *buffer;
	
	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	if (gossip_chat_view_get_selection_bounds (chat->view, NULL, NULL)) {
		gossip_chat_view_copy_clipboard (chat->view);
		return;
	}

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	if (gtk_text_buffer_get_selection_bounds (buffer, NULL, NULL)) {
		GtkClipboard *clipboard;

		clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

		gtk_text_buffer_copy_clipboard (buffer, clipboard);
	}
}

void
gossip_chat_paste (GossipChat *chat)
{
	GtkTextBuffer *buffer;
	GtkClipboard  *clipboard;

	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (chat->input_text_view));
	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

	gtk_text_buffer_paste_clipboard (buffer, clipboard, NULL, TRUE);
}

void
gossip_chat_present (GossipChat *chat)
{
	GossipChatPriv *priv;

	g_return_if_fail (GOSSIP_IS_CHAT (chat));

	priv = GET_PRIV (chat);
	
	if (priv->window == NULL) {
		GossipChatWindow *window;
		gboolean          for_group_chat;

		for_group_chat = gossip_chat_is_group_chat (chat);

		/* get the default window for either group chats or
		   normal chats, we do not want to mix them */
		window = gossip_chat_window_get_default ();

		if (!window) {
			window = gossip_chat_window_new ();
		}

		gossip_chat_window_add_chat (window, chat);
        }

        gossip_chat_window_switch_to_chat (priv->window, chat);
	gossip_window_present (
		GTK_WINDOW (gossip_chat_window_get_dialog (priv->window)));

	gtk_widget_grab_focus (chat->input_text_view);
}

gboolean
gossip_chat_should_play_sound (GossipChat *chat)
{
	GossipChatWindow *window;
	gboolean          play = TRUE;

	g_return_val_if_fail (GOSSIP_IS_CHAT (chat), FALSE);

	window = gossip_chat_get_window (GOSSIP_CHAT (chat));
	if (!window) {
		return TRUE; 
	}

	play = !gossip_chat_window_has_focus (window);

	return play;
}

gboolean
gossip_chat_should_highlight_nick (GossipMessage *message,
				   GossipContact *my_contact)
{
	GossipContact *contact;
	const gchar   *msg, *to;
	gchar         *cf_msg, *cf_to;
	gchar         *ch;
	gboolean       ret_val;

	DEBUG_MSG (("Chat: Highlighting nickname"));

	ret_val = FALSE;

	g_return_val_if_fail (GOSSIP_IS_MESSAGE (message), FALSE);

	msg = gossip_message_get_body (message);
	if (!msg) {
		return FALSE;
	}
		
	if (my_contact) {
		contact = my_contact;
	} else {
		contact = gossip_message_get_recipient (message);
	}

	to = gossip_contact_get_name (contact);
	if (!to) {
		return FALSE;
	}

	cf_msg = g_utf8_casefold (msg, -1);
	cf_to = g_utf8_casefold (to, -1);

	ch = strstr (cf_msg, cf_to);
	if (ch == NULL) {
		goto finished;
	}

	if (ch != cf_msg) {
		/* Not first in the message */
		if ((*(ch - 1) != ' ') &&
		    (*(ch - 1) != ',') &&
		    (*(ch - 1) != '.')) {
			goto finished;
		}
	}

	ch = ch + g_utf8_strlen (cf_to, -1);
	if (ch >= cf_msg + g_utf8_strlen (cf_msg, -1)) {
		ret_val = TRUE;
		goto finished;
	}

	if ((*ch == ' ') ||
	    (*ch == ',') ||
	    (*ch == '.') ||
	    (*ch == ':')) {
		ret_val = TRUE;
		goto finished;
	}

finished:
	g_free (cf_msg);
	g_free (cf_to);

	return ret_val;
}

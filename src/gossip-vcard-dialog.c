/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004-2006 Imendio AB
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
#include <glade/glade.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libgnomeui/gnome-href.h>
#include <libgnomeui/libgnomeui.h> 
#include <loudmouth/loudmouth.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <unistd.h>

#include <libgossip/gossip-session.h>
#include <libgossip/gossip-protocol.h>
#include <libgossip/gossip-vcard.h>
#include <libgossip/gossip-utils.h>

#include "gossip-account-chooser.h"
#include "gossip-app.h"
#include "gossip-vcard-dialog.h"
#include "gossip-image-chooser.h"

#define DEBUG_MSG(x)
/* #define DEBUG_MSG(args) g_printerr args ; g_printerr ("\n");  */

#define STRING_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

#define VCARD_TIMEOUT     20000
#define SAVED_TIMEOUT     10000

#define RESPONSE_SAVE     1

#define AVATAR_MAX_HEIGHT 96
#define AVATAR_MAX_WIDTH  96

typedef struct {
	GtkWidget *dialog;

	GtkWidget *hbox_account;
	GtkWidget *label_account;
	GtkWidget *account_chooser;

	GtkWidget *table_vcard;

	GtkWidget *label_name;
	GtkWidget *label_nickname;
	GtkWidget *label_web_site;
	GtkWidget *label_email;
	GtkWidget *label_description;

	GtkWidget *entry_name;
	GtkWidget *entry_nickname;
	GtkWidget *entry_web_site;
	GtkWidget *entry_email;

	GtkWidget *textview_description;

	GtkWidget *button_cancel;
	GtkWidget *button_save;
	
	GtkWidget *button_image;
	GtkWidget *avatar_chooser;

	guint      timeout_id; 
	guint      saved_id;

	gboolean   requesting_vcard;

	gint       last_account_selected;

	GnomeThumbnailFactory *thumbs;
	gchar     *person;
} GossipVCardDialog;

static void     vcard_dialog_avatar_clicked_cb        (GtkWidget         *button,
						       GossipVCardDialog *dialog);
static void     vcard_dialog_avatar_update_preview_cb (GtkFileChooser    *chooser,
						       GossipVCardDialog *dialog);
static void     vcard_dialog_set_account_to_last      (GossipVCardDialog *dialog);
static void     vcard_dialog_lookup_start             (GossipVCardDialog *dialog);
static void     vcard_dialog_lookup_stop              (GossipVCardDialog *dialog);
static void     vcard_dialog_get_vcard_cb             (GossipResult       result,
						       GossipVCard       *vcard,
						       GossipVCardDialog *dialog);
static void     vcard_dialog_set_vcard                (GossipVCardDialog *dialog);
static void     vcard_dialog_set_vcard_cb             (GossipResult       result,
						       GossipVCardDialog *dialog);
static gboolean vcard_dialog_timeout_cb               (GossipVCardDialog *dialog);
static void     vcard_dialog_account_changed_cb       (GtkWidget         *combo_box,
						       GossipVCardDialog *dialog);
static void     vcard_dialog_response_cb              (GtkDialog         *widget,
						       gint               response,
						       GossipVCardDialog *dialog);
static void     vcard_dialog_destroy_cb               (GtkWidget         *widget,
						       GossipVCardDialog *dialog);

/* Called from Glade, so it shouldn't be static. */
GtkWidget *vcard_dialog_create_avatar_chooser (gpointer data);

GtkWidget *
vcard_dialog_create_avatar_chooser (gpointer data)
{
	GtkWidget *chooser;

	chooser = gossip_image_chooser_new ();

	gossip_image_chooser_set_image_max_size (GOSSIP_IMAGE_CHOOSER (chooser),
						 AVATAR_MAX_WIDTH, 
						 AVATAR_MAX_HEIGHT);

	gtk_widget_show_all (chooser);

	gtk_widget_set_size_request (chooser, 
				     AVATAR_MAX_WIDTH / 2,
				     AVATAR_MAX_HEIGHT / 2);

	return chooser;
}

static void
vcard_dialog_avatar_clicked_cb (GtkWidget         *button,
				GossipVCardDialog *dialog) 
{
	GtkWidget *chooser_dialog;
	gint       response;
	GtkWidget *avatar_chooser;
	GtkWidget *image;
	
	avatar_chooser = dialog->avatar_chooser;

	chooser_dialog = gtk_file_chooser_dialog_new (_("Select Your Avatar Image"),
						      GTK_WINDOW (dialog->dialog),
						      GTK_FILE_CHOOSER_ACTION_OPEN,
						      _("No Image"),
						      GTK_RESPONSE_NO,
						      GTK_STOCK_CANCEL,
						      GTK_RESPONSE_CANCEL,
						      GTK_STOCK_OPEN,
						      GTK_RESPONSE_OK,
						      NULL);

	gtk_window_set_transient_for (GTK_WINDOW (chooser_dialog), 
				      GTK_WINDOW (dialog->dialog));

/* 	gtk_dialog_set_default_response (GTK_DIALOG (chooser_dialog), GTK_RESPONSE_ACCEPT); */
/* 	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (chooser_dialog), g_get_home_dir ()); */

	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (chooser_dialog), FALSE);
	
	image = gtk_image_new ();
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (chooser_dialog), image);
	gtk_widget_set_size_request (image, 96, 96);
	gtk_widget_show (image);

	g_signal_connect (chooser_dialog, "update-preview",
			  G_CALLBACK (vcard_dialog_avatar_update_preview_cb), 
			  dialog);

	response = gtk_dialog_run (GTK_DIALOG (chooser_dialog));

	if (response == GTK_RESPONSE_OK) {
		gchar *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser_dialog));
		gossip_image_chooser_set_from_file (GOSSIP_IMAGE_CHOOSER (avatar_chooser), filename);
		g_free (filename);
	} else if (response == GTK_RESPONSE_NO) {
		gossip_image_chooser_set_from_file (GOSSIP_IMAGE_CHOOSER (avatar_chooser),
						    dialog->person);
	}

	gtk_widget_destroy (chooser_dialog);
}

static void 
vcard_dialog_avatar_update_preview_cb (GtkFileChooser    *chooser,
				       GossipVCardDialog *dialog) 
{
	gchar *uri;

	uri = gtk_file_chooser_get_preview_uri (chooser);
	
	if (uri) {
		GtkWidget *image;
		GdkPixbuf *pixbuf;
		gchar     *mime_type;

		if (!dialog->thumbs) {
			dialog->thumbs = gnome_thumbnail_factory_new (GNOME_THUMBNAIL_SIZE_NORMAL);
		}

		
		mime_type = gnome_vfs_get_mime_type (uri);
		pixbuf = gnome_thumbnail_factory_generate_thumbnail (dialog->thumbs,
								     uri,
								     mime_type);
		image = gtk_file_chooser_get_preview_widget (chooser);
		
		if (pixbuf != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
			g_object_unref (pixbuf);
		} else {
			gtk_image_set_from_stock (GTK_IMAGE (image),
						  "gtk-dialog-question",
						  GTK_ICON_SIZE_DIALOG);
		}

		g_free (mime_type);
	}

	gtk_file_chooser_set_preview_widget_active (chooser, TRUE);
}

static void
vcard_dialog_set_account_to_last (GossipVCardDialog *dialog) 
{
	g_signal_handlers_block_by_func (dialog->account_chooser, 
					 vcard_dialog_account_changed_cb, 
					 dialog);
		
	gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->account_chooser), 
				  dialog->last_account_selected);

	g_signal_handlers_unblock_by_func (dialog->account_chooser, 
					   vcard_dialog_account_changed_cb, 
					   dialog);
}

static void
vcard_dialog_lookup_start (GossipVCardDialog *dialog) 
{
	GossipSession        *session;
	GossipAccount        *account;
	GossipAccountChooser *account_chooser;

	gtk_widget_set_sensitive (dialog->table_vcard, FALSE);
	gtk_widget_set_sensitive (dialog->account_chooser, FALSE);
	gtk_widget_set_sensitive (dialog->button_save, FALSE);

	dialog->timeout_id = g_timeout_add (VCARD_TIMEOUT, 
					    (GSourceFunc)vcard_dialog_timeout_cb,
					    dialog);

	session = gossip_app_get_session ();
	account_chooser = GOSSIP_ACCOUNT_CHOOSER (dialog->account_chooser);
	account = gossip_account_chooser_get_account (account_chooser);

	gossip_session_get_vcard (session,
				  account,
				  NULL,
				  (GossipVCardCallback) vcard_dialog_get_vcard_cb,
				  dialog,
				  NULL);

	dialog->requesting_vcard = TRUE;

	g_object_unref (account);
}

static void  
vcard_dialog_lookup_stop (GossipVCardDialog *dialog)
{
	dialog->requesting_vcard = FALSE;

	gtk_widget_set_sensitive (dialog->table_vcard, TRUE);
	gtk_widget_set_sensitive (dialog->account_chooser, TRUE);
	gtk_widget_set_sensitive (dialog->button_save, TRUE);

	if (dialog->timeout_id != 0) {
		g_source_remove (dialog->timeout_id);
		dialog->timeout_id = 0;
	}

	if (dialog->saved_id != 0) {
		g_source_remove (dialog->saved_id);
		dialog->saved_id = 0;
	}
}

static void
vcard_dialog_get_vcard_cb (GossipResult       result,
			   GossipVCard       *vcard,
			   GossipVCardDialog *dialog)
{
	GtkComboBox   *combo_box;
	GtkTextBuffer *buffer;
	const gchar   *str;
	const guchar  *avatar;
	gsize          avatar_length;

	DEBUG_MSG (("VCardDialog: Got a VCard response"));

	vcard_dialog_lookup_stop (dialog);

	if (result != GOSSIP_RESULT_OK) {
		DEBUG_MSG (("VCardDialog: VCard result was not good"));
		return;
	}

	str = gossip_vcard_get_name (vcard);
	gtk_entry_set_text (GTK_ENTRY (dialog->entry_name), STRING_EMPTY (str) ? "" : str);
		
	str = gossip_vcard_get_nickname (vcard);
	gtk_entry_set_text (GTK_ENTRY (dialog->entry_nickname), STRING_EMPTY (str) ? "" : str);

	str = gossip_vcard_get_email (vcard);
	gtk_entry_set_text (GTK_ENTRY (dialog->entry_email), STRING_EMPTY (str) ? "" : str);

	str = gossip_vcard_get_url (vcard);
	gtk_entry_set_text (GTK_ENTRY (dialog->entry_web_site), STRING_EMPTY (str) ? "" : str);

	str = gossip_vcard_get_description (vcard);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->textview_description));
	gtk_text_buffer_set_text (buffer, STRING_EMPTY (str) ? "" : str, -1);

	avatar = gossip_vcard_get_avatar (vcard, &avatar_length);
	if (avatar) {
		gossip_image_chooser_set_image_data (GOSSIP_IMAGE_CHOOSER (dialog->avatar_chooser),
						     (gchar*) avatar, avatar_length);
	}	

	/* Save position incase the next lookup fails. */
	combo_box = GTK_COMBO_BOX (dialog->account_chooser);
	dialog->last_account_selected = gtk_combo_box_get_active (combo_box);
}

static void
vcard_dialog_set_vcard (GossipVCardDialog *dialog)
{
	GossipVCard          *vcard;
	GossipAccount        *account;
	GossipAccountChooser *account_chooser;
	GossipProtocol       *protocol;
	GossipContact        *contact;
	GError               *error = NULL;
	GtkTextBuffer        *buffer;
	GtkTextIter           iter_begin, iter_end;
	gchar                *description;
	const gchar          *str;
	gchar                *avatar;
	gsize                 avatar_length;

	if (!gossip_app_is_connected ()) {
		DEBUG_MSG (("VCardDialog: Not connected, not setting VCard"));
		return;
	}

	vcard = gossip_vcard_new ();

	str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_name));
	gossip_vcard_set_name (vcard, str);

	str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_nickname));
	gossip_vcard_set_nickname (vcard, str);

	str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_web_site));
	gossip_vcard_set_url (vcard, str);

	str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_email));
	gossip_vcard_set_email (vcard, str);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->textview_description));
	gtk_text_buffer_get_bounds (buffer, &iter_begin, &iter_end);
	description = gtk_text_buffer_get_text (buffer, &iter_begin, &iter_end, FALSE);
	gossip_vcard_set_description (vcard, description);
	g_free (description);

	gossip_image_chooser_get_image_data (GOSSIP_IMAGE_CHOOSER (dialog->avatar_chooser),
					     &avatar, &avatar_length);
	gossip_vcard_set_avatar (vcard, avatar, avatar_length);

	/* NOTE: if account is NULL, all accounts will get the same vcard */
	account_chooser = GOSSIP_ACCOUNT_CHOOSER (dialog->account_chooser);
	account = gossip_account_chooser_get_account (account_chooser);

	gossip_session_set_vcard (gossip_app_get_session (),
				  account,
				  vcard, 
				  (GossipResultCallback) vcard_dialog_set_vcard_cb,
				  dialog, &error);

	protocol = gossip_session_get_protocol (gossip_app_get_session (), account);
	contact = gossip_protocol_get_own_contact (protocol);
	gossip_contact_set_avatar (GOSSIP_CONTACT(contact), avatar, avatar_length);

	g_free (avatar);

	g_object_unref (account);
}

static void
vcard_dialog_set_vcard_cb (GossipResult       result, 
			   GossipVCardDialog *dialog)
{
	DEBUG_MSG (("VCardDialog: Got a VCard response"));
	
	gtk_widget_destroy (dialog->dialog);
}

static gboolean
vcard_dialog_timeout_cb (GossipVCardDialog *dialog)
{
	GtkWidget *md;

	vcard_dialog_lookup_stop (dialog);

	/* Select last successfull account */
	vcard_dialog_set_account_to_last (dialog);

	/* Show message dialog and the account dialog */
	md = gtk_message_dialog_new_with_markup (GTK_WINDOW (dialog->dialog),
						 GTK_DIALOG_MODAL |
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_INFO,
						 GTK_BUTTONS_CLOSE,
						 "<b>%s</b>\n\n%s",
						 _("The server does not seem to be responding."),
						 _("Try again later."));
	
	g_signal_connect_swapped (md, "response",
				  G_CALLBACK (gtk_widget_destroy), md);
	gtk_widget_show (md);
	
	return FALSE;
}

static void
vcard_dialog_account_changed_cb (GtkWidget         *combo_box,
				 GossipVCardDialog *dialog)
{
	vcard_dialog_lookup_start (dialog);
}

static void
vcard_dialog_response_cb (GtkDialog         *widget, 
			  gint               response, 
			  GossipVCardDialog *dialog)
{
	switch (response) {
	case RESPONSE_SAVE:
		vcard_dialog_set_vcard (dialog);
		return;

	case GTK_RESPONSE_CANCEL:
		if (dialog->requesting_vcard) {
			/* Change widgets so they are unsensitive */
			vcard_dialog_lookup_stop (dialog);
			
			/* Select last successfull account */
			vcard_dialog_set_account_to_last (dialog);
			return;
		}

	default:
		break;
	}

	gtk_widget_destroy (dialog->dialog);
}

static void
vcard_dialog_destroy_cb (GtkWidget         *widget, 
			 GossipVCardDialog *dialog)
{
	vcard_dialog_lookup_stop (dialog);

	g_free (dialog);
}

void
gossip_vcard_dialog_show (GtkWindow *parent)
{
	GossipSession     *session;
	GossipVCardDialog *dialog;
	GladeXML          *glade;
	GList             *accounts;
	GtkSizeGroup      *size_group;
	GtkIconInfo       *icon;
	GtkIconTheme      *theme;
	GdkScreen         *screen;

	dialog = g_new0 (GossipVCardDialog, 1);

	glade = gossip_glade_get_file (GLADEDIR "/main.glade",
				       "vcard_dialog",
				       NULL,
				       "vcard_dialog", &dialog->dialog,
				       "hbox_account", &dialog->hbox_account,
				       "label_account", &dialog->label_account,
				       "table_vcard", &dialog->table_vcard,
				       "label_name", &dialog->label_name,
				       "label_nickname", &dialog->label_nickname,
				       "label_web_site", &dialog->label_web_site,
				       "label_email", &dialog->label_email,
				       "label_description", &dialog->label_description,
				       "entry_name", &dialog->entry_name,
				       "entry_nickname", &dialog->entry_nickname,
				       "entry_web_site", &dialog->entry_web_site,
				       "entry_email", &dialog->entry_email,
				       "textview_description", &dialog->textview_description,
				       "button_cancel", &dialog->button_cancel,
				       "button_save", &dialog->button_save,
				       "button_image", &dialog->button_image,
				       "avatar_chooser", &dialog->avatar_chooser,
				       NULL);

	gossip_glade_connect (glade, 
			      dialog,
			      "vcard_dialog", "destroy", vcard_dialog_destroy_cb,
			      "vcard_dialog", "response", vcard_dialog_response_cb,
			      NULL);

	g_object_unref (glade);

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_size_group_add_widget (size_group, dialog->label_account);
	gtk_size_group_add_widget (size_group, dialog->label_name);
	gtk_size_group_add_widget (size_group, dialog->label_nickname);
	gtk_size_group_add_widget (size_group, dialog->label_email);
	gtk_size_group_add_widget (size_group, dialog->label_web_site);
	gtk_size_group_add_widget (size_group, dialog->label_description);

	g_object_unref (size_group);

	/* Setup image chooser */
	g_signal_connect (dialog->button_image, "clicked",
			  G_CALLBACK (vcard_dialog_avatar_clicked_cb),
			  dialog);

	screen = gtk_window_get_screen (GTK_WINDOW (dialog->dialog));
	theme = gtk_icon_theme_get_for_screen (screen);

	icon = gtk_icon_theme_lookup_icon (theme, "stock_person", 80, 0);
	dialog->person = g_strdup (gtk_icon_info_get_filename (icon));

	gtk_icon_info_free (icon);

	gossip_image_chooser_set_from_file (GOSSIP_IMAGE_CHOOSER (dialog->avatar_chooser), 
					    dialog->person);
	
	/* Sort out accounts */
	session = gossip_app_get_session ();

	dialog->account_chooser = gossip_account_chooser_new (session);
	gtk_box_pack_start (GTK_BOX (dialog->hbox_account), 
			    dialog->account_chooser,
			    TRUE, TRUE, 0);

	g_signal_connect (dialog->account_chooser, "changed",
			  G_CALLBACK (vcard_dialog_account_changed_cb),
			  dialog);

	gtk_widget_show (dialog->account_chooser);

	/* Select first */
	accounts = gossip_session_get_accounts (session);
	if (g_list_length (accounts) > 1) {
		gtk_widget_show (dialog->hbox_account);
	} else {
		/* Show no accounts combo box */	
		gtk_widget_hide (dialog->hbox_account);
	}

	g_list_foreach (accounts, (GFunc)g_object_unref, NULL);
	g_list_free (accounts);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog->dialog), parent); 
	}

	gtk_widget_show (dialog->dialog);
	
	vcard_dialog_lookup_start (dialog);
}


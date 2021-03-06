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
#include <stdio.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "gossip-app.h"
#include "gossip-edit-chatroom-dialog.h"
#include "gossip-glade.h"
#include "gossip-ui-utils.h"

typedef struct {
    GtkWidget      *dialog;
    GtkWidget      *entry_name;
    GtkWidget      *entry_nickname;
    GtkWidget      *entry_server;
    GtkWidget      *entry_room;
    GtkWidget      *entry_password;
    GtkWidget      *checkbutton_auto_connect;
    GtkWidget      *button_save;

    GossipChatroom *chatroom;
} GossipEditChatroomDialog;

static void edit_chatroom_dialog_set              (GossipEditChatroomDialog *dialog);
static void edit_chatroom_dialog_entry_changed_cb (GtkEntry                 *entry,
                                                   GossipEditChatroomDialog *dialog);
static void edit_chatroom_dialog_response_cb      (GtkWidget                *widget,
                                                   gint                      response,
                                                   GossipEditChatroomDialog *dialog);
static void edit_chatroom_dialog_destroy_cb       (GtkWidget                *widget,
                                                   GossipEditChatroomDialog *dialog);

static void
edit_chatroom_dialog_set (GossipEditChatroomDialog *dialog)
{
    GossipChatroomManager *manager;
    GtkToggleButton       *togglebutton;
    const gchar           *str;

    manager = gossip_app_get_chatroom_manager ();

    /* Set chatroom information */
    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_name));
    gossip_chatroom_set_name (dialog->chatroom, str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_nickname));
    gossip_chatroom_set_nick (dialog->chatroom, str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_server));
    gossip_chatroom_set_server (dialog->chatroom, str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_room));
    gossip_chatroom_set_room (dialog->chatroom, str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_password));
    gossip_chatroom_set_password (dialog->chatroom, str);

    togglebutton = GTK_TOGGLE_BUTTON (dialog->checkbutton_auto_connect);
    gossip_chatroom_set_auto_connect (dialog->chatroom, 
                                      gtk_toggle_button_get_active (togglebutton));

    gossip_chatroom_manager_store (manager);
}

static void
edit_chatroom_dialog_entry_changed_cb (GtkEntry                 *entry,
                                       GossipEditChatroomDialog *dialog)
{
    const gchar *str;
    gboolean     disabled = FALSE;

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_nickname));
    disabled |= G_STR_EMPTY (str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_server));
    disabled |= G_STR_EMPTY (str);

    str = gtk_entry_get_text (GTK_ENTRY (dialog->entry_room));
    disabled |= G_STR_EMPTY (str);

    gtk_widget_set_sensitive (dialog->button_save, !disabled);
}

static void
edit_chatroom_dialog_response_cb (GtkWidget               *widget,
                                  gint                     response,
                                  GossipEditChatroomDialog *dialog)
{
    if (response == GTK_RESPONSE_OK) {
        edit_chatroom_dialog_set (dialog);
    }

    gtk_widget_destroy (widget);
}

static void
edit_chatroom_dialog_destroy_cb (GtkWidget                *widget,
                                 GossipEditChatroomDialog *dialog)
{
    g_object_unref (dialog->chatroom);
    g_free (dialog);
}

void
gossip_edit_chatroom_dialog_show (GtkWindow      *parent,
                                  GossipChatroom *chatroom)
{
    GossipEditChatroomDialog *dialog;
    GossipAccount            *account;
    GladeXML                 *glade;
    GtkWidget                *label_server;

    g_return_if_fail (chatroom != NULL);

    dialog = g_new0 (GossipEditChatroomDialog, 1);

    dialog->chatroom = g_object_ref (chatroom);

    glade = gossip_glade_get_file ("group-chat.glade",
                                   "edit_chatroom_dialog",
                                   NULL,
                                   "edit_chatroom_dialog", &dialog->dialog,
                                   "entry_name", &dialog->entry_name,
                                   "entry_nickname", &dialog->entry_nickname,
                                   "label_server", &label_server,
                                   "entry_server", &dialog->entry_server,
                                   "entry_room", &dialog->entry_room,
                                   "entry_password", &dialog->entry_password,
                                   "checkbutton_auto_connect", &dialog->checkbutton_auto_connect,
                                   "button_save", &dialog->button_save,
                                   NULL);

    gossip_glade_connect (glade,
                          dialog,
                          "edit_chatroom_dialog", "destroy", edit_chatroom_dialog_destroy_cb,
                          "edit_chatroom_dialog", "response", edit_chatroom_dialog_response_cb,
                          "entry_name", "changed", edit_chatroom_dialog_entry_changed_cb,
                          "entry_nickname", "changed", edit_chatroom_dialog_entry_changed_cb,
                          "entry_server", "changed", edit_chatroom_dialog_entry_changed_cb,
                          "entry_room", "changed", edit_chatroom_dialog_entry_changed_cb,
                          NULL);

    g_object_unref (glade);

    account = gossip_chatroom_get_account (chatroom);

    gtk_entry_set_text (GTK_ENTRY (dialog->entry_name),
                        gossip_chatroom_get_name (chatroom));
    gtk_entry_set_text (GTK_ENTRY (dialog->entry_nickname),
                        gossip_chatroom_get_nick (chatroom));
    gtk_entry_set_text (GTK_ENTRY (dialog->entry_server),
                        gossip_chatroom_get_server (chatroom));
    gtk_entry_set_text (GTK_ENTRY (dialog->entry_room),
                        gossip_chatroom_get_room (chatroom));
    gtk_entry_set_text (GTK_ENTRY (dialog->entry_password),
                        gossip_chatroom_get_password (chatroom));
    gtk_toggle_button_set_active (
        GTK_TOGGLE_BUTTON (dialog->checkbutton_auto_connect),
        gossip_chatroom_get_auto_connect (chatroom));

    if (parent) {
        gtk_window_set_transient_for (GTK_WINDOW (dialog->dialog), parent);
    }

    gtk_widget_show (dialog->dialog);
}

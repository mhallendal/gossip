/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2004 Martyn Russell <mr@gnome.org>
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

#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtktreeview.h>
#include <libgnome/gnome-i18n.h>

#include "gossip-ui-utils.h"
#include "gossip-spell-dialog.h"

#define d(x)

struct _GossipSpellDialog {
	GtkWidget   *window;
	GtkWidget   *button_ok;
	GtkWidget   *label_word;
	GtkWidget   *treeview_words;

	GossipChat  *chat;
	GossipSpell *spell;

	gchar       *word;
	GtkTextIter start;
	GtkTextIter end;
};

typedef struct _GossipSpellDialog GossipSpellDialog;

enum {
	COL_SPELL_WORD,
	COL_SPELL_COUNT
};

static void spell_dialog_setup                (GossipSpellDialog *dialog);
static void spell_dialog_populate_columns     (GossipSpellDialog *dialog);
static void spell_dialog_populate_suggestions (GossipSpellDialog *dialog);
static void spell_dialog_selection_changed_cb (GtkTreeSelection  *treeselection,
					       GossipSpellDialog *dialog);
static void spell_dialog_response_cb          (GtkWidget         *widget,
					       gint               response,
					       GossipSpellDialog *dialog);
static void spell_dialog_destroy_cb           (GtkWidget         *widget,
					       GossipSpellDialog *dialog);


void
gossip_spell_dialog_show (GossipChat  *chat,
			  GossipSpell *spell,
			  GtkTextIter  start,
			  GtkTextIter  end,
			  const gchar *word)
{
	GossipSpellDialog *dialog;
	GladeXML          *gui;
	gchar             *str;

	g_return_if_fail (chat != NULL);
	g_return_if_fail (word != NULL);
	g_return_if_fail (strlen (word) > 0);

	dialog = g_new0 (GossipSpellDialog, 1);

	dialog->chat = g_object_ref (chat);
	dialog->spell = spell;

	dialog->word = g_strdup (word);

	dialog->start = start;
	dialog->end = end;

	gui = gossip_glade_get_file (GLADEDIR "/main.glade",
				     "spell_dialog",
				     NULL,
				     "spell_dialog", &dialog->window,
				     "button_ok", &dialog->button_ok,
				     "label_word", &dialog->label_word,
				     "treeview_words", &dialog->treeview_words,
				     NULL);

	gossip_glade_connect (gui,
			      dialog,
			      "spell_dialog", "response", spell_dialog_response_cb,
			      "spell_dialog", "destroy", spell_dialog_destroy_cb,
			      NULL);

	g_object_unref (gui);

	str = g_strdup_printf ("%s:\n<b>%s</b>", 
			       _("Suggestions for the word"),
			       word);

	gtk_label_set_markup (GTK_LABEL (dialog->label_word), str);
	g_free (str);
	
	spell_dialog_setup (dialog);
}

static void 
spell_dialog_setup (GossipSpellDialog *dialog)
{
	GtkListStore     *store;
	GtkTreeSelection *selection;

	store = gtk_list_store_new (COL_SPELL_COUNT,
				    G_TYPE_STRING);   /* word */
	
	gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->treeview_words), 
				 GTK_TREE_MODEL (store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (dialog->treeview_words));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	
	g_signal_connect (selection, "changed",
			  G_CALLBACK (spell_dialog_selection_changed_cb),
			  dialog);

	spell_dialog_populate_columns (dialog);
	spell_dialog_populate_suggestions (dialog);

	g_object_unref (store);
}

static void 
spell_dialog_populate_columns (GossipSpellDialog *dialog)
{
	GtkTreeModel      *model;
	GtkTreeViewColumn *column; 
	GtkCellRenderer   *renderer;
	guint              col_offset;
	
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview_words));
	
	renderer = gtk_cell_renderer_text_new ();
	col_offset = gtk_tree_view_insert_column_with_attributes (
		GTK_TREE_VIEW (dialog->treeview_words),
		-1, _("Word"),
		renderer, 
		"text", COL_SPELL_WORD,
		NULL);
	
	g_object_set_data (G_OBJECT (renderer),
			   "column", GINT_TO_POINTER (COL_SPELL_WORD));
	
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (dialog->treeview_words), col_offset - 1);
	gtk_tree_view_column_set_sort_column_id (column, COL_SPELL_WORD);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);
}

static void
spell_dialog_populate_suggestions (GossipSpellDialog *dialog)
{
	GossipChat   *chat;

	GList        *l;
	GList        *suggestions;

	GtkTreeModel *model;
	GtkListStore *store;

	chat = dialog->chat;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (dialog->treeview_words));
	store = GTK_LIST_STORE (model);

	suggestions = gossip_spell_suggestions (dialog->spell, dialog->word);

	if (!suggestions) {
		return;
	}

	for (l=suggestions; l; l=l->next) {
		GtkTreeIter  iter;
		gchar       *word;

		word = l->data;
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, COL_SPELL_WORD, word, -1);
	}

	g_list_foreach (suggestions, (GFunc)g_free, NULL);
	g_list_free (suggestions);
}

static void
spell_dialog_selection_changed_cb (GtkTreeSelection  *treeselection,
				   GossipSpellDialog *dialog)
{
	gint count;

	count = gtk_tree_selection_count_selected_rows (treeselection);
	gtk_widget_set_sensitive (dialog->button_ok, (count == 1));
}

static void
spell_dialog_destroy_cb (GtkWidget         *widget, 
			 GossipSpellDialog *dialog)
{
	g_object_unref (dialog->chat);
	g_free (dialog->word);
	
 	g_free (dialog); 
}

static void
spell_dialog_response_cb (GtkWidget         *widget,
			  gint               response, 
			  GossipSpellDialog *dialog)
{
	if (response == GTK_RESPONSE_OK) {
		GtkTreeView      *view;
		GtkTreeModel     *model;
		GtkTreeSelection *selection;
		GtkTreeIter       iter;

		gchar            *new_word;

		view = GTK_TREE_VIEW (dialog->treeview_words);
		selection = gtk_tree_view_get_selection (view);

		if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
			return;
		}
		
		gtk_tree_model_get (model, &iter, COL_SPELL_WORD, &new_word, -1);

		gossip_chat_correct_word (dialog->chat, 
					  dialog->start,
					  dialog->end,
					  new_word);
		
		g_free (new_word);
	}

	gtk_widget_destroy (dialog->window);
}

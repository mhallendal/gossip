/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2003-2007 Imendio AB
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib/gi18n.h>

#include <libgossip/gossip.h>

#include "gossip-app.h"
#include "gossip-glade.h"
#include "gossip-ui-utils.h"
#include "gossip-preferences.h"
#include "gossip-theme-manager.h"
#include "gossip-spell.h"
#include "gossip-contact-list.h"

typedef struct {
    GtkWidget *dialog;

    GtkWidget *notebook;

    GtkWidget *checkbutton_show_avatars;
    GtkWidget *checkbutton_compact_contact_list;
    GtkWidget *checkbutton_show_smileys;
    GtkWidget *checkbutton_separate_chat_windows;
    GtkWidget *radiobutton_contact_list_sort_by_name;
    GtkWidget *radiobutton_contact_list_sort_by_state;

    GtkWidget *checkbutton_sounds_for_messages;
    GtkWidget *checkbutton_sounds_when_busy;
    GtkWidget *checkbutton_sounds_when_away;
    GtkWidget *checkbutton_popups_when_available;

    GtkWidget *treeview_spell_checker;
    GtkWidget *checkbutton_spell_checker;

    GtkWidget *combobox_theme_chat;
    GtkWidget *checkbutton_theme_chat_rooms;
    GtkWidget *radiobutton_theme_chat_use_system_font;
    GtkWidget *radiobutton_theme_chat_use_other_font;
    GtkWidget *fontbutton_theme_chat;

    GList     *notify_ids;
} GossipPreferences;

static void     preferences_setup_widgets                (GossipPreferences      *preferences);
static void     preferences_languages_setup              (GossipPreferences      *preferences);
static void     preferences_languages_add                (GossipPreferences      *preferences);
static void     preferences_languages_save               (GossipPreferences      *preferences);
static gboolean preferences_languages_save_foreach       (GtkTreeModel           *model,
                                                          GtkTreePath            *path,
                                                          GtkTreeIter            *iter,
                                                          gchar                 **languages);
static void     preferences_languages_load               (GossipPreferences      *preferences);
static gboolean preferences_languages_load_foreach       (GtkTreeModel           *model,
                                                          GtkTreePath            *path,
                                                          GtkTreeIter            *iter,
                                                          gchar                 **languages);
static void     preferences_languages_cell_toggled_cb    (GtkCellRendererToggle  *cell,
                                                          gchar                  *path_string,
                                                          GossipPreferences      *preferences);
static void     preferences_themes_setup                 (GossipPreferences      *preferences);
static void     preferences_widget_sync_bool             (const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_widget_sync_int              (const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_widget_sync_string           (const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_widget_sync_string_combo     (const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_notify_int_cb                (GossipConf             *conf,
                                                          const gchar            *key,
                                                          gpointer                user_data);
static void     preferences_notify_string_cb             (GossipConf             *conf,
                                                          const gchar            *key,
                                                          gpointer                user_data);
static void     preferences_notify_string_combo_cb       (GossipConf             *conf,
                                                          const gchar            *key,
                                                          gpointer                user_data);
static void     preferences_notify_bool_cb               (GossipConf             *conf,
                                                          const gchar            *key,
                                                          gpointer                user_data);
static void     preferences_notify_sensitivity_cb        (GossipConf             *conf,
                                                          const gchar            *key,
                                                          gpointer                user_data);
G_GNUC_UNUSED static void     preferences_hookup_spin_button           (GossipPreferences      *preferences,
                                                                        const gchar            *key,
                                                                        GtkWidget              *widget);
G_GNUC_UNUSED static void     preferences_hookup_entry                 (GossipPreferences      *preferences,
                                                                        const gchar            *key,
                                                                        GtkWidget              *widget);
static void     preferences_hookup_toggle_button         (GossipPreferences      *preferences,
                                                          const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_hookup_radio_button          (GossipPreferences      *preferences,
                                                          const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_hookup_font_button           (GossipPreferences      *preferences,
                                                          const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_hookup_string_combo          (GossipPreferences      *preferences,
                                                          const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_hookup_sensitivity           (GossipPreferences      *preferences,
                                                          const gchar            *key,
                                                          GtkWidget              *widget);
static void     preferences_spin_button_value_changed_cb (GtkWidget              *button,
                                                          gpointer                user_data);
static void     preferences_entry_value_changed_cb       (GtkWidget              *entry,
                                                          gpointer                user_data);
static void     preferences_toggle_button_toggled_cb     (GtkWidget              *button,
                                                          gpointer                user_data);
static void     preferences_radio_button_toggled_cb      (GtkWidget              *button,
                                                          gpointer                user_data);
static void     preferences_font_button_changed_cb       (GtkWidget              *button,
                                                          gpointer                user_data);
static void     preferences_string_combo_changed_cb      (GtkWidget              *button,
                                                          gpointer                user_data);
static void     preferences_destroy_cb                   (GtkWidget              *widget,
                                                          GossipPreferences      *preferences);
static void     preferences_response_cb                  (GtkWidget              *widget,
                                                          gint                    response,
                                                          GossipPreferences      *preferences);

enum {
    COL_LANG_ENABLED,
    COL_LANG_CODE,
    COL_LANG_NAME,
    COL_LANG_COUNT
};

enum {
    COL_COMBO_VISIBLE_NAME,
    COL_COMBO_NAME,
    COL_COMBO_COUNT
};

static void
preferences_setup_widgets (GossipPreferences *preferences)
{
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_SOUNDS_FOR_MESSAGES,
                                      preferences->checkbutton_sounds_for_messages);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_SOUNDS_WHEN_AWAY,
                                      preferences->checkbutton_sounds_when_away);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_SOUNDS_WHEN_BUSY,
                                      preferences->checkbutton_sounds_when_busy);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_POPUPS_WHEN_AVAILABLE,
                                      preferences->checkbutton_popups_when_available);

    preferences_hookup_sensitivity (preferences,
                                    GOSSIP_PREFS_SOUNDS_FOR_MESSAGES,
                                    preferences->checkbutton_sounds_when_away);
    preferences_hookup_sensitivity (preferences,
                                    GOSSIP_PREFS_SOUNDS_FOR_MESSAGES,
                                    preferences->checkbutton_sounds_when_busy);

    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_UI_SEPARATE_CHAT_WINDOWS,
                                      preferences->checkbutton_separate_chat_windows);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_UI_SHOW_AVATARS,
                                      preferences->checkbutton_show_avatars);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_UI_COMPACT_CONTACT_LIST,
                                      preferences->checkbutton_compact_contact_list);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_CHAT_SHOW_SMILEYS,
                                      preferences->checkbutton_show_smileys);
    preferences_hookup_radio_button (preferences,
                                     GOSSIP_PREFS_CONTACTS_SORT_CRITERIUM,
                                     preferences->radiobutton_contact_list_sort_by_name);

    preferences_hookup_string_combo (preferences,
                                     GOSSIP_PREFS_CHAT_THEME,
                                     preferences->combobox_theme_chat);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_CHAT_THEME_CHAT_ROOM,
                                      preferences->checkbutton_theme_chat_rooms);
    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_CHAT_THEME_USE_SYSTEM_FONT,
                                      preferences->radiobutton_theme_chat_use_system_font);
    preferences_hookup_font_button (preferences,
                                    GOSSIP_PREFS_CHAT_THEME_FONT_NAME,
                                    preferences->fontbutton_theme_chat);

    preferences_hookup_toggle_button (preferences,
                                      GOSSIP_PREFS_CHAT_SPELL_CHECKER_ENABLED,
                                      preferences->checkbutton_spell_checker);
    preferences_hookup_sensitivity (preferences,
                                    GOSSIP_PREFS_CHAT_SPELL_CHECKER_ENABLED,
                                    preferences->treeview_spell_checker);
}

static void
preferences_languages_setup (GossipPreferences *preferences)
{
    GtkTreeView       *view;
    GtkListStore      *store;
    GtkTreeSelection  *selection;
    GtkTreeModel      *model;
    GtkTreeViewColumn *column;
    GtkCellRenderer   *renderer;
    guint              col_offset;

    view = GTK_TREE_VIEW (preferences->treeview_spell_checker);

    store = gtk_list_store_new (COL_LANG_COUNT,
                                G_TYPE_BOOLEAN,  /* enabled */
                                G_TYPE_STRING,   /* code */
                                G_TYPE_STRING);  /* name */

    gtk_tree_view_set_model (view, GTK_TREE_MODEL (store));

    selection = gtk_tree_view_get_selection (view);
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

    model = GTK_TREE_MODEL (store);

    renderer = gtk_cell_renderer_toggle_new ();
    g_signal_connect (renderer, "toggled",
                      G_CALLBACK (preferences_languages_cell_toggled_cb),
                      preferences);

    column = gtk_tree_view_column_new_with_attributes (NULL, renderer,
                                                       "active", COL_LANG_ENABLED,
                                                       NULL);

    gtk_tree_view_append_column (view, column);

    renderer = gtk_cell_renderer_text_new ();
    col_offset = gtk_tree_view_insert_column_with_attributes (view,
                                                              -1, _("Language"),
                                                              renderer,
                                                              "text", COL_LANG_NAME,
                                                              NULL);

    g_object_set_data (G_OBJECT (renderer),
                       "column", GINT_TO_POINTER (COL_LANG_NAME));

    column = gtk_tree_view_get_column (view, col_offset - 1);
    gtk_tree_view_column_set_sort_column_id (column, COL_LANG_NAME);
    gtk_tree_view_column_set_resizable (column, FALSE);
    gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

    g_object_unref (store);
}

static void
preferences_languages_add (GossipPreferences *preferences)
{
    GtkTreeView  *view;
    GtkListStore *store;
    GList        *codes, *l;

    view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
    store = GTK_LIST_STORE (gtk_tree_view_get_model (view));

    codes = gossip_spell_get_language_codes ();
    for (l = codes; l; l = l->next) {
        GtkTreeIter  iter;
        const gchar *code;
        const gchar *name;

        code = l->data;
        name = gossip_spell_get_language_name (code);
        if (!name) {
            continue;
        }

        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            COL_LANG_CODE, code,
                            COL_LANG_NAME, name,
                            -1);
    }

    g_list_foreach (codes, (GFunc) g_free, NULL);
    g_list_free (codes);
}

static void
preferences_languages_save (GossipPreferences *preferences)
{
    GtkTreeView       *view;
    GtkTreeModel      *model;

    gchar             *languages = NULL;

    view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
    model = gtk_tree_view_get_model (view);

    gtk_tree_model_foreach (model,
                            (GtkTreeModelForeachFunc) preferences_languages_save_foreach,
                            &languages);

    if (!languages) {
        /* Default to english */
        languages = g_strdup ("en");
    }

    gossip_conf_set_string (gossip_conf_get (),
                            GOSSIP_PREFS_CHAT_SPELL_CHECKER_LANGUAGES,
                            languages);

    g_free (languages);
}

static gboolean
preferences_languages_save_foreach (GtkTreeModel  *model,
                                    GtkTreePath   *path,
                                    GtkTreeIter   *iter,
                                    gchar        **languages)
{
    gboolean  enabled;
    gchar    *code;

    if (!languages) {
        return TRUE;
    }

    gtk_tree_model_get (model, iter, COL_LANG_ENABLED, &enabled, -1);
    if (!enabled) {
        return FALSE;
    }

    gtk_tree_model_get (model, iter, COL_LANG_CODE, &code, -1);
    if (!code) {
        return FALSE;
    }

    if (!(*languages)) {
        *languages = g_strdup (code);
    } else {
        gchar *str = *languages;
        *languages = g_strdup_printf ("%s,%s", str, code);
        g_free (str);
    }

    g_free (code);

    return FALSE;
}

static void
preferences_languages_load (GossipPreferences *preferences)
{
    GtkTreeView   *view;
    GtkTreeModel  *model;
    gchar         *value;
    gchar        **vlanguages;

    if (!gossip_conf_get_string (gossip_conf_get (),
                                 GOSSIP_PREFS_CHAT_SPELL_CHECKER_LANGUAGES,
                                 &value) || !value) {
        return;
    }

    vlanguages = g_strsplit (value, ",", -1);
    g_free (value);

    view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
    model = gtk_tree_view_get_model (view);

    gtk_tree_model_foreach (model,
                            (GtkTreeModelForeachFunc) preferences_languages_load_foreach,
                            vlanguages);

    g_strfreev (vlanguages);
}

static gboolean
preferences_languages_load_foreach (GtkTreeModel  *model,
                                    GtkTreePath   *path,
                                    GtkTreeIter   *iter,
                                    gchar        **languages)
{
    gchar    *code;
    gchar    *lang;
    gint      i;
    gboolean  found = FALSE;

    if (!languages) {
        return TRUE;
    }

    gtk_tree_model_get (model, iter, COL_LANG_CODE, &code, -1);
    if (!code) {
        return FALSE;
    }

    for (i = 0, lang = languages[i]; lang; lang = languages[++i]) {
        if (strcmp (lang, code) == 0) {
            found = TRUE;
        }
    }

    gtk_list_store_set (GTK_LIST_STORE (model), iter, COL_LANG_ENABLED, found, -1);
    return FALSE;
}

static void
preferences_languages_cell_toggled_cb (GtkCellRendererToggle *cell,
                                       gchar                 *path_string,
                                       GossipPreferences     *preferences)
{
    GtkTreeView  *view;
    GtkTreeModel *model;
    GtkListStore *store;
    GtkTreePath  *path;
    GtkTreeIter   iter;
    gboolean      enabled;

    view = GTK_TREE_VIEW (preferences->treeview_spell_checker);
    model = gtk_tree_view_get_model (view);
    store = GTK_LIST_STORE (model);

    path = gtk_tree_path_new_from_string (path_string);

    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model, &iter, COL_LANG_ENABLED, &enabled, -1);

    enabled ^= 1;

    gtk_list_store_set (store, &iter, COL_LANG_ENABLED, enabled, -1);
    gtk_tree_path_free (path);

    preferences_languages_save (preferences);
}

static void
preferences_themes_setup (GossipPreferences *preferences)
{
    GtkComboBox   *combo;
    GtkListStore  *model;
    GtkTreeIter    iter;
    const gchar  **themes;
    gint           i;

    combo = GTK_COMBO_BOX (preferences->combobox_theme_chat);

    model = gtk_list_store_new (COL_COMBO_COUNT,
                                G_TYPE_STRING,
                                G_TYPE_STRING);

    themes = gossip_theme_manager_get_themes ();
    for (i = 0; themes[i]; i += 2) {
        gtk_list_store_append (model, &iter);
        gtk_list_store_set (model, &iter,
                            COL_COMBO_VISIBLE_NAME, _(themes[i + 1]),
                            COL_COMBO_NAME, themes[i],
                            -1);
    }

    gtk_combo_box_set_model (combo, GTK_TREE_MODEL (model));
    g_object_unref (model);
}

static void
preferences_widget_sync_bool (const gchar *key, GtkWidget *widget)
{
    gboolean value;

    if (gossip_conf_get_bool (gossip_conf_get (), key, &value)) {
        if (GTK_IS_RADIO_BUTTON (widget)) {
            if (strcmp (key, GOSSIP_PREFS_CHAT_THEME_USE_SYSTEM_FONT) == 0) {
                GtkToggleButton *togglebutton;
                                
                if (!value) {
                    GSList *l;
                                        
                    l = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));

                    for (togglebutton = NULL; l && !togglebutton; l = l->next) {
                        if (GTK_WIDGET (l->data) == widget) {
                            continue;
                        } 

                        togglebutton = GTK_TOGGLE_BUTTON (l->data);
                    }
                } else {
                    togglebutton = GTK_TOGGLE_BUTTON (widget);
                }

                gtk_toggle_button_set_active (togglebutton, TRUE);
            }
        } else if (GTK_IS_TOGGLE_BUTTON (widget)) {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);
        } else {
            g_warning ("Unhandled key:'%s' just had string change", key);
        }
    }
}

static void
preferences_widget_sync_int (const gchar *key, GtkWidget *widget)
{
    gint value;

    if (gossip_conf_get_int (gossip_conf_get (), key, &value)) {
        if (GTK_IS_SPIN_BUTTON (widget)) {
            gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);
        }
    }
}

static void
preferences_widget_sync_string (const gchar *key, GtkWidget *widget)
{
    gchar *value;

    if (gossip_conf_get_string (gossip_conf_get (), key, &value) && value) {
        if (GTK_IS_ENTRY (widget)) {
            gtk_entry_set_text (GTK_ENTRY (widget), value);
        } else if (GTK_IS_FONT_BUTTON (widget)) {
            if (strcmp (key, GOSSIP_PREFS_CHAT_THEME_FONT_NAME) == 0) {
                gtk_font_button_set_font_name (GTK_FONT_BUTTON (widget), value);
            }
        } else if (GTK_IS_RADIO_BUTTON (widget)) {
            if (strcmp (key, GOSSIP_PREFS_CONTACTS_SORT_CRITERIUM) == 0) {
                GType        type;
                GEnumClass  *enum_class;
                GEnumValue  *enum_value;
                GSList      *list;
                GtkWidget   *toggle_widget;
                                
                /* Get index from new string */
                type = gossip_contact_list_sort_get_type ();
                enum_class = G_ENUM_CLASS (g_type_class_peek (type));
                enum_value = g_enum_get_value_by_nick (enum_class, value);
                                
                if (enum_value) { 
                    list = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
                    toggle_widget = g_slist_nth_data (list, enum_value->value);
                    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle_widget), TRUE);
                }
            } else {
                g_warning ("Unhandled key:'%s' just had string change", key);
            }
        }

        g_free (value);
    }
}

static void
preferences_widget_sync_string_combo (const gchar *key, GtkWidget *widget)
{
    gchar        *value;
    GtkTreeModel *model;
    GtkTreeIter   iter;
    gboolean      found;

    if (!gossip_conf_get_string (gossip_conf_get (), key, &value)) {
        return;
    }

    model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));

    found = FALSE;
    if (value && gtk_tree_model_get_iter_first (model, &iter)) {
        gchar *name;

        do {
            gtk_tree_model_get (model, &iter,
                                COL_COMBO_NAME, &name,
                                -1);

            if (strcmp (name, value) == 0) {
                found = TRUE;
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
                break;
            } else {
                found = FALSE;
            }

            g_free (name);
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    /* Fallback to the first one. */
    if (!found) {
        if (gtk_tree_model_get_iter_first (model, &iter)) {
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
        }
    }

    g_free (value);
}

static void
preferences_notify_int_cb (GossipConf  *conf,
                           const gchar *key,
                           gpointer     user_data)
{
    preferences_widget_sync_int (key, user_data);       
}

static void
preferences_notify_string_cb (GossipConf  *conf,
                              const gchar *key,
                              gpointer     user_data)
{
    preferences_widget_sync_string (key, user_data);
}

static void
preferences_notify_string_combo_cb (GossipConf  *conf,
                                    const gchar *key,
                                    gpointer     user_data)
{
    preferences_widget_sync_string_combo (key, user_data);
}

static void
preferences_notify_bool_cb (GossipConf  *conf,
                            const gchar *key,
                            gpointer     user_data)
{
    preferences_widget_sync_bool (key, user_data);
}

static void
preferences_notify_sensitivity_cb (GossipConf  *conf,
                                   const gchar *key,
                                   gpointer     user_data)
{
    gboolean value;

    if (gossip_conf_get_bool (conf, key, &value)) {
        gtk_widget_set_sensitive (GTK_WIDGET (user_data), value);
    }
}

static void
preferences_add_id (GossipPreferences *preferences, guint id)
{
    preferences->notify_ids = g_list_prepend (preferences->notify_ids,
                                              GUINT_TO_POINTER (id));
}

static void
preferences_hookup_spin_button (GossipPreferences *preferences,
                                const gchar       *key,
                                GtkWidget         *widget)
{
    guint id;

    preferences_widget_sync_int (key, widget);

    g_object_set_data_full (G_OBJECT (widget), "key",
                            g_strdup (key), g_free);

    g_signal_connect (widget,
                      "value_changed",
                      G_CALLBACK (preferences_spin_button_value_changed_cb),
                      NULL);

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_int_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_entry (GossipPreferences *preferences,
                          const gchar       *key,
                          GtkWidget         *widget)
{
    guint id;

    preferences_widget_sync_string (key, widget);

    g_object_set_data_full (G_OBJECT (widget), "key",
                            g_strdup (key), g_free);

    g_signal_connect (widget,
                      "changed",
                      G_CALLBACK (preferences_entry_value_changed_cb),
                      NULL);

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_string_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_toggle_button (GossipPreferences *preferences,
                                  const gchar       *key,
                                  GtkWidget         *widget)
{
    guint id;

    preferences_widget_sync_bool (key, widget);

    g_object_set_data_full (G_OBJECT (widget), "key",
                            g_strdup (key), g_free);

    g_signal_connect (widget,
                      "toggled",
                      G_CALLBACK (preferences_toggle_button_toggled_cb),
                      NULL);

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_bool_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_radio_button (GossipPreferences *preferences,
                                 const gchar       *key,
                                 GtkWidget         *widget)
{
    GSList *group, *l;
    guint   id;

    preferences_widget_sync_string (key, widget);

    group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (widget));
    for (l = group; l; l = l->next) {
        g_signal_connect (l->data,
                          "toggled",
                          G_CALLBACK (preferences_radio_button_toggled_cb),
                          NULL);

        g_object_set_data_full (G_OBJECT (l->data), "key",
                                g_strdup (key), g_free);
    }

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_string_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_string_combo (GossipPreferences *preferences,
                                 const gchar       *key,
                                 GtkWidget         *widget)
{
    guint id;

    preferences_widget_sync_string_combo (key, widget);

    g_object_set_data_full (G_OBJECT (widget), "key",
                            g_strdup (key), g_free);

    g_signal_connect (widget,
                      "changed",
                      G_CALLBACK (preferences_string_combo_changed_cb),
                      NULL);

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_string_combo_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_font_button (GossipPreferences *preferences,
                                const gchar       *key,
                                GtkWidget         *widget)
{
    guint id;

    preferences_widget_sync_string (key, widget);

    g_signal_connect (widget,
                      "font-set",
                      G_CALLBACK (preferences_font_button_changed_cb),
                      NULL);

    g_object_set_data_full (G_OBJECT (widget), "key",
                            g_strdup (key), g_free);

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_string_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_hookup_sensitivity (GossipPreferences *preferences,
                                const gchar       *key,
                                GtkWidget         *widget)
{
    gboolean value;
    guint    id;

    if (gossip_conf_get_bool (gossip_conf_get (), key, &value)) {
        gtk_widget_set_sensitive (widget, value);
    }

    id = gossip_conf_notify_add (gossip_conf_get (),
                                 key,
                                 preferences_notify_sensitivity_cb,
                                 widget);
    if (id) {
        preferences_add_id (preferences, id);
    }
}

static void
preferences_spin_button_value_changed_cb (GtkWidget *button,
                                          gpointer   user_data)
{
    const gchar *key;

    key = g_object_get_data (G_OBJECT (button), "key");

    gossip_conf_set_int (gossip_conf_get (),
                         key,
                         gtk_spin_button_get_value (GTK_SPIN_BUTTON (button)));
}

static void
preferences_entry_value_changed_cb (GtkWidget *entry,
                                    gpointer   user_data)
{
    const gchar *key;

    key = g_object_get_data (G_OBJECT (entry), "key");

    gossip_conf_set_string (gossip_conf_get (),
                            key,
                            gtk_entry_get_text (GTK_ENTRY (entry)));
}

static void
preferences_toggle_button_toggled_cb (GtkWidget *button,
                                      gpointer   user_data)
{
    const gchar *key;

    key = g_object_get_data (G_OBJECT (button), "key");

    gossip_conf_set_bool (gossip_conf_get (),
                          key,
                          gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)));
}

static void
preferences_radio_button_toggled_cb (GtkWidget *button,
                                     gpointer   user_data)
{
    const gchar *key;
    const gchar *value = NULL;

    key = g_object_get_data (G_OBJECT (button), "key");
    if (key) {
        if (strcmp (key, GOSSIP_PREFS_CONTACTS_SORT_CRITERIUM) == 0) {
            GSList      *group;
            GType        type;
            GEnumClass  *enum_class;
            GEnumValue  *enum_value;
        
            group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
                        
            /* Get string from index */
            type = gossip_contact_list_sort_get_type ();
            enum_class = G_ENUM_CLASS (g_type_class_peek (type));
            enum_value = g_enum_get_value (enum_class, g_slist_index (group, button));
                        
            if (!enum_value) {
                g_warning ("No GEnumValue for GossipContactListSort with GtkRadioButton index:%d", 
                           g_slist_index (group, button));
                return;
            }

            if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
                value = enum_value->value_nick;
            } else {
                /* If not active, we also get a callback for
                 * the other radio button in the group that
                 * has the right value, so ignore this.
                 */
                /* FIXME: Note that this hardcoding of keys
                 * if not how I meant this code to be used
                 * at all, should be fixed at some point.
                 */
                return;
            }
        } else if (strcmp (key, GOSSIP_PREFS_CHAT_THEME_USE_SYSTEM_FONT) == 0) {
            if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
                value = "TRUE";
            } else {
                value = "FALSE";
            }
        }
    }

    gossip_conf_set_string (gossip_conf_get (), key, value);
}

static void
preferences_font_button_changed_cb (GtkWidget *button,
                                    gpointer   user_data)
{
    const gchar *key;
    const gchar *font_name;
        
    key = g_object_get_data (G_OBJECT (button), "key");
    font_name = gtk_font_button_get_font_name (GTK_FONT_BUTTON (button));
    gossip_conf_set_string (gossip_conf_get (), key, font_name);
}

static void
preferences_string_combo_changed_cb (GtkWidget *combo,
                                     gpointer   user_data)
{
    const gchar  *key;
    GtkTreeModel *model;
    GtkTreeIter   iter;
    gchar        *name;

    key = g_object_get_data (G_OBJECT (combo), "key");

    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter)) {
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));

        gtk_tree_model_get (model, &iter,
                            COL_COMBO_NAME, &name,
                            -1);
        gossip_conf_set_string (gossip_conf_get (), key, name);
        g_free (name);
    }
}

static void
preferences_response_cb (GtkWidget         *widget,
                         gint               response,
                         GossipPreferences *preferences)
{
    gtk_widget_destroy (widget);
}

static void
preferences_destroy_cb (GtkWidget         *widget,
                        GossipPreferences *preferences)
{
    GList *l;

    for (l = preferences->notify_ids; l; l = l->next) {
        guint id;

        id = GPOINTER_TO_UINT (l->data);
        gossip_conf_notify_remove (gossip_conf_get (), id);
    }

    g_list_free (preferences->notify_ids);
    g_free (preferences);
}

void
gossip_preferences_show (void)
{
    static GossipPreferences *preferences;
    GladeXML                 *glade;

    if (preferences) {
        gtk_window_present (GTK_WINDOW (preferences->dialog));
        return;
    }

    preferences = g_new0 (GossipPreferences, 1);

    glade = gossip_glade_get_file (
        "main.glade",
        "preferences_dialog",
        NULL,
        "preferences_dialog", &preferences->dialog,
        "notebook", &preferences->notebook,
        "checkbutton_show_avatars", &preferences->checkbutton_show_avatars,
        "checkbutton_compact_contact_list", &preferences->checkbutton_compact_contact_list,
        "checkbutton_show_smileys", &preferences->checkbutton_show_smileys,
        "checkbutton_separate_chat_windows", &preferences->checkbutton_separate_chat_windows,
        "radiobutton_contact_list_sort_by_name", &preferences->radiobutton_contact_list_sort_by_name,
        "radiobutton_contact_list_sort_by_state", &preferences->radiobutton_contact_list_sort_by_state,
        "checkbutton_sounds_for_messages", &preferences->checkbutton_sounds_for_messages,
        "checkbutton_sounds_when_busy", &preferences->checkbutton_sounds_when_busy,
        "checkbutton_sounds_when_away", &preferences->checkbutton_sounds_when_away,
        "checkbutton_popups_when_available", &preferences->checkbutton_popups_when_available,
        "treeview_spell_checker", &preferences->treeview_spell_checker,
        "checkbutton_spell_checker", &preferences->checkbutton_spell_checker,
        "combobox_theme_chat", &preferences->combobox_theme_chat,
        "checkbutton_theme_chat_rooms", &preferences->checkbutton_theme_chat_rooms,
        "radiobutton_theme_chat_use_system_font", &preferences->radiobutton_theme_chat_use_system_font,
        "radiobutton_theme_chat_use_other_font", &preferences->radiobutton_theme_chat_use_other_font,
        "fontbutton_theme_chat", &preferences->fontbutton_theme_chat,
        NULL);

    gossip_glade_connect (glade,
                          preferences,
                          "preferences_dialog", "destroy", preferences_destroy_cb,
                          "preferences_dialog", "response", preferences_response_cb,
                          NULL);

    g_object_unref (glade);

    g_object_add_weak_pointer (G_OBJECT (preferences->dialog), (gpointer) &preferences);

    preferences_themes_setup (preferences);

    preferences_setup_widgets (preferences);

    preferences_languages_setup (preferences);
    preferences_languages_add (preferences);
    preferences_languages_load (preferences);

    if (gossip_spell_supported ()) {
        GtkWidget *page;

        page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (preferences->notebook), 2);
        gtk_widget_show (page);
    }

    gtk_window_set_transient_for (GTK_WINDOW (preferences->dialog),
                                  GTK_WINDOW (gossip_app_get_window ()));

    gtk_widget_show (preferences->dialog);
}

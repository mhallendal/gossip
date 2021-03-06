/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
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
#include <stdlib.h>

#include <glib/gi18n.h>
#include <glade/glade.h>

#include <libgossip/gossip.h>

#include "gossip-marshal.h"
#include "gossip-app.h"
#include "gossip-glade.h"
#include "gossip-ui-utils.h"
#include "gossip-presence-chooser.h"
#include "gossip-status-presets.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOSSIP_TYPE_PRESENCE_CHOOSER, GossipPresenceChooserPriv))

typedef struct {
    GtkWidget           *hbox;
    GtkWidget           *image;
    GtkWidget           *label;

    GtkWidget           *menu;

    GossipPresenceState  last_state;

    guint                heartbeat_id;

    GossipPresenceState  flash_state_1;
    GossipPresenceState  flash_state_2;

    /* The handle the kind of unnessecary scroll support. */
    guint                scroll_timeout_id;
    GossipPresenceState  scroll_state;
    gchar               *scroll_status;
} GossipPresenceChooserPriv;

static void     presence_chooser_finalize               (GObject               *object);
static void     presence_chooser_reset_scroll_timeout   (GossipPresenceChooser *chooser);
static void     presence_chooser_set_state              (GossipPresenceChooser *chooser,
                                                         GossipPresenceState    state,
                                                         const gchar           *status,
                                                         gboolean               save);
static void     presence_chooser_dialog_response_cb     (GtkWidget             *dialog,
                                                         gint                   response,
                                                         GossipPresenceChooser *chooser);
static void     presence_chooser_show_dialog            (GossipPresenceChooser *chooser,
                                                         GossipPresenceState    state);
static void     presence_chooser_custom_activate_cb     (GtkWidget             *item,
                                                         GossipPresenceChooser *chooser);
static void     presence_chooser_clear_response_cb      (GtkWidget             *widget,
                                                         gint                   response,
                                                         gpointer               user_data);
static void     presence_chooser_clear_activate_cb      (GtkWidget             *item,
                                                         GossipPresenceChooser *chooser);
static GtkWidget *
presence_chooser_menu_add_item          (GossipPresenceChooser *chooser,
                                         GtkWidget             *menu,
                                         const gchar           *str,
                                         GossipPresenceState    state,
                                         gint                   position,
                                         gboolean               custom);
static void     presence_chooser_menu_align_func        (GtkMenu               *menu,
                                                         gint                  *x,
                                                         gint                  *y,
                                                         gboolean              *push_in,
                                                         GossipPresenceChooser *chooser);
static void     presence_chooser_menu_selection_done_cb (GtkMenuShell          *menushell,
                                                         GossipPresenceChooser *chooser);
static void     presence_chooser_menu_detach            (GtkWidget             *attach_widget,
                                                         GtkMenu               *menu);
static void     presence_chooser_menu_popup             (GossipPresenceChooser *chooser);
static void     presence_chooser_menu_popdown           (GossipPresenceChooser *chooser);
static void     presence_chooser_toggled_cb             (GtkWidget             *chooser,
                                                         gpointer               user_data);
static gboolean presence_chooser_button_press_event_cb  (GtkWidget             *chooser,
                                                         GdkEventButton        *event,
                                                         gpointer               user_data);
static gboolean presence_chooser_scroll_event_cb        (GtkWidget             *chooser,
                                                         GdkEventScroll        *event,
                                                         gpointer               user_data);
static void     presence_chooser_size_allocate_cb       (GtkWidget             *chooser,
                                                         GtkAllocation         *allocation,
                                                         gpointer               user_data);
static gboolean presence_chooser_flash_heartbeat_func   (GossipHeartbeat       *heartbeat,
                                                         gpointer               user_data);
static void     presence_chooser_flash_start_cb         (GossipSelfPresence    *self_presence,
                                                         GossipPresenceChooser *chooser);
static void     presence_chooser_flash_stop_cb          (GossipSelfPresence    *self_presence,
                                                         GossipPresenceChooser *chooser);

G_DEFINE_TYPE (GossipPresenceChooser, gossip_presence_chooser, GTK_TYPE_TOGGLE_BUTTON);

enum {
    CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
gossip_presence_chooser_class_init (GossipPresenceChooserClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = presence_chooser_finalize;

    signals[CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      gossip_marshal_VOID__INT_STRING,
                      G_TYPE_NONE, 2,
                      G_TYPE_INT, G_TYPE_STRING);

    g_type_class_add_private (object_class, sizeof (GossipPresenceChooserPriv));
}

static void
gossip_presence_chooser_init (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;
    GtkWidget                 *arrow;
    GtkWidget                 *alignment;

    priv = GET_PRIV (chooser);

    gtk_button_set_relief (GTK_BUTTON (chooser), GTK_RELIEF_NONE);
    gtk_button_set_focus_on_click (GTK_BUTTON (chooser), FALSE);

    alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
    gtk_widget_show (alignment);
    gtk_container_add (GTK_CONTAINER (chooser), alignment);
    gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 1, 0);

    priv->hbox = gtk_hbox_new (FALSE, 1);
    gtk_widget_show (priv->hbox);
    gtk_container_add (GTK_CONTAINER (alignment), priv->hbox);

    priv->image = gtk_image_new ();
    gtk_widget_show (priv->image);
    gtk_box_pack_start (GTK_BOX (priv->hbox), priv->image, FALSE, TRUE, 0);

    priv->label = gtk_label_new (NULL);
    gtk_widget_show (priv->label);
    gtk_box_pack_start (GTK_BOX (priv->hbox), priv->label, TRUE, TRUE, 0);
    gtk_label_set_ellipsize (GTK_LABEL (priv->label), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment (GTK_MISC (priv->label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (priv->label), 4, 1);

    alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
    gtk_widget_show (alignment);
    gtk_box_pack_start (GTK_BOX (priv->hbox), alignment, FALSE, FALSE, 0);

    arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_OUT);
    gtk_widget_show (arrow);
    gtk_container_add (GTK_CONTAINER (alignment), arrow);

    g_signal_connect (chooser, "toggled",
                      G_CALLBACK (presence_chooser_toggled_cb),
                      NULL);
    g_signal_connect (chooser, "button-press-event",
                      G_CALLBACK (presence_chooser_button_press_event_cb),
                      NULL);
    g_signal_connect (chooser, "scroll-event",
                      G_CALLBACK (presence_chooser_scroll_event_cb),
                      NULL);
    g_signal_connect (chooser, "size-allocate",
                      G_CALLBACK (presence_chooser_size_allocate_cb),
                      NULL);

    g_signal_connect (gossip_app_get_self_presence (), "start-flash",
                      G_CALLBACK (presence_chooser_flash_start_cb),
                      chooser);

    g_signal_connect (gossip_app_get_self_presence (), "stop-flash",
                      G_CALLBACK (presence_chooser_flash_stop_cb),
                      chooser);
}

static void
presence_chooser_finalize (GObject *object)
{
    GossipPresenceChooserPriv *priv;

    priv = GET_PRIV (object);

    if (priv->heartbeat_id) {
        gossip_heartbeat_callback_remove (gossip_app_get_flash_heartbeat (), 
                                          priv->heartbeat_id);
    }

    if (priv->scroll_timeout_id) {
        g_source_remove (priv->scroll_timeout_id);
    }

    g_signal_handlers_disconnect_by_func (gossip_app_get_self_presence (),
                                          presence_chooser_flash_stop_cb,
                                          object);

    g_signal_handlers_disconnect_by_func (gossip_app_get_self_presence (),
                                          presence_chooser_flash_start_cb,
                                          object);

    G_OBJECT_CLASS (gossip_presence_chooser_parent_class)->finalize (object);
}

static void
presence_chooser_reset_scroll_timeout (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;

    priv = GET_PRIV (chooser);

    if (priv->scroll_timeout_id) {
        g_source_remove (priv->scroll_timeout_id);
        priv->scroll_timeout_id = 0;
    }

    g_free (priv->scroll_status);
    priv->scroll_status = NULL;
}

static void
presence_chooser_set_state (GossipPresenceChooser *chooser,
                            GossipPresenceState    state,
                            const gchar           *status,
                            gboolean               save)
{
    GossipPresenceChooserPriv *priv;
    const gchar               *default_status;

    priv = GET_PRIV (chooser);

    default_status = gossip_presence_state_get_default_status (state);

    if (G_STR_EMPTY (status)) {
        status = default_status;
    } else {
        /* Only store the value if it differs from the default ones. */
        if (save && strcmp (status, default_status) != 0) {
            gossip_status_presets_set_last (state, status);
        }
    }

    priv->last_state = state;

    gtk_widget_set_tooltip_text (GTK_WIDGET (chooser), status);

    presence_chooser_reset_scroll_timeout (chooser);
    g_signal_emit (chooser, signals[CHANGED], 0, state, status);
}

static void
presence_chooser_dialog_response_cb (GtkWidget             *dialog,
                                     gint                   response,
                                     GossipPresenceChooser *chooser)
{
    if (response == GTK_RESPONSE_OK) {
        GtkWidget           *entry;
        GtkWidget           *checkbutton;
        GtkListStore        *store;
        GtkTreeModel        *model;
        GtkTreeIter          iter;
        GossipPresenceState  state;
        const gchar         *status;
        gboolean             save;
        gboolean             duplicate = FALSE;
        gboolean             has_next;

        entry = g_object_get_data (G_OBJECT (dialog), "entry");
        status = gtk_entry_get_text (GTK_ENTRY (entry));
        store = g_object_get_data (G_OBJECT (dialog), "store");
        model = GTK_TREE_MODEL (store);

        has_next = gtk_tree_model_get_iter_first (model, &iter);
        while (has_next) {
            gchar *str;

            gtk_tree_model_get (model, &iter,
                                0, &str,
                                -1);

            if (strcmp (status, str) == 0) {
                g_free (str);
                duplicate = TRUE;
                break;
            }

            g_free (str);

            has_next = gtk_tree_model_iter_next (model, &iter);
        }

        if (!duplicate) {
            gtk_list_store_append (store, &iter);
            gtk_list_store_set (store, &iter, 0, status, -1);
        }

        checkbutton = g_object_get_data (G_OBJECT (dialog), "checkbutton");
        save = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton));
        state = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "state"));

        presence_chooser_set_state (chooser, state, status, save);
    }

    gtk_widget_destroy (dialog);
}

static void
presence_chooser_show_dialog (GossipPresenceChooser *chooser,
                              GossipPresenceState    state)
{
    GossipPresenceChooserPriv *priv;
    static GtkWidget          *dialog;
    static GtkListStore       *store[3] = { NULL, NULL, NULL };
    GladeXML                  *glade;
    GtkWidget                 *parent;
    GtkWidget                 *image;
    GtkWidget                 *combo;
    GtkWidget                 *entry;
    GtkWidget                 *checkbutton;
    GdkPixbuf                 *pixbuf;
    const gchar               *default_status;
    gboolean                   visible;

    priv = GET_PRIV (chooser);

    if (dialog) {
        gtk_widget_destroy (dialog);
        dialog = NULL;
    }

    glade = gossip_glade_get_file ("main.glade",
                                   "status_message_dialog",
                                   NULL,
                                   "status_message_dialog", &dialog,
                                   "comboentry_status", &combo,
                                   "image_status", &image,
                                   "checkbutton_add", &checkbutton,
                                   NULL);

    g_object_unref (glade);

    g_signal_connect (dialog, "destroy",
                      G_CALLBACK (gtk_widget_destroyed),
                      &dialog);
    g_signal_connect (dialog, "response",
                      G_CALLBACK (presence_chooser_dialog_response_cb),
                      chooser);

    pixbuf = gossip_pixbuf_for_presence_state (state);
    gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
    g_object_unref (pixbuf);

    if (!store[state]) {
        GList       *presets, *l;
        GtkTreeIter  iter;

        store[state] = gtk_list_store_new (1, G_TYPE_STRING);

        presets = gossip_status_presets_get (state, -1);
        for (l = presets; l; l = l->next) {
            gtk_list_store_append (store[state], &iter);
            gtk_list_store_set (store[state], &iter, 0, l->data, -1);
        }

        g_list_free (presets);
    }

    default_status = gossip_presence_state_get_default_status (state);

    entry = GTK_BIN (combo)->child;
    gtk_entry_set_text (GTK_ENTRY (entry), default_status);
    gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
    gtk_entry_set_width_chars (GTK_ENTRY (entry), 25);

    gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (store[state]));
    gtk_combo_box_entry_set_text_column (GTK_COMBO_BOX_ENTRY (combo), 0);

    /* We make the dialog transient to the app window if it's visible. */
    parent = gossip_app_get_window ();

    g_object_get (parent, "visible", &visible, NULL);

    visible = visible && !(gdk_window_get_state (parent->window) &
                           GDK_WINDOW_STATE_ICONIFIED);

    if (visible) {
        gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                      GTK_WINDOW (parent));
    }

    g_object_set_data (G_OBJECT (dialog), "store", store[state]);
    g_object_set_data (G_OBJECT (dialog), "entry", entry);
    g_object_set_data (G_OBJECT (dialog), "checkbutton", checkbutton);
    g_object_set_data (G_OBJECT (dialog), "state", GINT_TO_POINTER (state));

    gtk_widget_show_all (dialog);
}

static void
presence_chooser_custom_activate_cb (GtkWidget             *item,
                                     GossipPresenceChooser *chooser)
{
    GossipPresenceState state;

    state = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "state"));

    presence_chooser_show_dialog (chooser, state);
}

static void
presence_chooser_noncustom_activate_cb (GtkWidget             *item,
                                        GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;
    GossipPresenceState        state;
    const gchar               *status;

    priv = GET_PRIV (chooser);

    status = g_object_get_data (G_OBJECT (item), "status");
    state = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "state"));

    gtk_widget_set_tooltip_text (GTK_WIDGET (chooser), status);

    presence_chooser_reset_scroll_timeout (chooser);
    g_signal_emit (chooser, signals[CHANGED], 0, state, status);
}

static void
presence_chooser_clear_response_cb (GtkWidget *widget,
                                    gint       response,
                                    gpointer   user_data)
{
    if (response == GTK_RESPONSE_OK) {
        gossip_status_presets_reset ();
    }

    gtk_widget_destroy (widget);
}

static void
presence_chooser_clear_activate_cb (GtkWidget             *item,
                                    GossipPresenceChooser *chooser)
{
    GtkWidget *dialog;
    GtkWidget *toplevel;
    GtkWindow *parent = NULL;

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (chooser));
    if (gtk_widget_is_toplevel (toplevel) &&
        GTK_IS_WINDOW (toplevel)) {
        GtkWindow *window;
        gboolean   visible;

        window = GTK_WINDOW (toplevel);
        visible = gossip_window_get_is_visible (window);

        if (visible) {
            parent = window;
        }
    }

    dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
                                     0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
                                     _("Are you sure you want to clear the list?"));

    gtk_message_dialog_format_secondary_text (
        GTK_MESSAGE_DIALOG (dialog),
        _("This will remove any custom messages you have "
          "added to the list of preset status messages."));

    gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            _("Clear List"), GTK_RESPONSE_OK,
                            NULL);

    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), FALSE);

    g_signal_connect (dialog, "response",
                      G_CALLBACK (presence_chooser_clear_response_cb),
                      NULL);

    gtk_widget_show (dialog);
}

static GtkWidget *
presence_chooser_menu_add_item (GossipPresenceChooser *chooser,
                                GtkWidget             *menu,
                                const gchar           *str,
                                GossipPresenceState    state,
                                gint                   position,
                                gboolean               custom)
{
    GtkWidget   *item;
    GtkWidget   *image;
    const gchar *stock;

    item = gtk_image_menu_item_new_with_label (str);

    switch (state) {
    case GOSSIP_PRESENCE_STATE_AVAILABLE:
        stock = GOSSIP_STOCK_AVAILABLE;
        break;

    case GOSSIP_PRESENCE_STATE_BUSY:
        stock = GOSSIP_STOCK_BUSY;
        break;

    case GOSSIP_PRESENCE_STATE_AWAY:
        stock = GOSSIP_STOCK_AWAY;
        break;

    default:
        g_assert_not_reached ();
        stock = NULL;
        break;
    }

    if (custom) {
        g_signal_connect (
            item,
            "activate",
            G_CALLBACK (presence_chooser_custom_activate_cb),
            chooser);
    } else {
        g_signal_connect (
            item,
            "activate",
            G_CALLBACK (presence_chooser_noncustom_activate_cb),
            chooser);
    }

    image = gtk_image_new_from_stock (stock, GTK_ICON_SIZE_MENU);
    gtk_widget_show (image);

    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
    gtk_widget_show (item);

    g_object_set_data_full (G_OBJECT (item),
                            "status", g_strdup (str),
                            (GDestroyNotify) g_free);

    g_object_set_data (G_OBJECT (item), "state", GINT_TO_POINTER (state));

    if (position == -1) {
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    } else {
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, position);
    }

    return item;
}

static void
presence_chooser_menu_align_func (GtkMenu               *menu,
                                  gint                  *x,
                                  gint                  *y,
                                  gboolean              *push_in,
                                  GossipPresenceChooser *chooser)
{
    GtkWidget      *widget;
    GtkRequisition  req;
    GdkScreen      *screen;
    gint            screen_height;

    widget = GTK_WIDGET (chooser);

    gtk_widget_size_request (GTK_WIDGET (menu), &req);

    gdk_window_get_origin (widget->window, x, y);

    *x += widget->allocation.x + 1;
    *y += widget->allocation.y;

    screen = gtk_widget_get_screen (GTK_WIDGET (menu));
    screen_height = gdk_screen_get_height (screen);

    if (req.height > screen_height) {
        /* Too big for screen height anyway. */
        *y = 0;
        return;
    }

    if ((*y + req.height + widget->allocation.height) > screen_height) {
        /* Can't put it below the button. */
        *y -= req.height;
        *y += 1;
    } else {
        /* Put menu below button. */
        *y += widget->allocation.height;
        *y -= 1;
    }

    *push_in = FALSE;
}

static void
presence_chooser_menu_selection_done_cb (GtkMenuShell          *menushell,
                                         GossipPresenceChooser *chooser)
{
    gtk_widget_destroy (GTK_WIDGET (menushell));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chooser), FALSE);
}

static void
presence_chooser_menu_destroy_cb (GtkWidget             *menu,
                                  GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;

    priv = GET_PRIV (chooser);

    priv->menu = NULL;
}

static void
presence_chooser_menu_detach (GtkWidget *attach_widget,
                              GtkMenu   *menu)
{
    /* We don't need to do anything, but attaching the menu means
     * we don't own the ref count and it is cleaned up properly.
     */
}

static void
presence_chooser_menu_popup (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;
    GtkWidget                 *menu;

    priv = GET_PRIV (chooser);

    if (priv->menu) {
        return;
    }

    menu = gossip_presence_chooser_create_menu (chooser, TRUE, TRUE, -1);

    g_signal_connect_after (menu, "selection-done",
                            G_CALLBACK (presence_chooser_menu_selection_done_cb),
                            chooser);

    g_signal_connect (menu, "destroy",
                      G_CALLBACK (presence_chooser_menu_destroy_cb),
                      chooser);

    gtk_menu_attach_to_widget (GTK_MENU (menu),
                               GTK_WIDGET (chooser),
                               presence_chooser_menu_detach);

    gtk_menu_popup (GTK_MENU (menu),
                    NULL, NULL,
                    (GtkMenuPositionFunc) presence_chooser_menu_align_func,
                    chooser,
                    1,
                    gtk_get_current_event_time ());

    priv->menu = menu;
}

static void
presence_chooser_menu_popdown (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;

    priv = GET_PRIV (chooser);

    if (priv->menu) {
        gtk_widget_destroy (priv->menu);
    }
}

static void
presence_chooser_toggled_cb (GtkWidget *chooser,
                             gpointer   user_data)
{
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (chooser))) {
        presence_chooser_menu_popup (GOSSIP_PRESENCE_CHOOSER (chooser));
    } else {
        presence_chooser_menu_popdown (GOSSIP_PRESENCE_CHOOSER (chooser));
    }
}

static gboolean
presence_chooser_button_press_event_cb (GtkWidget      *chooser,
                                        GdkEventButton *event,
                                        gpointer        user_data)
{
    if (event->button != 1 || event->type != GDK_BUTTON_PRESS) {
        return FALSE;
    }

    if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (chooser))) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chooser), TRUE);
        return TRUE;
    }

    return FALSE;
}

typedef struct {
    GossipPresenceState  state;
    const gchar         *status;
} StateAndStatus;

static StateAndStatus *
presence_chooser_state_and_status_new (GossipPresenceState  state,
                                       const gchar         *status)
{
    StateAndStatus *sas;

    sas = g_new0 (StateAndStatus, 1);

    sas->state = state;
    sas->status = status;

    return sas;
}

static GList *
presence_chooser_get_presets (GossipPresenceChooser *chooser)
{
    GList          *list, *presets, *p;
    StateAndStatus *sas;

    list = NULL;

    sas = presence_chooser_state_and_status_new (
        GOSSIP_PRESENCE_STATE_AVAILABLE, _("Available"));
    list = g_list_append (list, sas);

    presets = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_AVAILABLE, 5);
    for (p = presets; p; p = p->next) {
        sas = presence_chooser_state_and_status_new (
            GOSSIP_PRESENCE_STATE_AVAILABLE, p->data);
        list = g_list_append (list, sas);
    }
    g_list_free (presets);

    sas = presence_chooser_state_and_status_new (
        GOSSIP_PRESENCE_STATE_BUSY, _("Busy"));
    list = g_list_append (list, sas);

    presets = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_BUSY, 5);
    for (p = presets; p; p = p->next) {
        sas = presence_chooser_state_and_status_new (
            GOSSIP_PRESENCE_STATE_BUSY, p->data);
        list = g_list_append (list, sas);
    }
    g_list_free (presets);

    sas = presence_chooser_state_and_status_new (
        GOSSIP_PRESENCE_STATE_AWAY, _("Away"));
    list = g_list_append (list, sas);

    presets = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_AWAY, 5);
    for (p = presets; p; p = p->next) {
        sas = presence_chooser_state_and_status_new (
            GOSSIP_PRESENCE_STATE_AWAY, p->data);
        list = g_list_append (list, sas);
    }
    g_list_free (presets);

    return list;
}

static gboolean
presence_chooser_scroll_timeout_cb (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;

    priv = GET_PRIV (chooser);

    g_signal_emit (chooser, signals[CHANGED], 0,
                   priv->scroll_state,
                   priv->scroll_status);

    priv->scroll_timeout_id = 0;

    g_free (priv->scroll_status);
    priv->scroll_status = NULL;

    return FALSE;
}

static void
presence_chooser_flash_start (GossipPresenceChooser *chooser,
                              GossipPresenceState    state_1,
                              GossipPresenceState    state_2)
{
    GossipPresenceChooserPriv *priv;

    g_return_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser));

    priv = GET_PRIV (chooser);

    if (priv->heartbeat_id) {
        /* Already blinking */
        return;
    }

    priv->flash_state_1 = state_1;
    priv->flash_state_2 = state_2;

    priv->heartbeat_id = gossip_heartbeat_callback_add (gossip_app_get_flash_heartbeat (),
                                                        presence_chooser_flash_heartbeat_func,
                                                        chooser);
}

static void
presence_chooser_flash_stop (GossipPresenceChooser *chooser,
                             GossipPresenceState    state)
{
    GossipPresenceChooserPriv *priv;
    GdkPixbuf                 *pixbuf;

    g_return_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser));

    priv = GET_PRIV (chooser);

    if (priv->heartbeat_id) {
        gossip_heartbeat_callback_remove (gossip_app_get_flash_heartbeat (),
                                          priv->heartbeat_id);
        priv->heartbeat_id = 0;
    }

    pixbuf = gossip_pixbuf_for_presence_state (state);
    gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image), pixbuf);
    g_object_unref (pixbuf);

    priv->last_state = state;
}

static void
presence_chooser_flash_start_cb (GossipSelfPresence    *self_presence,
                                 GossipPresenceChooser *chooser)
{
    presence_chooser_flash_start (chooser,
                                  gossip_self_presence_get_current_state (gossip_app_get_self_presence ()),
                                  gossip_self_presence_get_previous_state (gossip_app_get_self_presence ()));
        
}

static void
presence_chooser_flash_stop_cb (GossipSelfPresence    *self_presence,
                                GossipPresenceChooser *chooser)
{
    presence_chooser_flash_stop (chooser,
                                 gossip_self_presence_get_current_state (gossip_app_get_self_presence ()));
}

static gboolean
presence_chooser_scroll_event_cb (GtkWidget      *chooser,
                                  GdkEventScroll *event,
                                  gpointer        user_data)
{
    GossipPresenceChooserPriv *priv;
    GList                     *list, *l;
    const gchar               *current_status;
    StateAndStatus            *sas;
    gboolean                   match;

    priv = GET_PRIV (chooser);

    switch (event->direction) {
    case GDK_SCROLL_UP:
        break;
    case GDK_SCROLL_DOWN:
        break;
    default:
        return FALSE;
    }

    current_status = gtk_label_get_text (GTK_LABEL (priv->label));

    /* Get the list of presets, which in this context means all the items
     * without a trailing "...".
     */
    list = presence_chooser_get_presets (GOSSIP_PRESENCE_CHOOSER (chooser));
    sas = NULL;
    match = FALSE;
    for (l = list; l; l = l->next) {
        sas = l->data;

        if (sas->state == priv->last_state &&
            strcmp (sas->status, current_status) == 0) {
            sas = NULL;
            match = TRUE;
            if (event->direction == GDK_SCROLL_UP) {
                if (l->prev) {
                    sas = l->prev->data;
                }
            }
            else if (event->direction == GDK_SCROLL_DOWN) {
                if (l->next) {
                    sas = l->next->data;
                }
            }
            break;
        }

        sas = NULL;
    }

    if (sas) {
        presence_chooser_reset_scroll_timeout (GOSSIP_PRESENCE_CHOOSER (chooser));

        priv->scroll_status = g_strdup (sas->status);
        priv->scroll_state = sas->state;

        priv->scroll_timeout_id =
            g_timeout_add (500,
                           (GSourceFunc) presence_chooser_scroll_timeout_cb,
                           chooser);

        gossip_presence_chooser_set_status (GOSSIP_PRESENCE_CHOOSER (chooser),
                                            sas->status);
        gossip_presence_chooser_set_state (GOSSIP_PRESENCE_CHOOSER (chooser),
                                           sas->state);
    }
    else if (!match) {
        /* If we didn't get any match at all, it means the last state
         * was a custom one. Just switch to the first one.
         */
        presence_chooser_reset_scroll_timeout (GOSSIP_PRESENCE_CHOOSER (chooser));
        g_signal_emit (chooser, signals[CHANGED], 0,
                       GOSSIP_PRESENCE_STATE_AVAILABLE,
                       _("Available"));
    }

    g_list_foreach (list, (GFunc) g_free, NULL);
    g_list_free (list);

    return TRUE;
}

static void
presence_chooser_size_allocate_cb (GtkWidget     *chooser,
                                   GtkAllocation *allocation,
                                   gpointer       user_data)
{
    GossipPresenceChooserPriv *priv;
    PangoLayout               *layout;

    priv = GET_PRIV (chooser);

    layout = gtk_label_get_layout (GTK_LABEL (priv->label));

    if (pango_layout_is_ellipsized (layout)) {
        gtk_widget_set_has_tooltip (chooser, TRUE);
    } else {
        gtk_widget_set_has_tooltip (chooser, FALSE);
    }
}

GtkWidget *
gossip_presence_chooser_new (void)
{
    GtkWidget *chooser;

    chooser = g_object_new (GOSSIP_TYPE_PRESENCE_CHOOSER, NULL);

    return chooser;
}

GtkWidget *
gossip_presence_chooser_create_menu (GossipPresenceChooser *chooser,
                                     gint                   position,
                                     gboolean               sensitive,
                                     gboolean               include_clear)
{
    GtkWidget *menu;

    g_return_val_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser), NULL);

    menu = gtk_menu_new ();

    gossip_presence_chooser_insert_menu (chooser, 
                                         menu, 
                                         position,
                                         sensitive, 
                                         include_clear);

    return menu;
}

void
gossip_presence_chooser_insert_menu (GossipPresenceChooser *chooser,
                                     GtkWidget             *menu,
                                     gint                   position,
                                     gboolean               sensitive,
                                     gboolean               include_clear)
{
    GtkWidget *item;
    GList     *list, *l;
    gint       i;

    g_return_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser));
    g_return_if_fail (GTK_IS_MENU (menu));

    i = position;

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Available"),
                                           GOSSIP_PRESENCE_STATE_AVAILABLE,
                                           i++,
                                           FALSE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }

    list = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_AVAILABLE, 5);
    for (l = list; l; l = l->next) {
        item = presence_chooser_menu_add_item (chooser,
                                               menu,
                                               l->data,
                                               GOSSIP_PRESENCE_STATE_AVAILABLE,
                                               i++,
                                               FALSE);
        if (!sensitive) {
            gtk_widget_set_sensitive (item, FALSE);
        }
    }

    g_list_free (list);

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Custom message..."),
                                           GOSSIP_PRESENCE_STATE_AVAILABLE,
                                           i++,
                                           TRUE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }


    /* Separator. */
    item = gtk_menu_item_new ();

    if (position == -1) {
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    } else {
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, i++);
    }

    gtk_widget_show (item);

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Busy"),
                                           GOSSIP_PRESENCE_STATE_BUSY,
                                           i++,
                                           FALSE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }
        
    list = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_BUSY, 5);
    for (l = list; l; l = l->next) {
        item = presence_chooser_menu_add_item (chooser,
                                               menu,
                                               l->data,
                                               GOSSIP_PRESENCE_STATE_BUSY,
                                               i++,
                                               FALSE);
        if (!sensitive) {
            gtk_widget_set_sensitive (item, FALSE);
        }
    }

    g_list_free (list);

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Custom message..."),
                                           GOSSIP_PRESENCE_STATE_BUSY,
                                           i++,
                                           TRUE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }

    /* Separator. */
    item = gtk_menu_item_new ();

    if (position == -1) {
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    } else {
        gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, i++);
    }

    gtk_widget_show (item);

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Away"),
                                           GOSSIP_PRESENCE_STATE_AWAY,
                                           i++,
                                           FALSE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }

    list = gossip_status_presets_get (GOSSIP_PRESENCE_STATE_AWAY, 5);
    for (l = list; l; l = l->next) {
        item = presence_chooser_menu_add_item (chooser,
                                               menu,
                                               l->data,
                                               GOSSIP_PRESENCE_STATE_AWAY,
                                               i++,
                                               FALSE);
        if (!sensitive) {
            gtk_widget_set_sensitive (item, FALSE);
        }
    }

    g_list_free (list);

    item = presence_chooser_menu_add_item (chooser,
                                           menu,
                                           _("Custom message..."),
                                           GOSSIP_PRESENCE_STATE_AWAY,
                                           i++,
                                           TRUE);
    if (!sensitive) {
        gtk_widget_set_sensitive (item, FALSE);
    }

    if (include_clear) {
        /* Separator. */
        item = gtk_menu_item_new ();

        if (position == -1) {
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        } else {
            gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, i++);
        }

        gtk_widget_show (item);
                
        item = gtk_menu_item_new_with_label (_("Clear List..."));

        if (position == -1) {
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        } else {
            gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, i++);
        }

        if (!sensitive) {
            gtk_widget_set_sensitive (item, FALSE);
        }

        gtk_widget_show (item);
                
        g_signal_connect (item,
                          "activate",
                          G_CALLBACK (presence_chooser_clear_activate_cb),
                          chooser);
    }
}

void
gossip_presence_chooser_set_state (GossipPresenceChooser *chooser,
                                   GossipPresenceState    state)
{
    GossipPresenceChooserPriv *priv;

    g_return_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser));

    priv = GET_PRIV (chooser);

    presence_chooser_flash_stop (chooser, state);
}

void
gossip_presence_chooser_set_status (GossipPresenceChooser *chooser,
                                    const gchar           *status)
{
    GossipPresenceChooserPriv *priv;

    g_return_if_fail (GOSSIP_IS_PRESENCE_CHOOSER (chooser));

    priv = GET_PRIV (chooser);

    gtk_label_set_text (GTK_LABEL (priv->label), status);

    gtk_widget_set_tooltip_text (GTK_WIDGET (chooser), status);
}

static void
presence_chooser_flash (GossipPresenceChooser *chooser)
{
    GossipPresenceChooserPriv *priv;
    GossipPresenceState        state;
    GdkPixbuf                 *pixbuf;
    static gboolean            on = FALSE;

    priv = GET_PRIV (chooser);

    if (!on) {
        state = priv->flash_state_1;
    } else {
        state = priv->flash_state_2;
    }

    pixbuf = gossip_pixbuf_for_presence_state (state);
    gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image), pixbuf);
    g_object_unref (pixbuf);

    on = !on;
}

static gboolean
presence_chooser_flash_heartbeat_func (GossipHeartbeat *heartbeat,
                                       gpointer         user_data)
{
    presence_chooser_flash (GOSSIP_PRESENCE_CHOOSER (user_data));
        
    return TRUE;
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2006 Imendio AB
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
#include <gtk/gtk.h>

#include <libgossip/gossip-debug.h>
#include <libgossip/gossip-log.h>
#include <libgossip/gossip-session.h>

#include "gossip-app.h"
#include "gossip-cell-renderer-expander.h"
#include "gossip-cell-renderer-text.h"
#include "gossip-chat-invite.h"
#include "gossip-contact-groups.h"
#include "gossip-contact-info-dialog.h"
#include "gossip-contact-list.h"
#include "gossip-edit-contact-dialog.h"
#include "gossip-ft-window.h"
#include "gossip-log-window.h"
#include "gossip-marshal.h"
#include "gossip-sound.h"
#include "gossip-stock.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                       GOSSIP_TYPE_CONTACT_LIST, GossipContactListPriv))

#define DEBUG_DOMAIN "ContactList"

/* Flashing delay for icons (milliseconds). */
#define FLASH_TIMEOUT 500

/* Active users are those which have recently changed state
 * (e.g. online, offline or from normal to a busy state).
 */

/* Time user is shown as active */
#define ACTIVE_USER_SHOW_TIME 7000

/* Time after connecting which we wait before active users are enabled */
#define ACTIVE_USER_WAIT_TO_ENABLE_TIME 5000

struct _GossipContactListPriv {
	GossipSession       *session;

	GHashTable          *groups;
	GHashTable          *flash_table;

	GtkUIManager        *ui;

	GtkTreeRowReference *drag_row;

	gboolean             show_offline;
	gboolean             show_avatars;
	gboolean             is_compact;
	gboolean             show_active;
};

typedef struct {
	gint            event_id;
	GossipEventType event_type;
	gboolean        flash_on;
	guint           flash_timeout_id;
} FlashData;

typedef struct {
	GossipContactList *list;
	GossipContact     *contact;
} FlashTimeoutData;

typedef struct {
	const gchar *name;
	gboolean     found;
	GtkTreeIter  iter;
} FindGroup;

typedef struct {
	GossipContact *contact;
	gboolean       found;
	GList         *iters;
} FindContact;

typedef struct {
	GossipContactList *list;
	GtkTreePath       *path;
	guint              timeout_id;
} DragMotionData;

typedef struct {
	GossipContactList *list;
	GossipContact     *contact;
	gboolean           remove;
} ShowActiveData;

static void     gossip_contact_list_class_init               (GossipContactListClass *klass);
static void     gossip_contact_list_init                     (GossipContactList      *list);
static void     contact_list_finalize                        (GObject                *object);
static void     contact_list_get_property                    (GObject                *object,
							      guint                   param_id,
							      GValue                 *value,
							      GParamSpec             *pspec);
static void     contact_list_set_property                    (GObject                *object,
							      guint                   param_id,
							      const GValue           *value,
							      GParamSpec             *pspec);
static void     contact_list_connected_cb                    (GossipSession          *session,
							      GossipContactList      *list);
static gboolean contact_list_show_active_users_cb            (GossipContactList      *list);
static void     contact_list_contact_update                  (GossipContactList      *list,
							      GossipContact          *contact);
static void     contact_list_contact_added_cb                (GossipSession          *session,
							      GossipContact          *contact,
							      GossipContactList      *list);
static void     contact_list_contact_updated_cb              (GossipContact          *contact,
							      GParamSpec             *param,
							      GossipContactList      *list);
static void     contact_list_contact_groups_updated_cb       (GossipContact          *contact,
							      GParamSpec             *param,
							      GossipContactList      *list);
static void     contact_list_contact_removed_cb              (GossipSession          *session,
							      GossipContact          *contact,
							      GossipContactList      *list);
static void     contact_list_contact_composing_cb            (GossipSession          *session,
							      GossipContact          *contact,
							      gboolean                composing,
							      GossipContactList      *list);
static void     contact_list_contact_set_active              (GossipContactList      *list,
							      GossipContact          *contact,
							      gboolean                active,
							      gboolean                set_changed);
static ShowActiveData *
		contact_list_contact_active_new              (GossipContactList      *list,
							      GossipContact          *contact,
							      gboolean                remove);
static void     contact_list_contact_active_free             (ShowActiveData         *data);
static gboolean contact_list_contact_active_cb               (ShowActiveData         *data);
static gchar *  contact_list_get_parent_group                (GtkTreeModel           *model,
							      GtkTreePath            *path,
							      gboolean               *path_is_group);
static void     contact_list_get_group                       (GossipContactList      *list,
							      const gchar            *name,
							      GtkTreeIter            *iter_to_set,
							      gboolean               *created);
static gboolean contact_list_get_group_foreach               (GtkTreeModel           *model,
							      GtkTreePath            *path,
							      GtkTreeIter            *iter,
							      FindGroup              *fg);
static void     contact_list_add_contact                     (GossipContactList      *list,
							      GossipContact          *contact);
static void     contact_list_remove_contact                  (GossipContactList      *list,
							      GossipContact          *contact,
							      gboolean                remove_flash);
static void     contact_list_create_model                    (GossipContactList      *list);
static gboolean contact_list_search_equal_func               (GtkTreeModel           *model,
							      gint                    column,
							      const gchar            *key,
							      GtkTreeIter            *iter,
							      gpointer                search_data);
static void     contact_list_setup_view                      (GossipContactList      *list);
static void     contact_list_drag_data_received              (GtkWidget              *widget,
							      GdkDragContext         *context,
							      gint                    x,
							      gint                    y,
							      GtkSelectionData       *selection,
							      guint                   info,
							      guint                   time,
							      gpointer                user_data);
static gboolean contact_list_drag_motion                     (GtkWidget              *widget,
							      GdkDragContext         *context,
							      gint                    x,
							      gint                    y,
							      guint                   time,
							      gpointer                data);
static gboolean contact_list_drag_motion_cb                  (DragMotionData         *data);
static void     contact_list_drag_begin                      (GtkWidget              *widget,
							      GdkDragContext         *context,
							      gpointer                user_data);
static void     contact_list_drag_data_get                   (GtkWidget              *widget,
							      GdkDragContext         *contact,
							      GtkSelectionData       *selection,
							      guint                   info,
							      guint                   time,
							      gpointer                user_data);
static void     contact_list_drag_end                        (GtkWidget              *widget,
							      GdkDragContext         *context,
							      gpointer                user_data);
static void     contact_list_cell_set_background             (GossipContactList      *list,
							      GtkCellRenderer        *cell,
							      gboolean                is_group,
							      gboolean                is_active);
static void     contact_list_pixbuf_cell_data_func           (GtkTreeViewColumn      *tree_column,
							      GtkCellRenderer        *cell,
							      GtkTreeModel           *model,
							      GtkTreeIter            *iter,
							      GossipContactList      *list);
static void     contact_list_avatar_cell_data_func           (GtkTreeViewColumn      *tree_column,
							      GtkCellRenderer        *cell,
							      GtkTreeModel           *model,
							      GtkTreeIter            *iter,
							      GossipContactList      *list);
static void     contact_list_text_cell_data_func             (GtkTreeViewColumn      *tree_column,
							      GtkCellRenderer        *cell,
							      GtkTreeModel           *model,
							      GtkTreeIter            *iter,
							      GossipContactList      *list);
static void     contact_list_expander_cell_data_func         (GtkTreeViewColumn      *column,
							      GtkCellRenderer        *cell,
							      GtkTreeModel           *model,
							      GtkTreeIter            *iter,
							      GossipContactList      *list);
static GtkWidget *contact_list_get_contact_menu              (GossipContactList      *list,
							      gboolean                can_send_file,
							      gboolean                can_show_log);
static gboolean contact_list_button_press_event_cb           (GossipContactList      *list,
							      GdkEventButton         *event,
							      gpointer                user_data);
static void     contact_list_row_activated_cb                (GossipContactList      *list,
							      GtkTreePath            *path,
							      GtkTreeViewColumn      *col,
							      gpointer                user_data);
static void     contact_list_row_expand_or_collapse_cb       (GossipContactList      *list,
							      GtkTreeIter            *iter,
							      GtkTreePath            *path,
							      gpointer                user_data);
static gint     contact_list_sort_func                       (GtkTreeModel           *model,
							      GtkTreeIter            *iter_a,
							      GtkTreeIter            *iter_b,
							      gpointer                user_data);
static GList *  contact_list_find_contact                    (GossipContactList      *list,
							      GossipContact          *contact);
static gboolean contact_list_find_contact_foreach            (GtkTreeModel           *model,
							      GtkTreePath            *path,
							      GtkTreeIter            *iter,
							      FindContact            *fc);
static void     contact_list_action_cb                       (GtkAction              *action,
							      GossipContactList      *list);
static void     contact_list_action_activated                (GossipContactList      *list,
							      GossipContact          *contact);
static void     contact_list_action_entry_activate_cb        (GtkWidget              *entry,
							      GtkDialog              *dialog);
static void     contact_list_action_remove_response_cb       (GtkWidget              *dialog,
							      gint                    response,
							      GossipContactList      *list);
static void     contact_list_action_remove_selected          (GossipContactList      *list);
static void     contact_list_action_invite_selected          (GossipContactList      *list);
static void     contact_list_action_rename_group_selected    (GossipContactList      *list);
static void     contact_list_free_flash_timeout_data         (FlashTimeoutData       *timeout_data);
static void     contact_list_flash_free_data                 (FlashData              *data);
static gboolean contact_list_flash_timeout_func              (FlashTimeoutData       *timeout_data);
static void     contact_list_event_added_cb                  (GossipEventManager     *manager,
							      GossipEvent            *event,
							      GossipContactList      *list);
static void     contact_list_event_removed_cb                (GossipEventManager     *manager,
							      GossipEvent            *event,
							      GossipContactList      *list);
static gboolean contact_list_update_list_mode_foreach        (GtkTreeModel           *model,
							      GtkTreePath            *path,
							      GtkTreeIter            *iter,
							      GossipContactList      *list);

enum {
	CONTACT_ACTIVATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

enum {
	COL_PIXBUF_STATUS,
	COL_PIXBUF_AVATAR,
	COL_PIXBUF_AVATAR_VISIBLE,
	COL_NAME,
	COL_STATUS,
	COL_STATUS_VISIBLE,
	COL_CONTACT,
	COL_IS_GROUP,
	COL_IS_ACTIVE,
	COL_IS_ONLINE,
	COL_IS_COMPOSING,
	COL_COUNT
};

enum {
	PROP_0,
	PROP_SHOW_OFFLINE,
	PROP_SHOW_AVATARS,
	PROP_IS_COMPACT,
};

static const GtkActionEntry entries[] = {
	{ "ContactMenu", NULL,
	  N_("_Contact"), NULL, NULL,
	  NULL
	},
	{ "GroupMenu", NULL,
	  N_("_Group"),NULL, NULL,
	  NULL
	},
	{ "Chat", GOSSIP_STOCK_MESSAGE,
	  N_("_Chat"), NULL, N_("Chat with contact"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Information", GOSSIP_STOCK_CONTACT_INFORMATION,
	  N_("Infor_mation"), "<control>I", N_("View contact information"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Rename", NULL,
	  N_("Re_name"), NULL, N_("Rename"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Edit", GTK_STOCK_EDIT,
	  N_("_Edit"), NULL, N_("Edit the groups and name for this contact"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Remove", GTK_STOCK_REMOVE,
	  N_("_Remove"), NULL, N_("Remove contact"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Invite", GOSSIP_STOCK_GROUP_MESSAGE,
	  N_("_Invite to Chat Room"), NULL, N_("Invite to a currently open chat room"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "SendFile", NULL,
	  N_("_Send File..."), NULL, N_("Send a file"),
	  G_CALLBACK (contact_list_action_cb)
	},
	{ "Log", GTK_STOCK_JUSTIFY_LEFT,
	  N_("_View Previous Conversations"), NULL, N_("View previous conversations with this contact"),
	  G_CALLBACK (contact_list_action_cb)
	},
};

static guint n_entries = G_N_ELEMENTS (entries);

static const gchar *ui_info =
	"<ui>"
	"  <popup name='Contact'>"
	"    <menuitem action='Chat'/>"
	"    <menuitem action='Log'/>"
	"    <menuitem action='SendFile'/>"
	"    <separator/>"
	"    <menuitem action='Invite'/>"
	"    <separator/>"
	"    <menuitem action='Edit'/>"
	"    <menuitem action='Remove'/>"
	"    <separator/>"
	"    <menuitem action='Information'/>"
	"  </popup>"
	"  <popup name='Group'>"
	"    <menuitem action='Rename'/>"
	"  </popup>"
	"</ui>";

enum DndDragType {
	DND_DRAG_TYPE_CONTACT_ID,
	DND_DRAG_TYPE_URL,
	DND_DRAG_TYPE_STRING,
};

static const GtkTargetEntry drag_types_dest[] = {
	{ "text/contact-id", 0, DND_DRAG_TYPE_CONTACT_ID },
	{ "text/uri-list",   0, DND_DRAG_TYPE_URL },
	{ "text/plain",      0, DND_DRAG_TYPE_STRING },
	{ "STRING",          0, DND_DRAG_TYPE_STRING },
};

static const GtkTargetEntry drag_types_source[] = {
	{ "text/contact-id", 0, DND_DRAG_TYPE_CONTACT_ID },
};

static GdkAtom drag_atoms_dest[G_N_ELEMENTS (drag_types_dest)];
static GdkAtom drag_atoms_source[G_N_ELEMENTS (drag_types_source)];

G_DEFINE_TYPE (GossipContactList, gossip_contact_list, GTK_TYPE_TREE_VIEW);

static void
gossip_contact_list_class_init (GossipContactListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = contact_list_finalize;
	object_class->get_property = contact_list_get_property;
	object_class->set_property = contact_list_set_property;

	signals[CONTACT_ACTIVATED] =
		g_signal_new ("contact-activated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      gossip_marshal_VOID__OBJECT_INT,
			      G_TYPE_NONE,
			      2, GOSSIP_TYPE_CONTACT, G_TYPE_INT);

	g_object_class_install_property (object_class,
					 PROP_SHOW_OFFLINE,
					 g_param_spec_boolean ("show-offline",
							       "Show Offline",
							       "Whether contact list should display "
							       "offline contacts",
							       FALSE,
							       G_PARAM_READWRITE));

	 g_object_class_install_property (object_class,
					  PROP_SHOW_AVATARS,
					  g_param_spec_boolean ("show-avatars",
								"Show Avatars",
								"Whether contact list should display "
								"avatars for contacts",
								TRUE,
								G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_IS_COMPACT,
					 g_param_spec_boolean ("is-compact",
							       "Is Compact",
							       "Whether the contact list is in compact mode or not",
							       FALSE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GossipContactListPriv));
}

static void
gossip_contact_list_init (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GossipSession         *session;
	GtkActionGroup        *action_group;
	GError                *error = NULL;
#if 0
	GtkWidget             *window;
#endif

	session = gossip_app_get_session ();

	priv = GET_PRIV (list);

	priv->session = g_object_ref (session);

	priv->is_compact = FALSE;

	priv->flash_table = g_hash_table_new_full (gossip_contact_hash,
						   gossip_contact_equal,
						   (GDestroyNotify) g_object_unref,
						   (GDestroyNotify) contact_list_flash_free_data);

	contact_list_create_model (list);
	contact_list_setup_view (list);

	/* Get saved group states. */
	gossip_contact_groups_get_all ();

	/* Set up UI Manager */
	priv->ui = gtk_ui_manager_new ();

	action_group = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (action_group, entries, n_entries, list);
	gtk_ui_manager_insert_action_group (priv->ui, action_group, 0);

#if 0
	/* FIXME: This errors */
	window = gtk_widget_get_ancestor (GTK_WIDGET (list), GTK_TYPE_WINDOW);
	if (window) {
		gtk_window_add_accel_group (GTK_WINDOW (window),
					    gtk_ui_manager_get_accel_group (priv->ui));
	}
#endif

	if (!gtk_ui_manager_add_ui_from_string (priv->ui, ui_info, -1, &error)) {
		g_warning ("Could not build contact menus from string:'%s'", error->message);
		g_error_free (error);
	}

	g_object_unref (action_group);

	/* Signal connection. */
	g_signal_connect (priv->session,
			  "connected",
			  G_CALLBACK (contact_list_connected_cb),
			  list);
	g_signal_connect (priv->session,
			  "contact-added",
			  G_CALLBACK (contact_list_contact_added_cb),
			  list);
	g_signal_connect (priv->session,
			  "contact-removed",
			  G_CALLBACK (contact_list_contact_removed_cb),
			  list);
	g_signal_connect (priv->session,
			  "composing",
			  G_CALLBACK (contact_list_contact_composing_cb),
			  list);

	/* Connect to event manager signals. */
	g_signal_connect (gossip_app_get_event_manager (),
			  "event-added",
			  G_CALLBACK (contact_list_event_added_cb),
			  list);
	g_signal_connect (gossip_app_get_event_manager (),
			  "event-removed",
			  G_CALLBACK (contact_list_event_removed_cb),
			  list);

	/* Connect to tree view signals rather than override. */
	g_signal_connect (list,
			  "button-press-event",
			  G_CALLBACK (contact_list_button_press_event_cb),
			  NULL);
	g_signal_connect (list,
			  "row-activated",
			  G_CALLBACK (contact_list_row_activated_cb),
			  NULL);
	g_signal_connect (list,
			  "row-expanded",
			  G_CALLBACK (contact_list_row_expand_or_collapse_cb),
			  GINT_TO_POINTER (TRUE));
	g_signal_connect (list,
			  "row-collapsed",
			  G_CALLBACK (contact_list_row_expand_or_collapse_cb),
			  GINT_TO_POINTER (FALSE));
}

static void
contact_list_finalize (GObject *object)
{
	GossipContactListPriv *priv;

	priv = GET_PRIV (object);

	g_object_unref (priv->session);

	g_hash_table_destroy (priv->flash_table);

	g_object_unref (priv->ui);

	G_OBJECT_CLASS (gossip_contact_list_parent_class)->finalize (object);
}

static void
contact_list_get_property (GObject    *object,
			   guint       param_id,
			   GValue     *value,
			   GParamSpec *pspec)
{
	GossipContactListPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_SHOW_OFFLINE:
		g_value_set_boolean (value, priv->show_offline);
		break;
	case PROP_SHOW_AVATARS:
		g_value_set_boolean (value, priv->show_avatars);
		break;
	case PROP_IS_COMPACT:
		g_value_set_boolean (value, priv->is_compact);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
contact_list_set_property (GObject      *object,
			   guint         param_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
	GossipContactListPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_SHOW_OFFLINE:
		gossip_contact_list_set_show_offline (GOSSIP_CONTACT_LIST (object),
						      g_value_get_boolean (value));
		break;
	case PROP_SHOW_AVATARS:
		gossip_contact_list_set_show_avatars (GOSSIP_CONTACT_LIST (object),
						      g_value_get_boolean (value));
		break;
	case PROP_IS_COMPACT:
		gossip_contact_list_set_is_compact (GOSSIP_CONTACT_LIST (object),
						    g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
contact_list_connected_cb (GossipSession *session, GossipContactList *list)
{
	gossip_debug (DEBUG_DOMAIN, "Connected");

	g_timeout_add (ACTIVE_USER_WAIT_TO_ENABLE_TIME,
		       (GSourceFunc) contact_list_show_active_users_cb,
		       list);
}

static gboolean
contact_list_show_active_users_cb (GossipContactList *list)
{
	GossipContactListPriv *priv;

	priv = GET_PRIV (list);

	priv->show_active = TRUE;

	return FALSE;
}

static void
contact_list_contact_update (GossipContactList *list,
			     GossipContact     *contact)
{
	GossipContactListPriv *priv;
	GossipContactType      type;
	ShowActiveData        *data;
	GtkTreeModel          *model;
	GList                 *iters, *l;
	gboolean               in_list;
	gboolean               should_be_in_list;
	gboolean               was_online = TRUE;
	gboolean               now_online = FALSE;
	gboolean               set_model = FALSE;
	gboolean               has_events = FALSE;
	gboolean               do_remove = FALSE;
	gboolean               do_set_active = FALSE;
	gboolean               do_set_refresh = FALSE;
	GossipAccount         *account;
	gdouble                seconds = 0;
	gboolean               is_composing;
	const GdkPixbuf       *pixbuf;
	GdkPixbuf             *pixbuf_composing;
	GdkPixbuf             *pixbuf_presence;
	GdkPixbuf             *pixbuf_avatar;

	priv = GET_PRIV (list);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	type = gossip_contact_get_type (contact);
	if (type != GOSSIP_CONTACT_TYPE_CONTACTLIST) {
		gossip_debug (DEBUG_DOMAIN,
			      "Presence from none-contact list contact (doing nothing)");
		return;
	}

	iters = contact_list_find_contact (list, contact);
	if (!iters) {
		in_list = FALSE;
	} else {
		in_list = TRUE;
	}

	/* Find out if we have any events waiting */
	has_events = g_hash_table_lookup (priv->flash_table, contact) != NULL;

	if (has_events || priv->show_offline || gossip_contact_is_online (contact)) {
		should_be_in_list = TRUE;
	} else {
		should_be_in_list = FALSE;
	}

	/* Get online state now. */
	now_online = gossip_contact_is_online (contact);

	if (!in_list && !should_be_in_list) {
		/* Nothing to do. */
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' in list:NO, should be:NO",
			      gossip_contact_get_name (contact));

		g_list_foreach (iters, (GFunc)gtk_tree_iter_free, NULL);
		g_list_free (iters);
		return;
	}
	else if (in_list && !should_be_in_list) {
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' in list:YES, should be:NO",
			      gossip_contact_get_name (contact));

		account = gossip_session_find_account (priv->session, contact);
		if (account) {
			seconds = gossip_session_get_connected_time (priv->session,
								     account);
			g_object_unref (account);
		}

		if (priv->show_active) {
			do_remove = TRUE;
			do_set_active = TRUE;
			do_set_refresh = TRUE;

			set_model = TRUE;
			gossip_debug (DEBUG_DOMAIN, "Remove item (after timeout)");
		} else {
			gossip_debug (DEBUG_DOMAIN, "Remove item (now)!");
			contact_list_remove_contact (list, contact, FALSE);
		}
	}
	else if (!in_list && should_be_in_list) {
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' in list:NO, should be:YES",
			      gossip_contact_get_name (contact));

		account = gossip_session_find_account (priv->session, contact);
		if (account) {
			seconds = gossip_session_get_connected_time (priv->session,
								     account);
			g_object_unref (account);
		}

		contact_list_add_contact (list, contact);

		if (priv->show_active) {
			do_set_active = TRUE;

			gossip_debug (DEBUG_DOMAIN, "Set active (contact added)");
		}
	} else {
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' in list:YES, should be:YES",
			      gossip_contact_get_name (contact));

		/* Get online state before. */
		if (iters && g_list_length (iters) > 0) {
			GtkTreeIter *iter;

			iter = g_list_nth_data (iters, 0);
			gtk_tree_model_get (model, iter, COL_IS_ONLINE, &was_online, -1);
		}

		/* Is this really an update or an online/offline. */
		if (priv->show_active) {
			if (was_online != now_online) {
				gchar *str;

				do_set_active = TRUE;
				do_set_refresh = TRUE;

				if (was_online) {
					str = "online  -> offline";
				} else {
					str = "offline -> online";
				}

				gossip_debug (DEBUG_DOMAIN, "Set active (contact updated %s)", str);
			} else {
				/* Was TRUE for presence updates. */
				/* do_set_active = FALSE;  */
				do_set_refresh = TRUE;

				gossip_debug (DEBUG_DOMAIN, "Set active (contact updated)");
			}
		}

		set_model = TRUE;
	}

	pixbuf_presence = gossip_pixbuf_for_contact (contact);
	pixbuf_composing = gossip_pixbuf_from_stock (GOSSIP_STOCK_TYPING,
						     GTK_ICON_SIZE_MENU);
	pixbuf_avatar = gossip_pixbuf_avatar_from_contact_scaled (contact, 32, 32);

	pixbuf = pixbuf_presence;

	for (l = iters; l && set_model; l = l->next) {
		gtk_tree_model_get (model, l->data,
				    COL_IS_COMPOSING, &is_composing,
				    -1);

		if (is_composing) {
			pixbuf = pixbuf_composing;
		}

		gtk_tree_store_set (GTK_TREE_STORE (model), l->data,
				    COL_PIXBUF_STATUS, pixbuf,
				    COL_STATUS, gossip_contact_get_status (contact),
				    COL_IS_ONLINE, now_online,
				    COL_NAME, gossip_contact_get_name (contact),
				    COL_PIXBUF_AVATAR, pixbuf_avatar,
				    -1);
	}

	g_object_unref (pixbuf_presence);
	g_object_unref (pixbuf_composing);
	if (pixbuf_avatar) {
		g_object_unref (pixbuf_avatar);
	}

	if (priv->show_active && do_set_active) {
		contact_list_contact_set_active (list, contact, do_set_active, do_set_refresh);

		if (do_set_active) {
			data = contact_list_contact_active_new (list, contact, do_remove);
			g_timeout_add (ACTIVE_USER_SHOW_TIME,
				       (GSourceFunc) contact_list_contact_active_cb,
				       data);
		}
	}

	/* FIXME: when someone goes online then offline quickly, the
	 * first timeout sets the user to be inactive and the second
	 * timeout removes the user from the contact list, really we
	 * should remove the first timeout.
	 */
	g_list_foreach (iters, (GFunc)gtk_tree_iter_free, NULL);
	g_list_free (iters);
}

static void
contact_list_contact_added_cb (GossipSession     *session,
			       GossipContact     *contact,
			       GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkTreeModel          *model;
	gboolean               has_events;

	priv = GET_PRIV (list);

	gossip_debug (DEBUG_DOMAIN, "Contact:'%s' added",
		      gossip_contact_get_name (contact));

	/* Connect notifications for contact updates */
	g_signal_connect (contact, "notify::groups",
			  G_CALLBACK (contact_list_contact_groups_updated_cb),
			  list);
	g_signal_connect (contact, "notify::presences",
			  G_CALLBACK (contact_list_contact_updated_cb),
			  list);
	g_signal_connect (contact, "notify::name",
			  G_CALLBACK (contact_list_contact_updated_cb),
			  list);
	g_signal_connect (contact, "notify::avatar",
			  G_CALLBACK (contact_list_contact_updated_cb),
			  list);
	g_signal_connect (contact, "notify::type",
			  G_CALLBACK (contact_list_contact_updated_cb),
			  list);

	has_events = g_hash_table_lookup (priv->flash_table, contact) != NULL;

	if (!has_events &&
	    !priv->show_offline &&
	    !gossip_contact_is_online (contact)) {
		return;
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	contact_list_add_contact (list, contact);
}

static void
contact_list_contact_groups_updated_cb (GossipContact     *contact,
					GParamSpec        *param,
					GossipContactList *list)
{
	GossipContactListPriv *priv;
	GossipContactType      type;

	priv = GET_PRIV (list);

	type = gossip_contact_get_type (contact);
	if (type != GOSSIP_CONTACT_TYPE_CONTACTLIST) {
		gossip_debug (DEBUG_DOMAIN,
			      "Update to none-contact list "
			      "contact %s (doing nothing)",
			      gossip_contact_get_name (contact));
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Contact:'%s' groups updated",
		      gossip_contact_get_name (contact));

	/* We do this to make sure the groups are correct, if not, we
	 * would have to check the groups already set up for each
	 * contact and then see what has been updated.
	 */
	contact_list_remove_contact (list, contact, FALSE);
	contact_list_add_contact (list, contact);
}

static void
contact_list_contact_updated_cb (GossipContact     *contact,
				 GParamSpec        *param,
				 GossipContactList *list)
{
	contact_list_contact_update (list, contact);
}

static void
contact_list_contact_removed_cb (GossipSession     *session,
				 GossipContact     *contact,
				 GossipContactList *list)
{
	gossip_debug (DEBUG_DOMAIN, "Contact:'%s' removed",
		   gossip_contact_get_name (contact));

	contact_list_remove_contact (list, contact, TRUE);
}

static void
contact_list_contact_composing_cb (GossipSession     *session,
				   GossipContact     *contact,
				   gboolean           composing,
				   GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkTreeModel          *model;
	GList                 *iters, *l;
	GdkPixbuf             *pixbuf = NULL;

	gossip_debug (DEBUG_DOMAIN, "Contact:'%s' %s typing",
		      gossip_contact_get_name (contact),
		      composing ? "is" : "is not");

	priv = GET_PRIV (list);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	if (! g_hash_table_lookup (priv->flash_table, contact)) {
		if (composing) {
			pixbuf = gossip_pixbuf_from_stock (GOSSIP_STOCK_TYPING,
							   GTK_ICON_SIZE_MENU);
		} else {
			pixbuf = gossip_pixbuf_for_contact (contact);
		}
	}

	iters = contact_list_find_contact (list, contact);
	for (l = iters; l; l = l->next) {
		GtkTreePath *path;
		GtkTreeIter *iter;

		iter = l->data;

		gtk_tree_store_set (GTK_TREE_STORE (model), iter,
				    COL_IS_COMPOSING, composing,
				    -1);

		if (pixbuf) {
			gtk_tree_store_set (GTK_TREE_STORE (model), iter,
					    COL_PIXBUF_STATUS, pixbuf,
					    -1);
		}

		path = gtk_tree_model_get_path (model, iter);
		gtk_tree_model_row_changed (model, path, iter);
		gtk_tree_path_free (path);
	}

	if (pixbuf) {
		g_object_unref (pixbuf);
	}

	g_list_foreach (iters, (GFunc)gtk_tree_iter_free, NULL);
	g_list_free (iters);
}

static void
contact_list_contact_set_active (GossipContactList *list,
				 GossipContact     *contact,
				 gboolean           active,
				 gboolean           set_changed)
{
	GtkTreeModel *model;
	GList        *iters, *l;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	iters = contact_list_find_contact (list, contact);
	for (l = iters; l; l = l->next) {
		GtkTreePath *path;
		GtkTreeIter *iter;

		iter = l->data;

		gtk_tree_store_set (GTK_TREE_STORE (model), iter,
				    COL_IS_ACTIVE, active,
				    -1);
		gossip_debug (DEBUG_DOMAIN, "Set item %s", active ? "active" : "inactive");

		if (set_changed) {
			path = gtk_tree_model_get_path (model, iter);
			gtk_tree_model_row_changed (model, path, iter);
			gtk_tree_path_free (path);
		}
	}

	g_list_foreach (iters, (GFunc)gtk_tree_iter_free, NULL);
	g_list_free (iters);
}

static ShowActiveData *
contact_list_contact_active_new (GossipContactList *list,
				 GossipContact     *contact,
				 gboolean           remove)
{
	ShowActiveData *data;

	g_return_val_if_fail (list != NULL, NULL);
	g_return_val_if_fail (contact != NULL, NULL);

	gossip_debug (DEBUG_DOMAIN, 
		      "Contact:'%s' now active, and %s be removed",
		      gossip_contact_get_name (contact), 
		      remove ? "WILL" : "WILL NOT");
	
	data = g_slice_new0 (ShowActiveData);

	data->list = g_object_ref (list);
	data->contact = g_object_ref (contact);

	data->remove = remove;

	return data;
}

static void
contact_list_contact_active_free (ShowActiveData *data)
{
	g_return_if_fail (data != NULL);

	g_object_unref (data->contact);
	g_object_unref (data->list);

	g_slice_free (ShowActiveData, data);
}

static gboolean
contact_list_contact_active_cb (ShowActiveData *data)
{
	GossipContactListPriv *priv;

	g_return_val_if_fail (data != NULL, FALSE);

	priv = GET_PRIV (data->list);

	if (data->remove &&
	    !priv->show_offline &&
	    !gossip_contact_is_online (data->contact)) {
		gossip_debug (DEBUG_DOMAIN, 
			      "Contact:'%s' active timeout, removing item",
			      gossip_contact_get_name (data->contact));
		contact_list_remove_contact (data->list,
					     data->contact,
					     FALSE);
	}

	gossip_debug (DEBUG_DOMAIN, 
		      "Contact:'%s' no longer active",
		      gossip_contact_get_name (data->contact));
	contact_list_contact_set_active (data->list,
					 data->contact,
					 FALSE,
					 TRUE);

	contact_list_contact_active_free (data);

	return FALSE;
}

static gchar *
contact_list_get_parent_group (GtkTreeModel *model,
			       GtkTreePath  *path,
			       gboolean     *path_is_group)
{
	GtkTreeIter  parent_iter, iter;
	gchar       *name;
	gboolean     is_group;

	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (path_is_group != NULL, NULL);

	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter,
			    COL_IS_GROUP, &is_group,
			    -1);

	if (!is_group) {
		if (!gtk_tree_model_iter_parent (model, &parent_iter, &iter)) {
			return NULL;
		}

		iter = parent_iter;

		gtk_tree_model_get (model, &iter,
				    COL_IS_GROUP, &is_group,
				    -1);

		if (!is_group) {
			return NULL;
		}

		*path_is_group = TRUE;
	}

	gtk_tree_model_get (model, &iter,
			    COL_NAME, &name,
			    -1);

	return name;
}

static void
contact_list_get_group (GossipContactList *list,
			const gchar       *name,
			GtkTreeIter       *iter_to_set,
			gboolean          *created)
{
	GtkTreeModel *model;
	FindGroup     fg;

	memset (&fg, 0, sizeof (fg));

	fg.name = name;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));
	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) contact_list_get_group_foreach,
				&fg);

	if (!fg.found) {
		if (created) {
			*created = TRUE;
		}

		gtk_tree_store_append (GTK_TREE_STORE (model), iter_to_set, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (model), iter_to_set,
				    COL_PIXBUF_STATUS, NULL,
				    COL_NAME, name,
				    COL_IS_GROUP, TRUE,
				    COL_IS_ACTIVE, FALSE,
				    COL_IS_COMPOSING, FALSE,
				    -1);
	} else {
		if (created) {
			*created = FALSE;
		}

		*iter_to_set = fg.iter;
	}
}

static gboolean
contact_list_get_group_foreach (GtkTreeModel *model,
				GtkTreePath  *path,
				GtkTreeIter  *iter,
				FindGroup    *fg)
{
	gchar    *str;
	gboolean  is_group;

	/* Groups are only at the top level. */
	if (gtk_tree_path_get_depth (path) != 1) {
		return FALSE;
	}

	gtk_tree_model_get (model, iter,
			    COL_NAME, &str,
			    COL_IS_GROUP, &is_group,
			    -1);
	if (is_group && strcmp (str, fg->name) == 0) {
		fg->found = TRUE;
		fg->iter = *iter;
	}

	g_free (str);

	return fg->found;
}

static void
contact_list_add_contact (GossipContactList *list,
			  GossipContact     *contact)
{
	GossipContactListPriv *priv;
	GtkTreeIter            iter, iter_group;
	GtkTreeModel          *model;
	GList                 *l, *groups;

	priv = GET_PRIV (list);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	/* If no groups just add it at the top level. */
	groups = gossip_contact_get_groups (contact);
	if (!groups) {
		GdkPixbuf *pixbuf_status;
		GdkPixbuf *pixbuf_avatar;
		gboolean   show_avatar = FALSE;

		pixbuf_status = gossip_pixbuf_for_contact (contact);
		pixbuf_avatar = gossip_pixbuf_avatar_from_contact_scaled (
			contact, 32, 32);

		if (priv->show_avatars && !priv->is_compact) {
			show_avatar = TRUE;
		}

		gtk_tree_store_append (GTK_TREE_STORE (model), &iter, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
				    COL_PIXBUF_STATUS, pixbuf_status,
				    COL_PIXBUF_AVATAR, pixbuf_avatar,
				    COL_PIXBUF_AVATAR_VISIBLE, show_avatar,
				    COL_NAME, gossip_contact_get_name (contact),
				    COL_STATUS, gossip_contact_get_status (contact),
				    COL_STATUS_VISIBLE, !priv->is_compact,
				    COL_CONTACT, contact,
				    COL_IS_GROUP, FALSE,
				    COL_IS_ACTIVE, FALSE,
				    COL_IS_ONLINE, gossip_contact_is_online (contact),
				    COL_IS_COMPOSING, FALSE,
				    -1);

		if (pixbuf_avatar) {
			g_object_unref (pixbuf_avatar);
		}

		g_object_unref (pixbuf_status);
	}

	/* Else add to each group. */
	for (l = groups; l; l = l->next) {
		GtkTreePath *path;
		GdkPixbuf   *pixbuf_status;
		GdkPixbuf   *pixbuf_avatar;
		const gchar *name;
		gboolean     created;
		gboolean     show_avatar = FALSE;

		name = l->data;
		if (!name) {
			continue;
		}

		pixbuf_status = gossip_pixbuf_for_contact (contact);
		pixbuf_avatar = gossip_pixbuf_avatar_from_contact_scaled (
			contact, 32, 32);

		contact_list_get_group (list, name, &iter_group, &created);

		if (priv->show_avatars && !priv->is_compact) {
			show_avatar = TRUE;
		}

		gtk_tree_store_append (GTK_TREE_STORE (model), &iter, &iter_group);
		gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
				    COL_PIXBUF_STATUS, pixbuf_status,
				    COL_PIXBUF_AVATAR, pixbuf_avatar,
				    COL_PIXBUF_AVATAR_VISIBLE, show_avatar,
				    COL_NAME, gossip_contact_get_name (contact),
				    COL_STATUS, gossip_contact_get_status (contact),
				    COL_STATUS_VISIBLE, !priv->is_compact,
				    COL_CONTACT, contact,
				    COL_IS_GROUP, FALSE,
				    COL_IS_ACTIVE, FALSE,
				    COL_IS_ONLINE, gossip_contact_is_online (contact),
				    COL_IS_COMPOSING, FALSE,
				    -1);

		if (pixbuf_avatar) {
			g_object_unref (pixbuf_avatar);
		}

		g_object_unref (pixbuf_status);

		if (!created) {
			continue;
		}

		path = gtk_tree_model_get_path (model, &iter_group);
		if (!path) {
			continue;
		}

		if (gossip_contact_group_get_expanded (name)) {
			g_signal_handlers_block_by_func (list,
							 contact_list_row_expand_or_collapse_cb,
							 GINT_TO_POINTER (TRUE));
			gtk_tree_view_expand_row (GTK_TREE_VIEW (list), path, TRUE);
			g_signal_handlers_unblock_by_func (list,
							   contact_list_row_expand_or_collapse_cb,
							   GINT_TO_POINTER (TRUE));
		} else {
			g_signal_handlers_block_by_func (list,
							 contact_list_row_expand_or_collapse_cb,
							 GINT_TO_POINTER (FALSE));
			gtk_tree_view_collapse_row (GTK_TREE_VIEW (list), path);
			g_signal_handlers_unblock_by_func (list,
							   contact_list_row_expand_or_collapse_cb,
							   GINT_TO_POINTER (FALSE));
		}

		gtk_tree_path_free (path);
	}
}

static void
contact_list_remove_contact (GossipContactList *list,
			     GossipContact     *contact,
			     gboolean           remove_flash)
{
	GossipContactListPriv *priv;
	GtkTreeModel          *model;
	GList                 *iters, *l;

	priv = GET_PRIV (list);

	iters = contact_list_find_contact (list, contact);
	if (!iters) {
		return;
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	for (l = iters; l; l = l->next) {
		GtkTreeIter parent;

		if (gtk_tree_model_iter_parent (model, &parent, l->data) &&
		    gtk_tree_model_iter_n_children (model, &parent) <= 1) {
			gtk_tree_store_remove (GTK_TREE_STORE (model), &parent);
		} else {
			gtk_tree_store_remove (GTK_TREE_STORE (model), l->data);
		}
	}

	g_list_foreach (iters, (GFunc)gtk_tree_iter_free, NULL);
	g_list_free (iters);

	if (remove_flash) {
		g_hash_table_remove (priv->flash_table, contact);
	}
}

static void
contact_list_create_model (GossipContactList *list)
{
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (
		gtk_tree_store_new (COL_COUNT,
				    GDK_TYPE_PIXBUF,     /* Status pixbuf */
				    GDK_TYPE_PIXBUF,     /* Avatar pixbuf */
				    G_TYPE_BOOLEAN,      /* Avatar pixbuf visible */
				    G_TYPE_STRING,       /* Name */
				    G_TYPE_STRING,       /* Status string */
				    G_TYPE_BOOLEAN,      /* Show status */
				    GOSSIP_TYPE_CONTACT, /* Contact type */
				    G_TYPE_BOOLEAN,      /* Is group */
				    G_TYPE_BOOLEAN,      /* Is active */
				    G_TYPE_BOOLEAN,      /* Is online */
				    G_TYPE_BOOLEAN));    /* Is composing */

	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
					 COL_NAME,
					 contact_list_sort_func,
					 list, NULL);

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
					      COL_NAME,
					      GTK_SORT_ASCENDING);

	gtk_tree_view_set_model (GTK_TREE_VIEW (list), model);

	g_object_unref (model);
}

static gboolean
contact_list_search_equal_func (GtkTreeModel *model,
				gint          column,
				const gchar  *key,
				GtkTreeIter  *iter,
				gpointer      search_data)
{
	gchar    *name, *name_folded;
	gchar    *key_folded;
	gboolean  ret;

	gtk_tree_model_get (model, iter,
			    COL_NAME, &name,
			    -1);

	name_folded = g_utf8_casefold (name, -1);
	key_folded = g_utf8_casefold (key, -1);

	if (strstr (name_folded, key_folded)) {
		ret = FALSE;
	} else {
		ret = TRUE;
	}

	g_free (name);
	g_free (name_folded);
	g_free (key_folded);

	return ret;
}

static void
contact_list_setup_view (GossipContactList *list)
{
	GtkCellRenderer   *cell;
	GtkTreeViewColumn *col;
	gint               i;

	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (list),
					     contact_list_search_equal_func,
					     list,
					     NULL);

	g_object_set (list,
		      "headers-visible", FALSE,
		      "reorderable", TRUE,
		      "show-expanders", FALSE,
		      NULL);

	col = gtk_tree_view_column_new ();

	/* State */
	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (
		col, cell,
		(GtkTreeCellDataFunc) contact_list_pixbuf_cell_data_func,
		list, NULL);

	g_object_set (cell,
		      "xpad", 5,
		      "ypad", 1,
		      "visible", FALSE,
		      NULL);

	/* Name */
	cell = gossip_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (col, cell, TRUE);
	gtk_tree_view_column_set_cell_data_func (
		col, cell,
		(GtkTreeCellDataFunc) contact_list_text_cell_data_func,
		list, NULL);

	gtk_tree_view_column_add_attribute (col, cell,
					    "name", COL_NAME);
	gtk_tree_view_column_add_attribute (col, cell,
					    "status", COL_STATUS);
	gtk_tree_view_column_add_attribute (col, cell,
					    "is_group", COL_IS_GROUP);

	/* Avatar */
	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (
		col, cell,
		(GtkTreeCellDataFunc) contact_list_avatar_cell_data_func,
		list, NULL);

	g_object_set (cell,
		      "xpad", 0,
		      "ypad", 0,
		      "visible", FALSE,
		      "width", 32,
		      "height", 32,
		      NULL);

	/* Expander */
	cell = gossip_cell_renderer_expander_new ();
	gtk_tree_view_column_pack_end (col, cell, FALSE);
	gtk_tree_view_column_set_cell_data_func (
		col, cell,
		(GtkTreeCellDataFunc) contact_list_expander_cell_data_func,
		list, NULL);

	/* Actually add the column now we have added all cell renderers */
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), col);

	/* Drag & Drop. */
	for (i = 0; i < G_N_ELEMENTS (drag_types_dest); ++i) {
		drag_atoms_dest[i] = gdk_atom_intern (drag_types_dest[i].target,
						      FALSE);
	}

	for (i = 0; i < G_N_ELEMENTS (drag_types_source); ++i) {
		drag_atoms_source[i] = gdk_atom_intern (drag_types_source[i].target,
							FALSE);
	}

	/* Note: We support the COPY action too, but need to make the
	 * MOVE action the default.
	 */
	gtk_drag_source_set (GTK_WIDGET (list),
			     GDK_BUTTON1_MASK,
			     drag_types_source,
			     G_N_ELEMENTS (drag_types_source),
			     GDK_ACTION_MOVE);

	gtk_drag_dest_set (GTK_WIDGET (list),
			   GTK_DEST_DEFAULT_ALL,
			   drag_types_dest,
			   G_N_ELEMENTS (drag_types_dest),
			   GDK_ACTION_MOVE | GDK_ACTION_LINK);

	g_signal_connect (GTK_WIDGET (list),
			  "drag-data-received",
			  G_CALLBACK (contact_list_drag_data_received),
			  NULL);

	/* FIXME: noticed but when you drag the row over the treeview
	 * fast, it seems to stop redrawing itself, if we don't
	 * connect this signal, all is fine.
	 */
	g_signal_connect (GTK_WIDGET (list),
			  "drag-motion",
			  G_CALLBACK (contact_list_drag_motion),
			  NULL);

	g_signal_connect (GTK_WIDGET (list),
			  "drag-begin",
			  G_CALLBACK (contact_list_drag_begin),
			  NULL);
	g_signal_connect (GTK_WIDGET (list),
			  "drag-data-get",
			  G_CALLBACK (contact_list_drag_data_get),
			  NULL);
	g_signal_connect (GTK_WIDGET (list),
			  "drag-end",
			  G_CALLBACK (contact_list_drag_end),
			  NULL);
}

static void
contact_list_drag_data_received (GtkWidget         *widget,
				 GdkDragContext    *context,
				 gint               x,
				 gint               y,
				 GtkSelectionData  *selection,
				 guint              info,
				 guint              time,
				 gpointer           user_data)
{
	GossipContactListPriv   *priv;
	GtkTreeModel            *model;
	GtkTreePath             *path;
	GtkTreeViewDropPosition  position;
	GossipContact           *contact;
	GList                   *groups;
	const gchar             *id;
	gchar                   *old_group;
	gboolean                 is_row;
	gboolean                 drag_success = TRUE;
	gboolean                 drag_del = FALSE;

	priv = GET_PRIV (widget);

	id = (const gchar*) selection->data;
	gossip_debug (DEBUG_DOMAIN, "Received %s%s drag & drop contact from roster with id:'%s'",
		      context->action == GDK_ACTION_MOVE ? "move" : "",
		      context->action == GDK_ACTION_COPY ? "copy" : "",
		      id);

	contact = gossip_session_find_contact (priv->session, id);
	if (!contact) {
		gossip_debug (DEBUG_DOMAIN, "No contact found associated with drag & drop");
		return;
	}

	groups = gossip_contact_get_groups (contact);

	is_row = gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW (widget),
						    x,
						    y,
						    &path,
						    &position);

	if (!is_row) {
		if (g_list_length (groups) != 1) {
			/* if they have dragged a contact out of a
			 * group then we would set the contact to have
			 * NO groups but only if they were ONE group
			 * to begin with - should we do this
			 * regardless to how many groups they are in
			 * already or not at all?
			 */
			return;
		}

		gossip_contact_set_groups (contact, NULL);
	} else {
		GList    *l, *new_groups;
		gchar    *name;
		gboolean  is_group;

		model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
		name = contact_list_get_parent_group (model, path, &is_group);

		if (groups && name &&
		    g_list_find_custom (groups, name, (GCompareFunc)strcmp)) {
			g_free (name);
			return;
		}

		/* Get source group information. */
		priv = GET_PRIV (widget);
		if (!priv->drag_row) {
			g_free (name);
			return;
		}

		path = gtk_tree_row_reference_get_path (priv->drag_row);
		if (!path) {
			g_free (name);
			return;
		}

		old_group = contact_list_get_parent_group (model, path, &is_group);
		gtk_tree_path_free (path);

		if (!name && old_group && GDK_ACTION_MOVE) {
			drag_success = FALSE;
		}

		if (context->action == GDK_ACTION_MOVE) {
			drag_del = TRUE;
		}

		/* Create new groups GList. */
		for (l = groups, new_groups = NULL; l && drag_success; l = l->next) {
			gchar *str;

			str = l->data;
			if (context->action == GDK_ACTION_MOVE &&
			    old_group != NULL &&
			    strcmp (str, old_group) == 0) {
				continue;
			}

			if (str == NULL) {
				continue;
			}

			new_groups = g_list_append (new_groups, g_strdup (str));
		}

		if (drag_success) {
			if (name) {
				new_groups = g_list_append (new_groups, name);
			}
			gossip_contact_set_groups (contact, new_groups);
		} else {
			g_free (name);
		}
	}

	if (drag_success) {
		gossip_session_update_contact (priv->session,
					       contact);
	}

	gtk_drag_finish (context, drag_success, drag_del, GDK_CURRENT_TIME);
}

static gboolean
contact_list_drag_motion (GtkWidget      *widget,
			  GdkDragContext *context,
			  gint            x,
			  gint            y,
			  guint           time,
			  gpointer        data)
{
	static DragMotionData *dm = NULL;
	GtkTreePath           *path;
	gboolean               is_row;
	gboolean               is_different = FALSE;
	gboolean               cleanup = TRUE;

	is_row = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
						x,
						y,
						&path,
						NULL,
						NULL,
						NULL);

	cleanup &= (!dm);

	if (is_row) {
		cleanup &= (dm && gtk_tree_path_compare (dm->path, path) != 0);
		is_different = (!dm || (dm && gtk_tree_path_compare (dm->path, path) != 0));
	} else {
		cleanup &= FALSE;
	}

	if (!is_different && !cleanup) {
		return TRUE;
	}

	if (dm) {
		gtk_tree_path_free (dm->path);
		if (dm->timeout_id) {
			g_source_remove (dm->timeout_id);
		}

		g_free (dm);

		dm = NULL;
	}

	if (!gtk_tree_view_row_expanded (GTK_TREE_VIEW (widget), path)) {
		dm = g_new0 (DragMotionData, 1);

		dm->list = GOSSIP_CONTACT_LIST (widget);
		dm->path = gtk_tree_path_copy (path);

		dm->timeout_id = g_timeout_add (
			1500,
			(GSourceFunc) contact_list_drag_motion_cb,
			dm);
	}

	return TRUE;
}

static gboolean
contact_list_drag_motion_cb (DragMotionData *data)
{
	gtk_tree_view_expand_row (GTK_TREE_VIEW (data->list),
				  data->path,
				  FALSE);

	data->timeout_id = 0;

	return FALSE;
}

static void
contact_list_drag_begin (GtkWidget      *widget,
			 GdkDragContext *context,
			 gpointer        user_data)
{
	GossipContactListPriv *priv;
	GtkTreeSelection      *selection;
	GtkTreeModel          *model;
	GtkTreePath           *path;
	GtkTreeIter            iter;

	priv = GET_PRIV (widget);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return;
	}

	path = gtk_tree_model_get_path (model, &iter);
	priv->drag_row = gtk_tree_row_reference_new (model, path);
	gtk_tree_path_free (path);
}

static void
contact_list_drag_data_get (GtkWidget             *widget,
			    GdkDragContext        *context,
			    GtkSelectionData      *selection,
			    guint                  info,
			    guint                  time,
			    gpointer               user_data)
{
	GossipContactListPriv *priv;
	GtkTreePath           *src_path;
	GtkTreeIter            iter;
	GtkTreeModel          *model;
	GossipContact         *contact;
	const gchar           *id;

	priv = GET_PRIV (widget);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	if (!priv->drag_row) {
		return;
	}

	src_path = gtk_tree_row_reference_get_path (priv->drag_row);
	if (!src_path) {
		return;
	}

	if (!gtk_tree_model_get_iter (model, &iter, src_path)) {
		gtk_tree_path_free (src_path);
		return;
	}

	gtk_tree_path_free (src_path);

	contact = gossip_contact_list_get_selected (GOSSIP_CONTACT_LIST (widget));
	if (!contact) {
		return;
	}

	id = gossip_contact_get_id (contact);
	g_object_unref (contact);

	switch (info) {
	case DND_DRAG_TYPE_CONTACT_ID:
		gtk_selection_data_set (selection, drag_atoms_source[info], 8,
					(guchar*)id, strlen (id) + 1);
		break;

	default:
		return;
	}
}

static void
contact_list_drag_end (GtkWidget      *widget,
		       GdkDragContext *context,
		       gpointer        user_data)
{
	GossipContactListPriv *priv;

	priv = GET_PRIV (widget);

	if (priv->drag_row) {
		gtk_tree_row_reference_free (priv->drag_row);
		priv->drag_row = NULL;
	}
}

static void
contact_list_cell_set_background (GossipContactList  *list,
				  GtkCellRenderer    *cell,
				  gboolean            is_group,
				  gboolean            is_active)
{
	GdkColor  color;
	GtkStyle *style;

	g_return_if_fail (list != NULL);
	g_return_if_fail (cell != NULL);

	style = gtk_widget_get_style (GTK_WIDGET (list));

	if (!is_group) {
		if (is_active) {
			color = style->bg[GTK_STATE_SELECTED];

			/* Here we take the current theme colour and add it to
			 * the colour for white and average the two. This
			 * gives a colour which is inline with the theme but
			 * slightly whiter.
			 */
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
	} else {
		color = style->text_aa[GTK_STATE_INSENSITIVE];

		color.red = (color.red + (style->white).red) / 2;
		color.green = (color.green + (style->white).green) / 2;
		color.blue = (color.blue + (style->white).blue) / 2;

		g_object_set (cell,
			      "cell-background-gdk", &color,
			      NULL);
	}
}

static void
contact_list_pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
				    GtkCellRenderer   *cell,
				    GtkTreeModel      *model,
				    GtkTreeIter       *iter,
				    GossipContactList *list)
{
	GdkPixbuf *pixbuf;
	gboolean   is_group;
	gboolean   is_active;

	gtk_tree_model_get (model, iter,
			    COL_IS_GROUP, &is_group,
			    COL_IS_ACTIVE, &is_active,
			    COL_PIXBUF_STATUS, &pixbuf,
			    -1);

	g_object_set (cell,
		      "visible", !is_group,
		      "pixbuf", pixbuf,
		      NULL);

	if (pixbuf) {
		g_object_unref (pixbuf);
	}

	contact_list_cell_set_background (list, cell, is_group, is_active);
}

static void
contact_list_avatar_cell_data_func (GtkTreeViewColumn *tree_column,
				    GtkCellRenderer   *cell,
				    GtkTreeModel      *model,
				    GtkTreeIter       *iter,
				    GossipContactList *list)
{
	GdkPixbuf *pixbuf;
	gboolean   show_avatar;
	gboolean   is_group;
	gboolean   is_active;

	gtk_tree_model_get (model, iter,
			    COL_PIXBUF_AVATAR, &pixbuf,
			    COL_PIXBUF_AVATAR_VISIBLE, &show_avatar,
			    COL_IS_GROUP, &is_group,
			    COL_IS_ACTIVE, &is_active,
			    -1);

	g_object_set (cell,
		      "visible", !is_group && show_avatar,
		      "pixbuf", pixbuf,
		      NULL);

	if (pixbuf) {
		g_object_unref (pixbuf);
	}

	contact_list_cell_set_background (list, cell, is_group, is_active);
}

static void
contact_list_text_cell_data_func (GtkTreeViewColumn *tree_column,
				  GtkCellRenderer   *cell,
				  GtkTreeModel      *model,
				  GtkTreeIter       *iter,
				  GossipContactList *list)
{
	gboolean is_group;
	gboolean is_active;
	gboolean show_status;

	gtk_tree_model_get (model, iter,
			    COL_IS_GROUP, &is_group,
			    COL_IS_ACTIVE, &is_active,
			    COL_STATUS_VISIBLE, &show_status,
			    -1);

	g_object_set (cell,
		      "show-status", show_status,
		      NULL);

	contact_list_cell_set_background (list, cell, is_group, is_active);
}

static void
contact_list_expander_cell_data_func (GtkTreeViewColumn *column,
				      GtkCellRenderer   *cell,
				      GtkTreeModel      *model,
				      GtkTreeIter       *iter,
				      GossipContactList *list)
{
	gboolean is_group;
	gboolean is_active;

	gtk_tree_model_get (model, iter,
			    COL_IS_GROUP, &is_group,
			    COL_IS_ACTIVE, &is_active,
			    -1);

	if (gtk_tree_model_iter_has_child (model, iter)) {
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

	contact_list_cell_set_background (list, cell, is_group, is_active);
}

static GtkWidget *
contact_list_get_contact_menu (GossipContactList *list,
			       gboolean           can_send_file,
			       gboolean           can_show_log)
{
	GossipContactListPriv *priv;
	GtkAction             *action;
	GtkWidget             *widget;

	priv = GET_PRIV (list);

	/* Sort out sensitive items */
	action = gtk_ui_manager_get_action (priv->ui, "/Contact/Log");
	gtk_action_set_sensitive (action, can_show_log);

	/* FIXME: disable file transfer until finished. */
	action = gtk_ui_manager_get_action (priv->ui, "/Contact/SendFile");
	gtk_action_set_visible (action, can_send_file);

	widget = gtk_ui_manager_get_widget (priv->ui, "/Contact");

	return widget;
}

GtkWidget *
gossip_contact_list_get_group_menu (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkWidget             *widget;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), NULL);

	priv = GET_PRIV (list);

	widget = gtk_ui_manager_get_widget (priv->ui, "/Group");

	return widget;
}

GtkWidget *
gossip_contact_list_get_contact_menu (GossipContactList *list,
				      GossipContact     *contact)
{
	GtkWidget *menu;
	gboolean   can_show_log;
	gboolean   can_send_file;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), NULL);
	g_return_val_if_fail (GOSSIP_IS_CONTACT (contact), NULL);

	can_show_log = gossip_log_exists_for_contact (contact);
	can_send_file = FALSE;

	menu = contact_list_get_contact_menu (list,
					      can_send_file,
					      can_show_log);
	return menu;
}

static gboolean
contact_list_button_press_event_cb (GossipContactList *list,
				    GdkEventButton    *event,
				    gpointer           user_data)
{
	GossipContactListPriv *priv;
	GossipContact         *contact;
	GtkTreePath           *path;
	GtkTreeSelection      *selection;
	GtkTreeModel          *model;
	GtkTreeIter            iter;
	gboolean               row_exists;
	GtkWidget             *menu;

	if (event->button != 3) {
		return FALSE;
	}

	priv = GET_PRIV (list);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	gtk_widget_grab_focus (GTK_WIDGET (list));

	row_exists = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (list),
						    event->x, event->y,
						    &path,
						    NULL, NULL, NULL);
	if (!row_exists) {
		return FALSE;
	}

	gtk_tree_selection_unselect_all (selection);
	gtk_tree_selection_select_path (selection, path);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);

	gtk_tree_model_get (model, &iter, COL_CONTACT, &contact, -1);

	if (contact) {
		menu = gossip_contact_list_get_contact_menu (list, contact);
		g_object_unref (contact);
	} else {
		menu = gossip_contact_list_get_group_menu (list);
	}

	if (!menu) {
		return FALSE;
	}

	gtk_widget_show (menu);

	gtk_menu_popup (GTK_MENU (menu),
			NULL, NULL, NULL, NULL,
			event->button, event->time);

	return TRUE;
}

static void
contact_list_row_activated_cb (GossipContactList *list,
			       GtkTreePath       *path,
			       GtkTreeViewColumn *col,
			       gpointer           user_data)
{
	GossipContact *contact;
	GtkTreeView   *view;
	GtkTreeModel  *model;
	GtkTreeIter    iter;

	view = GTK_TREE_VIEW (list);
	model = gtk_tree_view_get_model (view);

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, COL_CONTACT, &contact, -1);

	if (contact) {
		contact_list_action_activated (list, contact);
		g_object_unref (contact);
	}
}

static void
contact_list_row_expand_or_collapse_cb (GossipContactList *list,
					GtkTreeIter       *iter,
					GtkTreePath       *path,
					gpointer           user_data)
{
	GtkTreeModel *model;
	gchar        *name;
	gboolean      expanded;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	gtk_tree_model_get (model, iter,
			    COL_NAME, &name,
			    -1);

	expanded = GPOINTER_TO_INT (user_data);
	gossip_contact_group_set_expanded (name, expanded);

	g_free (name);
}

static gint
contact_list_sort_func (GtkTreeModel *model,
			GtkTreeIter  *iter_a,
			GtkTreeIter  *iter_b,
			gpointer      user_data)
{
	gchar         *name_a, *name_b;
	GossipContact *contact_a, *contact_b;
	gint           ret_val;

	gtk_tree_model_get (model, iter_a,
			    COL_NAME, &name_a,
			    COL_CONTACT, &contact_a,
			    -1);
	gtk_tree_model_get (model, iter_b,
			    COL_NAME, &name_b,
			    COL_CONTACT, &contact_b,
			    -1);

	/* If contact is NULL it means it's a group. */

	if (!contact_a && contact_b) {
		ret_val = 1;
	} else if (contact_a && !contact_b) {
		ret_val = -1;
	} else {
		ret_val = g_utf8_collate (name_a, name_b);
	}

	g_free (name_a);
	g_free (name_b);

	if (contact_a) {
		g_object_unref (contact_a);
	}

	if (contact_b) {
		g_object_unref (contact_b);
	}

	return ret_val;
}

static gboolean
contact_list_iter_equal_contact (GtkTreeModel  *model,
				 GtkTreeIter   *iter,
				 GossipContact *contact)
{
	GossipContact *c;
	gboolean       equal;

	gtk_tree_model_get (model, iter,
			    COL_CONTACT, &c,
			    -1);

	if (!c) {
		return FALSE;
	}

	equal = gossip_contact_equal (c, contact);
	g_object_unref (c);

	return equal;
}

static GList *
contact_list_find_contact (GossipContactList *list,
			   GossipContact     *contact)
{
	GtkTreeModel *model;
	FindContact   fc;
	GList        *l = NULL;

	memset (&fc, 0, sizeof (fc));

	fc.contact = contact;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));
	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc) contact_list_find_contact_foreach,
				&fc);

	if (fc.found) {
		l = fc.iters;
	}

	return l;
}

static gboolean
contact_list_find_contact_foreach (GtkTreeModel *model,
				   GtkTreePath  *path,
				   GtkTreeIter  *iter,
				   FindContact  *fc)
{
	if (contact_list_iter_equal_contact (model, iter, fc->contact)) {
		fc->found = TRUE;
		fc->iters = g_list_append (fc->iters, gtk_tree_iter_copy (iter));
	}

	/* We want to find ALL contacts that match, this means if we
	 * have the same contact in 3 groups, all iters should be
	 * returned.
	 */
	return FALSE;
}

static void
contact_list_action_cb (GtkAction         *action,
			GossipContactList *list)
{
	const gchar *name;

	name = gtk_action_get_name (action);
	if (!name) {
		return;
	}

	gossip_debug (DEBUG_DOMAIN, "Action:'%s' activated", name);

	if (strcmp (name, "Chat") == 0) {
		GossipContact *contact;

		contact = gossip_contact_list_get_selected (list);
		contact_list_action_activated (list, contact);
		g_object_unref (contact);

		return;
	}

	if (strcmp (name, "Information") == 0) {
		GossipContact *contact;
		GtkWindow     *parent;

		parent = GTK_WINDOW (gossip_app_get_window ());

		contact = gossip_contact_list_get_selected (list);
		if (contact) {
			gossip_contact_info_dialog_show (contact, parent);
			g_object_unref (contact);
		}

		return;
	}

	if (strcmp (name, "Rename") == 0) {
		contact_list_action_rename_group_selected (list);
		return;
	}

	if (strcmp (name, "Edit") == 0) {
		GossipContact *contact;
		GtkWindow     *parent;

		parent = GTK_WINDOW (gossip_app_get_window ());

		contact = gossip_contact_list_get_selected (list);
		if (contact) {
			gossip_edit_contact_dialog_show (contact, parent);
			g_object_unref (contact);
		}

		return;
	}

	if (strcmp (name, "Remove") == 0) {
		contact_list_action_remove_selected (list);
		return;
	}

	if (strcmp (name, "Invite") == 0) {
		contact_list_action_invite_selected (list);
		return;
	}

	if (strcmp (name, "SendFile") == 0) {
		GossipContact *contact;

		contact = gossip_contact_list_get_selected (list);
		if (contact) {
			gossip_ft_window_send_file (contact);
			g_object_unref (contact);
		}

		return;
	}

	if (strcmp (name, "Log") == 0) {
		GossipContact *contact;

		contact = gossip_contact_list_get_selected (list);
		if (contact) {
			gossip_log_window_show (contact, NULL);
			g_object_unref (contact);
		}

		return;
	}
}

static void
contact_list_action_activated (GossipContactList *list,
			       GossipContact     *contact)
{
	GossipContactListPriv *priv;
	FlashData             *data;
	gint                   event_id = 0;

	priv = GET_PRIV (list);

	data = g_hash_table_lookup (priv->flash_table, contact);
	if (data) {
		event_id = data->event_id;
	}

	g_signal_emit (list, signals[CONTACT_ACTIVATED], 0, contact, event_id);
}

static void
contact_list_action_entry_activate_cb (GtkWidget *entry,
				       GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static void
contact_list_action_remove_response_cb (GtkWidget         *dialog,
					gint               response,
					GossipContactList *list)
{
	if (response == GTK_RESPONSE_YES) {
		GossipContact         *contact;
		GossipContactListPriv *priv;

		priv = GET_PRIV (list);

		contact = g_object_get_data (G_OBJECT (dialog), "contact");
		gossip_session_remove_contact (priv->session, contact);
	}

	gtk_widget_destroy (dialog);
}

static void
contact_list_action_remove_selected (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GossipContact         *contact;
	GtkWidget             *dialog;
	gchar                 *name;

	priv = GET_PRIV (list);

	contact = gossip_contact_list_get_selected (list);
	if (!contact) {
		return;
	}

	name = g_strdup_printf ("<b>%s</b>\n",
				gossip_contact_get_name (contact));

	dialog = gtk_message_dialog_new (GTK_WINDOW (gossip_app_get_window ()),
					 0,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 "%s\n\n"
					 "%s"
					 "%s\n",
					 _("Do you want to remove this contact from your roster?"),
					 name ? name : "",
					 gossip_contact_get_id (contact));
	g_free (name);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
				GTK_STOCK_REMOVE, GTK_RESPONSE_YES,
				NULL);

	g_object_set (GTK_MESSAGE_DIALOG (dialog)->label,
		      "use-markup", TRUE,
		      "wrap", FALSE,
		      NULL);

	g_object_set_data_full (G_OBJECT (dialog), "contact", contact, g_object_unref);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (contact_list_action_remove_response_cb),
			  list);
	
	gtk_widget_show_all (dialog);
}

static void
contact_list_action_invite_selected (GossipContactList *list)
{
	GossipContact *contact;

	contact = gossip_contact_list_get_selected (list);
	if (!contact) {
		return;
	}

	gossip_chat_invite_dialog_show (contact, 0);
}

static void
contact_list_action_rename_group_response_cb (GtkWidget         *dialog,
					      gint               response,
					      GossipContactList *list)
{
	if (response == GTK_RESPONSE_OK) {
		GtkWidget   *entry;
		const gchar *name;
		const gchar *group;
		
		entry = g_object_get_data (G_OBJECT (dialog), "entry");
		group = g_object_get_data (G_OBJECT (dialog), "group");
		name = gtk_entry_get_text (GTK_ENTRY (entry));
		
		if (!G_STR_EMPTY (name)) {
			GossipContactListPriv *priv;

			priv = GET_PRIV (list);
			gossip_session_rename_group (priv->session, group, name);
		}
	}

	gtk_widget_destroy (dialog);
}

static void
contact_list_action_rename_group_selected (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkWidget             *dialog;
	GtkWidget             *entry;
	GtkWidget             *hbox;
	gchar                 *str;
	gchar                 *group;

	priv = GET_PRIV (list);

	group = gossip_contact_list_get_selected_group (list);
	if (!group) {
		return;
	}

	/* Translator: %s denotes the contact ID */
	str = g_strdup_printf ("<b>%s</b>", group);
	dialog = gtk_message_dialog_new (GTK_WINDOW (gossip_app_get_window ()),
					 0,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_OK_CANCEL,
					 _("Please enter a new name for the group:\n%s"),
					 str);

	g_free (str);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    hbox, FALSE, TRUE, 4);

	entry = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry), group);
	gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
	gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 4);

	g_object_set (GTK_MESSAGE_DIALOG (dialog)->label, "use-markup", TRUE, NULL);
	g_object_set_data (G_OBJECT (dialog), "entry", entry);
	g_object_set_data_full (G_OBJECT (dialog), "group", group, g_free);

	g_signal_connect (entry, "activate",
			  G_CALLBACK (contact_list_action_entry_activate_cb),
			  dialog);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (contact_list_action_rename_group_response_cb),
			  list);

	gtk_widget_show_all (dialog);
}

static void
contact_list_free_flash_timeout_data (FlashTimeoutData *timeout_data)
{
	g_return_if_fail (timeout_data != NULL);

	gossip_debug (DEBUG_DOMAIN,
		      "Contact:'%s' Cleaning up event flash data",
		      gossip_contact_get_name (timeout_data->contact));

	g_object_unref (timeout_data->contact);

	g_slice_free (FlashTimeoutData, timeout_data);
}

static void
contact_list_flash_free_data (FlashData *data)
{
	g_return_if_fail (data != NULL);

	if (data->flash_timeout_id) {
		g_source_remove (data->flash_timeout_id);
	}

	g_slice_free (FlashData, data);
}

static gboolean
contact_list_flash_timeout_func (FlashTimeoutData *timeout_data)
{
	GossipContactList     *list;
	GossipContactListPriv *priv;
	GossipContact         *contact;
	GList                 *l, *iters;
	GtkTreeModel          *model;
	GdkPixbuf             *pixbuf = NULL;
	FlashData             *data;
	gboolean               retval = FALSE;
	const gchar           *stock_id = NULL;

	list = timeout_data->list;
	priv = GET_PRIV (list);

	contact = timeout_data->contact;

	iters = contact_list_find_contact (list, contact);
	if (!iters) {
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' not found in treeview?",
			      gossip_contact_get_name (contact));

		return FALSE;
	}

	data = g_hash_table_lookup (priv->flash_table, contact);
	if (data) {
		data->flash_on = !data->flash_on;
		retval = TRUE;

		if (data->flash_on) {
			switch (data->event_type) {
			case GOSSIP_EVENT_NEW_MESSAGE:
			case GOSSIP_EVENT_SERVER_MESSAGE:
				stock_id = GOSSIP_STOCK_MESSAGE;
				break;
			case GOSSIP_EVENT_FILE_TRANSFER_REQUEST:
			case GOSSIP_EVENT_SUBSCRIPTION_REQUEST:
				stock_id = GTK_STOCK_DIALOG_QUESTION;
				break;
			default:
				/* Shouldn't happen */
				stock_id = GTK_STOCK_DIALOG_WARNING;
				break;
			}
		}
	} else {
		gossip_debug (DEBUG_DOMAIN,
			      "Contact:'%s' not found in flashing event table",
			      gossip_contact_get_name (contact));
	}

	if (stock_id) {
		pixbuf = gossip_pixbuf_from_stock (stock_id, GTK_ICON_SIZE_MENU);
	} else {
		pixbuf = gossip_pixbuf_for_contact (contact);
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	for (l = iters; l; l = l->next) {
		gtk_tree_store_set (GTK_TREE_STORE (model), l->data,
				    COL_PIXBUF_STATUS, pixbuf,
				    -1);
	}

	g_object_unref (pixbuf);

	g_list_foreach (iters, (GFunc) gtk_tree_iter_free, NULL);
	g_list_free (iters);

	return retval;
}

static void
contact_list_event_added_cb (GossipEventManager *manager,
			     GossipEvent        *event,
			     GossipContactList  *list)
{
	GossipContactListPriv *priv;
	GossipMessage         *message;
	GossipFT              *ft;
	GossipContact         *contact;
	GossipEventType        type;
	FlashData             *data;
	FlashTimeoutData      *timeout_data;

	priv = GET_PRIV (list);

	type = gossip_event_get_type (event);

	switch (type) {
	case GOSSIP_EVENT_NEW_MESSAGE:
		message = GOSSIP_MESSAGE (gossip_event_get_data (event));
		contact = gossip_message_get_sender (message);
		break;

	case GOSSIP_EVENT_SUBSCRIPTION_REQUEST:
		contact = GOSSIP_CONTACT (gossip_event_get_data (event));
		break;

	case GOSSIP_EVENT_FILE_TRANSFER_REQUEST:
		ft = GOSSIP_FT (gossip_event_get_data (event));
		contact = gossip_ft_get_contact (ft);
		break;

	default:
		/* Not handled */
		gossip_debug (DEBUG_DOMAIN,
			      "Event type not added to the flashing event table",
			      type);
		return;
	}

	data = g_hash_table_lookup (priv->flash_table, contact);
	if (data) {
		/* Already flashing this item. */
		gossip_debug (DEBUG_DOMAIN,
			      "Event already flashing for contact:'%s'",
			      gossip_contact_get_name (contact));
		return;
	}

	timeout_data = g_slice_new0 (FlashTimeoutData);

	timeout_data->list = list;
	timeout_data->contact = g_object_ref (contact);

	data = g_slice_new0 (FlashData);

	data->event_id = gossip_event_get_id (event);
	data->event_type = type;

	data->flash_on = TRUE;
	data->flash_timeout_id =
		g_timeout_add_full (G_PRIORITY_DEFAULT, FLASH_TIMEOUT,
				    (GSourceFunc) contact_list_flash_timeout_func,
				    timeout_data,
				    (GDestroyNotify) contact_list_free_flash_timeout_data);

	g_hash_table_insert (priv->flash_table, g_object_ref (contact), data);

	gossip_debug (DEBUG_DOMAIN,
		      "Contact:'%s' added to the flashing event table",
		      gossip_contact_get_name (contact));

	/* Update contact on roster to show him if there are events
	 * are waiting, regardless to preferences as to whether we
	 * show offline contacts.
	 */
	contact_list_contact_update (list, contact);
}

static void
contact_list_event_removed_cb (GossipEventManager *manager,
			       GossipEvent        *event,
			       GossipContactList  *list)
{
	GossipContactListPriv *priv;
	GossipMessage         *message;
	GossipFT              *ft;
	GossipContact         *contact;
	GossipEventType        type;
	FlashData             *data;

	priv = GET_PRIV (list);

	type = gossip_event_get_type (event);

	switch (type) {
	case GOSSIP_EVENT_NEW_MESSAGE:
		message = GOSSIP_MESSAGE (gossip_event_get_data (event));
		contact = gossip_message_get_sender (message);
		break;

	case GOSSIP_EVENT_SUBSCRIPTION_REQUEST:
		contact = GOSSIP_CONTACT (gossip_event_get_data (event));
		break;

	case GOSSIP_EVENT_FILE_TRANSFER_REQUEST:
		ft = GOSSIP_FT (gossip_event_get_data (event));
		contact = gossip_ft_get_contact (ft);
		break;

	default:
		/* Not handled */
		return;
	}

	data = g_hash_table_lookup (priv->flash_table, contact);
	if (!data) {
		/* Not flashing this contact. */
		return;
	}

	g_hash_table_remove (priv->flash_table, contact);

	gossip_debug (DEBUG_DOMAIN,
		      "Contact:'%s' removed from flashing event table",
		      gossip_contact_get_name (contact));

	/* Update contact on roster to show him if there are events
	 * are waiting, regardless to preferences as to whether we
	 * show offline contacts.
	 */
	contact_list_contact_update (list, contact);

#if 0
	iters = contact_list_find_contact (list, contact);
	if (!iters) {
		return;
	}

	pixbuf_presence = gossip_pixbuf_for_contact (contact);
	pixbuf_composing = gossip_pixbuf_from_stock (GOSSIP_STOCK_TYPING,
						     GTK_ICON_SIZE_MENU);

	pixbuf = pixbuf_presence;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	for (l = iters; l; l = l->next) {
		gtk_tree_model_get (model, l->data,
				    COL_IS_COMPOSING, &is_composing,
				    -1);

		if (is_composing) {
			pixbuf = pixbuf_composing;
		}

		gtk_tree_store_set (GTK_TREE_STORE (model),
				    l->data,
				    COL_PIXBUF_STATUS, pixbuf,
				    -1);
	}

	g_object_unref (pixbuf_presence);
	g_object_unref (pixbuf_composing);

	g_list_foreach (iters, (GFunc) gtk_tree_iter_free, NULL);
	g_list_free (iters);
#endif
}

static gboolean
contact_list_update_list_mode_foreach (GtkTreeModel      *model,
				       GtkTreePath       *path,
				       GtkTreeIter       *iter,
				       GossipContactList *list)
{
	GossipContactListPriv *priv;
	gboolean               show_avatar = FALSE;

	priv = GET_PRIV (list);

	if (priv->show_avatars && !priv->is_compact) {
		show_avatar = TRUE;
	}

	gtk_tree_store_set (GTK_TREE_STORE (model), iter,
			    COL_PIXBUF_AVATAR_VISIBLE, show_avatar,
			    COL_STATUS_VISIBLE, !priv->is_compact,
			    -1);

	return FALSE;
}

GossipContactList *
gossip_contact_list_new (void)
{
	return g_object_new (GOSSIP_TYPE_CONTACT_LIST, NULL);
}

GossipContact *
gossip_contact_list_get_selected (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkTreeSelection      *selection;
	GtkTreeIter            iter;
	GtkTreeModel          *model;
	GossipContact         *contact;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), NULL);

	priv = GET_PRIV (list);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter, COL_CONTACT, &contact, -1);

	return contact;
}

gchar *
gossip_contact_list_get_selected_group (GossipContactList *list)
{
	GossipContactListPriv *priv;
	GtkTreeSelection      *selection;
	GtkTreeIter            iter;
	GtkTreeModel          *model;
	gboolean               is_group;
	gchar                 *name;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), NULL);

	priv = GET_PRIV (list);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (list));
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter,
			    COL_IS_GROUP, &is_group,
			    COL_NAME, &name,
			    -1);

	if (!is_group) {
		g_free (name);
		return NULL;
	}

	return name;
}

gboolean
gossip_contact_list_get_show_offline (GossipContactList *list)
{
	GossipContactListPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), FALSE);

	priv = GET_PRIV (list);

	return priv->show_offline;
}

void
gossip_contact_list_set_show_offline (GossipContactList *list,
				      gboolean           show_offline)
{
	GossipContactListPriv *priv;
	GossipSession         *session;
	const GList           *contacts;
	const GList           *l;
	gboolean               show_active;

	g_return_if_fail (GOSSIP_IS_CONTACT_LIST (list));

	priv = GET_PRIV (list);

	priv->show_offline = show_offline;
	show_active = priv->show_active;

	/* Disable temporarily. */
	priv->show_active = FALSE;

	session = priv->session;
	contacts = gossip_session_get_contacts (session);
	for (l = contacts; l; l = l->next) {
		GossipContact *contact;

		contact = GOSSIP_CONTACT (l->data);

		contact_list_contact_update (list, contact);
	}

	/* Restore to original setting. */
	priv->show_active = show_active;
}

gboolean
gossip_contact_list_get_show_avatars (GossipContactList *list)
{
	GossipContactListPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), TRUE);

	priv = GET_PRIV (list);

	return priv->show_avatars;
}

void
gossip_contact_list_set_show_avatars (GossipContactList *list,
				      gboolean           show_avatars)
{
	GossipContactListPriv *priv;
	GtkTreeModel          *model;

	g_return_if_fail (GOSSIP_IS_CONTACT_LIST (list));

	priv = GET_PRIV (list);

	priv->show_avatars = show_avatars;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc)
				contact_list_update_list_mode_foreach,
				list);
}

gboolean
gossip_contact_list_get_is_compact (GossipContactList *list)
{
	GossipContactListPriv *priv;

	g_return_val_if_fail (GOSSIP_IS_CONTACT_LIST (list), TRUE);

	priv = GET_PRIV (list);

	return priv->is_compact;
}

void
gossip_contact_list_set_is_compact (GossipContactList *list,
				    gboolean           is_compact)
{
	GossipContactListPriv *priv;
	GtkTreeModel          *model;

	g_return_if_fail (GOSSIP_IS_CONTACT_LIST (list));

	priv = GET_PRIV (list);

	priv->is_compact = is_compact;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc)
				contact_list_update_list_mode_foreach,
				list);
}


/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Martyn Russell <ginxd@btopenworld.com>
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
#include <libgnomeui/gnome-druid.h>
#include <libgnome/gnome-i18n.h>
#include <loudmouth/loudmouth.h>
#include "gossip-utils.h"
#include "gossip-app.h"
#include "gossip-disco.h"
#include "gossip-disco-register.h"
#include "gossip-disco-servers.h"
#include "gossip-disco-window.h"

#define d(x) x


struct _GossipDiscoWindow {
	GtkWidget *window;

	GtkWidget *druid;

	/* page 1 */
	GtkWidget *page_1;
	GtkWidget *radiobutton_msn;
	GtkWidget *radiobutton_icq;
	GtkWidget *radiobutton_aim;
	GtkWidget *radiobutton_yahoo;

	/* page 2 */
	GtkWidget *page_2;
	GtkWidget *label_server_response;
	GtkWidget *label_server_result;
	
	/* page 3 */
	GtkWidget *page_3;
	GtkWidget *label_server;
	GtkWidget *expander_requirements_response;
	GtkWidget *label_requirements_result;
	GtkWidget *label_instructions;
	GtkWidget *label_username;
	GtkWidget *label_password;
	GtkWidget *label_email;
	GtkWidget *label_nickname;
	GtkWidget *entry_username;
	GtkWidget *entry_password;
	GtkWidget *entry_email;
	GtkWidget *entry_nickname;

	/* page 4 */
	GtkWidget *page_4;
	GtkWidget *label_register_response;
	GtkWidget *label_register_result;

	GossipDisco *disco;
	GossipDiscoRegister *reg;

	GossipJID *server_jid;
	const gchar *protocol_type;
	gboolean server_found;
};

enum {
	COL_DISCO_NAME,
	COL_DISCO_JID,
	COL_DISCO_REGISTERED,
	COL_DISCO_COUNT
};


static void disco_window_prepare_page_1        (GnomeDruidPage      *page,
						GnomeDruid          *druid,
						GossipDiscoWindow   *window);
static void disco_window_prepare_page_2        (GnomeDruidPage      *page,
						GnomeDruid          *druid,
						GossipDiscoWindow   *window);
static void disco_window_prepare_page_3        (GnomeDruidPage      *page,
						GnomeDruid          *druid,
						GossipDiscoWindow   *window);
static void disco_window_prepare_page_4        (GnomeDruidPage      *page,
						GnomeDruid          *druid,
						GossipDiscoWindow   *window);
static void disco_window_finish_page_4         (GnomeDruidPage      *page,
						GtkWidget           *widget,
						GossipDiscoWindow   *window);
static void disco_window_druid_stop            (GtkWidget           *widget,
						GossipDiscoWindow   *window);
static void disco_window_entry_details_changed (GtkEntry            *entry,
						GossipDiscoWindow   *window);
static void disco_window_destroy               (GtkWidget           *widget,
						GossipDiscoWindow   *window);
static void disco_window_check_local           (GossipDiscoWindow   *window);
static void disco_window_check_local_cb        (GossipDisco         *disco,
						GossipDiscoItem     *item,
						gboolean             last_item,
						gboolean             timeout,
						GossipDiscoWindow   *window);
static void disco_window_check_others          (GossipDiscoWindow   *window);
static void disco_window_check_others_1_cb     (const GList         *servers,
						GossipDiscoWindow   *window);
static void disco_window_check_others_2_cb     (GossipDisco         *disco,
						GossipDiscoItem     *item,
						gboolean             last_item,
						gboolean             timeout,
						GossipDiscoWindow   *window);
static void disco_window_proceed_to_register   (GossipDiscoWindow   *window);
static void disco_window_requirements          (GossipDiscoWindow   *window);
static void disco_window_register              (GossipDiscoWindow   *window);
static void disco_window_register_1_cb         (GossipDiscoRegister *reg,
						const gchar         *error_code,
						const gchar         *error_reason,
						GossipDiscoWindow   *window);
static void disco_window_register_2_cb         (GossipDiscoRegister *reg,
						const gchar         *error_code,
						const gchar         *error_reason,
						GossipDiscoWindow   *window);

    

GossipDiscoWindow *
gossip_disco_window_show (void)
{
	GossipDiscoWindow *window;
	GladeXML          *gui;
	
	window = g_new0 (GossipDiscoWindow, 1);

	gui = gossip_glade_get_file (GLADEDIR "/main.glade",
				     "disco_window",
				     NULL,
				     "disco_window", &window->window,
				     "druid", &window->druid,
				     "page_1", &window->page_1,
				     "page_2", &window->page_2,
				     "page_3", &window->page_3,
				     "page_4", &window->page_4,
				     "radiobutton_msn", &window->radiobutton_msn,
				     "radiobutton_icq", &window->radiobutton_icq,
				     "radiobutton_aim", &window->radiobutton_aim,
				     "radiobutton_yahoo", &window->radiobutton_yahoo,
				     "label_server_response", &window->label_server_response,
				     "label_server_result", &window->label_server_result,
				     "label_server", &window->label_server,
				     "expander_requirements_response", &window->expander_requirements_response,
				     "label_requirements_result", &window->label_requirements_result,
 				     "label_register_response", &window->label_register_response, 
 				     "label_register_result", &window->label_register_result, 
				     "label_instructions", &window->label_instructions,
				     "label_username", &window->label_username,
				     "label_password", &window->label_password,
				     "label_email", &window->label_email, 
				     "label_nickname", &window->label_nickname,
				     "entry_username", &window->entry_username,
				     "entry_password", &window->entry_password,
				     "entry_email", &window->entry_email, 
				     "entry_nickname", &window->entry_nickname,
				     NULL);

	gossip_glade_connect (gui,
			      window,
			      "disco_window", "destroy", disco_window_destroy,
			      "druid", "cancel", disco_window_druid_stop,
			      "entry_username", "changed", disco_window_entry_details_changed,
			      "entry_password", "changed", disco_window_entry_details_changed,
			      "entry_email", "changed", disco_window_entry_details_changed,
			      "entry_nickname", "changed", disco_window_entry_details_changed,
			      NULL);

	g_object_unref (gui);

	g_signal_connect_after (window->page_1, "prepare",
				G_CALLBACK (disco_window_prepare_page_1),
				window);
	g_signal_connect_after (window->page_2, "prepare",
				G_CALLBACK (disco_window_prepare_page_2),
				window);
	g_signal_connect_after (window->page_3, "prepare",
				G_CALLBACK (disco_window_prepare_page_3),
				window);
	g_signal_connect_after (window->page_4, "prepare",
				G_CALLBACK (disco_window_prepare_page_4),
				window);
	g_signal_connect_after (window->page_4, "finish",
				G_CALLBACK (disco_window_finish_page_4),
				window);

	gtk_widget_show (window->window);

	return window;
}


/*
 * 1. Check local server for service.
 * 2. Look up list at imendio.com OR use local list
 * 3. Search 5-10 of the servers on the list
 *
 *
 * NOTE: 
 * How do we know if they are registered with MSN/ICQ/etc in the first
 * place? We could always go through the contact list and find out
 * which JIDs are services and query them (but what if they are not a
 * contact?) OR we could look at the actual contacts and get the
 * server part and ask the server if we are registered?!
 *
 */

static void 
disco_window_check_local (GossipDiscoWindow *window)
{
	GossipJID   *jid;
	const gchar *host;

	jid = gossip_app_get_jid ();
 	host = gossip_jid_get_part_host (jid); 

	window->server_found = FALSE;
	gossip_disco_request (host, 
			      (GossipDiscoItemFunc) disco_window_check_local_cb,
			      window);
	
	d(g_print ("running disco on local server\n"));
}

static void
disco_window_check_local_cb (GossipDisco       *disco, 
			     GossipDiscoItem   *item, 
			     gboolean           last_item, 
			     gboolean           timeout,
			     GossipDiscoWindow *window)
{
	if (!window->server_found &&  
	    (!item ||
	     !gossip_disco_item_has_feature (item, "jabber:iq:register") ||
	     !gossip_disco_item_has_category (item, "gateway") ||
	     !gossip_disco_item_has_type (item, window->protocol_type))) {
		if (timeout) {
			d(g_print ("disco timed out\n"));
		} else if (last_item) {
			d(g_print ("all local disco services received\n"));
			disco_window_check_others (window);
		}

		return;
	} 

	if (!window->server_found) {
		window->server_found = TRUE;

		if (timeout) {
			d(g_print ("disco timed out\n"));
		} else if (last_item) {
			d(g_print ("all local disco services received\n"));
		}
		 
		window->server_jid = gossip_disco_item_get_jid (item);
		disco_window_proceed_to_register (window);
	}
}

static void
disco_window_check_others (GossipDiscoWindow *window)
{
	d(g_print ("checking other servers, protocol:'%s' not found locally\n", 
		   window->protocol_type));

	gossip_disco_servers_fetch ((GossipDiscoServersFunc) disco_window_check_others_1_cb,
				    window);
}

static void
disco_window_check_others_1_cb (const GList       *servers, 
				GossipDiscoWindow *window)
{
	GList *l;
	GRand *rand;
	gint   i;

	d(g_print ("got list (%d entries) of servers to disco, trying the first 5\n", 
		   g_list_length ((GList*)servers)));

	l = (GList*)servers;

	/* generate random generator */
	rand = g_rand_new ();
	
	/* browse 5 servers */
	for (i = 0; i < 5 && l != NULL; i++) {
		guint        j;
		const gchar *jid_str;

		j = g_rand_int_range (rand, 0, g_list_length ((GList*)servers));
	
		/* sleep for 1/5 of a sec before each req */
		g_usleep (G_USEC_PER_SEC * 1);
		
		jid_str = (gchar*) g_list_nth_data (l, j);
		gossip_disco_request (jid_str, 
				      (GossipDiscoItemFunc) disco_window_check_others_2_cb,
				      window);
	}
	
	g_rand_free (rand);
}

static void
disco_window_check_others_2_cb (GossipDisco       *disco, 
				GossipDiscoItem   *item, 
				gboolean           last_item,
				gboolean           timeout,
				GossipDiscoWindow *window)
{
	if (!window->server_found &&
	    (!item ||
	     !gossip_disco_item_has_feature (item, "jabber:iq:register") ||
	     !gossip_disco_item_has_category (item, "gateway") ||
	     !gossip_disco_item_has_type (item, window->protocol_type))) {
		if (timeout) {
			d(g_print ("disco timed out\n"));
		} else if (last_item) {
			d(g_print ("all disco services received\n"));
		}

		return;
	} 

	if (!window->server_found) {
		window->server_found = TRUE;

		if (timeout) {
			d(g_print ("disco timed out\n"));
		} else if (last_item) {
			d(g_print ("all disco services received\n"));
		}
		 
		window->server_jid = gossip_disco_item_get_jid (item);
		disco_window_proceed_to_register (window);
	}
}


static void 
disco_window_proceed_to_register (GossipDiscoWindow *window)
{
	d(g_print ("proceeding to register protocol:'%s' with jid:'%s'\n", 
		   window->protocol_type, 
		   gossip_jid_get_full (window->server_jid)));

	gtk_label_set_text (GTK_LABEL (window->label_server), 
			    gossip_jid_get_full (window->server_jid));

	gnome_druid_set_page (GNOME_DRUID (window->druid), 
			      GNOME_DRUID_PAGE (window->page_3));

	disco_window_requirements (window);
}

static void
disco_window_requirements (GossipDiscoWindow *window)
{
	d(g_print ("asking for disco registration requirements\n"));

	gossip_disco_register_requirements (gossip_jid_get_full (window->server_jid),
					    (GossipDiscoRegisterFunc) disco_window_register_1_cb,
					    window);
}

static void
disco_window_register (GossipDiscoWindow *window)
{
	const gchar *username;
	const gchar *password;
	const gchar *email;
	const gchar *nickname;

	username = gtk_entry_get_text (GTK_ENTRY (window->entry_username));
	password = gtk_entry_get_text (GTK_ENTRY (window->entry_password));
	email = gtk_entry_get_text (GTK_ENTRY (window->entry_email));
	nickname = gtk_entry_get_text (GTK_ENTRY (window->entry_nickname));

	gossip_disco_register_set_username (window->reg, username);
	gossip_disco_register_set_password (window->reg, password);
	gossip_disco_register_set_email (window->reg, email);
	gossip_disco_register_set_nickname (window->reg, nickname);

	d(g_print ("requesting disco registration\n"));

 	gossip_disco_register_request (window->reg, 
				       (GossipDiscoRegisterFunc) disco_window_register_2_cb, 
				       window); 
}


static void
disco_window_register_1_cb (GossipDiscoRegister *reg,
			    const gchar         *error_code,
			    const gchar         *error_reason,
			    GossipDiscoWindow   *window)
{
	gtk_widget_hide (window->expander_requirements_response);
	gtk_widget_show (window->label_register_result);

	if (error_code || error_reason) {
		gchar *str;

		str = g_strdup_printf ("%s\n\n%s",
				       _("Unable to Register"),
				       error_reason);

		gtk_widget_show (window->label_requirements_result);
		gtk_label_set_markup (GTK_LABEL (window->label_requirements_result), str);

		g_free (str);
		return;
	}

	if (gossip_disco_register_get_require_nickname (reg)) {
		gtk_widget_show (window->label_nickname);
		gtk_widget_show (window->entry_nickname);
		gtk_widget_grab_focus (window->entry_nickname);
	} else {
		gtk_widget_hide (window->label_nickname);
		gtk_widget_hide (window->entry_nickname);
	}

	if (gossip_disco_register_get_require_email (reg)) {
		gtk_widget_show (window->label_email);
		gtk_widget_show (window->entry_email);
		gtk_widget_grab_focus (window->entry_email);
	} else {
		gtk_widget_hide (window->label_email);
		gtk_widget_hide (window->entry_email);
	}

	if (gossip_disco_register_get_require_password (reg)) {
		gtk_widget_show (window->label_password);
		gtk_widget_show (window->entry_password);
		gtk_widget_grab_focus (window->entry_password);
	} else {
		gtk_widget_hide (window->label_password);
		gtk_widget_hide (window->entry_password);
	}

	if (gossip_disco_register_get_require_username (reg)) {
		gtk_widget_show (window->label_username);
		gtk_widget_show (window->entry_username);
		gtk_widget_grab_focus (window->entry_username);
	} else {
		gtk_widget_hide (window->label_username);
		gtk_widget_hide (window->entry_username);
	}

	gtk_label_set_text (GTK_LABEL (window->label_instructions),
			    gossip_disco_register_get_instructions (reg));

	window->reg = reg;
}

static void
disco_window_register_2_cb (GossipDiscoRegister *reg,
			    const gchar         *error_code,
			    const gchar         *error_reason,
			    GossipDiscoWindow   *window)
{
	gtk_widget_hide (window->label_register_response);
	gtk_widget_show (window->label_register_result);

	if (error_code || error_reason) {
		gchar *str;

		str = g_strdup_printf ("<b>%s</b>\n\n%s",
				       _("Unable to Register"),
				       error_reason);

		gtk_label_set_markup (GTK_LABEL (window->label_register_result), str);

		g_free (str);

		gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
						   FALSE, FALSE, TRUE, TRUE); 

	} else {
		gchar *str;

		str = g_strdup_printf ("<b>%s</b>\n\n%s",
				       _("Registration Successful!"),
				       _("You are now able to add contacts using this transport."));
		
		gtk_label_set_markup (GTK_LABEL (window->label_register_result), str);

		g_free (str);

		gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
						   FALSE, TRUE, FALSE, TRUE); 
	}
}

static void
disco_window_prepare_page_1 (GnomeDruidPage    *page,
			     GnomeDruid        *druid, 
			     GossipDiscoWindow *window)
{
	gtk_widget_grab_focus (window->radiobutton_msn);

	gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
					   FALSE, TRUE, TRUE, TRUE); 
}

static void
disco_window_prepare_page_2 (GnomeDruidPage    *page,
			     GnomeDruid        *druid, 
			     GossipDiscoWindow *window)
{
	/* set up widgets */
	gtk_widget_show (window->label_server_response);

	gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
					   TRUE, FALSE, TRUE, TRUE); 

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (window->radiobutton_msn))) {
		window->protocol_type = "msn";
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (window->radiobutton_icq))) {
		window->protocol_type = "icq";
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (window->radiobutton_aim))) {
		window->protocol_type = "aim";
	} else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (window->radiobutton_yahoo))) {
		window->protocol_type = "yahoo";
	} 

	disco_window_check_local (window);
}

static void
disco_window_prepare_page_3 (GnomeDruidPage    *page,
			     GnomeDruid        *druid, 
			     GossipDiscoWindow *window)
{
	gtk_widget_show (window->expander_requirements_response);
	gtk_widget_hide (window->label_requirements_result);

	gtk_label_set_text (GTK_LABEL (window->label_server), "");

	gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
					   FALSE, FALSE, TRUE, TRUE); 

	disco_window_requirements (window);
}

static void
disco_window_prepare_page_4 (GnomeDruidPage    *page,
			     GnomeDruid        *druid, 
			     GossipDiscoWindow *window)
{
	gtk_widget_show (window->label_register_response);
	gtk_widget_hide (window->label_register_result);

	gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
					   FALSE, FALSE, TRUE, TRUE); 

	gnome_druid_set_show_finish (GNOME_DRUID (window->druid), TRUE);

	disco_window_register (window);
}

static void
disco_window_finish_page_4 (GnomeDruidPage    *page,
			    GtkWidget         *widget, 
			    GossipDiscoWindow *window)
{
	gtk_widget_destroy (window->window);
}

static void
disco_window_druid_stop (GtkWidget         *widget, 
			 GossipDiscoWindow *window)
{
	gtk_widget_destroy (window->window);
}

static void 
disco_window_entry_details_changed (GtkEntry          *entry,
				    GossipDiscoWindow *window)
{
	const gchar *username;
	const gchar *password;
	const gchar *email;
	const gchar *nickname;

	username = gtk_entry_get_text (GTK_ENTRY (window->entry_username));
	password = gtk_entry_get_text (GTK_ENTRY (window->entry_password));
	email = gtk_entry_get_text (GTK_ENTRY (window->entry_email));
	nickname = gtk_entry_get_text (GTK_ENTRY (window->entry_nickname));
	
	if ((GTK_WIDGET_VISIBLE (window->entry_username) && 
	     (!username || strlen (username) < 1)) ||
	    (GTK_WIDGET_VISIBLE (window->entry_password) && 
	     (!password || strlen (password) < 1)) ||
	    (GTK_WIDGET_VISIBLE (window->entry_email) && 
	     (!email || strlen (email) < 1)) ||
	    (GTK_WIDGET_VISIBLE (window->entry_nickname) && 
	     (!nickname || strlen (nickname) < 1))) {
		gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
						   TRUE, FALSE, TRUE, TRUE); 
	} else {
		gnome_druid_set_buttons_sensitive (GNOME_DRUID (window->druid), 
						   TRUE, TRUE, TRUE, TRUE); 
	}		
}

static void
disco_window_destroy (GtkWidget         *widget, 
		      GossipDiscoWindow *window)
{
	gossip_disco_destroy (window->disco);

 	g_free (window); 
}
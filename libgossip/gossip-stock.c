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

#include <config.h>

#include <libgossip/gossip-paths.h>

#include "gossip-stock.h"

static GtkIconFactory *icon_factory = NULL;

static GtkStockItem stock_items[] = {
    { GOSSIP_STOCK_OFFLINE,                 NULL },
    { GOSSIP_STOCK_AVAILABLE,               NULL },
    { GOSSIP_STOCK_BUSY,                    NULL },
    { GOSSIP_STOCK_AWAY,                    NULL },
    { GOSSIP_STOCK_EXT_AWAY,                NULL },
    { GOSSIP_STOCK_PENDING,                 NULL },
    { GOSSIP_STOCK_MESSAGE,                 NULL },
    { GOSSIP_STOCK_TYPING,                  NULL },
    { GOSSIP_STOCK_CONTACT_INFORMATION,     NULL },
    { GOSSIP_STOCK_GROUP_MESSAGE,           NULL },
    { GOSSIP_STOCK_FILE_TRANSFER,           NULL }
};

void
gossip_stock_init (void)
{
    GtkIconSet *icon_set;
    gint        i;

    g_assert (icon_factory == NULL);

    gtk_stock_add (stock_items, G_N_ELEMENTS (stock_items));

    icon_factory = gtk_icon_factory_new ();
    gtk_icon_factory_add_default (icon_factory);
    g_object_unref (icon_factory);

    for (i = 0; i < G_N_ELEMENTS (stock_items); i++) {
        gchar     *path, *filename;
        GdkPixbuf *pixbuf;

        filename = g_strdup_printf ("%s.png", stock_items[i].stock_id);
        path = gossip_paths_get_image_path (filename);
        pixbuf = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);
        g_free (filename);

        icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);

        gtk_icon_factory_add (icon_factory,
                              stock_items[i].stock_id,
                              icon_set);

        gtk_icon_set_unref (icon_set);

        g_object_unref (pixbuf);
    }
}

void
gossip_stock_finalize (void)
{
    g_assert (icon_factory != NULL);

    gtk_icon_factory_remove_default (icon_factory);

    icon_factory = NULL;
}

GdkPixbuf *
gossip_stock_create_pixbuf (GtkWidget   *widget,
                            const gchar *stock,
                            GtkIconSize  size)
{
    return gtk_widget_render_icon (widget, stock, size, NULL);
}



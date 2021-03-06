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

#ifndef __GOSSIP_STOCK_H__
#define __GOSSIP_STOCK_H__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOSSIP_STOCK_OFFLINE             "gossip-offline"
#define GOSSIP_STOCK_AVAILABLE           "gossip-available"
#define GOSSIP_STOCK_BUSY                "gossip-busy"
#define GOSSIP_STOCK_AWAY                "gossip-away"
#define GOSSIP_STOCK_EXT_AWAY            "gossip-extended-away"
#define GOSSIP_STOCK_PENDING             "gossip-pending"

#define GOSSIP_STOCK_MESSAGE             "gossip-message"
#define GOSSIP_STOCK_TYPING              "gossip-typing"

#define GOSSIP_STOCK_CONTACT_INFORMATION "vcard_16"

#define GOSSIP_STOCK_GROUP_MESSAGE       "gossip-group-message"

#define GOSSIP_STOCK_FILE_TRANSFER       "gossip-file-transfer"

void         gossip_stock_init          (void);
void         gossip_stock_finalize      (void);
GdkPixbuf *  gossip_stock_create_pixbuf (GtkWidget   *widget,
                                         const gchar *stock,
                                         GtkIconSize  size);

G_END_DECLS

#endif /* __GOSSIP_STOCK_ICONS_H__ */

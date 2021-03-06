/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
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
 * Authors: Martyn Russell <martyn@imendio.com>
 */

#ifndef __GOSSIP_GEOMETRY_H__
#define __GOSSIP_GEOMETRY_H__

G_BEGIN_DECLS

void gossip_geometry_save_for_chat        (GossipChat *chat,
                                           gint        x,
                                           gint        y,
                                           gint        w,
                                           gint        h);
void gossip_geometry_load_for_chat        (GossipChat *chat,
                                           gint       *x,
                                           gint       *y,
                                           gint       *w,
                                           gint       *h);
void gossip_geometry_save_for_main_window (gint        x,
                                           gint        y,
                                           gint        w,
                                           gint        h);
void gossip_geometry_load_for_main_window (gint       *x,
                                           gint       *y,
                                           gint       *w,
                                           gint       *h);

G_END_DECLS

#endif /* __GOSSIP_GEOMETRY_H__ */

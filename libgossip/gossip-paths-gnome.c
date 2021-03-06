/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Richard Hult <richard@imendio.com>
 */

#include <config.h>

#include "gossip-paths.h"

gchar *
gossip_paths_get_glade_path (const gchar *filename)
{
    return g_build_filename (SHAREDIR, PACKAGE_TARNAME, filename, NULL);
}

gchar *
gossip_paths_get_image_path (const gchar *filename)
{
    return g_build_filename (SHAREDIR, PACKAGE_TARNAME, filename, NULL);
}

gchar *
gossip_paths_get_dtd_path (const gchar *filename)
{
    return g_build_filename (SHAREDIR, PACKAGE_TARNAME, filename, NULL);
}

gchar *
gossip_paths_get_sound_path (const gchar *filename)
{
    return g_build_filename (SHAREDIR, "sounds", PACKAGE_TARNAME, filename, NULL);
}

gchar *
gossip_paths_get_locale_path ()
{
    return g_strdup (LOCALEDIR);
}

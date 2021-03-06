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

#include <config.h>

#include <string.h>

#include "gossip-avatar.h"

#define DEBUG_DOMAIN "Avatar"

#define AVATAR_SIZE 32

static gboolean
avatar_pixbuf_is_opaque (GdkPixbuf *pixbuf)
{
    gint           width;
    gint           height;
    gint           rowstride; 
    gint           i;
    unsigned char *pixels;
    unsigned char *row;

    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        return TRUE;
    }

    width     = gdk_pixbuf_get_width (pixbuf);
    height    = gdk_pixbuf_get_height (pixbuf);
    rowstride = gdk_pixbuf_get_rowstride (pixbuf);
    pixels    = gdk_pixbuf_get_pixels (pixbuf);

    row = pixels;
    for (i = 3; i < rowstride; i+=4) {
        if (row[i] != 0xff) {
            return FALSE;
        }
    }

    for (i = 1; i < height - 1; i++) {
        row = pixels + (i * rowstride);
        if (row[3] != 0xff || row[rowstride-1] != 0xff) {
            return FALSE;
        }
    }

    row = pixels + ((height-1) * rowstride);
    for (i = 3; i < rowstride; i+=4) {
        if (row[i] != 0xff) {
            return FALSE;
        }
    }

    return TRUE;
}

/* From pidgin */
static void
avatar_pixbuf_roundify (GdkPixbuf *pixbuf)
{
    gint    width;
    gint    height;
    gint    rowstride;
    guchar *pixels;

    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        return;
    }

    width     = gdk_pixbuf_get_width(pixbuf);
    height    = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels    = gdk_pixbuf_get_pixels(pixbuf);

    if (width < 6 || height < 6) {
        return;
    }

    /* Top left */
    pixels[3] = 0;
    pixels[7] = 0x80;
    pixels[11] = 0xC0;
    pixels[rowstride + 3] = 0x80;
    pixels[rowstride * 2 + 3] = 0xC0;

    /* Top right */
    pixels[width * 4 - 1] = 0;
    pixels[width * 4 - 5] = 0x80;
    pixels[width * 4 - 9] = 0xC0;
    pixels[rowstride + (width * 4) - 1] = 0x80;
    pixels[(2 * rowstride) + (width * 4) - 1] = 0xC0;

    /* Bottom left */
    pixels[(height - 1) * rowstride + 3] = 0;
    pixels[(height - 1) * rowstride + 7] = 0x80;
    pixels[(height - 1) * rowstride + 11] = 0xC0;
    pixels[(height - 2) * rowstride + 3] = 0x80;
    pixels[(height - 3) * rowstride + 3] = 0xC0;

    /* Bottom right */
    pixels[height * rowstride - 1] = 0;
    pixels[(height - 1) * rowstride - 1] = 0x80;
    pixels[(height - 2) * rowstride - 1] = 0xC0;
    pixels[height * rowstride - 5] = 0x80;
    pixels[height * rowstride - 9] = 0xC0;
}

GType
gossip_avatar_get_gtype (void)
{
    static GType type_id = 0;

    if (!type_id) {
        type_id = g_boxed_type_register_static ("GossipAvatar",
                                                (GBoxedCopyFunc) gossip_avatar_ref,
                                                (GBoxedFreeFunc) gossip_avatar_unref);
    }

    return type_id;
}

GossipAvatar *
gossip_avatar_new (guchar      *data,
                   gsize        len,
                   const gchar *format)
{
    GossipAvatar *avatar;

    g_return_val_if_fail (data != NULL, NULL);
    g_return_val_if_fail (len > 0, NULL);

    avatar = g_slice_new0 (GossipAvatar);
    avatar->data = g_memdup (data, len);
    avatar->len = len;
    avatar->format = g_strdup (format);
    avatar->refcount = 1;

    return avatar;
}

static GdkPixbuf *
avatar_create_pixbuf (GossipAvatar *avatar, gint size)
{
    GdkPixbuf       *tmp_pixbuf;
    GdkPixbuf       *ret_pixbuf;
    GdkPixbufLoader *loader;
    GError          *error = NULL;
    gint             orig_width;
    gint             orig_height;
    gint             scale_width;
    gint             scale_height;

    if (!avatar) {
        return NULL;
    }

    if (avatar->format) {
        /* Some avatars are written by crap clients. This is just to
         * help things along here.
         */
        if (G_UNLIKELY (!strchr (avatar->format, '/'))) {
            gchar *old_format;

            /* This is obviously wrong, so we try to
             * correct it.
             */
            old_format = avatar->format;
            avatar->format = g_strdup_printf ("image/%s", old_format);
            g_free (old_format);
        }

        loader = gdk_pixbuf_loader_new_with_mime_type (avatar->format, &error);

        if (error) {
            g_warning ("Couldn't create GdkPixbuf loader for image format:'%s', %s",
                       avatar->format, 
                       error->message);
            g_error_free (error);
            return NULL;
        }
    } else {
        loader = gdk_pixbuf_loader_new ();
    }

    if (!gdk_pixbuf_loader_write (loader, avatar->data, avatar->len, &error)) {
        g_warning ("Couldn't write avatar image:%p with "
                   "length:%" G_GSIZE_FORMAT " to pixbuf loader, %s",
                   avatar->data, avatar->len, error->message);
        g_error_free (error);
        return NULL;
    }

    gdk_pixbuf_loader_close (loader, NULL);

    tmp_pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    scale_width = orig_width = gdk_pixbuf_get_width (tmp_pixbuf);
    scale_height = orig_height = gdk_pixbuf_get_height (tmp_pixbuf);

    if (scale_height > scale_width) {
        scale_width = (gdouble) size * (gdouble) scale_width / (gdouble) scale_height;
        scale_height = size;
    } else {
        scale_height = (gdouble) size * (gdouble) scale_height / (gdouble) scale_width;
        scale_width = size;
    }

    ret_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, size, size);
    gdk_pixbuf_fill (ret_pixbuf, 0x00000000);
    gdk_pixbuf_scale (tmp_pixbuf, ret_pixbuf, 
                      (size - scale_width) / 2,
                      (size - scale_height) / 2,
                      scale_width, 
                      scale_height, 
                      (size - scale_width) / 2, 
                      (size - scale_height) / 2, 
                      (gdouble) scale_width / (gdouble) orig_width, 
                      (gdouble) scale_height / (gdouble) orig_height,
                      GDK_INTERP_BILINEAR);

    if (avatar_pixbuf_is_opaque (ret_pixbuf)) {
        avatar_pixbuf_roundify (ret_pixbuf);
    }

    g_object_unref (loader);

    return ret_pixbuf;
}

GdkPixbuf *
gossip_avatar_get_pixbuf (GossipAvatar *avatar)
{
    g_return_val_if_fail (avatar != NULL, NULL);

    if (!avatar->pixbuf) {
        avatar->pixbuf = avatar_create_pixbuf (avatar, AVATAR_SIZE);
    }

    return avatar->pixbuf;
}

GdkPixbuf *
gossip_avatar_create_pixbuf_with_size (GossipAvatar *avatar, gint size)
{
    return avatar_create_pixbuf (avatar, size);
}

void
gossip_avatar_unref (GossipAvatar *avatar)
{
    g_return_if_fail (avatar != NULL);

    avatar->refcount--;
    if (avatar->refcount == 0) {
        g_free (avatar->data);
        g_free (avatar->format);

        if (avatar->pixbuf) {
            g_object_unref (avatar->pixbuf);
        }

        g_slice_free (GossipAvatar, avatar);
    }
}

GossipAvatar *
gossip_avatar_ref (GossipAvatar *avatar)
{
    g_return_val_if_fail (avatar != NULL, NULL);

    avatar->refcount++;

    return avatar;
}


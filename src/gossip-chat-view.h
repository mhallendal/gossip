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

#ifndef __GOSSIP_CHAT_VIEW_H__
#define __GOSSIP_CHAT_VIEW_H__

#include <gtk/gtk.h>

#include <libgossip/gossip.h>

G_BEGIN_DECLS

#define GOSSIP_TYPE_CHAT_VIEW         (gossip_chat_view_get_type ())
#define GOSSIP_CHAT_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_CHAT_VIEW, GossipChatView))
#define GOSSIP_CHAT_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GOSSIP_TYPE_CHAT_VIEW, GossipChatViewClass))
#define GOSSIP_IS_CHAT_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_CHAT_VIEW))
#define GOSSIP_IS_CHAT_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_CHAT_VIEW))
#define GOSSIP_CHAT_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_CHAT_VIEW, GossipChatViewClass))

typedef struct _GossipChatView      GossipChatView;
typedef struct _GossipChatViewClass GossipChatViewClass;
typedef struct _GossipChatViewPriv  GossipChatViewPriv;

#include "gossip-theme.h"

struct _GossipChatView {
    GtkTextView parent;
};

struct _GossipChatViewClass {
    GtkTextViewClass parent_class;
};

typedef enum {
    BLOCK_TYPE_NONE,
    BLOCK_TYPE_SELF,
    BLOCK_TYPE_OTHER,
    BLOCK_TYPE_EVENT,
    BLOCK_TYPE_TIME,
    BLOCK_TYPE_INVITE
} BlockType;

GType           gossip_chat_view_get_type                  (void) G_GNUC_CONST;
GossipChatView *gossip_chat_view_new                       (void);
void            gossip_chat_view_append_message_from_self  (GossipChatView *view,
                                                            GossipMessage  *msg,
                                                            GossipContact  *my_contact,
                                                            GdkPixbuf      *avatar);
void            gossip_chat_view_append_message_from_other (GossipChatView *view,
                                                            GossipMessage  *msg,
                                                            GossipContact  *my_contact,
                                                            GdkPixbuf      *avatar);
void            gossip_chat_view_append_event              (GossipChatView *view,
                                                            const gchar    *str);
void            gossip_chat_view_append_invite             (GossipChatView *view,
                                                            GossipMessage  *message);
void            gossip_chat_view_append_button             (GossipChatView *view,
                                                            const gchar    *message,
                                                            GtkWidget      *button1,
                                                            GtkWidget      *button2);
void            gossip_chat_view_set_margin                (GossipChatView *view,
                                                            gint            margin);
void            gossip_chat_view_allow_scroll              (GossipChatView *view,
                                                            gboolean        allow_scrolling);
void            gossip_chat_view_scroll_down               (GossipChatView *view);
void            gossip_chat_view_scroll_down_smoothly      (GossipChatView *view);
gboolean        gossip_chat_view_get_selection_bounds      (GossipChatView *view,
                                                            GtkTextIter    *start,
                                                            GtkTextIter    *end);
void            gossip_chat_view_clear                     (GossipChatView *view);
gboolean        gossip_chat_view_find_previous             (GossipChatView *view,
                                                            const gchar    *search_criteria,
                                                            gboolean        new_search);
gboolean        gossip_chat_view_find_next                 (GossipChatView *view,
                                                            const gchar    *search_criteria,
                                                            gboolean        new_search);
void            gossip_chat_view_find_abilities            (GossipChatView *view,
                                                            const gchar    *search_criteria,
                                                            gboolean       *can_do_previous,
                                                            gboolean       *can_do_next);
void            gossip_chat_view_highlight                 (GossipChatView *view,
                                                            const gchar    *text);
void            gossip_chat_view_copy_clipboard            (GossipChatView *view);
GossipTheme *   gossip_chat_view_get_theme                 (GossipChatView *view);
void            gossip_chat_view_set_theme                 (GossipChatView *view,
                                                            GossipTheme    *theme);
void            gossip_chat_view_set_margin                (GossipChatView *view,
                                                            gint            margin);
void            gossip_chat_view_set_is_group_chat         (GossipChatView *view,
                                                            gboolean        is_group_chat);
GossipContact * gossip_chat_view_get_last_contact          (GossipChatView *view);
void            gossip_chat_view_set_last_contact          (GossipChatView *view, 
                                                            GossipContact  *contact);
BlockType       gossip_chat_view_get_last_block_type       (GossipChatView *view);
void            gossip_chat_view_set_last_block_type       (GossipChatView *view,
                                                            BlockType       block_type);
time_t          gossip_chat_view_get_last_timestamp        (GossipChatView *view);
void            gossip_chat_view_set_last_timestamp        (GossipChatView *view,
                                                            time_t          timestamp);

G_END_DECLS

#endif /* __GOSSIP_CHAT_VIEW_H__ */

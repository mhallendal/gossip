/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2003 Imendio AB
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

#include <gtk/gtktextview.h>

#include <libgossip/gossip-message.h>

#define GOSSIP_TYPE_CHAT_VIEW         (gossip_chat_view_get_type ())
#define GOSSIP_CHAT_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_CHAT_VIEW, GossipChatView))
#define GOSSIP_CHAT_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GOSSIP_TYPE_CHAT_VIEW, GossipChatViewClass))
#define GOSSIP_IS_CHAT_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_CHAT_VIEW))
#define GOSSIP_IS_CHAT_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_CHAT_VIEW))
#define GOSSIP_CHAT_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_CHAT_VIEW, GossipChatViewClass))


typedef struct _GossipChatView      GossipChatView;
typedef struct _GossipChatViewClass GossipChatViewClass;
typedef struct _GossipChatViewPriv  GossipChatViewPriv;


struct _GossipChatView {
	GtkTextView         parent;

	GossipChatViewPriv *priv;
};


struct _GossipChatViewClass {
	GtkTextViewClass    parent_class;
};


typedef enum {
	GOSSIP_SMILEY_NORMAL,       /*  :)   */
	GOSSIP_SMILEY_WINK,         /*  ;)   */
	GOSSIP_SMILEY_BIGEYE,       /*  =)   */
	GOSSIP_SMILEY_NOSE,         /*  :-)  */
	GOSSIP_SMILEY_CRY,          /*  :'(  */
	GOSSIP_SMILEY_SAD,          /*  :(   */
	GOSSIP_SMILEY_SCEPTICAL,    /*  :/   */
	GOSSIP_SMILEY_BIGSMILE,     /*  :D   */
	GOSSIP_SMILEY_INDIFFERENT,  /*  :|   */
	GOSSIP_SMILEY_TOUNGE,       /*  :p   */
	GOSSIP_SMILEY_SHOCKED,      /*  :o   */
	GOSSIP_SMILEY_COOL,         /*  8)   */
	GOSSIP_SMILEY_SORRY,        /*  *|   */
	GOSSIP_SMILEY_KISS,         /*  :*   */
	GOSSIP_SMILEY_SHUTUP,       /*  :#   */
	GOSSIP_SMILEY_YAWN,         /*  |O   */
	GOSSIP_SMILEY_CONFUSED,     /*  :$   */
	GOSSIP_SMILEY_ANGEL,        /*  <)   */
	GOSSIP_SMILEY_OOOH,         /*  :x   */
	GOSSIP_SMILEY_LOOKAWAY,     /*  *)   */
	GOSSIP_SMILEY_BLUSH,        /*  *S   */
	GOSSIP_SMILEY_COOLBIGSMILE, /*  8D   */ 
	GOSSIP_SMILEY_ANGRY,        /*  :@   */
	GOSSIP_SMILEY_BOSS,         /*  @)   */
	GOSSIP_SMILEY_MONKEY,       /*  #)   */
	GOSSIP_SMILEY_SILLY,        /*  O)   */
	GOSSIP_SMILEY_SICK,         /*  +o(  */
						
	NUM_SMILEYS
} GossipSmiley;


GType           gossip_chat_view_get_type                  (void) G_GNUC_CONST;
GossipChatView *gossip_chat_view_new                       (void);
void            gossip_chat_view_append_message_from_self  (GossipChatView *view,
							    GossipMessage  *msg,
							    GossipContact  *my_contact);
void            gossip_chat_view_append_message_from_other (GossipChatView *view,
							    GossipMessage  *msg,
							    GossipContact  *my_contact);
void            gossip_chat_view_append_invite             (GossipChatView *view,
							    GossipMessage  *message);
void            gossip_chat_view_append_event              (GossipChatView *view,
							    const gchar    *str);
void            gossip_chat_view_set_margin                (GossipChatView *view,
							    gint            margin);
void            gossip_chat_view_clear                     (GossipChatView *view);
void            gossip_chat_view_scroll_down               (GossipChatView *view);
gboolean        gossip_chat_view_get_selection_bounds      (GossipChatView *view,
							    GtkTextIter    *start,
							    GtkTextIter    *end);
void            gossip_chat_view_copy_clipboard            (GossipChatView *view);
void            gossip_chat_view_set_irc_style             (GossipChatView *view,
							    gboolean        irc_style);
gboolean        gossip_chat_view_get_irc_style             (GossipChatView *view);

#endif /* __GOSSIP_CHAT_VIEW_H__ */

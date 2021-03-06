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
 *
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#ifndef __GOSSIP_GROUP_CHAT_H__
#define __GOSSIP_GROUP_CHAT_H__

#include <libgossip/gossip.h>

#include "gossip-chat.h"

G_BEGIN_DECLS

#define GOSSIP_TYPE_GROUP_CHAT         (gossip_group_chat_get_type ())
#define GOSSIP_GROUP_CHAT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOSSIP_TYPE_GROUP_CHAT, GossipGroupChat))
#define GOSSIP_GROUP_CHAT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOSSIP_TYPE_GROUP_CHAT, GossipGroupChatClass))
#define GOSSIP_IS_GROUP_CHAT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOSSIP_TYPE_GROUP_CHAT))
#define GOSSIP_IS_GROUP_CHAT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOSSIP_TYPE_GROUP_CHAT))
#define GOSSIP_GROUP_CHAT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOSSIP_TYPE_GROUP_CHAT, GossipGroupChatClass))

typedef struct _GossipGroupChat      GossipGroupChat;
typedef struct _GossipGroupChatClass GossipGroupChatClass;
typedef struct _GossipGroupChatPriv  GossipGroupChatPriv;

struct _GossipGroupChat {
    GossipChat           parent;

    GossipGroupChatPriv *priv;
};

struct _GossipGroupChatClass {
    GossipChatClass parent_class;
};

GType            gossip_group_chat_get_type                   (void) G_GNUC_CONST;
GossipGroupChat *gossip_group_chat_new                        (GossipChatroomProvider *provider,
                                                               GossipChatroom         *chatroom);
GossipChatroomId gossip_group_chat_get_chatroom_id            (GossipGroupChat        *group_chat);
GossipChatroomProvider *
gossip_group_chat_get_chatroom_provider      (GossipGroupChat        *group_chat);
GossipChatroom * gossip_group_chat_get_chatroom               (GossipGroupChat        *group_chat);
GossipContact *  gossip_group_chat_get_selected_contact       (GossipGroupChat        *group_chat);
GtkWidget *      gossip_group_chat_contact_menu               (GossipGroupChat        *group_chat);

/* Actions */
void             gossip_group_chat_change_subject             (GossipGroupChat        *group_chat);
void             gossip_group_chat_change_nick                (GossipGroupChat        *group_chat);
void             gossip_group_chat_contact_kick               (GossipGroupChat        *group_chat,
                                                               GossipContact          *contact);

/* Privileges */
gboolean         gossip_group_chat_contact_can_message_all    (GossipGroupChat        *group_chat,
                                                               GossipContact          *contact);
gboolean         gossip_group_chat_contact_can_change_subject (GossipGroupChat        *group_chat,
                                                               GossipContact          *contact);
gboolean         gossip_group_chat_contact_can_kick           (GossipGroupChat        *group_chat,
                                                               GossipContact          *contact);
gboolean         gossip_group_chat_contact_can_change_role    (GossipGroupChat        *group_chat,
                                                               GossipContact          *contact);

G_END_DECLS

#endif /* __GOSSIP_GROUP_CHAT_H__ */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2007 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * 
 */

#ifndef __LM_BS_RECEIVER_H__
#define __LM_BS_RECEIVER_H__

#if !defined (LM_INSIDE_LOUDMOUTH_H) && !defined (LM_COMPILATION)
#error "Only <loudmouth/loudmouth.h> can be included directly, this file may disappear or change contents."
#endif

#include "lm-bs-session.h"
#include "lm-bs-transfer.h"

G_BEGIN_DECLS

#define LM_BS_RECEIVER(obj) (LmBsReceiver *) obj;

typedef struct _LmBsReceiver LmBsReceiver;

LmBsReceiver *lm_bs_receiver_new            (LmBsClient   *client,
                                             LmBsTransfer *transfer,
                                             const gchar  *jid_used);
void          lm_bs_receiver_unref          (LmBsReceiver *receiver);
void          lm_bs_receiver_start_transfer (LmBsReceiver *receiver);

G_END_DECLS

#endif /* __LM_BS_RECEIVER_H__  */


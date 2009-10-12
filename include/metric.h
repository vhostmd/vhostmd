/*
 * Copyright (C) 2008 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Author: Jim Fehlig <jfehlig@novell.com>
 */

#ifndef __METRIC_H__
#define __METRIC_H__

#include <stdint.h>

#include "util.h"

/* Supported types for metric values */
typedef enum _metric_type {
   M_INT32,
   M_UINT32,
   M_INT64,
   M_UINT64,
   M_REAL32,
   M_REAL64,
   M_STRING,
   M_GROUP,
   M_XML
} metric_type;

typedef enum _metric_context {
   METRIC_CONTEXT_HOST,
   METRIC_CONTEXT_VM
} metric_context;

typedef int (*metric_func)(void *);

/* Encapsulation of metric definition */
typedef struct _metric {
   char *name;
   char *action;
   char *type_str;
   int cnt;
   metric_context ctx;
   metric_type type;
   metric_func pf;
   char *value;
   vu_vm *vm;
   
   struct _metric *next;
} metric;


char *metric_type_to_str(metric_type t);

int metric_type_from_str(const xmlChar *type, metric_type *typ);

int metric_value_to_str(metric *def, char **str);

int metric_value_get(metric *def);

int metric_xml(metric *m, vu_buffer *buf);

#ifdef LIBXENSTAT
int xen_metrics(metric **user_metrics);
#endif

#ifdef WITH_XENSTORE
int metrics_xenstore_update(char *buffer, int *ids, int num_vms);
#endif

#endif /* __METRIC_H__ */

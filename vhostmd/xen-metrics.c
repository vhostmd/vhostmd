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
 * Author: Pat Campbell <plc@novell.com>
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "util.h"
#include "metric.h"

#include "xenstat.h"

static xenstat_handle *xhandle = NULL;
static xenstat_node *cur_node = NULL;

int node_tot_mem(void *vp) {
	metric *m = vp;

	cur_node = xenstat_get_node(xhandle, XENSTAT_ALL);
	if (cur_node == NULL) {
		vu_log(VHOSTMD_WARN, "Failed to retrieve statistics from libxenstat\n");
		return -1;
	}
	m->value.r32 = xenstat_node_tot_mem(cur_node);
	xenstat_free_node(cur_node);
	return 0;
}

int func_str_test(void *vp) {
	metric *m = vp;
	int len = 0;
	char value[] = "func_str_test";

	if (m->value.str) {
		len = strlen(m->value.str);
		m->value.str[0] = '\0';
		if (strlen(value) > len)
			m->value.str = realloc(m->value.str, strlen(value) + 1);
	}
	else
		m->value.str = calloc(1, strlen(value) + 1);
	if (m->value.str == NULL)
		goto out;
	sprintf(m->value.str, "%s", value);
out:
	return 0;
}


metric m[] = {
	{   "test",
		NULL,
		M_STRING,
		METRIC_CONTEXT_HOST,
		func_str_test,
		0,
		NULL
	},
	{   "node_tot_mem",
		NULL,
		M_REAL32,
		METRIC_CONTEXT_HOST,
		node_tot_mem,
		0,
		NULL
	},
	{   "pages paged out",
		"vmstat -s |grep \"pages paged out\" | awk '{print $1}'",
		M_UINT32,
		METRIC_CONTEXT_HOST,
		NULL,
		0,
		NULL
	}
};


int xen_metrics(metric **user_metrics) {
	int i;
	metric *mdef;
	metric *metrics = *user_metrics;

	xhandle = xenstat_init();
	if (xhandle == NULL) {
		vu_log(VHOSTMD_WARN, "Failed to initialize xenstat library\n");
		return -1;
	}

	for (i = 0; i < sizeof(m)/sizeof(metric); i++) {
		mdef = calloc(sizeof(metric), 1);
		if (mdef) {
			memcpy(mdef,&m[i], sizeof(metric));
			mdef->next = metrics;
			metrics = mdef;
		}
		else {
			vu_log(VHOSTMD_WARN, "Unable to allocate metric node, ignoring ...");
		}
	}
	*user_metrics = metrics;
	return 0;
}

/*
 * Copyright (C) 2009 Novell, Inc.
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
 *
 * Preliminary, this could be done better
 */

#include <stdio.h>
#include <string.h>
#include <xenctrl.h>
#include <xs.h>

#include "util.h"

#define MAX_DOMS  1024
static int xc_handle = -1;
static xc_dominfo_t info[MAX_DOMS];

int vu_num_vms(void)
{
    uint32_t first_dom = 0;
    int max_doms = MAX_DOMS, nr_doms = 0;

    if (xc_handle == -1)
       xc_handle = xc_interface_open();

    if (xc_handle == -1)
        return -1;

    nr_doms = xc_domain_getinfo(xc_handle, first_dom, max_doms, info);

    return(nr_doms);
}

int vu_get_vms(int *ids, int max_ids)
{
    uint32_t first_dom = 0;
    int max_doms = MAX_DOMS, nr_doms = 0, i;

    if (xc_handle == -1)
       xc_handle = xc_interface_open();

    if (xc_handle == -1)
        return -1;

    nr_doms = xc_domain_getinfo(xc_handle, first_dom, max_doms, info);
    if (nr_doms < 0) {
        goto out;
    }

    if (max_ids < nr_doms)
        nr_doms = max_ids;
    for (i = 0; i < nr_doms; i++)
        ids[i] = (int)info[i].domid;

out:
    return nr_doms;
}

vu_vm *vu_get_vm(int id)
{
   vu_vm *vm = NULL;
   char *path = NULL, *buf = NULL;
   char *name = NULL;
   char *uuid = NULL;
   struct xs_handle *xsh = NULL;
   unsigned len;
   char *cp;
   
   vm = calloc(1, sizeof(vu_vm));
   if (vm == NULL)
      return NULL;
   
   vm->id = id;

   xsh = xs_daemon_open();
   if (xsh == NULL)
       goto error;

   path = xs_get_domain_path(xsh, id);
   if (path == NULL) {
      goto error;
   }

   asprintf(&buf, "%s/vm", path);
   uuid = xs_read(xsh, XBT_NULL, buf, &len);
   if (uuid == NULL) {
      goto error;
   }
   cp = strrchr(uuid, '/');
   memmove(uuid, cp+1, strlen(cp));
   vm->uuid = strdup(uuid);
   free(buf);

   asprintf(&buf, "%s/name", path);
   name = xs_read(xsh, XBT_NULL, buf, &len);
   if (name)
       vm->name = strdup(name);

   goto out;

error:
   if (vm) free(vm);
   vm = NULL;

out:
   if (buf) free(buf);
   if (path) free(path);
   if (name) free(name);
   if (uuid) free(uuid);
   if (xsh) xs_daemon_close(xsh);
   
   return vm;
}

void vu_vm_free(vu_vm *vm)
{
   if (vm) {
      free(vm->name);
      free(vm->uuid);
      free(vm);
   }
}

void vu_vm_connect_close()
{
    if (xc_handle != -1)
        xc_interface_close(xc_handle);
    xc_handle = -1;
}


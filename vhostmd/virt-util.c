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
 * Author: Jim Fehlig <jfehlig@novell.com>
 */

#include <stdio.h>
#include <string.h>
#include <libvirt/libvirt.h>

#include "util.h"

static virConnectPtr conn = NULL;

int vu_num_vms(void)
{
   if (conn == NULL)
      conn = virConnectOpen(NULL);
      
   if (conn == NULL) {
      conn = virConnectOpenReadOnly(NULL);
      if (conn == NULL) {
         vu_log(VHOSTMD_ERR, "Unable to open libvirt connection");
         return -1;
      }
   }
   
   return virConnectNumOfDomains(conn);
}

int vu_get_vms(int *ids, int max_ids)
{
   if (conn == NULL)
      conn = virConnectOpen(NULL);
      
   if (conn == NULL) {
      vu_log(VHOSTMD_ERR, "Unable to open libvirt connection");
      return -1;
   }
   
   return (virConnectListDomains(conn, ids, max_ids));
}

vu_vm *vu_get_vm(int id)
{
   vu_vm *vm;
   const char *name;
   virDomainPtr dom = NULL;
   char uuid[VIR_UUID_STRING_BUFLEN];
   
   vm = calloc(1, sizeof(vu_vm));
   if (vm == NULL)
      return NULL;
   
   vm->id = id;
   
   if (conn == NULL)
      conn = virConnectOpen(NULL);
   
   if (conn == NULL) {
      vu_log(VHOSTMD_ERR, "Unable to open libvirt connection");
      goto error;
   }
   
   dom = virDomainLookupByID(conn, id);
   if (dom == NULL) {
       vu_log(VHOSTMD_ERR, "Failed to lookup domain for id %d", id);
       goto error;
   }

   uuid[0] = '\0';
   virDomainGetUUIDString(dom, uuid);
   vm->uuid = strdup(uuid);

   name = virDomainGetName(dom);
   if (name)
       vm->name = strdup(name);

   virDomainFree(dom);
   return vm;

 error:
   virDomainFree(dom);
   free(vm);
   return NULL;
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
   if (conn) {
      virConnectClose(conn);
      conn = NULL;
   }
}


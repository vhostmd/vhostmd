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
 *         Jim Fehlig <jfehlig@novell.com>
 */

#include "libmetrics.h"

int get_host_memory_metrics(memory_metrics *rec)
{
   metric *mdef;

   if (get_metric("TotalPhyMem", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->total_physical_memory = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("UsedMem", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->used_physical_memory = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("FreeMem", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->free_physical_memory = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("PagedInMemory", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->paged_in_memory = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("PagedOutMemory", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->paged_out_memory = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("PageInRate", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->page_in_rate = mdef->value.ui64;
   }
   metric_free(mdef);

   if (get_metric("PageFaultRate", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->page_fault_rate = mdef->value.ui64;
   }
   metric_free(mdef);

   return 0;
}

int get_host_cpu_metrics(cpu_metrics *rec) {
   metric *mdef;

   if (get_metric("TotalPhyCPUs", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->total_phys_cpus = mdef->value.ui32;
   }
   metric_free(mdef);

   if (get_metric("NumCPUs", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->num_phys_cpus_utilized = mdef->value.ui32;
   }
   metric_free(mdef);

   if (get_metric("TotalCPUTime", &mdef, METRIC_CONTEXT_HOST) == 0) {
      rec->total_cpu_time = mdef->value.r64;
   }
   metric_free(mdef);

   return 0;
}

int get_host_totalPhyCPUs(uint32_t * var) 
{
   metric *mdef;
   int ret = -1;

   if (get_metric("TotalPhyCPUs", &mdef, METRIC_CONTEXT_HOST) == 0) {
      *var = mdef->value.ui32;
      ret = 0;
   }
   metric_free(mdef);
   return ret;
}


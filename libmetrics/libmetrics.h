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

#ifndef __LIBVHOSTMD_H__
#define __LIBVHOSTMD_H__

#include <stdint.h>

/* metric value types */
typedef enum _metric_type {
   M_INT32,
   M_UINT32,
   M_INT64,
   M_UINT64,
   M_REAL32,
   M_REAL64,
   M_STRING,
} metric_type;

/* metric contexts */
typedef enum _metric_context {
   METRIC_CONTEXT_HOST,
   METRIC_CONTEXT_VM
} metric_context;

/* metric definition */
typedef struct _metric {
   metric_type type;
   union 
   {
      int32_t i32;
      uint32_t ui32;
      int64_t i64;
      uint64_t ui64;
      float r32;
      double r64;
      char *str;   /*Location of string within the structure */
   } value;
} metric;

/* metric class, memory */
typedef struct memory_metrics 
{
    uint64_t total_physical_memory;
    uint64_t used_physical_memory;
    uint64_t free_physical_memory;
    uint64_t paged_out_memory;
    uint64_t paged_in_memory;
    uint64_t page_in_rate;
    uint64_t page_fault_rate;
} memory_metrics;

/* metric class, cpu */
typedef struct cpu_metrics 
{
    uint32_t total_phys_cpus;
    uint32_t num_phys_cpus_utilized;
    double total_cpu_time;
} cpu_metrics;

/* allocate metric(s) */
memory_metrics *memory_metrics_alloc(void);
cpu_metrics *cpu_metrics_alloc(void);

/* free metric(s) allocated by this lib */
void metric_free(metric *rec);
void memory_metrics_free(memory_metrics *rec);
void cpu_metrics_free(cpu_metrics *rec);

/* get a 'class' of metrics */
int get_host_memory_metrics(memory_metrics *rec);
int get_vm_memory_metrics(memory_metrics *rec);
int get_host_cpu_metrics(cpu_metrics *rec);
int get_vm_cpu_metrics(cpu_metrics *rec);

/* get generic metric */
int get_metric(const char *name, metric **rec, metric_context context);

/* dump metrics to xml formatted file */
int dump_metrics(const char *dest_file);

/* dump metrics from xenstore to xml formatted file */
int dump_xenstore_metrics(const char *dest_file);

#endif

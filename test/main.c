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
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <dlfcn.h>

#include "libmetrics.h"

static void usage(const char *argv0)
{
   fprintf (stderr,
         "\n\
         Usage:\n\
         %s [options]\n\
         \n\
         Options:\n\
         -v | --verbose         Verbose messages.\n\
         \n\
         Geust metrics print:\n\
         \n",
         argv0);
}

#if !defined USE_DL_OPEN
static int test_static()
{
   metric *mdef;
   cpu_metrics *cpu_rec;
   memory_metrics *memory_rec;


   /* Generic metric get */
   if (get_metric("UsedMem", &mdef, METRIC_CONTEXT_HOST) == 0) {
      uint64_t usedMem = mdef->value.ui64;
      fprintf(stderr, "UsedMem: %"PRIu64"\n", usedMem);
   }
   else {
      fprintf(stderr, "UsedMem: metric not found\n");
   }
   metric_free(mdef);

   if (get_metric("TotalCPUTime", &mdef, METRIC_CONTEXT_HOST) == 0) {
      double cpu_time = mdef->value.r64;
      fprintf(stderr, "TotalCPUTime: %f\n", cpu_time);
   }
   else {
      fprintf(stderr, "TotalCPUTime: metric not found\n");
   }
   metric_free(mdef);


   if (get_metric("TotalCPUTime", &mdef, METRIC_CONTEXT_VM) == 0) {
      double cpu_time = mdef->value.r64;
      fprintf(stderr, "VM TotalCPUTime: %f\n", cpu_time);
   }
   else {
      fprintf(stderr, "Indv VM TotalCPUTime: metric not found\n");
   }
   metric_free(mdef);

   /* Class metrics get, host cpu */
   cpu_rec = cpu_metrics_alloc();
   if (get_host_cpu_metrics(cpu_rec) == 0) {
      fprintf(stderr, "Class Host CPU\n");
      fprintf(stderr, "\t total_phys_cpus: %d\n", cpu_rec->total_phys_cpus);
      fprintf(stderr, "\t num_phys_cpus_utilized: %d\n", cpu_rec->num_phys_cpus_utilized);
      fprintf(stderr, "\t total_cpu_time: %lf\n", cpu_rec->total_cpu_time);
   }
   cpu_metrics_free(cpu_rec);

   /* Class metrics get, host memory */
   memory_rec = memory_metrics_alloc();
   if (get_host_memory_metrics(memory_rec) == 0) {
      fprintf(stderr, "Class Host Memory \n");
      fprintf(stderr, "\t total_physical_memory: %"PRIu64"\n", memory_rec->total_physical_memory);
      fprintf(stderr, "\t used_physical_memory: %"PRIu64"\n", memory_rec->used_physical_memory);
      fprintf(stderr, "\t free_physical_memory: %"PRIu64"\n", memory_rec->free_physical_memory);
      fprintf(stderr, "\t paged_in_memory: %"PRIu64"\n", memory_rec->paged_in_memory);
      fprintf(stderr, "\t paged_out_memory: %"PRIu64"\n", memory_rec->paged_out_memory);
      fprintf(stderr, "\t page_in_rate: %"PRIu64"\n", memory_rec->page_in_rate);
      fprintf(stderr, "\t paged_fault_rate: %"PRIu64"\n", memory_rec->page_fault_rate);
   }
   memory_metrics_free(memory_rec);

   return 0;
}
#endif

#ifdef USE_DL_OPEN
static int test_dlopen()
{
   metric *mdef;
   cpu_metrics *cpu_rec;
   memory_metrics *memory_rec;
   void *dlh;

   int (*getMetric)(const char *, metric **, metric_context);
   void (*metricFree)(metric *);

   memory_metrics *(*memoryMetricsAlloc)(void);
   void (*memoryMetricsFree)(memory_metrics *);

   cpu_metrics * (*cpuMetricsAlloc)(void);
   int (*cpuMetricsFree)(cpu_metrics *);

   int (*getHostCpuMetrics)(cpu_metrics *);
   int (*getHostMemoryMetrics)(memory_metrics *);

   if ((dlh = dlopen("libmetrics.so.0", RTLD_LAZY | RTLD_LOCAL)) == NULL) {
      fprintf(stderr, "Unable to open dynamic metrics library\n");
      exit(1);
   }

   /* get the dynamic function pointers */
   getMetric = dlsym(dlh, "get_metric");
   metricFree = dlsym(dlh, "metric_free");
   memoryMetricsAlloc = dlsym(dlh, "memory_metrics_alloc");
   memoryMetricsFree = dlsym(dlh, "memory_metrics_free");
   cpuMetricsAlloc = dlsym(dlh, "cpu_metrics_alloc");
   cpuMetricsFree = dlsym(dlh, "cpu_metrics_free");
   getHostCpuMetrics = dlsym(dlh, "get_host_cpu_metrics");
   getHostMemoryMetrics = dlsym(dlh, "get_host_memory_metrics");

   /* Generic metric get */
   if (getMetric("UsedMem", &mdef, METRIC_CONTEXT_HOST) == 0) {
      uint64_t usedMem = mdef->value.ui64;
      fprintf(stderr, "UsedMem: %"PRIu64"\n", usedMem);
   }
   else {
      fprintf(stderr, "UsedMem: metric not found\n");
   }
   metricFree(mdef);

   if (getMetric("TotalCPUTime", &mdef, METRIC_CONTEXT_HOST) == 0) {
      double cpu_time = mdef->value.r64;
      fprintf(stderr, "TotalCPUTime: %f\n", cpu_time);
   }
   else {
      fprintf(stderr, "TotalCPUTime: metric not found\n");
   }
   metricFree(mdef);


   if (getMetric("TotalCPUTime", &mdef, METRIC_CONTEXT_VM) == 0) {
      double cpu_time = mdef->value.r64;
      fprintf(stderr, "VM TotalCPUTime: %f\n", cpu_time);
   }
   else {
      fprintf(stderr, "Indv VM TotalCPUTime: metric not found\n");
   }
   metricFree(mdef);

   /* Class metrics get, host cpu */
   cpu_rec = cpuMetricsAlloc();
   if (getHostCpuMetrics(cpu_rec) == 0) {
      fprintf(stderr, "Class Host CPU\n");
      fprintf(stderr, "\t total_phys_cpus: %d\n", cpu_rec->total_phys_cpus);
      fprintf(stderr, "\t num_phys_cpus_utilized: %d\n", cpu_rec->num_phys_cpus_utilized);
      fprintf(stderr, "\t total_cpu_time: %lf\n", cpu_rec->total_cpu_time);
   }
   cpuMetricsFree(cpu_rec);

   /* Class metrics get, host memory */
   memory_rec = memoryMetricsAlloc();
   if (getHostMemoryMetrics(memory_rec) == 0) {
      fprintf(stderr, "Class Host Memory \n");
      fprintf(stderr, "\t total_physical_memory: %"PRIu64"\n", memory_rec->total_physical_memory);
      fprintf(stderr, "\t used_physical_memory: %"PRIu64"n", memory_rec->used_physical_memory);
      fprintf(stderr, "\t free_physical_memory: %"PRIu64"\n", memory_rec->free_physical_memory);
      fprintf(stderr, "\t paged_in_memory: %"PRIu64"\n", memory_rec->paged_in_memory);
      fprintf(stderr, "\t paged_out_memory: %"PRIu64"\n", memory_rec->paged_out_memory);
      fprintf(stderr, "\t page_in_rate: %"PRIu64"\n", memory_rec->page_in_rate);
      fprintf(stderr, "\t paged_fault_rate: %"PRIu64"\n", memory_rec->page_fault_rate);
   }
   memoryMetricsFree(memory_rec);

   dlclose(dlh);
   return 0;
}
#endif

int main(int argc, char *argv[])
{
   int verbose = 0;

   struct option opts[] = {
      { "verbose", no_argument, &verbose, 1},
      { "help", no_argument, NULL, '?' },
      {0, 0, 0, 0}
   };

   while (1) {
      int optidx = 0;
      int c;

      c = getopt_long(argc, argv, "f:v", opts, &optidx);

      if (c == -1)
         break;

      switch (c) {
         case 0:
            /* Got one of the flags */
            break;
         case 'v':
            verbose = 1;
            break;
         case '?':
            usage(argv[0]);
            return 2;
         default:
            fprintf(stderr, "guestmetrics: unknown option: %c\n", c);
            exit(1);
      }
   }
#ifdef USE_DL_OPEN
   test_dlopen();
#else
   test_static();
#endif
   return 0;
}

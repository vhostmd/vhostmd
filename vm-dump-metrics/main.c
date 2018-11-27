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

#include "libmetrics.h"

static void usage(const char *argv0)
{
   char *options_str = "Options:\n"
         "\t-v | --verbose         Verbose messages.\n"
         "\t-d | --dest            Metrics destination file .\n"
#ifdef WITH_XENSTORE
         "\t-x | --xenstore        Get metrics from xenstore.\n"
#endif
         "\t-i | --virtio          Get metrics from virtio channel.\n"
         "\t-b | --vbd             Get metrics from vbd.\n";

   fprintf (stderr, "\nUsage: %s [options]\n\n%s\n", argv0, options_str);
}

int main(int argc, char *argv[])
{
   int verbose = 0;
   int vbd = 0;
#ifdef WITH_XENSTORE
   int xenstore = 0;
#endif
   int virtio = 0;
   const char *dfile = NULL;

   struct option opts[] = {
      { "verbose", no_argument, &verbose, 1},
      { "vbd", no_argument, &vbd, 1},
#ifdef WITH_XENSTORE
      { "xenstore", no_argument, &xenstore, 1},
#endif
      { "virtio", no_argument, &virtio, 1},
      { "help", no_argument, NULL, '?' },
      { "dest", optional_argument, NULL, 'd'},
      {0, 0, 0, 0}
   };

   while (1) {
      int optidx = 0;
      int c;

#ifdef WITH_XENSTORE
      c = getopt_long(argc, argv, "d:vbix", opts, &optidx);
#else
      c = getopt_long(argc, argv, "d:vbi", opts, &optidx);
#endif

      if (c == -1)
         break;

      switch (c) {
         case 0:
            /* Got one of the flags */
            break;
         case 'v':
            verbose = 1;
            break;
         case 'b':
            vbd = 1;
            break;
         case 'i':
            virtio = 1;
            break;
#ifdef WITH_XENSTORE
         case 'x':
            xenstore = 1;
            break;
#endif
         case 'd':
            dfile = optarg;
            break;
         case '?':
            usage(argv[0]);
            return 2;
         default:
            fprintf(stderr, "guestmetrics: unknown option: %c\n", c);
            exit(1);
      }
   }

#ifdef WITH_XENSTORE
   if (xenstore) {
       if (dump_xenstore_metrics(dfile) == -1)
           exit(1);
       exit(0);
   }
#endif

   if (virtio) {
       if (dump_virtio_metrics(dfile) == -1)
           exit(1);
       exit(0);
   }

   if (vbd) {
       if (dump_metrics(dfile) == -1)
           exit(1);
       exit(0);
   }

   /*
    * If no metrics source is specfied, try default order
    * disk, virtio, xenstore
    */
   if (dump_metrics(dfile) == -1) {
       if (dump_virtio_metrics(dfile) == -1) {
#ifdef WITH_XENSTORE
           if (dump_xenstore_metrics(dfile) == -1)
               exit(1);
#else
           exit(1);
#endif
       }
   }

   exit(0);
}

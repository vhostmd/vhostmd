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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <libxml/xpath.h>
#ifdef WITH_XENSTORE
#include <xenstore.h>
#endif

#include "libmetrics.h"

typedef struct _mdisk_header
{
   uint32_t sig;
   uint32_t busy;
   uint32_t sum;
   uint32_t length;
} mdisk_header;

typedef struct _metric_disk {
   char uuid[256];
   char *disk_name;
   char *buffer;
   uint32_t sum;
   uint32_t length;
   xmlParserCtxtPtr pctxt;
   xmlDocPtr doc;
}metric_disk;

typedef struct _private_metric
{
   char *name;
   char *value;
   char *uuid; 
   char *context;
   metric_type type;
}private_metric;

#define MDISK_SIGNATURE     0x6d766264  /* 'mvbd' */
#define SYS_BLOCK    "/sys/block"
#define HOST_CONTEXT "host"
#define VM_CONTEXT   "vm"

/* Global variables */
static metric_disk *mdisk = NULL;
static pthread_mutex_t libmetrics_mutex; 

/*
 * Log library messages
 */
static void libmsg(const char *fmt, ...)
{
   va_list args;
   int len;

   va_start(args, fmt);
   fprintf(stderr, "LIBMETRICS: ");
   vfprintf(stderr, fmt, args);
   va_end(args);

   len = strlen(fmt);
   if (fmt[len -1] != '\n')
      fputc('\n', stderr);
}

static char *context_to_str(metric_context context)
{
   char *type;

   if (context == METRIC_CONTEXT_HOST)
      type = strdup(HOST_CONTEXT);
   else if (context == METRIC_CONTEXT_VM)
      type = strdup(VM_CONTEXT);
   else
      type = strdup(HOST_CONTEXT);

   return type;
}

static int metric_type_from_str(const char *t, metric_type *typ)
{
   int ret = 0;
   char *cp = (char *)t;
   
   if (strcasecmp(cp, "int32") == 0)
      *typ = M_INT32;
   else if (strcasecmp(cp, "uint32") == 0)
      *typ = M_UINT32;
   else if (strcasecmp(cp, "int64") == 0)
      *typ = M_INT64;
   else if (strcasecmp(cp, "uint64") == 0)
      *typ = M_UINT64;
   else if (strcasecmp(cp, "real32") == 0)
      *typ = M_REAL32;
   else if (strcasecmp(cp, "real64") == 0)
      *typ = M_REAL64;
   else if (strcasecmp(cp, "string") == 0)
      *typ = M_STRING;
   else
      ret = -1;
   
   return ret;
}

static int metric_value_str_to_type(metric *mdef, char *str)
{
   switch (mdef->type) {
      case M_INT32:
         mdef->value.i32 = atoi(str);
         break;
      case M_UINT32:
         mdef->value.ui32 = atoi(str);
         break;
      case M_INT64:
         mdef->value.ui64 = atoll(str);
         break;
      case M_UINT64:
         mdef->value.ui64 = atoll(str);
         break;
      case M_REAL32:
         mdef->value.r32 = atof(str);
         break;
      case M_REAL64:
         mdef->value.r64 = atof(str);
         break;
      case M_STRING:
         mdef->value.str = (char *)(mdef) + sizeof(metric);
         memcpy(mdef->value.str, str, strlen(str) + 1);
         break;
      default:
         libmsg("%s() Unknown type, can not convert:%x\n",
              __func__, mdef->type);
         break;
   }
   return 0;
}

/* 
 * Allocate metric disk
 */
static metric_disk * mdisk_alloc()
{
   mdisk = calloc(1, sizeof(metric_disk));
   return mdisk;
}

/* 
 * Free the metric disk content 
 */
static void mdisk_content_free()
{
   if (mdisk) {
      if (mdisk->doc)
          xmlFreeDoc(mdisk->doc);
      if (mdisk->pctxt)
         xmlFreeParserCtxt(mdisk->pctxt);
      if (mdisk->buffer)
         free(mdisk->buffer);
      if (mdisk->disk_name)
         free(mdisk->disk_name);
   }
}

/* 
 * Free the metric disk 
 */
static void mdisk_free()
{
   if (mdisk) {
      mdisk_content_free();
      if (mdisk)
         free(mdisk);
      mdisk = NULL;
   }
}

static metric *metric_alloc_padded(int pad)
{
   metric *m;

   m = calloc(1, sizeof(metric) + pad);

   return m;
}

/*
 * Get metric from the xml buffer, value set in pmdef
 */
static int get_mdef(metric_disk *mdisk, private_metric *pmdef)
{
   xmlXPathContextPtr ctxt = NULL;
   xmlXPathObjectPtr obj;
   xmlNodePtr node;
   char *str;
   char *xpath;
   int ret = 0;

   ctxt = xmlXPathNewContext(mdisk->doc);
   if (!ctxt) {
      return -1;
   }

   /* Get the matching metric node type */
   asprintf(&xpath, "//metrics/metric[name='%s'][@context='%s']", pmdef->name, pmdef->context);
   obj = xmlXPathEval(BAD_CAST xpath, ctxt);
   free(xpath);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      libmsg("%s(): No metrics found that matches %s in context:%s or malformed definition\n",
              __func__, pmdef->name, pmdef->context);
      ret = -1;
      goto out;
   }
   if (xmlXPathNodeSetGetLength(obj->nodesetval) != 1) {
      libmsg("%s(): No metrics found that matches %s in context:%s or malformed definition\n",
              __func__, pmdef->name, pmdef->context);
      ret = -1;
      goto out;
   }
   node = obj->nodesetval->nodeTab[0];
   if ((str = (char *)xmlGetProp(node, BAD_CAST "type")) == NULL) {
      libmsg("%s(): Metric type not specified\n", __func__);
      ret = -1;
      goto out;
   }
   metric_type_from_str((char *)str, &(pmdef->type));
   free(str);
   xmlXPathFreeObject(obj);

   /* Get the matching metric node value */
   asprintf(&xpath, "//metrics/metric[name='%s'][@context='%s']/value/text()", pmdef->name, pmdef->context);
   obj = xmlXPathEval( BAD_CAST xpath, ctxt);  /* worked but no nodes */
   free(xpath);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      libmsg("%s(): No metrics value found!\n", __func__);
      ret = -1;
      goto out;
   }

   /* Get the nodes value content */
   node = obj->nodesetval->nodeTab[0];
   str = (char *)xmlNodeListGetString(mdisk->doc, node, 1);
   pmdef->value = strdup(str);
   free(str);

out:
   if (obj)
      xmlXPathFreeObject(obj);
   if (ctxt)
      xmlXPathFreeContext(ctxt);
   return ret;
}

/* Read from an O_DIRECT device.  You can't do arbitrary reads on
 * such devices.  You can only read block-aligned block-sized
 * chunks of data into block-aligned memory.
 *
 * This returns 'size' bytes in 'buf', read from 'offset' in the
 * file 'fd'.
 */

/* There's no way to determine this, so just choose a large
 * block size.
 */
#define BLOCK_SIZE 65536

static int
odirect_read (int fd, void *buf, size_t offset, size_t size)
{
  if (lseek (fd, 0, SEEK_SET) == -1)
    return -1;

  size_t n = ((offset + size) + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
  void *mem;
  int r;
  r = posix_memalign (&mem, BLOCK_SIZE, n);
  if (r != 0) {
    errno = r;
    return -1;
  }

  if (read (fd, mem, n) != (ssize_t) n) {
    free (mem);
    return -1;
  }

  memcpy (buf, (char *) mem + offset, size);

  free (mem);
  return 0;
}

/*
 * Read metrics disk and populate mdisk
 *  Location of metrics disk is derived by looking at all block
 *  devices and then reading until a valid metrics disk signature
 *  is found.
 */
static int read_mdisk(metric_disk *mdisk)
{
   mdisk_header md_header;
   uint32_t busy;
   uint32_t sig;
   int fd;
   char *path;

   DIR* dir;
   struct dirent* entry;

   dir = opendir(SYS_BLOCK);
   if (dir == NULL)
      goto error;

   while((entry = readdir(dir))) {
retry:
#ifndef DEBUG_FROM_DOM0
      if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
         continue;

      asprintf(&path, "/dev/%s", entry->d_name);
#else
      path = strdup("/dev/shm/vhostmd0");
#endif
      /* Open with O_DIRECT to avoid kernel keeping old copies around
       * in the cache.
       */
      fd = open (path, O_RDONLY|O_DIRECT);
      if (fd == -1) {
         free (path);
         continue;
      }
      if (odirect_read (fd, &md_header, 0, sizeof md_header) == -1) {
         free (path);
	 close (fd);
         continue;
      }

      if ((sig = ntohl(md_header.sig)) == MDISK_SIGNATURE) {
         busy = ntohl(md_header.busy);
         if (busy) {
	     close(fd);
             free(path);
             sleep(1);
             goto retry;
         }
         mdisk->sum = ntohl(md_header.sum);
         mdisk->length = ntohl(md_header.length);
         mdisk->buffer = malloc(mdisk->length);
         mdisk->disk_name = strdup(path);
	 /* XXX check return value */
         odirect_read (fd, mdisk->buffer, sizeof md_header, mdisk->length);
	 free(path);

         /* Verify data still valid */
         if (odirect_read (fd, &md_header, 0, sizeof md_header) == -1) {
	     mdisk_content_free();
             close (fd);
             sleep(1);
             goto retry;
         }
         busy = ntohl(md_header.busy);
         if (busy || mdisk->sum != ntohl(md_header.sum)) {
             mdisk_content_free();
             close (fd);
             sleep(1);
             goto retry;
         }
         close (fd);
	 break;
      }
      close (fd);
   }

   if (mdisk->buffer == NULL)
      goto error;

   /* Set up a parser context */
   mdisk->pctxt = xmlNewParserCtxt();
   if (!mdisk->pctxt || !mdisk->pctxt->sax) {
      goto error;
   }

   mdisk->doc = xmlCtxtReadMemory(mdisk->pctxt, mdisk->buffer, 
           mdisk->length, "mdisk.xml", NULL, 
           XML_PARSE_NOENT | XML_PARSE_NONET |
           XML_PARSE_NOWARNING);
   if (!mdisk->doc) {
      libmsg("%s(): libxml failed to parse mdisk.xml buffer\n", __func__);
      goto error;
   }

   closedir(dir);

   return 0;
error:
   if (dir)
       closedir(dir);

   libmsg("%s(): Unable to read metrics disk\n", __func__);

   return -1;
}

/*
 * Read the sum from the metrics disk header
 */
static uint32_t read_mdisk_sum(metric_disk *mdisk)
{
   mdisk_header md_header;
   uint32_t sum = 0;
   int fd;

   if (mdisk == NULL || mdisk->disk_name == NULL)
       return 0;

   fd = open(mdisk->disk_name, O_RDONLY|O_DIRECT);
   if (fd == -1) 
       return 0;
   
   if (odirect_read (fd, &md_header, 0, sizeof md_header) == -1) {
       close(fd);
       return 0;
   }
   close (fd);

   if (ntohl(md_header.sig) == MDISK_SIGNATURE) {
      if (ntohl(md_header.busy)) {
         return 0;
      }
      sum = ntohl(md_header.sum);
   }

   return sum;
}

#ifdef WITH_XENSTORE
static int get_dom_id()
{
    struct xs_handle *xsh = NULL;
    char *domid = NULL;
    int domID = -1;
    unsigned int len;

    xsh = xs_domain_open();
    if (xsh == NULL) {
        libmsg("xs_domain_open: cannot open xenstore\n");
        return -1;
    }
    domid = xs_read(xsh, XBT_NULL, "domid", &len);
    if (domid) {
        domID = atoi(domid);
        free(domid);
    }

    return domID;
}
#endif

#ifdef NEED_UUID 
static int get_dom_uuid()
{
   FILE *fp;
   char *cp = NULL;

   if (mdisk == NULL)
       return -1;

   /* Get the UUID of this VM */
   fp = fopen("/sys/hypervisor/uuid", "r");
   if (fp != NULL) {
      fread (mdisk->uuid, sizeof (mdisk->uuid) - 1, 1, fp);
      fclose (fp);
      if ((cp = strrchr(mdisk->uuid, '\n'))) 
         *cp = '\0';
   }
#ifdef WITH_XENSTORE
   else if ((fp = popen("xenstore-read vm", "r"))) {
      char buffer[256];
      if (fread(buffer, 1, 256, fp)) {
         if ((cp = strrchr(buffer, '/'))) {
            cp++;
            strcpy(mdisk->uuid, cp);
         }
      }
   }
#endif
   else {
      libmsg("%s(): Error, unable to determine VM uuid\n", __func__);
      goto error;
   }
   return 0;

error:
   return -1;
}
#endif

/*
 * Allocate group metric(s) structs
 */
memory_metrics *memory_metrics_alloc(void)
{
   memory_metrics *m;

   m = calloc(1, sizeof(memory_metrics));

   return m;
}

cpu_metrics *cpu_metrics_alloc(void)
{
   cpu_metrics *m;

   m = calloc(1, sizeof(cpu_metrics));

   return m;
}

/*
 * Free metric(s) allocated by this lib
 */
void metric_free(metric *rec)
{
   if (rec)
      free(rec);
}

void memory_metrics_free(memory_metrics *rec)
{
   if (rec)
      free(rec);
}

void cpu_metrics_free(cpu_metrics *rec)
{
   if (rec)
      free(rec);
}


/*
 * Get metric
 */
int get_metric(const char *metric_name, metric **mdef, metric_context context) 
{
   private_metric pmdef;
   metric *lmdef;
   uint32_t sum;
   int extra_len = 0;
   int ret = -1;

   *mdef = NULL;

   if (mdisk == NULL) {
       errno = ENODEV;
       return -1;
   }

   /* lock library data */
   pthread_mutex_lock(&libmetrics_mutex);

   /* refresh library data if sum changed */
   sum = read_mdisk_sum(mdisk);
   if (sum != mdisk->sum) {
       mdisk_free();
       if (mdisk_alloc() == NULL) {
           errno = ENOMEM;
           return -1;
       }
       read_mdisk(mdisk);
   }

   pmdef.name = strdup(metric_name);
   pmdef.context = context_to_str(context);
   pmdef.uuid = mdisk->uuid;
   pmdef.value = NULL;
   pmdef.type = 0;

   if (get_mdef(mdisk, &pmdef) == 0) {

      if (pmdef.type == M_STRING) {
         extra_len = strlen(pmdef.value) + 1;
      }
   
      if ((lmdef = metric_alloc_padded(extra_len)) == NULL) {
         errno = ENOMEM;
         return -1;
      }

      lmdef->type = pmdef.type;
      metric_value_str_to_type(lmdef, pmdef.value);
      *mdef = lmdef;
      ret = 0;
   }
   if (pmdef.name)
      free(pmdef.name);
   if (pmdef.value)
      free(pmdef.value);
   if (pmdef.context)
      free(pmdef.context);

   /* unlock library data */
   pthread_mutex_unlock(&libmetrics_mutex);
   return ret;
}

/*
 * Initialize metrics library data
 */
void __attribute__ ((constructor)) libmetrics_init(void)
{

   xmlInitParser();

   pthread_mutex_init(&libmetrics_mutex, NULL);

   if (mdisk_alloc() == NULL) 
      goto error;

   mdisk->sum = 0;
   return;

error:
   mdisk_free();
}

/*
 * Destroy metrics library data
 */
void __attribute__ ((destructor)) libmetrics_fini(void){
   mdisk_free();
   pthread_mutex_destroy(&libmetrics_mutex);
   xmlCleanupParser();
}

int dump_metrics(const char *dest_file)
{
    FILE *fp;

    if (mdisk == NULL || read_mdisk(mdisk) < 0) {
        errno = ENOMEDIUM;
        return -1;
    }

    if (dest_file) {
        fp = fopen(dest_file, "w");
        if (fp == NULL) {
            libmsg("Error, unable to dump metrics: %s\n", strerror(errno));
            return -1;
        }
    }
    else {
        fp = stdout;
    }

    if (fwrite(mdisk->buffer, 1, mdisk->length, fp) != mdisk->length) {
        libmsg("Error, unable to export metrics to file:%s - error:%s\n",
                dest_file ? dest_file : "stdout", strerror(errno));
    }
    if (dest_file)
        fclose(fp);

    return 0;
}

#ifdef WITH_XENSTORE
/*
 * dump metrics received from xenstore to the dest file 
 */
int dump_xenstore_metrics(const char *dest_file)
{
    char *buf = NULL, *path = NULL, *metrics = NULL;
    struct xs_handle *xsh = NULL;
    unsigned int len;
    int ret = 0;
	xmlParserCtxtPtr pctxt = NULL;
	xmlDocPtr doc = NULL;
    int domid;
    FILE *fp;

    if (dest_file) {
        fp = fopen(dest_file, "w");
        if (fp == NULL) {
            libmsg("Error, unable to dump metrics from xenstore: %s\n", strerror(errno));
            return -1;
        }
    }
    else {
        fp = stdout;
    }

    if ((domid = get_dom_id()) == -1) {
        libmsg("Unable to derive domID.\n" );
        ret = -1;
        goto out;
    }

    xsh = xs_domain_open();
    if (xsh == NULL) {
        libmsg("xs_domain_open() error. errno: %d.\n", errno);
        ret = -1;
        goto out;
    }

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        libmsg("xs_get_domain_path() error. domid %d.\n", 0);
        ret = -1;
        goto out;
    }
    asprintf(&buf, "%s/metrics", path);
    metrics = xs_read(xsh, XBT_NULL, buf, &len);
    if (metrics == NULL) {
        libmsg("xs_read(): uuid get error. %s.\n", buf);
        ret = -1;
        goto out;
    }

    pctxt = xmlNewParserCtxt();
    if (!pctxt || !pctxt->sax) {
      libmsg("%s(): failed to create parser \n", __func__);
      ret = -1;
      goto out;
    }

    doc = xmlCtxtReadMemory(pctxt, metrics,
                            strlen(metrics), "mdisk.xml", NULL,
                            XML_PARSE_NOENT | XML_PARSE_NONET |
                            XML_PARSE_NOWARNING);
    if (!doc) {
      libmsg("%s(): libxml failed to xenstore metrics attribute\n", __func__);
      ret = -1;
      goto out;
    }
    xmlDocFormatDump(fp, doc, 1);

out:
    if (fp && fp != stdout)
        fclose(fp);
    if (doc)
      xmlFreeDoc(doc);
    if (pctxt)
      xmlFreeParserCtxt(pctxt);
    free(path);
    free(buf);
    free(metrics);
    return ret;
}
#endif

/*
 * dump metrics from virtio serial port to buffer
 */
static char *get_virtio_metrics(void)
{
    const char request[] = "GET /metrics/XML\n\n", end_token[] = "\n\n";
    const char dev[] = "/dev/virtio-ports/org.github.vhostmd.1";

    char *response = NULL;
    int fd = -1;
    size_t pos;
    size_t buf_size = (1 << 16);
    const size_t req_len = (size_t) strlen(request);
    const time_t timeout = 5;
    time_t end_time;

    response = calloc(1UL, buf_size);
    if (response == NULL)
        goto error;

    fd = open(dev, O_RDWR | O_NONBLOCK);

    if (fd < 0) {
        libmsg("%s(): Unable to export metrics: open(%s) %s\n",
                __func__, dev, strerror(errno));
        goto error;
    }

    pos = 0;
    end_time = time(NULL) + timeout;
    while (pos < req_len) {
        ssize_t len = write(fd, &request[pos], req_len - pos);
        if (len > 0)
            pos += (size_t) len;
        else {
            if (errno == EAGAIN) {
                usleep(10000);
                if (time(NULL) > end_time) {
                    libmsg("%s(): Unable to send metrics request"
                            " - timeout after %us\n", __func__, timeout);
                    goto error;
                }
            }
            else
                goto error;
        }
    }

    pos = 0;
    end_time = time(NULL) + timeout;
    do {
        ssize_t len = read(fd, &response[pos], buf_size - pos - 1);
        if (len > 0) {
            pos += (size_t) len;
            response[pos] = 0;

            if ((pos + 1) >= buf_size) {
                buf_size = buf_size << 1;  /* increase response buffer */
                if (buf_size > (1 << 24))  /* max 16MB */
                    goto error;

                response = realloc(response, buf_size);
                if (response == NULL)
                    goto error;

                memset(&response[pos], 0, buf_size - pos);
            }
        } else {
            if (errno == EAGAIN) {
                usleep(10000);
                if (time(NULL) > end_time) {
                    libmsg("%s(): Unable to read metrics"
                            " - timeout after %us\n", __func__, timeout);
                    goto error;
                }
            } else
                goto error;
        }
    } while ((pos < (size_t) strlen(end_token) ||
             strcmp(end_token, &response[pos - (size_t) strlen(end_token)]) != 0) &&
             pos < buf_size);

    if (fd >= 0)
        close(fd);

    return response;

  error:
    if (fd >= 0)
        close(fd);
    if (response)
        free(response);

    libmsg("%s(): Unable to read metrics\n", __func__);

    return NULL;
}

/*
 * dump metrics from virtio serial port to xml formatted file
 */
int dump_virtio_metrics(const char *dest_file)
{
    FILE *fp = stdout;
    char *response = NULL;
    size_t len;

    response = get_virtio_metrics();
    if (response == NULL)
        goto error;

    len = strlen(response);

    if (dest_file) {
        fp = fopen(dest_file, "w");
        if (fp == NULL) {
            libmsg("%s(), unable to dump metrics: fopen(%s) %s\n",
                   __func__, dest_file, strerror(errno));
            goto error;
        }
    }

    if (fwrite(response, 1UL, len, fp) != len) {
        libmsg("%s(), unable to export metrics to file:%s %s\n",
                __func__, dest_file ? dest_file : "stdout", strerror(errno));
        goto error;
    }

    if (response)
        free(response);

    return 0;

  error:
    if (dest_file && fp)
        fclose(fp);

    if (response)
        free(response);

    return -1;
}

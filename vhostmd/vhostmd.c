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

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <paths.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "util.h"
#include "metric.h"


/*
 * vhostmd will periodically write metrics to a disk.  The metrics
 * to write, how often, and where to write them are all adjustable
 * via the vhostmd.xml configuration file.
 *
 * Currently, the disk format is quite simple: a raw, memory-backed
 * disk containing
 * - 4 byte signature, big endian
 * - 4 byte busy flag, big endian
 * - 4 byte content checksum, big endian
 * - 4 byte content length, big endian
 * - content
 */

#define MDISK_SIZE_MIN      1024
#define MDISK_SIZE_MAX      (256 * 1024 * 1024)
#define MDISK_SIGNATURE     0x6d766264  /* 'mvbd' */

typedef struct _mdisk_header
{
   uint32_t sig;
   uint32_t busy;
   uint32_t sum;
   uint32_t length;
} mdisk_header;

#define MDISK_HEADER_SIZE   (sizeof(mdisk_header))

/* 
 * Macro for determining usable size of metrics disk
 */
#define MDISK_SIZE          (mdisk_size - MDISK_HEADER_SIZE)

/*
 * Transports
 */
#define VBD      (1 << 0)
#define XENSTORE (1 << 1)

/* Global variables */
static int down = 0;
static int mdisk_size = MDISK_SIZE_MIN;
static int update_period = 5;
static char *def_mdisk_path = "/dev/shm/vhostmd0";
static char *mdisk_path = NULL;
static char *pid_file = "/var/run/vhostmd.pid";
static metric *metrics = NULL;
static mdisk_header md_header =
         {
            .sig = 0,
            .busy = 0,
            .sum = 0,
            .length = 0,
         };
static char *search_path = NULL;
static int transports = 0;


/**********************************************************************
 * Basic daemon support functions
 *********************************************************************/

static void sig_handler(int sig, siginfo_t *siginfo ATTRIBUTE_UNUSED,
                        void *context ATTRIBUTE_UNUSED)
{
   switch (sig) {
      case SIGINT:
      case SIGTERM:
      case SIGQUIT:
         down = 1;
         break;
      default:
         break;
   }
}

static int write_pid_file(const char *pfile)
{
   int fd;
   FILE *fh;

   if (pfile[0] == '\0')
      return 0;

   if ((fd = open(pfile, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0) {
      vu_log(VHOSTMD_ERR, "Failed to open pid file '%s' : %s",
                  pfile, strerror(errno));
      return -1;
   }

   if (!(fh = fdopen(fd, "w"))) {
      vu_log(VHOSTMD_ERR, "Failed to fdopen pid file '%s' : %s",
                  pfile, strerror(errno));
      close(fd);
      return -1;
   }

   if (fprintf(fh, "%lu\n", (unsigned long)getpid()) < 0) {
      vu_log(VHOSTMD_ERR, "Failed to write to pid file '%s' : %s",
                  pfile, strerror(errno));
      close(fd);
      return -1;
   }

   if (fclose(fh) == EOF) {
      vu_log(VHOSTMD_ERR, "Failed to close pid file '%s' : %s",
                  pfile, strerror(errno));
      return -1;
   }

   return 0;
}

static int daemonize(void)
{
   int pid = fork();
   switch (pid) {
      case 0:
      {
         int stdinfd = -1;
         int stdoutfd = -1;
         int nextpid;
         
         if ((stdinfd = open(_PATH_DEVNULL, O_RDONLY)) < 0)
            goto cleanup;
         if ((stdoutfd = open(_PATH_DEVNULL, O_WRONLY)) < 0)
            goto cleanup;
         if (dup2(stdinfd, STDIN_FILENO) != STDIN_FILENO)
            goto cleanup;
         if (dup2(stdoutfd, STDOUT_FILENO) != STDOUT_FILENO)
            goto cleanup;
         if (dup2(stdoutfd, STDERR_FILENO) != STDERR_FILENO)
            goto cleanup;
         if (close(stdinfd) < 0)
            goto cleanup;
         stdinfd = -1;
         if (close(stdoutfd) < 0)
            goto cleanup;
         stdoutfd = -1;

         if (chdir ("/") == -1)
            goto cleanup;
         
         if (setsid() < 0)
            goto cleanup;
         
         nextpid = fork();
         switch (nextpid) {
            case 0:
               return 0;
            case -1:
               return -1;
            default:
               _exit(0);
         }
         
      cleanup:
         if (stdoutfd != -1)
            close(stdoutfd);
         if (stdinfd != -1)
            close(stdinfd);
         return -1;
         
      }
      
      case -1:
         return -1;
         
      default:
      {
         int got, status = 0;
         /* We wait to make sure the next child forked successfully */
         if ((got = waitpid(pid, &status, 0)) < 0 ||
             got != pid || status != 0) {
            return -1;
         }
         _exit(0);
      }
   }
}

/**********************************************************************
 * Config file parsing functions
 *********************************************************************/

/* Parse a XML group metric node and return success indication */
static int parse_group_metric(xmlDocPtr xml ATTRIBUTE_UNUSED,
                              xmlXPathContextPtr ctxt, xmlNodePtr node, metric *mdef)
{
   xmlXPathObjectPtr obj = NULL;
   xmlChar *path = NULL;
   char *cp = NULL;
   xmlChar *prop;
   int ret = -1;
   int i;

   free(mdef->name);
   free(mdef->type_str);
   mdef->name = NULL;
   mdef->type_str = NULL;
   mdef->cnt = 0;

   path = xmlGetNodePath(node);
   if (path == NULL) {
      vu_log(VHOSTMD_WARN, "parse_group_metric: node path not found");
      return -1;
   }
   asprintf(&cp, "%s/variable", path);

   obj = xmlXPathEval( BAD_CAST cp, ctxt);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      vu_log(VHOSTMD_WARN, "parse_group_metric: variable set not found");
      goto error;
   }

   mdef->cnt = xmlXPathNodeSetGetLength(obj->nodesetval);
   vu_log(VHOSTMD_INFO, "parse_group_metric: number of variable nodes: %d", mdef->cnt);
   for (i = 0; i < mdef->cnt; i++) {
      xmlNode *n = obj->nodesetval->nodeTab[i];
      if ((prop = xmlGetProp(n, BAD_CAST "name")) == NULL) {
         vu_log(VHOSTMD_WARN, "parse_group_metric: metric name not specified");
         goto error;
      }
      vu_append_string(&mdef->name, prop);
      free(prop);

      if ((prop = xmlGetProp(n, BAD_CAST "type")) == NULL) {
         vu_log(VHOSTMD_WARN, "parse_group_metric: metric type not specified");
         goto error;
      }
      vu_append_string(&mdef->type_str, prop);
      free(prop);
   }
   ret = 0;
error:
   free(path);
   free(cp);
   if (obj)
      xmlXPathFreeObject(obj);
   return ret;
}

/* Parse a XML metric node and return a metric definition */
static metric *parse_metric(xmlDocPtr xml, xmlXPathContextPtr ctxt, xmlNodePtr node)
{
   metric *mdef = NULL;
   xmlNodePtr cur;
   xmlChar *mtype = NULL;
   xmlChar *mcontext = NULL;
   xmlChar *str;

   mdef = calloc(1, sizeof(metric));
   if (mdef == NULL) {
      vu_log(VHOSTMD_WARN, "Unable to allocate memory for "
                  "metrics definition");
      return NULL;
   }
   
   /* Get the metric type attribute */
   if ((mtype = xmlGetProp(node, BAD_CAST "type")) == NULL) {
      vu_log(VHOSTMD_WARN, "metric type not specified");
      goto error;
   }

   if (metric_type_from_str(mtype, &(mdef->type))) {
      vu_log(VHOSTMD_WARN, "Unsupported metric type %s", mtype);
      goto error;
   }
   mdef->type_str = strdup((char *)mtype);

   /* Get the metric context attribute */
   if ((mcontext = xmlGetProp(node, BAD_CAST "context")) == NULL) {
      vu_log(VHOSTMD_WARN, "metric context not specified");
      goto error;
   }
   if (xmlStrEqual(mcontext, BAD_CAST "host"))
      mdef->ctx = METRIC_CONTEXT_HOST;
   else if (xmlStrEqual(mcontext, BAD_CAST "vm"))
      mdef->ctx = METRIC_CONTEXT_VM;
   else {
      vu_log(VHOSTMD_WARN, "Unsupported metric context (%s) :"
                  "supported contexts (host) and (vm)", mcontext);
      goto error;
   }
      
   /* Get the metric name and the action */
   cur = node->xmlChildrenNode;

   while(cur != NULL) {
      str = xmlNodeListGetString(xml, cur->xmlChildrenNode, 1);
      if (str && xmlStrEqual(cur->name, BAD_CAST "name")) {
         mdef->name= strdup((char *)str);
      }
      if (str && xmlStrEqual(cur->name, BAD_CAST "action")) {
         mdef->action = strdup((char *)str);
      }
      if (str)
         free(str);
      cur = cur->next;
   }
   
   if (mdef->name == NULL) {
         vu_log(VHOSTMD_WARN, "Metric name not specified");
         goto error;
   }
   if (mdef->action == NULL) {
         vu_log(VHOSTMD_WARN, "Metric action not specified");
         goto error;
   }

   vu_log(VHOSTMD_INFO, "Adding %s metric '%s'",
               mdef->ctx == METRIC_CONTEXT_HOST ? "host" : "vm",
               mdef->name);
   vu_log(VHOSTMD_INFO, "\t action: %s", mdef->action);

   mdef->cnt = 1;
   if (mdef->type == M_GROUP) {
      if (parse_group_metric(xml, ctxt, node, mdef) == -1) {
         goto error;
      }
   }

   free(mtype);
   free(mcontext);

   return mdef;
   
 error:
   if (mdef) {
      free(mdef->name);
      free(mdef->action);
      free(mdef->type_str);
      free(mdef);
   }
   free(mtype);
   free(mcontext);
   
   return NULL;
}

/* Parse metrics nodes contained in XML doc */
static int parse_metrics(xmlDocPtr xml,
                         xmlXPathContextPtr ctxt)
{
   xmlXPathObjectPtr obj;
   xmlNodePtr relnode;
   metric *mdef;
   int num = 0;
   int i;
   
   if (ctxt == NULL) {
      vu_log(VHOSTMD_ERR, "Invalid parameter to parse_metrics");
      return -1;
   }

   relnode = ctxt->node;
   
   obj = xmlXPathEval( BAD_CAST "//vhostmd/metrics/metric", ctxt);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      xmlXPathFreeObject(obj);
      vu_log(VHOSTMD_WARN, "No metrics found or malformed definition");
      return -1;
   }

   num = xmlXPathNodeSetGetLength(obj->nodesetval);
   vu_log(VHOSTMD_INFO, "Number of metrics nodes: %d", num);
   for (i = 0; i < num; i++) {
      mdef = parse_metric(xml, ctxt, obj->nodesetval->nodeTab[i]);
      if (mdef) {
         mdef->next = metrics;
         metrics = mdef;
      }
      else {
         vu_log(VHOSTMD_WARN, "Unable to parse metric node, ignoring ...");
         continue;
      }
   }

   xmlXPathFreeObject(obj);
   ctxt->node = relnode;
   return 0;
}

static int parse_transports(xmlDocPtr xml,
                         xmlXPathContextPtr ctxt)
{
   xmlXPathObjectPtr obj;
   xmlNodePtr relnode;
   xmlNodePtr cur;
   xmlChar *str;
   int num = 0;
   int i;
   
   if (ctxt == NULL) {
      vu_log(VHOSTMD_ERR, "Invalid parameter to parse_transports");
      return -1;
   }

   relnode = ctxt->node;
   
   obj = xmlXPathEval( BAD_CAST "//vhostmd/globals/transport", ctxt);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      xmlXPathFreeObject(obj);
      vu_log(VHOSTMD_WARN, "No transport found or malformed definition");
      return -1;
   }

   num = xmlXPathNodeSetGetLength(obj->nodesetval);
   for (i = 0; i < num; i++) {
      cur = obj->nodesetval->nodeTab[i]->xmlChildrenNode;
      str = xmlNodeListGetString(xml, cur, 1);
      if (str) {
         if (strncasecmp((char *)str, "vbd", strlen("vbd")) == 0)
             transports |= VBD;
         if (strncasecmp((char *)str, "xenstore", strlen("xenstore")) == 0) {
#ifdef WITH_XENSTORE
             transports |= XENSTORE;
#else
	     vu_log (VHOSTMD_ERR, "No support for xenstore transport in this vhostmd");
	     return -1;
#endif
	 }
         free(str);
      }
   }
   xmlXPathFreeObject(obj);
   ctxt->node = relnode;
   /* Should not happen */
   if (transports == 0)
       transports = VBD;

   return 0;
}

static int validate_config_file(const char *filename)
{
    xmlDocPtr doc = NULL;
    xmlParserCtxtPtr pctxt = NULL;
    xmlNode *root_element = NULL;
    int ret = -1;

    pctxt = xmlNewParserCtxt();
    if (!pctxt || !pctxt->sax) {
        vu_log(VHOSTMD_ERR, "%s(): failed to allocate parser context \n", __FUNCTION__);
        goto error;
    }

    doc = xmlCtxtReadFile(pctxt, filename, NULL, XML_PARSE_DTDVALID);
    if (!doc) {
        vu_log(VHOSTMD_ERR, "%s(): could not read file:%s \n", __FUNCTION__, filename);
        goto error;
    }
    if (pctxt->valid == 0) {
        vu_log(VHOSTMD_ERR, "%s(): Failed to validate :%s \n", __FUNCTION__, filename);
        goto error;
    }

    root_element = xmlDocGetRootElement(doc);
    if (!root_element) {
        vu_log(VHOSTMD_ERR, "%s(): could not locate root element\n", __FUNCTION__);
        goto error;
    }

    if (xmlStrncmp((const xmlChar*)"vhostmd", root_element->name, strlen("vhostmd")) != 0) {
        vu_log(VHOSTMD_ERR, "%s(): Incorrect root element name:%s\n", __FUNCTION__,
                    root_element->name);
        goto error;
    }
    ret = 0;

error:
    //if (root_element)
       //xmlFreeNode(root_element);
    if (doc)
       xmlFreeDoc(doc);
    if (pctxt)
       xmlFreeParserCtxt(pctxt);
    return(ret);

}

/* Parse vhostmd configuration file */
static int parse_config_file(const char *filename)
{
   xmlParserCtxtPtr pctxt = NULL;
   xmlDocPtr xml = NULL;
   xmlXPathContextPtr ctxt = NULL;
   xmlNodePtr root;
   //config_file_element *element = NULL;
   char *unit = NULL;
   long l;
   int ret = -1;

   /* Set up a parser context so we can catch the details of XML errors. */
   pctxt = xmlNewParserCtxt();
   if (!pctxt || !pctxt->sax)
      goto out;

   xml = xmlCtxtReadFile(pctxt, filename, NULL,
                         XML_PARSE_NOENT | XML_PARSE_NONET |
                         XML_PARSE_NOWARNING);
   if (!xml) {
      vu_log(VHOSTMD_ERR, "libxml failed to parse config file %s",
                  filename);
      goto out;
   }

   if ((root = xmlDocGetRootElement(xml)) == NULL) {
      vu_log(VHOSTMD_ERR, "Config file %s missing root element",
                  filename);
      goto out;
   }

   if (!xmlStrEqual(root->name, BAD_CAST "vhostmd")) {
      vu_log(VHOSTMD_ERR, "Config file contains incorrect root element");
      goto out;
   }

   ctxt = xmlXPathNewContext(xml);
   if (ctxt == NULL) {
      vu_log(VHOSTMD_ERR, "Unable to allocate memory");
      goto out;
   }

   ctxt->node = root;

   /* Get global settings */
   mdisk_path = vu_xpath_string("string(./globals/disk/path[1])", ctxt);
   if (mdisk_path == NULL)
      mdisk_path = strdup(def_mdisk_path);

   unit = vu_xpath_string("string(./globals/disk/size[1]/@unit)", ctxt);
   if (vu_xpath_long("string(./globals/disk/size[1])", ctxt, &l) == 0) {
      mdisk_size = vu_val_by_unit(unit, (int)l);
   }
   else {
      vu_log(VHOSTMD_ERR, "Unable to parse metrics disk size");
      goto out;
   }

   if (vu_xpath_long("string(./globals/update_period[1])", ctxt, &l) == 0) {
      update_period = (int)l;
   }
   else {
      vu_log(VHOSTMD_ERR, "Unable to parse update period");
      goto out;
   }

   if ((search_path = vu_xpath_string("string(./globals/path[1])", ctxt)) != NULL) {
      setenv("PATH", search_path, 1);
   }

   if (parse_transports(xml, ctxt) == -1) {
      vu_log(VHOSTMD_ERR, "Unable to parse transports");
      goto out;
   }
    
   /* Parse requested metrics definitions */
   if (parse_metrics(xml, ctxt)) {
      vu_log(VHOSTMD_ERR, "Unable to parse metrics definition "
                  "in vhostmd config file");
      goto out;
   }
      
   ret = 0;

 out:
   free(unit);
   xmlXPathFreeContext(ctxt);
   xmlFreeDoc(xml);
   xmlFreeParserCtxt(pctxt);
   return ret;
}

/**********************************************************************
 * daemon-specific functions
 *********************************************************************/

/* Ensure valid config settings, returning non-zero of failure */
static int check_config(void)
{
   /* check valid disk path */
   if (!mdisk_path) {
      vu_log(VHOSTMD_ERR, "Metrics disk path not specified");
      return -1;
   }

   /* check valid disk size */
   if (mdisk_size < MDISK_SIZE_MIN || mdisk_size > MDISK_SIZE_MAX) {
      vu_log(VHOSTMD_ERR, "Specified metrics disk size "
                  "(%d) not within supported range: (%d - %d)",
                  mdisk_size, MDISK_SIZE_MIN, MDISK_SIZE_MAX);
      return -1;
   }

   /* check valid update period */
   if (update_period < 1) {
      vu_log(VHOSTMD_ERR, "Specified update period (%d) less "
                  "than minimum supported (1)",
                  update_period);
      return -1;
   }

   vu_log(VHOSTMD_INFO, "Using metrics disk path %s", mdisk_path);
   vu_log(VHOSTMD_INFO, "Using metrics disk size %d", mdisk_size);
   vu_log(VHOSTMD_INFO, "Using update period of %d seconds",
               update_period);

   return 0;
}

static int metrics_disk_busy(int fd, int busy)
{
   md_header.busy = (uint32_t)(htonl(busy));
   
   lseek(fd, offsetof(mdisk_header, busy), SEEK_SET);
   write(fd, &(md_header.busy), sizeof(uint32_t));
   return 0;
}

static int metrics_disk_header_update(int fd, vu_buffer *buf)
{
   uint32_t sum;

   md_header.sig = htonl(MDISK_SIGNATURE);
   md_header.length = 0;
   sum = md_header.sum = 0;
   
   if (buf) {
      md_header.length = htonl(buf->use);
      md_header.sum = sum = htonl(vu_buffer_checksum(buf));
   }

   if (lseek(fd, offsetof(mdisk_header, sig), SEEK_SET) == -1)
       goto error;
   if (write(fd, &(md_header.sig), sizeof(uint32_t)) != sizeof(uint32_t)) {
       vu_log(VHOSTMD_ERR, "Error writing metrics disk header sig: %s",
               strerror(errno));
       goto error;
   }

   if (lseek(fd, offsetof(mdisk_header, sum), SEEK_SET) == -1)
       goto error;
   if (write(fd, &sum, sizeof(uint32_t)) != sizeof(uint32_t)) {
       vu_log(VHOSTMD_ERR, "Error writing metrics disk header sum: %s",
               strerror(errno));
       goto error;
   }

   if (lseek(fd, offsetof(mdisk_header, length), SEEK_SET) == -1)
       goto error;
   if (write(fd, &(md_header.length), sizeof(uint32_t)) != sizeof(uint32_t)) {
       vu_log(VHOSTMD_ERR, "Error writing metrics disk header length: %s",
               strerror(errno));
       goto error;
   }

   return 0;
error:
   return -1;
}

static int metrics_disk_update(int fd, vu_buffer *buf)
{
   if (buf->use > MDISK_SIZE) {
      vu_log(VHOSTMD_ERR, "Metrics data is larger than metrics disk");
      return -1;
   }
      
   metrics_disk_busy(fd, 1);
   metrics_disk_header_update(fd, buf);
   lseek(fd, MDISK_HEADER_SIZE, SEEK_SET);
   write(fd, buf->content, buf->use);
   metrics_disk_busy(fd, 0);

   return 0;
}

static int metrics_free()
{
   metric *m = metrics;
   metric *m_old;

   while (m) {
      if (m->name)
         free(m->name);
      if (m->action)
         free(m->action);
      if (m->value)
         free(m->value);
      if (m->type_str)
         free(m->type_str);
      m_old = m;
      m = m->next;
      free(m_old);
   }
   return 0;
}

static void metrics_disk_close(int fd)
{
   if (fd != -1)
      close(fd);
   if (mdisk_path) {
      free(mdisk_path);
   }
   if (search_path)
      free(search_path);
   metrics_free();
}

static int metrics_disk_create(void)
{
   char *dir = NULL;
   char *tmp;
   char *buf = NULL;
   int fd = -1;
   int i;
   int size = mdisk_size - MDISK_HEADER_SIZE;
   
   /* create directory */
   if ((tmp = strrchr(mdisk_path, '/'))) {
      dir = strndup(mdisk_path, tmp - mdisk_path);
      if (dir == NULL) {
         vu_log(VHOSTMD_ERR, "Unable to allocate memory");
         return -1;
      }

      if ((mkdir(dir, 0700) < 0) && (errno != EEXIST)) {
         vu_log(VHOSTMD_ERR, "Unable to create directory '%s' "
                     "for metrics disk: %s", dir, strerror(errno));
         goto error;
      }
   }
   
   /* buffer for zero filling disk to requested size */
   buf = calloc(1, MDISK_SIZE_MIN);
   if (buf == NULL) {
      vu_log(VHOSTMD_ERR, "Unable to allocate memory");
      goto error;
   }

   /* create disk */
   fd = open(mdisk_path, O_RDWR | O_CREAT | O_TRUNC,
             (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
   if (fd < 0) {
      vu_log(VHOSTMD_ERR, "Failed to open metrics disk: %s",
                  strerror(errno));
      goto error;
   }

   /* write header, mark disk as busy */
   metrics_disk_busy(fd, 1);
   if (metrics_disk_header_update(fd, NULL)) {
      vu_log(VHOSTMD_ERR, "Error writing metrics disk header");
      goto error;
   }

   /* truncate to a possible new size */
   ftruncate(fd, mdisk_size);

   /* zero fill metrics data */
   lseek(fd, MDISK_HEADER_SIZE, SEEK_SET);
   for (i = 0; i < size / MDISK_SIZE_MIN; i++)
      if (write(fd, buf, MDISK_SIZE_MIN) != MDISK_SIZE_MIN) {
         vu_log(VHOSTMD_ERR, "Error creating disk of requested "
                     "size: %s", strerror(errno));
         goto error;
      }
   if (write(fd, buf, size % MDISK_SIZE_MIN) <
       size % MDISK_SIZE_MIN ) {
         vu_log(VHOSTMD_ERR, "Error creating disk of requested "
                     "size: %s", strerror(errno));
         goto error;
   }

   free(dir);
   free(buf);
   return fd;

 error:
   free(dir);
   free(buf);
   close(fd);
   return -1;
}

static int metrics_host_get(vu_buffer *buf)
{
   metric *m = metrics;
   
   while (m) {
      if (m->ctx != METRIC_CONTEXT_HOST) {
         m = m->next;
         continue;
      }
      
      if (metric_xml(m, buf))
         vu_log(VHOSTMD_ERR, "Error retrieving metric %s", m->name);
         
      m = m->next;
   }
   return 0;
}

static int metrics_vm_get(vu_vm *vm, vu_buffer *buf)
{
   metric *m = metrics;

   while (m) {
      if (m->ctx != METRIC_CONTEXT_VM) {
         m = m->next;
         continue;
      }
      m->vm = vm;
      if (metric_xml(m, buf))
         vu_log(VHOSTMD_ERR, "Error retrieving metric %s", m->name);
         
      m = m->next;
   }
   return 0;
}

static int metrics_vms_get(vu_buffer *buf, int **ids)
{
   int num_vms;
   int i;

   *ids = NULL;
   
   num_vms = vu_num_vms();
   if (num_vms == -1)
      return -1;
   if (num_vms == 0)
      return 0;
   
   *ids = calloc(num_vms, sizeof(int));
   if (*ids == NULL) {
      vu_log (VHOSTMD_ERR, "calloc: %m");
      return -1;
   }

   num_vms = vu_get_vms(*ids, num_vms);
   for (i = 0; i < num_vms; i++) {
      vu_vm *vm;
      
      vm = vu_get_vm((*ids)[i]);
      if (vm == NULL)
         continue;
      
      metrics_vm_get(vm, buf);
      vu_vm_free(vm);
   }
   
   return num_vms;
}


/* Main run loop for vhostmd */
static int vhostmd_run(int diskfd)
{
   int *ids = NULL;
   int num_vms = 0;
   vu_buffer *buf = NULL;
   
   if (vu_buffer_create(&buf, MDISK_SIZE_MIN - MDISK_HEADER_SIZE)) {
      vu_log(VHOSTMD_ERR, "Unable to allocate memory");
      return -1;
   }
   
   while (!down) {
      vu_buffer_add(buf, "<metrics>\n", -1);
      if (metrics_host_get(buf))
         vu_log(VHOSTMD_ERR, "Failed to collect host metrics "
                     "during update");

      if ((num_vms = metrics_vms_get(buf, &ids)) == -1)
         vu_log(VHOSTMD_ERR, "Failed to collect vm metrics "
                     "during update");

      vu_buffer_add(buf, "</metrics>\n", -1);
      if (transports & VBD)
         metrics_disk_update(diskfd, buf);
#ifdef WITH_XENSTORE
      if (transports & XENSTORE)
         metrics_xenstore_update(buf->content, ids, num_vms);
#endif
      if (ids)
          free(ids);
      sleep(update_period);
      vu_buffer_erase(buf);
   }
   vu_buffer_delete(buf);
   return 0;
}

static void usage(const char *argv0)
{
   fprintf (stderr,
            "\n\
   Usage:\n\
   %s [options]\n\
   \n\
   Options:\n\
   -v | --verbose         Verbose messages.\n\
   -c | --connect <uri>   Set the libvirt URI.\n\
   -d | --no-daemonize    Process will not daemonize - useful for debugging.\n\
   -f | --config <file>   Configuration file.\n\
   -p | --pid-file <file> PID file.\n\
   -u | --user <user>     Drop root privs and run as <user>.\n\
   \n\
   Host metrics gathering daemon:\n\
   \n",
            argv0);
}

int main(int argc, char *argv[])
{
   struct sigaction sig_action;
   const char *pfile = NULL;
   const char *cfile = SYSCONF_DIR "/vhostmd/vhostmd.conf";
   int verbose = 0;
   int no_daemonize = 0;
   int ret = 1;
   int mdisk_fd = -1;
   const char *user = NULL;

   struct option opts[] = {
      { "verbose", no_argument, &verbose, 1},
      { "no-daemonize", no_argument, &no_daemonize, 1},
      { "config", required_argument, NULL, 'f'},
      { "pid-file", required_argument, NULL, 'p'},
      { "user", required_argument, NULL, 'u'},
      { "connect", required_argument, NULL, 'c'},
      { "help", no_argument, NULL, '?' },
      {0, 0, 0, 0}
   };

   while (1) {
      int optidx = 0;
      int c;

      c = getopt_long(argc, argv, "c:df:p:u:v", opts, &optidx);

      if (c == -1)
         break;

      switch (c) {
         case 0:
            /* Got one of the flags */
            break;
         case 'v':
            verbose = 1;
            break;
         case 'd':
            no_daemonize = 1;
            break;
         case 'f':
            cfile = optarg;
            break;
         case 'p':
            pfile = optarg;
            break;
         case 'u':
	    user = optarg;
	    break;
         case 'c':
	    libvirt_uri = optarg;
	    break;
         case '?':
            usage(argv[0]);
            return 2;
         default:
            fprintf(stderr, "hostmetricsd: unknown option: %c\n", c);
            exit(1);
      }
   }

   vu_log_init(no_daemonize, verbose);
   
   if (!no_daemonize) {
      if (daemonize() < 0) {
         vu_log(VHOSTMD_ERR, "Failed to fork as daemon: %s",
                     strerror(errno));
         goto out;
      }
   }

   /* If running as root and no PID file is set, use the default */
   if (pfile == NULL &&
       getuid() == 0 &&
       pid_file[0] != '\0')
      pfile = pid_file;

   /* If we have a pidfile set, claim it now, exiting if already taken */
   if (pfile != NULL &&
       write_pid_file(pfile) < 0)
      goto out;

   sig_action.sa_sigaction = sig_handler;
   sig_action.sa_flags = SA_SIGINFO;
   sigemptyset(&sig_action.sa_mask);

   sigaction(SIGHUP, &sig_action, NULL);
   sigaction(SIGINT, &sig_action, NULL);
   sigaction(SIGQUIT, &sig_action, NULL);
   sigaction(SIGTERM, &sig_action, NULL);

   xmlInitParser();

   if (validate_config_file(cfile)) {
      xmlCleanupParser();
      vu_log(VHOSTMD_ERR, "Config file: %s, fails DTD validation ", cfile);
      goto out;
   }

   if (parse_config_file(cfile)) {
      xmlCleanupParser();
      vu_log(VHOSTMD_ERR, "Please ensure configuration file "
                  "%s exists and is valid", cfile);
      goto out;
   }

   xmlCleanupParser();

   if (check_config()) {
      vu_log(VHOSTMD_ERR, "Configuration file %s contains invalid "
                  "setting(s)", cfile);
      goto out;
   }

   if ((mdisk_fd = metrics_disk_create()) < 0) {
      vu_log(VHOSTMD_ERR, "Failed to create metrics disk %s", mdisk_path);
      goto out;
   }

   /* Drop root privileges if requested.  Note: We do this after
    * opening the metrics disk, parsing the config file, etc.
    */
   if (user) {
       struct passwd *pw;

       errno = 0;
       pw = getpwnam (user);
       if (!pw) {
	   vu_log (VHOSTMD_ERR, "No entry in password file for user %s: %m",
		   user);
	   goto out;
       }

       if (pw->pw_uid == 0 || pw->pw_gid == 0) {
	   vu_log (VHOSTMD_ERR, "Cannot switch to root using the '-u' command line flag.");
	   goto out;
       }

       if (setgid (pw->pw_gid) == -1) {
	   vu_log (VHOSTMD_ERR, "setgid: %d: %m", pw->pw_gid);
	   goto out;
       }

       if (initgroups (user, pw->pw_gid) == -1) {
           vu_log (VHOSTMD_ERR, "initgroups: %m");
           goto out;
       }

       if (setuid (pw->pw_uid) == -1) {
	   vu_log (VHOSTMD_ERR, "setuid: %d: %m", pw->pw_uid);
	   goto out;
       }

       vu_log (VHOSTMD_INFO, "Switched to uid:gid %d:%d",
	       pw->pw_uid, pw->pw_gid);
   }

   ret = vhostmd_run(mdisk_fd);

 out:
   metrics_disk_close(mdisk_fd);
   if (pfile)
      unlink(pfile);

   vu_log_close();
   vu_vm_connect_close();

   return ret;
}

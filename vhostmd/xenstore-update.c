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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <xs.h>

#include "util.h"
#include "metric.h"

#define BUF_SIZE_MIN 1024

/*
 * Serialize xml node into mbuf as xml
 */
static void node_to_vu_buffer(xmlDocPtr doc, xmlNodePtr cur, vu_buffer *mbuf)
{
    char *str;
    asprintf(&str, "\t<%s", (const char *)cur->name);
    vu_buffer_add(mbuf, str, -1);
    free(str);
    if (cur->properties != NULL) {
        xmlAttrPtr attr = cur->properties;
        while (attr != NULL) {
            asprintf(&str, " %s=\"%s\"", attr->name, attr->children->content);
            vu_buffer_add(mbuf, str, -1);
            free(str);
            attr = attr->next;
        }
    }
    asprintf(&str, ">\n");
    vu_buffer_add(mbuf, str, -1);
    free(str);
    if (cur->children != NULL) {
        xmlNodePtr child = cur->children;
        while(child != NULL) {
            char *cp = (char *)xmlNodeListGetString(doc, child->xmlChildrenNode, 1);
            if (cp) {
                asprintf(&str, "\t\t<%s>%s</%s>\n", child->name,cp,child->name);
                vu_buffer_add(mbuf, str, -1);
                free(str);
                free(cp);
            }
            child = child->next;
        }
    }
    asprintf(&str, "\t</%s>\n", cur->name);
    vu_buffer_add(mbuf, str, -1);
    free(str);
}


/*
 * Gets metrics that are host specific AND metrics that are associated with
 *  the argumented uuid from buffer and places them into mbuf 
 */
static int get_assoicated_metrics(char *buffer, vu_buffer *mbuf, char *uuid)
{
   xmlParserCtxtPtr pctxt = NULL;
   xmlXPathContextPtr ctxt = NULL;
   xmlXPathObjectPtr obj = NULL;
   xmlDocPtr doc = NULL;
   xmlNodeSetPtr nodes;
   xmlNodePtr cur;
   int i;
   char *str;
   char *xpath;
   int ret = 0;
   int cnt;

   /* Set up a parser context */
   pctxt = xmlNewParserCtxt();
   if (!pctxt || !pctxt->sax) {
      goto out;
   }

   doc = xmlCtxtReadMemory(pctxt, buffer, 
                           strlen(buffer), "mdisk.xml", NULL, 
                           XML_PARSE_NOENT | XML_PARSE_NONET |
                           XML_PARSE_NOWARNING);
   if (!doc) {
      vu_log(VHOSTMD_ERR, "%s(): libxml failed to parse mdisk.xml buffer\n", __func__);
      goto out;
   }

   ctxt = xmlXPathNewContext(doc);
   if (!ctxt) {
      goto out;
   }

   /* Set the buffer prefix */
   asprintf(&str, "<metrics>\n");
   vu_buffer_add(mbuf, str, -1);
   free(str);

   /* Get the host metrics set */
   asprintf(&xpath, "//metrics/metric[@context='host']");
   obj = xmlXPathEval(BAD_CAST xpath, ctxt);
   free(xpath);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      vu_log(VHOSTMD_ERR, "%s(): No host metrics found \n", __func__);
      ret = -1;
      goto out;
   }
   if ((cnt =xmlXPathNodeSetGetLength(obj->nodesetval)) <= 0) {
      vu_log(VHOSTMD_ERR, "%s(): No host metrics set found \n", __func__);
      ret = -1;
      goto out;
   }
   nodes = obj->nodesetval;
   /* Iterate thru adding to mbuf */
   for(i = 0; i < cnt; ++i) {
       if(nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
           cur = nodes->nodeTab[i];        
           node_to_vu_buffer(doc, cur, mbuf);
       }
   }
   xmlXPathFreeObject(obj);
   obj = NULL;

   /* Get the associated vm metrics set */
   asprintf(&xpath, "//metrics/metric[@context='%s'][@uuid='%s']", "vm", uuid);
   obj = xmlXPathEval( BAD_CAST xpath, ctxt);  // worked but no nodes 
   free(xpath);
   if ((obj == NULL) || (obj->type != XPATH_NODESET)) {
      vu_log(VHOSTMD_ERR, "%s(): No VM metrics found!\n", __func__);
      ret = -1;
      goto out;
   }
   if ((cnt = xmlXPathNodeSetGetLength(obj->nodesetval)) <= 0) {
      vu_log(VHOSTMD_ERR, "%s(): No VM metrics set found \n", __func__);
      ret = -1;
      goto out;
   }
   nodes = obj->nodesetval;
   for(i = 0; i < cnt; ++i) {
       if(nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
           cur = nodes->nodeTab[i];        
           node_to_vu_buffer(doc, cur, mbuf);
       }
   }
   xmlXPathFreeObject(obj);
   obj = NULL;

   /* Terminate the buffer */
   asprintf(&str, "</metrics>");
   vu_buffer_add(mbuf, str, -1);
   free(str);

out:
   if (obj)
      xmlXPathFreeObject(obj);
   if (ctxt)
      xmlXPathFreeContext(ctxt);
   if (doc)
      xmlFreeDoc(doc);
   if (pctxt)
      xmlFreeParserCtxt(pctxt);
   return ret;
}

int metrics_xenstore_update(char *mbuffer, int *ids, int num_vms)
{
    char *buf = NULL, *path, *uuid = NULL;
    unsigned int i, len;
    char *cp;
    struct xs_handle *xsh = NULL;
    vu_buffer *mbuf = NULL;

    xsh = xs_daemon_open();
    if (xsh == NULL)
        return 0;

    if (vu_buffer_create(&mbuf, BUF_SIZE_MIN)) {
        vu_log(VHOSTMD_ERR, "Unable to allocate memory");
        goto out;
    }
    for (i = 0; i < num_vms; i++) {
        path = xs_get_domain_path(xsh, ids[i]);
        if (path == NULL) {
            vu_log(VHOSTMD_ERR, "xs_get_domain_path() error. domid %d.\n", ids[i]);
            continue;
        }
        asprintf(&buf, "%s/vm", path);
        uuid = xs_read(xsh, XBT_NULL, buf, &len);
        free(buf);
        if (uuid == NULL) {
            vu_log(VHOSTMD_ERR, "xs_read(): uuid get error. %s.\n", buf);
            free(path);
            continue;
        }
        cp = strrchr(uuid, '/');
        memmove(uuid, cp+1, strlen(cp));
        get_assoicated_metrics(mbuffer, mbuf, uuid);
        free(uuid);

        asprintf(&buf, "%s/metrics", path);
        free(path);
        xs_write(xsh, XBT_NULL, buf, mbuf->content, mbuf->use);
        free(buf);
        vu_buffer_erase(mbuf);
    }
    vu_buffer_delete(mbuf);
out:
    xs_daemon_close(xsh);
    return 0;
}

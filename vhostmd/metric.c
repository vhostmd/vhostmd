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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

#include "util.h"
#include "metric.h"

static int metric_action_subst(metric *m, char **action)
{
   char *str;
   char *temp;
   char *domid;

   if ((str = vu_str_replace(m->action, "NAME", m->vm->name)) == NULL)
	   return -1;

   temp = str;
   if (asprintf(&domid, "%d", m->vm->id) == -1)
	   return -1;
   if ((str = vu_str_replace(temp, "VMID", domid)) == NULL)
	   return -1;
   free(domid);
   free(temp);

   temp = str;
   if ((str = vu_str_replace(temp, "UUID", m->vm->uuid)) == NULL)
	   return -1;
   free(temp);

   *action = str;

   return 0;
}

/*
 * Verify XML contained in parameter xml is valid.
 * Returns 1 is XML is valid, 0 otherwise.
 */
static int is_valid_metric_xml(char *xml)
{
   int ret = 0;
   xmlDoc *xmldoc;
   xmlDtdPtr dtd = NULL;
   xmlValidCtxtPtr vcp = NULL;
   int len = strlen(xml) + 1;
   
   if ((xmldoc = xmlParseMemory(xml, len)) == NULL) {
      vu_log(VHOSTMD_WARN, "%s(): Failed to parse metric XML", __FUNCTION__);
      goto out;
   }

   if ((dtd = xmlParseDTD(NULL, BAD_CAST "/etc/vhostmd/metric.dtd" )) == NULL) {
      vu_log(VHOSTMD_ERR, "%s(): Failed to parse metric DTD", __FUNCTION__);
      goto out;
   }

   if ((vcp = xmlNewValidCtxt()) == NULL) {
      vu_log(VHOSTMD_ERR, "%s(): Failed to allocate new validation context",
             __FUNCTION__);
      goto out;
   }
      
   if (!xmlValidateDtd(vcp, xmldoc, dtd)) {
      vu_log(VHOSTMD_WARN, "%s(): Metric XML failed validation", __FUNCTION__);
      goto out;
   }

   ret = 1;
   
out:
   if (vcp)
      xmlFreeValidCtxt(vcp);
   xmlFreeDoc(xmldoc);
   return ret;
}

/*
 * Metric type 'xml' can contain 1 or more <metric> nodes.
 * Parse the value into discrete <metric> nodes, passing each
 * node to is_valid_metric_xml() for validation.
 * Returns 1 if all <metric> nodes are valid, 0 otherwise.
 */
static int validate_metric_xml_value(char *xml)
{
   char *start_element;
   char *end_element;
   char *temp;

   start_element = strstr(xml, "<metric");
   if (start_element == NULL) {
      vu_log(VHOSTMD_WARN, "%s(): Unable to find any \"<metric>\" "
             "elements in metric XML", __FUNCTION__);
      return 0;
   }
   
   while (start_element) {
      end_element = strstr(start_element, "</metric>");
      if (end_element == NULL) {
         vu_log(VHOSTMD_WARN, "%s(): Unable to find any \"</metric>\" "
                "elements in metric XML", __FUNCTION__);
         return 0;
      }
      end_element = end_element + 10;
      
      temp = strndup(start_element, end_element - start_element);
      if (temp == NULL) {
         vu_log(VHOSTMD_WARN, "%s(): Failed to allocate memory",
                __FUNCTION__);
         
         return 0;
      }
      
      if (!is_valid_metric_xml(temp)) {
         free(temp);
         return 0;
      }
      
      free(temp);
      start_element = strstr(end_element, "<metric");
   }

   return 1;
}

char *metric_type_to_str(metric_type t)
{
   switch (t) {
      case M_INT32:
         return "int32";
      case M_UINT32:
         return "uint32";
      case M_INT64:
         return "int64";
      case M_UINT64:
         return "uint64";
      case M_REAL32:
         return "real32";
      case M_REAL64:
         return "real64";
      case M_STRING:
         return "string";
      case M_GROUP:
         return "group";
      case M_XML:
         return "XML";

      default:
         return "unknown";
   }
}

int metric_type_from_str(const xmlChar *t, metric_type *typ)
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
   else if (strcasecmp(cp, "group") == 0)
      *typ = M_GROUP;
   else if (strcasecmp(cp, "xml") == 0)
      *typ = M_XML;
   else
      ret = -1;
   
   return ret;
}

int metric_value_get(metric *m)
{
   FILE *fp = NULL;
   int ret = -1;
   size_t len;
   
   if (m->pf) { 
	   ret = m->pf(m);
	   return ret;
   }

   if (m->ctx == METRIC_CONTEXT_VM) {
      char *cmd = NULL;

      if (metric_action_subst(m, &cmd)) {
         vu_log(VHOSTMD_ERR, "Failed action 'KEYWORD' substitution");
         return ret;
      }
      fp = popen(cmd, "r");
      free(cmd);
   }
   else {
      fp = popen(m->action, "r");
   }
   
   if (fp == NULL)
      return ret;
      
   if (m->type == M_XML)
      len = 2048;
   else
      len = 256;
   
   if (m->value)
      memset(m->value, 0, len);
   else
      m->value = calloc(1, len);

   if (m->value == NULL)
      goto out;

   fread(m->value, 1, len-1, fp);
   
 out:
   ret = pclose(fp);
   
   return ret;
}

int metric_xml(metric *m, vu_buffer *buf)
{
   char *n;
   char *v;
   char *t;
   int i;

   if (metric_value_get(m))
      return -1;
   
   if (m->type == M_XML) {
      if (validate_metric_xml_value(m->value)) {
         vu_buffer_add(buf, m->value, strlen(m->value));
         return 0;
      } else  {
         vu_log(VHOSTMD_WARN, "Validation of XML returned by metric %s failed",
                m->name);
         return -1;
      }
   }
   
   for (i=0; i < m->cnt; i++) {
       n = vu_get_nth_token(m->name, ",", i, m->cnt); 
       t = vu_get_nth_token(m->type_str, ",", i, m->cnt);
       v = vu_get_nth_token(m->value, ",", i, m->cnt);
       
	   if (m->ctx == METRIC_CONTEXT_HOST) {
		   vu_buffer_vsprintf(buf,
				   "  <metric type='%s' context='host'>\n"
				   "    <name>%s</name>\n"
				   "    <value>%s</value>\n"
				   "  </metric>\n",
				   t,
				   n,
				   v);
	   }
	   else {
		   vu_buffer_vsprintf(buf,
				   "  <metric type='%s' context='vm' id='%d' uuid='%s'>\n"
				   "    <name>%s</name>\n"
				   "    <value>%s</value>\n"
				   "  </metric>\n",
				   t,
				   m->vm->id,
				   m->vm->uuid,
				   n,
				   v);
	   }
	   if (n) free(n);
	   if (t) free(t);
	   if (v) free(v);
   }

   return 0;
}

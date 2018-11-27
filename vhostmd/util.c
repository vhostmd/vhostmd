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
#include <syslog.h>
#include <limits.h>
#include <math.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "util.h"


static int verbose = 0;
static int no_daemonize = 0;

/*
 * Grow the buffer to at least len bytes.
 * Return zero on success, -1 on error.
 */
static int buffer_grow(vu_buffer *buf, int len)
{
    int size;

    if ((len + buf->use) < buf->size)
        return 0;

    size = buf->use + len;

    if ((buf->content = realloc(buf->content, size)) == NULL)
        return -1;

    buf->size = size;
    memset(&buf->content[buf->use], 0, len);
    return 0;
}

/*
 * Init logging interface.  If running as daemon messages
 * are sent to syslog, otherwise to stderr.
 */
void vu_log_init(int no_daemon, int noisy)
{
   no_daemonize = no_daemon;
   verbose = noisy;
   openlog("vhostmd", 0, 0);

}

/*
 * Close logging interface.
 */
void vu_log_close(void)
{
   if (!no_daemonize)
      closelog();
}

/* 
 * Logging function.
 */
void vu_log(int priority, const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);

   if (!no_daemonize) {
      int sysprio = -1;

      switch(priority) {
         case VHOSTMD_ERR:
            sysprio = LOG_ERR;
            break;
         case VHOSTMD_WARN:
            sysprio = LOG_WARNING;
            break;
         case VHOSTMD_INFO:
            if (verbose)
               sysprio = LOG_INFO;
            break;
            #ifdef ENABLE_DEBUG
         case VHOSTMD_DEBUG:
            if (verbose)
               sysprio = LOG_DEBUG;
            break;
            #endif
         default:
            break;
      }

      if (sysprio != -1)
         vsyslog(sysprio, fmt, args);
   } else {
      switch(priority) {
         case VHOSTMD_ERR:
         case VHOSTMD_WARN:
            vfprintf(stderr, fmt, args);
            fputc('\n', stderr);
            break;

         case VHOSTMD_INFO:
            if (verbose) {
               vprintf(fmt, args);
               fputc('\n', stdout);
            }
            break;

            #ifdef ENABLE_DEBUG
         case VHOSTMD_DEBUG:
            if (verbose) {
               vprintf(fmt, args);
               fputc('\n', stdout);
            }
            break;
            #endif
         default:
            break;
      }
   }

   va_end(args);
}

/*
 * Create buffer capable of holding len content.
 */
int vu_buffer_create(vu_buffer **buf, int len)
{
   *buf = calloc(1, sizeof(vu_buffer));
   if (*buf == NULL)
      return -1;
   
   return buffer_grow(*buf, len);
}

/*
 * Delete buffer
 */
int vu_buffer_delete(vu_buffer *buf)
{
   if (buf->content)
      free(buf->content);
   free(buf);
   return 0;
}

/*
 * Add str to buffer. 
 */
void vu_buffer_add(vu_buffer *buf, const char *str, int len)
{
    unsigned int needSize;

    if ((str == NULL) || (buf == NULL) || (len == 0))
        return;

    if (len < 0)
        len = strlen(str);

    needSize = buf->use + len + 2;
    if (needSize > buf->size &&
        buffer_grow(buf, needSize - buf->use) < 0)
        return;

    memcpy (&buf->content[buf->use], str, len);
    buf->use += len;
    buf->content[buf->use] = '\0';
}

/*
 * Do a formatted print to buffer.
 */
void vu_buffer_vsprintf(vu_buffer *buf, const char *format, ...)
{
    int size, count, grow_size;
    va_list locarg, argptr;

    if ((format == NULL) || (buf == NULL))
        return;

    if (buf->size == 0 &&
        buffer_grow(buf, 100) < 0)
        return;

    size = buf->size - buf->use - 1;
    va_start(argptr, format);
    va_copy(locarg, argptr);
    while (((count = vsnprintf(&buf->content[buf->use], size, format,
                               locarg)) < 0) || (count >= size - 1)) {
        buf->content[buf->use] = 0;
        va_end(locarg);

        grow_size = (count > 1000) ? count : 1000;
        if (buffer_grow(buf, grow_size) < 0)
            return;

        size = buf->size - buf->use - 1;
        va_copy(locarg, argptr);
    }
    va_end(locarg);
    buf->use += count;
    buf->content[buf->use] = '\0';
}

/*
 * Erase buffer, setting use to 0 and clearing content.
 */
void vu_buffer_erase(vu_buffer *buf)
{
   if (buf) {
      memset(buf->content, '\0', buf->size);
      buf->use = 0;
   }
}

/*
 * Calculate simple buffer checksum
 */
unsigned int vu_buffer_checksum(vu_buffer *buf)
{
   if (buf) {
      unsigned int i;
      unsigned int chksum = 0;

      for(i = 0; i < buf->size; i++) 
         chksum += buf->content[i];
      return chksum;
   }
   return 0;
}

/*
 * Empty buffer, freeing its contents.
 */
void vu_buffer_empty(vu_buffer *buf)
{
   if (buf) {
      free(buf->content);
      buf->size = 0;
      buf->use = 0;
   }
}

/*
 * Return val * unit after converting unit from string to number
 * representation.  E.g. unit=k, val=1 -> return 1024 * 1
 * A return vaule of -1 indicates failure.
 */
int vu_val_by_unit(const char *unit, int val)
{
   int mult;
   
   if (!unit) {
      mult = 1;
   } else {
      switch (unit[0]) {
         case 'k':
         case 'K':
            mult = 1024;
            break;
            
         case 'm':
         case 'M':
            mult = 1024 * 1024;
            break;
            
         default:
            return -1;
      }
   }

   if (val > (INT_MAX / mult)) {
      return -1;
   }
   
   return (val * mult);
}

/* Get string value of element specified in xpath.
 * Returns string value on sucess, NULL on failure.
 */
char *vu_xpath_string(const char *xpath, xmlXPathContextPtr ctxt)
{
   xmlXPathObjectPtr obj;
   xmlNodePtr relnode;
   char *ret;

   if ((ctxt == NULL) || (xpath == NULL))
      return NULL;

   relnode = ctxt->node;
   obj = xmlXPathEval(BAD_CAST xpath, ctxt);
   if ((obj == NULL) || (obj->type != XPATH_STRING) ||
       (obj->stringval == NULL) || (obj->stringval[0] == 0)) {
      xmlXPathFreeObject(obj);
      return NULL;
   }
   ret = strdup((char *) obj->stringval);
   xmlXPathFreeObject(obj);
   ctxt->node = relnode;
   return ret;
}

/* Get long value of element specified in xpath.
 * Places long value in param value and returns 0 on success.
 * Returns -1 on failure.
 */
int vu_xpath_long(const char *xpath, xmlXPathContextPtr ctxt, long *value)
{
   xmlXPathObjectPtr obj;
   xmlNodePtr relnode;
   int ret = 0;

   if ((ctxt == NULL) || (xpath == NULL) || (value == NULL))
      return -1;

   relnode = ctxt->node;
   obj = xmlXPathEval(BAD_CAST xpath, ctxt);
   if ((obj != NULL) && (obj->type == XPATH_STRING) &&
       (obj->stringval != NULL) && (obj->stringval[0] != 0)) {
      char *conv = NULL;
      long val;

      val = strtol((const char *) obj->stringval, &conv, 10);
      if (conv == (const char *) obj->stringval) {
         ret = -1;
      } else {
         *value = val;
      }
   } else if ((obj != NULL) && (obj->type == XPATH_NUMBER) &&
              (!(isnan(obj->floatval)))) {
      *value = (long) obj->floatval;
      if (*value != obj->floatval) {
         ret = -1;
      }
   } else {
      ret = -1;
   }

   xmlXPathFreeObject(obj);
   ctxt->node = relnode;
   return ret;
}

/*
 * Calculate simple checksum of char buffer
 */
unsigned int vu_str_checksum(char *buf)
{
   if (buf) {
      size_t len = strlen(buf);
      unsigned int i;
      unsigned int chksum = 0;

      for(i = 0; i < len; i++) 
         chksum += buf[i];
      return chksum;
   }
   return 0;
}

/*
 * Replace all occurance of string origstr with newstr in haystack,
 * returns new string
 */
char *vu_str_replace(const char *haystack, const char *origstr, const char *newstr)
{
   char *p;
   char *cp;
   char *tempstr;
   char *dest;
   int newlen = strlen(newstr);
   int origlen = strlen(origstr);
   int cnt;
   int i;

   tempstr = strdup(haystack);
   if (tempstr == NULL) {
      return(NULL);
   }

   cp = tempstr;
   cnt = 0;
   while((cp = strstr(cp, origstr))) {
      cnt++;
      cp = cp + origlen;
   }

   dest = malloc(strlen(haystack) - (origlen * cnt) + (newlen * cnt) + 1);
   if (dest == NULL) {
      return(NULL);
   }
   *dest = '\0';

   cp = tempstr;
   for (i=0; i <cnt; i++) {
      p = strstr(cp, origstr);
      *p = '\0';
      strcat(dest, cp);
      strcat(dest, newstr);
      cp = p + origlen;
   }
   strcat(dest, cp);
   free(tempstr);

   return dest;
}

/*
 * Get nth token from str.
 * Returns str if nth token not found
 */
char *vu_get_nth_token(char *str, char *delim, int nth, int cnt)
{
   int i;
   char *sp;
   char *token;

   if (str == NULL || str[0] == '\0')
       return (strdup(""));

   if (cnt == 1)
       return (strdup(str));

   /*
    * Verify we have that many delimiters, if not just return copy of original
    */
   sp = str;
   i = 0;
   while((sp = strstr(sp, delim)) != NULL) {
      i++;
      sp += 1;
   }
   if ( i < nth)
      return(strdup(str));

   /*
    * Get the token
    */
   sp = strdup(str);
   token = strtok(sp, delim);
   for (i = 0; i < nth; i++) {
      token = strtok(NULL, delim);
   }
   token = strdup(token);
   free(sp);

   return token;
}

/*
 * Append src string onto dest, comma separated.  Allocate
 * if necessary
 */
int vu_append_string(char **dest, xmlChar * str)
{
   char *cp;

   if (*dest) {
      asprintf(&cp, "%s,%s", *dest, str);
      free(*dest);
      *dest = cp;
   }
   else {
      *dest = strdup((char *)str);
   }
   return 0;
}

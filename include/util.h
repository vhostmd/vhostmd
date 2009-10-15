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

#ifndef __UTIL_H__
#define __UTIL_H__

#include <libxml/parser.h>
#include <libxml/xpath.h>


typedef enum {
   VHOSTMD_ERR,
   VHOSTMD_WARN,
   VHOSTMD_INFO,
   #ifdef ENABLE_DEBUG
   VHOSTMD_DEBUG
   #endif
} vu_log_priority;

typedef struct _vu_buffer {
    unsigned int size;
    unsigned int use;
    char *content;
}vu_buffer;

typedef struct _vu_vm 
{
   int id;
   char *name;
   char *uuid;
} vu_vm;

/* The libvirt URI to connect to (-c argument on the command line).  If
 * not set, this will be NULL.
 */
extern const char *libvirt_uri;

/*
 * Init logging interface.  If running as daemon messages
 * are sent to syslog, otherwise to stderr.
 */
void vu_log_init(int no_daemon, int noisy);

/*
 * Close logging interface.
 */
void vu_log_close(void);

/* 
 * Logging function.
 */
void vu_log(int priority, const char *fmt, ...)
  __attribute__((format (printf, 2, 3)));

/*
 * Create buffer capable of holding len content.
 */
int vu_buffer_create(vu_buffer **buf, int len);

/*
 * Delete buffer 
 */
int vu_buffer_delete(vu_buffer *buf);

/*
 * Add str to buffer. 
 */
void vu_buffer_add(vu_buffer *buf, const char *str, int len);

/*
 * Do a formatted print to buffer.
 */
void vu_buffer_vsprintf(vu_buffer *buf, const char *format, ...)
  __attribute__((format (printf, 2, 3)));

/*
 * Erase buffer, setting use to 0 and clearing content.
 */
void vu_buffer_erase(vu_buffer *buf);

/*
 * Calculate simple buffer checksum
 */
unsigned int vu_buffer_checksum(vu_buffer *buf);

/*
 * Empty buffer, freeing its contents.
 */
void vu_buffer_empty(vu_buffer *buf);

/*
 * Return val * unit after converting unit from string to number
 * representation.  E.g. unit=k, val=1 -> return 1024 * 1
 * A return vaule of -1 indicates failure.
 */
int vu_val_by_unit(const char *unit, int val);

/*
 * Get string value of element specified in xpath.
 * Returns string value on sucess, NULL on failure.
 */
char *vu_xpath_string(const char *xpath, xmlXPathContextPtr ctxt);

/*
 * Get long value of element specified in xpath.
 * Places long value in param value and returns 0 on success.
 * Returns -1 on failure.
 */
int vu_xpath_long(const char *xpath, xmlXPathContextPtr ctxt, long *value);

int vu_num_vms(void);

int vu_get_vms(int *ids, int max_ids);

vu_vm *vu_get_vm(int id);

void vu_vm_free(vu_vm *vm);

void vu_vm_connect_close(void);

/*
 * Calculate simple checksum of char buffer
 */
unsigned int vu_str_checksum(char *buf);

/*
 * Replace all occurance of string origstr with newstr in haystack,
 * returns new string
 */
char *vu_str_replace(const char *haystack, const char *origstr, const char *newstr);

/*
 * Get nth token from str
 * Returns str if nth token not found
 */
char *vu_get_nth_token(char *str, char *delim, int nth, int cnt);

/*
 * Append src string onto dest, comma separated
 */
int vu_append_string(char **dest, xmlChar *str);

#endif /* __UTIL_H__ */

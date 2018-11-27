/*
 * Copyright (C) 2018 SAP SE
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
 * Author: Michael Trapp <michael.trapp@sap.com>
 */

#ifndef __LIBSERIALCLIENT_H__
#define __LIBSERIALCLIENT_H__

/* dump metrics from virtio serial port to xml formatted file */
int dump_virtio_metrics(const char *dest_file);

/* dump metrics from virtio serial port to buffer */
char *get_virtio_metrics(const char *dev_name);

#endif

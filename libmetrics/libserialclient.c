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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "libserialclient.h"


/*
 * dump metrics from virtio serial port to xml formatted file
 */
int dump_virtio_metrics(const char *dest_file)
{
    FILE *fp = stdout;
    char *response;
    size_t len;

    response = get_virtio_metrics(NULL);
    if (response == NULL)
        goto error;

    len = strlen(response);

    if (dest_file) {
        fp = fopen(dest_file, "w");
        if (fp == NULL) {
            fprintf(stderr,
                    "LIB_SERIALCLIENT: Error, unable to dump metrics: fopen(%s) %s\n",
                    dest_file, strerror(errno));
            goto error;
        }
    }

    if (fwrite(response, 1UL, len, fp) != len) {
        fprintf(stderr,
                "LIB_SERIALCLIENT: Error, unable to export metrics to file:%s - error:%s\n",
                dest_file ? dest_file : "STDOUT", strerror(errno));
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

/*
 * dump metrics from virtio serial port to buffer
 */
char *get_virtio_metrics(const char *dev_name)
{
    const char request[] = "GET /metrics/XML\n\n", end_token[] = "\n\n";
    const char *dev;
    char *response = NULL;
    int fd = -1;
    size_t pos;
    size_t buf_size = (1 << 16);
    const size_t req_len = (size_t) strlen(request);
    const time_t start_time = time(NULL);

    if (dev_name)
        dev = dev_name;
    else
        dev = "/dev/virtio-ports/org.github.vhostmd.1";

    response = calloc(1UL, buf_size);
    if (response == NULL)
        goto error;

    fd = open(dev, O_RDWR | O_NONBLOCK);

    if (fd < 0) {
        fprintf(stderr,
                "LIB_SERIALCLIENT: Error, unable to dump metrics: open(%s) %s\n",
                dev, strerror(errno));
        goto error;
    }

    pos = 0;
    while (pos < req_len) {
        ssize_t len = write(fd, &request[pos], req_len - pos);
        if (len > 0)
            pos += (size_t) len;
        else {
            if (errno == EAGAIN)
                usleep(10000);
            else
                goto error;
        }
    }

    pos = 0;
    do {
        ssize_t len = read(fd, &response[pos], buf_size - pos - 1);
        if (len > 0) {
            pos += (size_t) len;
            response[pos] = 0;

            if ((pos + 1) >= buf_size) {
                buf_size = buf_size << 1;       /* increase response buffer */
                if (buf_size > (1 << 24))       /* max 16MB */
                    goto error;

                response = realloc(response, buf_size);
                if (response == NULL)
                    goto error;

                memset(&response[pos], 0, buf_size - pos);
            }
        } else {
            if (errno == EAGAIN) {
                usleep(10000);
                if (time(NULL) > (start_time + 30)) {
                    fprintf(stderr,
                            "LIB_SERIALCLIENT: Error, unable to read metrics"
                            " - timeout after 30s\n");
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

    return NULL;
}

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

#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <search.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>

#include "util.h"
#include "virtio.h"


#define DEFAULT_VU_BUFFER_SIZE 512
#define SUN_PATH_LEN (sizeof(((struct sockaddr_un *) 0)->sun_path))
#define FREE -1


typedef struct {
    int fd;              /* UDS filehandle */
    int id;              /* domain id */
    time_t update_ts;    /* timestamp of last metrics update */
    char *name;          /* domain name */
    char *uds_name;      /* full UDS name */
    vu_buffer *metrics;  /* metrics buffer */
    vu_buffer *request;  /* virtio request buffer */
    vu_buffer *response; /* virtio response buffer */
} channel_t;

typedef struct {
    int id;              /* domain id */
    int index;           /* index of channel */
} id_map_t;

typedef enum {
    REQ_INCOMPLETE,
    REQ_INVALID,
    REQ_GET_XML
} REQUEST_T;

static channel_t *channel = NULL;
static id_map_t *id_map = NULL;
static time_t exp_period = 0;

static const char *channel_path = "/var/lib/libvirt/qemu/channel/target";
static const char *channel_name = "org.github.vhostmd.1";
static int channel_max = 0;
static volatile int channel_count = 0;
static volatile int connection_count = 0;

static int epoll_fd = -1;
static struct epoll_event *epoll_events = NULL;
static pthread_mutex_t channel_mtx;

static enum {
    VIRTIO_INIT,
    VIRTIO_ACTIVE,
    VIRTIO_STOP,
    VIRTIO_ERROR
} virtio_status = VIRTIO_INIT;


/*
 * Static functions
 */
static int vio_id_compare(const void * a, const void * b);
static channel_t *vio_channel_find(int id, const char * name, int insert);
static void vio_channel_free(channel_t * c);
static int vio_channel_open(channel_t * c);
static void vio_channel_close(channel_t * c);
static int vio_channel_update(channel_t * c);
static int vio_readdir(const char * path);
static void vio_recv(channel_t * c);
static void vio_send(channel_t * c, uint32_t ep_event);
static void vio_expire(void);
static REQUEST_T vio_check_request(channel_t * c);
static void vio_handle_io(unsigned epoll_wait_ms);

/*
 * Update response buffer of a channel.
 * Concat host and VM buffer into the response buffer.
 */
static int vio_channel_update(channel_t * c)
{
    static const char *metrics_start_str = "<metrics>\n";
    static const char *metrics_end_str = "</metrics>\n\n";

    int rc = 0;

    vu_buffer_erase(c->response);
    vu_buffer_add(c->response, metrics_start_str, -1);

    pthread_mutex_lock(&channel_mtx);

    /* Dom0/host */
    if (channel[0].metrics->content && channel[0].metrics->use)
        vu_buffer_add(c->response, channel[0].metrics->content, -1);
    else
        vu_buffer_add(c->response,
                      "<!-- host metrics not available -->", -1);

    /* VM */
    if (c->metrics->use)
        vu_buffer_add(c->response, c->metrics->content, -1);
    else {
        vu_buffer_add(c->response,
                      "<!-- VM metrics not available -->", -1);
        rc = -1;
    }

    pthread_mutex_unlock(&channel_mtx);

    vu_buffer_add(c->response, metrics_end_str, -1);

#ifdef ENABLE_DEBUG
    vu_log(VHOSTMD_DEBUG, "New response for '%d %s' (%u)\n>>>%s<<<\n",
           c->id, c->name, c->response->use,
           c->response->content);
#endif
    return rc;
}

/*
 * Free allocated buffers and init values.
 */
static void vio_channel_free(channel_t * c)
{
    if (c->fd != FREE) {
        struct epoll_event evt;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c->fd, &evt);
        close(c->fd);
        c->fd = FREE;
    }
    if (c->name) {
        free(c->name);
        c->name = NULL;
    }
    if (c->uds_name) {
        free(c->uds_name);
        c->uds_name = NULL;
    }
    if (c->metrics) {
        vu_buffer_delete(c->metrics);
        c->metrics = NULL;
    }
    if (c->request) {
        vu_buffer_delete(c->request);
        c->request = NULL;
    }
    if (c->response) {
        vu_buffer_delete(c->response);
        c->response = NULL;
    }
    c->id = FREE;
}

/*
 * Connect channel and add the socket to the epoll desriptor.
 */
static int vio_channel_open(channel_t * c)
{
    struct sockaddr_un address;
    struct epoll_event evt;
    int flags;

    bzero(&address, sizeof(address));
    address.sun_family = AF_LOCAL;

    strncpy(address.sun_path, c->uds_name, SUN_PATH_LEN - 1);

    if ((c->fd = socket(AF_LOCAL, SOCK_STREAM, 0)) == -1)
        goto error;

    flags = fcntl(c->fd, F_GETFL, 0);
    if (flags < 0)
        goto error;

    flags |= flags | O_NONBLOCK;
    if (fcntl(c->fd, F_SETFL, flags) == -1)
        goto error;

    if (connect(c->fd, (struct sockaddr *) &address,
                (socklen_t) sizeof(address)) < 0)
        goto error;

    evt.data.ptr = (void*) c;
    evt.events = EPOLLIN;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, c->fd, &evt) == -1)
        goto error;

    connection_count++;

    vu_log(VHOSTMD_INFO, "Opened channel '%d %s' (%d/%d/%d)",
           c->id, c->name, connection_count, channel_count, channel_max);

    return 0;

error:
    vu_log(VHOSTMD_ERR, "Could not add channel '%d %s' (%s)",
           c->id, c->name, strerror(errno));
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = FREE;
    }

    return -1;
}

/*
 * Close channel.
 */
static void vio_channel_close(channel_t * c)
{
    int id = c->id;

    channel_count--;
    if (c->fd != FREE)
        connection_count--;

    vu_log(VHOSTMD_INFO, "Closed channel '%d %s' (%d/%d/%d)",
           c->id, c->name, connection_count, channel_count,  channel_max);
    vio_channel_free(c);

    if (id > 0) {
        id_map_t key, *result;

        key.id = id;
        result = (id_map_t *) bsearch(&key, id_map, channel_max, sizeof(id_map_t),
                                      vio_id_compare);

        if (result) {
            result->id = FREE;
            qsort (id_map, channel_max, sizeof(id_map_t), vio_id_compare);
        }
    }
}

/*
 * Lookup UDS sockets in the directory.
 * For valid type/name/channel connect to the socket.
 */
static int vio_readdir(const char * path)
{
    struct dirent *ent;
    DIR *dir = NULL;

    if ((dir = opendir(path)) == NULL) {
        vu_log(VHOSTMD_ERR, "opendir(%s) failed (%s)", path, strerror(errno));
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        int rc, id;

        if (sscanf(ent->d_name, "domain-%d-", &id) == 1) {

            char tmp[SUN_PATH_LEN + 8];
            struct stat st;

            rc = snprintf(tmp, sizeof(tmp), "%s/%s/%s", path, ent->d_name, channel_name);

            if (rc > 0 && rc < (int) sizeof(tmp) &&
                strlen(tmp) < SUN_PATH_LEN &&
                stat(tmp, &st) == 0 &&
                S_ISSOCK(st.st_mode)) {

                channel_t *c = NULL;
                const char *name = strchr(&(ent->d_name[strlen("domain-")]), '-');

                pthread_mutex_lock(&channel_mtx);
                c = vio_channel_find(id, name, 0);
                pthread_mutex_unlock(&channel_mtx);

                if (c && c->fd == FREE) {
                    c->uds_name = strdup(tmp);
                    if (c->uds_name == NULL)
                        goto error;
                    if (vio_channel_open(c))
                        goto error;
                }
            }
        }
    }
    closedir(dir);

    return 0;

error:
    closedir(dir);
    return -1;
}

/*
 * Check 'last_uppdate' value of available channels
 * and close expired channels.
 */
static void vio_expire(void)
{
    int i;
    time_t ts = time(NULL) - exp_period;

    for (i = 1; i <= channel_max; i++) {
        channel_t *c = &channel[i];

        /* a channel expires when update_ts is older than exp_period */
        if (c->metrics &&
            c->update_ts < ts) {

#ifdef ENABLE_DEBUG
            vu_log(VHOSTMD_DEBUG, "Expire channel '%s' (%d/%d/%d)",
                   c->name, connection_count, channel_count, channel_max);
#endif
            vio_channel_close(c);
        }
    }
}

/*
 * Lookup/add channel and allocate buffers.
 */
static channel_t *vio_channel_find(int id, const char * name, int insert)
{
    channel_t *c = NULL;
    id_map_t key, *result;

    key.id = id;
    result = (id_map_t *) bsearch(&key, id_map, channel_max, sizeof(id_map_t),
                                  vio_id_compare);

    if (result != NULL)
        return &channel[result->index];

    if (insert == 0)
        return NULL;

    if (channel_count >= channel_max) {
#ifdef ENABLE_DEBUG
        int i;
#endif
        vu_log(VHOSTMD_ERR,
               "Could not add channel '%d %s' - too many VMs (%d/%d/%d)",
               id, name, connection_count, channel_count, channel_max);

#ifdef ENABLE_DEBUG
        vu_log(VHOSTMD_DEBUG, "available channels:\n");
        for (i = 1; i <= channel_max; i++) {
            if (channel[i].id > 0)
                vu_log(VHOSTMD_DEBUG, "\t%d %d %lu\n", i,
                       channel[i].id, channel[i].update_ts);
        }
#endif
        return NULL;
    }

    /* new channel, add id and allocate buffer */
    key.id = FREE;
    result = (id_map_t *) bsearch(&key, id_map, channel_max,
                                  sizeof(id_map_t), vio_id_compare);
    if (result == NULL)
        return NULL;

    channel_count++;
    result->id = id;
    c = &channel[result->index];
    c->id = id;
    qsort (id_map, channel_max, sizeof(id_map_t), vio_id_compare);

    c->name = strdup(name);

    if (c->name == NULL ||
        vu_buffer_create(&c->metrics, DEFAULT_VU_BUFFER_SIZE) ||
        vu_buffer_create(&c->request, DEFAULT_VU_BUFFER_SIZE) ||
        vu_buffer_create(&c->response, DEFAULT_VU_BUFFER_SIZE))
        goto error;

    vu_log(VHOSTMD_INFO, "Added channel '%d %s' (%d/%d/%d)",
           c->id, c->name, connection_count, channel_count, channel_max);

    return c;

error:
    result->id = FREE;
    qsort (id_map, channel_max, sizeof(id_map_t), vio_id_compare);

    vu_log(VHOSTMD_ERR, "Could not add channel '%d %s'", id, name);
    vio_channel_free(c);
    return NULL;
}

/*
 * qsort() compare function for id_map_t.
 */
static int vio_id_compare(const void * a, const void * b)
{
    id_map_t *da = (id_map_t *) a;
    id_map_t *db = (id_map_t *) b;

    if (da->id < db->id)
        return -1;
    if (da->id == db->id)
        return 0;
    return 1;
}


/*
 * Check availbale request and return REQ_? status.
 * At the moment there is one request supported:
 * - reading host + VM metrics in XML format in a single buffer.
 */
static REQUEST_T vio_check_request(channel_t * c)
{
    if (strcmp(c->request->content, "GET /metrics/XML\n\n") == 0 ||
        strcmp(c->request->content, "GET /metrics/XML\r\n\r\n") == 0) {
        /* valid request */
        vu_buffer_erase(c->request);
        return REQ_GET_XML;
    } else if (c->request->use >= (c->request->size - 1) ||
               strstr(c->request->content, "\n\n") ||
               strstr(c->request->content, "\r\n\r\n")) {
        /* invalid request -> reset buffer */
        vu_buffer_erase(c->request);

        vu_buffer_erase(c->response);
        vu_buffer_add(c->response, "INVALID REQUEST\n\n", -1);
        return REQ_INVALID;
    } else {
        /* fragment */
        c->request->use = (unsigned) strnlen(c->request->content,
                                             (size_t) c->request->size);
    }

    return REQ_INCOMPLETE;
}

/*
 * Receive data from the socket
 * and append it to the request buffer.
 */
static void vio_recv(channel_t * c)
{
    ssize_t rc = 0;
    REQUEST_T req_type = REQ_INCOMPLETE;

    do {
        char *buf = &c->request->content[c->request->use];
        size_t len = c->request->size - c->request->use - 1;

        rc = recv(c->fd, buf, len, 0);

        if (rc > 0) {
            req_type = vio_check_request(c);
        }
    } while (rc > 0 && req_type == REQ_INCOMPLETE);

    if (req_type == REQ_GET_XML) {
        vio_channel_update(c);
        vio_send(c, EPOLLIN);
    } else if (req_type == REQ_INVALID)
        vio_send(c, EPOLLIN);
}

/*
 * Send data from the response buffer to the socket.
 * The send position is tracked by the vu_buffer.pos value.
 */
static void vio_send(channel_t * c, uint32_t ep_event)
{
    struct epoll_event evt;
    int len;

    while ((len = (int) (c->response->use - c->response->pos)) > 0)
    {
        char *buf = &c->response->content[c->response->pos];
        ssize_t rc = send(c->fd, buf, (size_t) len, 0);

        if (rc > 0)
            c->response->pos += (unsigned) rc;
        else
            break;
    }

    if (ep_event == EPOLLOUT) {
        if (c->response->use <= c->response->pos) {
            /* next request */
            evt.data.ptr = (void *) c;
            evt.events = EPOLLIN;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, c->fd, &evt);
        }
    } else if (ep_event == EPOLLIN) {
        if (c->response->use > c->response->pos) {
            /* incomplete response */
            evt.data.ptr = (void *) c;
            evt.events = EPOLLOUT;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, c->fd, &evt);
        }
    }
}

/*
 * Wrapper function for epoll / IO loop.
 * The main task of the virtio thread is IO on the UDS
 * of virtio channels. Therefore we can call epoll_wait
 * repeatedly for 'epoll_wait_ms'
 */
static void vio_handle_io(unsigned epoll_wait_ms)
{
    int i = 0;
    uint64_t ts_end, ts_now;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts_now = (uint64_t) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    ts_end = ts_now + epoll_wait_ms;

    while (ts_now < ts_end) {
        int wait_ms = (int) (ts_end - ts_now);
        int n =
            epoll_wait(epoll_fd, epoll_events, channel_max + 1, wait_ms);

        for (i = 0; i < n; i++) {
            channel_t *c = (channel_t *) (epoll_events + i)->data.ptr;

            if ((epoll_events + i)->events & EPOLLHUP) {
                /* close the channel when the socket is closed */
                pthread_mutex_lock(&channel_mtx);
                vio_channel_close(c);
                pthread_mutex_unlock(&channel_mtx);
            } else if ((epoll_events + i)->events & EPOLLIN) {
                vio_recv(c);
            } else if ((epoll_events + i)->events & EPOLLOUT) {
                vio_send(c, EPOLLOUT);
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts_now = (uint64_t) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
}

/*
 * Initialize virtio layer
 * Due to preallocation of the channel_t structs we have fix
 * addresses and don't need to lookup buffers from the IO side.
 * Once the channel is added to epoll the vu_buffer can be accessed
 * by the epoll_event.data.ptr.
 */
int virtio_init(int _max_channel, int _expiration_period)
{
    int i;

    if (virtio_status == VIRTIO_INIT) {
        pthread_mutex_init(&channel_mtx, NULL);

        channel_max = _max_channel;
        exp_period = _expiration_period;
        channel_count = 0;
        connection_count = 0;

        channel = (channel_t *) calloc((size_t) (channel_max + 1),
                                       sizeof(channel_t));
        if (channel == NULL)
            goto error;

        channel[0].id = 0;      /* Dom0 */
        vu_buffer_create(&channel[0].metrics, DEFAULT_VU_BUFFER_SIZE);
        for (i = 1; i <= channel_max; i++) {
            channel[i].id = FREE;
            channel[i].fd = -1;
        }

        id_map = (id_map_t *) calloc((size_t) channel_max, sizeof(id_map_t));
        if (id_map == NULL)
            goto error;

        for (i = 0; i < channel_max; i++) {
            id_map[i].id = FREE;
            id_map[i].index = i + 1;
        }

        if (epoll_fd == -1) {

            epoll_events = calloc((size_t) (channel_max + 1),
                                  sizeof(struct epoll_event));
            if (epoll_events == NULL)
                goto error;

            epoll_fd = epoll_create(1);
            if (epoll_fd == -1)
                goto error;
        }

        virtio_status = VIRTIO_ACTIVE;
        vu_log(VHOSTMD_INFO,
               "Activating virtio, using max_channels %d, expiration_time %ld",
               channel_max, exp_period);
    }

    return 0;

  error:
    vu_log(VHOSTMD_ERR, "Virtio initialization failed");
    virtio_status = VIRTIO_ERROR;

    return -1;
}

/*
 * Cleanup virtio layer.
 * Close connections, free resources and set initial values.
 */
int virtio_cleanup(void)
{
    if (virtio_status == VIRTIO_STOP) {

        if (epoll_fd != -1) {
            close(epoll_fd);
            epoll_fd = -1;
        }

        if (channel) {
            int i;
            for (i = 0; i <= channel_max; i++)
                vio_channel_free(&channel[i]);

            free(channel);
            channel = NULL;
        }
        channel_count = 0;
        connection_count = 0;
        channel_max = 0;

        if (id_map) {
            free(id_map);
            id_map = NULL;
        }

        if (epoll_events) {
            free(epoll_events);
            epoll_events = NULL;
        }

        pthread_mutex_destroy(&channel_mtx);

        virtio_status = VIRTIO_INIT;

        return 0;
    }
    return -1;
}

/*
 * Main virtio function and
 * 'start_routine' of pthread_create()
 */
void *virtio_run(void *arg ATTRIBUTE_UNUSED)
{
    if (virtio_status != VIRTIO_ACTIVE) {
        vu_log(VHOSTMD_ERR, "Virtio was not initialized");
        return NULL;
    }

    while (virtio_status == VIRTIO_ACTIVE) {

        if (connection_count < channel_count)
            vio_readdir(channel_path);

        /* Read and process available requests.
         * At the moment a fix time of 3sec should be ok
         * because there is no need to check for new channels
         * or expire available channels more frequently.
         */
        vio_handle_io(3000);

        pthread_mutex_lock(&channel_mtx);
        vio_expire();
        pthread_mutex_unlock(&channel_mtx);
    }

    virtio_cleanup();

    return NULL;
}

/*
 * Update the metrics buffer of a VM/host.
 */
int virtio_metrics_update(const char * buf,
                          int len,
                          int id,
                          const char *name)
{
    int rc = -1;
    channel_t *c = NULL;

    if (buf == NULL || len <= 0 ||
        name == NULL || id < 0 ||
        virtio_status != VIRTIO_ACTIVE)
        return -1;

    pthread_mutex_lock(&channel_mtx);
    if (id == 0) {
        /* Dom0 */
        c = &channel[0];
        vu_buffer_erase(c->metrics);
        vu_buffer_add(c->metrics, buf, len);
        rc = 0;
    }
    else {
        /* VM */
        c = vio_channel_find(id, name, 1);
        if (c) {
            /* update timestamp + buffer */
            vu_buffer_erase(c->metrics);
            vu_buffer_add(c->metrics, buf, len);
            c->update_ts = time(NULL);
            rc = 0;
        }
    }
    pthread_mutex_unlock(&channel_mtx);

    return rc;
}

/*
 * Stop virtio thread.
 */
void virtio_stop(void)
{
    if (virtio_status == VIRTIO_ACTIVE)
        virtio_status = VIRTIO_STOP;
}

/*
 * uv_event_list.c
 *
 *  Created on: 2014-4-18
 *      Author: wang.guilin
 */

#include <unistd.h>
#include <stdlib.h>
#include "uv.h"

typedef struct EVENT_QUEUE_NODE_S
{
    QUEUE queue;
    void* data;
} EVENT_QUEUE_NODE_T;

static inline EVENT_QUEUE_NODE_T* uv_event_list_get_node(uv_event_list_t* event_list, QUEUE* h)
{
    QUEUE* q = null;
    EVENT_QUEUE_NODE_T* node = null;

    uv_mutex_lock(&event_list->mtx);
    if (!QUEUE_EMPTY(h))
    {
        q = QUEUE_HEAD(h);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        node = QUEUE_DATA(q, EVENT_QUEUE_NODE_T, queue);
    }
    uv_mutex_unlock(&event_list->mtx);
    return node;
}

static inline EVENT_QUEUE_NODE_T* uv_event_list_new_node(uv_event_list_t* event_list)
{
    EVENT_QUEUE_NODE_T* node = null;

    node = uv_event_list_get_node(event_list, &event_list->empty_node_queue);
    if (!node)
    {
        node = calloc(1, sizeof(EVENT_QUEUE_NODE_T));
    }

    return node;
}

static inline void uv_event_list_put_node(uv_event_list_t* event_list, EVENT_QUEUE_NODE_T* node)
{
    memset(node, 0, sizeof(EVENT_QUEUE_NODE_T));
    uv_mutex_lock(&event_list->mtx);
    QUEUE_INSERT_TAIL(&event_list->empty_node_queue, &node->queue);
    uv_mutex_unlock(&event_list->mtx);
}

static void uv_event_list_clean_node(uv_event_list_t* event_list)
{
    QUEUE* q = null;
    EVENT_QUEUE_NODE_T* node = null;

    while (!QUEUE_EMPTY(&event_list->empty_node_queue))
    {

        q = QUEUE_HEAD(&event_list->empty_node_queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        node = QUEUE_DATA(q, EVENT_QUEUE_NODE_T, queue);
        ASSERT(node);
        free(node);
    }
}

static void uv_event_list_on_read(uv_event_list_t* event_list)
{
    void* data = null;
    uint64_t u = 0ull;
    EVENT_QUEUE_NODE_T* node = null;

    ASSERT(read(event_list->efd, &u, sizeof(u)) == sizeof(u));

    u = u * 2;
    while (u-- > 0)
    {
        node = uv_event_list_get_node(event_list, &event_list->queue);
        if (node)
        {
            data = node->data;
            uv_event_list_put_node(event_list, node);
            event_list->read_cb(data, event_list->user_data);
        }
        else
        {
            break;
        }
    }
}

static void uv_event_list_on_event(uv_poll_t* handle, int status, int events, void* user_data)
{

    uv_event_list_t* event_list = container_of(handle, uv_event_list_t, poll_handle);

    if (status)
    {
        return;
    }

    if (events & UV_READABLE)
    {
        uv_event_list_on_read(event_list);
    }
}

void uv_event_list_init(uv_loop_t* loop, uv_event_list_t* event_list)
{
    ASSERT(loop);
    ASSERT(event_list);

    memset(event_list, 0, sizeof(uv_event_list_t));

    uv_mutex_init(&event_list->mtx);

    QUEUE_INIT(&event_list->queue);
    QUEUE_INIT(&event_list->empty_node_queue);

    event_list->efd = uv_eventfd(0);

    ASSERT(event_list->efd >= 0);

    uv_poll_init(loop, &event_list->poll_handle, event_list->efd);
}

void uv_event_list_close(uv_event_list_t* event_list)
{
    uv_close((uv_handle_t*) &event_list->poll_handle);
    close(event_list->efd);
    ASSERT(QUEUE_EMPTY(&event_list->queue));

    uv_mutex_destroy(&event_list->mtx);
}

void uv_event_list_start(uv_event_list_t* event_list, uv_event_list_cb read_cb, void* user_data)
{
    ASSERT(event_list);
    ASSERT(read_cb);
    ASSERT(event_list->efd >= 0);

    uv_mutex_lock(&event_list->mtx);

    event_list->read_cb = read_cb;
    event_list->user_data = user_data;

    uv_poll_start(&event_list->poll_handle, UV_READABLE, uv_event_list_on_event, null);

    uv_mutex_unlock(&event_list->mtx);
}

void uv_event_list_stop(uv_event_list_t* event_list)
{
    QUEUE* q = null;
    EVENT_QUEUE_NODE_T* node = null;
    ASSERT(event_list);

    uv_mutex_lock(&event_list->mtx);

    uv_poll_stop(&event_list->poll_handle);

    while (!QUEUE_EMPTY(&event_list->queue))
    {

        q = QUEUE_HEAD(&event_list->queue);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        node = QUEUE_DATA(q, EVENT_QUEUE_NODE_T, queue);
        ASSERT(node);
        event_list->read_cb(node->data, event_list->user_data);
        free(node);
    }

    uv_event_list_clean_node(event_list);

    event_list->read_cb = null;

    uv_mutex_unlock(&event_list->mtx);
}

void uv_event_list_clean_cache(uv_event_list_t* event_list)
{
    uv_mutex_lock(&event_list->mtx);
    uv_event_list_clean_node(event_list);
    uv_mutex_unlock(&event_list->mtx);
}

int uv_event_list_push(uv_event_list_t* event_list, void* data)
{
    int error = 0;
    uint64_t u = 1;
    EVENT_QUEUE_NODE_T* node = null;
    ASSERT(event_list);
    ASSERT(event_list->read_cb);

    node = uv_event_list_new_node(event_list);

    if (node)
    {
        node->data = data;
        uv_mutex_lock(&event_list->mtx);
        QUEUE_INSERT_TAIL(&event_list->queue, &node->queue);
        uv_mutex_unlock(&event_list->mtx);
        write(event_list->efd, &u, sizeof(u));
    }
    else
    {
        error = UV_ENOMEM;
    }
    return error;
}

/*
 * uv_signal_slots.c
 *
 *  Created on: 2014-11-10
 *      Author: wang.guilin
 */

#include "uv.h"

void uv_signal_slots_init(uv_signal_slots_t* uv_signal_slots)
{
    ASSERT(uv_signal_slots);
    QUEUE_INIT(&uv_signal_slots->queue);
}

void uv_signal_slots_close(uv_signal_slots_t* uv_signal_slots)
{
    QUEUE* q;
    QUEUE* n;
    uv_slot_t* slot;

    ASSERT(uv_signal_slots);

    QUEUE_FOREACH_SAFE(q, n, &uv_signal_slots->queue)
    {
        slot = QUEUE_DATA(q, uv_slot_t, queue_uv);
        uv_signal_slot_disconnect(slot);
    }
}

uint32_t uv_signal_slots_trigger(uv_signal_slots_t* uv_signal_slots, void* data)
{
    QUEUE* q;
    QUEUE* n;
    uint32_t count = 0;
    uv_slot_t* slot;

    ASSERT(uv_signal_slots);

    if (data && uv_signal_slots->filter)
    {
        if (uv_signal_slots->filter(data, uv_signal_slots->user_data) != 0)
        {
            return 0;
        }
    }

    QUEUE_FOREACH_SAFE(q, n, &uv_signal_slots->queue)
    {
        slot = QUEUE_DATA(q, uv_slot_t, queue_uv);
        slot->cb(data, slot->user_data);
        count++;
    }

    return count;
}

void uv_signal_slots_filter(uv_signal_slots_t* uv_signal_slots, uv_filter_cb filter, void* user_data)
{
    ASSERT(uv_signal_slots);
    ASSERT(filter);

    uv_signal_slots->filter = filter;
    uv_signal_slots->user_data = user_data;
}

uv_slot_t* uv_signal_slot_connect(uv_signal_slots_t* uv_signal_slots, uv_slot_cb cb, void* user_data)
{
    uv_slot_t* slot;

    ASSERT(uv_signal_slots);
    ASSERT(cb);

    slot = calloc(1, sizeof(uv_slot_t));
    if (slot)
    {
        slot->cb = cb;
        slot->user_data = user_data;
        QUEUE_INSERT_TAIL(&uv_signal_slots->queue, &slot->queue_uv);
    }

    return slot;
}

void uv_signal_slot_disconnect(uv_slot_t* slot)
{
    ASSERT(slot);
    QUEUE_REMOVE(&slot->queue_uv);
    QUEUE_INIT(&slot->queue_uv);
    free(slot);
}

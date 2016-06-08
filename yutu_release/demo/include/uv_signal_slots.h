/*
 * uv_signal_slots.h
 *
 *  Created on: 2014-11-10
 *      Author: wang.guilin
 */

#ifndef UV_SIGNAL_SLOTS_H_
#define UV_SIGNAL_SLOTS_H_

#include "queue.h"

typedef void (*uv_slot_cb)(void* data, void* user_data);
typedef int (*uv_filter_cb)(void* data, void* user_data);

typedef struct uv_slot_s
{
    uv_slot_cb cb;
    void* user_data;
    QUEUE queue_uv;
    QUEUE queue;
} uv_slot_t;

typedef struct uv_signal_slots_
{
    uv_filter_cb filter;
    void* user_data;
    QUEUE queue;
} uv_signal_slots_t;


void uv_signal_slots_init(uv_signal_slots_t* uv_signal_slots);
void uv_signal_slots_close(uv_signal_slots_t* uv_signal_slots);
uint32_t uv_signal_slots_trigger(uv_signal_slots_t* uv_signal_slots, void* data);
void uv_signal_slots_filter(uv_signal_slots_t* uv_signal_slots, uv_filter_cb filter, void* user_data);
uv_slot_t* uv_signal_slot_connect(uv_signal_slots_t* uv_signal_slots, uv_slot_cb cb, void* user_data);
void uv_signal_slot_disconnect(uv_slot_t* slot);



#endif /* UV_SIGNAL_SLOTS_H_ */

/**
 * @file   queue.h
 * @author Lee <cong.li@huamaitel.com>
 * @date   2015-08-06
 * 
 * @brief  form nginx_queue
 * 
 */
#ifndef _queue_H_INCLUDED_
#define _queue_H_INCLUDED_

struct queue_s
{
    struct queue_s *prev;
    struct queue_s *next;
};
typedef struct queue_s  queue_t;


#define queue_init(q)                                                     \
    (q)->prev = q;                                                            \
    (q)->next = q


#define queue_empty(h)                                                    \
    (h == (h)->prev)


#define queue_insert_head(h, x)                                           \
    (x)->next = (h)->next;                                                    \
    (x)->next->prev = x;                                                      \
    (x)->prev = h;                                                            \
    (h)->next = x


#define queue_insert_after   queue_insert_head


#define queue_insert_tail(h, x)                                           \
    (x)->prev = (h)->prev;                                                    \
    (x)->prev->next = x;                                                      \
    (x)->next = h;                                                            \
    (h)->prev = x


#define queue_head(h)                                                     \
    (h)->next


#define queue_last(h)                                                     \
    (h)->prev


#define queue_sentinel(h)                                                 \
    (h)


#define queue_next(q)                                                     \
    (q)->next


#define queue_prev(q)                                                     \
    (q)->prev


#if (NGX_DEBUG)

#define queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next;                                              \
    (x)->prev = NULL;                                                         \
    (x)->next = NULL

#else

#define queue_remove(x)                                                   \
    if ((x)->next) (x)->next->prev = (x)->prev;                           \
    if ((x)->prev) (x)->prev->next = (x)->next

#endif


#define queue_split(h, q, n)                                              \
    (n)->prev = (h)->prev;                                                    \
    (n)->prev->next = n;                                                      \
    (n)->next = q;                                                            \
    (h)->prev = (q)->prev;                                                    \
    (h)->prev->next = h;                                                      \
    (q)->prev = n;


#define queue_add(h, n)                                                   \
    (h)->prev->next = (n)->next;                                              \
    (n)->next->prev = (h)->prev;                                              \
    (h)->prev = (n)->prev;                                                    \
    (h)->prev->next = h;


#ifndef offsetof
#define offsetof(type, identifier) ((size_t)&(((type *)0)->identifier))
#endif

#define queue_data(q, type, link)                                         \
    (type *) ((uint8_t *) q - offsetof(type, link))


queue_t *queue_middle(queue_t *queue);
void queue_sort(queue_t *queue, int (*cmp)(const queue_t *, const queue_t *));

//=============================================================================
struct list_s /* simple list "_l" must be head(not used for data) */
{
    struct list_s *next;
    struct list_s *prev; /* just for remove */
};
typedef struct list_s  list_t;

#define BEGIN_MACRO {
#define END_MACRO }
/* Insert element "_e" into the list, head of "_l" */
#define insert_lhead(_e,_l)  \
    BEGIN_MACRO       \
    (_e)->next = (_l)->next;   \
    (_l)->next = (_e);   \
    (_e)->prev = (_l);   \
    (_e)->next->prev = (_e);   \
    END_MACRO

/* Remove the element "_e" from "_l", "_tmp" is just for cmp */
/* If cannot find "_e" , "_tmp" must be NULL */
#define removee(_e)         \
    BEGIN_MACRO             \
    (_e)->prev->next = (_e)->next;\
    (_e)->next->prev = (_e)->prev;\
    END_MACRO

/* Return non-zero if the given list "_l" is empty, */
/* zero if the list is not empty */
#define lempty(_l, _t) \
    ((_l)->next == (_t))

/* Initialize a list head */
#define linit_head(_l, _t)  \
    BEGIN_MACRO     \
    (_l)->next = (_t); \
    END_MACRO
/* Initialize a list tail */
#define linit_tail(_t)  \
    BEGIN_MACRO     \
    (_t)->next = (NULL); \
    END_MACRO

#define list_data(q, type, link)                                         \
    (type *) ((uint8_t *) q - offsetof(type, link))

#endif /* _queue_H_INCLUDED_ */


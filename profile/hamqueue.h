/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef HAMQUEUE_H
#define HAMQUEUE_H 1

#include <ramses/bufmap.h>

#include <stdint.h>
#include <stdbool.h>

typedef uint16_t hamqueue_off_t;

struct HamQueue {
	hamqueue_off_t len;
	hamqueue_off_t count;
	hamqueue_off_t head;
	struct BMPos q[];
};

/* Check if p refers to the beginning of a fully mapped row in bm. */
bool row_fully_mapped(struct BufferMap *bm, struct BMPos p);

/*
 * Prepare a queue with consecutive elements from bm, in DRAM address order.
 * Returns false when bm is exhausted and no more queues can be formed.
 * If strict is nonzero will only select fully mapped rows.
 */
bool hamqueue_ready(struct HamQueue *q, struct BufferMap *bm, int strict);

/* Compute the storage size of a HamQueue with len elements */
static inline size_t hamqueue_size(int len)
{
	return sizeof(struct HamQueue) + len * sizeof(struct BMPos);
}

static inline void hamqueue_clear(struct HamQueue *q)
{
	q->count = 0;
	q->head = 0;
}

static inline hamqueue_off_t hamqueue_idx(struct HamQueue *q, hamqueue_off_t i)
{
	return (q->head + i) % q->len;
}

#endif /* hamqueue.h */

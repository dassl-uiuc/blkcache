#pragma once
#include <stdlib.h>
#include <atomic>
#include <string.h>
#include <pthread.h>

struct item;
typedef struct item item_t;
typedef item_t *item_handle_t;

struct item {
	char data[4096];
};

typedef union rdma_request_short {
	struct {
		uint64_t offset;
		uint64_t size;
		uint64_t nd_idx;
		uint64_t id;
	};
	uint64_t sector_id;
} __attribute__((aligned(4096))) rdma_request_short_t;

// align structure to page size
struct control_block {
	std::atomic<std::uint64_t> head;
	std::atomic<std::uint64_t> tail;
	uint64_t size;
} __attribute__((aligned(4096)));

typedef struct control_block control_block_t;

// long rdma request
typedef struct rdma_request_long {
	rdma_request_short_t metadata;
	char data[4096];
} __attribute__((aligned(4096))) rdma_request_long_t;

struct circular_buffer {
	control_block_t cb;
	rdma_request_long_t buffer[];
};

uint64_t next(struct circular_buffer *circ_buf, uint64_t p)
{
	return (p + 1) % circ_buf->cb.size;
}

bool push(struct circular_buffer *circ_buf, rdma_request_long_t *itemp)
{
	uint64_t tail = circ_buf->cb.tail.load(std::memory_order_relaxed);
	uint64_t next_tail = next(circ_buf, tail);

	if (next_tail != circ_buf->cb.head.load(std::memory_order_acquire)) {
		memcpy(&circ_buf->buffer[tail], itemp, sizeof(rdma_request_long_t));
		circ_buf->cb.tail.store(next_tail, std::memory_order_release);
		return true;
	}

	return false;
}

bool pop(struct circular_buffer *circ_buf, rdma_request_long_t *itemp)
{
	uint64_t head = circ_buf->cb.head.load(std::memory_order_relaxed);
	uint64_t next_head;

	if (head == circ_buf->cb.tail.load(std::memory_order_acquire))
		return false;

	next_head = next(circ_buf, head);
	memcpy(itemp, &circ_buf->buffer[head], sizeof(rdma_request_long_t));
	circ_buf->cb.head.store(next_head, std::memory_order_release);
	return true;
}
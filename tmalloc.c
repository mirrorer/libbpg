/*
 * Tiny malloc
 * 
 * Copyright (c) 2014 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#ifndef MALLOC_TEST
#define NDEBUG
#endif
#include <assert.h>

/*
 * Note: only works for 32 bit pointers
 */
#define MALLOC_ALIGN 8
#define MALLOC_BLOCK_SIZE 32

#define STATE_FREE      0xaa
#define STATE_ALLOCATED 0x55

struct list_head {
    struct list_head *prev, *next;
};

#define list_entry(el, type, member) \
    ((type *)((uint8_t *)(el) - offsetof(type, member)))

/* Note: the 'state' byte is stored just before the MemBlock header,
   so at most 23 bytes can be allocated in a single block. */
typedef struct MemBlock {
    struct list_head link;
    union {
        uint8_t data[0] __attribute((aligned(MALLOC_ALIGN)));
        struct list_head free_link;
    } u;
} MemBlock;

void *sbrk(intptr_t increment);

/* Invariants: the last block is always a free block. The last free
   block is always the last block. */
static struct list_head free_list;
static struct list_head block_list;
static uint8_t *mem_top;

/* insert 'el' after prev */
static void list_add(struct list_head *el, struct list_head *prev)
{
    struct list_head *next = prev->next;
    prev->next = el;
    el->prev = prev;
    el->next = next;
    next->prev = el;
}

static void list_del(struct list_head *el)
{
    struct list_head *prev, *next;
    prev = el->prev;
    next = el->next;
    prev->next = next;
    next->prev = prev;
}

static size_t get_alloc_size(size_t size)
{
    size = offsetof(MemBlock, u.data) + size;
    /* one more byte for the state byte from the next block */
    size = (size + MALLOC_BLOCK_SIZE) & ~(MALLOC_BLOCK_SIZE - 1);
    return size;
}

/* Note: this size includes the 'state' byte from the next block */
static size_t get_block_size(MemBlock *p)
{
    uint8_t *end;
    struct list_head *el;
    el = p->link.next;
    if (el == &block_list)
        end = mem_top;
    else
        end = (uint8_t *)list_entry(el, MemBlock, link);
    return end - (uint8_t *)p;
}

static inline void set_block_state(MemBlock *p, int state)
{
    ((uint8_t *)p)[-1] = state;
}

static inline int get_block_state(const MemBlock *p)
{
    return ((const uint8_t *)p)[-1];
}

void *malloc(size_t size)
{
    MemBlock *p, *p1;
    struct list_head *el;
    size_t block_size;

    if (size == 0 || size > (INT_MAX - 2 * MALLOC_BLOCK_SIZE))
        return NULL;
    if (free_list.next == NULL) {
        /* init */
        p = sbrk(MALLOC_BLOCK_SIZE * 2);
        if (p == (void *)-1)
            return NULL;
        
        mem_top = sbrk(0);
        free_list.prev = free_list.next = &free_list;
        block_list.prev = block_list.next = &block_list;
        p++;
        set_block_state(p, STATE_FREE);
        list_add(&p->link, &block_list);
        list_add(&p->u.free_link, &free_list);
    }

    size = get_alloc_size(size);
    el = free_list.next;
    for(;;) {
        p = list_entry(el, MemBlock, u.free_link);
        assert(get_block_state(p) == STATE_FREE);
        block_size = get_block_size(p);
        if (size < block_size) {
            goto done1;
        } else if (el == free_list.prev) {
            /* last free block: increase its size */
            if (sbrk(size + MALLOC_BLOCK_SIZE - block_size) == (void *)-1)
                return NULL;
            mem_top = sbrk(0);
        done1:
            p1 = (MemBlock *)((uint8_t *)p + size);
            list_add(&p1->link, &p->link);
            list_add(&p1->u.free_link, &p->u.free_link);
            set_block_state(p1, STATE_FREE);
            list_del(&p->u.free_link);
        done:
            set_block_state(p, STATE_ALLOCATED);
            return p->u.data;
        } else if (size == block_size) {
            list_del(&p->u.free_link);
            goto done;
        }
        el = el->next;
    }
}

void free(void *ptr)
{
    MemBlock *p, *p1;
    struct list_head *el;

    if (!ptr)
        return;
    p = (MemBlock *)((uint8_t *)ptr - offsetof(MemBlock, u.data));
    assert(get_block_state(p) == STATE_ALLOCATED);

    /* mark as free */
    list_add(&p->u.free_link, &free_list);
    set_block_state(p, STATE_FREE);

    /* merge with previous free block if possible */
    el = p->link.prev;
    if (el != &block_list) {
        p1 = list_entry(el, MemBlock, link);
        if (get_block_state(p1) == STATE_FREE) {
            list_del(&p->link);
            list_del(&p->u.free_link);
            p = p1;
        }
    }
    /* merge with next block if possible */
    el = p->link.next;
    if (el != &block_list) {
        p1 = list_entry(el, MemBlock, link);
        if (get_block_state(p1) == STATE_FREE) {
            list_del(&p1->link);
            /* keep p in the same position in free_list as p1 */
            list_del(&p->u.free_link);
            list_add(&p->u.free_link, &p1->u.free_link);
            list_del(&p1->u.free_link);
        }
    }
}

void *realloc(void *ptr, size_t size)
{
    MemBlock *p;
    void *ptr1;
    size_t size1;

    if (ptr == NULL) {
        return malloc(size);
    } else if (size == 0) {
        free(ptr);
        return NULL;
    } else {
        p = (MemBlock *)((uint8_t *)ptr - offsetof(MemBlock, u.data));
        assert(get_block_state(p) == STATE_ALLOCATED);
        ptr1 = malloc(size);
        if (!ptr1)
            return NULL;
        /* Note: never the last block so it is valid */
        size1 = (uint8_t *)list_entry(p->link.next, MemBlock, link) - 
            p->u.data - 1;
        if (size < size1)
            size1 = size;
        memcpy(ptr1, ptr, size1);
        free(ptr);
        return ptr1;
    }
}

#ifdef MALLOC_TEST
static void malloc_check(void)
{
    MemBlock *p;
    struct list_head *el;
    int state;

    for(el = block_list.next; el != &block_list; el = el->next) {
        p = list_entry(el, MemBlock, link);
        state = get_block_state(p);
        assert(state == STATE_FREE || state == STATE_ALLOCATED);
        if (el->next != &block_list)
            assert(el->next > el);
    }
    for(el = free_list.next; el != &free_list; el = el->next) {
        p = list_entry(el, MemBlock, u.free_link);
        assert(get_block_state(p) == STATE_FREE);
    }

    /* check invariant */
    el = free_list.prev;
    if (el != &free_list) {
        p = list_entry(el, MemBlock, u.free_link);
        assert(&p->link == block_list.prev);
    }
}

static void malloc_dump(void)
{
    MemBlock *p;
    struct list_head *el;
    
    printf("blocks:\n");
    for(el = block_list.next; el != &block_list; el = el->next) {
        p = list_entry(el, MemBlock, link);
        printf("block: %p next=%p free=%d size=%u\n", p, p->link.next, 
               get_block_state(p) == STATE_FREE,
               (unsigned int)get_block_size(p));
    }
    printf("free list:\n");
    for(el = free_list.next; el != &free_list; el = el->next) {
        p = list_entry(el, MemBlock, u.free_link);
        printf("block: %p size=%u\n", p, (unsigned int)get_block_size(p));
    }
}

int main(int argc, char **argv)
{
    int i, n, j, size;
    void **tab;

    n = 100;
    tab = malloc(sizeof(void *) * n);
    memset(tab, 0, n * sizeof(void *));

    for(i = 0; i < n * 1000; i++) {
        j = random() % n;

        free(tab[j]);

        malloc_check();

        size = random() % 500;
        tab[j] = malloc(size);
        memset(tab[j], 0x11, size);

        malloc_check();
    }

    malloc_dump();

    for(i = 0; i < n; i++) {
        free(tab[i]);
    }
    return 0;
}
#endif

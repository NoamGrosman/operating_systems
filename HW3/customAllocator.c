#include <stdbool.h>
#include "customAllocator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

// Explicit declarations for sbrk and brk (needed for C99 standard)
extern void *sbrk(intptr_t increment);
extern int brk(void *addr);

Block* blockList = NULL;
static void* heap_start = NULL;
//helper function declaration:
static void init_heap_start_if_needed(void);
static size_t align4(size_t x);
static void* block_to_payload(Block* b);
static Block* find_block_by_payload(void* payload);
static Block* find_best_fit(size_t need);
static void split_block_if_worth(Block* b, size_t need);
static int are_adjacent(Block* a, Block* b);
static Block* find_prev(Block* target);
static void init_heap_start_if_needed(void) {
    if (!heap_start) {
        heap_start = sbrk(0);
    }
}
static size_t align4(size_t x) {
    if (x == 0) return 0;
    return (size_t)ALIGN_TO_MULT_OF_4(x);
}
static void* block_to_payload(Block* b) {
    return (void*)(b + 1);
}

static Block* find_block_by_payload(void* payload) {
    for (Block* it = blockList; it != NULL; it = it->next) {
        if (block_to_payload(it) == payload) {
            return it;
        }
    }
    return NULL;
}
static Block* find_best_fit(size_t need) {
    Block* best = NULL;
    for (Block* it = blockList; it != NULL; it = it->next) {
        if (it->free && it->size >= need) {
            if (!best || it->size < best->size) {
                best = it;
            }
        }
    }
    return best;
}

static void split_block_if_worth(Block* b, size_t need) {
    if (!b) return;

    const size_t MIN_REMAIN = sizeof(Block) + 4;
    if (b->size >= need + MIN_REMAIN) {
        char* base = (char*)b;

        Block* newb = (Block*)(base + sizeof(Block) + need);
        newb->size = b->size - need - sizeof(Block);
        newb->free = true;
        newb->next = b->next;

        b->size = need;
        b->next = newb;
    }
}
static int are_adjacent(Block* a, Block* b) {
    if (!a || !b) return 0;
    char* end_a = (char*)a + sizeof(Block) + a->size;
    return end_a == (char*)b;
}
static Block* find_prev(Block* target) {
    if (!blockList || blockList == target) return NULL;
    for (Block* it = blockList; it && it->next; it = it->next) {
        if (it->next == target) return it;
    }
    return NULL;
}
static void coalesce_around(Block* b) {
    if (!b) return;
    while (b->next && b->next->free && are_adjacent(b, b->next)) {
        Block* nxt = b->next;
        b->size += sizeof(Block) + nxt->size;
        b->next = nxt->next;
    }
    Block* prev = find_prev(b);
    if (prev && prev->free && are_adjacent(prev, b)) {
        prev->size += sizeof(Block) + b->size;
        prev->next = b->next;
        b = prev;
        while (b->next && b->next->free && are_adjacent(b, b->next)) {
            Block* nxt = b->next;
            b->size += sizeof(Block) + nxt->size;
            b->next = nxt->next;
        }
    }
}
static void try_shrink_heap(void) {
    while (blockList) {
        Block* prev = NULL;
        Block* last = blockList;
        while (last->next) {
            prev = last;
            last = last->next;
        }

        if (!last->free) return;

        void* cur_brk = sbrk(0);
        char* end_last = (char*)last + sizeof(Block) + last->size;
        if ((void*)end_last != cur_brk) return;
        if (brk((void*)last) != 0) {
            printf("<sbrk/brk error>: out of memory\n");
            exit(1);
        }
        if (prev) prev->next = NULL;
        else blockList = NULL;
    }
}
void* customMalloc(size_t size){
    if (size == 0)return NULL;
    init_heap_start_if_needed();
    size_t need_size = align4(size);
    Block *allocate = find_best_fit(need_size);
    if (allocate){
        allocate->free = false;
        split_block_if_worth(allocate, need_size);
        return block_to_payload(allocate);
    }
    void* mem = sbrk(sizeof(Block) + need_size);
    if (mem == SBRK_FAIL) {
        printf("<sbrk/brk error>: out of memory\n");
        exit(1);
    }

    Block* nb = (Block*)mem;
    nb->size = need_size;
    nb->free = false;
    nb->next = NULL;

    if (!blockList) {
        blockList = nb;
    } else {
        Block* it = blockList;
        while (it->next) it = it->next;
        it->next = nb;
    }
    return block_to_payload(nb);
}

void customFree(void* ptr){
    if (ptr == NULL){
        printf ("<free error>: passed null pointer\n");
        return;
    }
    init_heap_start_if_needed();
    Block *cur_ptr = find_block_by_payload(ptr);
    if (!cur_ptr){
        printf("<free error>: passed non-heap pointer\n");
        return;
    }
cur_ptr->free=true;
coalesce_around(cur_ptr);
try_shrink_heap();
}
void* customCalloc(size_t nmemb, size_t size){
    if ( (nmemb == 0) || (size == 0)){
        return NULL;
    }
    if (size != 0 && nmemb > (SIZE_MAX / size)) {
        return NULL;
    }
    size_t mul= nmemb*size;
    void *ptr_call = customMalloc(mul);
    if (ptr_call == NULL)return NULL;
    memset (ptr_call,0,mul);
    return ptr_call;
}
void* customRealloc(void* ptr, size_t size) {
    if (!ptr) {
        ptr = customMalloc(size);
        return ptr;
    }
    if (size == 0) {
        customFree(ptr);
        return NULL;
    }
    Block *new_block = find_block_by_payload(ptr);
    if (!new_block) {
        printf("<realloc error>: passed non-heap pointer\n");
        return NULL;
    }
    size_t new_size = align4(size);
    size_t old = new_block->size;
    if (new_size <= old) {
        if (new_size == old) return ptr;
        split_block_if_worth(new_block, new_size);
        if (new_block->size == new_size) return block_to_payload(new_block);
        void *new_ptr = customMalloc(size);
        if (!new_ptr)return NULL;
        memcpy(new_ptr, ptr, size);
        customFree(ptr);
        return new_ptr;
    }
    void *new_ptr = customMalloc(size);
    if (!new_ptr)return NULL;
    memcpy(new_ptr, ptr, old);
    customFree(ptr);
    return new_ptr;
}

/*=============================================================================
* Part B - Multi-threaded Memory Allocator Implementation
=============================================================================*/

// Global state for multi-threaded allocator
static MemRegion* mt_regions = NULL;       // Array of initial regions
static MemRegion* mt_extra_regions = NULL; // Linked list of dynamically added regions
static int mt_next_region = 0;             // Index for round-robin allocation
static pthread_mutex_t mt_global_lock = PTHREAD_MUTEX_INITIALIZER;
static bool mt_initialized = false;

// Helper: Align size to 4 bytes
static size_t mt_align4(size_t x) {
    if (x == 0) return 0;
    return (size_t)ALIGN_TO_MULT_OF_4(x);
}

// Helper: Convert MTBlock to payload pointer
static void* mt_block_to_payload(MTBlock* b) {
    return (void*)(b + 1);
}

// Helper: Find best fit block in a region
static MTBlock* mt_find_best_fit(MemRegion* region, size_t need) {
    MTBlock* best = NULL;
    for (MTBlock* it = region->block_list; it != NULL; it = it->next) {
        if (it->free && it->size >= need) {
            if (!best || it->size < best->size) {
                best = it;
            }
        }
    }
    return best;
}

// Helper: Split block if there's enough remaining space
static void mt_split_block_if_worth(MTBlock* b, size_t need) {
    if (!b) return;
    const size_t MIN_REMAIN = sizeof(MTBlock) + 4;
    if (b->size >= need + MIN_REMAIN) {
        char* base = (char*)b;
        MTBlock* newb = (MTBlock*)(base + sizeof(MTBlock) + need);
        newb->size = b->size - need - sizeof(MTBlock);
        newb->free = true;
        newb->next = b->next;
        b->size = need;
        b->next = newb;
    }
}

// Helper: Check if two blocks are adjacent in memory
static int mt_are_adjacent(MTBlock* a, MTBlock* b) {
    if (!a || !b) return 0;
    char* end_a = (char*)a + sizeof(MTBlock) + a->size;
    return end_a == (char*)b;
}

// Helper: Find previous block in list
static MTBlock* mt_find_prev(MemRegion* region, MTBlock* target) {
    if (!region->block_list || region->block_list == target) return NULL;
    for (MTBlock* it = region->block_list; it && it->next; it = it->next) {
        if (it->next == target) return it;
    }
    return NULL;
}

// Helper: Coalesce adjacent free blocks
static void mt_coalesce_around(MemRegion* region, MTBlock* b) {
    if (!b) return;
    // Merge with next blocks
    while (b->next && b->next->free && mt_are_adjacent(b, b->next)) {
        MTBlock* nxt = b->next;
        b->size += sizeof(MTBlock) + nxt->size;
        b->next = nxt->next;
    }
    // Merge with previous block
    MTBlock* prev = mt_find_prev(region, b);
    if (prev && prev->free && mt_are_adjacent(prev, b)) {
        prev->size += sizeof(MTBlock) + b->size;
        prev->next = b->next;
        b = prev;
        while (b->next && b->next->free && mt_are_adjacent(b, b->next)) {
            MTBlock* nxt = b->next;
            b->size += sizeof(MTBlock) + nxt->size;
            b->next = nxt->next;
        }
    }
}

// Helper: Find block by payload in a region
static MTBlock* mt_find_block_by_payload(MemRegion* region, void* payload) {
    for (MTBlock* it = region->block_list; it != NULL; it = it->next) {
        if (mt_block_to_payload(it) == payload) {
            return it;
        }
    }
    return NULL;
}

// Helper: Initialize a region with given memory
static void mt_init_region(MemRegion* region, void* mem, size_t size) {
    region->start = mem;
    region->total_size = size;
    pthread_mutex_init(&region->lock, NULL);
    region->next = NULL;
    
    // Initialize with a single free block covering the whole region
    MTBlock* initial_block = (MTBlock*)mem;
    initial_block->size = size - sizeof(MTBlock);
    initial_block->next = NULL;
    initial_block->free = true;
    region->block_list = initial_block;
}

// Helper: Find which region contains a pointer
static MemRegion* mt_find_region_for_ptr(void* ptr) {
    // Check initial regions
    for (int i = 0; i < MT_INITIAL_REGIONS; i++) {
        MemRegion* region = &mt_regions[i];
        char* start = (char*)region->start;
        char* end = start + region->total_size;
        if ((char*)ptr >= start && (char*)ptr < end) {
            return region;
        }
    }
    // Check extra regions
    for (MemRegion* region = mt_extra_regions; region != NULL; region = region->next) {
        char* start = (char*)region->start;
        char* end = start + region->total_size;
        if ((char*)ptr >= start && (char*)ptr < end) {
            return region;
        }
    }
    return NULL;
}

// Helper: Create a new extra region
static MemRegion* mt_create_extra_region(void) {
    // Allocate memory for region structure using sbrk
    void* region_mem = sbrk(sizeof(MemRegion));
    if (region_mem == SBRK_FAIL) {
        printf("<sbrk/brk error>: out of memory\n");
        exit(1);
    }
    MemRegion* new_region = (MemRegion*)region_mem;
    
    // Allocate memory for the region's heap space
    void* heap_mem = sbrk(MT_REGION_SIZE);
    if (heap_mem == SBRK_FAIL) {
        printf("<sbrk/brk error>: out of memory\n");
        exit(1);
    }
    
    mt_init_region(new_region, heap_mem, MT_REGION_SIZE);
    
    // Add to extra regions list
    new_region->next = mt_extra_regions;
    mt_extra_regions = new_region;
    
    return new_region;
}

// Initialize the multi-threaded heap
void heapCreate() {
    pthread_mutex_lock(&mt_global_lock);
    
    if (mt_initialized) {
        pthread_mutex_unlock(&mt_global_lock);
        return;
    }
    
    // Allocate memory for region structures
    void* regions_mem = sbrk(MT_INITIAL_REGIONS * sizeof(MemRegion));
    if (regions_mem == SBRK_FAIL) {
        printf("<sbrk/brk error>: out of memory\n");
        exit(1);
    }
    mt_regions = (MemRegion*)regions_mem;
    
    // Allocate and initialize each region
    for (int i = 0; i < MT_INITIAL_REGIONS; i++) {
        void* region_heap = sbrk(MT_REGION_SIZE);
        if (region_heap == SBRK_FAIL) {
            printf("<sbrk/brk error>: out of memory\n");
            exit(1);
        }
        mt_init_region(&mt_regions[i], region_heap, MT_REGION_SIZE);
    }
    
    mt_next_region = 0;
    mt_initialized = true;
    
    pthread_mutex_unlock(&mt_global_lock);
}

// Destroy the multi-threaded heap
void heapKill() {
    pthread_mutex_lock(&mt_global_lock);
    
    if (!mt_initialized) {
        pthread_mutex_unlock(&mt_global_lock);
        return;
    }
    
    // Destroy mutexes for initial regions
    for (int i = 0; i < MT_INITIAL_REGIONS; i++) {
        pthread_mutex_destroy(&mt_regions[i].lock);
    }
    
    // Destroy mutexes for extra regions
    for (MemRegion* region = mt_extra_regions; region != NULL; region = region->next) {
        pthread_mutex_destroy(&region->lock);
    }
    
    // Reset state (memory will be reclaimed when process exits)
    mt_regions = NULL;
    mt_extra_regions = NULL;
    mt_next_region = 0;
    mt_initialized = false;
    
    pthread_mutex_unlock(&mt_global_lock);
}

// Multi-threaded malloc
void* customMTMalloc(size_t size) {
    if (size == 0) return NULL;
    if (!mt_initialized) return NULL;
    
    size_t need_size = mt_align4(size);
    
    // Need space for block header too
    size_t total_need = need_size;
    
    // Check this won't exceed region size
    if (total_need + sizeof(MTBlock) > MT_REGION_SIZE) {
        // Cannot allocate more than region size
        return NULL;
    }
    
    pthread_mutex_lock(&mt_global_lock);
    
    int start_region = mt_next_region;
    int regions_checked = 0;
    int total_regions = MT_INITIAL_REGIONS;
    
    // Count extra regions
    for (MemRegion* r = mt_extra_regions; r != NULL; r = r->next) {
        total_regions++;
    }
    
    // Try to find a region with enough space using round-robin
    while (regions_checked < MT_INITIAL_REGIONS) {
        int region_idx = (start_region + regions_checked) % MT_INITIAL_REGIONS;
        MemRegion* region = &mt_regions[region_idx];
        
        pthread_mutex_lock(&region->lock);
        
        MTBlock* block = mt_find_best_fit(region, need_size);
        if (block) {
            block->free = false;
            mt_split_block_if_worth(block, need_size);
            
            // Update next region for round-robin
            mt_next_region = (region_idx + 1) % MT_INITIAL_REGIONS;
            
            pthread_mutex_unlock(&region->lock);
            pthread_mutex_unlock(&mt_global_lock);
            return mt_block_to_payload(block);
        }
        
        pthread_mutex_unlock(&region->lock);
        regions_checked++;
    }
    
    // Check extra regions
    for (MemRegion* region = mt_extra_regions; region != NULL; region = region->next) {
        pthread_mutex_lock(&region->lock);
        
        MTBlock* block = mt_find_best_fit(region, need_size);
        if (block) {
            block->free = false;
            mt_split_block_if_worth(block, need_size);
            
            pthread_mutex_unlock(&region->lock);
            pthread_mutex_unlock(&mt_global_lock);
            return mt_block_to_payload(block);
        }
        
        pthread_mutex_unlock(&region->lock);
    }
    
    // No existing region has space, create a new one
    MemRegion* new_region = mt_create_extra_region();
    
    pthread_mutex_lock(&new_region->lock);
    
    MTBlock* block = mt_find_best_fit(new_region, need_size);
    if (block) {
        block->free = false;
        mt_split_block_if_worth(block, need_size);
        
        pthread_mutex_unlock(&new_region->lock);
        pthread_mutex_unlock(&mt_global_lock);
        return mt_block_to_payload(block);
    }
    
    pthread_mutex_unlock(&new_region->lock);
    pthread_mutex_unlock(&mt_global_lock);
    
    return NULL;
}

// Multi-threaded free
void customMTFree(void* ptr) {
    if (ptr == NULL) {
        printf("<free error>: passed null pointer\n");
        return;
    }
    
    if (!mt_initialized) {
        printf("<free error>: passed non-heap pointer\n");
        return;
    }
    
    // Find which region this pointer belongs to
    MemRegion* region = mt_find_region_for_ptr(ptr);
    if (!region) {
        printf("<free error>: passed non-heap pointer\n");
        return;
    }
    
    pthread_mutex_lock(&region->lock);
    
    MTBlock* block = mt_find_block_by_payload(region, ptr);
    if (!block) {
        pthread_mutex_unlock(&region->lock);
        printf("<free error>: passed non-heap pointer\n");
        return;
    }
    
    block->free = true;
    mt_coalesce_around(region, block);
    
    pthread_mutex_unlock(&region->lock);
}

// Multi-threaded calloc
void* customMTCalloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    
    // Check for overflow
    if (size != 0 && nmemb > (SIZE_MAX / size)) {
        return NULL;
    }
    
    size_t total_size = nmemb * size;
    void* ptr = customMTMalloc(total_size);
    if (ptr == NULL) return NULL;
    
    memset(ptr, 0, total_size);
    return ptr;
}

// Multi-threaded realloc
void* customMTRealloc(void* ptr, size_t size) {
    // If ptr is NULL, equivalent to malloc
    if (!ptr) {
        return customMTMalloc(size);
    }
    
    // If size is 0, free and return NULL
    if (size == 0) {
        customMTFree(ptr);
        return NULL;
    }
    
    if (!mt_initialized) {
        printf("<realloc error>: passed non-heap pointer\n");
        return NULL;
    }
    
    // Find which region this pointer belongs to
    MemRegion* region = mt_find_region_for_ptr(ptr);
    if (!region) {
        printf("<realloc error>: passed non-heap pointer\n");
        return NULL;
    }
    
    pthread_mutex_lock(&region->lock);
    
    MTBlock* block = mt_find_block_by_payload(region, ptr);
    if (!block) {
        pthread_mutex_unlock(&region->lock);
        printf("<realloc error>: passed non-heap pointer\n");
        return NULL;
    }
    
    size_t old_size = block->size;
    size_t new_size = mt_align4(size);
    
    // If new size fits in current block
    if (new_size <= old_size) {
        if (new_size == old_size) {
            pthread_mutex_unlock(&region->lock);
            return ptr;
        }
        
        mt_split_block_if_worth(block, new_size);
        if (block->size == new_size) {
            pthread_mutex_unlock(&region->lock);
            return mt_block_to_payload(block);
        }
        
        // Could not split efficiently, allocate new block
        pthread_mutex_unlock(&region->lock);
        
        void* new_ptr = customMTMalloc(size);
        if (!new_ptr) return NULL;
        
        memcpy(new_ptr, ptr, size);
        customMTFree(ptr);
        return new_ptr;
    }
    
    pthread_mutex_unlock(&region->lock);
    
    // Need more space, allocate new block
    void* new_ptr = customMTMalloc(size);
    if (!new_ptr) return NULL;
    
    memcpy(new_ptr, ptr, old_size);
    customMTFree(ptr);
    return new_ptr;
}

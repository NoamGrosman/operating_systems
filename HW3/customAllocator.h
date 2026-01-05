#ifndef __CUSTOM_ALLOCATOR__
#define __CUSTOM_ALLOCATOR__

/*=============================================================================
* do no edit lines below!
=============================================================================*/
#include <stddef.h> //for size_t

//Part A - single thread memory allocator
void* customMalloc(size_t size);
void customFree(void* ptr);
void* customCalloc(size_t nmemb, size_t size);
void* customRealloc(void* ptr, size_t size);

//Part B - multi thread memory allocator
void* customMTMalloc(size_t size);
void customMTFree(void* ptr);
void* customMTCalloc(size_t nmemb, size_t size);
void* customMTRealloc(void* ptr, size_t size);

// Part B - helper functions for multi thread memory allocator
void heapCreate();
void heapKill();

/*=============================================================================
* do no edit lines above!
=============================================================================*/

/*=============================================================================
* defines
=============================================================================*/
#define SBRK_FAIL (void*)(-1)
#define ALIGN_TO_MULT_OF_4(x) (((((x) - 1) >> 2) << 2) + 4)

/*=============================================================================
* Block
=============================================================================*/
//suggestion for block usage - feel free to change this
typedef struct Block
{
    size_t size;
    struct Block* next;
    bool free;
} Block;
extern Block* blockList;

/*=============================================================================
* Part B - Multi-threaded allocator definitions
=============================================================================*/
#include <pthread.h>

#define MT_REGION_SIZE 4096        // 4KB per region
#define MT_INITIAL_REGIONS 8       // 8 initial regions

// Block structure for multi-threaded allocator (within regions)
typedef struct MTBlock
{
    size_t size;
    struct MTBlock* next;
    bool free;
} MTBlock;

// Memory region structure
typedef struct MemRegion
{
    void* start;                   // Start of region memory
    size_t total_size;             // Total size of region
    MTBlock* block_list;           // List of blocks in this region
    pthread_mutex_t lock;          // Per-region mutex
    struct MemRegion* next;        // Link to next region (for dynamic regions)
} MemRegion;

#endif // CUSTOM_ALLOCATOR

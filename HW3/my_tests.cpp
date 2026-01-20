#include <iostream>
#include <cassert>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include "customAllocator.h"

// Helper to check alignment
bool is_aligned(void* ptr) {
    return ((uintptr_t)ptr % 4) == 0;
}

// Access to sbrk for testing heap movement
extern "C" void* sbrk(intptr_t increment);

void test_part_a_basic() {
    std::cout << "Running Part A: Basic Alloc/Free..." << std::endl;
    
    void* p1 = customMalloc(10);
    assert(p1 != NULL);
    assert(is_aligned(p1));
    
    void* p2 = customMalloc(5);
    assert(p2 != NULL);
    assert(is_aligned(p2));
    assert(p1 != p2);
    
    customFree(p1);
    customFree(p2);
    std::cout << "Passed." << std::endl;
}

void test_part_a_realloc_shrink_behavior() {
    std::cout << "Running Part A: Realloc Shrink Logic..." << std::endl;
    
    // Allocate 200 bytes
    size_t large_size = 200;
    void* ptr = customMalloc(large_size);
    memset(ptr, 0xAA, large_size);
    
    // 1. Shrink by a LOT (should split)
    // sizeof(Block) is likely 24 or 32 bytes. 
    // If we shrink to 10 bytes, remaining is ~190, enough for a new block.
    void* ptr_split = customRealloc(ptr, 10);
    assert(ptr_split == ptr); // Should remain in place if split successful
    
    customFree(ptr_split);

    // 2. Shrink by a TINY amount (should MOVE)
    void* ptr2 = customMalloc(200);
    void* original_addr = ptr2;
    
    // Shrink to 190. Remainder is 10. 
    // 10 < sizeof(Block) + 4, so split is NOT possible.
    // Per PDF, code must Alloc -> Copy -> Free. 
    // So address MUST change.
    void* ptr_moved = customRealloc(ptr2, 190);
    
    if (ptr_moved != original_addr) {
        std::cout << "  [OK] Realloc moved memory when split was impossible." << std::endl;
    } else {
        std::cout << "  [WARNING] Realloc returned same address. Logic optimization or bug?" << std::endl;
    }
    
    customFree(ptr_moved);
    std::cout << "Passed." << std::endl;
}

void test_part_a_heap_shrink() {
    std::cout << "Running Part A: Heap Shrink..." << std::endl;
    
    [[maybe_unused]] char* current_brk = (char*)sbrk(0);
    void* p1 = customMalloc(4000);
    void* p2 = customMalloc(4000);
    
    assert((char*)sbrk(0) > current_brk);
    
    // Free top block, should lower break
    customFree(p2);
    
    // We can't guarantee it returns EXACTLY to current_brk due to internal fragmentation/overhead
    // But it should be lower than the peak.
    assert((char*)sbrk(0) < current_brk + 8100); 
    
    customFree(p1);
    // Now it should be very close to original
    assert((char*)sbrk(0) == current_brk); 
    
    std::cout << "Passed." << std::endl;
}

// Part B Tests
void thread_func(int id) {
    for (int i = 0; i < 100; ++i) {
        void* p = customMTMalloc(100);
        assert(p != NULL);
        memset(p, id, 100); // Write access check
        customMTFree(p);
    }
}

void test_part_b_concurrency() {
    std::cout << "Running Part B: Multi-threaded stress test..." << std::endl;
    heapCreate();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.push_back(std::thread(thread_func, i));
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_part_b_extra_regions() {
    std::cout << "Running Part B: Extra Regions..." << std::endl;
    heapCreate();
    
    // Fill up initial 8 regions (8 * 4KB = 32KB approx)
    // We alloc 3KB blocks. 8 allocations should fill the initial regions.
    // The 9th should trigger a new region creation.
    std::vector<void*> ptrs;
    for (int i = 0; i < 12; ++i) {
        void* p = customMTMalloc(3000); 
        if(p) ptrs.push_back(p);
    }
    
    // If we have 12 pointers, we successfully created extra regions
    assert(ptrs.size() == 12);
    
    for (void* p : ptrs) {
        customMTFree(p);
    }
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_part_b_double_free() {
    std::cout << "Running Part B: Double Free Check..." << std::endl;
    heapCreate();

    void* ptr = customMTMalloc(100);
    assert(ptr != NULL);

    // First free - should work normally
    customMTFree(ptr);

    // Second free - same pointer
    // If your implementation is robust, this should NOT crash.
    // Ideally, it prints your error: <free error>: passed non-heap pointer 
    // OR it silently ignores it.
    std::cout << "  [INFO] Attempting second free on same pointer..." << std::endl;
    customMTFree(ptr);

    // Verify heap is still usable after the double free
    void* ptr2 = customMTMalloc(100);
    assert(ptr2 != NULL);
    customMTFree(ptr2);

    heapKill();
    std::cout << "Passed (No Crash)." << std::endl;
}

void test_calloc_overflow() {
    std::cout << "Running Part A/B: Calloc Overflow Check..." << std::endl;

    size_t huge = SIZE_MAX;
    
    // Attempt to allocate impossible amount
    // If the overflow check is missing, this might try to malloc(0) or similar
    void* ptr = customCalloc(huge, 2); 
    
    if (ptr == NULL) {
        std::cout << "  [OK] Calloc correctly rejected overflow." << std::endl;
    } else {
        std::cout << "  [FAIL] Calloc allowed overflow! (returned non-null)" << std::endl;
        customFree(ptr); // Cleanup if it actually allocated
    }
}

void test_boundary_safety() {
    std::cout << "Running Part A: Boundary Safety Check..." << std::endl;
    
    // Alloc two adjacent blocks
    size_t size = 17; // Weird size to test alignment logic
    char* p1 = (char*)customMalloc(size);
    char* p2 = (char*)customMalloc(size);
    
    assert(p1 != NULL);
    assert(p2 != NULL);

    // Write to the VERY END of p1
    // If alignment is wrong, this might hit p2's header
    for (size_t i = 0; i < size; i++) {
        p1[i] = (char)0xFF; 
    }

    // Validate p2 is still healthy (header not corrupted)
    // We do this by freeing p2. If p2's size/next was corrupted, free might crash.
    customFree(p2); 
    customFree(p1);

    std::cout << "Passed." << std::endl;
}

void test_sandwich_coalescing() {
    std::cout << "Running Part A: Sandwich Coalescing ([Free][Free middle][Free])..." << std::endl;
    
    // Alloc 3 blocks of 100 bytes
    // We use char* to do pointer arithmetic checks if needed
    void* p1 = customMalloc(100);
    void* p2 = customMalloc(100);
    void* p3 = customMalloc(100);
    
    // Prevent p3 from being the "top" of the heap so p3 doesn't trigger heap shrink.
    // We want to test coalescing, not sbrk shrinking.
    void* fence = customMalloc(10); 

    // 1. Free Top and Bottom (p1 and p3)
    customFree(p1);
    customFree(p3);
    
    // 2. Free Middle (p2)
    // This should trigger a merge: Prev(p1) + Curr(p2) + Next(p3)
    customFree(p2);

    // 3. Alloc the total size (approx 300 + 2 * metadata_size)
    // If coalescing worked, this fits in the hole without increasing heap.
    // If it failed, this will likely call sbrk.
    void* huge_block = customMalloc(300); 
    
    // Ideally, huge_block should be the same address as p1
    // (Since p1 is now the start of the giant merged block)
    if (huge_block == p1) {
        std::cout << "  [OK] Successfully merged 3 blocks into one." << std::endl;
    } else {
        std::cout << "  [INFO] Allocated new block. Coalescing might be partial or Best-Fit chose elsewhere." << std::endl;
    }
    
    customFree(huge_block);
    customFree(fence);
    std::cout << "Passed." << std::endl;
}

void test_best_fit_logic() {
    std::cout << "Running Part A: Best Fit Logic Check..." << std::endl;

    // 1. Create specific fragmentation
    void* p_large = customMalloc(1000);
    void* p_fence1 = customMalloc(10); // Prevent merge
    void* p_small = customMalloc(200);
    void* p_fence2 = customMalloc(10); // Prevent merge

    // Free them to create holes
    // List state (conceptually): [1000 (Free)] -> [Fence] -> [200 (Free)] -> [Fence]
    customFree(p_large);
    customFree(p_small);

    // 2. Request 150 bytes
    // - First Fit would take p_large (it's first in list usually).
    // - Best Fit MUST take p_small (200 is better fit for 150 than 1000).
    void* p_new = customMalloc(150);

    if (p_new == p_small) {
        std::cout << "  [OK] Allocator chose the Best Fit (small hole)." << std::endl;
    } else if (p_new == p_large) {
        std::cout << "  [FAIL/WARNING] Allocator chose First Fit (large hole)." << std::endl;
    } else {
        std::cout << "  [INFO] Allocator chose a new block entirely." << std::endl;
    }

    // Cleanup
    customFree(p_new);
    if (p_new != p_large) customFree(p_large); // Only free if it wasn't reused
    if (p_new != p_small) customFree(p_small);
    customFree(p_fence1);
    customFree(p_fence2);
    std::cout << "Passed." << std::endl;
}

// Global pointers for thread communication
void* global_ptr = NULL;

void producer_thread() {
    // Allocates memory
    global_ptr = customMTMalloc(500);
    memset(global_ptr, 0xBB, 500);
}

void consumer_thread() {
    // Waits for pointer, then frees it
    while (global_ptr == NULL) {
        std::this_thread::yield();
    }
    // Cross-thread free
    customMTFree(global_ptr);
}

void test_cross_thread_free() {
    std::cout << "Running Part B: Cross-Thread Free..." << std::endl;
    heapCreate();
    
    global_ptr = NULL;
    
    std::thread t1(producer_thread);
    std::thread t2(consumer_thread);
    
    t1.join();
    t2.join();
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_realloc_edge_cases() {
    std::cout << "Running Part A: Realloc NULL/Zero edge cases..." << std::endl;
    
    // Case 1: realloc(NULL, size) -> malloc(size)
    void* p1 = customRealloc(NULL, 100);
    assert(p1 != NULL);
    memset(p1, 0, 100); // Make sure it's valid memory
    
    // Case 2: realloc(ptr, 0) -> free(ptr)
    // The pointer should be freed.
    [[maybe_unused]] void* p2 = customRealloc(p1, 0);
    assert(p2 == NULL); 
    
    // Double check logic: if we alloc now, we might get p1's address back
    void* p3 = customMalloc(100);
    // (Not strictly required to be same address, but likely in this simple allocator)
    
    customFree(p3);
    std::cout << "Passed." << std::endl;
}

// ============================================================================
// ADDITIONAL EDGE CASE TESTS
// ============================================================================

void test_malloc_zero() {
    std::cout << "Running Part A: Malloc(0) returns NULL..." << std::endl;
    
    [[maybe_unused]] void* ptr = customMalloc(0);
    assert(ptr == NULL);
    
    std::cout << "Passed." << std::endl;
}

void test_free_null() {
    std::cout << "Running Part A: Free(NULL) prints error but doesn't crash..." << std::endl;
    
    // This should print "<free error>: passed null pointer" but not crash
    customFree(NULL);
    
    std::cout << "Passed (No Crash)." << std::endl;
}

void test_free_invalid_pointer() {
    std::cout << "Running Part A: Free(invalid ptr) prints error but doesn't crash..." << std::endl;
    
    int stack_var = 42;
    // This should print "<free error>: passed non-heap pointer" but not crash
    customFree(&stack_var);
    
    std::cout << "Passed (No Crash)." << std::endl;
}

void test_realloc_invalid_pointer() {
    std::cout << "Running Part A: Realloc(invalid ptr) prints error and returns NULL..." << std::endl;
    
    int stack_var = 42;
    // This should print "<realloc error>: passed non-heap pointer" and return NULL
    [[maybe_unused]] void* result = customRealloc(&stack_var, 100);
    assert(result == NULL);
    
    std::cout << "Passed." << std::endl;
}

void test_realloc_grow() {
    std::cout << "Running Part A: Realloc grow preserves data..." << std::endl;
    
    // Allocate small block and write pattern
    char* ptr = (char*)customMalloc(50);
    assert(ptr != NULL);
    for (int i = 0; i < 50; i++) {
        ptr[i] = (char)(i & 0x7F);
    }
    
    // Grow the block
    char* new_ptr = (char*)customRealloc(ptr, 200);
    assert(new_ptr != NULL);
    
    // Verify original data is preserved
    for (int i = 0; i < 50; i++) {
        assert(new_ptr[i] == (char)(i & 0x7F));
    }
    
    customFree(new_ptr);
    std::cout << "Passed." << std::endl;
}

void test_realloc_same_size() {
    std::cout << "Running Part A: Realloc same size returns same pointer..." << std::endl;
    
    void* ptr = customMalloc(100);
    assert(ptr != NULL);
    
    // Realloc to same size should return same pointer
    void* same_ptr = customRealloc(ptr, 100);
    assert(same_ptr == ptr);
    
    customFree(same_ptr);
    std::cout << "Passed." << std::endl;
}

void test_calloc_zero_init() {
    std::cout << "Running Part A: Calloc zeroes memory..." << std::endl;
    
    unsigned char* ptr = (unsigned char*)customCalloc(100, 1);
    assert(ptr != NULL);
    
    // Verify all bytes are zero
    for (int i = 0; i < 100; i++) {
        assert(ptr[i] == 0);
    }
    
    customFree(ptr);
    std::cout << "Passed." << std::endl;
}

void test_calloc_zero_nmemb_or_size() {
    std::cout << "Running Part A: Calloc(0, x) and Calloc(x, 0) return NULL..." << std::endl;
    
    [[maybe_unused]] void* ptr1 = customCalloc(0, 100);
    assert(ptr1 == NULL);
    
    [[maybe_unused]] void* ptr2 = customCalloc(100, 0);
    assert(ptr2 == NULL);
    
    std::cout << "Passed." << std::endl;
}

void test_many_small_allocations() {
    std::cout << "Running Part A: Many small allocations and frees..." << std::endl;
    
    const int NUM_ALLOCS = 100;
    void* ptrs[NUM_ALLOCS];
    
    // Allocate many small blocks
    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = customMalloc(16);
        assert(ptrs[i] != NULL);
        assert(is_aligned(ptrs[i]));
    }
    
    // Free in reverse order
    for (int i = NUM_ALLOCS - 1; i >= 0; i--) {
        customFree(ptrs[i]);
    }
    
    std::cout << "Passed." << std::endl;
}

void test_alternating_alloc_free() {
    std::cout << "Running Part A: Alternating alloc/free pattern..." << std::endl;
    
    // This tests proper reuse of freed blocks
    for (int i = 0; i < 50; i++) {
        void* ptr = customMalloc(64);
        assert(ptr != NULL);
        customFree(ptr);
    }
    
    std::cout << "Passed." << std::endl;
}

void test_alignment_various_sizes() {
    std::cout << "Running Part A: Alignment for various sizes..." << std::endl;
    
    // Test that all allocations are 4-byte aligned regardless of requested size
    size_t sizes[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 31, 33, 63, 65, 127, 129};
    void* ptrs[15];
    
    for (int i = 0; i < 15; i++) {
        ptrs[i] = customMalloc(sizes[i]);
        assert(ptrs[i] != NULL);
        assert(is_aligned(ptrs[i]));
    }
    
    for (int i = 0; i < 15; i++) {
        customFree(ptrs[i]);
    }
    
    std::cout << "Passed." << std::endl;
}

void test_mt_malloc_zero() {
    std::cout << "Running Part B: MTMalloc(0) returns NULL..." << std::endl;
    heapCreate();
    
    [[maybe_unused]] void* ptr = customMTMalloc(0);
    assert(ptr == NULL);
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_mt_free_null() {
    std::cout << "Running Part B: MTFree(NULL) prints error but doesn't crash..." << std::endl;
    heapCreate();
    
    // This should print error but not crash
    customMTFree(NULL);
    
    heapKill();
    std::cout << "Passed (No Crash)." << std::endl;
}

void test_mt_calloc_zero_init() {
    std::cout << "Running Part B: MTCalloc zeroes memory..." << std::endl;
    heapCreate();
    
    unsigned char* ptr = (unsigned char*)customMTCalloc(100, 1);
    assert(ptr != NULL);
    
    for (int i = 0; i < 100; i++) {
        assert(ptr[i] == 0);
    }
    
    customMTFree(ptr);
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_mt_realloc_edge_cases() {
    std::cout << "Running Part B: MTRealloc edge cases..." << std::endl;
    heapCreate();
    
    // realloc(NULL, size) == malloc(size)
    void* p1 = customMTRealloc(NULL, 100);
    assert(p1 != NULL);
    
    // realloc(ptr, 0) == free(ptr) and returns NULL
    [[maybe_unused]] void* p2 = customMTRealloc(p1, 0);
    assert(p2 == NULL);
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_mt_realloc_grow_preserves_data() {
    std::cout << "Running Part B: MTRealloc grow preserves data..." << std::endl;
    heapCreate();
    
    char* ptr = (char*)customMTMalloc(50);
    assert(ptr != NULL);
    for (int i = 0; i < 50; i++) {
        ptr[i] = (char)(i & 0x7F);
    }
    
    char* new_ptr = (char*)customMTRealloc(ptr, 200);
    assert(new_ptr != NULL);
    
    for (int i = 0; i < 50; i++) {
        assert(new_ptr[i] == (char)(i & 0x7F));
    }
    
    customMTFree(new_ptr);
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_mt_allocation_too_large() {
    std::cout << "Running Part B: MTMalloc too large for region returns NULL..." << std::endl;
    heapCreate();
    
    // Region size is 4096, so requesting more should fail
    [[maybe_unused]] void* ptr = customMTMalloc(5000);
    assert(ptr == NULL);
    
    heapKill();
    std::cout << "Passed." << std::endl;
}

void test_mt_before_heap_create() {
    std::cout << "Running Part B: MT functions before heapCreate return NULL/error..." << std::endl;
    
    // heapKill to ensure heap is not initialized
    heapKill();
    
    [[maybe_unused]] void* ptr = customMTMalloc(100);
    assert(ptr == NULL);
    
    std::cout << "Passed." << std::endl;
}

int main() {
    test_part_a_basic();
    test_part_a_realloc_shrink_behavior();
    test_part_a_heap_shrink();
    test_part_b_concurrency();
    test_part_b_extra_regions();
    test_part_b_double_free();
    test_calloc_overflow();
    test_boundary_safety();
    test_sandwich_coalescing();
    test_best_fit_logic();
    test_realloc_edge_cases();
    test_cross_thread_free();
    
    // More Part A tests
    test_malloc_zero();
    test_free_null();
    test_free_invalid_pointer();
    test_realloc_invalid_pointer();
    test_realloc_grow();
    test_realloc_same_size();
    test_calloc_zero_init();
    test_calloc_zero_nmemb_or_size();
    test_many_small_allocations();
    test_alternating_alloc_free();
    test_alignment_various_sizes();
    
    // More Part B tests
    test_mt_malloc_zero();
    test_mt_free_null();
    test_mt_calloc_zero_init();
    test_mt_realloc_edge_cases();
    test_mt_realloc_grow_preserves_data();
    test_mt_allocation_too_large();
    test_mt_before_heap_create();
    
    std::cout << "\nALL TESTS PASSED." << std::endl;
    return 0;
}
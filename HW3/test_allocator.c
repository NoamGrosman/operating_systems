#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "customAllocator.h"

/*=============================================================================
* Part A Tests - Single Threaded Allocator
=============================================================================*/

void test_part_a_basic_malloc() {
    printf("=== Test Part A: Basic malloc ===\n");
    
    // Test basic allocation
    int* arr = (int*)customMalloc(10 * sizeof(int));
    if (arr == NULL) {
        printf("FAIL: malloc returned NULL\n");
        return;
    }
    
    // Write to allocated memory
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 10;
    }
    
    // Verify values
    bool pass = true;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i * 10) {
            pass = false;
            break;
        }
    }
    
    printf("Basic malloc: %s\n", pass ? "PASS" : "FAIL");
    customFree(arr);
}

void test_part_a_calloc() {
    printf("=== Test Part A: Calloc ===\n");
    
    // Test calloc zeros memory
    int* arr = (int*)customCalloc(10, sizeof(int));
    if (arr == NULL) {
        printf("FAIL: calloc returned NULL\n");
        return;
    }
    
    bool pass = true;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != 0) {
            pass = false;
            break;
        }
    }
    
    printf("Calloc zeros memory: %s\n", pass ? "PASS" : "FAIL");
    customFree(arr);
}

void test_part_a_realloc() {
    printf("=== Test Part A: Realloc ===\n");
    
    // Allocate initial memory
    int* arr = (int*)customMalloc(5 * sizeof(int));
    for (int i = 0; i < 5; i++) {
        arr[i] = i + 100;
    }
    
    // Realloc to larger size
    arr = (int*)customRealloc(arr, 10 * sizeof(int));
    if (arr == NULL) {
        printf("FAIL: realloc returned NULL\n");
        return;
    }
    
    // Check original values preserved
    bool pass = true;
    for (int i = 0; i < 5; i++) {
        if (arr[i] != i + 100) {
            pass = false;
            break;
        }
    }
    
    printf("Realloc preserves data: %s\n", pass ? "PASS" : "FAIL");
    customFree(arr);
}

void test_part_a_best_fit() {
    printf("=== Test Part A: Best Fit Strategy ===\n");
    
    // Allocate three blocks
    void* a = customMalloc(100);  // Block A: 100 bytes
    void* b = customMalloc(200);  // Block B: 200 bytes
    void* c = customMalloc(50);   // Block C: 50 bytes
    
    // Free A and B (creates two free blocks)
    customFree(a);
    customFree(b);
    
    // Allocate 80 bytes - should use block A (104 bytes aligned) as best fit
    void* d = customMalloc(80);
    
    // d should be at the same location as a (best fit)
    bool pass = (d == a);
    printf("Best fit allocation: %s\n", pass ? "PASS" : "FAIL");
    
    customFree(c);
    customFree(d);
}

void test_part_a_coalesce() {
    printf("=== Test Part A: Coalesce Adjacent Free Blocks ===\n");
    
    // Allocate two adjacent blocks
    void* a = customMalloc(100);
    void* b = customMalloc(100);
    
    // Free both - they should coalesce
    customFree(a);
    customFree(b);
    
    // Now allocate a larger block that fits in the coalesced space
    void* c = customMalloc(180);
    
    // c should be at the same location as a (coalesced block)
    bool pass = (c == a);
    printf("Coalesce and reuse: %s\n", pass ? "PASS" : "FAIL");
    
    customFree(c);
}

/*=============================================================================
* Part B Tests - Multi-Threaded Allocator
=============================================================================*/

void test_part_b_basic_malloc() {
    printf("=== Test Part B: Basic MT malloc ===\n");
    
    int* arr = (int*)customMTMalloc(10 * sizeof(int));
    if (arr == NULL) {
        printf("FAIL: MTMalloc returned NULL\n");
        return;
    }
    
    // Write to allocated memory
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 10;
    }
    
    // Verify values
    bool pass = true;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != i * 10) {
            pass = false;
            break;
        }
    }
    
    printf("Basic MT malloc: %s\n", pass ? "PASS" : "FAIL");
    customMTFree(arr);
}

void test_part_b_calloc() {
    printf("=== Test Part B: MT Calloc ===\n");
    
    int* arr = (int*)customMTCalloc(10, sizeof(int));
    if (arr == NULL) {
        printf("FAIL: MTCalloc returned NULL\n");
        return;
    }
    
    bool pass = true;
    for (int i = 0; i < 10; i++) {
        if (arr[i] != 0) {
            pass = false;
            break;
        }
    }
    
    printf("MT Calloc zeros memory: %s\n", pass ? "PASS" : "FAIL");
    customMTFree(arr);
}

void test_part_b_realloc() {
    printf("=== Test Part B: MT Realloc ===\n");
    
    int* arr = (int*)customMTMalloc(5 * sizeof(int));
    for (int i = 0; i < 5; i++) {
        arr[i] = i + 200;
    }
    
    arr = (int*)customMTRealloc(arr, 10 * sizeof(int));
    if (arr == NULL) {
        printf("FAIL: MTRealloc returned NULL\n");
        return;
    }
    
    bool pass = true;
    for (int i = 0; i < 5; i++) {
        if (arr[i] != i + 200) {
            pass = false;
            break;
        }
    }
    
    printf("MT Realloc preserves data: %s\n", pass ? "PASS" : "FAIL");
    customMTFree(arr);
}

// Thread function for multi-threaded test
void* thread_alloc_func(void* arg) {
    int id = *(int*)arg;
    
    // Each thread allocates and frees memory
    for (int i = 0; i < 10; i++) {
        int* data = (int*)customMTMalloc(100);
        if (data) {
            data[0] = id * 1000 + i;
            customMTFree(data);
        }
    }
    
    return NULL;
}

void test_part_b_multithreaded() {
    printf("=== Test Part B: Multi-threaded allocation ===\n");
    
    pthread_t threads[8];
    int thread_ids[8];
    
    // Create 8 threads
    for (int i = 0; i < 8; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, thread_alloc_func, &thread_ids[i]);
    }
    
    // Wait for all threads
    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Multi-threaded test: PASS (no crashes)\n");
}

void test_part_b_round_robin() {
    printf("=== Test Part B: Round-robin allocation ===\n");
    
    // Allocate 8 small blocks - each should go to different region
    void* ptrs[8];
    for (int i = 0; i < 8; i++) {
        ptrs[i] = customMTMalloc(64);
        if (ptrs[i] == NULL) {
            printf("FAIL: Allocation %d failed\n", i);
            return;
        }
    }
    
    // All pointers should be different and spread across regions
    bool all_different = true;
    for (int i = 0; i < 8 && all_different; i++) {
        for (int j = i + 1; j < 8; j++) {
            if (ptrs[i] == ptrs[j]) {
                all_different = false;
                break;
            }
        }
    }
    
    printf("Round-robin allocations: %s\n", all_different ? "PASS" : "FAIL");
    
    // Free all
    for (int i = 0; i < 8; i++) {
        customMTFree(ptrs[i]);
    }
}

int main() {
    printf("\n========================================\n");
    printf("       PART A TESTS (Single Thread)     \n");
    printf("========================================\n\n");
    
    test_part_a_basic_malloc();
    test_part_a_calloc();
    test_part_a_realloc();
    test_part_a_best_fit();
    test_part_a_coalesce();
    
    printf("\n========================================\n");
    printf("       PART B TESTS (Multi-Thread)      \n");
    printf("========================================\n\n");
    
    heapCreate();
    
    test_part_b_basic_malloc();
    test_part_b_calloc();
    test_part_b_realloc();
    test_part_b_round_robin();
    test_part_b_multithreaded();
    
    heapKill();
    
    printf("\n========================================\n");
    printf("           ALL TESTS COMPLETE           \n");
    printf("========================================\n\n");
    
    return 0;
}

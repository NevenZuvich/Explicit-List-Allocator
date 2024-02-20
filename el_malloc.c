// el_malloc.c: implementation of explicit list allocator functions.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "el_malloc.h"

// Global control functions

// Global control variable for the allocator. Must be initialized in
// el_init().
el_ctl_t el_ctl = {};

// Create an initial block of memory for the heap using mmap(). Initialize the
// el_ctl data structure to point at this block. The initial size/position of
// the heap for the memory map are given in the symbols EL_HEAP_INITIAL_SIZE
// and EL_HEAP_START_ADDRESS. Initialize the lists in el_ctl to contain a
// single large block of available memory and no used blocks of memory.
int el_init() {
    void *heap = mmap(EL_HEAP_START_ADDRESS, EL_HEAP_INITIAL_SIZE,
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(heap == EL_HEAP_START_ADDRESS);

    el_ctl.heap_bytes = EL_HEAP_INITIAL_SIZE; // make the heap as big as possible to begin with
    el_ctl.heap_start = heap; // set addresses of start and end of heap
    el_ctl.heap_end = PTR_PLUS_BYTES(heap, el_ctl.heap_bytes);

    if (el_ctl.heap_bytes < EL_BLOCK_OVERHEAD) {
        fprintf(stderr,"el_init: heap size %ld to small for a block overhead %ld\n",
                el_ctl.heap_bytes,EL_BLOCK_OVERHEAD);
        return -1;
    }

    el_init_blocklist(&el_ctl.avail_actual);
    el_init_blocklist(&el_ctl.used_actual);
    el_ctl.avail = &el_ctl.avail_actual;
    el_ctl.used = &el_ctl.used_actual;

    // establish the first available block by filling in size in
    // block/foot and null links in head
    size_t size = el_ctl.heap_bytes - EL_BLOCK_OVERHEAD;
    el_blockhead_t *ablock = el_ctl.heap_start;
    ablock->size = size;
    ablock->state = EL_AVAILABLE;
    el_blockfoot_t *afoot = el_get_footer(ablock);
    afoot->size = size;
    el_add_block_front(el_ctl.avail, ablock);
    return 0;
}

// Clean up the heap area associated with the system
void el_cleanup() {
    munmap(el_ctl.heap_start, el_ctl.heap_bytes);
    el_ctl.heap_start = NULL;
    el_ctl.heap_end = NULL;
}

// Pointer arithmetic functions to access adjacent headers/footers

// Compute the address of the foot for the given head which is at a higher
// address than the head.
el_blockfoot_t *el_get_footer(el_blockhead_t *head) {
    size_t size = head->size;
    el_blockfoot_t *foot = PTR_PLUS_BYTES(head, sizeof(el_blockhead_t) + size);
    return foot;
}

// TODO
// Compute the address of the head for the given foot, which is at a
// lower address than the foot.
el_blockhead_t *el_get_header(el_blockfoot_t *foot) {
    size_t size = foot->size;
    return PTR_MINUS_BYTES(foot, sizeof(el_blockhead_t) + size);
}

// Return a pointer to the block that is one block higher in memory
// from the given block. This should be the size of the block plus
// the EL_BLOCK_OVERHEAD which is the space occupied by the header and
// footer. Returns NULL if the block above would be off the heap.
// DOES NOT follow next pointer, looks in adjacent memory.
el_blockhead_t *el_block_above(el_blockhead_t *block) {
    el_blockhead_t *higher = PTR_PLUS_BYTES(block, block->size + EL_BLOCK_OVERHEAD);
    if ((void *) higher >= (void*) el_ctl.heap_end) {
        return NULL;
    } else {
        return higher;
    }
}

// Return a pointer to the block that is one block lower in memory
// from the given block. Uses the size of the preceding block found
// in its foot. DOES NOT follow block->next pointer, looks in adjacent
// memory. Returns NULL if the block below would be outside the heap.
//
// WARNING: This function must perform slightly different arithmetic
// than el_block_above(). Take care when implementing it.
el_blockhead_t *el_block_below(el_blockhead_t *block) {
    // gets a pointer to the footer for block
    el_blockfoot_t *below_footer = PTR_MINUS_BYTES(block, sizeof(el_blockfoot_t));
    if ((void *) below_footer >= el_ctl.heap_start) { // if above start of heap
        // compute header
        el_blockhead_t *below_header = el_get_header(below_footer);
        return below_header; // return said header
    } else  // not in valid memory (out of memory range)
        return NULL; // return null correspondingly
}

// Block list operations

// Print an entire blocklist. The format appears as follows.
//
// {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
//
// Note that the '@' column uses the actual address of items which
// relies on a consistent mmap() starting point for the heap.
void el_print_blocklist(el_blocklist_t *list) {
    printf("{length: %3lu  bytes: %5lu}\n", list->length, list->bytes);
    el_blockhead_t *block = list->beg;
    for (int i=0 ; i < list->length; i++) {
        printf("  ");
        block = block->next;
        printf("[%3d] head @ %p ", i, block);
        printf("{state: %c  size: %5lu}\n", block->state, block->size);
        el_blockfoot_t *foot = el_get_footer(block);
        printf("%6s", "");          // indent
        printf("  foot @ %p ", foot);
        printf("{size: %5lu}", foot->size);
        printf("\n");
    }
}

// Print out basic heap statistics. This shows total heap info along
// with the Available and Used Lists. The output format resembles the following.
//
// HEAP STATS (overhead per node: 40)
// heap_start:  0x600000000000
// heap_end:    0x600000001000
// total_bytes: 4096
// AVAILABLE LIST: {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
// USED LIST: {length:   3  bytes:   696}
//   [  0] head @ 0x600000000200 {state: u  size:   312}
//         foot @ 0x600000000358 {size:   312}
//   [  1] head @ 0x600000000198 {state: u  size:    64}
//         foot @ 0x6000000001f8 {size:    64}
//   [  2] head @ 0x6000000000a8 {state: u  size:   200}
//         foot @ 0x600000000190 {size:   200}
void el_print_stats() {
    printf("HEAP STATS (overhead per node: %lu)\n", EL_BLOCK_OVERHEAD);
    printf("heap_start:  %p\n", el_ctl.heap_start);
    printf("heap_end:    %p\n", el_ctl.heap_end);
    printf("total_bytes: %lu\n", el_ctl.heap_bytes);
    printf("AVAILABLE LIST: ");
    el_print_blocklist(el_ctl.avail);
    printf("USED LIST: ");
    el_print_blocklist(el_ctl.used);
}

// Initialize the specified list to be empty. Sets the beg/end
// pointers to the actual space and initializes those data to be the
// ends of the list. Initializes length and size to 0.
void el_init_blocklist(el_blocklist_t *list) {
    list->beg = &(list->beg_actual);
    list->beg->state = EL_BEGIN_BLOCK;
    list->beg->size = EL_UNINITIALIZED;
    list->end = &(list->end_actual);
    list->end->state = EL_END_BLOCK;
    list->end->size = EL_UNINITIALIZED;
    list->beg->next = list->end;
    list->beg->prev = NULL;
    list->end->next = NULL;
    list->end->prev = list->beg;
    list->length = 0;
    list->bytes = 0;
}

// Add to the front of list; links for block are adjusted as are links
// within list. Length is incremented and the bytes for the list are
// updated to include the new block's size and its overhead.
void el_add_block_front(el_blocklist_t *list, el_blockhead_t *block) {
    // insert block into the front of the list
    block->prev = list->beg;        // sets block's prev appropriately
    block->next = list->beg->next;  // sets block's next appropriately
    block->prev->next = block;      // sets list's beginning's next to block
    block->next->prev = block;      // sets old front's previous to block
    (list->length)++;               // increments length of list
    // increases number of bytes apropriately 
    list->bytes += EL_BLOCK_OVERHEAD + block->size; 
}

// Unlink block from the specified list.
// Updates the length and bytes for that list including
// the EL_BLOCK_OVERHEAD bytes associated with header/footer.
void el_remove_block(el_blocklist_t *list, el_blockhead_t *block) {
    // unlinks block from chain by properly reseting other node's next/prev
    if (list != NULL && block != NULL && 
        block->prev != NULL && block->next != NULL) {
        // first ensures no NULL args are pass and no unconnected blocks are passed

        // then does the unlinking
        block->prev->next = block->next;
        block->next->prev = block->prev;

        // sets next and prev fields correspondingly (now block is unconected)
        block->prev = NULL; block->next = NULL;

        (list->length)--;   // decrements list size
        list->bytes = list->bytes - (EL_BLOCK_OVERHEAD + block->size); // substracts total bytes of block arg
    }
}

// Allocation-related functions

// Find the first block in the available list with block size of at
// least (size + EL_BLOCK_OVERHEAD). Overhead is accounted for so this
// routine may be used to find an available block to split: splitting
// requires adding in a new header/footer. Returns a pointer to the
// found block or NULL if no of sufficient size is available.
el_blockhead_t *el_find_first_avail(size_t size) {
    // gets first non-dummy node
    el_blockhead_t *cur_node = el_ctl.avail->beg->next; 
    while (cur_node != NULL) {
        if (cur_node->size >= size + EL_BLOCK_OVERHEAD) 
            // if block is big enough
            return cur_node;    // return this block
        else // if not
            cur_node = cur_node->next; // iterate forward through list
    }
    return NULL;    // if no available block found, return NULL
}

// Set the pointed to block to the given size and add a footer to it. Creates
// another block above it by creating a new header and assigning it the
// remaining space. Ensures that the new block has a footer with the correct
// size. Returns a pointer to the newly created block while the parameter block
// has its size altered to parameter size. Does not do any linking of blocks.
// If the parameter block does not have sufficient size for a split (at least
// new_size + EL_BLOCK_OVERHEAD for the new header/footer) makes no changes and
// returns NULL.
el_blockhead_t *el_split_block(el_blockhead_t *block, size_t new_size) {
    if (new_size + EL_BLOCK_OVERHEAD < block->size) {
        size_t orig_size = block->size; // stores old size for later

        block->size = new_size; // changes header size correctly
        el_blockfoot_t new_foot = {new_size}; // makes foot with correct size
        // block has had its size updated, can use el_get_footer()
        // gets correct address for new foot
        el_blockfoot_t *new_foot_ptr = el_get_footer(block); 
        *new_foot_ptr = new_foot; // sets said affress to the new foot struct

        // computes new (higher) block's size
        size_t new_block_size = orig_size - new_size - EL_BLOCK_OVERHEAD;
        el_blockhead_t new_head; // declare new head
        new_head.size = new_block_size; // initializes properly
        new_head.state = EL_AVAILABLE;
        el_blockhead_t *new_head_ptr = PTR_PLUS_BYTES(new_foot_ptr, sizeof(el_blockfoot_t)); // computes address for new head 
        *new_head_ptr = new_head; // puts new_head at its proper address
        
        // new_head initialized with true size, can use el_get_footer()
        // get pointer for the old foot
        el_blockfoot_t *first_foot = el_get_footer(new_head_ptr); 
        first_foot->size = new_block_size; // change foot's size properly
        return new_head_ptr; // return newly created (higher) block
    } else
        return NULL;
}

// Return pointer to a block of memory with at least the given size
// for use by the user. The pointer returned is to the usable space,
// not the block header. Makes use of find_first_avail() to find a
// suitable block and el_split_block() to split it. Returns NULL if
// no space is available.
void *el_malloc(size_t nbytes) {
    el_blockhead_t *avail_block = el_find_first_avail(nbytes);
    if (avail_block != NULL) { // if there is available space
        // stores the upper (available) split block into upper_split
        el_remove_block(el_ctl.avail, avail_block); // 1st remove unsplit block
        // then split the block
        el_blockhead_t *upper_split = el_split_block(avail_block, nbytes);
        // make sure to set the upper of the two split blocks to available
        if (upper_split != NULL) { 
            // ensures not return NULL to avoid trying to access fields of NULL
            // This would mean a block of perfect size was found, leading to no
            // upper of the two split blocks being made and NULL being returned
            // if and only if an upper block was returned
            upper_split->state = EL_AVAILABLE; // make said block available 
            el_add_block_front(el_ctl.avail, upper_split); // and add that to avail
        }
        avail_block->state = EL_USED;   // sets state to used
        el_add_block_front(el_ctl.used, avail_block);  // adds to front of used block
        return PTR_PLUS_BYTES(avail_block, sizeof(el_blockhead_t)); // returns actually free memory;
    } else { // no available space
        return NULL;
    }
}

// De-allocation/free() related functions

// Attempt to merge the block 'lower' with the next block in memory. Does
// nothing if lower is NULL or not EL_AVAILABLE and does nothing if the next
// higher block is NULL (because lower is the last block) or not EL_AVAILABLE.
//
// Otherwise, locates the next block with el_block_above() and merges these two
// into a single block. Adjusts the fields of lower to incorporate the size of
// higher block and the reclaimed overhead. Adjusts footer of higher to
// indicate the two blocks are merged. Removes both lower and higher from the
// available list and re-adds lower to the front of the available list.
void el_merge_block_with_above(el_blockhead_t *lower) {
    if (lower != NULL && lower->state == EL_AVAILABLE &&
        el_block_above(lower) != NULL && 
        el_block_above(lower)->state == EL_AVAILABLE) { 
            // if lower and higher blocks are not null and are available

            el_blocklist_t *avail_list = el_ctl.avail;

            // get higher block
            el_blockhead_t *higher = el_block_above(lower);

            // remove both higher and lower from available list
            el_remove_block(avail_list, lower);
            el_remove_block(avail_list, higher);

            // compute new size
            size_t new_size = higher->size + lower->size + EL_BLOCK_OVERHEAD;
            lower->size = new_size; // updates header size
            el_blockfoot_t *foot = el_get_footer(lower); // gets footer of merged
            foot->size = new_size; // updates size field for the footer
            // re-add merged block to available list
            el_add_block_front(avail_list, lower); 
    }
}

// Free the block pointed to by the given ptr. The area immediately
// preceding the pointer should contain an el_blockhead_t with information
// on the block size. Attempts to merge the free'd block with adjacent
// blocks using el_merge_block_with_above().
void el_free(void *ptr) {
    // gets header of the block ptr's user memory belongs to
    el_blockhead_t *header = PTR_MINUS_BYTES(ptr, sizeof(el_blockhead_t));
    header->state = EL_AVAILABLE; // set block to available
    el_remove_block(el_ctl.used, header); // removes block from list

    el_blockhead_t *block_above = el_block_above(header);
    el_merge_block_with_above(header); // attempts to merge block with above block
    el_blockhead_t *block_below = el_block_below(header);
    el_merge_block_with_above(block_below); // attempts to merge block with below block

    
    if ((block_below == NULL || block_below->state == EL_USED) &&
        (block_above == NULL || block_above->state == EL_USED)) 
        // if no merging occured (both are above and below are either NULL or used)
        // then and only then add to avail list; otherwise merging does so
        el_add_block_front(el_ctl.avail, header); 
}

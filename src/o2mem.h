/* o2mem.h -- real-time allocation, non-blocking queues
 *
 * Roger B. Dannenberg
 * April 2020
 */

/* Free memory will consist of a bunch of free lists organized by size.
   Allocate by removing from head of list. Free by pushing back on list.
   
   If a list is empty, add to it from a large block of memory.
   
   To support larger object sizes, there are two free lists: a linear
   list with blocks in increments of 8, and an exponential list with
   block sizes that are powers of 2.
   
   The linear list block sizes go up to MAX_LINEAR_BYTES.

   When the current large block of memory (called chunk) runs out of
   space, and IF allocation is enabled (the malloc_ok variable, set
   through the mallocp parameter passed to o2_memory()), then malloc
   another chunk if the needed memory size is < 512. For bigger blocks,
   instead of allocating another chunk (which might waste what's left
   in the current chunk), directly allocate the needed block with malloc.
   This block becomes a chunk with a single object allocated from it.

   All chunks are kept on a global list in order to free all O2 memory
   back to to the system if/when O2 is shut down.
 */

#define O2MEM_H

#ifdef __cplusplus
extern "C" {
#endif


void o2_mem_init(char *first_chunk, int64_t size);

#ifdef __cplusplus
}
#endif


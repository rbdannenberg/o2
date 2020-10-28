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
 */

#define O2MEM_H

#ifdef __cplusplus
extern "C" {
#endif


void o2_mem_init(char *first_chunk, int64_t size);

#ifdef __cplusplus
}
#endif


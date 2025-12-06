#include <iostream>
#include <vector>
#include <mutex>
using namespace std;

// alignup to (int n)*align
inline size_t AlignUp(size_t size, size_t align) 
{
    if (size % align != 0) return (size / align + 1) * align;
    else return (size / align) * align;
}
class MemoryPool
{
    private:
        struct FreeBlock
        {
            FreeBlock *next; // point to the next FreeBlock
        };
        void Preallocate(size_t num_blocks); // preallocate num_blocks chunks
        size_t block_size_; // block size after alignment
        size_t align_; // rule of alignment
        FreeBlock *free_list_; // free block list head
        vector<void*> pools_; // record all large memory chunk in preallocation (for free)
        mutex mutex_; // Thread-safe lock
    public:
        explicit MemoryPool(size_t block_size, size_t align = 8, size_t prealloc = 1024)
        : align_(align), free_list_(nullptr)
        {
            // assign the first batch chunks
            if(prealloc > 0) Preallocate(prealloc);
        }
        ~MemoryPool();
        void *Allocate() // allocate memory chunk
        {
            size_t expand_num;
            if(!free_list_)
            {
                if(pools_.empty()) expand_num = 1024;
                else expand_num = pools_.size() * 1024;
                Preallocate(expand_num);
            }
            // head for empty linked list
            FreeBlock *block = free_list_;
            free_list_ = block->next;
            return block;
        }
        void Free(void *ptr); // free memory chunk
};

int main()
{
    return 0;
}

/*
1. Preallocation: preallocation + enlarge dynamically have been done? ╰(⇀‸↼)╯
    Initialization, request a continuous memory from the os (size = sizeof chunk * num of chunks)
    Split it into many block_size_ small pieces, using FreeBlock to connect
    FreeBlock == empty, enlarge dynamically
2. Memory Chunk Management:
    Allocation: 
        Lock if Free List not null, 
        take the head block, 
        Update Free List head, 
        Unlock, 
        Return block address

    Deallocation: 
        Lock, 
        Set released chunk as new head, pointing to the original Free List,
        Update Free List head,
        Unlock

3. Multithreading: 
    Single: no mutex, free_list_ max efficiency
    Low Concurrency: mutex to protect Allocation / Free 
    High Concurrency: Three-Level Cache 
        Thread Cache Lock-Free->
        Central Cache Fine-Grained Locking->
        Page Cache Global Locking: 
        no lock contention.
*/
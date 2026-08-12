#ifndef MEMPOOL_HPP
#define MEMPOOL_HPP

#include <cstddef>
#include <mutex>
#include <vector>

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
        FreeBlock *next;
    };

public:
    explicit MemoryPool(size_t block_size, size_t align = 8, size_t prealloc = 1024);
    ~MemoryPool();

    void *Allocate();
    void Free(void *ptr);

private:
    void Preallocate(size_t num_blocks);

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

private:
    size_t block_size_;
    size_t align_;
    FreeBlock *free_list_;
    std::vector<void*> pools_;
    std::mutex mutex_;
};

class UserObject 
{
public:
    static MemoryPool& GetPool();

    void *operator new(size_t size);
    void operator delete(void *ptr, size_t size);

    int id_;
    char name_[16];
};
#endif
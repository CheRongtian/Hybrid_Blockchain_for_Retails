#include "mempool.hpp"
#include <iostream>
#include <cstdlib>
#include <algorithm>


MemoryPool::MemoryPool(size_t block_size, size_t align, size_t prealloc)
    : align_(align), free_list_(nullptr) {
    size_t min_size = std::max(block_size, sizeof(FreeBlock*));
    block_size_ = AlignUp(min_size, align_);
    if(prealloc > 0) Preallocate(prealloc);
}

MemoryPool::~MemoryPool() 
{
    for(void *pool: pools_) {
        std::free(pool);
    }
}

void *MemoryPool::Allocate() 
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!free_list_) 
    {
        size_t expand_num = pools_.empty() ? 1024 : pools_.size() * 1024;
        Preallocate(expand_num);
    }
    FreeBlock *block = free_list_;
    free_list_ = block->next;
    return block;
}

void MemoryPool::Free(void *ptr) 
{
    if(!ptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    FreeBlock *block = static_cast<FreeBlock*>(ptr);
    block->next = free_list_;
    free_list_ = block;
}

void MemoryPool::Preallocate(size_t num_blocks) 
{
    size_t total_size = num_blocks * block_size_;
    void* pool = std::malloc(total_size);
    if(!pool) throw std::bad_alloc();

    pools_.push_back(pool);
    FreeBlock *start = static_cast<FreeBlock*>(pool);
    FreeBlock *current = start;
    for(size_t i = 0; i < num_blocks - 1; i++) 
    {
        current->next = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(current) + block_size_);
        current = current->next;
    }
    current->next = nullptr;

    if(free_list_) current->next = free_list_;
    free_list_ = start;
    
    /* too many if you want to do the test :)
    std::cout << "Preallocated " << num_blocks << " blocks(size: " << block_size_
              << "B), total pools: " << pools_.size() << std::endl;
    */
    
}

MemoryPool& UserObject::GetPool() 
{
    static MemoryPool *pool = new MemoryPool(sizeof(UserObject), 8, 1024);
    return *pool;
}

void *UserObject::operator new(size_t size) 
{
    if(size != sizeof(UserObject)) return ::operator new(size);
    return GetPool().Allocate();
}

void UserObject::operator delete(void *ptr, size_t size) 
{
    if(!ptr) return;
    if(size != sizeof(UserObject)) {
        ::operator delete(ptr);
        return;
    }
    GetPool().Free(ptr);
}
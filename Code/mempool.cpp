#include <iostream>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <algorithm>
#include <cstddef>

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
    public:
        explicit MemoryPool(size_t block_size, size_t align = 8, size_t prealloc = 1024)
        : align_(align), free_list_(nullptr)
        {
            block_size_=AlignUp(block_size, align_);
            // assign the first batch chunks
            if(prealloc > 0) Preallocate(prealloc);
        }
        ~MemoryPool()
        {
            for(void *pool: pools_)
            {
                std::free(pool);
            }
        }
        void *Allocate() // allocate memory chunk
        {
            std::lock_guard<std::mutex> lock(mutex_); // for safety
            if(!free_list_)
            {
                size_t expand_num=pools_.empty()?1024:pools_.size()*1024;
                Preallocate(expand_num);
            }
            // head for empty linked list
            FreeBlock *block = free_list_;
            free_list_ = block->next;
            return block;
        }
        void Free(void *ptr) // free memory chunk
        {
            if(!ptr) return;
            std::lock_guard<std::mutex>lock(mutex_); // safety for threads
            // insert free chunk into the head of a free linked list
            FreeBlock *block = static_cast<FreeBlock*>(ptr);
            block->next = free_list_;
            free_list_ = block;
        }
    private:
        void Preallocate(size_t num_blocks) // preallocate num_blocks chunks
        {
            // calculate the total memory
            size_t total_size = num_blocks * block_size_;
            // ask for sequential memory
            void* pool = std::malloc(total_size);
            if(!pool) throw std::bad_alloc(); // if failed

            // record big chunks
            pools_.push_back(pool);
            // make sequential memory num_block chunks, creating linked list
            FreeBlock *start = static_cast<FreeBlock*>(pool);
            FreeBlock *current = start;
            for(size_t i=0; i<num_blocks-1; i++)
            {
                current->next=reinterpret_cast<FreeBlock*>
                    (reinterpret_cast<char*>(current)+block_size_);
                current=current->next;
            }
            current->next=nullptr;

            // new linked list connected with free ll
            if(free_list_) current->next=free_list_;
            free_list_=start;
            std::cout<<"Preallocated"<<num_blocks<<" blocks(size: "<<block_size_
                     <<"B), total pools: "<<pools_.size()<<std::endl;
        }
        MemoryPool(const MemoryPool&) = delete;
        MemoryPool&operator=(const MemoryPool&) = delete;
    
    private:
        size_t block_size_; // block size after alignment
        size_t align_; // rule of alignment
        FreeBlock *free_list_; // free block list head
        std::vector<void*> pools_; // record all large memory chunk in preallocation (for free)
        std::mutex mutex_; // Thread-safe lock
};

class UserObject
{
    public:
        // single example mempool: chunk size = sizeof(UserObj), align with 8B, preallocate 1024 chunks
        static MemoryPool&GetPool()
        {
            static MemoryPool pool(sizeof(UserObject), 8, 1024);
            return pool;
        }
        // overload operator new
        void *operator new(size_t size)
        {
            if(size!=sizeof(UserObject)) return std::malloc(size);
            return GetPool().Allocate();
        }
        //overload delete
        void operator delete(void *ptr)
        {
            if(!ptr) return;
            GetPool().Free(ptr);
        }

        // for test
        int id_;
        char name_[16];
};
int main()
{
    // sample test code here
    UserObject *obj1 = new UserObject();
    UserObject *obj2 = new UserObject();
    UserObject *obj3 = new UserObject();

    obj1->id_=1; obj2->id_=2; obj3->id_=3;

    std::cout<<"obj1: "<<obj1<<"(id: "<<obj1->id_<<")"<<std::endl;
    std::cout<<"obj2: "<<obj2<<"(id: "<<obj2->id_<<")"<<std::endl;
    std::cout<<"obj3: "<<obj3<<"(id: "<<obj3->id_<<")"<<std::endl;

    delete obj1; delete obj2; delete obj3;

    std::vector<UserObject *> ojbs;
    ojbs.reserve(2000);
    for(int i=0; i<2000; i++)
    {
        ojbs.push_back(new UserObject());
        ojbs.back()->id_=i;
    }
    std::cout<<"Allocate 2000 UserObject (triggered expansion)"<<std::endl;
    for(auto &ojb:ojbs) delete ojb;
    return 0;
}

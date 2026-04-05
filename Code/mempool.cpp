#include <iostream>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>
#ifdef
_WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// alignup to (int n)*align
inline size_t AlignUp(size_t size, size_t align) 
{
    if (size % align != 0) return (size / align + 1) * align;
    else return (size / align) * align;
}

struct Span
{
    size_t page_id;
    size_t page_count;
    size_t block_size;
    size_t free_count;
    void *free_list;
    Span *prev;
    Span *next;
    bool is_used;
    Span() : page_id(0), page_count(0), block_size(0), free_count(0), free_list(nullptr),
             prev(nullptr), next(nullptr), is_used(false){}
};

// SpanList manage the same size level Span(double linked list)
class SpanList
{
    public:
        SpanList()
        {
            head_ = new Span();
            tail_ = new Span();
            head_->next=tail_;
            tail_->prev=head_;
        }

        ~SpanList()
        {
            Span *cur = head_;
            while(cur)
            {
                Span *next = cur->next;
                delete cur;
                cur = next;
            }
        }

        void PushBack(Span *span)
        {
            span->prev = tail_->prev;
            span->next = tail_;
            tail_->prev->next = span;
            tail_->prev = span;
        }
        Span *PopFront()
        {
            if(head_->next == tail_) return nullptr;
            Span *span = head_->next;
            RemoveSpan(span);
            return span;
        }
        void RemoveSpan(Span *span)
        {
            span->prev->next = span->next;
            span->next->prev = span->prev;
            span->prev = nullptr;
            span->next = nullptr;
        }
        
        bool Empty() const
        {
            return head_->next==tail_;
        }
    private:
        Span *head_;
        Span *tail_;

};

class PageCache
{
    private:
        static const size_t kMaxPageCount = 128;
        SpanList span_lists_[kMaxPageCount + 1];
        std::mutex mutex_;
        size_t page_size_;
        std::unordered_map<size_t, Span*> page_id_to_span_;
        PageCache()
        {
            #ifdef
            _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            page_size_ = si.dwPageSize;
            #else
            page_size_=sysconf(_SC_PAGESIZE);
            #endif
        }
        PageCache(const PageCache&)=delete;
        PageCache& operator=(const PageCache&)=delete;
    
    public:
        static PageCache& GetInstance()
        {
            static PageCache instance;
            return instance;
        }
        
        Span *AllocatePages(size_t n)
        {
            if(n==0||n>kMaxPageCount) throw std::bad_alloc();
            std::lock_guard<std::mutex> lock(mutex_);
            if(!span_lists_[n].Empty())
            {
                Span *span = span_lists_[n].PopFront();
                for(size_t i=0; i<span->page_count; ++i)
                    page_id_to_span_[span->page_id + i] = span;
                span->is_used=true;
                return span;
            }

            for(size_t i=n+1; i<=kMaxPageCount; ++i)
            {
                if(!span_lists_[i].Empty())
                {
                    Span *big_span = span_lists_[i].PopFront();
                    Span *small_span = new Span();
                    small_span->page_id = big_span->page_id;
                    small_span->page_count = n;
                    small_span->is_used = true;

                    big_span->page_id += n;
                    big_span->page_count -= n;
                    span_lists_[big_span->page_count].PushBack(big_span);

                    for(size_t j=0; j<small_span->page_id+j; ++j)
                        page_id_to_span_[small_span->page_id+j] = small_span;
                    return small_span;
                }
            }

            size_t total_size = n * page_size_;
            void *ptr = SystemAllocate(n);
            if(!ptr) throw std::bad_alloc();

            Span *span = new Span();
            span->page_id = reinterpret_cast<size_t>(ptr)/page_size_;
            span->page_count = n;
            span->is_used = true;

            for(size_t i=0; i<n; ++i) page_id_to_span_[span->page_id+i]=span;
            return span;
        }

        void FreePages(Span *span)
        {
            if(!span||!span->is_used) return;
            std::lock_guard<std::mutex> lock(mutex_);
            span->is_used=false;
            span->free_count=0;
            span->free_list=nullptr;
            
            MergeSpan(span);

            span_lists_[span->page_count].PushBack(span);

            for(size_t i=0; i<span->page_count;++i) page_id_to_span_.erase(span->page_id+i);
        }
        
        Span *FindSpanByAddr(void *addr)
        {
            if(!addr) return nullptr;
            size_t page_id = reinterpret_cast<size_t>(addr)/page_size_;
            std::lock_guard<std::mutex>lock(mutex_);
            auto it=page_id_to_span_.find(page_id);
            return it!=page_id_to_span_.end()?it->second:nullptr;
        }

        size_t GetPageSize()const
        {
            return page_size_;
        }
    private:
        void MergeSpan(Span *span);
        void *SystemAllocate(size_t n)
        {
            size_t total_size = n * page_size_;
            #ifdef
            _WIN32
                return VirtualAlloc(nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            #else
                return mmap(nullptr, total_size, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANOYMOUS, -1, 0);
            #endif
        }

        void PageCache::SystemFree(void *ptr, size_t n)
        {
            size_t total_size = n * page_size_;
            #ifdef
            _WIN32
                VirtualFree(ptr, 0, MEM_RELEASE);
            #else
                munmap(ptr, total_size);
            #endif
        }
        
};

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

class ThreadCache
{
    private:
        // free linked list node
        struct FreeListNode
        {
            FreeListNode *next;
        };
        static const size_t kMaxSmallObjSize = 4096;
        static const size_t kAlign = 8;
        static const size_t kFreeListCount = kMaxSmallObjSize / kAlign;

        // free linked list array, every index corresponds to a sized block
        FreeListNode *free_lists_[kFreeListCount] = {nullptr};
    
    public:
        void *Allocate(size_t size);
        void Free(void* ptr, size_t size);
    
    private:
        // calculate the size
        size_t SizeToIndex(size_t size) const
        {
            return (AlignUp(size, kAlign)/kAlign)-1;
        }

        // ask central cache for batch blocks(batch_num decreases)
        void FetchFromCentralCache(size_t index, size_t size);
        // calculate the batch_num(small obj needs more than big obj)
        size_t CalcBatchNum(size_t size) const
        {
            if(size<=64) return 32;
            if(size<=256) return 16;
            if(size<=1024) return 8;
            return 4;
        }
};

thread_local ThreadCache tls_thread_cache;


class CentralCache
{
    private:
        static const size_t kAlign = 8;
        static const size_t kMaxSmallObjSize = 4096;
        static const size_t kFreeListCount = kMaxSmallObjSize / kAlign;
        SpanList span_lists_[kFreeListCount];
        std::mutex mutexes_[kFreeListCount];
        CentralCache()=default;
        CentralCache(const CentralCache&)=delete;
        CentralCache& operator=(const CentralCache&)=delete;
    
    public:
        static CentralCache& GetInstance()
        {
            static CentralCache instance;
            return instance;
        }
        void *AllocateBatch(size_t size, size_t batch_num);
        void FreeBatch(size_t size, void *start, size_t count);
    
    private:
        size_t SizeToIndex(size_t size) const
        {
            return (AlignUp(size, kAlign)/kAlign)-1;
        }

        // ask n page memory from Page Cache, creating Span
        Span *FetchFromPageCache(size_t page_count);
        // splits span into small pieces, creating free linked list
        void *SplitSpan(Span *span, size_t size, size_t batch_num);
};


class ICentralCache
{
    public:
        virtual ~ICentralCache()=default;
        virtual void *Allocate(size_t size, size_t batch_num)=0;
        virtual void FreeBatch(size_t size, void *start, size_t count)=0;
};

class CentralCache : public ICentralCache
{

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

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
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif
#include <cstring>

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

class SpanPool
{
    void* free_list_=nullptr;
    std::mutex mutex_;
    public:
        static SpanPool& GetInstance()
        {
            static SpanPool instance;
            return instance;
        }

        Span *New()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!free_list_)
            {
                size_t alloc_size = 128 * 1024;
                #ifdef _WIN32
                    void* ptr = VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                #else
                    void* ptr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                #endif
                char *start=(char*)ptr;
                size_t obj_count = alloc_size / sizeof(Span);
                for(size_t i=0; i<obj_count-1; ++i)
                    *(void**)(start+i*sizeof(Span)) = start + (i+1)*sizeof(Span);
                *(void**)(start + (obj_count-1)*sizeof(Span))=nullptr;
                free_list_=start;
            }
            void *obj=free_list_;
            free_list_ = *(void**)obj;
            return new(obj) Span();
        }

        void Delete(Span *span)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            span->~Span();
            *(void**)span = free_list_;
            free_list_ = span;
        }
};

// SpanList manage the same size level Span(double linked list)
class SpanList
{
    public:
        SpanList()
        {
            head_ = SpanPool::GetInstance().New();
            tail_ = SpanPool::GetInstance().New();
            head_->next=tail_;
            tail_->prev=head_;
        }
        
        Span* Begin() 
        { 
            return head_->next; 
        }
        
        Span* End() 
        { 
            return tail_; 
        }

        ~SpanList()
        {
            Span *cur = head_;
            while(cur)
            {
                Span *next = cur->next;
                SpanPool::GetInstance().Delete(cur);
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

template <int BITS>
class PageMap2
{
    private:
        static const int kLeafBits = 15;
        static const int kLeafLength = 1 << kLeafBits;
        static const int kRootBits = BITS - kLeafBits;
        static const int kRootLength = 1 << kLeafBits;
        Span** root_[kRootLength];
    
    public:
        PageMap2()
        {
            memset(root_, 0, sizeof(root_));
        }

        Span* get(size_t k) const
        {
            size_t i1 = k >> kLeafBits;
            size_t i2 = k & (kLeafLength - 1);
            if(i1 >= kRootLength || !root_[i1]) return nullptr;
            return root_[i1][i2];
        }

        void set(size_t k, Span *v)
        {
            size_t i1 = k >> kLeafBits;
            size_t i2 = k & (kLeafLength - 1);
            if(!root_[i1])
            {
                #ifdef _WIN32
                    void *ptr = VirtualAlloc(nullptr, kLeafLength * sizeof(Span *), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                #else
                    void *ptr = mmap(nullptr, kLeafLength * sizeof(Span *), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                #endif
                memset(ptr, 0, kLeafLength * sizeof(Span*));
                root_[i1] = (Span**)ptr;
            }
            root_[i1][i2]=v;
        }
};

class PageCache
{
    private:
        static const size_t kMaxPageCount = 128;
        SpanList span_lists_[kMaxPageCount + 1];
        std::mutex mutex_;
        size_t page_size_;
        //std::unordered_map<size_t, Span*> page_id_to_span_;
        PageMap2<36> page_id_to_span_;
        PageCache()
        {
            #ifdef _WIN32
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
                    page_id_to_span_.set(span->page_id + i, span);
                span->is_used=true;
                return span;
            }

            for(size_t i=n+1; i<=kMaxPageCount; ++i)
            {
                if(!span_lists_[i].Empty())
                {
                    Span *big_span = span_lists_[i].PopFront();
                    Span *small_span = SpanPool::GetInstance().New();
                    small_span->page_id = big_span->page_id;
                    small_span->page_count = n;
                    small_span->is_used = true;

                    big_span->page_id += n;
                    big_span->page_count -= n;
                    span_lists_[big_span->page_count].PushBack(big_span);

                    for(size_t j=0; j<small_span->page_count; ++j)
                        page_id_to_span_.set(small_span->page_id+j, small_span);
                    return small_span;
                }
            }

            size_t total_size = n * page_size_;
            void *ptr = SystemAllocate(n);
            if(!ptr) throw std::bad_alloc();

            Span *span = SpanPool::GetInstance().New();
            span->page_id = reinterpret_cast<size_t>(ptr)/page_size_;
            span->page_count = n;
            span->is_used = true;

            for(size_t i=0; i<n; ++i) page_id_to_span_.set(span->page_id+i, span);
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

            // for(size_t i=0; i<span->page_count;++i) page_id_to_span_.erase(span->page_id+i);
        }
        
        Span *FindSpanByAddr(void *addr)
        {
            /*
            if(!addr) return nullptr;
            size_t page_id = reinterpret_cast<size_t>(addr)/page_size_;
            std::lock_guard<std::mutex>lock(mutex_);
            auto it=page_id_to_span_.find(page_id);
            return it!=page_id_to_span_.end()?it->second:nullptr;
            */
            if(!addr) return nullptr;
            size_t page_id = reinterpret_cast<size_t>(addr)/page_size_;
            return page_id_to_span_.get(page_id);
        }

        size_t GetPageSize()const
        {
            return page_size_;
        }

    private:
        void MergeSpan(Span *&span)
        {
            size_t prev_page_id = span->page_id-1;
            Span *prev_span = page_id_to_span_.get(prev_page_id);
            if(prev_span!=nullptr)
            {
                if(!prev_span->is_used && span->page_count + prev_span->page_count <= kMaxPageCount&&prev_span->page_id + prev_span->page_count == span->page_id)
                {
                    span_lists_[prev_span->page_count].RemoveSpan(prev_span);
                    prev_span->page_count += span->page_count;
                    SpanPool::GetInstance().Delete(span);
                    span=prev_span;
                    for(size_t i=0; i<span->page_count; ++i) page_id_to_span_.set(span->page_id+i, span);
                }
            }

            size_t next_page_id = span->page_id + span->page_count;
            Span *next_span = page_id_to_span_.get(next_page_id);
            if(next_span!=nullptr)
            {
                if(!next_span->is_used && next_span->page_count + span->page_count <= kMaxPageCount &&span->page_id+span->page_count==next_span->page_id)
                {
                    span_lists_[next_span->page_count].RemoveSpan(next_span);
                    span->page_count += next_span->page_count;
                   SpanPool::GetInstance().Delete(next_span);
                    for(size_t i=0; i<span->page_count; ++i) page_id_to_span_.set(span->page_id+i, span);
                }
            }
        }

        void *SystemAllocate(size_t n)
        {
            size_t total_size = n * page_size_;
            #ifdef _WIN32
                return VirtualAlloc(nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            #else
                return mmap(nullptr, total_size, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            #endif
        }

        void SystemFree(void *ptr, size_t n)
        {
            size_t total_size = n * page_size_;
            #ifdef _WIN32
                VirtualFree(ptr, 0, MEM_RELEASE);
            #else
                munmap(ptr, total_size);
            #endif
        }
};

class SpinLock
{
    std::atomic_flag flag_=ATOMIC_FLAG_INIT;
    public:
        void lock()
        {
            while(flag_.test_and_set(std::memory_order_acquire))
            {

            }
        }

        void unlock()
        {
            flag_.clear(std::memory_order_release);
        }
};

class CentralCache
{
    private:
        static const size_t kAlign = 8;
        static const size_t kMaxSmallObjSize = 4096;
        static const size_t kFreeListCount = kMaxSmallObjSize / kAlign;
        SpanList span_lists_[kFreeListCount];
        SpinLock mutexes_[kFreeListCount];
        CentralCache()=default;
        CentralCache(const CentralCache&)=delete;
        CentralCache& operator=(const CentralCache&)=delete;
    
    public:
        static CentralCache& GetInstance()
        {
            static CentralCache instance;
            return instance;
        }
        
        void *AllocateBatch(size_t size, size_t batch_num)
        {
            if(size==0||batch_num==0||size>kMaxSmallObjSize) return nullptr;
            size_t align_size = AlignUp(size, kAlign);
            size_t index = (align_size/kAlign)-1;
            std::lock_guard<SpinLock> lock(mutexes_[index]);

            Span *span=nullptr;
            for(Span *cur=span_lists_[index].Begin(); cur!=span_lists_[index].End(); cur=cur->next)
            {
                if(cur->free_count>0)
                {
                    span=cur;
                    break;
                }
            }
            if(!span)
            {
                size_t page_count = (align_size * batch_num + PageCache::GetInstance().GetPageSize()-1)/PageCache::GetInstance().GetPageSize();
                page_count = std::max(page_count, static_cast<size_t>(1));
                Span *new_span=PageCache::GetInstance().AllocatePages(page_count);
                new_span->block_size=align_size;
                new_span->free_count=(page_count * PageCache::GetInstance().GetPageSize())/align_size;

                char *start=reinterpret_cast<char*>(new_span->page_id*PageCache::GetInstance().GetPageSize());
                char *end = start+page_count*PageCache::GetInstance().GetPageSize();
                new_span->free_list = start;
                char *cur=start;
                while(cur+align_size<end)
                {
                    *reinterpret_cast<char**>(cur)=cur+align_size;
                    cur+=align_size;
                }
                *reinterpret_cast<char**>(cur)=nullptr;

                span_lists_[index].PushBack(new_span);
                span =new_span;
            }

            void *start=span->free_list;
            void *end=start;
            size_t actual_num=std::min(batch_num, span->free_count);
            for(size_t i=0; i<actual_num-1; ++i)
                end = *reinterpret_cast<void**>(end);
            
            span->free_list = *reinterpret_cast<void**>(end);
            *reinterpret_cast<void**>(end) = nullptr;
            span->free_count-=actual_num;
            return start;
        }
        
        void FreeBatch(size_t size, void *start, size_t count)
        {
            if(!start||size==0||count==0||size>kMaxSmallObjSize) return;
            size_t align_size = AlignUp(size, kAlign);
            size_t index = (align_size/kAlign)-1;
            std::lock_guard<SpinLock> lock(mutexes_[index]);

            Span *span=PageCache::GetInstance().FindSpanByAddr(start);
            if(!span) return;
            
            /*
            *reinterpret_cast<void**>(reinterpret_cast<char*>(start)+(count-1)*align_size)=span->free_list;
            span->free_list = start;
            span->free_count += count;

            size_t total_blocks = (span->page_count * PageCache::GetInstance().GetPageSize())/align_size;
            if(span->free_count==total_blocks)
            {
                span_lists_[index].RemoveSpan(span);
                PageCache::GetInstance().FreePages(span);
            }
            */
            void *cur=start;
            while(cur)
            {
                void *next = *reinterpret_cast<void**>(cur);
                Span *span = PageCache::GetInstance().FindSpanByAddr(cur);
                if(span)
                {
                    *reinterpret_cast<void**>(cur)=span->free_list;
                    span->free_list=cur;
                    span->free_count++;
                    
                    size_t total_blocks=(span->page_count * PageCache::GetInstance().GetPageSize())/align_size;
                    if(span->free_count==total_blocks)
                    {
                        span_lists_[index].RemoveSpan(span);
                        PageCache::GetInstance().FreePages(span);
                    }
                }
                cur=next;
            }
        }
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
        void *Allocate(size_t size)
        {
            if (size == 0) return nullptr;
        
            if (size > kMaxSmallObjSize) 
            {
                size_t page_count = (size + PageCache::GetInstance().GetPageSize() - 1) / PageCache::GetInstance().GetPageSize();
                Span* span = PageCache::GetInstance().AllocatePages(page_count);
                return reinterpret_cast<void*>(span->page_id * PageCache::GetInstance().GetPageSize());
            }
            size_t align_size = AlignUp(size, kAlign);
            size_t index = (align_size / kAlign) - 1;
        
            if (free_lists_[index]) 
            {
                FreeListNode* node = free_lists_[index];
                free_lists_[index] = node->next;
                return node;
            }
        
            FetchFromCentralCache(index, align_size);
        
            FreeListNode* node = free_lists_[index];
            free_lists_[index] = node->next;
            return node;
        }

        void Free(void* ptr, size_t size)
        {
            if (!ptr || size == 0) return;
            if (size > kMaxSmallObjSize) 
            {
                Span* span = PageCache::GetInstance().FindSpanByAddr(ptr);
                if (span) PageCache::GetInstance(). FreePages(span);
                return;
            }
            size_t align_size = AlignUp(size, kAlign);
            size_t index = (align_size / kAlign) - 1;
        
            FreeListNode* node = static_cast<FreeListNode*>(ptr);
            node->next = free_lists_[index];
            free_lists_[index] = node;
        
            size_t batch_num = CalcBatchNum(align_size);
            if(GetListLength(free_lists_[index]) > batch_num * 2)
                ReleaseToCentralCache(index, align_size, batch_num);
        }
    
    private:
        // calculate the size
        size_t GetListLength(FreeListNode* head) 
        {
            size_t len = 0;
            while (head) 
            {
                len++;
                head = head->next;
            }
            return len;
        }

        // ask central cache for batch blocks(batch_num decreases)
        void FetchFromCentralCache(size_t index, size_t size)
        {
            size_t batch_num = CalcBatchNum(size);
            void* start = CentralCache::GetInstance().AllocateBatch(size, batch_num);
            if (!start) throw std::bad_alloc();
        
            FreeListNode* cur = static_cast<FreeListNode*>(start);
            FreeListNode* end = cur;
            /*
            for (size_t i = 0; i < batch_num - 1; ++i) 
                end = end->next;
            */
            while(end->next!=nullptr) end=end->next;
            
            end->next = free_lists_[index];
            free_lists_[index] = cur;
        }
        // calculate the batch_num(small obj needs more than big obj)
        size_t CalcBatchNum(size_t size) const
        {
            if(size<=64) return 32;
            if(size<=256) return 16;
            if(size<=1024) return 8;
            return 4;
        }

        void ReleaseToCentralCache(size_t index, size_t size, size_t batch_num)
        {
            FreeListNode* cur = free_lists_[index];
            if (!cur) return;
        
            FreeListNode* end = cur;
            for (size_t i = 0; i < batch_num - 1 && end; ++i) 
            {
                end = end->next;
            }
            if (!end) return;

            free_lists_[index] = end->next;
            end->next = nullptr;
        
            CentralCache::GetInstance().FreeBatch(size, cur, batch_num);
        }
};

thread_local ThreadCache tls_thread_cache;

void* PoolAllocate(size_t size) 
{
    try {
        return tls_thread_cache.Allocate(size);
    } 
    catch (const std::bad_alloc& e) 
    {
        std::cerr << "PoolAllocate failed: " << e.what() << std::endl;
        return nullptr;
    }
}

void PoolDeallocate(void* ptr, size_t size) 
{
    tls_thread_cache.Free(ptr, size);
}

// test
struct TestObj 
{
    int data[2]; // 8B
};

void TestConcurrentAlloc() 
{
    const int kThreadNum = 8;
    const int kAllocNum = 100000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadNum; ++t) 
    {
        threads.emplace_back([=]() 
        {
            std::vector<void*> ptrs;
            ptrs.reserve(kAllocNum);
            
            for (int i = 0; i < kAllocNum; ++i) 
            {
                ptrs.push_back(PoolAllocate(sizeof(TestObj)));
                if (!ptrs.back()) 
                {
                    std::cerr << "Thread " << t << " allocate failed" << std::endl;
                    return;
                }
            }
            
            for (void* ptr : ptrs) 
            {
                PoolDeallocate(ptr, sizeof(TestObj));
            }
        });
    }
    for (auto& thread : threads) 
    {
        thread.join();
    }
    std::cout << "Concurrent allocation test passed!" << std::endl;
}

void TestSingleThreadAlloc() 
{
    TestObj* obj1 = static_cast<TestObj*>(PoolAllocate(sizeof(TestObj)));
    TestObj* obj2 = static_cast<TestObj*>(PoolAllocate(sizeof(TestObj)));
    TestObj* obj3 = static_cast<TestObj*>(PoolAllocate(sizeof(TestObj)));
    if (!obj1 || !obj2 || !obj3) 
    {
        std::cerr << "Single thread allocate failed" << std::endl;
        return;
    }
    obj1->data[0] = 1;
    obj2->data[0] = 2;
    obj3->data[0] = 3;
    std::cout << "Single thread alloc: " << obj1 << " (data: " << obj1->data[0] << ")" << std::endl;
    std::cout << "Single thread alloc: " << obj2 << " (data: " << obj2->data[0] << ")" << std::endl;
    std::cout << "Single thread alloc: " << obj3 << " (data: " << obj3->data[0] << ")" << std::endl;
    PoolDeallocate(obj1, sizeof(TestObj));
    PoolDeallocate(obj2, sizeof(TestObj));
    PoolDeallocate(obj3, sizeof(TestObj));
    std::cout << "Single thread test passed!" << std::endl;
}

int main() 
{
    try 
    {
        TestSingleThreadAlloc();
        TestConcurrentAlloc();
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
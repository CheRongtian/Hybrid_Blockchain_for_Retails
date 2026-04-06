#include "mempool.hpp"
#include <iostream>
#include <vector>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
size_t GetCurrentMemory() 
{
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
        return info.resident_size; // current macOS physical memory
    return 0;
}

#elif defined(__linux__)
#include <fstream>
#include <sstream>
size_t GetCurrentMemory() 
{
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line)) 
    {
        if (line.substr(0, 6) == "VmRSS:") // current Linux memory
        {
            std::string tmp;
            size_t mem_kb;
            std::stringstream ss(line);
            ss >> tmp >> mem_kb; 
            return mem_kb * 1024; 
        }
    }
    return 0;
}
#else
size_t GetCurrentMemory() 
{ 
    return 0; 
}
#endif


class NormalObject 
{
public:
    int id_;
    char name_[16];
};

int main() 
{
    const int NUM_ALLOCS = 1000000;

    std::vector<NormalObject*> normal_objs;
    std::vector<UserObject*> pool_objs;
    normal_objs.reserve(NUM_ALLOCS);
    pool_objs.reserve(NUM_ALLOCS);

    std::cout << "Space usage testing ongoing..." << std::endl;

    size_t mem_base = GetCurrentMemory();

    for(int i = 0; i < NUM_ALLOCS; i++)
        normal_objs.push_back(new NormalObject());
    
    size_t mem_after_normal = GetCurrentMemory();
    double normal_used_mb = (mem_after_normal - mem_base) / (1024.0 * 1024.0);

    for(int i = 0; i < NUM_ALLOCS; i++)
        pool_objs.push_back(new UserObject());

    size_t mem_after_pool = GetCurrentMemory();
    double pool_used_mb = (mem_after_pool - mem_after_normal) / (1024.0 * 1024.0);

    std::cout << "-----------------------------------" << std::endl;
    std::cout << "Allocate " << NUM_ALLOCS << " test cases:" << std::endl;
    std::cout << "1. Standard new uses: " << normal_used_mb << " MB" << std::endl;
    std::cout << "2. Mempool uses: " << pool_used_mb << " MB" << std::endl;
    
    double saved_mb = normal_used_mb - pool_used_mb;
    std::cout << "Mempool saved memory : " << saved_mb << " MB (" 
              << (saved_mb / normal_used_mb) * 100 << "%)" << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    for(auto obj : normal_objs) delete obj;
    for(auto obj : pool_objs) delete obj;

    return 0;
}
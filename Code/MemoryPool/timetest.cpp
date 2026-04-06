#include "mempool.hpp"
#include <iostream>
#include <chrono>
#include <vector>

// normal one
class NormalObject {
public:
    int id_;
    char name_[16];
};

int main() 
{
    const int NUM_ALLOCS = 1000000;
    
    std::vector<UserObject*> pool_objs;
    std::vector<NormalObject*> normal_objs;
    pool_objs.reserve(NUM_ALLOCS);
    normal_objs.reserve(NUM_ALLOCS);

    std::cout << "Start Benchmark test..." << std::endl;

    // test1：use mempool
    auto start_pool = std::chrono::high_resolution_clock::now();
    for(int i = 0; i < NUM_ALLOCS; i++) 
        pool_objs.push_back(new UserObject());
    
    for(auto obj : pool_objs)
        delete obj;
    
    auto end_pool = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> pool_time = end_pool - start_pool;

    // test 2：use standard new / malloc
    auto start_normal = std::chrono::high_resolution_clock::now();
    for(int i = 0; i < NUM_ALLOCS; i++) 
        normal_objs.push_back(new NormalObject());

    for(auto obj : normal_objs)
        delete obj;

    auto end_normal = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> normal_time = end_normal - start_normal;

    // 输出结果
    std::cout << "-----------------------------------" << std::endl;
    std::cout << "Allocate and free " << NUM_ALLOCS << " tests:" << std::endl;
    std::cout << "1. Mempool time: " << pool_time.count() << " ms" << std::endl;
    std::cout << "2. Standard new time: " << normal_time.count() << " ms" << std::endl;
    std::cout << "Performance Improved: " << normal_time.count() / pool_time.count() << " times" << std::endl;

    return 0;
}
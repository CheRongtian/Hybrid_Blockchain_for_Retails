#include <iostream>
#include <vector>
#include <thread>

struct TestObj { int data[2]; };

volatile size_t global_sink = 0;

void TestConcurrentAlloc() {
    const int kThreadNum = 8;
    const int kAllocNum = 100000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadNum; ++t) {
        threads.emplace_back([=]() {
            std::vector<TestObj*> ptrs;
            ptrs.reserve(kAllocNum);
            size_t local_sum = 0;
            for (int i = 0; i < kAllocNum; ++i) {
                TestObj* obj = new TestObj();
                obj->data[0] = i;
                local_sum += obj->data[0];
                ptrs.push_back(obj);
            }
            global_sink = local_sum;
            for (auto ptr : ptrs) {
                delete ptr;
            }
        });
    }
    for (auto& thread : threads) { thread.join(); }
}

void TestSingleThreadAlloc() {
    TestObj* obj1 = new TestObj();
    TestObj* obj2 = new TestObj();
    TestObj* obj3 = new TestObj();
    obj1->data[0] = 1; obj2->data[0] = 2; obj3->data[0] = 3;
    global_sink = obj1->data[0];
    delete obj1; delete obj2; delete obj3;
}

int main() {
    TestSingleThreadAlloc();
    TestConcurrentAlloc();
    return 0;
}
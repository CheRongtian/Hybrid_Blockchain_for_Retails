#ifndef CONMEM_POOL_HPP
#define CONMEM_POOL_HPP

#include <cstddef>

namespace conmem
{
void* allocate(std::size_t size) noexcept;
void deallocate(void* ptr, std::size_t size) noexcept;
}

#endif

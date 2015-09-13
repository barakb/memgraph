#ifndef MEMGRAPH_UTILS_MEMORY_ATOMIC_SHARED_PTR_HPP
#define MEMGRAPH_UTILS_MEMORY_ATOMIC_SHARED_PTR_HPP

#include <atomic>
#include <memory>

template<class T>
class atomic_shared_ptr final
{
public:
    atomic_shared_ptr(std::shared_ptr<T>&& ptr)
        : ptr(ptr) {}

    std::shared_ptr<T> load()
    {
        return std::move(std::atomic_load(&ptr));
    }

    bool compare_exchange_weak(std::shared_ptr<T>& expected,
                               std::shared_ptr<T> desired)
    {
        return atomic_compare_exchange_weak(&ptr, &expected, desired);
    }

private:
    std::shared_ptr<T> ptr;
};

#endif

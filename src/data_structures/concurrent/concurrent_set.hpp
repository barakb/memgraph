#pragma once

#include "data_structures/concurrent/common.hpp"
#include "data_structures/concurrent/skiplist.hpp"

template <class T>
class ConcurrentSet
{
    typedef SkipList<T> list;
    typedef typename SkipList<T>::Iterator list_it;
    typedef typename SkipList<T>::ConstIterator list_it_con;

public:
    ConcurrentSet() {}

    class Accessor : public AccessorBase<T>
    {
        friend class ConcurrentSet;

        using AccessorBase<T>::AccessorBase;

    private:
        using AccessorBase<T>::accessor;

    public:
        std::pair<list_it, bool> insert(const T &item)
        {
            return accessor.insert(item);
        }

        std::pair<list_it, bool> insert(T &&item)
        {
            return accessor.insert(std::forward<T>(item));
        }

        list_it_con find(const T &item) const { return accessor.find(item); }

        list_it find(const T &item) { return accessor.find(item); }

        bool contains(const T &item) const
        {
            return this->find(item) != this->end();
        }

        bool remove(const T &item) { return accessor.remove(item); }
    };

    Accessor access() { return Accessor(&skiplist); }

    const Accessor access() const { return Accessor(&skiplist); }

private:
    list skiplist;
};

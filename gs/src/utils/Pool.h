#pragma once

#include <deque>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <cassert>

template<class T> struct Pool
{
    std::function<void(T&)> on_acquire;
    std::function<void(T&)> on_release;

    typedef std::shared_ptr<T> Ptr;
    Pool();
    Ptr acquire();

private:
    std::function<void(T*)> m_garbage_collector;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<T>> m_items;

    int x_reused = 0;
    int x_new = 0;
    int x_returned = 0;
};


template<class T> Pool<T>::Pool()
{
    m_garbage_collector = [this](T* t)
    {
        x_returned++;
        if (on_release)
            on_release(*t);

        std::lock_guard<std::mutex> lg(m_mutex);
        m_items.emplace_back(static_cast<T*>(t)); //will create a unique pointer from the raw one
//        printf("%d// new:%d reused:%d returned:%d\n", this, x_new, x_reused, x_returned);
    };
}

template<class T> auto Pool<T>::acquire() -> Ptr
{
    //this will be called when the last shared_ptr to T dies. We can safetly return the object to pur pool

    std::lock_guard<std::mutex> lg(m_mutex);
    T* item = nullptr;
    if (!m_items.empty())
    {
        x_reused++;
        item = m_items.back().release(); //release the raw ptr from the control of the unique ptr
        m_items.pop_back();
//        printf("%d// new:%d reused:%d returned:%d\n", this, x_new, x_reused, x_returned);
    }
    else
    {
        x_new++;
        item = new T;
//        printf("%d// new:%d reused:%d returned:%d\n", this, x_new, x_reused, x_returned);
    }
    assert(item);

    if (on_acquire)
        on_acquire(static_cast<T&>(*item));

    return Ptr(item, [this](T* item) { m_garbage_collector(item); });
}

// emhash6::HashMap for C++11/14/17
// version 1.6.8
// https://github.com/ktprime/ktprime/blob/master/hash_table6.hpp
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019-2022 Huang Yuanbing & bailuzhou AT 163.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE

#pragma once

#include <cstring>
#include <string>
#include <cmath>
#include <cstdlib>
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstdint>
#include <functional>
#include <iterator>
#include <algorithm>

#ifdef __has_include
    #if __has_include("wyhash.h")
    #include "wyhash.h"
    #endif
#elif EMH_WY_HASH
    #include "wyhash.h"
#endif

#ifdef EMH_KEY
    #undef  EMH_KEY
    #undef  EMH_VAL
    #undef  EMH_PKV
    #undef  EMH_NEW
    #undef  EMH_SET
    #undef  EMH_BUCKET
    #undef  EMH_EMPTY
#endif

// likely/unlikely
#if (__GNUC__ >= 4 || __clang__)
#    define EMH_LIKELY(condition)   __builtin_expect(condition, 1)
#    define EMH_UNLIKELY(condition) __builtin_expect(condition, 0)
#else
#    define EMH_LIKELY(condition)   condition
#    define EMH_UNLIKELY(condition) condition
#endif

#ifndef EMH_BUCKET_INDEX
    #define EMH_BUCKET_INDEX 1
#endif
#if EMH_CACHE_LINE_SIZE < 32
    #define EMH_CACHE_LINE_SIZE 64
#endif

#ifndef EMH_DEFAULT_LOAD_FACTOR
#define EMH_DEFAULT_LOAD_FACTOR 0.88f
#endif

#if EMH_BUCKET_INDEX == 0
    #define EMH_KEY(p,n)     p[n].second.first
    #define EMH_VAL(p,n)     p[n].second.second
    #define EMH_BUCKET(p,n)  p[n].first / 2
    #define EMH_ADDR(p,n)    p[n].first
    #define EMH_EMPTY(p,n)   ((int)p[n].first < 0)
    #define EMH_PKV(p,n)     p[n].second
    #define EMH_NEW(key, val, bucket, next) new(_pairs + bucket) PairT(next, value_type(key, val)), _num_filled ++; EMH_SET(bucket)
#elif EMH_BUCKET_INDEX == 2
    #define EMH_KEY(p,n)     p[n].first.first
    #define EMH_VAL(p,n)     p[n].first.second
    #define EMH_BUCKET(p,n)  p[n].second / 2
    #define EMH_ADDR(p,n)    p[n].second
    #define EMH_EMPTY(p,n)   ((int)p[n].second < 0)
    #define EMH_PKV(p,n)     p[n].first
    #define EMH_NEW(key, val, bucket, next) new(_pairs + bucket) PairT(value_type(key, val), next), _num_filled ++; EMH_SET(bucket)
#else
    #define EMH_KEY(p,n)     p[n].first
    #define EMH_VAL(p,n)     p[n].second
    #define EMH_BUCKET(p,n)  p[n].bucket / 2
    #define EMH_ADDR(p,n)    p[n].bucket
    #define EMH_EMPTY(p,n)   (0 > (int)p[n].bucket)
    #define EMH_PKV(p,n)     p[n]
    #define EMH_NEW(key, val, bucket, next) new(_pairs + bucket) PairT(key, val, next), _num_filled ++; EMH_SET(bucket)
#endif

#define EMH_MASK(bucket) 1 << (bucket % MASK_BIT)
#define EMH_SET(bucket)  _bitmask[bucket / MASK_BIT] &= ~(EMH_MASK(bucket))
#define EMH_CLS(bucket)  _bitmask[bucket / MASK_BIT] |= EMH_MASK(bucket)
//#define EMH_EMPTY(bitmask, bucket)     (_bitmask[bucket / MASK_BIT] & (EMH_MASK(bucket))) != 0

#if _WIN32
    #include <intrin.h>
#if _WIN64
    #pragma intrinsic(_umul128)
#endif
#endif

namespace emhash6 {

#ifndef EMH_SIZE_TYPE_64BIT
    typedef uint32_t size_type;
    static constexpr size_type INACTIVE = 0 - 0x1u;
#else
    typedef uint64_t size_type;
    static constexpr size_type INACTIVE = 0 - 0x1ull;
#endif


constexpr size_type MASK_BIT = sizeof(size_type) * 8;
constexpr size_type BIT_PACK = sizeof(uint64_t) * 2 + sizeof(uint8_t);
constexpr size_type SIZE_BIT = sizeof(size_t) * 8;
constexpr size_type PACK_SIZE = 1; // > 1
static_assert(INACTIVE % 2 == 1, "INACTIVE must be even and < 0(to int)");
static_assert((int)INACTIVE < 0, "INACTIVE must be even and < 0(to int)");

//https://gist.github.com/jtbr/1896790eb6ad50506d5f042991906c30
inline static size_type CTZ(size_t n)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86) || (__BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

#elif __BIG_ENDIAN__ || (__BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    n = __builtin_bswap64(n);
#else
    static uint32_t endianness = 0x12345678;
    const auto is_big = *(const char *)&endianness == 0x12;
    if (is_big)
    n = __builtin_bswap64(n);
#endif

#if _WIN32
    unsigned long index;
    #if defined(_WIN64)
    _BitScanForward64(&index, n);
    #else
    _BitScanForward(&index, n);
    #endif
#elif defined (__LP64__) || (SIZE_MAX == UINT64_MAX) || defined (__x86_64__)
    auto index = __builtin_ctzll(n);
#elif 1
    auto index = __builtin_ctzl(n);
#else
    #if defined (__LP64__) || (SIZE_MAX == UINT64_MAX) || defined (__x86_64__)
    size_type index;
    __asm__("bsfq %1, %0\n" : "=r" (index) : "rm" (n) : "cc");
    #else
    size_type index;
    __asm__("bsf %1, %0\n" : "=r" (index) : "rm" (n) : "cc");
    #endif
#endif

    return (size_type)index;
}

template <typename First, typename Second>
struct entry {
    using first_type =  First;
    using second_type = Second;
    entry(const First& key, const Second& val, size_type ibucket) :second(val),first(key) { bucket = ibucket; }
    entry(First&& key, Second&& val, size_type ibucket) :second(std::move(val)), first(std::move(key)) { bucket = ibucket; }

    entry(const std::pair<First,Second>& pair) :second(pair.second),first(pair.first) { bucket = INACTIVE; }
    entry(std::pair<First, Second>&& pair) :second(std::move(pair.second)),first(std::move(pair.first)) { bucket = INACTIVE; }

    entry(const entry& pairT) :second(pairT.second),first(pairT.first) { bucket = pairT.bucket; }
    entry(entry&& pairT) noexcept :second(std::move(pairT.second)),first(std::move(pairT.first)) { bucket = pairT.bucket; }

    template<typename K, typename V>
    entry(K&& key, V&& val, size_type ibucket)
        :second(std::forward<V>(val)), first(std::forward<K>(key))
    {
        bucket = ibucket;
    }

    entry& operator = (entry&& pairT)
    {
        second = std::move(pairT.second);
        bucket = pairT.bucket;
        first = std::move(pairT.first);
        return *this;
    }

    entry& operator = (const entry& o)
    {
        second = o.second;
        bucket = o.bucket;
        first  = o.first;
        return *this;
    }

    bool operator == (const std::pair<First, Second>& p) const
    {
        return first == p.first && second == p.second;
    }

    bool operator == (const entry<First, Second>& p) const
    {
        return first == p.first && second == p.second;
    }

    void swap(entry<First, Second>& o)
    {
        std::swap(second, o.second);
        std::swap(first, o.first);
    }

#ifndef EMH_ORDER_KV
    Second second;//int
    size_type bucket;
    First first; //long
#else
    First first; //long
    size_type bucket;
    Second second;//int
#endif
};// __attribute__ ((packed));

/// A cache-friendly hash table with open addressing, linear/qua probing and power-of-two capacity
template <typename KeyT, typename ValueT, typename HashT = std::hash<KeyT>, typename EqT = std::equal_to<KeyT>>
class HashMap
{
public:
    typedef HashMap<KeyT, ValueT, HashT, EqT> htype;
    typedef std::pair<KeyT,ValueT>            value_type;

#if EMH_BUCKET_INDEX == 0
    typedef value_type                        value_pair;
    typedef std::pair<size_type, value_type>  PairT;
#elif EMH_BUCKET_INDEX == 2
    typedef value_type                        value_pair;
    typedef std::pair<value_type, size_type>  PairT;
#else
    typedef entry<KeyT, ValueT>               value_pair;
    typedef entry<KeyT, ValueT>               PairT;
#endif

    typedef KeyT   key_type;
    typedef ValueT val_type;
    typedef ValueT mapped_type;
    typedef HashT  hasher;
    typedef EqT    key_equal;
    typedef PairT&       reference;
    typedef const PairT& const_reference;

    class const_iterator;
    class iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef std::ptrdiff_t            difference_type;
        typedef value_pair                value_type;

        typedef value_pair*               pointer;
        typedef value_pair&               reference;

        iterator() : _map(nullptr) { }
        iterator(const const_iterator& it) : _map(it._map), _bucket(it._bucket), _from(it._from), _bmask(it._bmask) { }
        iterator(const htype* hash_map, size_type bucket, bool) : _map(hash_map), _bucket(bucket) { init(); }
        iterator(const htype* hash_map, size_type bucket) : _map(hash_map), _bucket(bucket) { _bmask = _from = 0; }

        void init()
        {
            _from = (_bucket / SIZE_BIT) * SIZE_BIT;
            if (_bucket < _map->bucket_count()) {
                _bmask = *(size_t*)((size_t*)_map->_bitmask + _from / SIZE_BIT);
                _bmask |= (1ull << _bucket % SIZE_BIT) - 1;
                _bmask = ~_bmask;
            } else {
                _bmask = 0;
            }
        }

        size_t bucket() const
        {
            return _bucket;
        }

        void clear(size_type bucket)
        {
            if (_bucket / SIZE_BIT == bucket / SIZE_BIT)
                _bmask &= ~(1ull << (bucket % SIZE_BIT));
        }

        iterator& next()
        {
            goto_next_element();
            _bmask &= _bmask - 1;
            return *this;
        }

        iterator& operator++()
        {
            _bmask &= _bmask - 1;
            goto_next_element();
            return *this;
        }

        iterator operator++(int)
        {
            iterator old = *this;
            _bmask &= _bmask - 1;
            goto_next_element();
            return old;
        }

        reference operator*() const
        {
            return _map->EMH_PKV(_pairs, _bucket);
        }

        pointer operator->() const
        {
            return &(_map->EMH_PKV(_pairs, _bucket));
        }

        bool operator==(const iterator& rhs) const { return _bucket == rhs._bucket; }
        bool operator!=(const iterator& rhs) const { return _bucket != rhs._bucket; }
        bool operator==(const const_iterator& rhs) const { return _bucket == rhs._bucket; }
        bool operator!=(const const_iterator& rhs) const { return _bucket != rhs._bucket; }

    private:
        void goto_next_element()
        {
            if (EMH_LIKELY(_bmask != 0)) {
                _bucket = _from + CTZ(_bmask);
                return;
            }

            do {
                _bmask = ~*(size_t*)((size_t*)_map->_bitmask + (_from += SIZE_BIT) / SIZE_BIT);
            } while (_bmask == 0);

            _bucket = _from + CTZ(_bmask);
        }

    public:
        const htype* _map;
        size_t    _bmask;
        size_type _bucket;
        size_type _from;
    };

    class const_iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef std::ptrdiff_t            difference_type;
        typedef value_pair                value_type;

        typedef const value_pair*          pointer;
        typedef const value_pair&          reference;

        const_iterator(const iterator& it) : _map(it._map), _bucket(it._bucket), _from(it._from), _bmask(it._bmask) { }
        const_iterator(const htype* hash_map, size_type bucket, bool) : _map(hash_map), _bucket(bucket) { init(); }
        const_iterator(const htype* hash_map, size_type bucket) : _map(hash_map), _bucket(bucket) { _bmask = _from = 0; }

        void init()
        {
            _from = (_bucket / SIZE_BIT) * SIZE_BIT;
            if (_bucket < _map->bucket_count()) {
                _bmask = *(size_t*)((size_t*)_map->_bitmask + _from / SIZE_BIT);
                _bmask |= (1ull << _bucket % SIZE_BIT) - 1;
                _bmask = ~_bmask;
            } else {
                _bmask = 0;
            }
        }

        size_t bucket() const
        {
            return _bucket;
        }

        const_iterator& operator++()
        {
            goto_next_element();
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator old(*this);
            goto_next_element();
            return old;
        }

        reference operator*() const
        {
            return _map->EMH_PKV(_pairs, _bucket);
        }

        pointer operator->() const
        {
            return &(_map->EMH_PKV(_pairs, _bucket));
        }

        bool operator==(const const_iterator& rhs) const { return _bucket == rhs._bucket; }
        bool operator!=(const const_iterator& rhs) const { return _bucket != rhs._bucket; }

    private:
        void goto_next_element()
        {
            _bmask &= _bmask - 1;
            if (_bmask != 0) {
                _bucket = _from + CTZ(_bmask);
                return;
            }

            do {
                _bmask = ~*(size_t*)((size_t*)_map->_bitmask + (_from += SIZE_BIT) / SIZE_BIT);
            } while (_bmask == 0);

            _bucket = _from + CTZ(_bmask);
        }

    public:
        const htype* _map;
        size_t    _bmask;
        size_type _bucket;
        size_type _from;
    };

    void init(size_type bucket, float lf = EMH_DEFAULT_LOAD_FACTOR)
    {
#if EMH_SAFE_HASH
        _num_main = _hash_inter = 0;
#endif
        _mask = 0;
        _pairs = nullptr;
        _bitmask = nullptr;
        _num_filled = 0;
        max_load_factor(lf);
        reserve(bucket + 1);
    }

    HashMap(size_type bucket = 4, float lf = EMH_DEFAULT_LOAD_FACTOR)
    {
        init(bucket, lf);
    }

    size_type AllocSize(size_type num_buckets) const
    {
        return num_buckets * sizeof(PairT) + PACK_SIZE * sizeof(PairT) + num_buckets / 8 + sizeof(uint64_t);
    }

    HashMap(const HashMap& rhs)
    {
        _pairs = (PairT*)malloc((3 + rhs._mask) * sizeof(PairT) + (rhs._mask + 1) / 8 + BIT_PACK);
        clone(rhs);
    }

    HashMap(HashMap&& rhs)
    {
        _mask = _num_filled = 0;
        _pairs = nullptr;
        swap(rhs);
    }

    HashMap(std::initializer_list<value_type> ilist)
    {
        init((size_type)ilist.size());
        for (auto it = ilist.begin(); it != ilist.end(); ++it)
            do_insert(*it);
    }

    template<class InputIt>
    HashMap(InputIt first, InputIt last, size_type bucket_count=4)
    {
        init(std::distance(first, last) + bucket_count);
        for (; first != last; ++first)
            emplace(*first);
    }

    HashMap& operator=(const HashMap& rhs)
    {
        if (this == &rhs)
            return *this;

        if (is_triviall_destructable())
            clearkv();

        if (_mask != rhs._mask) {
            free(_pairs);
            _pairs = (PairT*)malloc((3 + rhs._mask) * sizeof(PairT) + (rhs._mask + 1) / 8 + BIT_PACK);
        }

        clone(rhs);
        return *this;
    }

    HashMap& operator=(HashMap&& rhs)
    {
        if (this != &rhs) {
            swap(rhs);
            rhs.clear();
        }
        return *this;
    }

    template<typename Con>
    bool operator == (const Con& rhs) const
    {
        if (size() != rhs.size())
            return false;

        for (auto it = begin(), last = end(); it != last; it++) {
            auto oi = rhs.find(it->first);
            if (oi == rhs.end() || it->second != oi->second)
                return false;
        }
        return true;
    }

    template<typename Con>
    bool operator != (const Con& rhs) const { return !(*this == rhs); }

    ~HashMap()
    {
        if (is_triviall_destructable()) {
            for (auto it = cbegin(); _num_filled; ++it) {
                _num_filled --;
                it->~value_pair();
            }
        }
        free(_pairs);
    }

    void clone(const HashMap& rhs)
    {
        _hasher      = rhs._hasher;
//        _eq          = rhs._eq;
#if EMH_SAFE_HASH
        _num_main    = rhs._num_main;
        _hash_inter  = rhs._hash_inter;
#endif
        _num_filled  = rhs._num_filled;
        _mask        = rhs._mask;
        _mlf      = rhs._mlf;
        _bitmask     = (size_type*)((char*)_pairs + ((char*)rhs._bitmask - (char*)rhs._pairs));
        auto opairs  = rhs._pairs;

        auto _num_buckets = _mask + 1;
        if (is_copy_trivially())
            memcpy(_pairs, opairs, _num_buckets * sizeof(PairT));
        else {
            for (size_type bucket = 0; bucket < _num_buckets; bucket++) {
                auto next_bucket = EMH_ADDR(_pairs, bucket) = EMH_ADDR(opairs, bucket);
                if ((int)next_bucket >= 0)
                    new(_pairs + bucket) PairT(opairs[bucket]);
            }
        }
        memcpy(_pairs + _num_buckets, opairs + _num_buckets, 2 * sizeof(PairT) + _num_buckets / 8 + BIT_PACK);
    }

    void swap(HashMap& rhs)
    {
        std::swap(_hasher, rhs._hasher);
//      std::swap(_eq, rhs._eq);
        std::swap(_pairs, rhs._pairs);
#if EMH_SAFE_HASH
        std::swap(_num_main, rhs._num_main);
        std::swap(_hash_inter, rhs._hash_inter);
#endif
        std::swap(_num_filled, rhs._num_filled);
        std::swap(_mask, rhs._mask);
        std::swap(_mlf, rhs._mlf);
        std::swap(_bitmask, rhs._bitmask);
        std::swap(EMH_ADDR(_pairs, _mask + 1), EMH_ADDR(rhs._pairs, rhs._mask + 1));
    }

    // -------------------------------------------------------------
    iterator begin()
    {
        if (0 == _num_filled)
            return {this, _mask + 1};

        const auto bmask = ~(*(size_t*)_bitmask);
        if (bmask != 0)
            return {this, CTZ(bmask), true};

        iterator it(this, sizeof(bmask) * 8 - 1);
        return it.next();
    }

    const_iterator cbegin() const
    {
        const auto bmask = ~(*(size_t*)_bitmask);
        if (bmask != 0)
            return {this, CTZ(bmask), true};
        else if (0 == _num_filled)
            return {this, _mask + 1};

        iterator it(this, sizeof(bmask) * 8 - 1);
        return it.next();
    }

    iterator last() const
    {
        if (_num_filled == 0)
            return end();

        auto bucket = _mask;
        while (EMH_EMPTY(_pairs, bucket)) bucket--;
        return {this, bucket, true};
    }

    const_iterator begin() const { return cbegin(); }

    iterator end() { return {this, _mask + 1}; }
    const_iterator cend() const { return {this, _mask + 1}; }
    const_iterator end() const { return {this, _mask + 1}; }

    size_type size() const { return _num_filled; }
    bool empty() const { return _num_filled == 0; }

    size_type bucket_count() const { return _mask + 1; }
    float load_factor() const { return static_cast<float>(_num_filled) / (_mask + 1); }

    HashT& hash_function() const { return _hasher; }
    EqT& key_eq() const { return _eq; }

    void max_load_factor(float mlf)
    {
        if (mlf < 0.9999f && mlf > 0.2f)
            _mlf = (uint32_t)((1 << 27) / mlf);
    }

    constexpr float max_load_factor() const { return (1 << 27) / (float)_mlf; }
    constexpr size_type max_size() const { return (1ull << (sizeof(size_type) * 8 - 2)); }
    constexpr size_type max_bucket_count() const { return max_size(); }

#ifdef EMH_STATIS
    //Returns the bucket number where the element with key k is located.
    size_type bucket(const KeyT& key) const
    {
        const auto bucket = hash_key(key) & _mask;
        const auto next_bucket = EMH_ADDR(_pairs, bucket);
        if ((int)next_bucket < 0)
            return 0;
        else if (bucket == next_bucket * 2)
            return bucket + 1;

        return hash_main(bucket);
    }

    //Returns the number of elements in bucket n.
    size_type bucket_size(const size_type bucket) const
    {
        auto next_bucket = EMH_ADDR(_pairs, bucket);
        if ((int)next_bucket < 0)
            return 0;

        const auto& bucket_key = EMH_KEY(_pairs, bucket);
        next_bucket = hash_key(bucket_key) & _mask;
        size_type bucket_size = 1;

        //iterator each item in current main bucket
        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket) {
                break;
            }
            bucket_size++;
            next_bucket = nbucket;
        }
        return bucket_size;
    }

    size_type get_main_bucket(const size_type bucket) const
    {
        if (EMH_EMPTY(_pairs, bucket))
            return -1u;

        return hash_main(bucket);
    }

    int get_cache_info(size_type bucket, size_type next_bucket) const
    {
        auto pbucket = reinterpret_cast<std::ptrdiff_t>(&_pairs[bucket]);
        auto pnext   = reinterpret_cast<std::ptrdiff_t>(&_pairs[next_bucket]);
        if (pbucket / 64 == pnext / 64)
            return 0;
        auto diff = pbucket > pnext ? (pbucket - pnext) : pnext - pbucket;
        if (diff < 127 * 64)
            return diff / 64 + 1;
        return 127;
    }

    int get_bucket_info(const size_type bucket, size_type steps[], const size_type slots) const
    {
        auto next_bucket = EMH_ADDR(_pairs, bucket);
        if ((int)next_bucket < 0)
            return -1;

        const auto main_bucket = hash_main(bucket);
        if (main_bucket != bucket)
            return 0;
        else if (next_bucket == bucket)
            return 1;

        steps[get_cache_info(bucket, next_bucket) % slots] ++;
        size_type ibucket_size = 2;
        //find a new empty and linked it to tail
        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                break;

            steps[get_cache_info(nbucket, next_bucket) % slots] ++;
            ibucket_size ++;
            next_bucket = nbucket;
        }
        return ibucket_size;
    }

    void dump_statics() const
    {
        size_type buckets[129] = {0};
        size_type steps[129]   = {0};
        for (size_type bucket = 0; bucket <= _mask; ++bucket) {
            auto bsize = get_bucket_info(bucket, steps, 128);
            if (bsize > 0)
                buckets[bsize] ++;
        }

        size_type sumb = 0, collision = 0, sumc = 0, finds = 0, sumn = 0;
        puts("============== buckets size ration ========");
        for (size_type i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++) {
            const auto bucketsi = buckets[i];
            if (bucketsi == 0)
                continue;
            sumb += bucketsi;
            sumn += bucketsi * i;
            collision += bucketsi * (i - 1);
            finds += bucketsi * i * (i + 1) / 2;
            printf("  %2u  %8u  %0.8lf  %2.3lf\n", i, bucketsi, bucketsi * 1.0 * i / _num_filled, sumn * 100.0 / _num_filled);
        }

        puts("========== collision miss ration ===========");
        for (size_type i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            sumc += steps[i];
            if (steps[i] <= 2)
                continue;
            printf("  %2u  %8u  %0.2lf  %.2lf\n", i, steps[i], steps[i] * 100.0 / collision, sumc * 100.0 / collision);
        }

        if (sumb == 0)  return;
        printf("    _num_filled/aver_size/packed collision/cache_miss/hit_find = %u/%.2lf/%zd/ %.2lf%%/%.2lf%%/%.2lf\n",
                _num_filled, _num_filled * 1.0 / sumb, sizeof(PairT), (collision * 100.0 / _num_filled), (collision - steps[0]) * 100.0 / _num_filled, finds * 1.0 / _num_filled);
        assert(sumn == _num_filled);
        assert(sumc == collision);
        puts("============== buckets size end =============");
    }
#endif

    // ------------------------------------------------------------
    template<typename Key = KeyT>
    iterator find(const Key& key, size_t key_hash) noexcept
    {
        return {this, find_filled_hash(key, key_hash)};
    }

    template<typename Key = KeyT>
    const_iterator find(const Key& key, size_t key_hash) const noexcept
    {
        return {this, find_filled_hash(key, key_hash)};
    }

    template<typename Key=KeyT>
    iterator find(const Key& key) noexcept
    {
        return {this, find_filled_bucket(key)};
    }

    template<typename Key = KeyT>
    const_iterator find(const Key& key) const noexcept
    {
        return {this, find_filled_bucket(key)};
    }

    template<typename Key = KeyT>
    ValueT& at(const KeyT& key)
    {
        const auto bucket = find_filled_bucket(key);
        //throw
        return EMH_VAL(_pairs, bucket);
    }

    template<typename Key = KeyT>
    const ValueT& at(const KeyT& key) const
    {
        const auto bucket = find_filled_bucket(key);
        //throw
        return EMH_VAL(_pairs, bucket);
    }

    template<typename Key = KeyT>
    bool contains(const Key& key) const noexcept
    {
        return find_filled_bucket(key) <= _mask;
    }

    template<typename Key = KeyT>
    size_type count(const Key& key) const noexcept
    {
        return find_filled_bucket(key) <= _mask ? 1 : 0;
    }

    template<typename Key = KeyT>
    std::pair<iterator, iterator> equal_range(const Key& key) const noexcept
    {
        const auto found = find(key);
        if (found.bucket() > _mask)
            return { found, found };
        else
            return { found, std::next(found) };
    }

    template<typename K=KeyT>
    std::pair<const_iterator, const_iterator> equal_range(const K& key) const
    {
        const auto found = find(key);
        if (found.bucket() > _mask)
            return { found, found };
        else
            return { found, std::next(found) };
    }

    void merge(HashMap& rhs)
    {
        if (empty()) {
            *this = std::move(rhs);
            return;
        }

        for (auto rit = rhs.begin(); rit != rhs.end(); ) {
            auto fit = find(rit->first);
            if (fit.bucket() > _mask) {
                insert_unique(rit->first, std::move(rit->second));
                rit = rhs.erase(rit);
            } else {
                ++rit;
            }
        }
    }

    /// Returns false if key isn't found.
    bool try_get(const KeyT& key, ValueT& val) const noexcept
    {
        const auto bucket = find_filled_bucket(key);
        const auto found = bucket <= _mask;
        if (found) {
            val = EMH_VAL(_pairs, bucket);
        }
        return found;
    }

    /// Returns the matching ValueT or nullptr if k isn't found.
    ValueT* try_get(const KeyT& key) noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket <= _mask ? &EMH_VAL(_pairs, bucket) : nullptr;
    }

    /// Const version of the above
    ValueT* try_get(const KeyT& key) const noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket <= _mask ? &EMH_VAL(_pairs, bucket) : nullptr;
    }

    /// Convenience function.
    ValueT get_or_return_default(const KeyT& key) const noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket <= _mask ? EMH_VAL(_pairs, bucket) : ValueT();
    }

    // -----------------------------------------------------
    /// Returns a pair consisting of an iterator to the inserted element
    /// (or to the element that prevented the insertion)
    /// and a bool denoting whether the insertion took place.
    std::pair<iterator, bool> do_insert(const value_type& value)
    {
        const auto bucket = find_or_allocate(value.first);
        const auto next   = bucket / 2;
        const auto found  = EMH_EMPTY(_pairs, next);
        if (found) {
            EMH_NEW(value.first, value.second, next, bucket);
        }
        return { {this, next}, found };
    }

    std::pair<iterator, bool> do_insert(value_type&& value)
    {
        const auto bucket = find_or_allocate(value.first);
        const auto next   = bucket / 2;
        const auto found  = EMH_EMPTY(_pairs, next);
        if (found) {
            EMH_NEW(std::forward<KeyT>(value.first), std::forward<ValueT>(value.second), next, bucket);
        }
        return { {this, next}, found };
    }

    template<typename K, typename V>
    std::pair<iterator, bool> do_insert(K&& key, V&& val)
    {
        const auto bucket = find_or_allocate(key);
        const auto next   = bucket / 2;
        const auto found  = EMH_EMPTY(_pairs, next);
        if (found) {
            EMH_NEW(std::forward<K>(key), std::forward<V>(val), next, bucket);
        }
        return { {this, next}, found };
    }

    template<typename K, typename V>
    std::pair<iterator, bool> do_assign(K&& key, V&& val)
    {
        check_expand_need();
        const auto bucket = find_or_allocate(key);
        const auto next   = bucket / 2;
        const auto found = EMH_EMPTY(_pairs, next);
        if (found) {
            EMH_NEW(std::forward<K>(key), std::forward<V>(val), next, bucket);
        } else {
            EMH_VAL(_pairs, next) = std::move(val);
        }
        return { {this, next}, found };
    }

    std::pair<iterator, bool> insert(const value_type& value)
    {
        check_expand_need();
        return do_insert(value.first, value.second);
    }

    std::pair<iterator, bool> insert(value_type && value)
    {
        check_expand_need();
        return do_insert(std::move(value.first), std::move(value.second));
    }

    void insert(std::initializer_list<value_type> ilist)
    {
        reserve(ilist.size() + _num_filled);
        for (auto it = ilist.begin(); it != ilist.end(); ++it)
            do_insert(*it);
    }

    template <typename Iter>
    void insert(Iter begin, Iter end)
    {
        reserve(std::distance(begin, end) + _num_filled);
        for (; begin != end; ++begin)
            do_insert(begin->first, begin->second);
    }

#if 0
    size_type try_insert_mainbucket(const KeyT& key, const ValueT& val)
    {
        const auto bucket = hash_key(key) & _mask;
        if (EMH_EMPTY(_pairs, bucket))
        {
#if EMH_SAFE_HASH
            _num_main ++;
#endif
            EMH_NEW(key, val, bucket, bucket * 2);
            return bucket;
        }

        return -1u;
    }

    template <typename Iter>
    void insert2(Iter begin, Iter end)
    {
        Iter citbeg = begin;
        Iter citend = begin;
        reserve(std::distance(begin, end) + _num_filled);
        for (; begin != end; ++begin) {
            if ((int)try_insert_mainbucket(begin->first, begin->second) < 0) {
                std::swap(*begin, *citend++);
            }
        }

        for (; citbeg != citend; ++citbeg)
            insert(*citbeg);
    }

    template <typename Iter>
    void insert_unique(Iter begin, Iter end)
    {
        reserve(std::distance(begin, end) + _num_filled);
        for (; begin != end; ++begin)
            do_insert_unqiue(*begin);
    }
#endif

    /// Same as above, but contains(key) MUST be false
    size_type insert_unique(KeyT&& key, ValueT&& val)
    {
        return do_insert_unqiue(std::move(key), std::forward<ValueT>(val));
    }

    size_type insert_unique(const KeyT& key, ValueT&& val)
    {
        return do_insert_unqiue(key, std::forward<ValueT>(val));
    }

    size_type insert_unique(value_type&& value)
    {
        return do_insert_unqiue(std::move(value.first), std::move(value.second));
    }

    size_type insert_unique(const value_type& value)
    {
        return do_insert_unqiue(value.first, value.second);
    }

    template<typename K, typename V>
    inline size_type do_insert_unqiue(K&& key, V&& val)
    {
        check_expand_need();
        auto bucket = find_unique_bucket(key);
        EMH_NEW(std::forward<K>(key), std::forward<V>(val), bucket / 2, bucket);
        return bucket;
    }

    std::pair<iterator, bool> insert_or_assign(const KeyT& key, ValueT&& val) { return do_assign(key, std::forward<ValueT>(val)); }
    std::pair<iterator, bool> insert_or_assign(KeyT&& key, ValueT&& val) { return do_assign(std::move(key), std::forward<ValueT>(val)); }

    template <typename... Args>
    inline std::pair<iterator, bool> emplace(Args&&... args)
    {
        check_expand_need();
        return do_insert(std::forward<Args>(args)...);
    }

    template <class... Args>
    iterator emplace_hint(const_iterator hint, Args&&... args)
    {
        (void)hint;
        check_expand_need();
        return do_insert(std::forward<Args>(args)...).first;
    }

    template<class... Args>
    std::pair<iterator, bool> try_emplace(const KeyT& key, Args&&... args)
    {
        check_expand_need();
        return do_insert(key, std::forward<Args>(args)...).first;
    }

    template<class... Args>
    std::pair<iterator, bool> try_emplace(KeyT&& key, Args&&... args)
    {
        check_expand_need();
        return do_insert(std::forward<KeyT>(key), std::forward<Args>(args)...).first;
    }

    template <class... Args>
    inline size_type emplace_unique(Args&&... args)
    {
        return insert_unique(std::forward<Args>(args)...);
    }

    ValueT& operator[](const KeyT& key)
    {
        check_expand_need();
        const auto bucket = find_or_allocate(key);
        const auto next   = bucket / 2;
        /* Check if inserting a new value rather than overwriting an old entry */
        if (EMH_EMPTY(_pairs, next)) {
            EMH_NEW(key, std::move(ValueT()), next, bucket);
        }

        //bugs here if return local reference rehash happens
        return EMH_VAL(_pairs, next);
    }

    ValueT& operator[](KeyT&& key)
    {
        check_expand_need();
        const auto bucket = find_or_allocate(key);
        const auto next   = bucket / 2;
        if (EMH_EMPTY(_pairs, next)) {
            EMH_NEW(std::move(key), std::move(ValueT()), next, bucket);
        }

        return EMH_VAL(_pairs, next);
    }

    // -------------------------------------------------------
    /// Erase an element from the hash table.
    /// return 0 if element was not found
    template<typename Key = KeyT>
    size_type erase(const Key& key)
    {
        const auto bucket = erase_key(key);
        if (bucket == INACTIVE)
            return 0;

        clear_bucket(bucket);
        return 1;
    }

    //iterator erase const_iterator
    iterator erase(const_iterator cit)
    {
        iterator it(cit);
        return erase(it);
    }

    /// Erase an element typedef an iterator.
    /// Returns an iterator to the next element (or end()).
    iterator erase(iterator it)
    {
        const auto bucket = erase_bucket(it._bucket);
        clear_bucket(bucket);
        if (bucket == it._bucket) {
            return ++it;
        } else {
            //erase main bucket as next
            it.clear(bucket);
            return it;
        }
    }

    /// Erase an element typedef an iterator without return next iterator
    void _erase(const_iterator it)
    {
        const auto bucket = erase_bucket(it._bucket);
        clear_bucket(bucket);
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        auto iend = cend();
        auto next = first;
        for (; next != last && next != iend; )
            next = erase(next);

        return {this, next.bucket()};
    }

    template<typename Pred>
    size_type erase_if(Pred pred)
    {
        auto old_size = size();
        for (auto it = begin(), last = end(); it != last; ) {
            if (pred(*it))
                it = erase(it);
            else
                ++it;
        }
        return old_size - size();
    }

    static constexpr bool is_triviall_destructable()
    {
#if __cplusplus >= 201402L || _MSC_VER > 1600
        return !(std::is_trivially_destructible<KeyT>::value && std::is_trivially_destructible<ValueT>::value);
#else
        return !(std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    static constexpr bool is_copy_trivially()
    {
#if __cplusplus >= 201402L || _MSC_VER > 1600
        return (std::is_trivially_copyable<KeyT>::value && std::is_trivially_copyable<ValueT>::value);
#else
        return (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    void clearkv()
    {
        for (auto it = cbegin(); _num_filled; ++it)
            clear_bucket(it.bucket());
    }

    void reset_zero()
    {
#if EMH_FIND_HIT
        if constexpr (std::is_integral<KeyT>::value) {
            const auto bucket = hash_key(0) & _mask;
            reset_bucket(bucket);
        }
#endif
    }

    /// Remove all elements, keeping full capacity.
    void clear()
    {
        if (is_triviall_destructable())
            clearkv();
        else if (_num_filled) {
            memset(_bitmask, 0xFFFFFFFF, (_mask + 1) / 8);
            memset(_pairs, INACTIVE, sizeof(_pairs[0]) * (_mask + 1));
#if EMH_FIND_HIT
            if constexpr (std::is_integral<KeyT>::value) {
            for (size_type bucket = 0; bucket <= _mask; bucket++)
                EMH_KEY(_pairs, bucket) = 0;
            }
            reset_zero();
#endif
        }

        EMH_ADDR(_pairs, _mask + 1) = 0; //_last
        _num_filled = 0;
#if EMH_SAFE_HASH
        _num_main = _hash_inter = 0;
#endif
    }

    void shrink_to_fit()
    {
        rehash(_num_filled);
    }

    /// Make room for this many elements
    bool reserve(uint64_t num_elems)
    {
        const auto required_buckets = (size_type)(num_elems * _mlf >> 27);
        if (EMH_LIKELY(required_buckets <= _mask))
            return false;

#if EMH_STATIS
        if (_num_filled > EMH_STATIS) dump_statics(1);
#endif
        rehash(required_buckets + 2);
        return true;
    }

    ///three ways may incr rehash: bad hash function, load_factor is high, or need shrink
    void rehash(size_type required_buckets)
    {
        if (required_buckets < _num_filled)
            return;
#if 0 //(__GNUC__ >= 4 || __clang__)
        size_type num_buckets = 1ul << (sizeof(required_buckets) * 8 - __builtin_clz(required_buckets));
        if (num_buckets < sizeof(uint64_t))
            num_buckets = sizeof(uint64_t);
#else
        size_type num_buckets = _num_filled > (1u << 16) ? (1u << 16) : sizeof(uint64_t);
        while (num_buckets < required_buckets) { num_buckets *= 2; }
        //assert(num_buckets == (2 << CTZ(required_buckets)));
#endif

        //assert(num_buckets > _num_filled);
        auto old_num_filled  = _num_filled;
        auto new_pairs = (PairT*)malloc((2 + num_buckets) * sizeof(PairT) + num_buckets / 8 + BIT_PACK);
#if EMH_EXCEPT
        if (EMH_UNLIKELY(!new_pairs))
            throw std::bad_alloc();
#else
        assert(!!new_pairs);
#endif

        auto old_pairs = _pairs;

        _bitmask = (size_type*)(new_pairs + 2 + num_buckets);
        const auto bitmask_pack = ((size_t)_bitmask) % sizeof(uint64_t);
        if (bitmask_pack != 0) {
            _bitmask = (size_type*)((char*)_bitmask + sizeof(uint64_t) - bitmask_pack);
            assert(0 == ((size_t)_bitmask) % sizeof(uint64_t));
        }

        _num_filled  = 0;
        _mask        = num_buckets - 1;
        _pairs       = new_pairs;

#if EMH_SAFE_HASH
        if (old_num_filled > 100 && _hash_inter == 0)
            _hash_inter = old_num_filled / (_num_main * 3);
        _num_main = 0;
#endif

        for (size_type bucket = 0; bucket < num_buckets; bucket++) {
            EMH_ADDR(_pairs, bucket) = INACTIVE;
#if EMH_FIND_HIT
            if constexpr (std::is_integral<KeyT>::value)
                EMH_KEY(_pairs, bucket) = 0;
#endif
        }

        reset_zero();

        //pack tail two tombstones for fast iterator and find empty_bucket without checking overflow
        memset((char*)(_pairs + num_buckets), 0, sizeof(PairT) * 2);

        /***************** init bitmask ---------------------- ***********/
        memset(_bitmask, 0xFFFFFFFF, num_buckets / 8);
        memset((char*)_bitmask + num_buckets / 8, 0, sizeof(uint64_t) + sizeof(uint8_t));
        //pack last position to bit 0
        /**************** -------------------------------- *************/

#if EMH_REHASH_LOG
        auto collision = 0;
#endif
        for (size_type src_bucket = 0; _num_filled < old_num_filled; src_bucket++) {
            if (EMH_EMPTY(old_pairs, src_bucket))
                continue;

            auto&& key = EMH_KEY(old_pairs, src_bucket);
            const auto bucket = find_unique_bucket(key);
            EMH_NEW(std::move(key), std::move(EMH_VAL(old_pairs, src_bucket)), bucket / 2, bucket);
#if EMH_REHASH_LOG
            if (bucket / 2 != hash_main(bucket / 2))
                collision++;
#endif
            if (is_triviall_destructable())
                old_pairs[src_bucket].~PairT();
        }

#if EMH_REHASH_LOG
        if (_num_filled > EMH_REHASH_LOG) {
#ifndef EMH_SAFE_HASH
            auto _num_main = old_num_filled - collision;
#endif
            const auto num_buckets = _mask + 1;
            auto last = EMH_ADDR(_pairs, num_buckets);
            char buff[255] = {0};
            sprintf(buff, "    _num_filled/aver_size/K.V/pack/collision|last = %u/%.2lf/%s.%s/%zd/%.2lf%%|%.2lf%%",
                    _num_filled,(double)_num_filled / _num_main, typeid(KeyT).name(), typeid(ValueT).name(), sizeof(_pairs[0]), (collision * 100.0 / num_buckets), (last * 100.0 / num_buckets));
#ifdef EMH_LOG
            static size_type ihashs = 0;
            EMH_LOG() << "EMH_BUCKET_INDEX = " << EMH_BUCKET_INDEX << "|rhash_nums = " << ihashs ++ << "|" <<__FUNCTION__ << "|" << buff << endl;
#else
            puts(buff);
#endif
        }
#endif

        free(old_pairs);
        assert(old_num_filled == _num_filled);
    }

private:
    // Can we fit another element?
    inline bool check_expand_need()
    {
#if EMH_SAFE_HASH > 1
        if (EMH_UNLIKELY(_num_main * 3 < _num_filled) && _num_filled > 100 && _hash_inter == 0) {
            rehash(_num_filled);
            return true;
        }
#endif
        return reserve(_num_filled);
    }

    void reset_bucket(size_type bucket)
    {
#if EMH_FIND_HIT
        if constexpr (std::is_integral<KeyT>::value) {
            auto& key = EMH_KEY(_pairs, bucket);
            key = 0;
            while ((hash_key(key) & _mask) == bucket)
                key ++;
        }
#endif
    }

    void clear_bucket(size_type bucket)
    {
        reset_bucket(bucket);

        EMH_ADDR(_pairs, bucket) = INACTIVE; //loop call in destructor
        if (is_triviall_destructable()) {
            _pairs[bucket].~PairT();
            EMH_ADDR(_pairs, bucket) = INACTIVE;
        }

        EMH_CLS(bucket);
        _num_filled--;
    }

    template<typename UType, typename std::enable_if<std::is_integral<UType>::value, size_type>::type = 0>
    size_type erase_key(const UType& key)
    {
        const auto empty_bucket = INACTIVE;
        const auto bucket = hash_key(key) & _mask;
        auto next_bucket = EMH_ADDR(_pairs, bucket);

        if (next_bucket == bucket * 2) {
            const auto eqkey = _eq(key, EMH_KEY(_pairs, bucket));
#if EMH_SAFE_HASH
            return eqkey ? (_num_main --, bucket) : empty_bucket;
#else
            return eqkey ? bucket : empty_bucket;
#endif
        }
        else if (next_bucket % 2 > 0)
            return empty_bucket;
        else if (_eq(key, EMH_KEY(_pairs, bucket))) {
            next_bucket /= 2;
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (is_copy_trivially())
                EMH_PKV(_pairs, bucket) = EMH_PKV(_pairs, next_bucket);
            else
                EMH_PKV(_pairs, bucket).swap(EMH_PKV(_pairs, next_bucket));
            EMH_ADDR(_pairs, bucket) = (next_bucket == nbucket ? bucket : nbucket) * 2;
            return next_bucket;
        }

        next_bucket /= 2;
        auto prev_bucket = bucket;
        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (_eq(key, EMH_KEY(_pairs, next_bucket))) {
                EMH_ADDR(_pairs, prev_bucket) = (nbucket == next_bucket ? prev_bucket : nbucket) * 2 + (1 - (prev_bucket == bucket));
                return next_bucket;
            }

            if (nbucket == next_bucket)
                break;
            prev_bucket = next_bucket;
            next_bucket = nbucket;
        }

        return empty_bucket;
    }

    template<typename UType, typename std::enable_if<!std::is_integral<UType>::value, size_type>::type = 0>
    size_type erase_key(const UType& key)
    {
        const auto empty_bucket = INACTIVE;
        const auto bucket = hash_key(key) & _mask;
        auto next_bucket = EMH_ADDR(_pairs, bucket);

        if (next_bucket == bucket * 2) { //only one main bucket
            const auto eqkey = _eq(key, EMH_KEY(_pairs, bucket));
#if EMH_SAFE_HASH
            return eqkey ? (_num_main --, bucket) : empty_bucket;
#else
            return eqkey ? bucket : empty_bucket;
#endif
        }
        else if (next_bucket % 2 > 0)
            return empty_bucket;

        //find erase key and swap to last bucket
        size_type prev_bucket = bucket, find_bucket = empty_bucket;
        next_bucket = bucket;
        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (_eq(key, EMH_KEY(_pairs, next_bucket))) {
                find_bucket = next_bucket;
                if (nbucket == next_bucket) {
                    EMH_ADDR(_pairs, prev_bucket) = prev_bucket * 2 + 1 - (prev_bucket == bucket);
                    break;
                }
            }
            if (nbucket == next_bucket) {
                if ((int)find_bucket >= 0) {
                    EMH_PKV(_pairs, find_bucket).swap(EMH_PKV(_pairs, nbucket));
//                    EMH_PKV(_pairs, find_bucket) = EMH_PKV(_pairs, nbucket);
                    EMH_ADDR(_pairs, prev_bucket) = prev_bucket * 2 + 1 - (prev_bucket == bucket);
                    find_bucket = nbucket;
                }
                break;
            }
            prev_bucket = next_bucket;
            next_bucket = nbucket;
        }

        return find_bucket;
    }

    size_type erase_bucket(const size_type bucket)
    {
        auto next_bucket = EMH_ADDR(_pairs, bucket);
        if (next_bucket == bucket * 2) {
#if EMH_SAFE_HASH
            _num_main--;
#endif
            return bucket;
        }
        else if (next_bucket % 2 == 0) {
            next_bucket /= 2;
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (is_copy_trivially())
                EMH_PKV(_pairs, bucket) = EMH_PKV(_pairs, next_bucket);
            else
                EMH_PKV(_pairs, bucket).swap(EMH_PKV(_pairs, next_bucket));
            EMH_ADDR(_pairs, bucket) = (next_bucket == nbucket ? bucket : nbucket) * 2;
            return next_bucket;
        }

        const auto main_bucket = hash_main(bucket);
        next_bucket /= 2;
        const auto prev_bucket = find_prev_bucket(main_bucket, bucket);
        const auto odd_bucket = (prev_bucket == main_bucket ? 0 : 1);
        if (bucket == next_bucket)
            EMH_ADDR(_pairs, prev_bucket) = prev_bucket * 2 + odd_bucket;
        else
            EMH_ADDR(_pairs, prev_bucket) = next_bucket * 2 + odd_bucket;
        return bucket;
    }

    // Find the bucket with this key, or return bucket size
    template<typename K>
    size_type find_filled_hash(const K& key, const size_t key_hash) const
    {
        const auto bucket = key_hash & _mask;
        auto next_bucket = EMH_ADDR(_pairs, bucket);
        const auto _num_buckets = _mask + 1;
#ifndef EMH_FIND_HIT
        if (next_bucket % 2 > 0)
            return _num_buckets;
        else if (_eq(key, EMH_KEY(_pairs, bucket)))
            return bucket;
        else if (next_bucket == bucket * 2)
            return _num_buckets;
#else
        if constexpr (std::is_integral<K>::value) {
            if (_eq(key, EMH_KEY(_pairs, bucket)))
                return bucket;
            else if (next_bucket % 2 > 0 || next_bucket == bucket * 2)
                return _num_buckets;
//            else if (hash_main(bucket) != bucket)
//                return _num_buckets;
        } else {
            if (next_bucket % 2 > 0)
                return _num_buckets;
            else if (_eq(key, EMH_KEY(_pairs, bucket)))
                return bucket;
            else if (next_bucket == bucket * 2)
                return _num_buckets;
        }
#endif

        next_bucket /= 2;
        while (true) {
            if (_eq(key, EMH_KEY(_pairs, next_bucket)))
                return next_bucket;

            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                return _num_buckets;
            next_bucket = nbucket;
        }

        return 0;
    }

    // Find the bucket with this key, or return bucket size
    //1. next_bucket = INACTIVE, empty bucket
    //2. next_bucket % 2 == 0 is main bucket
    template<typename Key=KeyT>
    inline size_type find_filled_bucket(const Key& key) const
    {
        return find_filled_hash(key, hash_key(key));
    }

    //kick out bucket and find empty to occpuy
    //it will break the orgin link and relnik again.
    //before: main_bucket-->prev_bucket --> bucket   --> next_bucket
    //atfer : main_bucket-->prev_bucket --> (removed)--> new_bucket--> next_bucket
    size_type kickout_bucket(const size_type bucket)
    {
        const auto next_bucket = EMH_BUCKET(_pairs, bucket);
        const auto new_bucket  = find_empty_bucket(next_bucket);
        const auto main_bucket = hash_main(bucket);
        const auto prev_bucket = find_prev_bucket(main_bucket, bucket);
        new(_pairs + new_bucket) PairT(std::move(_pairs[bucket])); EMH_SET(new_bucket);
        if (next_bucket == bucket)
            EMH_ADDR(_pairs, new_bucket) = new_bucket * 2 + 1;

        EMH_ADDR(_pairs, prev_bucket) += (new_bucket - bucket) * 2;
#if EMH_SAFE_HASH
        _num_main ++;
#endif
        clear_bucket(bucket); _num_filled ++;
        return bucket * 2;
    }

/***
** inserts a new key into a hash table; first check whether key's main
** bucket/position is free. If not, check whether colliding node/bucket is in its main
** position or not: if it is not, move colliding bucket to an empty place and
** put new key in its main position; otherwise (colliding bucket is in its main
** position), new key goes to an empty position. ***/

    template<typename Key>
    size_type find_or_allocate(const Key& key)
    {
        const auto bucket = hash_key(key) & _mask;
        auto next_bucket = EMH_ADDR(_pairs, bucket);
#if EMH_SAFE_HASH
        if ((int)next_bucket < 0)
            return _num_main ++, bucket * 2;
        else if (_eq(key, EMH_KEY(_pairs, bucket)))
            return bucket * 2;
#else
        if ((int)next_bucket < 0 || _eq(key, EMH_KEY(_pairs, bucket)))
            return bucket * 2;
#endif

        //check current bucket_key is in main bucket or not
        if (next_bucket == bucket * 2)
            return (EMH_ADDR(_pairs, bucket) = find_empty_bucket(bucket) * 2) + 1;
        else if (next_bucket % 2 > 0)
            return kickout_bucket(bucket);

        int collisions = 2;
        next_bucket /= 2;
        //find next linked bucket and check key
        while (true) {
            if (_eq(key, EMH_KEY(_pairs, next_bucket))) {
#if EMH_LRU_SET
                EMH_PKV(_pairs, next_bucket).swap(EMH_PKV(_pairs, bucket));
                return bucket * 2;
#else
                return next_bucket * 2;
#endif
            }

            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                break;
            next_bucket = nbucket;
            collisions++;
        }

        //find a new empty and link it to tail
        const auto new_bucket = find_empty_bucket(next_bucket);
        return EMH_ADDR(_pairs, next_bucket) = new_bucket * 2 + 1;
    }

    // key is not in this map. Find a place to put it.
    size_type find_empty_bucket(const size_type bucket_from)
    {
        const auto boset = bucket_from % 8;
        const auto begin = (uint8_t*)_bitmask + bucket_from / 8;
        const auto bmask = *(size_t*)begin >> boset;
        if (EMH_LIKELY(bmask != 0))
            return bucket_from + CTZ(bmask);

        const auto qmask = _mask / SIZE_BIT;
        if (1) {
            const auto step = (bucket_from + 2 * SIZE_BIT) & qmask;
            const auto bmask2 = *((size_t*)_bitmask + step);
            if (bmask2 != 0)
                return step * SIZE_BIT + CTZ(bmask2);
        }

        auto& _last = EMH_ADDR(_pairs, _mask + 1);
        for (; ;) {
            const auto bmask2 = *((size_t*)_bitmask + _last);
            if (bmask2 != 0)
                return _last * SIZE_BIT + CTZ(bmask2);

            const auto tail = (_last + qmask / 2) & qmask;
            const auto bmask1 = *((size_t*)_bitmask + tail);
            if (bmask1 != 0) {
                _last = tail;
                return tail * SIZE_BIT + CTZ(bmask1);
            }
            _last = ++_last & qmask;
        }
        return 0;
    }

    size_type find_last_bucket(size_type main_bucket) const
    {
        auto next_bucket = EMH_BUCKET(_pairs, main_bucket);
        if (next_bucket == main_bucket)
            return main_bucket;

        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                return next_bucket;
            next_bucket = nbucket;
        }
    }

    size_type find_prev_bucket(size_type main_bucket, const size_type bucket) const
    {
        auto next_bucket = EMH_BUCKET(_pairs, main_bucket);
        if (next_bucket == bucket)
            return main_bucket;

        while (true) {
            const auto nbucket = EMH_BUCKET(_pairs, next_bucket);
            if (nbucket == bucket)
                return next_bucket;
            next_bucket = nbucket;
        }
    }

    size_type find_unique_bucket(const KeyT& key)
    {
        const auto bucket = hash_key(key) & _mask;
        const auto next_bucket = EMH_ADDR(_pairs, bucket);
        if ((int)next_bucket < 0) {
#if EMH_SAFE_HASH
            _num_main ++;
#endif
            return bucket * 2;
        }

        //check current bucket_key is in main bucket or not
        if (next_bucket == bucket * 2)
            return (EMH_ADDR(_pairs, bucket) = find_empty_bucket(bucket) * 2) + 1;
        else if (next_bucket % 2 > 0)
            return kickout_bucket(bucket);

        const auto last_bucket = find_last_bucket(next_bucket / 2);
        //find a new empty and link it to tail
        return EMH_ADDR(_pairs, last_bucket) = find_empty_bucket(last_bucket) * 2 + 1;
    }

    static constexpr uint64_t KC = UINT64_C(11400714819323198485);
    static inline uint64_t hash64(uint64_t key)
    {
#if __SIZEOF_INT128__ && EMH_FIBONACCI_HASH == 1
        __uint128_t r = key; r *= KC;
        return (uint64_t)(r >> 64) + (uint64_t)r;
#elif EMH_FIBONACCI_HASH == 2
        //MurmurHash3Mixer
        uint64_t h = key;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccd;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53;
        h ^= h >> 33;
        return h;
#elif _WIN64 && EMH_FIBONACCI_HASH == 1
        uint64_t high;
        return _umul128(key, KC, &high) + high;
#elif EMH_FIBONACCI_HASH == 3
        auto ror  = (key >> 32) | (key << 32);
        auto low  = key * 0xA24BAED4963EE407ull;
        auto high = ror * 0x9FB21C651E98DF25ull;
        auto mix  = low + high;
        return mix;
#elif EMH_FIBONACCI_HASH == 1
        uint64_t r = key * UINT64_C(0xca4bcaa75ec3f625);
        return (r >> 32) + r;
#else
        uint64_t x = key;
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        x = x ^ (x >> 31);
        return x;
#endif
    }

    inline uint64_t hash_main(const size_type bucket) const
    {
        return hash_key(EMH_KEY(_pairs, bucket)) & _mask;
    }

    template<typename UType, typename std::enable_if<std::is_integral<UType>::value, size_type>::type = 0>
    inline uint64_t hash_key(const UType key) const
    {
#ifdef EMH_FIBONACCI_HASH
        return hash64(key);
#elif EMH_SAFE_HASH
        return _hash_inter == 0 ? _hasher(key) : hash64(key);
#elif EMH_IDENTITY_HASH
        return key + (key >> (sizeof(UType) * 4));
#elif EMH_WYHASH64
        return wyhash64(key, KC);
#else
        return _hasher(key);
#endif
    }

    template<typename UType, typename std::enable_if<std::is_same<UType, std::string>::value, size_type>::type = 0>
    inline uint64_t hash_key(const UType& key) const
    {
#ifdef WYHASH_LITTLE_ENDIAN
        return wyhash(key.data(), key.size(), key.size());
#else
        return (size_type)_hasher(key);
#endif
    }

    template<typename UType, typename std::enable_if<!std::is_integral<UType>::value && !std::is_same<UType, std::string>::value, size_type>::type = 0>
    inline uint64_t hash_key(const UType& key) const
    {
#ifdef EMH_FIBONACCI_HASH
        return _hasher(key) * KC;
#else
        return _hasher(key);
#endif
    }

private:
    PairT*    _pairs;
    size_type* _bitmask;
    //8 * 2 + 4 * 5 = 16 + 20 = 32
    HashT     _hasher;
    EqT       _eq;
    size_type _mask;
    size_type _num_filled;
    size_type _mlf;

#if EMH_SAFE_HASH
    size_type _num_main;
    size_type _hash_inter;
#endif
};
} // namespace emhash
#if __cplusplus >= 201103L
//template <class Key, class Val> using emihash = emhash2::HashMap<Key, Val, std::hash<Key>>;
#endif



// emhash6::HashMap for C++11
// version 1.6.5
// https://github.com/ktprime/ktprime/blob/master/hash_table6.hpp
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019-2019 Huang Yuanbing & bailuzhou AT 163.com
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


// From
// NUMBER OF PROBES / LOOKUP       Successful            Unsuccessful
// Quadratic collision resolution   1 - ln(1-L) - L/2    1/(1-L) - L - ln(1-L)
// Linear collision resolution     [1+1/(1-L)]/2         [1+1/(1-L)2]/2
//
// -- enlarge_factor --           0.10  0.50  0.60  0.75  0.80  0.90  0.99
// QUADRATIC COLLISION RES.
//    probes/successful lookup    1.05  1.44  1.62  2.01  2.21  2.85  5.11
//    probes/unsuccessful lookup  1.11  2.19  2.82  4.64  5.81  11.4  103.6
// LINEAR COLLISION RES.
//    probes/successful lookup    1.06  1.5   1.75  2.5   3.0   5.5   50.5
//    probes/unsuccessful lookup  1.12  2.5   3.6   8.5   13.0  50.0

#pragma once

#include <cstring>
#include <cstdlib>
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstdint>
#include <functional>
#include <iterator>

#if EMHASH_TAF_LOG
    #include "servant/AutoLog.h"
    #include "servant/RollLogHelper.h"
#endif

#ifdef GET_KEY
    #undef  GET_KEY
    #undef  GET_VAL
    #undef  NEXT_BUCKET
    #undef  GET_PKV
#endif

// likely/unlikely
#if (__GNUC__ >= 4 || __clang__)
#    define EMHASH_LIKELY(condition)   __builtin_expect(condition, 1)
#    define EMHASH_UNLIKELY(condition) __builtin_expect(condition, 0)
#else
#    define EMHASH_LIKELY(condition)   condition
#    define EMHASH_UNLIKELY(condition) condition
#endif

#ifndef EMHASH_BUCKET_INDEX
    #define EMHASH_BUCKET_INDEX 1
#endif
#if EMHASH_CACHE_LINE_SIZE < 32
    #define EMHASH_CACHE_LINE_SIZE 64
#endif

#if EMHASH_BUCKET_INDEX == 0
    #define GET_KEY(p,n)     p[n].second.first
    #define GET_VAL(p,n)     p[n].second.second
    #define NEXT_BUCKET(s,n) s[n].first / 2
    #define GET_PKV(s,n)     s[n].second
    #define NEW_BUCKET(key, value, bucket) new(_pairs + bucket) PairT(bucket, std::pair<KeyT, ValueT>(key, value)), _num_filled ++; SET_BIT(bucket)
#elif EMHASH_BUCKET_INDEX == 2
    #define GET_KEY(p,n)     p[n].first.first
    #define GET_VAL(p,n)     p[n].first.second
    #define NEXT_BUCKET(s,n) s[n].second / 2
    #define ADDR_BUCKET(s,n) s[n].second
    #define ISEMPTY_BUCKET(s,n) (int)s[n].second < 0
    #define GET_PKV(s,n)     s[n].first
    #define NEW_BUCKET(key, value, bucket, next) new(_pairs + bucket) PairT(std::pair<KeyT, ValueT>(key, value), next), _num_filled ++; SET_BIT(bucket)
#else
    #define GET_KEY(p,n)     p[n].first
    #define GET_VAL(p,n)     p[n].second
    #define NEXT_BUCKET(s,n) s[n].bucket / 2
    #define ADDR_BUCKET(s,n) s[n].bucket
    #define ISEMPTY_BUCKET(s,n) 0 > (int)s[n].bucket
    #define GET_PKV(s,n)     s[n]
    #define NEW_BUCKET(key, value, bucket, next) new(_pairs + bucket) PairT(key, value, next), _num_filled ++; SET_BIT(bucket)
#endif

#define MASK_BIT         32
#define MASK_N(n)        1 << (n % MASK_BIT)
#define SET_BIT(bucket)  _bitmask[bucket / MASK_BIT] &= ~(MASK_N(bucket))
#define CLS_BIT(bucket)  _bitmask[bucket / MASK_BIT] |= MASK_N(bucket)
#define IS_SET(bucket)   _bitmask[bucket / MASK_BIT] & (MASK_N(bucket))

#if _WIN32 || _MSC_VER > 1400
    #include <intrin.h>
#endif

namespace emhash6 {

constexpr uint32_t INACTIVE = (0 - 1u);
constexpr uint32_t BIT_PACK = sizeof(uint64_t) * 2 + sizeof(uint8_t);
static_assert(INACTIVE % 2 == 1, "INACTIVE must be even");

inline static uint32_t CTZ(const uint64_t n)
{
#if _MSC_VER > 1400 || _WIN32
    unsigned long index;
#if __x86_64__ || __amd64__ || _M_X64
    _BitScanForward64(&index, n);
#else
    _BitScanForward(&index, n);
#endif
#elif __GNUC__ || __clang__
    uint32_t index = __builtin_ctzll(n);
#elif 1
    uint64_t index;
#if __GNUC__ || __clang__
    __asm__ ("bsfq %1, %0\n" : "=r" (index) : "rm" (n) : "cc");
#else
    __asm
    {
        bsfq eax, n
        mov index, eax
    }
#endif
#endif

    return (uint32_t)index;
}

template <typename First, typename Second>
struct entry {

    entry(const First& key, const Second& value, uint32_t ibucket) :second(value),first(key) { bucket = ibucket; }
    entry(First&& key, Second&& value, uint32_t ibucket) :second(std::move(value)), first(std::move(key)) { bucket = ibucket; }

    entry(const std::pair<First,Second>& pair) :second(pair.second),first(pair.first) { bucket = INACTIVE; }
    entry(std::pair<First, Second>&& pair) :second(std::move(pair.second)),first(std::move(pair.first)) { bucket = INACTIVE; }

    entry(const entry& pairT) :second(pairT.second),first(pairT.first) { bucket = pairT.bucket; }
    entry(entry&& pairT) :second(std::move(pairT.second)),first(std::move(pairT.first)) { bucket = pairT.bucket; }

    entry& operator = (entry&& pairT)
    {
        second = std::move(pairT.second);
        bucket = pairT.bucket;
        first = std::move(pairT.first);
        return *this;
    }

    entry& operator = (entry& o)
    {
        second = o.second;
        bucket = o.bucket;
        first  = o.first;
        return *this;
    }

    void swap(entry<First, Second>& o)
    {
        std::swap(second, o.second);
        std::swap(first, o.first);
    }

    Second second;//int
    uint32_t bucket;
    First first; //long
};// __attribute__ ((packed));

/// A cache-friendly hash table with open addressing, linear/qua probing and power-of-two capacity
template <typename KeyT, typename ValueT, typename HashT = std::hash<KeyT>, typename EqT = std::equal_to<KeyT>>
class HashMap
{
private:
    typedef HashMap<KeyT, ValueT, HashT, EqT> htype;

#if EMHASH_BUCKET_INDEX == 0
    typedef std::pair<KeyT, ValueT>          value_pair;
    typedef std::pair<uint32_t, value_pair > PairT;
#elif EMHASH_BUCKET_INDEX == 2
    typedef std::pair<KeyT, ValueT>         value_pair;
    typedef std::pair<value_pair, uint32_t> PairT;
#else
    typedef entry<KeyT, ValueT>            PairT;
    typedef entry<KeyT, ValueT>            value_pair;
#endif

public:
    typedef KeyT   key_type;
    typedef ValueT mapped_type;

    typedef size_t       size_type;
    typedef std::pair<KeyT,ValueT>        value_type;
    typedef PairT&       reference;
    typedef const PairT& const_reference;

    class iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef std::ptrdiff_t            difference_type;
        typedef value_pair                value_type;

        typedef value_pair*               pointer;
        typedef value_pair&               reference;

        iterator() { }
        iterator(htype* hash_map, uint32_t bucket) : _map(hash_map), _bucket(bucket) { }

        iterator& operator++()
        {
            this->goto_next_element();
            return *this;
        }

        iterator operator++(int)
        {
            auto old_index = _bucket;
            this->goto_next_element();
            return {_map, old_index};
        }

        reference operator*() const
        {
            return _map->GET_PKV(_pairs, _bucket);
        }

        pointer operator->() const
        {
            return &(_map->GET_PKV(_pairs, _bucket));
        }

        bool operator==(const iterator& rhs) const
        {
            return this->_bucket == rhs._bucket;
        }

        bool operator!=(const iterator& rhs) const
        {
            return this->_bucket != rhs._bucket;
        }

    private:
        void goto_next_element()
        {
            auto _bitmask = _map->_bitmask;
            do {
                _bucket++;
            } while (IS_SET(_bucket));
        }

    public:
        htype* _map;
        uint32_t _bucket;
    };

    class const_iterator
    {
    public:
        typedef std::forward_iterator_tag iterator_category;
        typedef std::ptrdiff_t            difference_type;
        typedef value_pair                value_type;

        typedef value_pair*               pointer;
        typedef value_pair&               reference;

        const_iterator() { }
        const_iterator(const iterator& proto) : _map(proto._map), _bucket(proto._bucket) { }
        const_iterator(const htype* hash_map, uint32_t bucket) : _map(hash_map), _bucket(bucket) { }

        const_iterator& operator++()
        {
            this->goto_next_element();
            return *this;
        }

        const_iterator operator++(int)
        {
            auto old_index = _bucket;
            this->goto_next_element();
            return {_map, old_index};
        }

        reference operator*() const
        {
            return _map->GET_PKV(_pairs, _bucket);
        }

        pointer operator->() const
        {
            return &(_map->GET_PKV(_pairs, _bucket));
        }

        bool operator==(const const_iterator& rhs) const
        {
            return this->_bucket == rhs._bucket;
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return this->_bucket != rhs._bucket;
        }

    private:
        void goto_next_element()
        {
            auto _bitmask = _map->_bitmask;
            do {
                _bucket++;
            } while (IS_SET(_bucket));
        }

    public:
        const htype* _map;
        uint32_t _bucket;
    };

    void init(uint32_t bucket)
    {
        _num_buckets = 0;
        _num_main = 0;
        _mask = 0;
        _pairs = nullptr;
        _bitmask = nullptr;
        _num_filled = 0;
        _hash_inter = 0;
        max_load_factor(0.95f);
        reserve(bucket);
    }

    HashMap(uint32_t bucket = 4)
    {
        init(bucket);
    }

    HashMap(const HashMap& other)
    {
        _pairs = (PairT*)malloc((2 + other._num_buckets) * sizeof(PairT) + other._num_buckets / 8 + BIT_PACK);
        clone(other);
    }

    HashMap(HashMap&& other)
    {
        //init(0);
        //*this = std::move(other);
        _num_filled = 0;
        _pairs = nullptr;
        this->swap(other);
    }

    HashMap(std::initializer_list<std::pair<KeyT, ValueT>> il)
    {
        init((uint32_t)il.size());
        for (auto begin = il.begin(); begin != il.end(); ++begin)
            insert(*begin);
    }

    HashMap& operator=(const HashMap& other)
    {
        if (this == &other)
            return *this;

        if (is_notriviall_destructable())
            clearkv();

        if (_num_buckets != other._num_buckets) {
            free(_pairs);
            _pairs = (PairT*)malloc((2 + other._num_buckets) * sizeof(PairT) + other._num_buckets / 8 + BIT_PACK);
        }

        clone(other);
        return *this;
    }

    HashMap& operator=(HashMap&& other)
    {
        this->swap(other);
        return *this;
    }

    ~HashMap()
    {
        if (is_notriviall_destructable())
            clearkv();
        free(_pairs);
    }

    void clone(const HashMap& other)
    {
        _hasher      = other._hasher;
        _num_buckets = other._num_buckets;
        _num_main    = other._num_main;
        _num_filled  = other._num_filled;
        _mask        = other._mask;
        _loadlf      = other._loadlf;
        _hash_inter  = other._hash_inter;
        _bitmask     = (uint32_t*)((char*)_pairs + ((char*)other._bitmask - (char*)other._pairs));
        auto opairs  = other._pairs;

#if __cplusplus >= 201103L || _MSC_VER > 1600 || __clang__
        if (std::is_trivially_copyable<KeyT>::value && std::is_trivially_copyable<ValueT>::value)
#else
        if (std::is_pod<KeyT>::value && std::is_pod<ValueT>::value)
#endif
            memcpy(_pairs, opairs, _num_buckets * sizeof(PairT));
        else {
            for (uint32_t bucket = 0; bucket < _num_buckets; bucket++) {
                auto next_bucket = ADDR_BUCKET(_pairs, bucket) = ADDR_BUCKET(opairs, bucket);
                if ((int)next_bucket >= 0)
                    new(_pairs + bucket) PairT(opairs[bucket]);
            }
        }
        memcpy(_pairs + _num_buckets, opairs + _num_buckets, 2 * sizeof(PairT) + _num_buckets / 8 + BIT_PACK);
    }

    void swap(HashMap& other)
    {
        std::swap(_hasher, other._hasher);
        std::swap(_eq, other._eq);
        std::swap(_pairs, other._pairs);
        std::swap(_num_buckets, other._num_buckets);
        std::swap(_num_main, other._num_main);
        std::swap(_num_filled, other._num_filled);
        std::swap(_mask, other._mask);
        std::swap(_loadlf, other._loadlf);
        std::swap(_hash_inter, other._hash_inter);
        std::swap(_bitmask, other._bitmask);
    }

    // -------------------------------------------------------------

    iterator begin()
    {
        uint32_t bucket = 0;
        while (IS_SET(bucket)) {
            ++bucket;
        }
        return {this, bucket};
    }

    const_iterator cbegin() const
    {
        uint32_t bucket = 0;
        while (IS_SET(bucket)) {
            ++bucket;
        }
        return {this, bucket};
    }

    const_iterator begin() const
    {
        return cbegin();
    }

    iterator end()
    {
        return {this, _num_buckets};
    }

    const_iterator cend() const
    {
        return {this, _num_buckets};
    }

    const_iterator end() const
    {
        return {this, _num_buckets};
    }

    size_type size() const
    {
        return _num_filled;
    }

    bool empty() const
    {
        return _num_filled == 0;
    }

    // Returns the number of buckets.
    size_type bucket_count() const
    {
        return _num_buckets;
    }

    /// Returns average number of elements per bucket.
    float load_factor() const
    {
        return static_cast<float>(_num_filled) / (_num_buckets + 1);
    }

    HashT& hash_function() const
    {
        return _hasher;
    }

    EqT& key_eq() const
    {
        return _eq;
    }

    constexpr float max_load_factor() const
    {
        return (1 << 17) / (float)_loadlf;
    }

    void max_load_factor(float value)
    {
        if (value < 0.995f && value > 0.2f)
            _loadlf = (uint32_t)((1 << 17) / value);
    }

    constexpr size_type max_size() const
    {
        return (1 << 31) / sizeof(PairT);
    }

    constexpr size_type max_bucket_count() const
    {
        return (1 << 31) / sizeof(PairT);
    }

#ifdef EMHASH_STATIS
    //Returns the bucket number where the element with key k is located.
    size_type bucket(const KeyT& key) const
    {
        const auto bucket = hash_bucket(key);
        const auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if ((int)next_bucket < 0)
            return 0;
        else if (bucket == 2 * next_bucket)
            return bucket + 1;

        const auto& bucket_key = GET_KEY(_pairs, bucket);
        return hash_bucket(bucket_key) + 1;
    }

    //Returns the number of elements in bucket n.
    size_type bucket_size(const size_type bucket) const
    {
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if ((int)next_bucket < 0)
            return 0;

        const auto& bucket_key = GET_KEY(_pairs, bucket);
        next_bucket = hash_bucket(bucket_key);
        uint32_t ibucket_size = 1;

        //iterator each item in current main bucket
        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket) {
                break;
            }
            ibucket_size ++;
            next_bucket = nbucket;
        }
        return ibucket_size;
    }

    size_type get_main_bucket(const uint32_t bucket) const
    {
        if (ISEMPTY_BUCKET(_pairs, bucket))
            return -1u;

        const auto& bucket_key = GET_KEY(_pairs, bucket);
        const auto main_bucket = hash_bucket(bucket_key);
        return main_bucket;
    }

    int get_cache_info(uint32_t bucket, uint32_t next_bucket) const
    {
        auto pbucket = reinterpret_cast<size_t>(&_pairs[bucket]);
        auto pnext   = reinterpret_cast<size_t>(&_pairs[next_bucket]);
        if (pbucket / 64 == pnext / 64)
            return 0;
        auto diff = pbucket > pnext ? (pbucket - pnext) : pnext - pbucket;
        if (diff < 127 * 64)
            return diff / 64 + 1;
        return 127;
    }

    int get_bucket_info(const uint32_t bucket, uint32_t steps[], const uint32_t slots) const
    {
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if ((int)next_bucket < 0)
            return -1;

        const auto& bucket_key = GET_KEY(_pairs, bucket);
        const auto main_bucket = hash_bucket(bucket_key);
        if (main_bucket != bucket)
            return 0;
        else if (next_bucket == bucket)
            return 1;

        steps[get_cache_info(bucket, next_bucket) % slots] ++;
        uint32_t ibucket_size = 2;
        //find a new empty and linked it to tail
        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                break;

            steps[get_cache_info(nbucket, next_bucket) % slots] ++;
            ibucket_size ++;
            next_bucket = nbucket;
        }
        return ibucket_size;
    }

    void dump_statis() const
    {
        uint32_t buckets[129] = {0};
        uint32_t steps[129]   = {0};
        for (uint32_t bucket = 0; bucket < _num_buckets; ++bucket) {
            auto bsize = get_bucket_info(bucket, steps, 128);
            if (bsize > 0)
                buckets[bsize] ++;
        }

        uint32_t sumb = 0, collision = 0, sumc = 0, finds = 0, sumn = 0;
        puts("============== buckets size ration =========");
        for (uint32_t i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++) {
            const auto bucketsi = buckets[i];
            if (bucketsi == 0)
                continue;
            sumb += bucketsi;
            sumn += bucketsi * i;
            collision += bucketsi * (i - 1);
            finds += bucketsi * i * (i + 1) / 2;
            printf("  %2u  %8u  %.2lf  %.2lf\n", i, bucketsi, bucketsi * 100.0 * i / _num_filled, sumn * 100.0 / _num_filled);
        }

        puts("========== collision miss ration ===========");
        for (uint32_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            sumc += steps[i];
            if (steps[i] <= 2)
                continue;
            printf("  %2u  %8u  %.2lf  %.2lf\n", i, steps[i], steps[i] * 100.0 / collision, sumc * 100.0 / collision);
        }

        if (sumb == 0)  return;
        printf("    _num_filled/bucket_size/packed collision/cache_miss/hit_find = %u/%.2lf/%zd/ %.2lf%%/%.2lf%%/%.2lf\n",
                _num_filled, _num_filled * 1.0 / sumb, sizeof(PairT), (collision * 100.0 / _num_filled), (collision - steps[0]) * 100.0 / _num_filled, finds * 1.0 / _num_filled);
        assert(sumn == _num_filled);
        assert(sumc == collision);
    }
#endif

    // ------------------------------------------------------------

    iterator find(const KeyT& key) noexcept
    {
        return {this, find_filled_bucket(key)};
    }

    const_iterator find(const KeyT& key) const noexcept
    {
        return {this, find_filled_bucket(key)};
    }

    bool contains(const KeyT& key) const noexcept
    {
        return find_filled_bucket(key) != _num_buckets;
    }

    size_type count(const KeyT& key) const noexcept
    {
        return (size_type)(find_filled_bucket(key) != _num_buckets);
    }

    std::pair<iterator, iterator> equal_range(const KeyT & key)
    {
        iterator found = find(key);
        if (found == end())
            return { found, found };
        else
            return { found, std::next(found) };
    }

    /// Returns the matching ValueT or nullptr if k isn't found.
    bool try_get(const KeyT& key, ValueT& val) const
    {
        const auto bucket = find_filled_bucket(key);
        const auto find = bucket != _num_buckets;
        if (find) {
            val = GET_VAL(_pairs, bucket);
        }
        return find;
    }

    /// Returns the matching ValueT or nullptr if k isn't found.
    ValueT* try_get(const KeyT& key) noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket == _num_buckets ? nullptr : &GET_VAL(_pairs, bucket);
    }

    /// Const version of the above
    ValueT* try_get(const KeyT& key) const noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket == _num_buckets ? nullptr : &GET_VAL(_pairs, bucket);
    }

    /// Convenience function.
    ValueT get_or_return_default(const KeyT& key) const noexcept
    {
        const auto bucket = find_filled_bucket(key);
        return bucket == _num_buckets ? ValueT() : GET_VAL(_pairs, bucket);
    }

    // -----------------------------------------------------

    /// Returns a pair consisting of an iterator to the inserted element
    /// (or to the element that prevented the insertion)
    /// and a bool denoting whether the insertion took place.
    std::pair<iterator, bool> insert(const KeyT& key, const ValueT& value)
    {
        check_expand_need();
        return do_insert(key, value);
    }

    std::pair<iterator, bool> insert(KeyT&& key, ValueT&& value)
    {
        check_expand_need();
        return do_insert(std::move(key), std::move(value));
    }

    std::pair<iterator, bool> insert(const KeyT& key, ValueT&& value)
    {
        check_expand_need();
        return do_insert(key, std::move(value));
    }

    std::pair<iterator, bool> insert(KeyT& key, const ValueT& value)
    {
        check_expand_need();
        return do_insert(std::move(key), value);
    }

    template<typename K, typename V>
    std::pair<iterator, bool> do_insert(K&& key, V&& value)
    {
        check_expand_need();
        const auto bucket = find_or_allocate(key);
        const auto next   = bucket / 2;
        const auto find = ISEMPTY_BUCKET(_pairs, next);
        if (find) {
            NEW_BUCKET(std::forward<K>(key), std::forward<V>(value), next, bucket);
        }
        return { {this, next}, find };
    }

    inline std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT>& p)
    {
        check_expand_need();
        return do_insert(p.first, p.second);
    }

    inline std::pair<iterator, bool> insert(std::pair<KeyT, ValueT>&& p)
    {
        check_expand_need();
        return do_insert(std::move(p.first), std::move(p.second));
    }

#if 0
    template <typename Iter>
    void insert(Iter begin, Iter end)
    {
        reserve(std::distance(begin, end) + _num_filled);
        for (; begin != end; ++begin) {
            emplace(*begin);
        }
    }

    void insert(std::initializer_list<value_type> ilist)
    {
        reserve(ilist.size() + _num_filled);
        for (auto begin = ilist.begin(); begin != end; ++begin) {
            emplace(*begin);
        }
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
#endif

    ///////////////////////////////////////////////////////////
    template <typename Iter>
    void insert_unique(Iter begin, Iter end)
    {
        reserve(std::distance(begin, end) + _num_filled);
        for (; begin != end; ++begin) {
            do_insert_unqiue(*begin);
        }
    }

    /// Same as above, but contains(key) MUST be false
    uint32_t insert_unique(KeyT&& key, ValueT&& value)
    {
        check_expand_need();
        return do_insert_unqiue(std::move(key), std::move(value));
    }

    uint32_t insert_unique(const KeyT& key, const ValueT& value)
    {
        check_expand_need();
        return do_insert_unqiue(key, value);
    }

    uint32_t insert_unique(std::pair<KeyT, ValueT>&& p)
    {
        check_expand_need();
        return do_insert_unqiue(std::move(p.first), std::move(p.second));
    }

    uint32_t insert_unique(const std::pair<KeyT, ValueT>& p)
    {
        check_expand_need();
        return do_insert_unqiue(p.first, p.second);
    }

    template<typename K, typename V>
    inline uint32_t do_insert_unqiue(K&& key, V&& value)
    {
        auto bucket = find_unique_bucket(key);
        NEW_BUCKET(std::forward<K>(key), std::forward<V>(value), bucket / 2, bucket);
        return bucket;
    }

    template <class... Args>
    inline std::pair<iterator, bool> emplace(Args&&... args)
    {
        check_expand_need();
        return do_insert(std::forward<Args>(args)...);
    }

    //no any optimize for position
    template <class... Args>
    iterator emplace_hint(const_iterator position, Args&&... args)
    {
        return insert(std::forward<Args>(args)...).first;
    }

    template<class... Args>
    std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args) { return insert(k, std::forward<Args>(args)...).first; }

    template<class... Args>
    std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args) { return insert(std::move(k), std::forward<Args>(args)...).first; }

    template <class... Args>
    inline std::pair<iterator, bool> emplace_unique(Args&&... args)
    {
        return insert_unique(std::forward<Args>(args)...);
    }

    uint32_t try_insert_mainbucket(const KeyT& key, const ValueT& value)
    {
        const auto bucket = hash_bucket(key);
        if (ISEMPTY_BUCKET(_pairs, bucket))
        {
            _num_main ++;
            NEW_BUCKET(key, value, bucket, bucket * 2);
            return bucket;
        }

        return -1u;
    }

    std::pair<iterator, bool> insert_or_assign(const KeyT&& key, ValueT&& value) { return insert(key, std::move(value)); }
    std::pair<iterator, bool> insert_or_assign(KeyT&& key, ValueT&& value) { return insert(std::move(key), std::move(value)); }

    ValueT& operator[](const KeyT& key)
    {
        auto bucket = find_or_allocate(key);
        auto next   = bucket / 2;
        /* Check if inserting a new value rather than overwriting an old entry */
        if (ISEMPTY_BUCKET(_pairs, next)) {
            if (EMHASH_UNLIKELY(check_expand_need())) {
                bucket = find_unique_bucket(key);
                next = bucket / 2;
            }

            NEW_BUCKET(key, std::move(ValueT()), next, bucket);
        }

        //bugs here if return local reference rehash happens
        return GET_VAL(_pairs, next);
    }

    ValueT& operator[](KeyT&& key)
    {
        auto bucket = find_or_allocate(key);
        auto next   = bucket / 2;
        if (ISEMPTY_BUCKET(_pairs, next)) {
            if (EMHASH_UNLIKELY(check_expand_need())) {
                bucket = find_unique_bucket(key);
                next = bucket / 2;
            }

            NEW_BUCKET(std::move(key), std::move(ValueT()), next, bucket);
        }

        return GET_VAL(_pairs, next);
    }

    // -------------------------------------------------------
    /// Erase an element from the hash table.
    /// return 0 if element was not found
    size_type erase(const KeyT& key)
    {
        const auto bucket = erase_key(key);
        if ((int)bucket < 0)
            return 0;

        clear_bucket(bucket);
        return 1;
    }

    //iterator erase(const_iterator begin_it, const_iterator end_it)

    iterator erase(const_iterator cit)
    {
        iterator it(this, cit._bucket);
        return erase(it);
    }

    /// Erase an element typedef an iterator.
    /// Returns an iterator to the next element (or end()).
    iterator erase(iterator it)
    {
        const auto bucket = erase_bucket(it._bucket);
        clear_bucket(bucket);
        //erase from main bucket, return main bucket as next
        return (bucket == it._bucket) ? ++it : it;
    }

    void _erase(const_iterator it)
    {
        const auto bucket = erase_bucket(it._bucket);
        clear_bucket(bucket);
    }

    static constexpr bool is_notriviall_destructable()
    {
#if __cplusplus >= 201103L || _MSC_VER > 1600 || __clang__
        return !(std::is_trivially_destructible<KeyT>::value && std::is_trivially_destructible<ValueT>::value);
#else
        return !(std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    static constexpr bool is_copy_trivially()
    {
#if __cplusplus >= 201103L || _MSC_VER > 1600 || __clang__
        return !(std::is_trivially_copy_assignable<KeyT>::value && std::is_trivially_copy_assignable<ValueT>::value);
#else
        return !(std::is_pod<KeyT>::value && std::is_pod<ValueT>::value);
#endif
    }

    void clearkv()
    {
        for (uint32_t bucket = 0; _num_filled > 0; ++bucket) {
            if (ISEMPTY_BUCKET(_pairs, bucket))
                continue;
            clear_bucket(bucket);
        }
    }

    /// Remove all elements, keeping full capacity.
    void clear()
    {
        if (is_notriviall_destructable() || sizeof(PairT) > EMHASH_CACHE_LINE_SIZE / 2 || _num_filled < _num_buckets / 4)
            clearkv();
        else {
            memset(_pairs, 0xFFFFFFFF, sizeof(_pairs[0]) * _num_buckets);
            memset(_bitmask, 0xFFFFFFFF, _num_buckets / 8);
            _bitmask[_num_buckets / MASK_BIT] &= (1 << _num_buckets % MASK_BIT) - 1;
        }
        _num_filled = 0;
        _num_main = 0;
    }

    void shrink_to_fit()
    {
        rehash(_num_filled);
    }

    /// Make room for this many elements
    bool reserve(uint64_t num_elems)
    {
        const auto required_buckets = (uint32_t)(num_elems * _loadlf >> 17);
        if (EMHASH_LIKELY(required_buckets < _mask))
            return false;

        rehash(required_buckets + 2);
        return true;
    }

    ///three ways may incr rehash: bad hash function, load_factor is high, or need shrink
    void rehash(uint32_t required_buckets)
    {
        if (required_buckets < _num_filled)
            return ;

        uint32_t num_buckets = _num_filled > 65536 ? (1u << 16) : 4u;
        while (num_buckets < required_buckets) { num_buckets *= 2; }

        //assert(num_buckets > _num_filled);
        auto old_num_buckets = _num_buckets;
        auto old_num_filled  = _num_filled;
        auto new_pairs = (PairT*)malloc((2 + num_buckets) * sizeof(PairT) + num_buckets / 8 + BIT_PACK);
#if 0
        if (EMHASH_UNLIKELY(!new_pairs))
            throw std::bad_alloc();
#else
        assert(!!new_pairs);
#endif

        auto old_pairs = _pairs;

        _bitmask = (uint32_t*)(new_pairs + 2 + num_buckets);
        const auto bitmask_pack = ((size_t)_bitmask) % sizeof(uint64_t);
        if (bitmask_pack != 0)
            _bitmask = (uint32_t*)((char*)_bitmask + sizeof(uint64_t) - bitmask_pack);
        assert(0 == ((size_t)_bitmask) % sizeof(uint64_t));

        _num_filled  = 0;
        _num_buckets = num_buckets;
        _mask        = num_buckets - 1;
        _pairs       = new_pairs;

#if EMHASH_SAFE_HASH
        if (_hash_inter == 0 && old_num_filled > 100) {
            //adjust hash function if bad hash function, alloc more memory
            uint32_t mbucket = 0;
            for (uint32_t src_bucket = 0; src_bucket < old_num_buckets; src_bucket++) {
                if (ADDR_BUCKET(old_pairs, src_bucket) % 2 == 0)
                    mbucket ++;
            }
            if (mbucket * 2 < old_num_filled) { _hash_inter = old_num_buckets / mbucket; }
        }
        _num_main = 0;
#endif

        if (sizeof(PairT) <= EMHASH_CACHE_LINE_SIZE / 2)
            memset(_pairs, 0xFFFFFFFF, sizeof(_pairs[0]) * num_buckets);
        else
            for (uint32_t bucket = 0; bucket < num_buckets; bucket++)
                ADDR_BUCKET(_pairs, bucket) = INACTIVE;
        memset(_pairs + num_buckets, 0, sizeof(PairT) * 2); //set two tombstones

        /***************** ----------------------**/
        memset(_bitmask, 0xFFFFFFFF, num_buckets / 8);
        memset((char*)_bitmask + num_buckets / 8, 0, sizeof(uint64_t) + 1);
        //pack last dword to bit 0
        _bitmask[num_buckets / MASK_BIT] &= (1 << num_buckets % MASK_BIT) - 1;
        /***************** ----------------------**/

        for (uint32_t src_bucket = 0; src_bucket < old_num_buckets; src_bucket++) {
            if (ISEMPTY_BUCKET(old_pairs, src_bucket))
                continue;

            auto& key = GET_KEY(old_pairs, src_bucket);
            const auto bucket = find_unique_bucket(key);
            NEW_BUCKET(std::move(key), std::move(GET_VAL(old_pairs, src_bucket)), bucket / 2, bucket);
            old_pairs[src_bucket].~PairT();
        }

#if EMHASH_REHASH_LOG
        if (_num_filled > 100) {
            uint32_t collision = _num_filled - _num_main;
            char buff[255] = {0};
            sprintf(buff, "    _num_filled/_hash_inter/aver_size/K.V/pack/collision = %u/%u/%.2lf/%s.%s/%zd/%.2lf%%",
                    _num_filled, _hash_inter, (double)_num_filled / _num_main, typeid(KeyT).name(), typeid(ValueT).name(), sizeof(_pairs[0]), (collision * 100.0 / _num_buckets));
#if EMHASH_TAF_LOG
            static uint32_t ihashs = 0;
            FDLOG() << "EMHASH_BUCKET_INDEX = " << EMHASH_BUCKET_INDEX << "|hash_nums = " << ihashs ++ << "|" <<__FUNCTION__ << "|" << buff << endl;
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
#if EMHASH_SAFE_HASH > 1
        if (_num_main * 2 < _num_filled && _num_filled > 100 && _hash_inter == 0) {
            rehash(_num_filled);
            return true;
        }
#endif
        return reserve(_num_filled);
    }

    void clear_bucket(uint32_t bucket)
    {
        ADDR_BUCKET(_pairs, bucket) = INACTIVE;
        _pairs[bucket].~PairT();
        _num_filled --;
        CLS_BIT(bucket);
    }

#if EMHASH_ERASE_SMALL
    uint32_t erase_key(const KeyT& key)
    {
        const auto bucket = hash_bucket(key);
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if (next_bucket % 2 > 0)
            return INACTIVE;

        const auto eqkey = _eq(key, GET_KEY(_pairs, bucket));
        if (next_bucket == bucket * 2) {
#if EMHASH_SAFE_HASH
            return eqkey ? (_num_main --, bucket) : INACTIVE;
#else
            return eqkey ? bucket : INACTIVE;
#endif
         } else if (eqkey) {
            next_bucket /= 2;
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            //if (is_copy_trivially())
            GET_PKV(_pairs, bucket).swap(GET_PKV(_pairs, next_bucket));
            //else
            //    GET_PKV(_pairs, bucket) = GET_PKV(_pairs, next_bucket);
            ADDR_BUCKET(_pairs, bucket) = next_bucket == nbucket ? bucket * 2 : nbucket * 2;
            return next_bucket;
        }

        next_bucket /= 2;
        auto prev_bucket = bucket;
        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (_eq(key, GET_KEY(_pairs, next_bucket))) {
                ADDR_BUCKET(_pairs, prev_bucket) = (nbucket == next_bucket ? prev_bucket * 2 : nbucket * 2) + (1 - (prev_bucket == bucket));
                return next_bucket;
            }

            if (nbucket == next_bucket)
                break;
            prev_bucket = next_bucket;
            next_bucket = nbucket;
        }

        return INACTIVE;
    }
#else
    uint32_t erase_key(const KeyT& key)
    {
        const auto empty_bucket = INACTIVE;
        const auto bucket = hash_bucket(key);
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if (next_bucket % 2 > 0)
            return empty_bucket;
        else if (next_bucket == bucket * 2) //only one main bucket
#if EMHASH_SAFE_HASH
            return _eq(key, GET_KEY(_pairs, bucket)) ? (_num_main --, bucket) : empty_bucket;
#else
            return _eq(key, GET_KEY(_pairs, bucket)) ? bucket : empty_bucket;
#endif

        //find erase key and swap to last bucket
        uint32_t prev_bucket = bucket, find_bucket = empty_bucket;
        next_bucket = bucket;
        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (_eq(key, GET_KEY(_pairs, next_bucket))) {
                find_bucket = next_bucket;
                if (nbucket == next_bucket) {
                    ADDR_BUCKET(_pairs, prev_bucket) = prev_bucket * 2 + 1 - (prev_bucket == bucket);
                    break;
                }
            }
            if (nbucket == next_bucket) {
                if ((int)find_bucket >= 0) {
                    GET_PKV(_pairs, find_bucket).swap(GET_PKV(_pairs, nbucket));
//                    GET_PKV(_pairs, find_bucket) = GET_PKV(_pairs, nbucket);
                    ADDR_BUCKET(_pairs, prev_bucket) = prev_bucket * 2 + 1 - (prev_bucket == bucket);
                    find_bucket = nbucket;
                }
                break;
            }
            prev_bucket = next_bucket;
            next_bucket = nbucket;
        }

        return find_bucket;
    }
#endif

    uint32_t erase_bucket(const uint32_t bucket)
    {
        const auto main_bucket = hash_bucket(GET_KEY(_pairs, bucket));
        auto next_bucket = NEXT_BUCKET(_pairs, bucket);
        if (bucket == next_bucket) { //erase the last bucket
            if (bucket != main_bucket) {
                const auto prev_bucket = find_prev_bucket(main_bucket, bucket);
                ADDR_BUCKET(_pairs, prev_bucket) = prev_bucket * 2 + 1 - (prev_bucket == main_bucket); //maybe only left main bucket
            }
#if EMHASH_SAFE_HASH
            else
                _num_main --;
#endif
            return bucket;
        }

        auto prev_bucket = bucket;
        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket) {
                GET_PKV(_pairs, bucket).swap(GET_PKV(_pairs, nbucket)); //swap the erase bucket to last bucket
                ADDR_BUCKET(_pairs, prev_bucket) = prev_bucket * 2 + (prev_bucket == main_bucket ? 0 : 1);
                return nbucket;
            }

            prev_bucket = next_bucket;
            next_bucket = nbucket;
        }
    }

    // Find the bucket with this key, or return bucket size
    //1. next_bucket = INACTIVE, empty bucket
    //2. next_bucket % 2 == 0 is main bucket
    uint32_t find_filled_bucket(const KeyT& key) const
    {
        const auto bucket = hash_bucket(key);
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);

        if (next_bucket % 2 > 0)
            return _num_buckets;
        else if (_eq(key, GET_KEY(_pairs, bucket)))
            return bucket;
        else if (next_bucket == bucket * 2)
            return _num_buckets;

        next_bucket /= 2;
        while (true) {
            if (_eq(key, GET_KEY(_pairs, next_bucket)))
                return next_bucket;

            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                break;
            next_bucket = nbucket;
        }

        return _num_buckets;
    }

    //kick out bucket and find empty to occpuy
    //it will break the orgin link and relnik again.
    //before: main_bucket-->prev_bucket --> bucket   --> next_bucket
    //atfer : main_bucket-->prev_bucket --> (removed)--> new_bucket--> next_bucket
    uint32_t kickout_bucket(const uint32_t bucket)
    {
        const auto main_bucket = hash_bucket(GET_KEY(_pairs, bucket));
        const auto prev_bucket = find_prev_bucket(main_bucket, bucket);
        const auto next_bucket = NEXT_BUCKET(_pairs, bucket);
        const auto new_bucket  = find_empty_bucket(next_bucket);

        ADDR_BUCKET(_pairs, prev_bucket) += (new_bucket - bucket) * 2;
        new(_pairs + new_bucket) PairT(std::move(_pairs[bucket])); SET_BIT(new_bucket);

        if (next_bucket == bucket)
            ADDR_BUCKET(_pairs, new_bucket) = new_bucket * 2 + 1;

#if EMHASH_SAFE_HASH
        _num_main ++;
#endif
        clear_bucket(bucket); _num_filled ++;
        return bucket * 2;
    }

/***
** inserts a new key into a hash table; first, check whether key's main
** bucket/position is free. If not, check whether colliding node/bucket is in its main
** position or not: if it is not, move colliding bucket to an empty place and
** put new key in its main position; otherwise (colliding bucket is in its main
** position), new key goes to an empty position. ***/

    uint32_t find_or_allocate(const KeyT& key)
    {
        auto bucket = hash_bucket(key);
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
#if EMHASH_SAFE_HASH
        if ((int)next_bucket < 0)
            return _num_main ++, bucket * 2;
        else if (_eq(key, GET_KEY(_pairs, bucket)))
            return bucket * 2;
#else
        if ((int)next_bucket < 0 || _eq(key, GET_KEY(_pairs, bucket)))
            return bucket * 2;
#endif

        //check current bucket_key is in main bucket or not
        if (next_bucket == bucket * 2) {
            return (ADDR_BUCKET(_pairs, bucket) = find_empty_bucket(bucket) * 2) + 1;
        } else if (next_bucket % 2 > 0) {
            return kickout_bucket(bucket);
        }

        next_bucket /= 2;
        //find next linked bucket and check key
        while (true) {
            if (_eq(key, GET_KEY(_pairs, next_bucket))) {
#if EMHASH_LRU_SET
                GET_PKV(_pairs, next_bucket).swap(GET_PKV(_pairs, bucket));
                return bucket * 2;
#else
                return next_bucket * 2;
#endif
            }

            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                break;
            next_bucket = nbucket;
        }

        //find a new empty and link it to tail
        const auto new_bucket = find_empty_bucket(next_bucket);
        return ADDR_BUCKET(_pairs, next_bucket) = new_bucket * 2 + 1;
    }

    uint32_t find_cacheline_bucket(const uint32_t bucket_from) const
    {
        //try find a near in the same cache line
        auto empty_bucket = 0;
        for (int i = 6; i > 2; i--) {
#if 1
            const auto near_bucket = (bucket_from + i) & _mask;
            const auto next_bucket = ADDR_BUCKET(_pairs, near_bucket);
            if (next_bucket % 2 == 0)
                continue;

            //assert(ADDR_BUCKET(_pairs, near_bucket) != INACTIVE);
            const auto main_bucket = hash_bucket(GET_KEY(_pairs, near_bucket));
            if (main_bucket + 10 > near_bucket)
                continue;

            const auto prev_bucket = find_prev_bucket(main_bucket, near_bucket);
            ADDR_BUCKET(_pairs, prev_bucket) += (empty_bucket - near_bucket) * 2;
            new(_pairs + empty_bucket) PairT(std::move(_pairs[near_bucket])); _pairs[near_bucket].~PairT();
            if (next_bucket / 2 == near_bucket)
                ADDR_BUCKET(_pairs, empty_bucket) = empty_bucket * 2 + 1;
            SET_BIT(empty_bucket);

            empty_bucket = near_bucket;
            ADDR_BUCKET(_pairs, near_bucket) = INACTIVE;
            break;
#endif
        }

        return empty_bucket;
    }

    // key is not in this map. Find a place to put it.
    uint32_t find_empty_bucket(const uint32_t bucket_from)
    {
        const auto bucket1 = bucket_from + 1;
        if (ISEMPTY_BUCKET(_pairs, bucket1))
            return bucket1;

#if BF
        const auto bucket2 = bucket_from + 2;
        if (ISEMPTY_BUCKET(_pairs, bucket2))
            return bucket2;

        uint32_t empty_bucket = 0;
        //12, 10, 7
        constexpr uint32_t rand_find = sizeof(_pairs[0]) < EMHASH_CACHE_LINE_SIZE / 2 ? (2 * EMHASH_CACHE_LINE_SIZE / sizeof(_pairs[0])) + 1 : 5;

        //fibonacci an2 = an1 + an0 --> 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, ...
        //for (uint32_t last = 2, slot = 3; ; slot += last, last = slot - last) {
        for (uint32_t last = 2, slot = 3; ; slot += ++last) {
            const auto next = (bucket_from + slot) & _mask;
            const auto bucket1 = next + 0, bucket2 = next + 1;
            if (ISEMPTY_BUCKET(_pairs, bucket1))
                return bucket1;

            else if (ISEMPTY_BUCKET(_pairs, bucket2))
                return bucket2;
#if BF > 1
            else if (slot > rand_find) {
                const auto next2 = (bucket_from + _num_filled + slot / 4) & _mask;
                const auto bucket3 = next2 + 0;
                if (ISEMPTY_BUCKET(_pairs, bucket3)) {
                    empty_bucket = bucket3;
                    break;
                }
            }
#endif
        }

        return empty_bucket;
#else
        const auto boset = bucket_from % 8;
        const auto bmask = *(uint64_t*)((uint8_t*)_bitmask + bucket_from / 8) >> boset;
        if (bmask != 0)
            return bucket_from + CTZ(bmask) - 0;

        const auto qmask = (64 + _num_buckets - 1) / 64 - 1;
#ifndef QS
        for (uint32_t last = 2, step = (bucket_from + _num_filled) & qmask; ;step = (step + ++last) & qmask) {
        //for (uint32_t last = qmask > 2 ? qmask / 2 + 2 : 3, step = (bucket_from + _num_filled) & qmask; ;step = (step + last) & qmask) {
#else
        for (uint32_t last = 2, step = (bucket_from + 2 * 64) & qmask; ; step = (step + ++last) & qmask) {
#endif
            const auto next2 = step;
            const auto bmask2 = *((uint64_t*)_bitmask + next2);
            if (bmask2 != 0)
                return next2 * 64 + CTZ(bmask2);

            const auto next = (next2 + 1) & qmask;
            const auto bmask = *((uint64_t*)_bitmask + next);
            if (bmask != 0)
                return next * 64 + CTZ(bmask);
        }

        return 0;
#endif
    }

    uint32_t find_last_bucket(uint32_t main_bucket) const
    {
        auto next_bucket = NEXT_BUCKET(_pairs, main_bucket);
        if (next_bucket == main_bucket)
            return main_bucket;

        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == next_bucket)
                return next_bucket;
            next_bucket = nbucket;
        }
    }

    uint32_t find_prev_bucket(uint32_t main_bucket, const uint32_t bucket) const
    {
        auto next_bucket = NEXT_BUCKET(_pairs, main_bucket);
        if (next_bucket == bucket)
            return main_bucket;

        while (true) {
            const auto nbucket = NEXT_BUCKET(_pairs, next_bucket);
            if (nbucket == bucket)
                return next_bucket;
            next_bucket = nbucket;
        }
    }

    uint32_t find_unique_bucket(const KeyT& key)
    {
        const auto bucket = hash_bucket(key);
        auto next_bucket = ADDR_BUCKET(_pairs, bucket);
        if ((int)next_bucket < 0) {
#if EMHASH_SAFE_HASH
            _num_main ++;
#endif
            return bucket * 2;
        }

        //check current bucket_key is in main bucket or not
        if (next_bucket == bucket * 2) {
            return (ADDR_BUCKET(_pairs, bucket) = find_empty_bucket(bucket) * 2) + 1;
        } else if (next_bucket % 2 > 0) {
            return kickout_bucket(bucket);
        }

        const auto last_bucket = find_last_bucket(next_bucket / 2);
        //find a new empty and link it to tail
        return ADDR_BUCKET(_pairs, last_bucket) = find_empty_bucket(last_bucket) * 2 + 1;
    }

    static inline uint32_t hash32(uint32_t key)
    {
#if 0
        key = ((key >> 16) ^ key) * 0x45d9f3b;
        key = ((key >> 16) ^ key) * 0x45d9f3b; //0x119de1f3
//        key = ((key >> 13) ^ key) * 0xc2b2ae35;
        key = (key >> 16) ^ key;
        return key;
#elif 1
        uint64_t const r = key * UINT64_C(2654435769);
        const uint32_t h = static_cast<uint32_t>(r >> 32);
        const uint32_t l = static_cast<uint32_t>(r);
        return h + l;
#elif 1
        key += ~(key << 15);
        key ^= (key >> 10);
        key += (key << 3);
        key ^= (key >> 6);
        key += ~(key << 11);
        key ^= (key >> 16);
        return key;
#endif
    }

    static inline uint64_t hash64(uint64_t key)
    {
#if __SIZEOF_INT128__ && _MPCLMUL
        //uint64_t const inline clmul_mod(const uint64_t& i,const uint64_t& j)
        __m128i I{}; I[0] ^= key;
        __m128i J{}; J[0] ^= UINT64_C(0xde5fb9d2630458e9);
        __m128i M{}; M[0] ^= 0xb000000000000000ull;
        __m128i X = _mm_clmulepi64_si128(I,J,0);
        __m128i A = _mm_clmulepi64_si128(X,M,0);
        __m128i B = _mm_clmulepi64_si128(A,M,0);
        return A[0]^A[1]^B[1]^X[0]^X[1];
#elif __SIZEOF_INT128__
        constexpr uint64_t k = UINT64_C(11400714819323198485);
        __uint128_t r = key; r *= k;
        return (uint32_t)(r >> 64) + (uint32_t)r;
#elif 1
        uint64_t const r = key * UINT64_C(0xca4bcaa75ec3f625);
        const uint32_t h = static_cast<uint32_t>(r >> 32);
        const uint32_t l = static_cast<uint32_t>(r);
        return h + l;
#elif 1
        //MurmurHash3Mixer
        uint64_t h = key;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccd;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53;
        h ^= h >> 33;
        return static_cast<size_t>(h);
#elif 1
        uint64_t x = key;
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        x = x ^ (x >> 31);
        return x;
#endif
    }

    template<typename UType, typename std::enable_if<std::is_integral<UType>::value, uint32_t>::type = 0>
    inline uint32_t hash_bucket(const UType key) const
    {
#ifdef EMHASH_FIBONACCI_HASH
        if (sizeof(UType) <= sizeof(uint32_t))
            return hash32(key) & _mask;
        else
            return uint32_t(hash64(key) & _mask);
#elif EMHASH_SAFE_HASH
        if (_hash_inter > 0) {
            if (sizeof(UType) <= sizeof(uint32_t))
                return hash32(key) & _mask;
            else
                return uint32_t(hash64(key) & _mask);
        }
        return _hasher(key) & _mask;
#elif EMHASH_IDENTITY_HASH
        return (key + (key >> 20)) & _mask;
#else
        return _hasher(key) & _mask;
#endif
    }

    template<typename UType, typename std::enable_if<!std::is_integral<UType>::value, uint32_t>::type = 0>
    inline uint32_t hash_bucket(const UType& key) const
    {
#ifdef EMHASH_FIBONACCI_HASH
        return (_hasher(key) * 11400714819323198485ull) & _mask;
#elif EMHASH_STD_STRING
        uint32_t hash = 0;
        if (key.size() < 32) {
            for (const auto c : key) hash = c + hash * 131;
        } else {
            for (int i = 0, j = 1; i < key.size(); i += j++)
                hash = key[i] + hash * 131;
        }
        return hash & _mask;
#else
        return _hasher(key) & _mask;
#endif
    }

private:
    //8 * 3 + 4 + 4 + 4 * 3 = 32 + 12 = 44
    HashT     _hasher;
    EqT       _eq;
    uint32_t  _mask;
    uint32_t  _num_buckets;
    //unchar  _cache_packed[128 / sizeof(char)];//packed no thread cache line share read

    uint32_t  _num_filled;
    uint32_t  _num_main;
    uint32_t  _hash_inter;
    uint32_t  _loadlf;
    PairT*    _pairs;
    uint32_t* _bitmask;
};
} // namespace emhash
#if __cplusplus >= 201103L
//template <class Key, class Val> using emihash = emhash2::HashMap<Key, Val, std::hash<Key>>;
#endif


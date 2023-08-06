// Copyright (C) 2022 Christian Mazakas
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_UNORDERED_UNORDERED_NODE_SET_HPP_INCLUDED
#define BOOST_UNORDERED_UNORDERED_NODE_SET_HPP_INCLUDED

#include <boost/minconfig.hpp>
#pragma once

#include <boost/unordered/detail/foa.hpp>
#include <boost/unordered/detail/foa/element_type.hpp>
#include <boost/unordered/detail/foa/node_handle.hpp>
#include <boost/unordered/detail/type_traits.hpp>
#include <boost/unordered/unordered_node_set_fwd.hpp>

#include <boost/container_hash/hash.hpp>
#include <boost/throw_exception.hpp>

#include <initializer_list>
#include <iterator>
#include <type_traits>
#include <utility>

namespace boost {
  namespace unordered {

#if defined(BOOST_MSVC)
#pragma warning(push)
#pragma warning(disable : 4714) /* marked as __forceinline not inlined */
#endif

    namespace detail {
      template <class Key> struct node_set_types
      {
        using key_type = Key;
        using init_type = Key;
        using value_type = Key;

        static Key const& extract(value_type const& key) { return key; }

        using element_type=foa::element_type<value_type>;

        static value_type& value_from(element_type const& x) { return *x.p; }
        static Key const& extract(element_type const& k) { return *k.p; }
        static element_type&& move(element_type& x) { return std::move(x); }
        static value_type&& move(value_type& x) { return std::move(x); }

        template <class A>
        static void construct(A& al, element_type* p, element_type const& copy)
        {
          construct(al, p, *copy.p);
        }

        template <typename Allocator>
        static void construct(
          Allocator&, element_type* p, element_type&& x) noexcept
        {
          p->p = x.p;
          x.p = nullptr;
        }

        template <class A, class... Args>
        static void construct(A& al, value_type* p, Args&&... args)
        {
          std::allocator_traits<A>::construct(al, p, std::forward<Args>(args)...);
        }

        template <class A, class... Args>
        static void construct(A& al, element_type* p, Args&&... args)
        {
          p->p = std::to_address(std::allocator_traits<A>::allocate(al, 1));
          BOOST_TRY
          {
            std::allocator_traits<A>::construct(al, p->p, std::forward<Args>(args)...);
          }
          BOOST_CATCH(...)
          {
            std::allocator_traits<A>::deallocate(al,
              std::pointer_traits<
                typename std::allocator_traits<A>::pointer>::pointer_to(*p->p),
              1);
            BOOST_RETHROW
          }
          BOOST_CATCH_END
        }

        template <class A> static void destroy(A& al, value_type* p) noexcept
        {
          std::allocator_traits<A>::destroy(al, p);
        }

        template <class A> static void destroy(A& al, element_type* p) noexcept
        {
          if (p->p) {
            destroy(al, p->p);
            std::allocator_traits<A>::deallocate(al,
              std::pointer_traits<typename std::allocator_traits<A>::pointer>::pointer_to(*(p->p)),
              1);
          }
        }
      };

      template <class TypePolicy, class Allocator>
      struct node_set_handle
          : public detail::foa::node_handle_base<TypePolicy, Allocator>
      {
      private:
        using base_type = detail::foa::node_handle_base<TypePolicy, Allocator>;

        using typename base_type::type_policy;

        template <class Key, class Hash, class Pred, class Alloc>
        friend class boost::unordered::unordered_node_set;

      public:
        using value_type = typename TypePolicy::value_type;

        constexpr node_set_handle() noexcept = default;
        node_set_handle(node_set_handle&& nh) noexcept = default;
        node_set_handle& operator=(node_set_handle&&) noexcept = default;

        value_type& value() const
        {
          BOOST_ASSERT(!this->empty());
          return const_cast<value_type&>(this->data());
        }
      };
    } // namespace detail

    template <class Key, class Hash, class KeyEqual, class Allocator>
    class unordered_node_set
    {
      using set_types = detail::node_set_types<Key>;

      using table_type = detail::foa::table<set_types, Hash, KeyEqual,
        typename std::allocator_traits<Allocator>::template rebind_alloc<
          typename set_types::value_type>>;

      table_type table_;

      template <class K, class H, class KE, class A, class Pred>
      typename unordered_node_set<K, H, KE, A>::size_type friend erase_if(
        unordered_node_set<K, H, KE, A>& set, Pred pred);

    public:
      using key_type = Key;
      using value_type = typename set_types::value_type;
      using init_type = typename set_types::init_type;
      using size_type = std::size_t;
      using difference_type = std::ptrdiff_t;
      using hasher = Hash;
      using key_equal = KeyEqual;
      using allocator_type = Allocator;
      using reference = value_type&;
      using const_reference = value_type const&;
      using pointer = typename std::allocator_traits<allocator_type>::pointer;
      using const_pointer =
        typename std::allocator_traits<allocator_type>::const_pointer;
      using iterator = typename table_type::iterator;
      using const_iterator = typename table_type::const_iterator;
      using node_type = detail::node_set_handle<set_types,
        typename std::allocator_traits<Allocator>::template rebind_alloc<
          typename set_types::value_type>>;
      using insert_return_type =
        detail::foa::insert_return_type<iterator, node_type>;

      unordered_node_set() : unordered_node_set(0) {}

      explicit unordered_node_set(size_type n, hasher const& h = hasher(),
        key_equal const& pred = key_equal(),
        allocator_type const& a = allocator_type())
          : table_(n, h, pred, a)
      {
      }

      unordered_node_set(size_type n, allocator_type const& a)
          : unordered_node_set(n, hasher(), key_equal(), a)
      {
      }

      unordered_node_set(size_type n, hasher const& h, allocator_type const& a)
          : unordered_node_set(n, h, key_equal(), a)
      {
      }

      template <class InputIterator>
      unordered_node_set(
        InputIterator f, InputIterator l, allocator_type const& a)
          : unordered_node_set(f, l, size_type(0), hasher(), key_equal(), a)
      {
      }

      explicit unordered_node_set(allocator_type const& a)
          : unordered_node_set(0, a)
      {
      }

      template <class Iterator>
      unordered_node_set(Iterator first, Iterator last, size_type n = 0,
        hasher const& h = hasher(), key_equal const& pred = key_equal(),
        allocator_type const& a = allocator_type())
          : unordered_node_set(n, h, pred, a)
      {
        this->insert(first, last);
      }

      template <class InputIt>
      unordered_node_set(
        InputIt first, InputIt last, size_type n, allocator_type const& a)
          : unordered_node_set(first, last, n, hasher(), key_equal(), a)
      {
      }

      template <class Iterator>
      unordered_node_set(Iterator first, Iterator last, size_type n,
        hasher const& h, allocator_type const& a)
          : unordered_node_set(first, last, n, h, key_equal(), a)
      {
      }

      unordered_node_set(unordered_node_set const& other) : table_(other.table_)
      {
      }

      unordered_node_set(
        unordered_node_set const& other, allocator_type const& a)
          : table_(other.table_, a)
      {
      }

      unordered_node_set(unordered_node_set&& other)
        noexcept(std::is_nothrow_move_constructible<hasher>::value&&
            std::is_nothrow_move_constructible<key_equal>::value&&
              std::is_nothrow_move_constructible<allocator_type>::value)
          : table_(std::move(other.table_))
      {
      }

      unordered_node_set(unordered_node_set&& other, allocator_type const& al)
          : table_(std::move(other.table_), al)
      {
      }

      unordered_node_set(std::initializer_list<value_type> ilist,
        size_type n = 0, hasher const& h = hasher(),
        key_equal const& pred = key_equal(),
        allocator_type const& a = allocator_type())
          : unordered_node_set(ilist.begin(), ilist.end(), n, h, pred, a)
      {
      }

      unordered_node_set(
        std::initializer_list<value_type> il, allocator_type const& a)
          : unordered_node_set(il, size_type(0), hasher(), key_equal(), a)
      {
      }

      unordered_node_set(std::initializer_list<value_type> init, size_type n,
        allocator_type const& a)
          : unordered_node_set(init, n, hasher(), key_equal(), a)
      {
      }

      unordered_node_set(std::initializer_list<value_type> init, size_type n,
        hasher const& h, allocator_type const& a)
          : unordered_node_set(init, n, h, key_equal(), a)
      {
      }

      ~unordered_node_set() = default;

      unordered_node_set& operator=(unordered_node_set const& other)
      {
        table_ = other.table_;
        return *this;
      }

      unordered_node_set& operator=(unordered_node_set&& other) noexcept(
        noexcept(std::declval<table_type&>() = std::declval<table_type&&>()))
      {
        table_ = std::move(other.table_);
        return *this;
      }

      allocator_type get_allocator() const noexcept
      {
        return table_.get_allocator();
      }

      /// Iterators
      ///

      iterator begin() noexcept { return table_.begin(); }
      const_iterator begin() const noexcept { return table_.begin(); }
      const_iterator cbegin() const noexcept { return table_.cbegin(); }

      iterator end() noexcept { return table_.end(); }
      const_iterator end() const noexcept { return table_.end(); }
      const_iterator cend() const noexcept { return table_.cend(); }

      /// Capacity
      ///

      [[nodiscard]] bool empty() const noexcept
      {
        return table_.empty();
      }

      size_type size() const noexcept { return table_.size(); }

      size_type max_size() const noexcept { return table_.max_size(); }

      /// Modifiers
      ///

      void clear() noexcept { table_.clear(); }

      BOOST_FORCEINLINE std::pair<iterator, bool> insert(
        value_type const& value)
      {
        return table_.insert(value);
      }

      BOOST_FORCEINLINE std::pair<iterator, bool> insert(value_type&& value)
      {
        return table_.insert(std::move(value));
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        detail::transparent_non_iterable<K, unordered_node_set>::value,
        std::pair<iterator, bool> >::type
      insert(K&& k)
      {
        return table_.try_emplace(std::forward<K>(k));
      }

      BOOST_FORCEINLINE iterator insert(const_iterator, value_type const& value)
      {
        return table_.insert(value).first;
      }

      BOOST_FORCEINLINE iterator insert(const_iterator, value_type&& value)
      {
        return table_.insert(std::move(value)).first;
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        detail::transparent_non_iterable<K, unordered_node_set>::value,
        iterator>::type
      insert(const_iterator, K&& k)
      {
        return table_.try_emplace(std::forward<K>(k)).first;
      }

      template <class InputIterator>
      void insert(InputIterator first, InputIterator last)
      {
        for (auto pos = first; pos != last; ++pos) {
          table_.emplace(*pos);
        }
      }

      void insert(std::initializer_list<value_type> ilist)
      {
        this->insert(ilist.begin(), ilist.end());
      }

      insert_return_type insert(node_type&& nh)
      {
        if (nh.empty()) {
          return {end(), false, node_type{}};
        }

        BOOST_ASSERT(get_allocator() == nh.get_allocator());

        auto itp = table_.insert(std::move(nh.element()));
        if (itp.second) {
          nh.reset();
          return {itp.first, true, node_type{}};
        } else {
          return {itp.first, false, std::move(nh)};
        }
      }

      iterator insert(const_iterator, node_type&& nh)
      {
        if (nh.empty()) {
          return end();
        }

        BOOST_ASSERT(get_allocator() == nh.get_allocator());

        auto itp = table_.insert(std::move(nh.element()));
        if (itp.second) {
          nh.reset();
          return itp.first;
        } else {
          return itp.first;
        }
      }

      template <class... Args>
      BOOST_FORCEINLINE std::pair<iterator, bool> emplace(Args&&... args)
      {
        return table_.emplace(std::forward<Args>(args)...);
      }

      template <class... Args>
      BOOST_FORCEINLINE iterator emplace_hint(const_iterator, Args&&... args)
      {
        return table_.emplace(std::forward<Args>(args)...).first;
      }

      BOOST_FORCEINLINE void erase(const_iterator pos)
      {
        return table_.erase(pos);
      }
      iterator erase(const_iterator first, const_iterator last)
      {
        while (first != last) {
          this->erase(first++);
        }
        return iterator{detail::foa::const_iterator_cast_tag{}, last};
      }

      BOOST_FORCEINLINE size_type erase(key_type const& key)
      {
        return table_.erase(key);
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        detail::transparent_non_iterable<K, unordered_node_set>::value,
        size_type>::type
      erase(K const& key)
      {
        return table_.erase(key);
      }

      void swap(unordered_node_set& rhs) noexcept(
        noexcept(std::declval<table_type&>().swap(std::declval<table_type&>())))
      {
        table_.swap(rhs.table_);
      }

      node_type extract(const_iterator pos)
      {
        BOOST_ASSERT(pos != end());
        node_type nh;
        auto elem = table_.extract(pos);
        nh.emplace(std::move(elem), get_allocator());
        return nh;
      }

      node_type extract(key_type const& key)
      {
        auto pos = find(key);
        return pos != end() ? extract(pos) : node_type();
      }

      template <class K>
      typename std::enable_if<
        boost::unordered::detail::transparent_non_iterable<K,
          unordered_node_set>::value,
        node_type>::type
      extract(K const& key)
      {
        auto pos = find(key);
        return pos != end() ? extract(pos) : node_type();
      }

      template <class H2, class P2>
      void merge(unordered_node_set<key_type, H2, P2, allocator_type>& source)
      {
        table_.merge(source.table_);
      }

      template <class H2, class P2>
      void merge(unordered_node_set<key_type, H2, P2, allocator_type>&& source)
      {
        table_.merge(std::move(source.table_));
      }

      /// Lookup
      ///

      BOOST_FORCEINLINE size_type count(key_type const& key) const
      {
        auto pos = table_.find(key);
        return pos != table_.end() ? 1 : 0;
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        detail::are_transparent<K, hasher, key_equal>::value, size_type>::type
      count(K const& key) const
      {
        auto pos = table_.find(key);
        return pos != table_.end() ? 1 : 0;
      }

      BOOST_FORCEINLINE iterator find(key_type const& key)
      {
        return table_.find(key);
      }

      BOOST_FORCEINLINE const_iterator find(key_type const& key) const
      {
        return table_.find(key);
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        boost::unordered::detail::are_transparent<K, hasher, key_equal>::value,
        iterator>::type
      find(K const& key)
      {
        return table_.find(key);
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        boost::unordered::detail::are_transparent<K, hasher, key_equal>::value,
        const_iterator>::type
      find(K const& key) const
      {
        return table_.find(key);
      }

      BOOST_FORCEINLINE bool contains(key_type const& key) const
      {
        return this->find(key) != this->end();
      }

      template <class K>
      BOOST_FORCEINLINE typename std::enable_if<
        boost::unordered::detail::are_transparent<K, hasher, key_equal>::value,
        bool>::type
      contains(K const& key) const
      {
        return this->find(key) != this->end();
      }

      std::pair<iterator, iterator> equal_range(key_type const& key)
      {
        auto pos = table_.find(key);
        if (pos == table_.end()) {
          return {pos, pos};
        }

        auto next = pos;
        ++next;
        return {pos, next};
      }

      std::pair<const_iterator, const_iterator> equal_range(
        key_type const& key) const
      {
        auto pos = table_.find(key);
        if (pos == table_.end()) {
          return {pos, pos};
        }

        auto next = pos;
        ++next;
        return {pos, next};
      }

      template <class K>
      typename std::enable_if<
        detail::are_transparent<K, hasher, key_equal>::value,
        std::pair<iterator, iterator> >::type
      equal_range(K const& key)
      {
        auto pos = table_.find(key);
        if (pos == table_.end()) {
          return {pos, pos};
        }

        auto next = pos;
        ++next;
        return {pos, next};
      }

      template <class K>
      typename std::enable_if<
        detail::are_transparent<K, hasher, key_equal>::value,
        std::pair<const_iterator, const_iterator> >::type
      equal_range(K const& key) const
      {
        auto pos = table_.find(key);
        if (pos == table_.end()) {
          return {pos, pos};
        }

        auto next = pos;
        ++next;
        return {pos, next};
      }

      /// Hash Policy
      ///

      size_type bucket_count() const noexcept { return table_.capacity(); }

      float load_factor() const noexcept { return table_.load_factor(); }

      float max_load_factor() const noexcept
      {
        return table_.max_load_factor();
      }

      void max_load_factor(float) {}

      size_type max_load() const noexcept { return table_.max_load(); }

      void rehash(size_type n) { table_.rehash(n); }

      void reserve(size_type n) { table_.reserve(n); }

      /// Observers
      ///

      hasher hash_function() const { return table_.hash_function(); }

      key_equal key_eq() const { return table_.key_eq(); }
    };

    template <class Key, class Hash, class KeyEqual, class Allocator>
    bool operator==(
      unordered_node_set<Key, Hash, KeyEqual, Allocator> const& lhs,
      unordered_node_set<Key, Hash, KeyEqual, Allocator> const& rhs)
    {
      if (&lhs == &rhs) {
        return true;
      }

      return (lhs.size() == rhs.size()) && ([&] {
        for (auto const& key : lhs) {
          auto pos = rhs.find(key);
          if ((pos == rhs.end()) || (key != *pos)) {
            return false;
          }
        }
        return true;
      })();
    }

    template <class Key, class Hash, class KeyEqual, class Allocator>
    bool operator!=(
      unordered_node_set<Key, Hash, KeyEqual, Allocator> const& lhs,
      unordered_node_set<Key, Hash, KeyEqual, Allocator> const& rhs)
    {
      return !(lhs == rhs);
    }

    template <class Key, class Hash, class KeyEqual, class Allocator>
    void swap(unordered_node_set<Key, Hash, KeyEqual, Allocator>& lhs,
      unordered_node_set<Key, Hash, KeyEqual, Allocator>& rhs)
      noexcept(noexcept(lhs.swap(rhs)))
    {
      lhs.swap(rhs);
    }

    template <class Key, class Hash, class KeyEqual, class Allocator,
      class Pred>
    typename unordered_node_set<Key, Hash, KeyEqual, Allocator>::size_type
    erase_if(unordered_node_set<Key, Hash, KeyEqual, Allocator>& set, Pred pred)
    {
      return erase_if(set.table_, pred);
    }

#if defined(BOOST_MSVC)
#pragma warning(pop) /* C4714 */
#endif

#if BOOST_UNORDERED_TEMPLATE_DEDUCTION_GUIDES
    template <class InputIterator,
      class Hash =
        boost::hash<typename std::iterator_traits<InputIterator>::value_type>,
      class Pred =
        std::equal_to<typename std::iterator_traits<InputIterator>::value_type>,
      class Allocator = std::allocator<
        typename std::iterator_traits<InputIterator>::value_type>,
      class = std::enable_if_t<detail::is_input_iterator_v<InputIterator> >,
      class = std::enable_if_t<detail::is_hash_v<Hash> >,
      class = std::enable_if_t<detail::is_pred_v<Pred> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(InputIterator, InputIterator,
      std::size_t = boost::unordered::detail::foa::default_bucket_count,
      Hash = Hash(), Pred = Pred(), Allocator = Allocator())
      -> unordered_node_set<
        typename std::iterator_traits<InputIterator>::value_type, Hash, Pred,
        Allocator>;

    template <class T, class Hash = boost::hash<T>,
      class Pred = std::equal_to<T>, class Allocator = std::allocator<T>,
      class = std::enable_if_t<detail::is_hash_v<Hash> >,
      class = std::enable_if_t<detail::is_pred_v<Pred> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(std::initializer_list<T>,
      std::size_t = boost::unordered::detail::foa::default_bucket_count,
      Hash = Hash(), Pred = Pred(), Allocator = Allocator())
      -> unordered_node_set<T, Hash, Pred, Allocator>;

    template <class InputIterator, class Allocator,
      class = std::enable_if_t<detail::is_input_iterator_v<InputIterator> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(InputIterator, InputIterator, std::size_t, Allocator)
      -> unordered_node_set<
        typename std::iterator_traits<InputIterator>::value_type,
        boost::hash<typename std::iterator_traits<InputIterator>::value_type>,
        std::equal_to<typename std::iterator_traits<InputIterator>::value_type>,
        Allocator>;

    template <class InputIterator, class Hash, class Allocator,
      class = std::enable_if_t<detail::is_hash_v<Hash> >,
      class = std::enable_if_t<detail::is_input_iterator_v<InputIterator> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(
      InputIterator, InputIterator, std::size_t, Hash, Allocator)
      -> unordered_node_set<
        typename std::iterator_traits<InputIterator>::value_type, Hash,
        std::equal_to<typename std::iterator_traits<InputIterator>::value_type>,
        Allocator>;

    template <class T, class Allocator,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(std::initializer_list<T>, std::size_t, Allocator)
      -> unordered_node_set<T, boost::hash<T>, std::equal_to<T>, Allocator>;

    template <class T, class Hash, class Allocator,
      class = std::enable_if_t<detail::is_hash_v<Hash> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(std::initializer_list<T>, std::size_t, Hash, Allocator)
      -> unordered_node_set<T, Hash, std::equal_to<T>, Allocator>;

    template <class InputIterator, class Allocator,
      class = std::enable_if_t<detail::is_input_iterator_v<InputIterator> >,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(InputIterator, InputIterator, Allocator)
      -> unordered_node_set<
        typename std::iterator_traits<InputIterator>::value_type,
        boost::hash<typename std::iterator_traits<InputIterator>::value_type>,
        std::equal_to<typename std::iterator_traits<InputIterator>::value_type>,
        Allocator>;

    template <class T, class Allocator,
      class = std::enable_if_t<detail::is_allocator_v<Allocator> > >
    unordered_node_set(std::initializer_list<T>, Allocator)
      -> unordered_node_set<T, boost::hash<T>, std::equal_to<T>, Allocator>;
#endif

  } // namespace unordered
} // namespace boost

#endif

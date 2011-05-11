// -*- mode: c++ -*-
//
//  Copyright(C) 2009-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __UTILS__DENSE_HASHTABLE__H__
#define __UTILS__DENSE_HASHTABLE__H__ 1

#include <string>
#include <memory>

#include <google/dense_hash_set>

#include <utils/memory.hpp>

namespace utils
{
  
  template <typename Key,
	    typename Value,
	    typename ExtractKey,
	    typename Hash,
	    typename Equal,
	    typename Alloc>
  class dense_hashtable : public Alloc
  {
  public:
    typedef Key   key_type;
    typedef Value value_type;
    typedef Alloc allocator_type;
    
  private:
    typedef dense_hashtable<Key,Value,ExtractKey,Hash,Equal,Alloc> self_type;

    struct hasher : public Hash, public ExtractKey
    {
      size_t operator()(const value_type* x) const
      {
	return (x == 0 || x == self_type::__deleted_key()) ? size_t(0) : Hash::operator()(ExtractKey::operator()(*x));
      }
    };
    struct equal : public Equal, public ExtractKey
    {
      bool operator()(const value_type* x, const value_type* y) const
      {
	return (x == y
		|| (x && y
		    && x != self_type::__deleted_key() && y != self_type::__deleted_key()
		    && Equal::operator()(ExtractKey::operator()(*x), ExtractKey::operator()(*y))));
      }
    };

    typedef typename Alloc::template rebind<value_type*>::other map_alloc_type;
    typedef google::dense_hash_set<value_type*, hasher, equal, map_alloc_type> hashtable_type;
    
    typedef typename hashtable_type::iterator       iterator_base_type;
    typedef typename hashtable_type::const_iterator const_iterator_base_type;
    
  public:
    typedef typename hashtable_type::size_type       size_type;
    typedef typename hashtable_type::difference_type difference_type;
    
    class const_iterator;

    class iterator
    {
    private:
      friend class const_iterator;
      
    private:
      typedef iterator_base_type base_type;
      
    public:
      typedef typename base_type::iterator_category iterator_category;
      typedef typename base_type::difference_type   difference_type;
      typedef typename base_type::size_type         size_type;
      
      typedef Value  value_type;
      typedef Value& reference;
      typedef Value* pointer;
      
      iterator(const base_type& x) : base(x) {}
      iterator(const const_iterator& x) : base(x.base) {}
      
      value_type& operator*() { return *(*base); }
      value_type* operator->() { return &(*(*base)); }

      iterator& operator++() { ++ base; return *this; }
      iterator operator++(int) { iterator tmp(*this); ++ base; return tmp; }
      
      friend
      bool operator==(const iterator& x, const iterator& y) { return x.base == y.base; }
      friend
      bool operator!=(const iterator& x, const iterator& y) { return x.base != y.base; }

      friend
      bool operator==(const iterator& x, const const_iterator& y) { return x == iterator(y); }
      friend
      bool operator!=(const iterator& x, const const_iterator& y) { return x != iterator(y); }
      
    private:
      base_type base;
    };
    
    class const_iterator
    {
    private:
      friend class iterator;

    private:
      typedef const_iterator_base_type base_type;
      
    public:
      typedef typename base_type::iterator_category iterator_category;
      typedef typename base_type::difference_type   difference_type;
      typedef typename base_type::size_type         size_type;
      
      typedef Value  value_type;
      typedef const Value& reference;
      typedef const Value* pointer;
      
      const_iterator(const base_type& x) : base(x) {}
      const_iterator(const iterator& x) : base(x.base) {}
      
      const value_type& operator*() { return *(*base); }
      const value_type* operator->() { return &(*(*base)); }
      
      const_iterator& operator++() { ++ base; return *this; }
      const_iterator operator++(int) { const_iterator tmp(*this); ++ base; return tmp; }
      
      friend
      bool operator==(const const_iterator& x, const const_iterator& y) { return x.base == y.base; }
      friend
      bool operator!=(const const_iterator& x, const const_iterator& y) { return x.base != y.base; }

      friend
      bool operator==(const const_iterator& x, const iterator& y) { return x == const_iterator(y); }
      friend
      bool operator!=(const const_iterator& x, const iterator& y) { return x != const_iterator(y); }
      
    private:
      base_type base;
    };
    
  public:
    // we assume aligned malloc, and the malloc of address 1 will not happen... is it true?
    dense_hashtable() : hashtable() {  hashtable.set_empty_key(0); hashtable.set_deleted_key(__deleted_key()); }
    template <typename Iterator>
    dense_hashtable(Iterator first, Iterator last) { hashtable.set_empty_key(0); hashtable.set_deleted_key(__deleted_key()); insert(first, last); } 
    dense_hashtable(const dense_hashtable& x) : hashtable() { hashtable.set_empty_key(0); hashtable.set_deleted_key(__deleted_key()); assign(x); }
    ~dense_hashtable() { clear(); }
    dense_hashtable& operator=(const dense_hashtable& x)
    {
      assign(x);
      return *this;
    }
    
  private:
    static value_type* __deleted_key()
    {
      static value_type __value;
      return &__value;
    }
    
  public:
    void swap(dense_hashtable& x)
    {
      using namespace std;
      swap(alloc(), x.alloc());
      swap(hashtable, x.hashtable);
    }
    
    bool empty() const { return hashtable.empty(); }
    size_type size() const { return hashtable.size(); }
    
  public:
    inline const_iterator begin() const { return hashtable.begin(); }
    inline       iterator begin()       { return hashtable.begin(); }
    inline const_iterator end() const { return hashtable.end(); }
    inline       iterator end()       { return hashtable.end(); }
    inline const_iterator find(const value_type& x) const { return hashtable.find(const_cast<value_type*>(&x)); }
    inline       iterator find(const value_type& x)       { return hashtable.find(const_cast<value_type*>(&x)); }

    template <typename Iterator>
    void insert(Iterator first, Iterator last)
    {
      for (/**/; first != last; ++ first) {
	std::pair<typename hashtable_type::iterator, bool> result = hashtable.insert(const_cast<value_type*>(&(*first)));
	if (result.second) {
	  const_cast<value_type*&>(*result.first) = alloc().allocate(1);
	  utils::construct_object(*result.first, *first);
	}
      }
    }
    
    std::pair<iterator, bool> insert(const value_type& x)
    {
      std::pair<typename hashtable_type::iterator, bool> result = hashtable.insert(const_cast<value_type*>(&x));
      if (result.second) {
	const_cast<value_type*&>(*result.first) = alloc().allocate(1);
	utils::construct_object(*result.first, x);
      }
      return result;
    }
    
    void erase(const value_type& x)
    {
      typename hashtable_type::iterator iter = hashtable.find(const_cast<value_type*>(&x));
      if (iter != hashtable.end()) {
	value_type* p = *iter;
	
	hashtable.erase(iter);

	utils::destroy_object(p);
	alloc().deallocate(p, 1);
      }
    }
    
    void erase(iterator x)
    {
      value_type* p = &(*x);
      
      hashtable.erase(x);
      
      utils::destroy_object(p);
      alloc().deallocate(p, 1);
    }
    
    
    void clear()
    {
      typename hashtable_type::iterator iter_end = hashtable.end();
      for (typename hashtable_type::iterator iter = hashtable.begin(); iter != iter_end; ++ iter) {
	utils::destroy_object(*iter);
	alloc().deallocate(*iter, 1);
      }
      hashtable.clear();
    }
    
    void assign(const dense_hashtable& x)
    {
      clear();
      
      typename hashtable_type::const_iterator iter_end = x.hashtable.end();
      for (typename hashtable_type::const_iterator iter = x.hashtable.begin(); iter != iter_end; ++ iter) {
	value_type* p = alloc().allocate(1);
	utils::construct_object(p, *(*iter));
	hashtable.insert(p);
      }
    }
    
  private:
    allocator_type& alloc() { return static_cast<allocator_type&>(*this); }
    const allocator_type& alloc() const { return static_cast<const allocator_type&>(*this); }
    
  private:
    hashtable_type hashtable;
  };
};

namespace std
{
  template <typename K, typename V, typename X, typename H, typename E, typename A>
  inline
  void swap(utils::dense_hashtable<K,V,X,H,E,A>& x,
	    utils::dense_hashtable<K,V,X,H,E,A>& y)
  {
    x.swap(y);
  }
};


#endif


// -*- mode: c++ -*-
//
//  Copyright(C) 2009-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

// indexed-trie...
// work as if indexed-set, but we will also consider parent...
// similar in spirit with symbol_tree

#ifndef __UTILS__INDEXED_TRIE__HPP__
#define __UTILS__INDEXED_TRIE__HPP__ 1

#include <stdint.h>

#include <vector>
#include <stdexcept>
#include <algorithm>

#include <utils/bithack.hpp>
#include <utils/memory.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/hashmurmur.hpp>

#include <boost/functional/hash/hash.hpp>

namespace utils
{
  
  template <typename Tp,
	    typename Hash=boost::hash<Tp>,
	    typename Equal=std::equal_to<Tp>,
	    typename Alloc=std::allocator<Tp> >
  class indexed_trie
  {
  public:
    typedef uint32_t id_type;
    typedef Tp value_type;
    
  public:
    static const id_type& npos() {
      static const id_type __npos(-1);
      return __npos;
    }
    
  private:
    struct node_type
    {
      value_type value;
      id_type    parent;
      
      node_type() : value(), parent(id_type(-1)) {}
      node_type(const value_type& _value,
		const id_type& _parent)
	: value(_value), parent(_parent) {}
    };
    
    struct node_hash_type : public Hash
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      node_hash_type() {}
      node_hash_type(const Hash& x) : Hash(x) {}
      
      size_t operator()(const node_type& node) const
      {
	return hasher_type()(node.parent, Hash::operator()(node.value));
      };
    };
    
    struct node_equal_type : public Equal
    {
      node_equal_type() {}
      node_equal_type(const Equal& x) : Equal(x) {}

      bool operator()(const node_type& x, const node_type& y) const
      {
	return x.parent == y.parent && Equal::operator()(x.value, y.value);
      }
    };
    
    typedef typename Alloc::template rebind<node_type >::other node_alloc_type;
    
    typedef utils::indexed_set<node_type, node_hash_type, node_equal_type, node_alloc_type> node_set_type;
    
    typedef typename node_set_type::size_type size_type;
    typedef typename node_set_type::const_iterator const_base_iterator;
    
  public:
    
    struct const_iterator : public const_base_iterator
    {
      const_iterator() : const_base_iterator() {}
      const_iterator(const const_base_iterator& x) : const_base_iterator(x) {}
      
      const value_type& operator*() const { return const_base_iterator::operator*().value; }
      const value_type* operator->() const { return &(const_base_iterator::operator*().value); }
    };
    typedef const_iterator iterator;

  public:
    indexed_trie(const size_type __size=8,
		 const Hash& __hash=Hash(),
		 const Equal& __equal=Equal())
      : nodes(__size, node_hash_type(__hash), node_equal_type(__equal)) {}
    
  public:
    id_type root() const { return id_type(-1); }
    bool is_root(id_type id) const { return id == root(); }
    id_type parent(id_type id) const { return nodes[id].parent; }
    
    const value_type& operator[](id_type pos) const { return nodes[pos].value; }
    
    const_iterator begin() const { return nodes.begin(); }
    const_iterator end() const { return nodes.end(); }
    
    std::pair<iterator, bool> insert(const id_type& __p, const value_type& __v) { return nodes.insert(node_type(__v, __p)); }
    
    // push-pop interface..
    // it will not perform any push/pop, but used as trie traversal...
    id_type push(const id_type& __p, const value_type& __v)
    {
      typename node_set_type::iterator iter = nodes.insert(node_type(__v, __p)).first;
      return iter - nodes.begin();
    }
    id_type pop(id_type id) const { return nodes[id].parent; }
    
    id_type find(const id_type& __p, const value_type& __v) const
    {
      typename node_set_type::const_iterator iter = nodes.find(node_type(__v, __p));
      if (iter != nodes.end())
	return iter - nodes.begin();
      else
	return root();
    }

    
    void clear() { nodes.clear(); }

    void swap(indexed_trie& x)
    {
      nodes.swap(x.nodes);
    }
    
  private:
    node_set_type nodes;
  };

};

namespace std
{
  template <typename _T, typename _H, typename _E, typename _A>
  inline
  void swap(utils::indexed_trie<_T,_H,_E,_A>& x,
	    utils::indexed_trie<_T,_H,_E,_A>& y)
  {
    x.swap(y);
  }
  
};

#endif

// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__TREE_TRANSDUCER__HPP__
#define __CICADA__TREE_TRANSDUCER__HPP__ 1

// base class for tree-transducer implementation...
// we will derive all the transducer based storage from here:

#include <stdint.h>

#include <vector>

#include <cicada/symbol.hpp>
#include <cicada/symbol_vector.hpp>
#include <cicada/vocab.hpp>
#include <cicada/rule.hpp>
#include <cicada/tree_rule.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/lattice.hpp>

#include <boost/shared_ptr.hpp>

#include <utils/piece.hpp>

namespace cicada
{
  
  class TreeTransducer
  {
  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    
    typedef cicada::Symbol   symbol_type;
    typedef cicada::Vocab    vocab_type;
    typedef cicada::TreeRule rule_type;

    typedef cicada::SymbolVector symbol_set_type;

    typedef cicada::HyperGraph hypergraph_type;
    typedef cicada::Lattice    lattice_type;

    typedef hypergraph_type::feature_set_type   feature_set_type;
    typedef hypergraph_type::attribute_set_type attribute_set_type;
    
    typedef boost::shared_ptr<rule_type> rule_ptr_type;
    
    struct RulePair
    {
      rule_ptr_type      source;
      rule_ptr_type      target;
      feature_set_type   features;
      attribute_set_type attributes;
      
      RulePair() : source(), target(), features(), attributes() {}
      RulePair(const rule_ptr_type& __source,
	       const rule_ptr_type& __target)
	: source(__source), target(__target), features(), attributes() {}
      RulePair(const rule_ptr_type& __source,
	       const rule_ptr_type& __target,
	       const feature_set_type& __features)
	: source(__source), target(__target), features(__features), attributes() {}
      RulePair(const rule_ptr_type& __source,
	       const rule_ptr_type& __target,
	       const feature_set_type& __features,
	       const attribute_set_type& __attributes)
	: source(__source), target(__target), features(__features), attributes(__attributes) {}
    };
    
    typedef RulePair rule_pair_type;
    typedef std::vector<rule_pair_type, std::allocator<rule_pair_type> > rule_pair_set_type;
    
    // 64-bit id type!
    typedef uint64_t id_type;
    
    struct edge_type
    {
      typedef uint32_t id_type;
      
      edge_type() : id(id_type(-1)) {}
      edge_type(const id_type& __id) : id(__id) {}
      
      id_type id;
    };
    
    typedef TreeTransducer transducer_type;
    typedef boost::shared_ptr<transducer_type> transducer_ptr_type;
    
  public:
    virtual ~TreeTransducer() {}
    virtual transducer_ptr_type clone() const = 0;

    // valid span
    virtual bool valid_span(int first, int last, int distance) const = 0;
    virtual bool is_cky() const = 0;
    bool is_cyk() const { return is_cky(); }

    // operation over edges...
    virtual edge_type edge(const symbol_type& symbol) const = 0;
    virtual edge_type edge(const symbol_set_type& symbols) const = 0;
    virtual edge_type edge(const symbol_type* first, const symbol_type* last) const = 0;
    
    // operation over nodes...
    virtual id_type root() const = 0;
    virtual id_type next(const id_type& node, const edge_type& edge) const = 0;
    virtual id_type next(const id_type& node, const symbol_type& symbol) const = 0;
    virtual bool has_next(const id_type& node) const = 0;
    virtual const rule_pair_set_type& rules(const id_type& node) const = 0;

    virtual void assign(const lattice_type& lattice) {}
    virtual void assign(const hypergraph_type& hypergraph) {}
    
  public:
    static const char* lists();
    static transducer_ptr_type create(const utils::piece& parameter);
  };
  
  inline
  size_t hash_value(TreeTransducer::edge_type const& x)
  {
    return x.id;
  }

  
  inline
  bool operator==(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id == y.id;
  }

  inline
  bool operator!=(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id != y.id;
  }

  inline
  bool operator<(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id < y.id;
  }
  
  inline
  bool operator>(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id > y.id;
  }

  inline
  bool operator<=(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id <= y.id;
  }
  
  inline
  bool operator>=(const TreeTransducer::edge_type& x, const TreeTransducer::edge_type& y)
  {
    return x.id >= y.id;
  }
  
};

#endif



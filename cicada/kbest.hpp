// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__KBEST__HPP__
#define __CICADA__KBEST__HPP__ 1

#include <vector>
#include <algorithm>

#include <cicada/hypergraph.hpp>
#include <cicada/semiring/traits.hpp>

#include <utils/bithack.hpp>
#include <utils/small_vector.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/hashmurmur3.hpp>
#include <utils/b_heap.hpp>
#include <utils/std_heap.hpp>
#include <utils/compact_set.hpp>

namespace cicada
{
  // k-best derication based on Algorithm 3 of
  //
  // @InProceedings{huang-chiang:2005:IWPT,
  //  author    = {Huang, Liang  and  Chiang, David},
  //  title     = {Better k-best Parsing},
  //  booktitle = {Proceedings of the Ninth International Workshop on Parsing Technology},
  //  month     = {October},
  //  year      = {2005},
  //  address   = {Vancouver, British Columbia},
  //  publisher = {Association for Computational Linguistics},
  //  pages     = {53--64},
  //  url       = {http://www.aclweb.org/anthology/W/W05/W05-1506}
  //  }

  
  // semiring,
  // yield, requires operator=(const yield&) (assignment) and  yield::yield() (constructor)
  // traversal function operator()(const HyperGraph::Edge&, const yield*, Iterator first, Iterator last);
  //                    where Iterator's value (*first etc.) is const yield&
  // semiring function
  
  
  template <typename Traversal,
	    typename Function,
	    typename Filter>
  struct KBest
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef HyperGraph hypergraph_type;
    
    typedef hypergraph_type::id_type id_type;
    typedef hypergraph_type::node_type node_type;
    typedef hypergraph_type::edge_type edge_type;
    
    typedef Traversal traversal_type;
    typedef Function  function_type;
    typedef Filter    filter_type;

    typedef typename traversal_type::value_type yield_type;
    typedef typename function_type::value_type  semiring_type;
    typedef typename function_type::value_type  weight_type;
    
    KBest(const hypergraph_type& __graph,
	  const size_type& __k_prime,
	  const traversal_type& __traversal,
	  const function_type& __function,
	  const filter_type& __filter)
      : traversal(__traversal),
	function(__function),
	filter(__filter),
	graph(__graph),
	states(__graph.nodes.size()) ,
	k_prime(__k_prime)
    {
      if (graph.goal == hypergraph_type::invalid)
	throw std::runtime_error("invalid hypergraph...");
    }
    
    typedef utils::small_vector<int, std::allocator<int> > index_set_type;
    
    struct Derivation
    {
      Derivation(const index_set_type& __j) : j(__j) {}
      Derivation(const edge_type& __edge, const index_set_type& __j) : edge(&__edge), j(__j) {}
      
      yield_type yield;
      const edge_type* edge;
      index_set_type   j;
      weight_type      score;
    };
    
    typedef Derivation derivation_type;
    typedef utils::chunk_vector<derivation_type, 4096 / sizeof(derivation_type), std::allocator<derivation_type> > derivation_set_type;

    struct compare_derivation_type
    {
      bool operator()(const derivation_type* x, const derivation_type* y) const
      {
	return x->score > y->score;
      }
    };
    
    struct compare_heap_type
    {
      bool operator()(const derivation_type* x, const derivation_type* y) const
      {
	return (x->score < y->score) || (!(y->score < x->score) && (cardinality(x->j) > cardinality(y->y)));
      }

      size_t cardinality(const index_set_type& x) const
      {
	return std::accumulate(x.begin(), x.end(), 0);
      }
    };
    
    //typedef std::vector<const derivation_type*, std::allocator<const derivation_type*> > derivation_heap_type;
    
    typedef std::vector<const derivation_type*, std::allocator<const derivation_type*> > derivation_heap_base_type;
    //typedef utils::b_heap<const derivation_type*, derivation_heap_base_type, compare_heap_type, 512 / sizeof(const derivation_type*)> derivation_heap_type;
    typedef utils::std_heap<const derivation_type*, derivation_heap_base_type, compare_heap_type> derivation_heap_type;
    
    
    typedef std::vector<const derivation_type*, std::allocator<const derivation_type*> > derivation_list_type;
    
    struct derivation_hash_type : public utils::hashmurmur3<size_t>
    {
      size_t operator()(const derivation_type* x) const
      {
	return (x == 0 ? size_t(0) : utils::hashmurmur3<size_t>::operator()(x->j.begin(), x->j.end(), (uintptr_t) x->edge));
      }
    };
    
    struct derivation_equal_type
    {
      bool operator()(const derivation_type* x, const derivation_type* y) const
      {
	return (x == y) || (x && y && x->edge == y->edge && x->j == y->j);
      }
    };

    struct derivation_unassigned
    {
      const derivation_type* operator()() const { return 0; }
    };
    
    typedef utils::compact_set<const derivation_type*,
			       derivation_unassigned, derivation_unassigned,
			       derivation_hash_type, derivation_equal_type,
			       std::allocator<const derivation_type*> > derivation_set_unique_type;

    
    struct State
    {
      State() {  }
      
      derivation_heap_type cand;
      derivation_list_type D;
      derivation_set_unique_type uniques;
    };
    
    typedef State state_type;
    typedef std::vector<state_type, std::allocator<state_type> > state_set_type;

    bool operator()(int k, yield_type& yield, weight_type& weight)
    {
      const derivation_type* derivation = lazy_kth_best(graph.goal, k);
      if (derivation) {
	yield = derivation->yield;
	weight = derivation->score;
	return true;
      } else {
	yield = yield_type();
	weight = weight_type();
	return false;
      }
    }
    
  private:
    typedef std::vector<const yield_type*, std::allocator<const yield_type*> > yield_set_type;

  public:
    class yield_iterator : public yield_set_type::const_iterator
    {
    public:
      typedef typename yield_set_type::const_iterator base_type;
      
      yield_iterator(const base_type& x) : base_type(x) {}
      
      const yield_type& operator*()  { return *(base_type::operator*()); }
      const yield_type* operator->() { return base_type::operator*(); }
      
      friend
      yield_iterator operator+(const yield_iterator& x, ptrdiff_t diff)
      {
	return yield_iterator(base_type(x) + diff);
      }

      friend
      yield_iterator operator-(const yield_iterator& x, ptrdiff_t diff)
      {
	return yield_iterator(base_type(x) - diff);
      }

    };
    
  private:
    const derivation_type* lazy_kth_best(int v, int k)
    {
      //std::cerr << "lazy-kth-best: node: " <<  v << " kbest: " << k << std::endl;

      state_type & state = get_candidate(v);
      derivation_heap_type& cand = state.cand;
      derivation_list_type& D = state.D;
      
      yield_set_type yields;
      
      while (static_cast<int>(D.size()) <= k) {
	
	// lazy-next for the last of the derivation, D
	if (! D.empty())
	  lazy_next(*D.back(), state);
	
	// We will add an item from cand into D.
	
	bool incremented = false;
	while (! cand.empty()) {
	  const derivation_type* derivation = cand.top();
	  cand.pop();
	  
	  // perform traversal here...
	  yields.clear();
	  for (size_t i = 0; i != derivation->edge->tails.size(); ++ i) {
	    const derivation_type* antecedent = lazy_kth_best(derivation->edge->tails[i], derivation->j[i]);
	    
	    if (! antecedent)
	      throw std::runtime_error("no antecedent???");
	    
	    yields.push_back(&(antecedent->yield));
	  }
	  
	  traversal(*(derivation->edge), const_cast<yield_type&>(derivation->yield), yield_iterator(yields.begin()), yield_iterator(yields.end()));
	  
	  // perform filtering here...!
	  // if we have duplicates of "yield", do not insert into D
	  if (! filter(graph.nodes[v], derivation->yield)) {
	    D.push_back(derivation);
	    incremented = true;
	    break;
	  }
	  
	  // lazy-next for this derivation, otherwise, we may have computed wrong k-best...
	  lazy_next(*derivation, state);
	}
	
	// if D was not incremented, no new item was found!
	if (! incremented) break;
      }
      
      return (k < static_cast<int>(D.size()) ? D[k] : 0);
    }
    
    void lazy_next(const derivation_type& derivation, state_type& state)
    {
      derivation_type query(derivation.j);
      index_set_type& j = query.j;
      
      for (size_t i = 0; i != j.size(); ++ i) {
	++ j[i];
	
	const derivation_type* antecedent = lazy_kth_best(derivation.edge->tails[i], j[i]);
	
	if (antecedent) {
	  query.edge = derivation.edge;
	  
	  if (state.uniques.find(&query) == state.uniques.end()) {
	    const derivation_type* derivation_new = make_derivation(*(derivation.edge), j);
	    
	    if (derivation_new) {
	      state.cand.push(derivation_new);
	      state.uniques.insert(derivation_new);
	    }
	  }
	}
	-- j[i];
      }
    }

    const derivation_type* make_derivation(const edge_type& edge, const index_set_type& j)
    {
      derivations.push_back(derivation_type(edge, j));
      
      derivation_type& derivation = derivations.back();
      
      derivation.score = function(edge);
      
      index_set_type::const_iterator iiter = j.begin();
      edge_type::node_set_type::const_iterator niter_end = edge.tails.end();
      for (edge_type::node_set_type::const_iterator niter = edge.tails.begin(); niter != niter_end; ++ niter, ++ iiter) {
	const derivation_type* antecedent = lazy_kth_best(*niter, *iiter);
	
	if (! antecedent) return 0;
	
	derivation.score *= antecedent->score;
      }
      
      return &derivation;
    }

    state_type& get_candidate(int v)
    {
      state_type& state = states[v];
      
      if (! state.D.empty() || ! state.cand.empty()) return state;
      
      const node_type& node = graph.nodes[v];
      
      state.cand.reserve(node.edges.size());
      
      node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
      for (node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
	const edge_type& edge = graph.edges[*eiter];
	
	const index_set_type j(edge.tails.size(), 0);
	const derivation_type* derivation = make_derivation(edge, j);
	
	if (! derivation)
	  throw std::runtime_error("no derivation?");
	
	state.cand.push(derivation);
      }
      
      return state;
    }
    
  private:
    const traversal_type traversal;
    const function_type  function;
    const filter_type    filter;
    
    const hypergraph_type& graph;
    
    derivation_set_type derivations;
    state_set_type      states;
    
    const size_type k_prime;
  };

};

#endif

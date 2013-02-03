// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__APPLY_CUBE_GROW__HPP__
#define __CICADA__APPLY_CUBE_GROW__HPP__ 1

#include <numeric>

#include <cicada/apply_state_less.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/model.hpp>

#include <cicada/semiring/traits.hpp>

#include <utils/compact_map.hpp>
#include <utils/compact_set.hpp>
#include <utils/small_vector.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/hashmurmur3.hpp>

#include <utils/b_heap.hpp>
#include <utils/std_heap.hpp>

namespace cicada
{
  
  // implementation of 
  //
  // @InProceedings{huang-chiang:2007:ACLMain,
  //  author    = {Huang, Liang  and  Chiang, David},
  //  title     = {Forest Rescoring: Faster Decoding with Integrated Language Models},
  //  booktitle = {Proceedings of the 45th Annual Meeting of the Association of Computational Linguistics},
  //  month     = {June},
  //  year      = {2007},
  //  address   = {Prague, Czech Republic},
  //  publisher = {Association for Computational Linguistics},
  //  pages     = {144--151},
  //  url       = {http://www.aclweb.org/anthology/P07-1019}
  //  }
  //
  
  // semiring and function to compute semiring from a feature vector

  template <typename Semiring, typename Function>
  struct ApplyCubeGrow
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef HyperGraph hypergraph_type;
    
    typedef hypergraph_type::id_type   id_type;
    typedef hypergraph_type::node_type node_type;
    typedef hypergraph_type::edge_type edge_type;

    typedef hypergraph_type::feature_set_type feature_set_type;

    typedef Model model_type;
    
    typedef model_type::state_type     state_type;
    typedef model_type::state_set_type state_set_type;
    
    typedef Semiring semiring_type;
    typedef Semiring score_type;
    
    typedef Function function_type;

    typedef std::vector<score_type, std::allocator<score_type> > score_set_type;
    
    typedef utils::small_vector<int, std::allocator<int> > index_set_type;
    
    struct Candidate
    {
      const edge_type* in_edge;
      edge_type        out_edge;

      state_type state;
      
      index_set_type j;
      
      score_type score;
      
      Candidate(const index_set_type& __j)
	: in_edge(0), j(__j) {}

      Candidate(const edge_type& __edge, const index_set_type& __j)
	: in_edge(&__edge), out_edge(__edge), j(__j) {}
    };

    typedef Candidate candidate_type;
    typedef utils::chunk_vector<candidate_type, 4096 / sizeof(candidate_type), std::allocator<candidate_type> > candidate_set_type;
        
    
    struct candidate_hash_type : public utils::hashmurmur3<size_t>
    {
      size_t operator()(const candidate_type* x) const
      {
	return (x == 0 ? size_t(0) : utils::hashmurmur3<size_t>::operator()(x->j.begin(), x->j.end(), x->in_edge->id));
      }
    };
    struct candidate_equal_type
    {
      bool operator()(const candidate_type* x, const candidate_type* y) const
      {
	return (x == y) || (x && y && x->in_edge->id == y->in_edge->id && x->j == y->j);
      }
    };
    
    struct compare_heap_type
    {
      // we use less, so that when popped from heap, we will grab "greater" in back...
      bool operator()(const candidate_type* x, const candidate_type* y) const
      {
	return (x->score < y->score) || (!(y->score < x->score) && cardinality(x->j) > cardinality(y->j));
      }

      size_t cardinality(const index_set_type& x) const
      {
	return std::accumulate(x.begin(), x.end(), 0);
      }
    };
    
    typedef std::vector<const candidate_type*, std::allocator<const candidate_type*> > candidate_list_type;
    
    typedef std::vector<const candidate_type*, std::allocator<const candidate_type*> > candidate_heap_base_type;
    //typedef utils::b_heap<const candidate_type*,  candidate_heap_base_type, compare_heap_type, 512 / sizeof(const candidate_type*)> candidate_heap_type;
    typedef utils::std_heap<const candidate_type*,  candidate_heap_base_type, compare_heap_type> candidate_heap_type;

    struct candidate_unassigned
    {
      const candidate_type* operator()() const { return 0; }
    };
    
    typedef utils::compact_map<state_type, id_type,
			       model_type::state_unassigned, model_type::state_unassigned,
			       model_type::state_hash, model_type::state_equal,
			       std::allocator<std::pair<const state_type, id_type> > > state_node_map_type;
    typedef utils::compact_set<const candidate_type*,
			       candidate_unassigned, candidate_unassigned,
			       candidate_hash_type, candidate_equal_type,
			       std::allocator<const candidate_type*> > candidate_set_unique_type;
    
    struct State
    {
      State(const size_type& hint, const size_type& state_size)
	: nodes(hint >> 1, model_type::state_hash(state_size), model_type::state_equal(state_size)),
	  fired(false)
      { }
      
      candidate_heap_type cand;
      candidate_heap_type buf;
      
      candidate_list_type D;
      candidate_set_unique_type uniques;
      
      state_node_map_type nodes;

      bool fired;
    };
    
    typedef State cand_state_type;
    typedef std::vector<cand_state_type, std::allocator<cand_state_type> > cand_state_set_type;
    
    ApplyCubeGrow(const model_type& _model,
		  const function_type& _function,
		  const int _cube_size_max)
      : model(_model),
	function(_function),
	cube_size_max(_cube_size_max)
    { 
      
    }
    
    void operator()(const hypergraph_type& graph_in,
		    hypergraph_type&       graph_out)
    {
      const_cast<model_type&>(model).initialize();

      graph_out.clear();

      if (model.is_stateless()) {
	ApplyStateLess __applier(model);
	__applier(graph_in, graph_out);
      } else {
	
	// no ... cube-pruning step
	//cube_prune(graph_in);
	
	candidates.clear();
	
	node_states.clear();
	node_states.reserve(graph_in.nodes.size() * cube_size_max);
	
	scores.clear();
	scores.reserve(graph_in.nodes.size() * cube_size_max);
	
	states.clear();
	states.reserve(graph_in.nodes.size());
	states.resize(graph_in.nodes.size(), cand_state_type(cube_size_max >> 1, model.state_size()));
	
	for (size_t j = 0; j != cube_size_max; ++ j) {
	  const size_type edge_size_prev = graph_out.edges.size();
	  lazy_jth_best(graph_in.goal, j, graph_in, graph_out);

	  // quit if no new edges inserted
	  if (edge_size_prev == graph_out.edges.size()) break;
	}

	//std::cerr << "topologically sort" << std::endl;
	
	// topologically sort...
	graph_out.topologically_sort();
	
	//std::cerr << "topologically sort: end" << std::endl;
	
	// re-initialize again...
	const_cast<model_type&>(model).initialize();
      }
    };
    
  private:
    
    void lazy_jth_best(id_type v, int j, const hypergraph_type& graph, hypergraph_type& graph_out)
    {
      //std::cerr << "node: " << v << std::endl;
      
      const bool is_goal = graph.goal == v;
      
      cand_state_type& state = states[v];
      
      if (! state.fired) {
	const node_type& node = graph.nodes[v];
	
	// for each edge in v
	//   fire(edge, 0, cand)
	node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
	for (node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
	  const edge_type& edge = graph.edges[*eiter];
	  const index_set_type j(edge.tails.size(), 0);
	  
	  fire(edge, j, state, graph, graph_out);
	}
	
	state.fired = true;
      }
      
      while (static_cast<int>(state.D.size()) <= j && state.buf.size() + state.D.size() < cube_size_max && ! state.cand.empty()) {
	// pop-min
	const candidate_type* item = state.cand.top();
	state.cand.pop();
	
	// push item to buffer
	push_buf(*item, state, is_goal, graph_out);
	
	// push succ
	push_succ(*item, state, graph, graph_out);
	
	// enum item with current bound
	if (! state.cand.empty())
	  enum_item(state, state.cand.top()->score, is_goal, graph_out);
      }
      
      // enum item with zero bound
      enum_item(state, semiring::traits<score_type>::zero(), is_goal, graph_out);
    }

    void fire(const edge_type& edge, const index_set_type& j, cand_state_type& state, const hypergraph_type& graph, hypergraph_type& graph_out)
    {
      candidate_type query(j);
      query.in_edge = &edge;
      
      if (state.uniques.find(&query) != state.uniques.end()) return;
      
      // for each edge, 
      index_set_type::const_iterator iiter = j.begin();
      edge_type::node_set_type::const_iterator niter_end = edge.tails.end();
      for (edge_type::node_set_type::const_iterator niter = edge.tails.begin(); niter != niter_end; ++ niter, ++ iiter) {
	lazy_jth_best(*niter, *iiter, graph, graph_out);
	
	if (static_cast<int>(states[*niter].D.size()) <= *iiter) return;
      }
      
      // push cand
      const candidate_type* item = make_candidate(edge, j, state, graph.goal == edge.head, graph_out);
      
      state.uniques.insert(item);
      state.cand.push(item);
    }
    
    void push_succ(const candidate_type& item, cand_state_type& state, const hypergraph_type& graph, hypergraph_type& graph_out)
    {
      index_set_type j = item.j;
      
      // for each i in 1 ... |e|
      //   fire(e, j + b_i, cand)
      for (size_t i = 0; i != item.j.size(); ++ i) {
	const int j_i_prev = j[i];
	++ j[i];
	
	fire(*item.in_edge, j, state, graph, graph_out);
	
	j[i] = j_i_prev;
      }
    }
    
    void enum_item(cand_state_type& state, const score_type bound, const bool is_goal, hypergraph_type& graph_out)
    {
      // while |buf| and min(buf) < bound (min-cost)
      //  append pop-min to D
      while (! state.buf.empty() && state.buf.top()->score > bound) {
	const candidate_type* item = state.buf.top();
	state.buf.pop();
	
	candidate_type& candidate = const_cast<candidate_type&>(*item);
	
	// we will add new node/edge here 
	// If possible, state merge
	if (is_goal) {
	  if (! graph_out.is_valid()) {
	    node_states.push_back(candidate.state);
	    scores.push_back(candidate.score);
	    
	    graph_out.goal = graph_out.add_node().id;
	  } else {
	    model.deallocate(candidate.state);
	    scores[graph_out.goal] = std::max(scores[graph_out.goal], candidate.score);
	  }
	  
	  // assign true head
	  candidate.out_edge.head = graph_out.goal;
	} else {
	  // we will merge states, but do not merge score/estimates, since we
	  // are enumerating jth best derivations... is this correct?
	  
	  typedef std::pair<state_node_map_type::iterator, bool> result_type;
	  
	  result_type result = state.nodes.insert(std::make_pair(candidate.state, 0));
	  if (result.second) {
	    node_states.push_back(candidate.state);
	    scores.push_back(candidate.score);
	    
	    result.first->second = graph_out.add_node().id;
	  } else {
	    model.deallocate(candidate.state);
	    scores[result.first->second] = std::max(scores[result.first->second], candidate.score);
	  }
	  
	  // assign true head
	  candidate.out_edge.head = result.first->second;
	}
	
	edge_type& edge = graph_out.add_edge(candidate.out_edge);
	graph_out.connect_edge(edge.id, candidate.out_edge.head);
	
	state.D.push_back(item);
      }
    }

    void push_buf(const candidate_type& __item, cand_state_type& state, const bool is_goal, hypergraph_type& graph_out)
    {
      // push this item into state.buf with "correct" score
      
#if 1
      state.buf.push(&__item);
#endif
#if 0
      candidate_type& candidate = const_cast<candidate_type&>(__item);
      
      candidate.state = model.apply(node_states, candidate.out_edge, candidate.out_edge.features, is_goal);
      
      candidate.score *= function(candidate.out_edge.features);
      candidate.score /= scores_edge[candidate.in_edge->head];
      
      state.buf.push(&candidate);
#endif
    }
    
    const candidate_type* make_candidate(const edge_type& edge, const index_set_type& j, cand_state_type& state, const bool is_goal, hypergraph_type& graph_out)
    {
      candidates.push_back(candidate_type(edge, j));
      
      candidate_type& candidate = candidates.back();
      
#if 1
      candidate.score = semiring::traits<score_type>::one();
      for (size_t i = 0; i != j.size(); ++ i) {
	const candidate_type& antecedent = *states[edge.tails[i]].D[j[i]];
	
	// assign real-node-id
	candidate.out_edge.tails[i] = antecedent.out_edge.head;
	candidate.score *= scores[antecedent.out_edge.head];
      }
      
      candidate.state = model.apply(node_states, candidate.out_edge, candidate.out_edge.features, is_goal);
      
      candidate.score *= function(candidate.out_edge.features);
#endif
      
#if 0
      candidate.score = scores_edge[edge.id];
      for (size_t i = 0; i != j.size(); ++ i) {
	const candidate_type& antecedent = *states[edge.tails[i]].D[j[i]];
	
	// assign real-node-id
	candidate.out_edge.tails[i] = antecedent.out_edge.head;
	candidate.score *= scores[antecedent.out_edge.head];
      }
#endif
      
      return &candidate;
    };
    

    void cube_prune(const hypergraph_type& graph)
    {
      // currently, we perform 1-best search... but it will be extended to cube-pruning...
      node_states_coarse.clear();
      node_states_coarse.reserve(graph.nodes.size());
      node_states_coarse.resize(graph.nodes.size());
      
      scores_edge.clear();
      scores_edge.reserve(graph.edges.size());
      scores_edge.resize(graph.edges.size(), semiring::traits<score_type>::zero());
      
      scores_node.clear();
      scores_node.reserve(graph.nodes.size());
      scores_node.resize(graph.nodes.size(), semiring::traits<score_type>::zero());
      
      hypergraph_type::node_set_type::const_iterator niter_end = graph.nodes.end();
      for (hypergraph_type::node_set_type::const_iterator niter = graph.nodes.begin(); niter != niter_end; ++ niter) {
	const node_type& node = *niter;

	const bool is_goal = (graph.goal == node.id);
	
	node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
	for (node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
	  const edge_type& edge = graph.edges[*eiter];
	  
	  feature_set_type features(edge.features);
	  
	  const state_type node_state = model.apply(node_states_coarse, edge, features, is_goal);
	  
	  score_type score = function(features);
	  
	  scores_edge[edge.id] = score;
	  
	  edge_type::node_set_type::const_iterator titer_end = edge.tails.end();
	  for (edge_type::node_set_type::const_iterator titer = edge.tails.begin(); titer != titer_end; ++ titer)
	    score *= scores_node[*titer];
	  
	  if (score > scores_node[node.id]) {
	    scores_node[node.id] = score;
	    model.deallocate(node_states_coarse[node.id]);
	    node_states_coarse[node.id] = node_state;
	  } else
	    model.deallocate(node_state);
	}
      }
      
      // deallocate model state...
      state_set_type::const_iterator siter_end = node_states_coarse.end();
      for (state_set_type::const_iterator siter = node_states_coarse.begin(); siter != siter_end; ++ siter)
	model.deallocate(*siter);
      
      scores_node.clear();
      node_states_coarse.clear();
    }
    
  private:
    candidate_set_type  candidates;
    
    state_set_type      node_states;
    score_set_type      scores;

    state_set_type      node_states_coarse;
    score_set_type      scores_node;
    score_set_type      scores_edge;
    
    cand_state_set_type states;
    
    const model_type& model;
    const function_type& function;
    size_type  cube_size_max;
  };
  
  template <typename Function>
  inline
  void apply_cube_grow(const Model& model, const HyperGraph& source, HyperGraph& target, const Function& func, const int cube_size)
  {
    ApplyCubeGrow<typename Function::value_type, Function>(model, func, cube_size)(source, target);
  }

  template <typename Function>
  inline
  void apply_cube_grow(const Model& model, HyperGraph& source, const Function& func, const int cube_size)
  {
    HyperGraph target;
    
    ApplyCubeGrow<typename Function::value_type, Function>(model, func, cube_size)(source, target);
    
    source.swap(target);
  }

};

#endif

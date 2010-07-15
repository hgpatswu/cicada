// -*- mode: c++ -*-

#ifndef __CICADA__SORT__HPP__
#define __CICADA__SORT__HPP__ 1

#include <vector>

#include <cicada/hypergraph.hpp>

#include <utils/sgi_hash_set.hpp>
#include <utils/hashmurmur.hpp>

namespace cicada
{
  struct TopologicallySort
  {
    typedef HyperGraph hypergraph_type;
    
    typedef hypergraph_type::id_type id_type;
    typedef hypergraph_type::node_type node_type;
    typedef hypergraph_type::edge_type edge_type;
    
    enum color_type {
      white,
      gray,
      black
    };

    struct dfs_type
    {
      id_type node;
      int edge;
      int tail;
      
      dfs_type(const id_type& _node, const int& _edge, const int& _tail) 
	: node(_node), edge(_edge), tail(_tail) {}
    };
    
    typedef std::vector<int, std::allocator<int> > reloc_set_type;
    typedef std::vector<color_type, std::allocator<color_type> > color_set_type;
    typedef std::vector<dfs_type, std::allocator<dfs_type> > stack_type;

    struct no_filter_edge
    {
      bool operator()(const edge_type& edge) const
      {
	return false;
      }
    };

    struct filter_edge
    {
      std::vector<bool, std::allocator<bool> > removed;

      filter_edge(size_t size) : removed(size, false) {}
      
      bool operator()(const edge_type& edge) const
      {
	return removed[edge.id];
      }
    };
    
    template <typename Filter>
    void operator()(const hypergraph_type& x, hypergraph_type& sorted, Filter filter)
    {
      sorted.clear();
      
      if (x.goal == hypergraph_type::invalid)
	return;
      
      reloc_set_type reloc_node(x.nodes.size(), -1);
      reloc_set_type reloc_edge(x.edges.size(), -1);
      color_set_type color(x.nodes.size(), white);
      stack_type stack;
      
      stack.reserve(x.nodes.size());
      stack.push_back(dfs_type(x.goal, 0, 0));
      
      int node_count = 0;
      int edge_count = 0;
      
      while (! stack.empty()) {
	const dfs_type& dfs = stack.back();
	id_type node_id = dfs.node;
	int pos_edge = dfs.edge;
	int pos_tail = dfs.tail;
	
	stack.pop_back();
	
	const node_type* curr_node = &(x.nodes[node_id]);
	
	while (pos_edge != curr_node->edges.size()) {
	  const edge_type& curr_edge = x.edges[curr_node->edges[pos_edge]];
	  
	  if (pos_tail == curr_edge.tails.size() || filter(curr_edge)) {
	    // reach end: proceed to the next edge with pos_tail initialized to the first tail
	    ++ pos_edge;
	    pos_tail = 0;
	    continue;
	  }
	  
	  const id_type tail_node = curr_edge.tails[pos_tail];
	  const color_type tail_color = color[tail_node];
	  
	  switch (tail_color) {
	  case white:
	    ++ pos_tail;
	    stack.push_back(dfs_type(node_id, pos_edge, pos_tail));
	    
	    node_id = tail_node;
	    curr_node = &(x.nodes[node_id]);
	    
	    color[node_id] = gray;
	    pos_edge = 0;
	    pos_tail = 0;
	    
	    break;
	  case black:
	    ++ pos_tail;
	    break;
	  case gray:
	    throw std::runtime_error("detected cycle!");
	    break;
	  }
	}
	
	for (int i = 0; i < curr_node->edges.size(); ++ i)
	  if (! filter(x.edges[curr_node->edges[i]]))
	    reloc_edge[curr_node->edges[i]] = edge_count ++;
	
	color[node_id] = black;
	reloc_node[node_id] = node_count ++;
      }
      
      // sorted graph!
      sorted.clear();
      
      // construct edges...
      for (int i = 0; i < reloc_edge.size(); ++ i)
	if (reloc_edge[i] >= 0) {
	  const edge_type& edge_old = x.edges[i];
	  
	  const id_type edge_id = sorted.edges.size();
	  
	  sorted.edges.push_back(edge_old);
	  
	  edge_type& edge_new = sorted.edges.back();
	  
	  edge_new.id = edge_id;
	  
	  edge_new.head = reloc_node[edge_new.head];
	  edge_type::node_set_type::iterator niter_end = edge_new.tails.end();
	  for (edge_type::node_set_type::iterator niter = edge_new.tails.begin(); niter != niter_end; ++ niter)
	    *niter = reloc_node[*niter];
	  
	  reloc_edge[i] = edge_id;
	}
      
      // construct reverse node-map ...
      reloc_set_type reloc_map_node(node_count, -1);
      for (int i = 0; i < x.nodes.size(); ++ i)
	if (reloc_node[i] >= 0)
	  reloc_map_node[reloc_node[i]] = i;


#ifdef HAVE_TR1_UNORDERED_SET 
      std::tr1::unordered_set<id_type, utils::hashmurmur<size_t>, std::equal_to<id_type>, std::allocator<id_type> > nodes_empty;
#else
      sgi::hash_set<id_type, utils::hashmurmur<size_t>, std::equal_to<id_type>, std::allocator<id_type> > nodes_empty;
#endif
      
      for (int i = 0; i < reloc_map_node.size(); ++ i) {
	const node_type& node_old = x.nodes[reloc_map_node[i]];
	node_type& node_new = sorted.add_node();
	
	node_type::edge_set_type::const_iterator eiter_end = node_old.edges.end();
	for (node_type::edge_set_type::const_iterator eiter = node_old.edges.begin(); eiter != eiter_end; ++ eiter)
	  if (reloc_edge[*eiter] >= 0)
	    node_new.edges.push_back(reloc_edge[*eiter]);
	
	if (node_new.edges.empty())
	  nodes_empty.insert(node_new.id);
      }
      
      sorted.goal = sorted.nodes.size() - 1;
      
      if (! nodes_empty.empty()) {
	hypergraph_type sorted_new;
	filter_edge filter(sorted.edges.size());
	
	for (typename hypergraph_type::edge_set_type::const_iterator eiter = sorted.edges.begin(); eiter != sorted.edges.end(); ++ eiter) {
	  const edge_type& edge = *eiter;
	  
	  typename edge_type::node_set_type::const_iterator titer_end = edge.tails.end();
	  for (typename edge_type::node_set_type::const_iterator titer = edge.tails.begin(); titer != titer_end; ++ titer)
	    if (nodes_empty.find(*titer) != nodes_empty.end()) {
	      filter.removed[edge.id] = true;
	      break;
	    }
	}
	
	operator()(sorted, sorted_new, filter);
	
	sorted.swap(sorted_new);
      }
    }
  };
  
  template <typename Filter>
  inline
  void topologically_sort(const HyperGraph& source, HyperGraph& target, Filter filter)
  {
    TopologicallySort()(source, target, filter);
  }
  
  inline
  void topologically_sort(const HyperGraph& source, HyperGraph& target)
  {
    TopologicallySort()(source, target, TopologicallySort::no_filter_edge());
  }
  
  inline
  void topologically_sort(HyperGraph& graph)
  {
    HyperGraph x;
    topologically_sort(graph, x);
    graph.swap(x);
  }
};

#endif

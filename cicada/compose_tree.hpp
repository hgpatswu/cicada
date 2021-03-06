// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__COMPOSE_TREE__HPP__
#define __CICADA__COMPOSE_TREE__HPP__ 1

#include <vector>
#include <deque>
#include <algorithm>
#include <sstream>

#include <cicada/symbol.hpp>
#include <cicada/sentence.hpp>
#include <cicada/vocab.hpp>
#include <cicada/tree_grammar.hpp>
#include <cicada/tree_transducer.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/hypergraph.hpp>

#include <utils/array_power2.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/hashmurmur3.hpp>
#include <utils/unordered_set.hpp>
#include <utils/unordered_map.hpp>
#include <utils/bithack.hpp>
#include <utils/indexed_set.hpp>
#include <utils/compact_map.hpp>
#include <utils/compact_set.hpp>
#include <utils/small_vector.hpp>
#include <utils/simple_vector.hpp>
#include <utils/alloc_vector.hpp>

#include <boost/fusion/tuple.hpp>

//
// hypergraph-to-hypergraph transduction by
//
//@InProceedings{zhang-EtAl:2009:EMNLP1,
//    author    = {Zhang, Hui  and  Zhang, Min  and  Li, Haizhou  and  Tan, Chew Lim},
//    title     = {Fast Translation Rule Matching for Syntax-based Statistical Machine Translation},
//    booktitle = {Proceedings of the 2009 Conference on Empirical Methods in Natural Language Processing},
//    month     = {August},
//    year      = {2009},
//    address   = {Singapore},
//    publisher = {Association for Computational Linguistics},
//    pages     = {1037--1045},
//    url       = {http://www.aclweb.org/anthology/D/D09/D09-1108}
//}
//
// The terminologies used in the above paper is somewhat confusing:
//   input-forest, input-hypergraph, encoded hyper-path etc.
//
// The algorithm computes:
//  for each node, try matching with tree fragments
//  when matched, the match is represented by a set of hypergraph-node-id of input-hypergraph
//  and a set of matched rules.
//  We can uncover output hypergraph by enumerating matched rules.
//  Book-keep the matched input-hypergraph in a chart so that we can construct translational packed forest.
// 
// TODO
// support phrasal/synchronous-CFG style rules:
//   We will rune CKY and try match syntactically constrained phrases indicated by source spans...

namespace cicada
{
  
  struct ComposeTree
  {
    typedef Symbol symbol_type;
    typedef Vocab  vocab_type;
    
    typedef Sentence sentence_type;
    typedef Sentence phrase_type;

    typedef TreeGrammar    tree_grammar_type;
    typedef TreeTransducer tree_transducer_type;
    
    typedef Grammar    grammar_type;
    typedef Transducer transducer_type;
    
    typedef HyperGraph     hypergraph_type;
    
    typedef hypergraph_type::feature_set_type   feature_set_type;
    typedef hypergraph_type::attribute_set_type attribute_set_type;

    typedef hypergraph_type::rule_type        rule_type;
    typedef hypergraph_type::rule_ptr_type    rule_ptr_type;
    
    typedef tree_transducer_type::rule_pair_set_type tree_rule_pair_set_type;
    typedef tree_transducer_type::rule_pair_type     tree_rule_pair_type;
    typedef tree_transducer_type::rule_type          tree_rule_type;
    typedef tree_transducer_type::rule_ptr_type      tree_rule_ptr_type;
    
    typedef std::vector<tree_transducer_type::edge_type, std::allocator<tree_transducer_type::edge_type> > edge_set_type;
    
    typedef std::vector<tree_transducer_type::id_type, std::allocator<tree_transducer_type::id_type> > node_queue_type;
    
    typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > frontier_type;
    typedef std::deque<frontier_type, std::allocator<frontier_type> > frontier_queue_type;
    
    typedef std::deque<feature_set_type, std::allocator<feature_set_type> >    feature_queue_type;
    typedef std::deque<attribute_set_type, std::allocator<attribute_set_type> > attribute_queue_type;

    typedef feature_set_type::feature_type     feature_type;
    typedef attribute_set_type::attribute_type attribute_type;
    
    // for phrasal matching...
    typedef utils::unordered_set<phrase_type, boost::hash<phrase_type>,  std::equal_to<phrase_type>, std::allocator<phrase_type> >::type phrase_unique_type;
    
    typedef utils::simple_vector<const phrase_type*, std::allocator<const phrase_type*> > phrase_set_type;
    typedef std::vector<phrase_set_type, std::allocator<phrase_set_type> > phrase_map_type;
    
    typedef hypergraph_type::edge_type::node_set_type tail_set_type;
    typedef rule_type::symbol_set_type                symbol_set_type;
    
    template <typename Seq>
    struct hash_sequence : utils::hashmurmur3<size_t>
    {
      typedef utils::hashmurmur3<size_t> hasher_type;
      
      size_t operator()(const Seq& x) const
      {
	return hasher_type::operator()(x.begin(), x.end(), 0);
      }
    };
    
    typedef utils::indexed_set<tail_set_type, hash_sequence<tail_set_type>, std::equal_to<tail_set_type>,
			       std::allocator<tail_set_type> > internal_tail_set_type;
    typedef utils::indexed_set<symbol_set_type, hash_sequence<symbol_set_type>, std::equal_to<symbol_set_type>,
			       std::allocator<symbol_set_type> > internal_symbol_set_type;

    typedef boost::fusion::tuple<internal_tail_set_type::index_type, internal_symbol_set_type::index_type, symbol_type> internal_label_type;
    typedef boost::fusion::tuple<hypergraph_type::id_type, internal_symbol_set_type::index_type, symbol_type> terminal_label_type;
    
    template <typename Tp>
    struct unassigned_key : utils::unassigned<symbol_type>
    {
      Tp operator()() const { return Tp(-1, -1, utils::unassigned<symbol_type>::operator()()); }
    };

    typedef utils::compact_map<internal_label_type, hypergraph_type::id_type,
			       unassigned_key<internal_label_type>, unassigned_key<internal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<internal_label_type>,
			       std::allocator<std::pair<const internal_label_type, hypergraph_type::id_type> > > internal_label_map_type;
    typedef utils::compact_map<terminal_label_type, hypergraph_type::id_type,
			       unassigned_key<terminal_label_type>, unassigned_key<terminal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<terminal_label_type>,
			       std::allocator<std::pair<const terminal_label_type, hypergraph_type::id_type> > > terminal_label_map_type;

    typedef utils::alloc_vector<terminal_label_map_type, std::allocator<terminal_label_map_type> > terminal_label_map_set_type;

    template <typename Tp>
    struct ptr_hash
    {
      size_t operator()(const boost::shared_ptr<Tp>& x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }

      size_t operator()(const Tp* x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }
    };
    
    template <typename Tp>
    struct ptr_equal_to
    {
      bool operator()(const boost::shared_ptr<Tp>& x, const boost::shared_ptr<Tp>& y) const
      {
	return x == y ||(x && y && *x == *y);
      }
      
      bool operator()(const Tp* x, const Tp* y) const
      {
	return x == y ||(x && y && *x == *y);
      }
    };

    typedef utils::unordered_map<const rule_type*, rule_ptr_type, ptr_hash<rule_type>, ptr_equal_to<rule_type>,
				 std::allocator<std::pair<const rule_type* const, rule_ptr_type> > >::type rule_cache_type;

    typedef utils::unordered_map<rule_ptr_type, std::string, ptr_hash<rule_type>, ptr_equal_to<rule_type>,
				 std::allocator<std::pair<const rule_ptr_type, std::string> > >::type frontier_set_type;
    
    typedef utils::unordered_map<tree_rule_ptr_type, std::string, ptr_hash<tree_rule_type>, ptr_equal_to<tree_rule_type>,
				 std::allocator<std::pair<const tree_rule_ptr_type, std::string> > >::type tree_frontier_set_type;
    
    struct State
    {
      frontier_type            frontier;
      feature_set_type         features;
      attribute_set_type       attributes;
      tree_transducer_type::id_type node;
      
      State() : frontier(), features(), attributes(), node(tree_transducer_type::id_type(-1)) {}
      State(const frontier_type& __frontier,
	    const tree_transducer_type::id_type& __node)
	: frontier(__frontier), features(), attributes(), node(__node) {}
      State(const frontier_type& __frontier,
	    const feature_set_type& __features,
	    const attribute_set_type& __attributes,
	    const tree_transducer_type::id_type& __node)
      : frontier(__frontier), features(__features), attributes(__attributes), node(__node) {}
    };
    typedef State state_type;

    typedef std::deque<state_type, std::allocator<state_type> > queue_type;
    
    typedef utils::compact_map<symbol_type, hypergraph_type::id_type,
			       utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			       boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, hypergraph_type::id_type> > > node_map_type;
    typedef std::vector<node_map_type, std::allocator<node_map_type> > node_map_set_type;

    typedef std::vector<bool, std::allocator<bool> > connected_type;

    struct phrase_cache_type
    {
      typedef utils::small_vector<transducer_type::id_type, std::allocator<transducer_type::id_type> > node_set_type;
      
      const phrase_type* phrase;
      node_set_type      nodes;
      phrase_cache_type() : phrase(), nodes() {}
    };
    typedef utils::array_power2<phrase_cache_type, 1024 * 4, std::allocator<phrase_cache_type> > phrase_cache_set_type;
    
    ComposeTree(const symbol_type& __goal,
		const tree_grammar_type& __tree_grammar,
		const grammar_type& __grammar,
		const bool __yield_source,
		const bool __frontier)
      : goal(__goal),
	tree_grammar(__tree_grammar), 
	grammar(__grammar),
	yield_source(__yield_source),
	frontier_attribute(__frontier),
	attr_internal_node("internal-node"),
	attr_source_root("source-root"),
	attr_tree_fallback("tree-fallback"),
	attr_glue_tree(__grammar.empty() ? "" : "glue-tree"),
	attr_glue_tree_fallback("glue-tree-fallback"),
	attr_frontier_source(__frontier ? "frontier-source" : ""),
        attr_frontier_target(__frontier ? "frontier-target" : "")
    {  
      goal_rule = rule_type::create(rule_type(vocab_type::GOAL,
					      rule_type::symbol_set_type(1, goal.non_terminal())));
    }
    
    void operator()(const hypergraph_type& graph_in, hypergraph_type& graph_out)
    {
      graph_out.clear();
      
      if (! graph_in.is_valid()) return;
      
      node_map.clear();
      node_map.reserve(graph_in.nodes.size());
      node_map.resize(graph_in.nodes.size());

      node_map_phrase.clear();
      node_map_phrase.reserve(graph_in.nodes.size());
      node_map_phrase.resize(graph_in.nodes.size());
      
      node_map_fallback.clear();
      node_map_fallback.reserve(graph_in.nodes.size());
      node_map_fallback.resize(graph_in.nodes.size());

      connected.clear();
      
      phrases.clear();

      phrase_map.clear();
      phrase_map.reserve(graph_in.nodes.size());
      phrase_map.resize(graph_in.nodes.size());

      tail_map.clear();
      symbol_map.clear();
      symbol_map_terminal.clear();
      label_map.clear();
      terminal_map_local.clear();
      terminal_map_global.clear();

      rule_cache.clear();

      frontiers_source.clear();
      frontiers_target.clear();
      tree_frontiers_source.clear();
      tree_frontiers_target.clear();

      caches_phrase.clear();
      
      // bottom-up topological order
      for (size_t id = 0; id != graph_in.nodes.size(); ++ id) {
	terminal_map_local.clear();
	
	match_tree(id, graph_in, graph_out);
	
	if (! grammar.empty())
	  match_phrase(id, graph_in, graph_out);
      }
      
      if (! grammar.empty()) {
	// connect all the node_map_phrase with node_map
	
	for (size_t id = 0; id != graph_in.nodes.size(); ++ id)
	  if (! node_map_phrase[id].empty() && ! node_map[id].empty()) {
	    const symbol_type& root_label = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;
	    
	    node_map_type::const_iterator piter_end = node_map[id].end();
	    for (node_map_type::const_iterator piter = node_map[id].begin(); piter != piter_end; ++ piter) {
	      node_map_type::const_iterator citer_end = node_map_phrase[id].end();
	      for (node_map_type::const_iterator citer = node_map_phrase[id].begin(); citer != citer_end; ++ citer) {
		
		hypergraph_type::edge_type& edge = graph_out.add_edge(&citer->second, (&citer->second) + 1);
		
		edge.rule = rule_type::create(rule_type(piter->first, rule_type::symbol_set_type(1, citer->first)));
		
		edge.attributes[attr_source_root] = static_cast<const std::string&>(root_label);
		edge.attributes[attr_glue_tree] = attribute_set_type::int_type(1);
		
		graph_out.connect_edge(edge.id, piter->second);
		
#if 0
		// this is not necessary!
		if (citer->second >= connected.size())
		  connected.resize(citer->second + 1, false);
		
		connected[citer->second] = true;
#endif
	      }
	    }
	  }
      }
      
      // goal node... the goal must be mapped goal...
      node_map_type::const_iterator niter = node_map[graph_in.goal].find(goal.non_terminal());
      if (niter != node_map[graph_in.goal].end()) {
	// goal node...
	graph_out.goal = graph_out.add_node().id;
	
	// hyperedge to goal...
	hypergraph_type::edge_type& edge = graph_out.add_edge(&(niter->second), (&(niter->second)) + 1);
	edge.rule = goal_rule;
	
	// connect!
	graph_out.connect_edge(edge.id, graph_out.goal);
	
	if (niter->second >= connected.size())
	  connected.resize(niter->second + 1, false);
	
	connected[niter->second] = true;
	
	// final patch work....!
	for (size_t id = 0; id != graph_in.nodes.size(); ++ id)
	  if (! node_map_fallback[id].empty() && ! node_map[id].empty()) {
	    const symbol_type& root_label = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;

	    node_map_type::const_iterator piter_end = node_map_fallback[id].end();
	    for (node_map_type::const_iterator piter = node_map_fallback[id].begin(); piter != piter_end; ++ piter)
	      if (connected[piter->second]) {
		node_map_type::const_iterator citer_end = node_map[id].end();
		for (node_map_type::const_iterator citer = node_map[id].begin(); citer != citer_end; ++ citer)
		  if (! connected[citer->second] && node_map_fallback[id].find(citer->first) == node_map_fallback[id].end()) {
		    hypergraph_type::edge_type& edge = graph_out.add_edge(&citer->second, (&citer->second) + 1);
		    
		    edge.rule = rule_type::create(rule_type(piter->first, rule_type::symbol_set_type(1, citer->first)));
		    
		    edge.attributes[attr_source_root] = static_cast<const std::string&>(root_label);
		    edge.attributes[attr_glue_tree_fallback] = attribute_set_type::int_type(1);
		    
		    graph_out.connect_edge(edge.id, piter->second);
		  }
	      }
	  }
      }
      
      node_map.clear();
      node_map_phrase.clear();
      
      if (graph_out.is_valid())
	graph_out.topologically_sort();
    }

    void match_phrase(const int id, const hypergraph_type& graph_in, hypergraph_type& graph_out)
    {
      typedef std::deque<phrase_type, std::allocator<phrase_type> >  buffer_type;
      typedef utils::compact_set<const phrase_type*,
				 utils::unassigned<const phrase_type*>, utils::unassigned<const phrase_type*>,
				 boost::hash<const phrase_type*>, std::equal_to<const phrase_type*>,
				 std::allocator<const phrase_type*> > buffer_unique_type;

      if (graph_in.nodes[id].edges.empty()) return;
      
      // first, construct prases

      buffer_type buffer;
      buffer_type buffer_next;
      
      buffer_unique_type buffer_unique;
      
      hypergraph_type::node_type::edge_set_type::const_iterator eiter_end = graph_in.nodes[id].edges.end();
      for (hypergraph_type::node_type::edge_set_type::const_iterator eiter = graph_in.nodes[id].edges.begin(); eiter != eiter_end; ++ eiter) {
	const hypergraph_type::edge_type& edge = graph_in.edges[*eiter];
	
	buffer.clear();
	buffer.push_back(phrase_type());

	int non_terminal_pos = 0;
	rule_type::symbol_set_type::const_iterator titer_end = edge.rule->rhs.end();
	for (rule_type::symbol_set_type::const_iterator titer = edge.rule->rhs.begin(); titer != titer_end; ++ titer)
	  if (titer->is_non_terminal()) {
	    const int __non_terminal_index = titer->non_terminal_index();
	    const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	    ++ non_terminal_pos;
	    
	    // combine buffer and tails...
	    buffer_next.clear();
	    
	    phrase_set_type::const_iterator piter_end = phrase_map[edge.tails[pos]].end();
	    for (phrase_set_type::const_iterator piter = phrase_map[edge.tails[pos]].begin(); piter != piter_end; ++ piter) {
	      buffer_type::const_iterator biter_end = buffer.end();
	      for (buffer_type::const_iterator biter = buffer.begin(); biter != biter_end; ++ biter) {
		buffer_next.push_back(*biter);
		buffer_next.back().insert(buffer_next.back().end(), (*piter)->begin(), (*piter)->end());
	      }
	    }
	    
	    buffer.swap(buffer_next);
	  } else if (*titer != vocab_type::EPSILON) {
	    buffer_type::iterator biter_end = buffer.end();
	    for (buffer_type::iterator biter = buffer.begin(); biter != biter_end; ++ biter)
	      biter->push_back(*titer);
	  }
	
	buffer_type::const_iterator biter_end = buffer.end();
	for (buffer_type::const_iterator biter = buffer.begin(); biter != biter_end; ++ biter)
	  buffer_unique.insert(&(*(phrases.insert(*biter).first)));
      }
      
      phrase_map[id] = phrase_set_type(buffer_unique.begin(), buffer_unique.end());
      
      // then, try matching within this span...
      
      const symbol_type& root_label = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;
      
      phrase_set_type::const_iterator piter_end = phrase_map[id].end();
      for (phrase_set_type::const_iterator piter = phrase_map[id].begin(); piter != piter_end; ++ piter) {
	const phrase_type* phrase_ptr = *piter;
	const phrase_type& phrase = *phrase_ptr;
	
	if (phrase.empty()) continue;
	
	const size_t cache_pos = boost::hash<const phrase_type*>()(phrase_ptr) & (caches_phrase.size() - 1);
	
	phrase_cache_type& cache = caches_phrase[cache_pos];
	
	if (cache.nodes.empty() || cache.phrase != phrase_ptr) {
	  cache.phrase = phrase_ptr;
	  cache.nodes.resize(grammar.size());
	  
	  for (size_t grammar_id = 0; grammar_id != grammar.size(); ++ grammar_id) {
	    const transducer_type& transducer = grammar[grammar_id];
	    
	    transducer_type::id_type node = transducer.root();
	    
	    phrase_type::const_iterator iter_end = phrase.end();
	    for (phrase_type::const_iterator iter = phrase.begin(); iter != iter_end; ++ iter) {
	      node = transducer.next(node, *iter);
	      if (node == transducer.root()) break;
	    }
	    
	    cache.nodes[grammar_id] = node;
	  }
	}
	
	for (size_t grammar_id = 0; grammar_id != grammar.size(); ++ grammar_id) {
	  const transducer_type& transducer = grammar[grammar_id];
	  const transducer_type::id_type node = cache.nodes[grammar_id];
	  
	  if (node == transducer.root()) continue;
	  
	  const transducer_type::rule_pair_set_type& rules = transducer.rules(node);
	  
	  if (rules.empty()) continue;
	  
	  transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
	  for (transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
	    
	    const rule_ptr_type rule = (yield_source ? riter->source : riter->target);
	    
	    hypergraph_type::edge_type& edge = graph_out.add_edge();
	    edge.rule = rule;
	    edge.features = riter->features;
	    edge.attributes = riter->attributes;
	    
	    edge.attributes[attr_source_root] = static_cast<const std::string&>(root_label);
	    
	    if (frontier_attribute) {
	      const rule_ptr_type& rule_source = riter->source;
	      const rule_ptr_type& rule_target = riter->target;

	      if (rule_source) {
		frontier_set_type::iterator siter = frontiers_source.find(rule_source);
		if (siter == frontiers_source.end()) {
		  std::ostringstream os;
		  os << rule_source->rhs;
		  
		  siter = frontiers_source.insert(std::make_pair(rule_source, os.str())).first;
		}
		
		edge.attributes[attr_frontier_source] = siter->second;
	      }
	      
	      if (rule_target) {
		frontier_set_type::iterator titer = frontiers_target.find(rule_target);
		if (titer == frontiers_target.end()) {
		  std::ostringstream os;
		  os << rule_target->rhs;
	  
		  titer = frontiers_target.insert(std::make_pair(rule_target, os.str())).first;
		}
	
		edge.attributes[attr_frontier_target] = titer->second;
	      }
	    }
	    
	    std::pair<node_map_type::iterator, bool> result = node_map_phrase[id].insert(std::make_pair(edge.rule->lhs, 0));
	    if (result.second)
	      result.first->second = graph_out.add_node().id;
	    
	    graph_out.connect_edge(edge.id, result.first->second);
	  }
	}
      }
    }

    struct FrontierIterator
    {
      FrontierIterator(std::string& __buffer) : buffer(__buffer) {}
      
      FrontierIterator& operator=(const std::string& value)
      {
	if (! buffer.empty())
	  buffer += ' ';
	buffer += value;
	return *this;
      }
      
      FrontierIterator& operator*() { return *this; }
      FrontierIterator& operator++() { return *this; }
      
      std::string& buffer;
    };
    
    void match_tree(const int id, const hypergraph_type& graph_in, hypergraph_type& graph_out)
    {
      if (graph_in.nodes[id].edges.empty()) return;
      
      //std::cerr << "node: " << id << std::endl;
      
      queue_type queue;
      
      for (size_t grammar_id = 0; grammar_id != tree_grammar.size(); ++ grammar_id) {
	const tree_transducer_type& transducer = tree_grammar[grammar_id];

	//std::cerr << "transducer: " << grammar_id << std::endl;
	
	const symbol_type cat = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;
	const tree_transducer_type::edge_type edge_id = transducer.edge(cat);
	
	if (edge_id == tree_transducer_type::edge_type()) continue;
	
	const tree_transducer_type::edge_type edge_epsilon = transducer.edge(vocab_type::EPSILON);
	const tree_transducer_type::edge_type edge_none    = transducer.edge(vocab_type::NONE);
	
	tree_transducer_type::id_type node = transducer.next(transducer.root(), edge_id);
	if (node == transducer.root()) continue;
	
	node = transducer.next(node, edge_none);
	if (node == transducer.root()) continue;
	
	//std::cerr << "grammar cat: " << cat << " id: " << edge_id << std::endl;
	
	queue.clear();
	queue.push_back(state_type(frontier_type(1, id), node));
	
	while (! queue.empty()) {
	  const state_type& state = queue.front();
	  
	  frontier_queue_type frontiers(1, frontier_type());
	  frontier_queue_type frontiers_next;
	  
	  node_queue_type nodes(1, state.node);
	  node_queue_type nodes_next;

	  feature_queue_type features(1, state.features);
	  feature_queue_type features_next;
	  
	  attribute_queue_type attributes(1, state.attributes);
	  attribute_queue_type attributes_next;

	  edge_set_type edges;
	  
	  frontier_type::const_iterator niter_end = state.frontier.end();
	  for (frontier_type::const_iterator niter = state.frontier.begin(); niter != niter_end; ++ niter) {
	    
	    frontiers_next.clear();
	    nodes_next.clear();
	    features_next.clear();
	    attributes_next.clear();
	    
	    edges.clear();
	    hypergraph_type::node_type::edge_set_type::const_iterator eiter_end = graph_in.nodes[*niter].edges.end();
	    for (hypergraph_type::node_type::edge_set_type::const_iterator eiter = graph_in.nodes[*niter].edges.begin(); eiter != eiter_end; ++ eiter) {
	      const hypergraph_type::edge_type& edge = graph_in.edges[*eiter];
	      
	      edges.push_back(transducer.edge(edge.rule->rhs));
	    }
	    
	    frontier_queue_type::const_iterator  fiter = frontiers.begin();
	    feature_queue_type::const_iterator   siter = features.begin();
	    attribute_queue_type::const_iterator aiter = attributes.begin();

	    node_queue_type::const_iterator titer_end = nodes.end();
	    for (node_queue_type::const_iterator titer = nodes.begin(); titer != titer_end; ++ titer, ++ fiter, ++ siter, ++ aiter) {
	      const tree_transducer_type::id_type node_epsilon = transducer.next(*titer, edge_epsilon);
	      if (node_epsilon != transducer.root()) {
		frontier_type frontier(*fiter);
		frontier.push_back(*niter);
		
		frontiers_next.push_back(frontier);
		nodes_next.push_back(node_epsilon);
		features_next.push_back(*siter);
		attributes_next.push_back(*aiter);
	      }

	      edge_set_type::const_iterator eiter_begin = edges.begin();
	      edge_set_type::const_iterator eiter_end = edges.end();
	      for (edge_set_type::const_iterator eiter = eiter_begin; eiter != eiter_end; ++ eiter)
		if (*eiter != tree_transducer_type::edge_type()) {
		  const tree_transducer_type::edge_type& edge_id = *eiter;
		  
		  const tree_transducer_type::id_type node_edge = transducer.next(*titer, edge_id);
		  if (node_edge != transducer.root()) {
		    const hypergraph_type::edge_type& edge = graph_in.edges[graph_in.nodes[*niter].edges[eiter - eiter_begin]];
		    
		    frontier_type frontier(*fiter);
		    frontier.insert(frontier.end(), edge.tails.begin(), edge.tails.end());
		    
		    frontiers_next.push_back(frontier);
		    nodes_next.push_back(node_edge);
		    features_next.push_back(*siter + edge.features);
		    attributes_next.push_back(*aiter + edge.attributes);
		  }
		}
	    }
	    
	    frontiers.swap(frontiers_next);
	    nodes.swap(nodes_next);
	    features.swap(features_next);
	    attributes.swap(attributes_next);
	    
	    frontiers_next.clear();
	    nodes_next.clear();
	    features_next.clear();
	    attributes_next.clear();
	  }

	  //std::cerr << "finished loop: " << frontiers.size() << std::endl;
	  
	  // frontiers and nodes contain new frontier!
	  // in addition, we need to traverse transducer.next() with edge_none!
	  
	  frontier_queue_type::const_iterator  fiter = frontiers.begin();
	  feature_queue_type::const_iterator   siter = features.begin();
	  attribute_queue_type::const_iterator aiter = attributes.begin();
	  
	  node_queue_type::const_iterator titer_end = nodes.end();
	  for (node_queue_type::const_iterator titer = nodes.begin(); titer != titer_end; ++ titer, ++ fiter, ++ siter, ++ aiter) {
	    const tree_transducer_type::id_type node_none = transducer.next(*titer, edge_none);
	    if (node_none == transducer.root()) continue;
	    
	    queue.push_back(state_type(*fiter, node_none));
	    
	    const tree_transducer_type::rule_pair_set_type& rules = transducer.rules(node_none);
	    
	    if (rules.empty()) continue;
	    
	    // try match with rules with *fiter == frontier-nodes and generate graph_out!
	    if (frontier_attribute) {
	      tree_transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
	      for (tree_transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter) {
		const tree_rule_ptr_type& rule_source = riter->source;
		const tree_rule_ptr_type& rule_target = riter->target;

		attribute_set_type attributes = riter->attributes + *aiter;
		
		if (rule_source) {
		  tree_frontier_set_type::iterator siter = tree_frontiers_source.find(rule_source);
		  if (siter == tree_frontiers_source.end()) {
		    std::string frontier;
		    rule_source->frontier(FrontierIterator(frontier));
		    
		    siter = tree_frontiers_source.insert(std::make_pair(rule_source, frontier)).first;
		  }
		  
		  attributes[attr_frontier_source] = siter->second;
		}

		if (rule_target) {
		  tree_frontier_set_type::iterator titer = tree_frontiers_target.find(rule_target);
		  if (titer == tree_frontiers_target.end()) {
		    std::string frontier;
		    rule_target->frontier(FrontierIterator(frontier));
		    
		    titer = tree_frontiers_target.insert(std::make_pair(rule_target, frontier)).first;
		  }
		  
		  attributes[attr_frontier_target] = titer->second;
		}
		
		apply_rule(yield_source ? *riter->source : *riter->target,
			   id,
			   *fiter,
			   riter->features + *siter,
			   attributes,
			   riter->source->size_internal(),
			   graph_in,
			   graph_out);
		
	      }
	    } else {
	      tree_transducer_type::rule_pair_set_type::const_iterator riter_end = rules.end();
	      for (tree_transducer_type::rule_pair_set_type::const_iterator riter = rules.begin(); riter != riter_end; ++ riter)
		apply_rule(yield_source ? *riter->source : *riter->target,
			   id,
			   *fiter,
			   riter->features + *siter,
			   riter->attributes + *aiter,
			   riter->source->size_internal(),
			   graph_in,
			   graph_out);
	    }
	  }
	  
	  queue.pop_front();
	}
      }
    }
    
    
    void apply_rule(const tree_rule_type& rule,
		    const hypergraph_type::id_type root_in,
		    const frontier_type& frontiers,
		    const feature_set_type& features,
		    const attribute_set_type& attributes,
		    const attribute_set_type::int_type& size_internal,
		    const hypergraph_type& graph_in,
		    hypergraph_type& graph_out)
    {
      // apply rule for the frontiers with root at graph_in and generate graph_out...
      // frontiers are ordered wrt source side rule's frontier nodes (in pre-traversal order),## the same as post-traversal order...
      //
      // collect frontiers...
      
      //
      // construct graph_out in pre-order...
      //
      
      //std::cerr << "apply rule pair: " << *rule_pair.source << " ||| " << *rule_pair.target << std::endl;
      
      const symbol_type& root_label = graph_in.edges[graph_in.nodes[root_in].edges.front()].rule->lhs;
      
      std::pair<node_map_type::iterator, bool> result = node_map[root_in].insert(std::make_pair(rule.label.non_terminal(), 0));
      if (result.second)
	result.first->second = graph_out.add_node().id;
      
      const bool is_fallback = is_tree_fallback(attributes);
      
      int non_terminal_pos = 0;
      hypergraph_type::id_type node_prev = hypergraph_type::invalid;
      int relative_pos = 0;
      
      const hypergraph_type::id_type edge_id = construct_graph(rule, result.first->second, frontiers, graph_out, non_terminal_pos, node_prev, relative_pos, is_fallback);
      
      graph_out.edges[edge_id].features   = features;
      graph_out.edges[edge_id].attributes = attributes;
      
      // root-label is assigned to source-root attribute
      graph_out.edges[edge_id].attributes[attr_source_root] = static_cast<const std::string&>(root_label);
      
      if (size_internal)
	graph_out.edges[edge_id].attributes[attr_internal_node] = size_internal;
    }
    
    hypergraph_type::id_type construct_graph(const tree_rule_type& rule,
					     hypergraph_type::id_type root,
					     const frontier_type& frontiers,
					     hypergraph_type& graph,
					     int& non_terminal_pos,
					     hypergraph_type::id_type& node_prev,
					     int& relative_pos,
					     const bool is_fallback)
    {
      typedef std::vector<symbol_type, std::allocator<symbol_type> > rhs_type;
      typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > tails_type;
      
      rhs_type rhs;
      tails_type tails;
      
      tree_rule_type::const_iterator aiter_end = rule.end();
      for (tree_rule_type::const_iterator aiter = rule.begin(); aiter != aiter_end; ++ aiter)
	if (aiter->label.is_non_terminal()) {
	  if (aiter->antecedents.empty()) {
	    const int __non_terminal_index = aiter->label.non_terminal_index();
	    const int non_terminal_index = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	    ++ non_terminal_pos;
	    
	    if (non_terminal_index >= static_cast<int>(frontiers.size()))
	      throw std::runtime_error("non-terminal index exceeds frontier size");
	    
	    const hypergraph_type::id_type node = frontiers[non_terminal_index];
	    
	    std::pair<node_map_type::iterator, bool> result = node_map[node].insert(std::make_pair(aiter->label.non_terminal(), 0));
	    if (result.second)
	      result.first->second = graph.add_node().id;
	    
	    if (is_fallback)
	      node_map_fallback[node].insert(*result.first);
	    
	    tails.push_back(result.first->second);
	    
	    node_prev = tails.back();
	    relative_pos = 0;
	  } else {
	    const hypergraph_type::id_type edge_id = construct_graph(*aiter,
								     hypergraph_type::invalid,
								     frontiers,
								     graph,
								     non_terminal_pos,
								     node_prev,
								     relative_pos,
								     is_fallback);
	    const hypergraph_type::id_type node_id = graph.edges[edge_id].head;
	    
	    tails.push_back(node_id);
	  }
	  
	  rhs.push_back(aiter->label.non_terminal());
	} else {
	  rhs.push_back(aiter->label);
	}
      
      hypergraph_type::id_type edge_id;
      
      if (root == hypergraph_type::invalid) {
	// we will share internal nodes

	if (! tails.empty()) {
	  internal_tail_set_type::iterator   titer = tail_map.insert(tail_set_type(tails.begin(), tails.end())).first;
	  internal_symbol_set_type::iterator siter = symbol_map.insert(symbol_set_type(rhs.begin(), rhs.end())).first;

	  typedef std::pair<internal_label_map_type::iterator, bool> result_type;
	  
	  result_type result = label_map.insert(std::make_pair(internal_label_type(titer - tail_map.begin(),
										   siter - symbol_map.begin(),
										   rule.label), 0));
	  
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;
	    
	    tails_type::const_iterator titer_end = tails.end();
	    for (tails_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
	      if (*titer >= connected.size())
		connected.resize(*titer + 1, false);
	      
	      connected[*titer] = true;
	    }
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	} else {
	  internal_symbol_set_type::iterator siter = symbol_map_terminal.insert(symbol_set_type(rhs.begin(), rhs.end())).first;

	  if (node_prev != hypergraph_type::invalid && node_prev >= terminal_map_global.size())
	    terminal_map_global.resize(node_prev + 1);
	  
	  terminal_label_map_type& terminal_map = (node_prev == hypergraph_type::invalid
						   ? terminal_map_local
						   : terminal_map_global[node_prev]);
	  
	  typedef std::pair<terminal_label_map_type::iterator, bool> result_type;
	  
	  result_type result = terminal_map.insert(std::make_pair(terminal_label_type(relative_pos ++,
										      siter - symbol_map_terminal.begin(),
										      rule.label), 0));
	  
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	}
      } else {
	edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	
	graph.edges[edge_id].rule = construct_rule(rule_type(rule.label, rhs.begin(), rhs.end()));
	graph.connect_edge(edge_id, root);
	
	tails_type::const_iterator titer_end = tails.end();
	for (tails_type::const_iterator titer = tails.begin(); titer != titer_end; ++ titer) {
	  if (*titer >= connected.size())
	    connected.resize(*titer + 1, false);
	  
	  connected[*titer] = true;
	}
      }
      
      return edge_id;
    }

    rule_ptr_type construct_rule(const rule_type& rule)
    {
      rule_cache_type::iterator riter = rule_cache.find(&rule);
      if (riter == rule_cache.end()) {
	const rule_ptr_type rule_ptr(rule_type::create(rule));
	
	riter = rule_cache.insert(std::make_pair(rule_ptr.get(), rule_ptr)).first;
      }

      return riter->second;
    }

    struct __tree_fallback : public boost::static_visitor<bool>
    {
      bool operator()(const attribute_set_type::int_type& x) const { return x; }
      template <typename Tp>
      bool operator()(const Tp& x) const { return false; }
    };
    
    bool is_tree_fallback(const attribute_set_type& attrs) const
    {
      attribute_set_type::const_iterator aiter = attrs.find(attr_tree_fallback);
      
      return aiter != attrs.end() && boost::apply_visitor(__tree_fallback(), aiter->second);
    }

    node_map_set_type node_map;
    node_map_set_type node_map_phrase;
    node_map_set_type node_map_fallback;

    connected_type connected;
    
    phrase_unique_type phrases;
    phrase_map_type phrase_map;
    
    internal_tail_set_type   tail_map;
    internal_symbol_set_type symbol_map;
    internal_symbol_set_type symbol_map_terminal;
    internal_label_map_type  label_map;
    
    terminal_label_map_type     terminal_map_local;
    terminal_label_map_set_type terminal_map_global;

    rule_ptr_type goal_rule;
    
    symbol_type goal;
    const tree_grammar_type& tree_grammar;
    const grammar_type& grammar;
    const bool yield_source;
    const bool frontier_attribute;
    
    const attribute_type attr_internal_node;
    const attribute_type attr_source_root;
    const attribute_type attr_tree_fallback;
    const attribute_type attr_glue_tree;
    const attribute_type attr_glue_tree_fallback;
    const attribute_type attr_frontier_source;
    const attribute_type attr_frontier_target;

    rule_cache_type rule_cache;

    frontier_set_type frontiers_source;
    frontier_set_type frontiers_target;

    tree_frontier_set_type tree_frontiers_source;
    tree_frontier_set_type tree_frontiers_target;
    
    phrase_cache_set_type caches_phrase;
  };
  
  
  inline
  void compose_tree(const Symbol& goal, const TreeGrammar& tree_grammar, const Grammar& grammar, const HyperGraph& graph_in, HyperGraph& graph_out, const bool yield_source=false, const bool frontier=false)
  {
    ComposeTree __composer(goal, tree_grammar, grammar, yield_source, frontier);
    __composer(graph_in, graph_out);
  }
};

#endif

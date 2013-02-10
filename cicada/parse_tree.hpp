// -*- mode: c++ -*-
//
//  Copyright(C) 2011-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__PARSE_TREE__HPP__
#define __CICADA__PARSE_TREE__HPP__ 1

#include <vector>
#include <deque>
#include <algorithm>
#include <queue>
#include <sstream>

#include <cicada/symbol.hpp>
#include <cicada/sentence.hpp>
#include <cicada/vocab.hpp>
#include <cicada/tree_grammar.hpp>
#include <cicada/tree_transducer.hpp>
#include <cicada/grammar.hpp>
#include <cicada/transducer.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/semiring.hpp>

#include <utils/chunk_vector.hpp>
#include <utils/chart.hpp>
#include <utils/hashmurmur3.hpp>
#include <utils/unordered_set.hpp>
#include <utils/unordered_map.hpp>
#include <utils/b_heap.hpp>
#include <utils/std_heap.hpp>
#include <utils/bithack.hpp>
#include <utils/simple_vector.hpp>
#include <utils/compact_map.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/functional/hash/hash.hpp>

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

//
// beam parse variant of tree-composition
//

namespace cicada
{
  
  template <typename Semiring, typename Function>
  struct ParseTree
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

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
    
    typedef typename utils::unordered_set<phrase_type, boost::hash<phrase_type>,  std::equal_to<phrase_type>, std::allocator<phrase_type> >::type phrase_set_type;
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
    typedef std::vector<int, std::allocator<int> > internal_level_map_type;

    typedef boost::fusion::tuple<typename internal_tail_set_type::index_type, typename internal_symbol_set_type::index_type, symbol_type> internal_label_type;
    typedef boost::fusion::tuple<int, typename internal_symbol_set_type::index_type, symbol_type> terminal_label_type;

    template <typename Tp>
    struct unassigned_key
    {
      Tp operator()() const { return Tp(-1, -1, symbol_type::id_type(-1)); }
    };

    typedef utils::compact_map<internal_label_type, hypergraph_type::id_type,
			       unassigned_key<internal_label_type>, unassigned_key<internal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<internal_label_type>,
			       std::allocator<std::pair<const internal_label_type, hypergraph_type::id_type> > > internal_label_map_type;
    typedef utils::compact_map<terminal_label_type, hypergraph_type::id_type,
			       unassigned_key<terminal_label_type>, unassigned_key<terminal_label_type>,
			       utils::hashmurmur3<size_t>, std::equal_to<terminal_label_type>,
			       std::allocator<std::pair<const terminal_label_type, hypergraph_type::id_type> > > terminal_label_map_type;

    struct rule_hash_type
    {
      size_t operator()(const rule_ptr_type& x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }
    };

    struct tree_rule_hash_type
    {
      size_t operator()(const tree_rule_ptr_type& x) const
      {
	return (x ? hash_value(*x) : size_t(0));
      }
    };
    
    struct rule_equal_type
    {
      bool operator()(const rule_ptr_type& x, const rule_ptr_type& y) const
      {
	return x == y ||(x && y && *x == *y);
      }
    };

    struct tree_rule_equal_type
    {
      bool operator()(const tree_rule_ptr_type& x, const tree_rule_ptr_type& y) const
      {
	return x == y ||(x && y && *x == *y);
      }
    };

    typedef typename utils::unordered_map<rule_ptr_type, std::string, rule_hash_type, rule_equal_type,
					  std::allocator<std::pair<const rule_ptr_type, std::string> > >::type frontier_set_type;
    
    typedef typename utils::unordered_map<tree_rule_ptr_type, std::string, tree_rule_hash_type, tree_rule_equal_type,
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

    typedef Semiring semiring_type;
    typedef Semiring score_type;
    
    typedef Function function_type;
    
    struct TreeCandidate
    {
      score_type    score;
      
      tree_rule_ptr_type rule;
      
      feature_set_type   features;
      attribute_set_type attributes;
      
      TreeCandidate() : score(), rule(), features(), attributes() {}
      TreeCandidate(const score_type& __score, const tree_rule_ptr_type& __rule, const feature_set_type& __features, const attribute_set_type& __attributes)
	: score(__score), rule(__rule), features(__features), attributes(__attributes) {}

      void swap(TreeCandidate& x)
      {
	std::swap(score, x.score);
	rule.swap(x.rule);
	features.swap(x.features);
	attributes.swap(x.attributes);
      }
      
      friend
      void swap(TreeCandidate& x, TreeCandidate& y)
      {
	x.swap(y);
      }
    };
    
    struct RuleCandidate
    {
      score_type    score;
      
      rule_ptr_type rule;
      
      feature_set_type   features;
      attribute_set_type attributes;
      
      RuleCandidate() : score(), rule(), features(), attributes() {}
      RuleCandidate(const score_type& __score, const rule_ptr_type& __rule, const feature_set_type& __features, const attribute_set_type& __attributes)
	: score(__score), rule(__rule), features(__features), attributes(__attributes) {}

      void swap(RuleCandidate& x)
      {
	std::swap(score, x.score);
	rule.swap(x.rule);
	features.swap(x.features);
	attributes.swap(x.attributes);
      }
      
      friend
      void swap(RuleCandidate& x, RuleCandidate& y)
      {
	x.swap(y);
      }
    };
    
    typedef TreeCandidate tree_candidate_type;
    typedef RuleCandidate rule_candidate_type;
    
    typedef utils::simple_vector<tree_candidate_type, std::allocator<tree_candidate_type> > tree_candidate_set_type;
    typedef utils::simple_vector<rule_candidate_type, std::allocator<rule_candidate_type> > rule_candidate_set_type;
    typedef std::vector<const rule_candidate_type*, std::allocator<const rule_candidate_type*> > rule_candidate_ptr_set_type;
    
    typedef typename utils::unordered_map<tree_transducer_type::id_type, tree_candidate_set_type, utils::hashmurmur3<size_t>, std::equal_to<tree_transducer_type::id_type>,
					  std::allocator<std::pair<const tree_transducer_type::id_type, tree_candidate_set_type> > >::type tree_candidate_map_type;
    typedef typename utils::unordered_map<transducer_type::id_type, rule_candidate_set_type, utils::hashmurmur3<size_t>, std::equal_to<transducer_type::id_type>,
					  std::allocator<std::pair<const transducer_type::id_type, rule_candidate_set_type> > >::type rule_candidate_map_type;
  
    typedef std::vector<tree_candidate_map_type, std::allocator<tree_candidate_map_type> > tree_candidate_table_type;
    typedef std::vector<rule_candidate_map_type, std::allocator<rule_candidate_map_type> > rule_candidate_table_type;

    struct Candidate
    {      
      score_type    score;

      typename tree_candidate_set_type::const_iterator tree_first;
      typename tree_candidate_set_type::const_iterator tree_last;
      
      typename rule_candidate_set_type::const_iterator rule_first;
      typename rule_candidate_set_type::const_iterator rule_last;
      
      frontier_type frontier;
      
      feature_set_type   features;
      attribute_set_type attributes;

      bool is_rule() const { return rule_first != rule_last; }
      bool is_tree() const { return rule_first == rule_last; }
      
      score_type candidate_score() const
      {
	return (rule_first == rule_last ? tree_first->score : rule_first->score) * score;
      }
      
      Candidate()
	: score(), tree_first(), tree_last(), rule_first(), rule_last(), frontier(), features(), attributes() { rule_first = rule_last; }
      Candidate(const score_type& __score,
		typename tree_candidate_set_type::const_iterator __tree_first,
		typename tree_candidate_set_type::const_iterator __tree_last,
		const frontier_type& __frontier,
		const feature_set_type& __features,
		const attribute_set_type& __attributes)
	: score(__score),
	  tree_first(__tree_first), tree_last(__tree_last), rule_first(), rule_last(),
	  frontier(__frontier),
	  features(__features),
	  attributes(__attributes) { rule_first = rule_last; }
      
      Candidate(typename rule_candidate_set_type::const_iterator __rule_first,
		typename rule_candidate_set_type::const_iterator __rule_last)
	: score(semiring::traits<score_type>::one()),
	  tree_first(), tree_last(), rule_first(__rule_first), rule_last(__rule_last),
	  frontier(),
	  features(),
	  attributes() { tree_first = tree_last; }
    };
    
    typedef Candidate candidate_type;
    typedef utils::chunk_vector<candidate_type, 1024 * 8 / sizeof(candidate_type), std::allocator<candidate_type> > candidate_set_type;
    
    struct compare_heap_type
    {
      // we use less, so that when popped from heap, we will grab "greater" in back...
      bool operator()(const candidate_type* x, const candidate_type* y) const
      {
	return x->candidate_score() < y->candidate_score();
      }
    };
    
    typedef std::vector<const candidate_type*, std::allocator<const candidate_type*> > candidate_heap_base_type;
    typedef utils::std_heap<const candidate_type*,  candidate_heap_base_type, compare_heap_type> candidate_heap_type;

    typedef std::vector<score_type,  std::allocator<score_type> >  score_set_type;
    
    
    ParseTree(const symbol_type& __goal,
	      const tree_grammar_type& __tree_grammar,
	      const grammar_type& __grammar,
	      const function_type& __function,
	      const int __beam_size,
	      const bool __yield_source,
	      const bool __frontier)
      : goal(__goal),
	tree_grammar(__tree_grammar), 
	grammar(__grammar),
	function(__function),
	beam_size(__beam_size),
	yield_source(__yield_source),
	frontier_attribute(__frontier),
	attr_internal_node("internal-node"),
	attr_source_root("source-root"),
	attr_glue_tree(__grammar.empty() ? "" : "glue-tree"),
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
      
      phrase_map.clear();
      phrase_map.reserve(graph_in.nodes.size());
      phrase_map.resize(graph_in.nodes.size());

      tail_map.clear();
      symbol_map.clear();
      symbol_map_terminal.clear();
      label_map.clear();
      
      rule_tables.clear();
      rule_tables.reserve(grammar.size());
      rule_tables.resize(grammar.size());
      
      tree_tables.clear();
      tree_tables.reserve(tree_grammar.size());
      tree_tables.resize(tree_grammar.size());
      
      scores.clear();
      scores.reserve(graph_in.nodes.size());
      scores.resize(graph_in.nodes.size(), semiring::traits<score_type>::min());

      frontiers_source.clear();
      frontiers_target.clear();
      tree_frontiers_source.clear();
      tree_frontiers_target.clear();
      
      // bottom-up topological order
      for (size_t id = 0; id != graph_in.nodes.size(); ++ id) {
	terminal_map.clear();
	candidates.clear();
	heap.clear();
	
	match_tree(id, graph_in);

	// we will do exact phrase-matching...
	if (! grammar.empty())
	  match_phrase(id, graph_in, graph_out);
	
	enumerate_tree(id, graph_in, graph_out);
      }
      
      if (! grammar.empty()) {
	// connect all the node_map_phrase with node_map
	
	for (size_t id = 0; id != graph_in.nodes.size(); ++ id)
	  if (! node_map_phrase[id].empty() && ! node_map[id].empty()) {
	    const symbol_type& root_label = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;
	    
	    typename node_map_type::const_iterator piter_end = node_map[id].end();
	    for (typename node_map_type::const_iterator piter = node_map[id].begin(); piter != piter_end; ++ piter) {
	      typename node_map_type::const_iterator citer_end = node_map_phrase[id].end();
	      for (typename node_map_type::const_iterator citer = node_map_phrase[id].begin(); citer != citer_end; ++ citer) {
		hypergraph_type::edge_type& edge = graph_out.add_edge(&citer->second, (&citer->second) + 1);
		
		edge.rule = rule_type::create(rule_type(piter->first, rule_type::symbol_set_type(1, citer->first)));
		
		edge.attributes[attr_source_root] = static_cast<const std::string&>(root_label);
		edge.attributes[attr_glue_tree] = attribute_set_type::int_type(1);
		
		graph_out.connect_edge(edge.id, piter->second);
	      }
	    }
	  }
      }
      
      // goal node... the goal must be mapped goal...
      typename node_map_type::const_iterator niter = node_map[graph_in.goal].find(goal.non_terminal());
      if (niter != node_map[graph_in.goal].end()) {
	// goal node...
	graph_out.goal = graph_out.add_node().id;
	
	// hyperedge to goal...
	hypergraph_type::edge_type& edge = graph_out.add_edge(&(niter->second), (&(niter->second)) + 1);
	edge.rule = goal_rule;
	
	// connect!
	graph_out.connect_edge(edge.id, graph_out.goal);
      }
      
      node_map.clear();
      node_map_phrase.clear();
      
      if (graph_out.is_valid())
	graph_out.topologically_sort();
    }

  private:

    void enumerate_tree(const int id, const hypergraph_type& graph_in, hypergraph_type& graph_out)
    {
      rules_enumerated.clear();

      for (int num_pop = 0; ! heap.empty() && num_pop != beam_size; ++ num_pop) {
	const candidate_type* item = heap.top();
	heap.pop();
	
	if (item->is_tree()) {
	  const tree_candidate_type& rule = *(item->tree_first);
	  
	  scores[id] = std::max(scores[id], item->score * rule.score);
	  
	  apply_rule(*rule.rule,
		     id,
		     item->frontier,
		     rule.features + item->features,
		     rule.attributes + item->attributes,
		     graph_in,
		     graph_out);
	  
	  // next queue!
	  ++ const_cast<candidate_type*>(item)->tree_first;
	  if (item->tree_first != item->tree_last)
	    heap.push(item);
	} else {
	  const rule_candidate_type& rule = *(item->rule_first);
	  
	  scores[id] = std::max(scores[id], item->score * rule.score);

	  rules_enumerated.push_back(&(*(item->rule_first)));
	  
	  ++ const_cast<candidate_type*>(item)->rule_first;
	  if (item->rule_first != item->rule_last)
	    heap.push(item);
	}
      }

      if (! rules_enumerated.empty()) {
	const symbol_type& root_label = graph_in.edges[graph_in.nodes[id].edges.front()].rule->lhs;
	
	typename rule_candidate_ptr_set_type::const_iterator piter_end = rules_enumerated.end();
	for (typename rule_candidate_ptr_set_type::const_iterator piter = rules_enumerated.begin(); piter != piter_end; ++ piter) {
	  const rule_candidate_type& cand = *(*piter);
	  
	  hypergraph_type::edge_type& edge = graph_out.add_edge();
	  
	  edge.rule = cand.rule;
	  edge.features = cand.features;
	  edge.attributes = cand.attributes;
	  edge.attributes[attr_source_root] = static_cast<const std::string&>(root_label);
	  
	  std::pair<typename node_map_type::iterator, bool> result = node_map_phrase[id].insert(std::make_pair(edge.rule->lhs, 0));
	  if (result.second)
	    result.first->second = graph_out.add_node().id;
	  
	  graph_out.connect_edge(edge.id, result.first->second);
	}
      }
    }
    
    void match_phrase(const int id, const hypergraph_type& graph_in, hypergraph_type& graph_out)
    {
      typedef std::deque<phrase_type, std::allocator<phrase_type> >  buffer_type;
      
      if (graph_in.nodes[id].edges.empty()) return;
      
      // first, construct prases

      buffer_type buffer;
      buffer_type buffer_next;
      
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
		buffer_next.back().insert(buffer_next.back().end(), piter->begin(), piter->end());
	      }
	    }
	    
	    buffer.swap(buffer_next);
	  } else if (*titer != vocab_type::EPSILON) {
	    buffer_type::iterator biter_end = buffer.end();
	    for (buffer_type::iterator biter = buffer.begin(); biter != biter_end; ++ biter)
	      biter->push_back(*titer);
	  }
	
	phrase_map[id].insert(buffer.begin(), buffer.end());
      }
      
      // then, try matching within this span...

      for (size_t grammar_id = 0; grammar_id != grammar.size(); ++ grammar_id) {
	const transducer_type& transducer = grammar[grammar_id];
	
	phrase_set_type::const_iterator piter_end = phrase_map[id].end();
	for (phrase_set_type::const_iterator piter = phrase_map[id].begin(); piter != piter_end; ++ piter) {
	  const phrase_type& phrase = *piter;
	  
	  transducer_type::id_type node = transducer.root();
	  
	  phrase_type::const_iterator iter_end = phrase.end();
	  for (phrase_type::const_iterator iter = phrase.begin(); iter != iter_end; ++ iter) {
	    node = transducer.next(node, *iter);
	    if (node == transducer.root()) break;
	  }
	  
	  if (node == transducer.root()) continue;
	  
	  const rule_candidate_set_type& rules = candidate_rules(grammar_id, node);
	  
	  if (rules.empty()) continue;
	  
	  candidates.push_back(candidate_type(rules.begin(), rules.end()));
	  heap.push(&candidates.back());
	}
      }
    }
    
    void match_tree(const int id, const hypergraph_type& graph_in)
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
	    
	    const tree_candidate_set_type& rules = candidate_trees(grammar_id, node_none);
	    
	    if (rules.empty()) continue;
	    
	    // compute frontier scores
	    score_type score = function(*siter);
	    frontier_type::const_iterator iter_end = fiter->end();
	    for (frontier_type::const_iterator iter = fiter->begin(); iter != iter_end; ++ iter)
	      score *= scores[*iter];
	    
	    candidates.push_back(candidate_type(score, rules.begin(), rules.end(), *fiter, *siter, *aiter));
	    heap.push(&candidates.back());
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

      const symbol_type& root_label = graph_in.edges[graph_in.nodes[root_in].edges.front()].rule->lhs;
      
      std::pair<typename node_map_type::iterator, bool> result = node_map[root_in].insert(std::make_pair(rule.label.non_terminal(), 0));
      if (result.second)
	result.first->second = graph_out.add_node().id;
      
      int non_terminal_pos = 0;
      level_map.clear();
      
      const hypergraph_type::id_type edge_id = construct_graph(rule, result.first->second, frontiers, graph_out, non_terminal_pos);
      
      graph_out.edges[edge_id].features   = features;
      graph_out.edges[edge_id].attributes = attributes;
      
      // root-label is assigned to source-root attribute
      graph_out.edges[edge_id].attributes[attr_source_root] = static_cast<const std::string&>(root_label);
    }
    
    hypergraph_type::id_type construct_graph(const tree_rule_type& rule,
					     hypergraph_type::id_type root,
					     const frontier_type& frontiers,
					     hypergraph_type& graph,
					     int& non_terminal_pos)
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
	    
	    std::pair<typename node_map_type::iterator, bool> result = node_map[node].insert(std::make_pair(aiter->label.non_terminal(), 0));
	    if (result.second)
	      result.first->second = graph.add_node().id;
	    
	    tails.push_back(result.first->second);
	  } else {
	    const hypergraph_type::id_type edge_id = construct_graph(*aiter, hypergraph_type::invalid, frontiers, graph, non_terminal_pos);
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
	  typename internal_tail_set_type::iterator   titer = tail_map.insert(tail_set_type(tails.begin(), tails.end())).first;
	  typename internal_symbol_set_type::iterator siter = symbol_map.insert(symbol_set_type(rhs.begin(), rhs.end())).first;
	    
	  std::pair<typename internal_label_map_type::iterator, bool> result = label_map.insert(std::make_pair(internal_label_type(titer - tail_map.begin(),
																   siter - symbol_map.begin(),
																   rule.label), 0));
	  
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = rule_type::create(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	  
	} else {
	  typename internal_symbol_set_type::iterator siter = symbol_map_terminal.insert(symbol_set_type(rhs.begin(), rhs.end())).first;
	  level_map.resize(symbol_map_terminal.size(), 0);
	  const size_t level_terminal = siter - symbol_map_terminal.begin();
	  
	  std::pair<typename terminal_label_map_type::iterator, bool> result = terminal_map.insert(std::make_pair(terminal_label_type(level_map[level_terminal],
																      level_terminal,
																      rule.label), 0));
	  
	  ++ level_map[level_terminal];
	  
	  if (result.second) {
	    edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	    root = graph.add_node().id;
	    
	    graph.edges[edge_id].rule = rule_type::create(rule_type(rule.label, rhs.begin(), rhs.end()));
	    graph.connect_edge(edge_id, root);
	    
	    result.first->second = edge_id;
	  } else {
	    edge_id = result.first->second;
	    root = graph.edges[edge_id].head;
	  }
	}
      } else {
	edge_id = graph.add_edge(tails.begin(), tails.end()).id;
	graph.edges[edge_id].rule = rule_type::create(rule_type(rule.label, rhs.begin(), rhs.end()));
	graph.connect_edge(edge_id, root);
      }
      
      return edge_id;
    }

    template <typename Tp>
    struct greater_score
    {
      bool operator()(const Tp& x, const Tp& y) const
      {
	return x.score > y.score;
      }
    };

    const rule_candidate_set_type& candidate_rules(const size_type& table, const transducer_type::id_type& node)
    {
      typename rule_candidate_map_type::iterator riter = rule_tables[table].find(node);
      if (riter == rule_tables[table].end()) {
	const transducer_type::rule_pair_set_type& rules = grammar[table].rules(node);

	riter = rule_tables[table].insert(std::make_pair(node, rule_candidate_set_type(rules.size()))).first;
	
	typename rule_candidate_set_type::iterator citer = riter->second.begin();
	transducer_type::rule_pair_set_type::const_iterator iter_end   = rules.end();
	for (transducer_type::rule_pair_set_type::const_iterator iter = rules.begin(); iter != iter_end; ++ iter, ++ citer) {
	  *citer = rule_candidate_type(function(iter->features),
				       yield_source ? iter->source : iter->target,
				       iter->features,
				       iter->attributes);
	  
	  if (frontier_attribute) {
	    const rule_ptr_type& rule_source = iter->source;
	    const rule_ptr_type& rule_target = iter->target;

	    if (rule_source) {
	      typename frontier_set_type::iterator siter = frontiers_source.find(rule_source);
	      if (siter == frontiers_source.end()) {
		std::ostringstream os;
		os << rule_source->rhs;
		siter = frontiers_source.insert(std::make_pair(rule_source, os.str())).first;
	      }
	      
	      citer->attributes[attr_frontier_source] = siter->second;
	    }
	    
	    if (rule_target) {
	      typename frontier_set_type::iterator titer = frontiers_target.find(rule_target);
	      if (titer == frontiers_target.end()) {
		std::ostringstream os;
		os << rule_target->rhs;
		titer = frontiers_target.insert(std::make_pair(rule_target, os.str())).first;
	      }
	      
	      citer->attributes[attr_frontier_target] = titer->second;
	    }
	  }
	}
	
	std::sort(riter->second.begin(), riter->second.end(), greater_score<rule_candidate_type>());
      }
      
      return riter->second;
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

    const tree_candidate_set_type& candidate_trees(const size_type& table, const tree_transducer_type::id_type& node)
    {
      typename tree_candidate_map_type::iterator riter = tree_tables[table].find(node);
      if (riter == tree_tables[table].end()) {
	const tree_transducer_type::rule_pair_set_type& rules = tree_grammar[table].rules(node);
	
	riter = tree_tables[table].insert(std::make_pair(node, tree_candidate_set_type(rules.size()))).first;
	
	
	typename tree_candidate_set_type::iterator citer = riter->second.begin();
	tree_transducer_type::rule_pair_set_type::const_iterator iter_begin = rules.begin();
	tree_transducer_type::rule_pair_set_type::const_iterator iter_end   = rules.end();
	for (tree_transducer_type::rule_pair_set_type::const_iterator iter = iter_begin; iter != iter_end; ++ iter, ++ citer) {
	  *citer = tree_candidate_type(function(iter->features),
				       yield_source ? iter->source : iter->target,
				       iter->features,
				       iter->attributes);

	  const attribute_set_type::int_type size_internal = iter->source->size_internal();
	  if (size_internal)
	    citer->attributes[attr_internal_node] = size_internal;
	  
	  if (frontier_attribute) {
	    const tree_rule_ptr_type& rule_source = iter->source;
	    const tree_rule_ptr_type& rule_target = iter->target;
	    
	    if (rule_source) {
	      typename tree_frontier_set_type::iterator siter = tree_frontiers_source.find(rule_source);
	      if (siter == tree_frontiers_source.end()) {
		std::string frontier;
		rule_source->frontier(FrontierIterator(frontier));
		    
		siter = tree_frontiers_source.insert(std::make_pair(rule_source, frontier)).first;
	      }
		  
	      citer->attributes[attr_frontier_source] = siter->second;
	    }

	    if (rule_target) {
	      typename tree_frontier_set_type::iterator titer = tree_frontiers_target.find(rule_target);
	      if (titer == tree_frontiers_target.end()) {
		std::string frontier;
		rule_target->frontier(FrontierIterator(frontier));
		    
		titer = tree_frontiers_target.insert(std::make_pair(rule_target, frontier)).first;
	      }
	      
	      citer->attributes[attr_frontier_target] = titer->second;
	    }
	  }
	}
	
	std::sort(riter->second.begin(), riter->second.end(), greater_score<tree_candidate_type>());
      }
      
      return riter->second;
    }
    
  private:

    node_map_set_type node_map;
    node_map_set_type node_map_phrase;
    
    phrase_map_type phrase_map;

    internal_level_map_type  level_map;
    internal_tail_set_type   tail_map;
    internal_symbol_set_type symbol_map;
    internal_symbol_set_type symbol_map_terminal;
    internal_label_map_type  label_map;
    terminal_label_map_type  terminal_map;
    
    candidate_set_type    candidates;
    candidate_heap_type   heap;

    rule_candidate_ptr_set_type rules_enumerated;

    rule_candidate_table_type rule_tables;
    tree_candidate_table_type tree_tables;
    
    score_set_type scores;

    rule_ptr_type goal_rule;
    
    symbol_type goal;
    const tree_grammar_type& tree_grammar;
    const grammar_type& grammar;
    const function_type& function;
    
    const int beam_size;
    const bool yield_source;
    const bool frontier_attribute;
    
    const attribute_type attr_internal_node;
    const attribute_type attr_source_root;
    const attribute_type attr_glue_tree;
    const attribute_type attr_frontier_source;
    const attribute_type attr_frontier_target;

    frontier_set_type frontiers_source;
    frontier_set_type frontiers_target;

    tree_frontier_set_type tree_frontiers_source;
    tree_frontier_set_type tree_frontiers_target;
  };
  
  template <typename Function>
  inline
  void parse_tree(const Symbol& goal, const TreeGrammar& tree_grammar, const Grammar& grammar, const Function& function, const HyperGraph& graph_in, HyperGraph& graph_out, const int size, const bool yield_source=false, const bool frontier=false)
  {
    ParseTree<typename Function::value_type, Function> __parser(goal, tree_grammar, grammar, function, size, yield_source, frontier);
    __parser(graph_in, graph_out);
  }
};

#endif

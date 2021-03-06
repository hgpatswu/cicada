// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__OPERATION__TRAVERSAL__HPP__
#define __CICADA__OPERATION__TRAVERSAL__HPP__ 1

#include <vector>

#include <cicada/alignment.hpp>
#include <cicada/dependency.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/rule.hpp>
#include <cicada/vocab.hpp>
#include <cicada/semiring.hpp>
#include <cicada/span_vector.hpp>

#include <utils/unordered_set.hpp>
#include <utils/bithack.hpp>

#include <boost/tuple/tuple.hpp>

namespace cicada
{
  namespace operation
  {
    struct edge_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      
      typedef hypergraph_type::feature_set_type feature_set_type;
      typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > edge_set_type;
      
      typedef edge_set_type value_type;
      
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	yield.clear();
	
	yield.push_back(edge.id);
	for (/**/; first != last; ++ first)
	  yield.insert(yield.end(), first->begin(), first->end());
      }
    };

    struct edge_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      
      typedef hypergraph_type::feature_set_type feature_set_type;
      typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > edge_set_type;

      typedef boost::tuple<edge_set_type, feature_set_type> value_type;
  
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();

	boost::get<0>(yield).push_back(edge.id);
	boost::get<1>(yield) = edge.features;
    
	// collect edge and features
	for (/**/; first != last; ++ first) {
	  boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*first).begin(), boost::get<0>(*first).end());
	  boost::get<1>(yield) += boost::get<1>(*first);
	}
      }
    };

    struct span_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::SpanVector span_set_type;
      typedef cicada::Vocab      vocab_type;
      typedef cicada::Symbol     symbol_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;
      
      typedef attribute_set_type::attribute_type attribute_type;
      
      typedef span_set_type value_type;
      
      span_traversal() 
	: attr_span_first("span-first"),
	  attr_span_last("span-last") {}
      
      attribute_type attr_span_first;
      attribute_type attr_span_last;
      
      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };
      
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	yield.clear();

	if (! edge.tails.empty()) {
	  const rule_type& rule = *edge.rule;
	  
	  if (rule.lhs.non_terminal_strip().find('^') == symbol_type::piece_type::npos()) {
	    attribute_set_type::const_iterator fiter = edge.attributes.find(attr_span_first);
	    attribute_set_type::const_iterator liter = edge.attributes.find(attr_span_last);
	    
	    if (fiter != edge.attributes.end() && liter != edge.attributes.end()) {
	      const int span_first = boost::apply_visitor(__point(), fiter->second);
	      const int span_last  = boost::apply_visitor(__point(), liter->second);
	      
	      if (span_first >= 0 && span_last >= 0)
		yield.push_back(span_set_type::span_type(span_first, span_last, rule.lhs));
	    }
	  }
	}
	
	for (/**/; first != last; ++ first)
	  yield.insert(yield.end(), first->begin(), first->end());
      }
    };

    struct span_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::SpanVector span_set_type;
      typedef cicada::Vocab      vocab_type;
      typedef cicada::Symbol     symbol_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;
      
      typedef attribute_set_type::attribute_type attribute_type;
      
      typedef boost::tuple<span_set_type, feature_set_type> value_type;
      
      span_feature_traversal() 
	: attr_span_first("span-first"),
	  attr_span_last("span-last") {}
      
      attribute_type attr_span_first;
      attribute_type attr_span_last;
      
      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };
      
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();
	boost::get<1>(yield) = edge.features;

	if (! edge.tails.empty()) {
	  const rule_type& rule = *edge.rule;
	  
	  if (rule.lhs.non_terminal_strip().find('^') == symbol_type::piece_type::npos()) {
	    attribute_set_type::const_iterator fiter = edge.attributes.find(attr_span_first);
	    attribute_set_type::const_iterator liter = edge.attributes.find(attr_span_last);
	    
	    if (fiter != edge.attributes.end() && liter != edge.attributes.end()) {
	      const int span_first = boost::apply_visitor(__point(), fiter->second);
	      const int span_last  = boost::apply_visitor(__point(), liter->second);
	      
	      if (span_first >= 0 && span_last >= 0)
		boost::get<0>(yield).push_back(span_set_type::span_type(span_first, span_last, rule.lhs));
	    }
	  }
	}
	
	for (/**/; first != last; ++ first) {
	  boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*first).begin(), boost::get<0>(*first).end());
	  boost::get<1>(yield) += boost::get<1>(*first);
	}
      }
    };

    struct alignment_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Alignment  alignment_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;

      typedef alignment_type value_type;

      alignment_traversal() :
	attr_source_position("source-position"),
	attr_target_position("target-position") {}

      attribute_type attr_source_position;
      attribute_type attr_target_position;

      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	yield.clear();
	
	attribute_set_type::const_iterator siter = edge.attributes.find(attr_source_position);
	attribute_set_type::const_iterator titer = edge.attributes.find(attr_target_position);

	if (siter != edge.attributes.end() && titer != edge.attributes.end()) {
	  const int source_pos = boost::apply_visitor(__point(), siter->second);
	  const int target_pos = boost::apply_visitor(__point(), titer->second);
	  
	  if (source_pos >= 0 && target_pos >= 0)
	    yield.push_back(std::make_pair(source_pos, target_pos));
	}
	
	// collect features...
	for (/**/; first != last; ++ first)
	  yield.insert(yield.end(), first->begin(), first->end());
      }
    };

    struct alignment_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Alignment  alignment_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;
  
      typedef boost::tuple<alignment_type, feature_set_type> value_type;

      alignment_feature_traversal() :
	attr_source_position("source-position"),
	attr_target_position("target-position") {}

      attribute_type attr_source_position;
      attribute_type attr_target_position;

      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();
	boost::get<1>(yield) = edge.features;
	
	attribute_set_type::const_iterator siter = edge.attributes.find(attr_source_position);
	attribute_set_type::const_iterator titer = edge.attributes.find(attr_target_position);

	if (siter != edge.attributes.end() && titer != edge.attributes.end()) {
	  const int source_pos = boost::apply_visitor(__point(), siter->second);
	  const int target_pos = boost::apply_visitor(__point(), titer->second);
	  
	  if (source_pos >= 0 && target_pos >= 0)
	    boost::get<0>(yield).push_back(std::make_pair(source_pos, target_pos));
	}
	
	// collect features...
	for (/**/; first != last; ++ first) {
	  boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*first).begin(), boost::get<0>(*first).end());
	  boost::get<1>(yield) += boost::get<1>(*first);
	}
      }
    };

    struct dependency_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Dependency dependency_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;

      typedef dependency_type value_type;

      dependency_traversal() :
	attr_dependency_head("dependency-head"),
	attr_dependency_dependent("dependency-dependent") {}

      attribute_type attr_dependency_head;
      attribute_type attr_dependency_dependent;

      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	yield.clear();
	
	attribute_set_type::const_iterator hiter = edge.attributes.find(attr_dependency_head);
	attribute_set_type::const_iterator diter = edge.attributes.find(attr_dependency_dependent);

	if (hiter != edge.attributes.end() && diter != edge.attributes.end()) {
	  const int pos_head      = boost::apply_visitor(__point(), hiter->second);
	  const int pos_dependent = boost::apply_visitor(__point(), diter->second);
	  
	  if (pos_head >= 0 && pos_dependent - 1 >= 0) {
	    if (pos_dependent - 1 >= static_cast<int>(yield.size()))
	      yield.resize(pos_dependent, -1);
	    
	    yield[pos_dependent - 1] = pos_head;
	  }
	}
	
	// collect features...
	for (/**/; first != last; ++ first) {
	  
	  yield.resize(utils::bithack::max(yield.size(), first->size()), -1);
	  
	  for (size_t i = 0; i != first->size(); ++ i)
	    if (first->operator[](i) >= 0) {
	      if (yield[i] >= 0)
		throw std::runtime_error("we have already assing value?");
	      
	      yield[i] = first->operator[](i);
	    }
	}
      }
    };

    struct dependency_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Dependency dependency_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;
  
      typedef boost::tuple<dependency_type, feature_set_type> value_type;

      dependency_feature_traversal() :
	attr_dependency_head("dependency-head"),
	attr_dependency_dependent("dependency-dependent") {}
      
      attribute_type attr_dependency_head;
      attribute_type attr_dependency_dependent;
      

      struct __point : public boost::static_visitor<int>
      {
	int operator()(const attribute_set_type::int_type& x) const { return x; }
	int operator()(const attribute_set_type::float_type& x) const { return -1; }
	int operator()(const attribute_set_type::string_type& x) const { return -1; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();
	boost::get<1>(yield) = edge.features;
	
	attribute_set_type::const_iterator hiter = edge.attributes.find(attr_dependency_head);
	attribute_set_type::const_iterator diter = edge.attributes.find(attr_dependency_dependent);

	if (hiter != edge.attributes.end() && diter != edge.attributes.end()) {
	  const int pos_head      = boost::apply_visitor(__point(), hiter->second);
	  const int pos_dependent = boost::apply_visitor(__point(), diter->second);
	  
	  if (pos_head >= 0 && pos_dependent - 1 >= 0) {
	    if (pos_dependent - 1 >= static_cast<int>(boost::get<0>(yield).size()))
	      boost::get<0>(yield).resize(pos_dependent, -1);
	    
	    boost::get<0>(yield).operator[](pos_dependent - 1) = pos_head;
	  }
	}
	
	// collect features...
	for (/**/; first != last; ++ first) {
	  const dependency_type& dep = boost::get<0>(*first);
	  
	  boost::get<0>(yield).resize(utils::bithack::max(boost::get<0>(yield).size(), dep.size()), -1);
	  
	  for (size_t i = 0; i != dep.size(); ++ i)
	    if (dep[i] >= 0) {
	      if (boost::get<0>(yield).operator[](i) >= 0)
		throw std::runtime_error("we have already assing value?");
	      
	      boost::get<0>(yield).operator[](i) = dep[i];
	    }
	  
	  
	  boost::get<1>(yield) += boost::get<1>(*first);
	}
      }
    };

    struct sentence_pos_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      
      typedef cicada::Sentence value_type;
      typedef cicada::Rule  rule_type;
      typedef cicada::Vocab vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;
      
      typedef attribute_set_type::attribute_type attribute_type;

      sentence_pos_traversal() : attr_insertion() {}
      sentence_pos_traversal(const std::string& __insertion_prefix) : insertion_prefix(__insertion_prefix), attr_insertion(__insertion_prefix.empty() ? "" : "insertion") {}

      struct __inserted : public boost::static_visitor<bool>
      {
	bool operator()(const attribute_set_type::int_type& x) const { return x; }
	bool operator()(const attribute_set_type::float_type& x) const { return false; }
	bool operator()(const attribute_set_type::string_type& x) const { return false; }
      };
      
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	// extract target-yield, features
	
	yield.clear();
	
	bool is_insertion = false;
	if (! insertion_prefix.empty()) {
	  attribute_set_type::const_iterator aiter = edge.attributes.find(attr_insertion);
	  if (aiter != edge.attributes.end())
	    is_insertion = boost::apply_visitor(__inserted(), aiter->second);
	}

	const rule_type& rule = *edge.rule;
	
	if (! is_insertion) {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = rule.rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      yield.insert(yield.end(), (first + pos)->begin(), (first + pos)->end());
	    } else if (*titer != vocab_type::EPSILON)
	      yield.push_back(static_cast<const std::string&>(*titer) + '|' + static_cast<const std::string&>(rule.lhs));
	} else {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = rule.rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      yield.insert(yield.end(), (first + pos)->begin(), (first + pos)->end());
	    } else if (*titer != vocab_type::EPSILON)
	      yield.push_back(insertion_prefix + static_cast<const std::string&>(*titer) + '|' + static_cast<const std::string&>(rule.lhs));
	}
      }
      
      std::string    insertion_prefix;
      attribute_type attr_insertion;
    };

    struct sentence_pos_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Sentence   sentence_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;
  
      typedef boost::tuple<sentence_type, feature_set_type> value_type;

      sentence_pos_feature_traversal() : attr_insertion() {}
      sentence_pos_feature_traversal(const std::string& __insertion_prefix) : insertion_prefix(__insertion_prefix), attr_insertion(__insertion_prefix.empty() ? "" : "insertion") {}
      
      struct __inserted : public boost::static_visitor<bool>
      {
	bool operator()(const attribute_set_type::int_type& x) const { return x; }
	bool operator()(const attribute_set_type::float_type& x) const { return false; }
	bool operator()(const attribute_set_type::string_type& x) const { return false; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();
	boost::get<1>(yield) = edge.features;
	
	bool is_insertion = false;
	if (! insertion_prefix.empty()) {
	  attribute_set_type::const_iterator aiter = edge.attributes.find(attr_insertion);
	  if (aiter != edge.attributes.end())
	    is_insertion = boost::apply_visitor(__inserted(), aiter->second);
	}

	const rule_type& rule = *edge.rule;

	if (! is_insertion) {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = rule.rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*(first + pos)).begin(), boost::get<0>(*(first + pos)).end());
	    } else if (*titer != vocab_type::EPSILON)
	      boost::get<0>(yield).push_back(static_cast<const std::string&>(*titer) + '|' + static_cast<const std::string&>(rule.lhs));
	} else {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = rule.rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = rule.rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*(first + pos)).begin(), boost::get<0>(*(first + pos)).end());
	    } else if (*titer != vocab_type::EPSILON)
	      boost::get<0>(yield).push_back(insertion_prefix + static_cast<const std::string&>(*titer) + '|' + static_cast<const std::string&>(rule.lhs));
	}
    
	// collect features...
	for (/**/; first != last; ++ first)
	  boost::get<1>(yield) += boost::get<1>(*first);
      }

      std::string    insertion_prefix;
      attribute_type attr_insertion;
    };
    
    struct sentence_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      
      typedef cicada::Sentence value_type;
      typedef cicada::Rule  rule_type;
      typedef cicada::Vocab vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;
      
      typedef attribute_set_type::attribute_type attribute_type;

      sentence_traversal() : attr_insertion() {}
      sentence_traversal(const std::string& __insertion_prefix) : insertion_prefix(__insertion_prefix), attr_insertion(__insertion_prefix.empty() ? "" : "insertion") {}

      struct __inserted : public boost::static_visitor<bool>
      {
	bool operator()(const attribute_set_type::int_type& x) const { return x; }
	bool operator()(const attribute_set_type::float_type& x) const { return false; }
	bool operator()(const attribute_set_type::string_type& x) const { return false; }
      };
      
      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	// extract target-yield, features
	
	yield.clear();
	
	bool is_insertion = false;
	if (! insertion_prefix.empty()) {
	  attribute_set_type::const_iterator aiter = edge.attributes.find(attr_insertion);
	  if (aiter != edge.attributes.end())
	    is_insertion = boost::apply_visitor(__inserted(), aiter->second);
	}
	
	if (! is_insertion) {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = edge.rule->rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = edge.rule->rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      yield.insert(yield.end(), (first + pos)->begin(), (first + pos)->end());
	    } else if (*titer != vocab_type::EPSILON)
	      yield.push_back(*titer);
	} else {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = edge.rule->rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = edge.rule->rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      yield.insert(yield.end(), (first + pos)->begin(), (first + pos)->end());
	    } else if (*titer != vocab_type::EPSILON)
	      yield.push_back(insertion_prefix + static_cast<const std::string&>(*titer));
	}
      }
      
      std::string    insertion_prefix;
      attribute_type attr_insertion;
    };


    struct sentence_feature_traversal
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Rule       rule_type;
      typedef cicada::Sentence   sentence_type;
      typedef cicada::Vocab      vocab_type;
      
      typedef hypergraph_type::feature_set_type   feature_set_type;
      typedef hypergraph_type::attribute_set_type attribute_set_type;

      typedef attribute_set_type::attribute_type attribute_type;
  
      typedef boost::tuple<sentence_type, feature_set_type> value_type;

      sentence_feature_traversal() : attr_insertion() {}
      sentence_feature_traversal(const std::string& __insertion_prefix) : insertion_prefix(__insertion_prefix), attr_insertion(__insertion_prefix.empty() ? "" : "insertion") {}
      
      struct __inserted : public boost::static_visitor<bool>
      {
	bool operator()(const attribute_set_type::int_type& x) const { return x; }
	bool operator()(const attribute_set_type::float_type& x) const { return false; }
	bool operator()(const attribute_set_type::string_type& x) const { return false; }
      };

      template <typename Edge, typename Iterator>
      void operator()(const Edge& edge, value_type& yield, Iterator first, Iterator last) const
      {
	boost::get<0>(yield).clear();
	boost::get<1>(yield) = edge.features;
	
	bool is_insertion = false;
	if (! insertion_prefix.empty()) {
	  attribute_set_type::const_iterator aiter = edge.attributes.find(attr_insertion);
	  if (aiter != edge.attributes.end())
	    is_insertion = boost::apply_visitor(__inserted(), aiter->second);
	}

	if (! is_insertion) {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = edge.rule->rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = edge.rule->rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*(first + pos)).begin(), boost::get<0>(*(first + pos)).end());
	    } else if (*titer != vocab_type::EPSILON)
	      boost::get<0>(yield).push_back(*titer);
	} else {
	  int non_terminal_pos = 0;
	  rule_type::symbol_set_type::const_iterator titer_end = edge.rule->rhs.end();
	  for (rule_type::symbol_set_type::const_iterator titer = edge.rule->rhs.begin(); titer != titer_end; ++ titer)
	    if (titer->is_non_terminal()) {
	      const int __non_terminal_index = titer->non_terminal_index();
	      const int pos = utils::bithack::branch(__non_terminal_index <= 0, non_terminal_pos, __non_terminal_index - 1);
	      ++ non_terminal_pos;
	      
	      boost::get<0>(yield).insert(boost::get<0>(yield).end(), boost::get<0>(*(first + pos)).begin(), boost::get<0>(*(first + pos)).end());
	    } else if (*titer != vocab_type::EPSILON)
	      boost::get<0>(yield).push_back(insertion_prefix + static_cast<const std::string&>(*titer));
	}
    
	// collect features...
	for (/**/; first != last; ++ first)
	  boost::get<1>(yield) += boost::get<1>(*first);
      }

      std::string    insertion_prefix;
      attribute_type attr_insertion;
    };

    struct kbest_span_filter
    {
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	return false;
      }
    };

    struct kbest_alignment_filter
    {
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	return false;
      }
    };
    

    struct kbest_dependency_filter
    {
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	return false;
      }
    };

    struct kbest_sentence_filter
    {
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	return false;
      }
    };

    struct kbest_span_filter_unique
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::SpanVector span_set_type;
      
      typedef utils::unordered_set<span_set_type, boost::hash<span_set_type>, std::equal_to<span_set_type>, std::allocator<span_set_type> >::type unique_type;
      typedef std::vector<unique_type, std::allocator<unique_type> > unique_set_type;

      kbest_span_filter_unique(const hypergraph_type& graph) : uniques(graph.nodes.size()) {}
  
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	unique_set_type& aligns = const_cast<unique_set_type&>(uniques);
	unique_type::iterator iter = aligns[node.id].find(boost::get<0>(yield));
	if (iter == aligns[node.id].end()) {
	  aligns[node.id].insert(boost::get<0>(yield));
	  return false;
	} else
	  return true;
      }

      unique_set_type uniques;
    };

    struct kbest_alignment_filter_unique
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Alignment  alignment_type;
      
      typedef utils::unordered_set<alignment_type, boost::hash<alignment_type>, std::equal_to<alignment_type>, std::allocator<alignment_type> >::type unique_type;
      typedef std::vector<unique_type, std::allocator<unique_type> > unique_set_type;
 

      kbest_alignment_filter_unique(const hypergraph_type& graph) : uniques(graph.nodes.size()) {}
  
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	unique_set_type& aligns = const_cast<unique_set_type&>(uniques);
	unique_type::iterator iter = aligns[node.id].find(boost::get<0>(yield));
	if (iter == aligns[node.id].end()) {
	  aligns[node.id].insert(boost::get<0>(yield));
	  return false;
	} else
	  return true;
      }

      unique_set_type uniques;
    };

    struct kbest_dependency_filter_unique
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Dependency dependency_type;
      
      typedef utils::unordered_set<dependency_type, boost::hash<dependency_type>, std::equal_to<dependency_type>, std::allocator<dependency_type> >::type unique_type;
      typedef std::vector<unique_type, std::allocator<unique_type> > unique_set_type;
 

      kbest_dependency_filter_unique(const hypergraph_type& graph) : uniques(graph.nodes.size()) {}
  
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	unique_set_type& aligns = const_cast<unique_set_type&>(uniques);
	unique_type::iterator iter = aligns[node.id].find(boost::get<0>(yield));
	if (iter == aligns[node.id].end()) {
	  aligns[node.id].insert(boost::get<0>(yield));
	  return false;
	} else
	  return true;
      }

      unique_set_type uniques;
    };

    struct kbest_sentence_filter_unique
    {
      typedef cicada::HyperGraph hypergraph_type;
      typedef cicada::Sentence sentence_type;
      
      typedef utils::unordered_set<sentence_type, boost::hash<sentence_type>, std::equal_to<sentence_type>, std::allocator<sentence_type> >::type unique_type;
      typedef std::vector<unique_type, std::allocator<unique_type> > unique_set_type;
 

      kbest_sentence_filter_unique(const hypergraph_type& graph) : uniques(graph.nodes.size()) {}
  
      template <typename Node, typename Yield>
      bool operator()(const Node& node, const Yield& yield) const
      {
	unique_set_type& sents = const_cast<unique_set_type&>(uniques);
	unique_type::iterator iter = sents[node.id].find(boost::get<0>(yield));
	if (iter == sents[node.id].end()) {
	  sents[node.id].insert(boost::get<0>(yield));
	  return false;
	} else
	  return true;
      }

      unique_set_type uniques;
    };

  };
};

#endif

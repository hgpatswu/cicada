#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/adapted.hpp>

#include <boost/tuple/tuple.hpp>


#include <stdexcept>
#include <algorithm>
#include <vector>
#include <iterator>

#include "graphviz.hpp"


BOOST_FUSION_ADAPT_STRUCT(
			  cicada::Rule,
			  (cicada::Rule::symbol_type,     lhs)
			  (cicada::Rule::symbol_set_type, target) // we swapped source/target for dot output!
			  (cicada::Rule::symbol_set_type, source)
			  )

namespace cicada
{
  
  template <typename Iterator>
  struct graphviz_rule_generator : boost::spirit::karma::grammar<Iterator, cicada::Rule()>
  {
    typedef cicada::Rule                 rule_type;
    typedef rule_type::symbol_type       symbol_type;
    typedef rule_type::symbol_set_type   symbol_set_type;
    
    graphviz_rule_generator() : graphviz_rule_generator::base_type(rule)
    {
      namespace karma = boost::spirit::karma;
      namespace standard = boost::spirit::standard;
      namespace phoenix = boost::phoenix;
      
      using karma::omit;
      using karma::repeat;
      using karma::lit;
      using karma::inf;
      using karma::buffer;
      using standard::char_;
      using karma::double_;
      using karma::int_;
      
      using namespace karma::labels;
      
      escape_char.add
	('\\', "\\\\")
	('\"', "\\\"")
	('{', "\\{")
	('}', "\\}")
	('<', "\\<")
	('>', "\\>")
	('|', "\\|")
	(' ',  "\\ ")
	('/', "\\/")
	('\n', "\\n")
	('\r', "\\r")
	('\t', "\\t");
      
      lhs %= *(escape_char | ~char_('\"'));
      phrase %= -(lhs % "\\ ");
      
      rule %= lhs << " | {" << phrase << " | " << phrase << " } ";
    }
    
    boost::spirit::karma::symbols<char, const char*> escape_char;
    
    boost::spirit::karma::rule<Iterator, symbol_type()>      lhs;
    boost::spirit::karma::rule<Iterator, symbol_set_type()>  phrase;
    boost::spirit::karma::rule<Iterator, rule_type()>        rule;
  };

  template <typename Iterator>
  struct graphviz_feature_generator : boost::spirit::karma::grammar<Iterator, cicada::Rule::feature_set_type()>
  {
    typedef cicada::Rule                 rule_type;
    typedef rule_type::symbol_type       symbol_type;
    typedef rule_type::symbol_set_type   symbol_set_type;

    typedef rule_type::feature_set_type  feature_set_type;
    typedef feature_set_type::value_type value_type;
    
    graphviz_feature_generator() : graphviz_feature_generator::base_type(features)
    {
      namespace karma = boost::spirit::karma;
      namespace standard = boost::spirit::standard;
      namespace phoenix = boost::phoenix;
      
      using karma::omit;
      using karma::repeat;
      using karma::lit;
      using karma::inf;
      using karma::buffer;
      using standard::char_;
      using karma::double_;
      using karma::int_;
      
      using namespace karma::labels;
      
      escape_char.add
	('\\', "\\\\")
	('\"', "\\\"")
	('{', "\\{")
	('}', "\\}")
	('<', "\\<")
	('>', "\\>")
	('|', "\\|")
	(' ',  "\\ ")
	('/', "\\/")
	('\n', "\\n")
	('\r', "\\r")
	('\t', "\\t");
      
      // left adjusted newlines
      features %= -(((+(escape_char | ~char_('\"')) << ":\\ " << double_) % "\\l") << "\\l");
    }
    
    boost::spirit::karma::symbols<char, const char*> escape_char;
    boost::spirit::karma::rule<Iterator, feature_set_type()> features;
    
  };


  
  std::ostream& graphviz(std::ostream& os, const HyperGraph& hypergraph)
  {
    typedef HyperGraph hypergraph_type;

    typedef hypergraph_type::node_type node_type;
    typedef hypergraph_type::edge_type edge_type;

    typedef std::back_insert_iterator<std::string> iterator_type;
    
    typedef graphviz_rule_generator<iterator_type>    rule_grammar_type;
    typedef graphviz_feature_generator<iterator_type> feature_grammar_type;

#ifdef HAVE_TLS
    static __thread rule_grammar_type* __rule_grammar_tls = 0;
    static boost::thread_specific_ptr<rule_grammar_type > __rule_grammar;
    
    if (! __rule_grammar_tls) {
      __rule_grammar.reset(new rule_grammar_type());
      __rule_grammar_tls = __rule_grammar.get();
    }
    
    rule_grammar_type& rule_grammar = *__rule_grammar_tls;
#else
    static boost::thread_specific_ptr<rule_grammar_type > __rule_grammar;
    if (! __rule_grammar.get())
      __rule_grammar.reset(new rule_grammar_type());
    
    rule_grammar_type& rule_grammar = *__rule_grammar;
#endif

#ifdef HAVE_TLS
    static __thread feature_grammar_type* __feature_grammar_tls = 0;
    static boost::thread_specific_ptr<feature_grammar_type > __feature_grammar;
    
    if (! __feature_grammar_tls) {
      __feature_grammar.reset(new feature_grammar_type());
      __feature_grammar_tls = __feature_grammar.get();
    }
    
    feature_grammar_type& feature_grammar = *__feature_grammar_tls;
#else
    static boost::thread_specific_ptr<feature_grammar_type > __feature_grammar;
    if (! __feature_grammar.get())
      __feature_grammar.reset(new feature_grammar_type());
    
    feature_grammar_type& feature_grammar = *__feature_grammar;
#endif

    std::string output_rule;
    std::string output_feature;


    os << "digraph { rankdir=BT;" << '\n';
    
    hypergraph_type::node_set_type::const_iterator niter_end = hypergraph.nodes.end();
    for (hypergraph_type::node_set_type::const_iterator niter = hypergraph.nodes.begin(); niter != niter_end; ++ niter) {
      const node_type& node = *niter;
      
      os << " node_" << node.id << " [label=\"\", shape=circle, height=0.1, width=0.1];" << '\n';
      
      node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
      for (node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
	const edge_type& edge = hypergraph.edges[*eiter];

	if (edge.rule) {
	  output_rule.clear();
	  iterator_type iter_rule(output_rule);
	  
	  boost::spirit::karma::generate(iter_rule, rule_grammar, *edge.rule);

	  output_feature.clear();
	  iterator_type iter_feature(output_feature);
	  
	  boost::spirit::karma::generate(iter_feature, feature_grammar, edge.features);
	  
	  os << "  edge_" << edge.id << " [label=\"" << output_rule << " | " << output_feature << "\", shape=record];" << '\n';
	} else
	  os << "  edge_" << edge.id << " [label=\"\", shape=rect];" << '\n';
	
	os << "    edge_" << edge.id << " -> node_" << node.id << ';' << '\n';

	edge_type::node_set_type::const_iterator niter_end = edge.tails.end();
	for (edge_type::node_set_type::const_iterator niter = edge.tails.begin(); niter != niter_end; ++ niter)
	  os << "    node_" << *niter << " -> edge_" << edge.id << ';' << '\n';
      }
    }
    
    os << '}' << '\n';
    
    return os;
  }


  std::ostream& graphviz(std::ostream& os, const Lattice& lattice)
  {
    
    
    return os;
  }
};

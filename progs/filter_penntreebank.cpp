#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <algorithm>
#include <iterator>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>

#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/spirit/include/support_istream_iterator.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/std_pair.hpp>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/tokenizer.hpp>
#include <boost/shared_ptr.hpp>

#include "cicada/hypergraph.hpp"

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/space_separator.hpp"


typedef boost::filesystem::path path_type;

// tree-bank parser...

struct treebank_type
{
  typedef std::vector<treebank_type> antecedents_type;

  std::string cat;
  antecedents_type antecedents;
  
  treebank_type() {}
  treebank_type(const std::string& __cat) : cat(__cat) {}

  void clear()
  {
    cat.clear();
    antecedents.clear();
  }
};


BOOST_FUSION_ADAPT_STRUCT(
			  treebank_type,
			  (std::string, cat)
			  (std::vector<treebank_type>, antecedents)
			  )


template <typename Iterator>
struct penntreebank_escaped_grammar : boost::spirit::qi::grammar<Iterator, treebank_type(), boost::spirit::standard::space_type>
{
  penntreebank_escaped_grammar() : penntreebank_escaped_grammar::base_type(treebank)
  {
    namespace qi = boost::spirit::qi;
    namespace standard = boost::spirit::standard;
    namespace phoenix = boost::phoenix;
    
    using qi::lit;
    using qi::lexeme;
    using qi::hold;
    using qi::repeat;
    using qi::attr;
    using qi::on_error;
    using qi::int_;
    using qi::double_;
    using standard::char_;
    using standard::space;

    escaped_char.add
      ("\\/",   '/')
      ("\\*",   '*');
    
    escaped_word.add
      ("-LRB-", "(")
      ("-RRB-", ")")
      ("-LSB-", "[")
      ("-RSB-", "]")
      ("-LCB-", "{")
      ("-RCB-", "}");
    
    cat %= lexeme[escaped_word | +(escaped_char | (char_ - space - '(' - ')'))];
    treebank %= hold['(' >> cat >> +treebank >> ')'] | cat;
  }
  
  boost::spirit::qi::symbols<char, char>        escaped_char;
  boost::spirit::qi::symbols<char, const char*> escaped_word;
  
  boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::standard::space_type>   cat;
  boost::spirit::qi::rule<Iterator, treebank_type(), boost::spirit::standard::space_type> treebank;
};

template <typename Iterator>
struct penntreebank_grammar : boost::spirit::qi::grammar<Iterator, treebank_type(), boost::spirit::standard::space_type>
{
  penntreebank_grammar() : penntreebank_grammar::base_type(root)
  {
    namespace qi = boost::spirit::qi;
    namespace standard = boost::spirit::standard;
    namespace phoenix = boost::phoenix;
    
    using qi::lit;
    using qi::lexeme;
    using qi::hold;
    using qi::repeat;
    using qi::attr;
    using qi::on_error;
    using qi::int_;
    using qi::double_;
    using standard::char_;
    using standard::space;
    
    cat %= lexeme[+(char_ - space - '(' - ')')];
    treebank %= hold['(' >> cat >> +treebank >> ')'] | cat;
    root %= hold['(' >> cat >> +treebank >> ')'] | cat;
  }
  
  boost::spirit::qi::rule<Iterator, std::string(), boost::spirit::standard::space_type>   cat;
  boost::spirit::qi::rule<Iterator, treebank_type(), boost::spirit::standard::space_type> root;
  boost::spirit::qi::rule<Iterator, treebank_type(), boost::spirit::standard::space_type> treebank;
};

typedef cicada::HyperGraph hypergraph_type;
typedef std::vector<std::string, std::allocator<std::string> > sentence_type;


void transform(const hypergraph_type::id_type node_id,
	       const treebank_type& treebank,
	       hypergraph_type& graph)
{
  typedef hypergraph_type::id_type   id_type;
  typedef hypergraph_type::rule_type rule_type;
  typedef std::vector<id_type, std::allocator<id_type> > node_set_type;
  
  if (treebank.antecedents.empty()) return;
  
  std::string rule = "[" + treebank.cat + "] |||";
  
  node_set_type nodes;
  for (treebank_type::antecedents_type::const_iterator aiter = treebank.antecedents.begin(); aiter != treebank.antecedents.end(); ++ aiter) {
    if (aiter->antecedents.empty())
      rule += " " + aiter->cat;
    else {
      rule += " [" + aiter->cat + "]";
      nodes.push_back(graph.add_node().id);
    }
  }
  rule += " |||";
  
  hypergraph_type::edge_type& edge = graph.add_edge(nodes.begin(), nodes.end());
  edge.rule.reset(new rule_type(rule));
  graph.connect_edge(edge.id, node_id);
  
  node_set_type::const_iterator niter = nodes.begin();
  for (treebank_type::antecedents_type::const_iterator aiter = treebank.antecedents.begin(); aiter != treebank.antecedents.end(); ++ aiter) {
    if (! aiter->antecedents.empty()) {
      transform(*niter, *aiter, graph);
      ++ niter;
    }
  }
}

void transform(const treebank_type& treebank, hypergraph_type& graph)
{
  graph.goal = graph.add_node().id;
  
  transform(graph.goal, treebank, graph);
}

void transform(const treebank_type& treebank, sentence_type& sent) 
{
  if (treebank.antecedents.empty())
    sent.push_back(treebank.cat);
  else
    for (treebank_type::antecedents_type::const_iterator aiter = treebank.antecedents.begin(); aiter != treebank.antecedents.end(); ++ aiter)
      transform(*aiter, sent);
}

void transform_map(treebank_type& treebank, sentence_type& sent)
{
  if (treebank.antecedents.empty()) {
    treebank.cat = sent.back();
    sent.pop_back();
  } else
    for (treebank_type::antecedents_type::iterator aiter = treebank.antecedents.begin(); aiter != treebank.antecedents.end(); ++ aiter)
      transform_map(*aiter, sent);
}

path_type input_file = "-";
path_type output_file = "-";
path_type map_file;

bool escaped = false;
bool leaf = false;
bool rule = false;

int debug = 0;

void options(int argc, char** argv);

int main(int argc, char** argv)
{
  try {
    options(argc, argv);

    typedef boost::spirit::istream_iterator iter_type;

    utils::compress_istream is(input_file, 1024 * 1024);
    is.unsetf(std::ios::skipws);
    
    utils::compress_ostream os(output_file);

    boost::shared_ptr<utils::compress_istream> ms;

    if (! map_file.empty()) {
      if (! boost::filesystem::exists(map_file))
	throw std::runtime_error("no map file: " + map_file.file_string());
      
      ms.reset(new utils::compress_istream(map_file, 1024 * 1024));
    }
    
    penntreebank_grammar<iter_type>         grammar;
    penntreebank_escaped_grammar<iter_type> grammar_escaped;

    treebank_type   parsed;
    hypergraph_type graph;
    sentence_type   sent;
    
    std::string line;
    iter_type iter(is);
    iter_type iter_end;
    
    int num = 0;
    while (iter != iter_end) {
      parsed.clear();

      if (debug)
	std::cerr << "parsing: " << num << std::endl;
      
      if (escaped) {
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, grammar_escaped, boost::spirit::standard::space, parsed))
	  throw std::runtime_error("parsing failed");
      } else {
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, grammar, boost::spirit::standard::space, parsed))
	  throw std::runtime_error("parsing failed");
      }


      ++ num;

      if (ms) {
	if (! std::getline(*ms, line))
	  throw std::runtime_error("# of lines do not match with map-file");

	boost::tokenizer<utils::space_separator> tokenizer(line);
	
	sent.assign(tokenizer.begin(), tokenizer.end());
	
	std::reverse(sent.begin(), sent.end());
	
	if (! sent.empty())
	  transform_map(parsed, sent);
      }

      if (leaf) {
	sent.clear();
	
	transform(parsed, sent);
	
	if (! sent.empty()) {
	  std::copy(sent.begin(), sent.end() - 1, std::ostream_iterator<std::string>(os, " "));
	  os << sent.back();
	  os << '\n';
	} else
	  os << '\n';
      } else {
	graph.clear();
	
	transform(parsed, graph);

	if (! graph.edges.empty())
	  graph.topologically_sort();
	else
	  graph.clear();

	if (rule) {
	  hypergraph_type::edge_set_type::const_iterator eiter_end = graph.edges.end();
	  for (hypergraph_type::edge_set_type::const_iterator eiter = graph.edges.begin(); eiter != eiter_end; ++ eiter)
	    if (eiter->rule)
	      os << *(eiter->rule) << '\n';
	} else
	  os << graph << '\n';
      }
    }
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}
  

void options(int argc, char** argv)
{
  namespace po = boost::program_options;
  
  po::options_description desc("options");
  desc.add_options()
    ("input",     po::value<path_type>(&input_file)->default_value(input_file),   "input file")
    ("output",    po::value<path_type>(&output_file)->default_value(output_file), "output")
    ("map",       po::value<path_type>(&map_file)->default_value(map_file), "map terminal symbols")
    
    ("escape",    po::bool_switch(&escaped), "escape English penntreebank")
    ("leaf",      po::bool_switch(&leaf),    "collect leaf nodes only")
    ("rule",      po::bool_switch(&rule),    "collect rules only")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
        
    ("help", "help message");
  
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), vm);
  po::notify(vm);
  
  if (vm.count("help")) {
    std::cout << argv[0] << " [options]" << '\n' << desc << '\n';
    exit(0);
  }
}

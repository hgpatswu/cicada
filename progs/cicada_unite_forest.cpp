
#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

#include "cicada_impl.hpp"
#include "cicada/graphviz.hpp"

#include "utils/program_options.hpp"

#include <boost/program_options.hpp>

typedef std::vector<feature_type, std::allocator<feature_type> > feature_list_type;

path_type input_file = "-";
path_type output_file = "-";
path_type confidence_feature_file;
path_type count_feature_file;

std::string confidence;
std::string count;
double count_weight = 1.0;

bool output_graphviz = false;

int debug = 0;

// input mode... use of one-line lattice input or sentence input?
void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    feature_list_type features_confidence;
    feature_list_type features_count;

    if (! confidence_feature_file.empty()) {
      if (confidence_feature_file != "-" && ! boost::filesystem::exists(confidence_feature_file))
	throw std::runtime_error("no confidence feature file? " + confidence_feature_file.file_string());
      
      utils::compress_istream is(confidence_feature_file);
      std::string feature;
      while (is >> feature)
	features_confidence.push_back(feature);
    }
    
    if (! count_feature_file.empty()) {
      if (count_feature_file != "-" && ! boost::filesystem::exists(count_feature_file))
	throw std::runtime_error("no count feature file? " + count_feature_file.file_string());
      
      utils::compress_istream is(count_feature_file);
      std::string feature;
      while (is >> feature)
	features_count.push_back(feature);
    }
    
    hypergraph_type merged;
    hypergraph_type hypergraph;
    
    cicada::Feature feature_confidence(confidence);
    cicada::Feature feature_count(count);
    
    utils::compress_istream is(input_file, 1024 * 1024);
    std::string line;
    
    int rank = 1;
    int id = 0;
    for (/**/; std::getline(is, line); ++ id, ++ rank) {
      std::string::const_iterator iter = line.begin();
      std::string::const_iterator end = line.end();
      
      if (! hypergraph.assign(iter, end))
	throw std::runtime_error("invalid hypergraph format");

      if (! hypergraph.is_valid()) continue;

      const double conf = 1.0 / (1.0 + rank);

      feature_set_type features;
      if (! features_confidence.empty()) {
	if (id >= features_confidence.size())
	  throw std::runtime_error("# of confidence features do not match");
	features[features_confidence[id]] = conf;
      }
      if (! features_count.empty()) {
	if (id >= features_count.size())
	  throw std::runtime_error("# of count features do not match");
	features[features_count[id]] = count_weight;
      }
      if (! feature_confidence.empty())
	features[feature_confidence] = conf;
      if (! feature_count.empty())
	features[feature_count] = count_weight;
      
      if (! features.empty()) {
	hypergraph_type::edge_set_type::iterator eiter_end = hypergraph.edges.end();
	for (hypergraph_type::edge_set_type::iterator eiter = hypergraph.edges.begin(); eiter != eiter_end; ++ eiter)
	  eiter->features += features;
      } 
      
      merged.unite(hypergraph);
    }
    
    utils::compress_ostream os(output_file, 1024 * 1024);
    
    if (output_graphviz)
      cicada::graphviz(os, merged);
    else
      os << merged << '\n';
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

  po::variables_map variables;
  
  po::options_description desc("options");
  desc.add_options()
    ("input",  po::value<path_type>(&input_file)->default_value("-"),   "input hypergraphs")
    ("output", po::value<path_type>(&output_file)->default_value("-"),  "output merged hypergraph")

    ("confidence-feature-file", po::value<path_type>(&confidence_feature_file), "confidence feature file")
    ("count-feature-file",      po::value<path_type>(&count_feature_file),      "count feature file")
    
    ("confidence",   po::value<std::string>(&confidence),    "add confidence weight feature name")
    ("count",        po::value<std::string>(&count),         "add count weight feature name")
    ("count-weight", po::value<double>(&count_weight),       "count weight")

    ("graphviz", po::bool_switch(&output_graphviz), "output in graphviz format")

    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");

  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}

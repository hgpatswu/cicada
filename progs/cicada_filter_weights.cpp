//
// weights management...
//
// read weights and
// --average to perform averaging
// --sum to perform summing
// --sort to perform sorting
// --sort-abs to performn sorting by absolute values
//
// --normalize-l1 to perform normalization via l1
// --normalize-l2 to perform normalization via l2
//

#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <algorithm>
#include <iterator>

#include "cicada/weight_vector.hpp"
#include "cicada/dot_product.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/shared_ptr.hpp>

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"

typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

typedef cicada::WeightVector<double> weight_set_type;

path_set_type input_files;
path_type     output_file = "-";

bool average_mode = false;
bool sum_mode = false;
bool sort_mode = false;
bool sort_abs_mode = false;
bool normalize_l1 = false;
bool normalize_l2 = false;

int debug = 0;

template <typename Tp>
struct greater_second
{
  bool operator()(const Tp& x, const Tp& y) const
  {
    return x.second > y.second;
  }
};

template <typename Tp>
struct greater_fabs_second
{
  bool operator()(const Tp& x, const Tp& y) const
  {
    return std::fabs(x.second) > std::fabs(y.second);
  }
};

void options(int argc, char** argv);

int main(int argc, char** argv)
{
  try {
    options(argc, argv);

    if (sum_mode && average_mode)
      throw std::runtime_error("You cannnot perform both sum and average");
    if (normalize_l1 && normalize_l2)
      throw std::runtime_error("You cannnot perform both normalize-l1 and normalize-l2");
    if (sort_mode && sort_abs_mode)
      throw std::runtime_error("You cannnot perform both sort and sort-abs");
    
    weight_set_type weights;
    size_t          num = 0;
    
    if (input_files.empty())
      input_files.push_back("-");
    
    weight_set_type weights_input;
    for (path_set_type::const_iterator fiter = input_files.begin(); fiter != input_files.end(); ++ fiter) {
      if (*fiter != "-" && ! boost::filesystem::exists(*fiter))
	throw std::runtime_error("no file: " + fiter->file_string());

      utils::compress_istream is(*fiter, 1024 * 1024);
      
      weights_input.clear();
      is >> weights_input;
      
      if (weights.empty())
	weights.swap(weights_input);
      else
	weights += weights_input;
      ++ num;
    }
    
    if (average_mode && num)
      weights *= (1.0 / num);
    
    if (normalize_l1) {
      
      double sum = 0.0;
      weight_set_type::iterator witer_end = weights.end();
      for (weight_set_type::iterator witer = weights.begin(); witer != witer_end; ++ witer)
	sum += std::fabs(*witer);
      
      if (sum != 0.0)
	weights *= 1.0 / std::sqrt(sum);
      
    } else if (normalize_l2) {
      const double sum = cicada::dot_product(weights);
      
      if (sum != 0.0)
	weights *= 1.0 / std::sqrt(sum);
    }
    
    if (sort_mode || sort_abs_mode) {
      typedef std::pair<weight_set_type::feature_type, double> value_type;
      typedef std::vector<value_type, std::allocator<value_type> > value_set_type;
     
      value_set_type values;
      
      weight_set_type::feature_type::id_type id = 0;
      weight_set_type::iterator witer_end = weights.end();
      for (weight_set_type::iterator witer = weights.begin(); witer != witer_end; ++ witer, ++ id)
	if (*witer != 0.0)
	  values.push_back(value_type(weight_set_type::feature_type(id), *witer));

      if (sort_mode)
	std::sort(values.begin(), values.end(), greater_second<value_type>());
      else
	std::sort(values.begin(), values.end(), greater_fabs_second<value_type>());
      
      utils::compress_ostream os(output_file, 1024 * 1024);

      value_set_type::const_iterator fiter_end = values.end();
      for (value_set_type::const_iterator fiter = values.begin(); fiter != fiter_end; ++ fiter)
	if (! fiter->first.empty())
	  os << fiter->first << ' ' << fiter->second << '\n';
      
    } else {
      utils::compress_ostream os(output_file, 1024 * 1024);
      os << weights;
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
    ("input",     po::value<path_set_type>(&input_files)->multitoken(),           "input files")
    ("output",    po::value<path_type>(&output_file)->default_value(output_file), "output")

    ("average",      po::bool_switch(&average_mode),  "average weights")
    ("sum",          po::bool_switch(&sum_mode),      "sum weights")
    ("sort",         po::bool_switch(&sort_mode),     "sort weights")
    ("sort-abs",     po::bool_switch(&sort_abs_mode), "sort weights wrt absolute value")
    ("normalize-l1", po::bool_switch(&normalize_l1),  "weight normalization by L1")
    ("normalize-l2", po::bool_switch(&normalize_l2),  "weight normalization by L2")
    
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

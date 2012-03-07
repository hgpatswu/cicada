
#include "utils/json_string_parser.hpp"
#include "utils/json_string_generator.hpp"

#include <iostream>
#include <iterator>

int main(int argc, char** argv)
{
  namespace qi = boost::spirit::qi;
  namespace karma = boost::spirit::karma;
  namespace standard = boost::spirit::standard;

  typedef std::ostream_iterator<char> oiterator_type;
    
  utils::json_string_parser<std::string::const_iterator> parser;
  utils::json_string_generator<oiterator_type> generator;
  utils::json_string_generator<oiterator_type, true> generator_no_space;
  
  std::string line;
  std::string parsed;
  while (std::getline(std::cin, line)) {
    std::string::const_iterator iter = line.begin();
    std::string::const_iterator end = line.end();
    
    parsed.clear();
    if (qi::phrase_parse(iter, end, parser, standard::space, parsed))
      std::cout << "parsed: " << parsed << std::endl;
    else
      std::cout << "failed: " << std::string(iter, end) << std::endl;

    karma::generate(oiterator_type(std::cout), generator, parsed);
    std::cout << std::endl;
    karma::generate(oiterator_type(std::cout), generator_no_space, parsed);
    std::cout << std::endl;
			     
  }
  
}
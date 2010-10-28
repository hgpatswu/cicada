
#include <iostream>

#include "tree_rule.hpp"

typedef cicada::TreeRule rule_type;


void pre_order(const rule_type& rule)
{
  std::cout << "label: " << rule.label << std::endl;
  for (rule_type::antecedent_set_type::const_iterator riter = rule.antecedents.begin(); riter != rule.antecedents.end(); ++ riter)
    pre_order(*riter);
}

void post_order(const rule_type& rule)
{
  for (rule_type::antecedent_set_type::const_iterator riter = rule.antecedents.begin(); riter != rule.antecedents.end(); ++ riter)
    post_order(*riter);
  std::cout << "label: " << rule.label << std::endl;
}

void process(const std::string& rule_str)
{
  const rule_type rule(rule_str);
  
  std::cout << "string: " << rule_str << std::endl;
  std::cout << "tree rule: " << rule << std::endl;
  
  std::cout << "pre-order" << std::endl;
  pre_order(rule);
  std::cout << "post-order" << std::endl;
  post_order(rule);
}

int main(int argc, char** argv)
{
  process("(a)");
  process("(a (b) (c) (d))");
  process("(a (b (e)) (c (f) (g)) (d))");
  process("(a (b (e)) (c (f) (g)) (\\())");
}

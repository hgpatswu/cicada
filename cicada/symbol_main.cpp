//
//  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#include <boost/lexical_cast.hpp>
#include <boost/type_traits.hpp>

#include "utils/random_seed.hpp"

#include "symbol.hpp"

#include <cicada/msgpack/symbol.hpp>

#include "msgpack_main_impl.hpp"

void process(const cicada::Symbol& x, int index)
{
  std::cout << x << ' ' << index << std::endl
	    << "is a non-terminal? " << x.is_non_terminal() << std::endl
	    << "non-terminal? " << x.non_terminal() << std::endl
	    << "non-terminal index? " << x.non_terminal_index() << std::endl
	    << "non-terminal index? " << x.non_terminal(index) << std::endl
	    << "non-terminal strip? " << x.non_terminal_strip() << std::endl
	    << "annotate 1 true?" << x.annotate(1, true) << std::endl
	    << "annotate 1 false?" << x.annotate(1, false) << std::endl
	    << "annotate 2 true?" << x.annotate(2, true) << std::endl
	    << "annotate 2 false?" << x.annotate(2, false) << std::endl
	    << "coarse 1? " << x.coarse(1) << std::endl
	    << "coarse 2? " << x.coarse(2) << std::endl
	    << "coarse? " << x.coarse() << std::endl
	    << "pos " << x.pos() << std::endl
	    << "terminal " << x.terminal() << std::endl;

  msgpack_test(x);
}

int main(int argc, char** argv)
{
  typedef cicada::Symbol symbol_type;

  std::cerr << "trivial assign: " << boost::has_trivial_assign<symbol_type>::value << std::endl
	    << "trivial construct: " << boost::has_trivial_constructor<symbol_type>::value << std::endl
	    << "trivial copy: " << boost::has_trivial_copy<symbol_type>::value << std::endl
	    << "trivial copy-construct: " << boost::has_trivial_copy_constructor<symbol_type>::value << std::endl
	    << "trivial default-construct: " << boost::has_trivial_default_constructor<symbol_type>::value << std::endl
	    << "trivial destructor: " << boost::has_trivial_destructor<symbol_type>::value << std::endl;
  
  process("good", 1);
  process("[,]", 2);
  process("[x]", 1);
  process("[x]", 2);
  process("[x,3]", 2);
  process("[x,3]", 3);
  process("[x,4]", 3);
  process("[x,100]", 5000);
  process("[x,5g]", 4);
  process("[BAD,4]", 3);
  process("[GOOD,5]", 3);

  process("[1]", 4);
  process("[1,3]", 1);
  
  process("[BAD@1]", 3);
  process("[BAD@1,5]", 3);
  process("[BAD@2]", 3);
  process("[BAD@3]", 3);
  process("[BAD@3,2]", 3);
  process("[BAD@1,4]", 3);
  
  std::cerr << "pos: " << symbol_type("Good/[ADJ]").pos() << " terminal: " << symbol_type("Good/[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good|[ADJ]").pos() << " terminal: " << symbol_type("Good|[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good\\[ADJ]").pos() << " terminal: " << symbol_type("Good\\[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good|/[ADJ]").pos() << " terminal: " << symbol_type("Good|/[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good[g]|[ADJ]").pos() << " terminal: " << symbol_type("Good[g]|[ADJ]").terminal() << std::endl;

  std::cerr << "pos: " << symbol_type("Good/[ADJ]").pos() << " terminal: " << symbol_type("Good/[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good|[ADJ]").pos() << " terminal: " << symbol_type("Good|[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good\\[ADJ]").pos() << " terminal: " << symbol_type("Good\\[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good|/[ADJ]").pos() << " terminal: " << symbol_type("Good|/[ADJ]").terminal() << std::endl;
  std::cerr << "pos: " << symbol_type("Good[g]|[ADJ]").pos() << " terminal: " << symbol_type("Good[g]|[ADJ]").terminal() << std::endl;

  srandom(utils::random_seed());

  for (int i = 0; i != 1024 * 4; ++ i) {
    const std::string rnd = boost::lexical_cast<std::string>(random());
    
    symbol_type symbol(rnd);
  }
}

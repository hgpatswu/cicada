// -*- mode: c++ -*-

#ifndef __CICADA__GRAPHVIZ__HPP__
#define __CICADA__GRAPHVIZ__HPP__ 1

#include <iostream>

#include <cicada/hypergraph.hpp>
#include <cicada/lattice.hpp>

namespace cicada
{
  std::ostream& graphviz(std::ostream& os, const HyperGraph& hypergraph);
  std::ostream& graphviz(std::ostream& os, const Lattice& lattice);
};

#endif

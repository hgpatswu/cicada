//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <iostream>

#include "cicada/operation/functional.hpp"
#include "cicada/operation/prune.hpp"

#include <cicada/parameter.hpp>
#include <cicada/prune.hpp>

#include <utils/lexical_cast.hpp>
#include <utils/resource.hpp>

namespace cicada
{
  namespace operation
  {
    Prune::Prune(const std::string& parameter, const int __debug)
      : weights(0), kbest(0), beam(0.0), density(0.0), scale(1.0), weights_one(false), 
	semiring_tropical(false), semiring_logprob(false), semiring_log(false),
	debug(__debug)
    {
      typedef cicada::Parameter param_type;
    
      param_type param(parameter);
      if (param.name() != "prune")
	throw std::runtime_error("this is not a pruner");

      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (strcasecmp(piter->first.c_str(), "beam") == 0)
	  beam = boost::lexical_cast<double>(piter->second);
	else if (strcasecmp(piter->first.c_str(), "kbest") == 0)
	  kbest = boost::lexical_cast<size_t>(piter->second);
	else if (strcasecmp(piter->first.c_str(), "density") == 0)
	  density = boost::lexical_cast<double>(piter->second);
	else if (strcasecmp(piter->first.c_str(), "scale") == 0)
	  scale = boost::lexical_cast<double>(piter->second);
	else if (strcasecmp(piter->first.c_str(), "weights") == 0)
	  weights = &base_type::weights(piter->second);
	else if (strcasecmp(piter->first.c_str(), "weights-one") == 0)
	  weights_one = utils::lexical_cast<bool>(piter->second);
	else if (strcasecmp(piter->first.c_str(), "semiring") == 0) {
	  const std::string& name = piter->second;
	
	  if (strcasecmp(name.c_str(), "tropical") == 0)
	    semiring_tropical = true;
	  else if (strcasecmp(name.c_str(), "logprob") == 0)
	    semiring_logprob = true;
	  else if (strcasecmp(name.c_str(), "log") == 0)
	    semiring_log = true;
	  else
	    throw std::runtime_error("unknown semiring: " + name);
	
	} else
	  std::cerr << "WARNING: unsupported parameter for prune: " << piter->first << "=" << piter->second << std::endl;
      }

      const bool beam_mode = beam > 0.0;
      const bool density_mode = density >= 1.0;
      const bool kbest_mode = kbest > 0;
    
      if (int(beam_mode) + density_mode + kbest_mode > 1)
	throw std::runtime_error("you cannot specify both kbest, beam and density pruning");
      
      if (int(beam_mode) + density_mode + kbest_mode == 0)
	throw std::runtime_error("you may want to specify either kbest, beam or density pruning");

      if (int(semiring_tropical) + semiring_logprob + semiring_log == 0)
	semiring_tropical = true;
    
      if (weights && weights_one)
	throw std::runtime_error("you have weights, but specified all-one parameter");
    }
    
    void Prune::operator()(data_type& data) const
      {
	hypergraph_type& hypergraph = data.hypergraph;
	hypergraph_type pruned;

	weight_set_type __weights;
	if (weights_one) {
	  __weights.allocate();
	  for (weight_set_type::feature_type::id_type id = 0; id != __weights.size(); ++ id)
	    if (! weight_set_type::feature_type(id).empty())
	      __weights[weight_set_type::feature_type(id)] = 1.0;
	}
    
	const weight_set_type* weights_prune = (weights ? weights : &__weights);

	if (debug)
	  std::cerr << "pruning:"
		    << " # of nodes: " << hypergraph.nodes.size()
		    << " # of edges: " << hypergraph.edges.size()
		    << " valid? " << utils::lexical_cast<std::string>(hypergraph.is_valid())
		    << std::endl;
    
	utils::resource prune_start;

	if (kbest > 0) {
	  if (semiring_tropical)
	    cicada::prune_kbest(hypergraph, pruned, weight_scaled_function<cicada::semiring::Tropical<double> >(*weights_prune, scale), kbest);
	  else if (semiring_logprob)
	    cicada::prune_kbest(hypergraph, pruned, weight_scaled_function<cicada::semiring::Logprob<double> >(*weights_prune, scale), kbest);
	  else
	    cicada::prune_kbest(hypergraph, pruned, weight_scaled_function<cicada::semiring::Log<double> >(*weights_prune, scale), kbest);
	} else if (beam > 0.0) {
	  if (semiring_tropical)
	    cicada::prune_beam(hypergraph, pruned, weight_scaled_function<cicada::semiring::Tropical<double> >(*weights_prune, scale), beam);
	  else if (semiring_logprob)
	    cicada::prune_beam(hypergraph, pruned, weight_scaled_function<cicada::semiring::Logprob<double> >(*weights_prune, scale), beam);
	  else
	    cicada::prune_beam(hypergraph, pruned, weight_scaled_function<cicada::semiring::Log<double> >(*weights_prune, scale), beam);
	} else if (density >= 1.0) {
	  if (semiring_tropical)
	    cicada::prune_density(hypergraph, pruned, weight_scaled_function<cicada::semiring::Tropical<double> >(*weights_prune, scale), density);
	  else if (semiring_logprob)
	    cicada::prune_density(hypergraph, pruned, weight_scaled_function<cicada::semiring::Logprob<double> >(*weights_prune, scale), density);
	  else
	    cicada::prune_density(hypergraph, pruned, weight_scaled_function<cicada::semiring::Log<double> >(*weights_prune, scale), density);
	} else
	  throw std::runtime_error("what pruning?");
    
	
	utils::resource prune_end;
    
	if (debug)
	  std::cerr << "prune cpu time: " << (prune_end.cpu_time() - prune_start.cpu_time())
		    << " user time: " << (prune_end.user_time() - prune_start.user_time())
		    << std::endl;
    
	if (debug)
	  std::cerr << "pruned:"
		    << " # of nodes: " << pruned.nodes.size()
		    << " # of edges: " << pruned.edges.size()
		    << " valid? " << utils::lexical_cast<std::string>(pruned.is_valid())
		    << std::endl;
    
	hypergraph.swap(pruned);

      }
    
    void Prune::assign(const weight_set_type& __weights)
    {
      weights = &__weights;
    }
  };
};

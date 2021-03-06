//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>

#include <iostream>

#include <cicada/parameter.hpp>
#include <cicada/apply.hpp>
#include <cicada/semiring.hpp>

#include <cicada/operation/apply.hpp>
#include <cicada/operation/functional.hpp>

#include <utils/lexical_cast.hpp>
#include <utils/resource.hpp>
#include <utils/piece.hpp>

namespace cicada
{
  namespace operation
  {
    Apply::Apply(const std::string& parameter,
		 const model_type& __model,
		 const int __debug)
      : model(__model), weights(0), weights_assigned(0), size(200), diversity(0.0),
	weights_one(false), weights_fixed(false), weights_extra(),
	rejection(false), exact(false), prune(false), grow(false), grow_coarse(false), incremental(false), forced(false), sparse(false), dense(false), state_less(false), state_full(false), prune_bin(false), debug(__debug)
    {
      typedef cicada::Parameter param_type;

      param_type param(parameter);
      if (utils::ipiece(param.name()) != "apply")
	throw std::runtime_error("this is not a feature-functin applier");

      for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	if (utils::ipiece(piter->first) == "size")
	  size = utils::lexical_cast<int>(piter->second);
	else if (utils::ipiece(piter->first) == "diversity")
	  diversity = utils::lexical_cast<double>(piter->second);
	else if (utils::ipiece(piter->first) == "rejection")
	  rejection = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "exact")
	  exact = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "prune")
	  prune = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "grow")
	  grow = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "grow-coarse")
	  grow_coarse = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "incremental")
	  incremental = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "forced")
	  forced = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "sparse")
	  sparse = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "dense")
	  dense = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "state-full")
	  state_full = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "state-less")
	  state_less = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "prune-bin")
	  prune_bin = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "weights")
	  weights = &base_type::weights(piter->second);
	else if (utils::ipiece(piter->first) == "weights-one")
	  weights_one = utils::lexical_cast<bool>(piter->second);
	else if (utils::ipiece(piter->first) == "feature" || utils::ipiece(piter->first) == "feature-function")
	  model_local.push_back(feature_function_type::create(piter->second));
	else if (utils::ipiece(piter->first) == "weight") {
	  namespace qi = boost::spirit::qi;
	  namespace standard = boost::spirit::standard;

	  std::string::const_iterator iter = piter->second.begin();
	  std::string::const_iterator iter_end = piter->second.end();

	  std::string name;
	  double      value;
	  
	  if (! qi::phrase_parse(iter, iter_end,
				 qi::lexeme[+(!(qi::lit('=') >> qi::double_ >> (standard::space | qi::eoi))
					      >> (standard::char_ - standard::space))]
				 >> '='
				 >> qi::double_,
				 standard::blank, name, value) || iter != iter_end)
	    throw std::runtime_error("weight parameter parsing failed");
	  
	  weights_extra[name] = value;
	} else
	  std::cerr << "WARNING: unsupported parameter for apply: " << piter->first << "=" << piter->second << std::endl;
      }

      // default to prune...
      switch (int(exact) + prune + grow + grow_coarse + incremental) {
      case 0: prune = true; break; // default to cube-prune
      case 1: break; // OK
      default:
	throw std::runtime_error("specify one of exact/prune/grow/grow-coarse/incremental");
      }

      if (prune || grow || grow_coarse || incremental)
	if (size <= 0)
	  throw std::runtime_error("invalid beam size: " + utils::lexical_cast<std::string>(size));
      
      if (sparse && dense)
	throw std::runtime_error("either sparse|dense");
      
      if (state_full && state_less)
	throw std::runtime_error("either state-full| state-less");

      if (rejection && diversity != 0.0)
	throw std::runtime_error("either rejection or diversified pruning");
      
      if (rejection || diversity != 0.0)
	if (! prune)
	  throw std::runtime_error("rejection or diversified can be only combined with cube-pruning");
      
      // construct sparse or dense
      if (sparse) {
	model_type model_sparse;

	model_type& __model = const_cast<model_type&>(! model_local.empty() ? model_local : model);
	
	for (model_type::const_iterator iter = __model.begin(); iter != __model.end(); ++ iter)
	  if ((*iter)->sparse_feature())
	    model_sparse.push_back(*iter);
	
	if (model_sparse.empty())
	  throw std::runtime_error("we have no sparse features");
	
	model_local.swap(model_sparse);
      } else if (dense) {
	model_type model_dense;

	model_type& __model = const_cast<model_type&>(! model_local.empty() ? model_local : model);

	for (model_type::const_iterator iter = __model.begin(); iter != __model.end(); ++ iter)
	  if (! (*iter)->sparse_feature())
	    model_dense.push_back(*iter);
	
	if (model_dense.empty())
	  throw std::runtime_error("we have no dense features");
	
	model_local.swap(model_dense);
      }
      
      // construct state-less or state-full
      if (state_less) {
	model_type model_less;

	model_type& __model = const_cast<model_type&>(! model_local.empty() ? model_local : model);

	for (model_type::const_iterator iter = __model.begin(); iter != __model.end(); ++ iter)
	  if (! (*iter)->state_size())
	    model_less.push_back(*iter);
	
	if (model_less.empty())
	  throw std::runtime_error("we have no state-less features");
	
	model_local.swap(model_less);
      } else if (state_full) {
	model_type model_full;

	model_type& __model = const_cast<model_type&>(! model_local.empty() ? model_local : model);

	for (model_type::const_iterator iter = __model.begin(); iter != __model.end(); ++ iter)
	  if ((*iter)->state_size())
	    model_full.push_back(*iter);
	
	if (model_full.empty())
	  throw std::runtime_error("we have no state-full features");
	
	model_local.swap(model_full);
      }

      if (const_cast<model_type&>(! model_local.empty() ? model_local : model).empty())
	throw std::runtime_error("no features to apply?");
      
      if (weights && weights_one)
	throw std::runtime_error("you have weights, but specified all-one parameter");

      if (weights_one && ! weights_extra.empty())
	throw std::runtime_error("you have extra weights, but specified all-one parameter");
      
      if (weights || weights_one)
	weights_fixed = true;
      
      if (! weights)
	weights = &base_type::weights();
      
      name = (std::string("apply-")
	      + std::string(exact
			    ? "exact"
			    : (incremental
			       ? "incremental"
			       : (grow_coarse
				  ? "grow-coarse"
				  : (grow
				     ? "grow"
				     : (diversity != 0.0
					? "prune-diverse"
					: (rejection
					   ? "prune-rejection"
					   : "prune"))))))
	      + std::string(sparse ? "-sparse" : (dense ? "-dense" : ""))
	      + std::string(state_full ? "-statefull" : (state_less ? "-stateless" : "")));
    }
    
    void Apply::operator()(data_type& data) const
    {
      if (! data.hypergraph.is_valid()) return;
      
      typedef cicada::semiring::Logprob<double> weight_type;

      hypergraph_type& hypergraph = data.hypergraph;
      hypergraph_type applied;

      model_type& __model = const_cast<model_type&>(! model_local.empty() ? model_local : model);
      
      // assignment...
      __model.assign(data.id, data.hypergraph, data.lattice, data.spans, data.targets, data.ngram_counts);
      
      if (forced)
	__model.apply_feature(true);
      
      const weight_set_type* weights_apply = (weights_assigned ? weights_assigned : &(weights->weights));
      
      if (debug)
	std::cerr << name << ": " << data.id << std::endl;
    
      utils::resource start;
    
      // apply...
      if (exact)
	cicada::apply_exact(__model, hypergraph, applied);
      else if (incremental) {
	if (weights_one)
	  cicada::apply_incremental(__model, hypergraph, applied, weight_function_one<weight_type>(), size, prune_bin);
	else if (! weights_extra.empty())
	  cicada::apply_incremental(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), size, prune_bin);
	else
	  cicada::apply_incremental(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), size, prune_bin);
      } else if (grow) {
	if (weights_one)
	  cicada::apply_cube_grow(__model, hypergraph, applied, weight_function_one<weight_type>(), size, prune_bin);
	else if (! weights_extra.empty())
	  cicada::apply_cube_grow(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), size, prune_bin);
	else
	  cicada::apply_cube_grow(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), size, prune_bin);
      } else if (grow_coarse) {
	if (weights_one)
	  cicada::apply_cube_grow_coarse(__model, hypergraph, applied, weight_function_one<weight_type>(), size, prune_bin);
	else if (! weights_extra.empty())
	  cicada::apply_cube_grow_coarse(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), size, prune_bin);
	else
	  cicada::apply_cube_grow_coarse(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), size, prune_bin);
      } else {
	if (diversity != 0.0) {
	  if (weights_one)
	    cicada::apply_cube_prune_diverse(__model, hypergraph, applied, weight_function_one<weight_type>(), size, diversity, prune_bin);
	  else if (! weights_extra.empty())
	    cicada::apply_cube_prune_diverse(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), size, diversity, prune_bin);
	  else
	    cicada::apply_cube_prune_diverse(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), size, diversity, prune_bin);
	} else if (rejection) {
	  if (weights_one)
	    cicada::apply_cube_prune_rejection(__model, hypergraph, applied, weight_function_one<weight_type>(), const_cast<sampler_type&>(sampler), size, prune_bin);
	  else if (! weights_extra.empty())
	    cicada::apply_cube_prune_rejection(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), const_cast<sampler_type&>(sampler), size, prune_bin);
	  else
	    cicada::apply_cube_prune_rejection(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), const_cast<sampler_type&>(sampler), size, prune_bin);
	  
	} else {
	  if (weights_one)
	    cicada::apply_cube_prune(__model, hypergraph, applied, weight_function_one<weight_type>(), size, prune_bin);
	  else if (! weights_extra.empty())
	    cicada::apply_cube_prune(__model, hypergraph, applied, weight_function_extra<weight_type>(*weights_apply, weights_extra.begin(), weights_extra.end()), size, prune_bin);
	  else
	    cicada::apply_cube_prune(__model, hypergraph, applied, weight_function<weight_type>(*weights_apply), size, prune_bin);
	}
      }
    
      utils::resource end;
    
      __model.apply_feature(false);
    
      if (debug)
	std::cerr << name << ": " << data.id
		  << " cpu time: " << (end.cpu_time() - start.cpu_time())
		  << " user time: " << (end.user_time() - start.user_time())
		  << " thread time: " << (end.thread_time() - start.thread_time())
		  << std::endl;
      
      if (debug)
	std::cerr << name << ": " << data.id
		  << " # of nodes: " << applied.nodes.size()
		  << " # of edges: " << applied.edges.size()
		  << " valid? " << utils::lexical_cast<std::string>(applied.is_valid())
		  << std::endl;
      
      statistics_type::statistic_type& stat = data.statistics[name];
      
      ++ stat.count;
      stat.node += applied.nodes.size();
      stat.edge += applied.edges.size();
      stat.user_time += (end.user_time() - start.user_time());
      stat.cpu_time  += (end.cpu_time() - start.cpu_time());
      stat.thread_time  += (end.thread_time() - start.thread_time());
      
      hypergraph.swap(applied);
    }

    void Apply::assign(const weight_set_type& __weights)
    {
      if (! weights_fixed)
	weights_assigned = &__weights;
    }
  };
};

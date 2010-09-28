// -*- mode: c++ -*-

#ifndef __CICADA__OPERATION__OUTPUT__HPP__
#define __CICADA__OPERATION__OUTPUT__HPP__ 1

#include <iostream>

#include <cicada/operation.hpp>
#include <cicada/parameter.hpp>
#include <cicada/semiring.hpp>
#include <cicada/kbest.hpp>
#include <cicada/graphviz.hpp>

#include <cicada/operation/functional.hpp>
#include <cicada/operation/traversal.hpp>

#include <utils/lexical_cast.hpp>
#include <utils/resource.hpp>
#include <utils/sgi_hash_map.hpp>

namespace cicada
{
  namespace operation
  {

    template <typename Hypergraph, typename Function, typename Filter>
    inline
    void kbest_derivations(std::ostream& os,
			   const size_t id,
			   const Hypergraph& graph,
			   const int kbest_size,
			   const Function& function,
			   const Filter& filter)
    {
      typedef Hypergraph hypergraph_type;
      typedef typename hypergraph_type::rule_type rule_type;

      cicada::KBest<kbest_traversal_edges, Function, Filter> derivations(graph, kbest_size, kbest_traversal_edges(), function, filter);
      
      typedef kbest_traversal_edges::value_type    derivation_type;
      typedef kbest_traversal_edges::edge_set_type edge_set_type;
  
      typedef typename hypergraph_type::id_type id_type;

      typedef typename Function::value_type weight_type;

#ifdef HAVE_TR1_UNORDERED_MAP
      typedef std::tr1::unordered_map<id_type, id_type, utils::hashmurmur<size_t>, std::equal_to<id_type>,
				      std::allocator<std::pair<id_type, id_type> > > node_map_type;
#else
      typedef sgi::hash_map<id_type, id_type, utils::hashmurmur<size_t>, std::equal_to<id_type>,
			    std::allocator<std::pair<id_type, id_type> > > node_map_type;
#endif

      derivation_type derivation;
      weight_type     weight;
      node_map_type   node_maps;
      hypergraph_type graph_kbest;

      edge_set_type tails;
  
      for (int k = 0; k < kbest_size; ++ k) {
	if (! derivations(k, derivation, weight))
	  break;
    
	const edge_set_type& edges = boost::get<0>(derivation);
	node_maps.clear();
	graph_kbest.clear();
    
	id_type node_id = 0;
	edge_set_type::const_iterator eiter_end = edges.end();
	for (edge_set_type::const_iterator eiter = edges.begin(); eiter != eiter_end; ++ eiter)
	  if (node_maps.find(graph.edges[*eiter].head) == node_maps.end()) {
	    node_maps[graph.edges[*eiter].head] = node_id;
	    ++ node_id;
	  }
    
	for (id_type node = 0; node != node_id; ++ node)
	  graph_kbest.add_node();
    
	for (edge_set_type::const_iterator eiter = edges.begin(); eiter != eiter_end; ++ eiter) {
	  const typename hypergraph_type::edge_type& edge = graph.edges[*eiter];
      
	  tails.clear();
	  typename hypergraph_type::edge_type::node_set_type::const_iterator titer_end = edge.tails.end();
	  for (typename hypergraph_type::edge_type::node_set_type::const_iterator titer = edge.tails.begin(); titer != titer_end; ++ titer) {
	    typename node_map_type::const_iterator niter = node_maps.find(*titer);
	    if (niter == node_maps.end())
	      throw std::runtime_error("no node?");
	
	    tails.push_back(niter->second);
	  }
      
	  typename hypergraph_type::edge_type& edge_kbest = graph_kbest.add_edge(tails.begin(), tails.end());
	  edge_kbest.rule = edge.rule;
	  edge_kbest.features = edge.features;
      
	  graph_kbest.connect_edge(edge_kbest.id, node_maps[edge.head]);
	}
    
	typename node_map_type::const_iterator niter = node_maps.find(graph.goal);
	if (niter == node_maps.end())
	  throw std::runtime_error("did not reach goal?");
    
	graph_kbest.goal = niter->second;

	graph_kbest.topologically_sort();
    
	os << id << " ||| " << graph_kbest << " |||";
	typename rule_type::feature_set_type::const_iterator fiter_end = boost::get<1>(derivation).end();
	for (typename rule_type::feature_set_type::const_iterator fiter = boost::get<1>(derivation).begin(); fiter != fiter_end; ++ fiter)
	  os << ' ' << fiter->first << '=' << fiter->second;
	os << " ||| ";
	os << weight;
	os << '\n';
      }
    }


    template <typename Hypergraph, typename Traversal, typename Function, typename Filter>
    inline
    void kbest_derivations(std::ostream& os,
			   const size_t id,
			   const Hypergraph& graph,
			   const int kbest_size,
			   const Traversal& traversal, 
			   const Function& function,
			   const Filter& filter)
    {
      typedef Hypergraph hypergraph_type;
      typedef typename hypergraph_type::rule_type rule_type;
      typedef typename rule_type::feature_set_type feature_set_type;
      
      cicada::KBest<Traversal, Function, Filter> derivations(graph, kbest_size, traversal, function, filter);
  
      typename Traversal::value_type derivation;
      typename Function::value_type  weight;
  
      for (int k = 0; k < kbest_size; ++ k) {
	if (! derivations(k, derivation, weight))
	  break;
    
	os << id << " ||| " << boost::get<0>(derivation) << " |||";
	typename rule_type::feature_set_type::const_iterator fiter_end = boost::get<1>(derivation).end();
	for (typename rule_type::feature_set_type::const_iterator fiter = boost::get<1>(derivation).begin(); fiter != fiter_end; ++ fiter)
	  os << ' ' << fiter->first << '=' << fiter->second;
	os << " ||| ";
	os << weight;
	os << '\n';
      }
    }

    class Output : public Operation
    {
    public:
      Output(const std::string& parameter, output_data_type& __output_data, const int __debug)
	: output_data(__output_data), file(), directory(), weights(0), weights_one(false),
	  kbest_size(0), kbest_unique(false),
	  yield_source(false), yield_target(false), yield_tree(false),
	  graphviz(false),
	  debug(__debug)
      {
	typedef cicada::Parameter param_type;
    
	param_type param(parameter);
	if (param.name() != "output")
	  throw std::runtime_error("this is not a outputter");

    
	for (param_type::const_iterator piter = param.begin(); piter != param.end(); ++ piter) {
	  if (strcasecmp(piter->first.c_str(), "kbest") == 0)
	    kbest_size = boost::lexical_cast<int>(piter->second);
	  else if (strcasecmp(piter->first.c_str(), "unique") == 0)
	    kbest_unique = utils::lexical_cast<bool>(piter->second);
	  else if (strcasecmp(piter->first.c_str(), "weights") == 0)
	    weights = &base_type::weights(piter->second);
	  else if (strcasecmp(piter->first.c_str(), "weights-one") == 0)
	    weights_one = utils::lexical_cast<bool>(piter->second);
	  else if (strcasecmp(piter->first.c_str(), "graphviz") == 0)
	    graphviz = utils::lexical_cast<bool>(piter->second);
	  else if (strcasecmp(piter->first.c_str(), "file") == 0)
	    file = piter->second;
	  else if (strcasecmp(piter->first.c_str(), "directory") == 0)
	    directory = piter->second;
	  else if (strcasecmp(piter->first.c_str(), "yield") == 0) {
	    const std::string& value = piter->second;
	
	    if (strcasecmp(value.c_str(), "source") == 0)
	      yield_source = true;
	    else if (strcasecmp(value.c_str(), "target") == 0)
	      yield_target = true;
	    else if (strcasecmp(value.c_str(), "derivation") == 0 || strcasecmp(value.c_str(), "tree") == 0)
	      yield_tree = true;
	    else
	      throw std::runtime_error("unknown yield: " + value);
	  } else
	    std::cerr << "WARNING: unsupported parameter for output: " << piter->first << "=" << piter->second << std::endl;
	}

    
	if (weights && weights_one)
	  throw std::runtime_error("you have weights, but specified all-one parameter");
    
	// default to stdout
	if (directory.empty() && file.empty())
	  file = "-";
    
	if (! directory.empty() && ! file.empty())
	  throw std::runtime_error("you cannot output both in directory and file");
    
	if (int(yield_source) + yield_target + yield_tree > 1)
	  throw std::runtime_error("only source, target or tree yield for kbest");
    
	if (! yield_source && ! yield_target && ! yield_tree)
	  yield_target = true;
      }

      void assign(const weight_set_type& __weights)
      {
	weights = &__weights;
      }
  
      void clear()
      {
	output_data.buffer.clear();
	
	if (output_data.os) {
	  if (! directory.empty())
	    output_data.os.reset();
	  else
	    *output_data.os << std::flush;
	}
      }
  
      void operator()(data_type& data) const
      {
	typedef cicada::semiring::Logprob<double> weight_type;

	const size_type& id = data.id;
	const hypergraph_type& hypergraph = data.hypergraph;
    
	if (! hypergraph.is_valid()) return;
	
	boost::iostreams::filtering_ostream os_buffer;
	if (output_data.use_buffer)
	  os_buffer.push(boost::iostreams::back_inserter(const_cast<std::string&>(output_data.buffer)));
	else if (! output_data.os) {
	  const path_type path = (! file.empty() ? file  : directory / (boost::lexical_cast<std::string>(id) + ".gz"));
	  const_cast<boost::shared_ptr<std::ostream>&>(output_data.os).reset(new utils::compress_ostream(path, 1024 * 1024));
	}
	
	std::ostream& os = (output_data.use_buffer
			    ? static_cast<std::ostream&>(os_buffer)
			    : *output_data.os);

	utils::resource start;
    
	if (kbest_size <= 0) {
	  if (debug)
	    std::cerr << "output graph: " << data.id << std::endl;

	  if (graphviz)
	    cicada::graphviz(os, hypergraph);
	  else
	    os << id << " ||| " << hypergraph << '\n';
	} else {
	  if (debug)
	    std::cerr << "output " << kbest << "-best for graph: "<< data.id << std::endl;

	  weight_set_type weights_zero;
	  const weight_set_type* weights_kbest = (weights ? weights : &weights_zero);
      
	  if (weights_one) {
	    if (kbest_unique) {
	      if (yield_source)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_source(), weight_function_one<weight_type>(), kbest_filter_unique(hypergraph));
	      else if (yield_target)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_target(), weight_function_one<weight_type>(), kbest_filter_unique(hypergraph));
	      else
		kbest_derivations(os, id, hypergraph, kbest_size, weight_function_one<weight_type>(), kbest_filter());
	    } else {
	      if (yield_source)	  
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_source(), weight_function_one<weight_type>(), kbest_filter());
	      else if (yield_target)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_target(), weight_function_one<weight_type>(), kbest_filter());
	      else
		kbest_derivations(os, id, hypergraph, kbest_size, weight_function_one<weight_type>(), kbest_filter());
	    }
	  } else {
	    if (kbest_unique) {
	      if (yield_source)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_source(), weight_function<weight_type>(*weights_kbest), kbest_filter_unique(hypergraph));
	      else if (yield_target)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_target(), weight_function<weight_type>(*weights_kbest), kbest_filter_unique(hypergraph));
	      else
		kbest_derivations(os, id, hypergraph, kbest_size, weight_function<weight_type>(*weights_kbest), kbest_filter());
	    } else {
	      if (yield_source)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_source(), weight_function<weight_type>(*weights_kbest), kbest_filter());
	      else if (yield_target)
		kbest_derivations(os, id, hypergraph, kbest_size, kbest_traversal_target(), weight_function<weight_type>(*weights_kbest), kbest_filter());
	      else
		kbest_derivations(os, id, hypergraph, kbest_size, weight_function<weight_type>(*weights_kbest), kbest_filter());
	    }
	  }
	}
	
	utils::resource end;
	
	if (debug)
	  std::cerr << "output cpu time: " << (end.cpu_time() - start.cpu_time())
		    << " user time: " << (end.user_time() - start.user_time())
		    << std::endl;
      }
      
      output_data_type& output_data;
  
      path_type file;
      path_type directory;
  
      const weight_set_type* weights;
      bool weights_one;
      int  kbest_size;
      bool kbest_unique;

      bool yield_source;
      bool yield_target;
      bool yield_tree;

      bool graphviz;
  
      int debug;
    };

  };
};


#endif

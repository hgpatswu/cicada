//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <cmath>

#include <cicada/operation/functional.hpp>
#include <cicada/operation/traversal.hpp>
#include <cicada/optimize/line_search.hpp>

#include <cicada/prune.hpp>
#include <cicada/dot_product.hpp>

struct LineSearch
{
  typedef cicada::eval::Scorer         scorer_type;
  typedef cicada::eval::ScorerDocument scorer_document_type;
  
  typedef scorer_type::scorer_ptr_type scorer_ptr_type;
  typedef scorer_type::score_ptr_type  score_ptr_type;

  typedef cicada::optimize::LineSearch line_search_type;
  
  typedef line_search_type::segment_type          segment_type;
  typedef line_search_type::segment_set_type      segment_set_type;
  typedef line_search_type::segment_document_type segment_document_type;

  typedef line_search_type::value_type optimum_type;
  
  typedef cicada::semiring::Envelope envelope_type;
  typedef std::vector<envelope_type, std::allocator<envelope_type> >  envelope_set_type;

  typedef cicada::Sentence sentence_type;

  LineSearch(const int __debug=0) : line_search(__debug), debug(__debug) {}

  segment_document_type segments;
  envelope_set_type     envelopes;
  
  line_search_type line_search;
  int debug;
  
  template <typename HypergraphSet, typename ScorerSet>
  double operator()(const HypergraphSet& graphs,
		    const ScorerSet& scorers,
		    const weight_set_type& origin,
		    const weight_set_type& direction,
		    const double lower,
		    const double upper)
  {
    segments.clear();
    segments.resize(graphs.size());
    
    for (size_t seg = 0; seg != graphs.size(); ++ seg) 
      if (graphs[seg].is_valid()) {
	
	if (debug >= 4)
	  std::cerr << "line-search segment: " << seg << std::endl;
	
	envelopes.clear();
	envelopes.resize(graphs[seg].nodes.size());
	
	cicada::inside(graphs[seg], envelopes, cicada::semiring::EnvelopeFunction<weight_set_type>(origin, direction));
	
	const envelope_type& envelope = envelopes[graphs[seg].goal];
	const_cast<envelope_type&>(envelope).sort();
	
	envelope_type::const_iterator eiter_end = envelope.end();
	for (envelope_type::const_iterator eiter = envelope.begin(); eiter != eiter_end; ++ eiter) {
	  const envelope_type::line_ptr_type& line = *eiter;
	  
	  const sentence_type yield = line->yield(cicada::operation::sentence_traversal());
	  
	  scorer_type::score_ptr_type score = scorers[seg]->score(yield);
	  
	  segments[seg].push_back(std::make_pair(line->x, score));
	}
      }
    
    // traverse segments...
    const optimum_type optimum = line_search(segments, lower, upper);
    return (optimum.lower + optimum.upper) * 0.5;
  }
  
};

struct OptimizerBase
{
  OptimizerBase(const hypergraph_set_type&           __graphs,
		const feature_function_ptr_set_type& __features)
    : graphs(__graphs),
      features(__features),
      weight_scale(1.0),
      weight_norm(0.0)
  {
    // initialize weights and weights scorer...
    for (size_t i = 0; i != features.size(); ++ i)
      if (features[i]) {
	feature_scorer = features[i]->feature_name();
	break;
      }
    
    weights_scorer[feature_scorer] = loss_scale;
  }
  
  typedef cicada::semiring::Log<double> weight_type;
  typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > gradient_type;
  typedef cicada::WeightVector<weight_type >  expectation_type;
  
  typedef std::vector<weight_type, std::allocator<weight_type> > inside_set_type;
  
  typedef cicada::semiring::Tropical<double> scorer_weight_type;
  typedef std::vector<scorer_weight_type, std::allocator<scorer_weight_type> > scorer_weight_set_type;
  
  struct gradient_set_type
  {
    typedef gradient_type value_type;
      
    template <typename Index>
    gradient_type& operator[](Index)
    {
      return gradient;
    }

    void clear() { gradient.clear(); }
      
    gradient_type gradient;
  };


  struct weight_function
  {
    typedef weight_type value_type;

    weight_function(const weight_set_type& __weights)
      : weights(__weights) {}

    const weight_set_type& weights;

    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      // p_e
      return cicada::semiring::traits<value_type>::exp(cicada::dot_product(edge.features, weights));
    }
  };

  struct weight_max_function
  {
    typedef weight_type value_type;

    weight_max_function(const weight_set_type& __weights, const scorer_weight_set_type& __scorers, const scorer_weight_type& __max_scorer)
      : weights(__weights), scorers(__scorers), max_scorer(__max_scorer) {}

    const weight_set_type&      weights;
    const scorer_weight_set_type& scorers;
    const scorer_weight_type      max_scorer;

    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      // p_e
      if (log(max_scorer) - log(scorers[edge.id]) >= 1e-4)
	return value_type();
      else
	return cicada::semiring::traits<value_type>::exp(cicada::dot_product(edge.features, weights));
    }
  };

  struct feature_function
  {
    typedef gradient_type value_type;

    feature_function(const weight_set_type& __weights) : weights(__weights) {}

    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      // p_e r_e
      gradient_type grad;
	
      const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(edge.features, weights));
	
      feature_set_type::const_iterator fiter_end = edge.features.end();
      for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	if (fiter->second != 0.0)
	  grad[fiter->first] = weight_type(fiter->second) * weight;
	
      return grad;
    }
      
    const weight_set_type& weights;
  };
  

  struct feature_max_function
  {
    typedef gradient_type value_type;

    feature_max_function(const weight_set_type& __weights, const scorer_weight_set_type& __scorers, const scorer_weight_type& __max_scorer)
      : weights(__weights), scorers(__scorers), max_scorer(__max_scorer) {}

    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      // p_e r_e
      if (log(max_scorer) - log(scorers[edge.id]) >= 1e-4)
	return gradient_type();

      gradient_type grad;
	
      const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(edge.features, weights));
	
      feature_set_type::const_iterator fiter_end = edge.features.end();
      for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	if (fiter->second != 0.0)
	  grad[fiter->first] = weight_type(fiter->second) * weight;
	
      return grad;
    }
    
    
    const weight_set_type&      weights;
    const scorer_weight_set_type& scorers;
    const scorer_weight_type      max_scorer;
  };
  
  bool operator()(const int id)
  {
    if (! graphs[id].is_valid()) return false;
    
    model_type model;
    model.push_back(features[id]);
    
    if (! apply_exact) {
      weights[feature_scorer] = loss_scale / weight_scale;
      cicada::apply_cube_prune(model, graphs[id], graph_reward, cicada::operation::weight_function<cicada::semiring::Logprob<double> >(weights), cube_size);
      
      weights[feature_scorer] = - loss_scale / weight_scale;
      cicada::apply_cube_prune(model, graphs[id], graph_penalty, cicada::operation::weight_function<cicada::semiring::Logprob<double> >(weights), cube_size);
      
      weights[feature_scorer] = 0.0;
      
      graph_reward.unite(graph_penalty);
      graph_penalty.clear();
    }
    
    const hypergraph_type& graph = (apply_exact ? graphs[id] : graph_reward);
    
    // compute inside/outside by scorer using tropical semiring...
    scorers_inside.clear();
    scorers_inside_outside.clear();
    
    scorers_inside.resize(graph.nodes.size());
    scorers_inside_outside.resize(graph.edges.size());
    
    cicada::inside_outside(graph,
			   scorers_inside,
			   scorers_inside_outside,
			   cicada::operation::weight_function<scorer_weight_type >(weights_scorer),
			   cicada::operation::weight_function<scorer_weight_type >(weights_scorer));
    
    const scorer_weight_type scorer_max = *std::max_element(scorers_inside_outside.begin(), scorers_inside_outside.end());
    
    // then, inside/outside to collect potentials...
    
    inside.clear();
    inside_correct.clear();
    
    gradients.clear();
    gradients_correct.clear();
    
    inside.resize(graph.nodes.size());
    inside_correct.resize(graph.nodes.size());
    
    if (softmax_margin) {
      weights[feature_scorer] = - loss_scale / weight_scale;
      
      cicada::inside_outside(graph, inside, gradients, weight_function(weights), feature_function(weights));
      
      cicada::inside_outside(graph, inside_correct, gradients_correct,
			     weight_max_function(weights, scorers_inside_outside, scorer_max),
			     feature_max_function(weights, scorers_inside_outside, scorer_max));
      
      weights[feature_scorer] = 0.0;
    } else {
      cicada::inside_outside(graph, inside, gradients, weight_function(weights), feature_function(weights));
      
      cicada::inside_outside(graph, inside_correct, gradients_correct,
			     weight_max_function(weights, scorers_inside_outside, scorer_max),
			     feature_max_function(weights, scorers_inside_outside, scorer_max));
    }
    
    Z = inside.back();
    Z_correct = inside_correct.back();
    
    gradients.gradient /= Z;
    gradients_correct.gradient /= Z_correct;

    const double margin = log(Z_correct) - log(Z);

    if (debug >= 3)
      std::cerr << "id: " << id
		<< " scorer: " << log(scorer_max)
		<< " correct: " << log(Z_correct)
		<< " partition: " << log(Z)
		<< " margin: " << margin
		<< std::endl;

    return true;
  }
  
  const hypergraph_set_type&           graphs;
  const feature_function_ptr_set_type& features;
  
  weight_set_type weights;
  weight_set_type weights_scorer;
  weight_set_type::feature_type feature_scorer;

  double objective;
  
  double weight_scale;
  double weight_norm;
  
  hypergraph_type graph_reward;
  hypergraph_type graph_penalty;

  scorer_weight_set_type scorers_inside;
  scorer_weight_set_type scorers_inside_outside;

  inside_set_type   inside;
  gradient_set_type gradients;

  inside_set_type   inside_correct;
  gradient_set_type gradients_correct;
  
  weight_type Z;
  weight_type Z_correct;
};

struct OptimizerSGDL2 : public OptimizerBase
{
  OptimizerSGDL2(const hypergraph_set_type&           __graphs,
		 const feature_function_ptr_set_type& __features)
    : OptimizerBase(__graphs, __features),
      samples(0),
      epoch(0),
      lambda(C / __graphs.size()) {}
  
  void initialize()
  {
    samples = 0;
    
    weights[feature_scorer] = 0.0;
    
    weight_scale = 1.0;
    weight_norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
    
    objective = 0.0;
  }
  
  void finalize()
  {
    weights[feature_scorer] = 0.0;
    
    weights *= weight_scale;
    
    weight_scale = 1.0;
    weight_norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
  }
  
  void operator()(const int segment)
  {
    //const double eta = 1.0 / (lambda * (epoch + 2));
    //const double eta = 1.0 / (1.0 + double(epoch) / graphs.size());
    const double eta = 0.2 * std::pow(0.85, double(epoch) / graphs.size());
    ++ epoch;
    
    // we will minimize...
    if (OptimizerBase::operator()(segment)) {
      // we have gradients_correct/gradients/Z_correct/Z
      
      rescale(1.0 - eta * lambda);
      
      // update wrt correct gradients
      gradient_type::const_iterator citer_end = gradients_correct.gradient.end();
      for (gradient_type::const_iterator citer = gradients_correct.gradient.begin(); citer != citer_end; ++ citer) 
	if (citer->first != feature_scorer) {
	  const double feature = citer->second;
	  update(weights[citer->first], feature * eta);
	}
      
      // update wrt marginal gradients...
      gradient_type::const_iterator miter_end = gradients.gradient.end();
      for (gradient_type::const_iterator miter = gradients.gradient.begin(); miter != miter_end; ++ miter) 
	if (miter->first != feature_scorer) {
	  const double feature = miter->second;
	  update(weights[miter->first], - feature * eta);
	}
      
      // projection...
      if (weight_norm > 1.0 / lambda)
	rescale(std::sqrt(1.0 / (lambda * weight_norm)));
      
      objective += double(log(Z_correct) - log(Z)) * weight_scale;
      
      if (weight_scale < 0.01 || 100 < weight_scale) {
	weights[feature_scorer] = 0.0;
	weights *= weight_scale;
	weight_scale = 1.0;
	weight_norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
      }

      ++ samples;
    }
  }  
  
  
  void update(double& x, const double& alpha)
  {
    weight_norm += 2.0 * x * alpha * weight_scale + alpha * alpha;
    x += alpha / weight_scale;
  }
  
  void rescale(const double scaling)
  {
    weight_norm *= scaling * scaling;
    if (scaling != 0.0)
      weight_scale *= scaling;
    else {
      weight_scale = 1.0;
      std::fill(weights.begin(), weights.end(), 0.0);
    }
  }
  
  size_t samples;
  size_t epoch;
  double lambda;
};

struct OptimizerSGDL1 : public OptimizerBase
{
  typedef cicada::WeightVector<double> penalty_set_type;
  
  OptimizerSGDL1(const hypergraph_set_type&           __graphs,
		 const feature_function_ptr_set_type& __features)
    : OptimizerBase(__graphs, __features),
      samples(0),
      epoch(0),
      lambda(C / __graphs.size()),
      penalties(),
      penalty(0.0) {}
  
  void initialize()
  {
    samples = 0;

    weights[feature_scorer] = 0.0;
    
    weight_scale = 1.0;
    
    objective = 0.0;
    
    //penalties.clear();
    //penalty = 0.0;
  }
  
  void finalize()
  {
    
  }
  
  void operator()(const int segment)
  {
    //const double eta = 1.0 / (1.0 + double(epoch) / graphs.size());
    //const double eta = 1.0 / (lambda * (epoch + 2));
    const double eta = 0.2 * std::pow(0.85, double(epoch) / graphs.size());
    ++ epoch;
    
    // cummulative penalty
    penalty += eta * lambda;
    
    // we will maximize, not minimize...
    if (OptimizerBase::operator()(segment)) {
      // we have gradients_correct/gradients/Z_correct/Z
      
      gradient_type::const_iterator citer_end = gradients_correct.gradient.end();
      for (gradient_type::const_iterator citer = gradients_correct.gradient.begin(); citer != citer_end; ++ citer) 
	if (citer->first != feature_scorer) {
	  const double feature = citer->second;
	  
	  weights[citer->first] += eta * feature;
	  apply(weights[citer->first], penalties[citer->first], penalty);
	}
      
      gradient_type::const_iterator miter_end = gradients.gradient.end();
      for (gradient_type::const_iterator miter = gradients.gradient.begin(); miter != miter_end; ++ miter) 
	if (miter->first != feature_scorer) {
	  const double feature = miter->second;
	  
	  weights[miter->first] -= eta * feature;
	  apply(weights[miter->first], penalties[miter->first], penalty);
	}
      
      objective += double(log(Z_correct) - log(Z));
      ++ samples;
    }
  }

  void apply(double& x, double& penalty, const double& cummulative)
  {
    const double x_half = x;
    if (x > 0.0)
      x = std::max(0.0, x - penalty - cummulative);
    else if (x < 0.0)
      x = std::min(0.0, x - penalty + cummulative);
    penalty += x - x_half;
  }

  size_t samples;
  size_t epoch;
  double lambda;
  
  penalty_set_type penalties;
  double penalty;
};

// we will implement MIRA/CP 
// we have two options: compute oracle by hill-climbing, or use dynamically computed oracle...
struct OptimizerMarginBase
{
  struct Accumulated
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > accumulated_type;
    
    typedef accumulated_type value_type;
    
    accumulated_type& operator[](size_t index)
    {
      return accumulated;
    }
    
    void clear() { accumulated.clear(); }
    
    value_type accumulated;
  };
  typedef Accumulated accumulated_type;

  struct count_function
  {
    typedef cicada::semiring::Log<double> value_type;
    
    template <typename Edge>
    value_type operator()(const Edge& x) const
    {
      return cicada::semiring::traits<value_type>::exp(0.0);
    }
  };

  struct feature_count_function
  {
    typedef cicada::semiring::Log<double> weight_type;
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > accumulated_type;
    typedef accumulated_type value_type;
    
    template <typename Edge>
    value_type operator()(const Edge& edge) const
    {
      accumulated_type accumulated;
      
      feature_set_type::const_iterator fiter_end = edge.features.end();
      for (feature_set_type::const_iterator fiter = edge.features.begin(); fiter != fiter_end; ++ fiter)
	if (fiter->second != 0.0)
	  accumulated[fiter->first] = weight_type(fiter->second);
      
      return accumulated;
    }
  };
  
  typedef std::vector<count_function::value_type, std::allocator<count_function::value_type> > count_set_type;

  OptimizerMarginBase(const hypergraph_set_type&           __graphs,
		      const feature_function_ptr_set_type& __features,
		      const double __beam,
		      const int __kbest)
    : graphs(__graphs),
      features(__features),
      beam(__beam),
      kbest(__kbest)
  {
    // initialize weights and weights scorer...
    for (size_t i = 0; i != features.size(); ++ i)
      if (features[i]) {
	feature_scorer = features[i]->feature_name();
	break;
      }
    
    weights_scorer[feature_scorer] = loss_scale;
  }

  bool operator()(const int id)
  {
    if (! graphs[id].is_valid()) return false;

    if (apply_exact) {
      weights[feature_scorer] = loss_scale;
      
      if (kbest > 0) 
	cicada::prune_kbest(graphs[id], graph_reward, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights_scorer), kbest);
      else
	cicada::prune_beam(graphs[id], graph_reward, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights_scorer), beam);
      
      weights[feature_scorer] = - loss_scale;
      
      if (kbest > 0)
	cicada::prune_kbest(graphs[id], graph_penalty, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights), kbest);
      else
	cicada::prune_beam(graphs[id], graph_penalty, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights), beam);
      
      weights[feature_scorer] = 0.0;
    } else {
      model_type model;
      model.push_back(features[id]);
      
      // we compute rewarded derivertive..
      weights[feature_scorer] = loss_scale;
      cicada::apply_cube_prune(model, graphs[id], graph_reward, cicada::operation::weight_function<cicada::semiring::Logprob<double> >(weights), cube_size);
      
      if (kbest > 0) 
	cicada::prune_kbest(graph_reward, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights_scorer), kbest);
      else
	cicada::prune_beam(graph_reward, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights_scorer), beam);
      
      // we compute violated derivertive..
      weights[feature_scorer] = - loss_scale;
      cicada::apply_cube_prune(model, graphs[id], graph_penalty, cicada::operation::weight_function<cicada::semiring::Logprob<double> >(weights), cube_size);
      
      if (kbest > 0)
	cicada::prune_kbest(graph_penalty, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights), kbest);
      else
	cicada::prune_beam(graph_penalty, cicada::operation::weight_function<cicada::semiring::Tropical<double> >(weights), beam);
      
      weights[feature_scorer] = 0.0;
    }
    
    count_set_type counts_reward(graph_reward.nodes.size());
    count_set_type counts_penalty(graph_penalty.nodes.size());
    
    accumulated_type accumulated_reward;
    accumulated_type accumulated_penalty;
    
    cicada::inside_outside(graph_reward,  counts_reward,  accumulated_reward,  count_function(), feature_count_function());
    cicada::inside_outside(graph_penalty, counts_penalty, accumulated_penalty, count_function(), feature_count_function());
    
    features_reward.assign(accumulated_reward.accumulated);
    features_penalty.assign(accumulated_penalty.accumulated);
    
    features_reward  *= (1.0 / double(counts_reward.back()));
    features_penalty *= (1.0 / double(counts_penalty.back()));
    
    return true;
  }
  
  const hypergraph_set_type&           graphs;
  const feature_function_ptr_set_type& features;

  const double beam;
  const int kbest;
  
  weight_set_type weights;
  weight_set_type weights_scorer;
  weight_set_type::feature_type feature_scorer;
  
  double objective;
  
  hypergraph_type graph_reward;
  hypergraph_type graph_penalty;

  feature_set_type features_reward;
  feature_set_type features_penalty;
};


struct OptimizerMIRA : public OptimizerMarginBase
{  
  typedef OptimizerMarginBase base_type;

  OptimizerMIRA(const hypergraph_set_type&           __graphs,
	       const feature_function_ptr_set_type& __features,
	       const double __beam,
	       const int __kbest)
    : OptimizerMarginBase(__graphs, __features, __beam, __kbest) {}
  
  void initialize()
  {
    samples = 0;

    weights[feature_scorer] = 0.0;
    objective = 0.0;
  }
  
  void operator()(const int seg)
  {
    if (base_type::operator()(seg)) {
      // compute difference and update!
      // do we use the difference of score...? or simply use zero-one?
      
      feature_set_type features(features_reward - features_penalty);
      const double loss = loss_scale * features[feature_scorer];
      features.erase(feature_scorer);
      
      const double margin = cicada::dot_product(weights, features);
      const double variance = cicada::dot_product(features, features);
      
      objective += loss - margin;
      
      const double alpha = std::max(0.0, std::min(1.0 / C, (loss - margin) / variance));
      
      if (alpha > 1e-10) {
	feature_set_type::const_iterator fiter_end = features.end();
	for (feature_set_type::const_iterator fiter = features.begin(); fiter != fiter_end; ++ fiter)
	  weights[fiter->first] += alpha * fiter->second;
      }

      ++ samples;
    }
  }
  
  void finalize()
  {
    
  }
  
  size_t samples;
};


struct OptimizerAROW : public OptimizerMarginBase
{  
  typedef OptimizerMarginBase base_type;

  OptimizerAROW(const hypergraph_set_type&           __graphs,
		const feature_function_ptr_set_type& __features,
		const double __beam,
		const int __kbest)
      : OptimizerMarginBase(__graphs, __features, __beam, __kbest) {}
  
  
  void initialize()
  {
    samples = 0;

    weights[feature_scorer] = 0.0;
    objective = 0.0;
  }
  
  void operator()(const int seg)
  {
    if (base_type::operator()(seg)) {
      // compute difference and update!
      // do we use the difference of score...? or simply use zero-one?
      
      // allocate enough buffer for covariances...
      covariances.allocate(1.0);
      
      feature_set_type features(features_reward - features_penalty);
      const double loss = loss_scale * features[feature_scorer];
      features.erase(feature_scorer);
      
      const double margin = cicada::dot_product(weights, features);
      const double variance = cicada::dot_product(features, covariances, features); // multiply covariances...
      
      objective += loss - margin;

      const double beta = 1.0 / (variance + C);
      const double alpha = std::max(0.0, (loss - margin) * beta);
      
      if (alpha > 1e-10) {
	feature_set_type::const_iterator fiter_end = features.end();
	for (feature_set_type::const_iterator fiter = features.begin(); fiter != fiter_end; ++ fiter) {
	  const double var = covariances[fiter->first];
	  
	  weights[fiter->first]     += alpha * fiter->second * var;
	  covariances[fiter->first] -= beta * (var * var) * (fiter->second * fiter->second);
	}
      }

      ++ samples;
    }
  }
  
  void finalize()
  {
    
  }
  
  weight_set_type covariances;
  
  size_t samples;
};

struct OptimizerCW : public OptimizerMarginBase
{  
  typedef OptimizerMarginBase base_type;
  
  OptimizerCW(const hypergraph_set_type&           __graphs,
	      const feature_function_ptr_set_type& __features,
	      const double __beam,
	      const int __kbest)
    : OptimizerMarginBase(__graphs, __features, __beam, __kbest) {}
  
  
  void initialize()
  {
    samples = 0;
    
    weights[feature_scorer] = 0.0;
    objective = 0.0;
  }
  
  void operator()(const int seg)
  {
    if (base_type::operator()(seg)) {
      // compute difference and update!
      // do we use the difference of score...? or simply use zero-one?
      
      // allocate enough buffer for covariances...
      covariances.allocate(1.0);
      
      feature_set_type features(features_reward - features_penalty);
      const double loss = loss_scale * features[feature_scorer];
      features.erase(feature_scorer);
      
      const double margin = cicada::dot_product(weights, features);
      const double variance = cicada::dot_product(features, covariances, features); // multiply covariances...
      
      objective += loss - margin;

      if (loss - margin > 0.0) {
	const double theta = 1.0 + 2.0 * C * (margin - loss);
	const double alpha = ((- theta + std::sqrt(theta * theta - 8.0 * C * (margin - loss - C * variance))) / (4.0 * C * variance));
	const double beta  = (2.0 * alpha * C) / (1.0 + 2.0 * alpha * C * variance);
	
	if (alpha > 1e-10 && beta > 0.0) {
	  feature_set_type::const_iterator fiter_end = features.end();
	  for (feature_set_type::const_iterator fiter = features.begin(); fiter != fiter_end; ++ fiter) {
	    const double var = covariances[fiter->first];
	    
	    weights[fiter->first]     += alpha * fiter->second * var;
	    covariances[fiter->first] -= beta * (var * var) * (fiter->second * fiter->second);
	  }
	}
      }
      
      ++ samples;
    }
  }
  
  void finalize()
  {
    
  }
  
  weight_set_type covariances;
  
  size_t samples;
};

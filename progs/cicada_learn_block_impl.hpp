//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA_LEARN_BLOCK_IMPL__HPP__
#define __CICADA_LEARN_BLOCK_IMPL__HPP__ 1

#define BOOST_SPIRIT_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>

#include <sstream>
#include <vector>

#include "cicada_kbest_impl.hpp"

#include "cicada/semiring.hpp"
#include "cicada/eval.hpp"

#include "cicada/kbest.hpp"
#include "cicada/operation/traversal.hpp"
#include "cicada/operation/functional.hpp"

#include "utils/sgi_hash_set.hpp"
#include "utils/base64.hpp"
#include "utils/space_separator.hpp"
#include "utils/piece.hpp"

#include <boost/tokenizer.hpp>

#include "lbfgs.h"
#include "liblinear/linear.h"

typedef cicada::eval::Scorer         scorer_type;
typedef cicada::eval::ScorerDocument scorer_document_type;

// MIRA learner
// We will run a qp solver and determine the alpha, then, translate this into w
struct LearnMIRA
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef hypothesis_type::feature_set_type feature_set_type;
  typedef hypothesis_type::feature_value_type feature_value_type;
  
  typedef std::vector<feature_set_type, std::allocator<feature_set_type> > feature_map_type;
  typedef std::vector<double, std::allocator<double> > label_map_type;
  
  //
  // typedef for unique sentences
  //
  struct hash_sentence : public utils::hashmurmur<size_t>
  {
    typedef utils::hashmurmur<size_t> hasher_type;

    size_t operator()(const hypothesis_type::sentence_type& x) const
    {
      return hasher_type()(x.begin(), x.end(), 0);
    }
  };
#ifdef HAVE_TR1_UNORDERED_SET
  typedef std::tr1::unordered_set<hypothesis_type::sentence_type, hash_sentence, std::equal_to<hypothesis_type::sentence_type>, std::allocator<hypothesis_type::sentence_type> > sentence_unique_type;
#else
  typedef sgi::hash_set<hypothesis_type::sentence_type, hash_sentence, std::equal_to<hypothesis_type::sentence_type>, std::allocator<hypothesis_type::sentence_type> > sentence_unique_type;
#endif


  template <typename FeatureSet>
  struct HMatrix
  {
    HMatrix(const FeatureSet& __features) : features(__features) {}
    
    double operator()(int i, int j) const
    {
      return cicada::dot_product(features[i].begin(), features[i].end(), features[j].begin(), features[j].end(), 0.0);
    }
    
    const FeatureSet& features;
  };

  typedef std::vector<double, std::allocator<double> >    alpha_type;
  typedef std::vector<double, std::allocator<double> >    gradient_type;
  typedef std::vector<bool, std::allocator<bool> >        skipped_type;

  LearnMIRA() : tolerance(1e-4), lambda(C) {}
  
  void clear()
  {
    
  }
  
  std::ostream& encode(std::ostream& os)
  {
    return os;
  }
  
  std::istream& decode(std::istream& is)
  {
    return is;
  }
  
  void encode(const size_type id, const hypothesis_set_type& kbests, const hypothesis_set_type& oracles, const bool merge=false)
  {
    typedef std::vector<feature_value_type, std::allocator<feature_value_type> > features_type;
    typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
    
    if (kbests.empty() || oracles.empty()) return;
    
    sentences.clear();
    for (size_t o = 0; o != oracles.size(); ++ o)
      sentences.insert(oracles[o].sentence);

    features_type feats;
    
    for (size_t o = 0; o != oracles.size(); ++ o)
      for (size_t k = 0; k != kbests.size(); ++ k) {
	const hypothesis_type& oracle = oracles[o];
	const hypothesis_type& kbest  = kbests[k];
	
	if (sentences.find(kbest.sentence) != sentences.end()) continue;
	
	hypothesis_type::feature_set_type::const_iterator oiter = oracle.features.begin();
	hypothesis_type::feature_set_type::const_iterator oiter_end = oracle.features.end();
	
	hypothesis_type::feature_set_type::const_iterator kiter = kbest.features.begin();
	hypothesis_type::feature_set_type::const_iterator kiter_end = kbest.features.end();
	
	feats.clear();
	
	while (oiter != oiter_end && kiter != kiter_end) {
	  if (oiter->first < kiter->first) {
	    feats.push_back(*oiter);
	    ++ oiter;
	  } else if (kiter->first < oiter->first) {
	    feats.push_back(feature_value_type(kiter->first, - kiter->second));
	    ++ kiter;
	  } else {
	    const double value = oiter->second - kiter->second;
	    if (value != 0.0)
	      feats.push_back(feature_value_type(kiter->first, value));
	    ++ oiter;
	    ++ kiter;
	  }
	}
	
	for (/**/; oiter != oiter_end; ++ oiter)
	  feats.push_back(*oiter);
	
	for (/**/; kiter != kiter_end; ++ kiter)
	  feats.push_back(feature_value_type(kiter->first, - kiter->second));
	
	if (feats.empty()) continue;
	
	const bool error_metric = oracle.score()->error_metric();
	const double loss = (oracle.score()->score() - kbest.score->score()) * (error_metric ? -1 : 1);
	// or, do we use simple loss == 1?
	
	labels.push_back(loss);
	features.push_back(feature_set_type(feats.begin(), feats.end()));
      }
  }
  
  void initialize(weight_set_type& weights)
  {
    
    
  }
  
  void finalize(weight_set_type& weights)
  {
    
  }
  
  double learn(weight_set_type& weights)
  {
    HMatrix<feature_map_type> H(features);

    if (features.empty()) return 0.0;
    
    alpha.clear();
    gradient.clear();
    skipped.clear();
    
    alpha.reserve(labels.size());
    gradient.reserve(labels.size());
    skipped.reserve(labels.size());
    
    alpha.resize(labels.size(), 0.0);
    gradient.resize(labels.size(), 0.0);
    skipped.resize(labels.size(), false);
    
    double objective_max = - std::numeric_limits<double>::infinity();
    double objective_min =   std::numeric_limits<double>::infinity();

    
    size_type num_instance = 0;
    for (size_t i = 0; i != labels.size(); ++ i) {
      gradient[i] = labels[i] - cicada::dot_product(features[i].begin(), features[i].end(), weights, 0.0);
      
      const bool skipping = (gradient[i] <= 0 || labels[i] <= 0);
      
      skipped[i] = skipping;
      num_instance += ! skipping;
      
      if (! skipping) {
	objective_max = std::max(objective_max, gradient[i]);
	objective_min = std::min(objective_min, gradient[i]);
      }
    }
    
    if (! num_instance) {
      features.clear();
      labels.clear();
    
      return 0.0;
    }

    const int model_size = labels.size();
    
    const dobule C = 1.0 / (lambda * num_instance);
    
    double alpha_neq = C;
    
    // compute initial primal, dual
    double obj_primal = 0.0;
    double obj_dual   = 0.0;
    for (int k = 0; k != model_size; ++ k) 
      if (! skipped[k]) {
	obj_primal += gradient[k] * C;
	obj_dual   += gradient[k] * alpha[k];
      }
    
    if (debug >= 2)
      std::cerr << "initial primal: " << obj_primal << " dual: " << obj_dual << std::endl;

    bool perform_update = false;
    for (int iter = 0; iter != 100; ++ iter) {
      bool perform_update_local = false;
      
      int u = -1;
      double max_obj = - std::numeric_limits<double>::infinity();
      double delta = 0.0;
      
      for (int k = 0; k != model_size; ++ k) 
	if (! skipped[k]) {
	  delta -= alpha[k] * gradient[k];
	  
	  if (gradient[k] > max_obj) {
	    max_obj = gradient[k];
	    u = k;
	  }
	}
      
      if (gradient[u] < 0.0)
	u = -1;
      else
	delta += C * gradient[u];
      
      // quit if delta is below tolerance...
      if (delta <= tolerance) break;
      
      if (u >= 0) {
	int v = -1;
	double max_improvement = - std::numeric_limits<double>::infinity();
	double tau = 1.0;
	
	for (int k = 0; k != model_size; ++ k) 
	  if (! skipped[k] && k != u && alpha[k] > 0.0) {
	    const double numer = alpha[k] * (gradient[u] - gradient[k]);
	    const double denom = alpha[k] * alpha[k] * (H(u, u) - 2.0 * H(u, k) + H(k, k));
	    
	    if (denom > 0.0) {
	      const double improvement = (numer < denom ? (numer * numer) / denom : numer - 0.5 * denom);
	      
	      if (improvement > max_improvement) {
		max_improvement = improvement;
		tau = std::max(0.0, std::min(1.0, numer / denom));
		v = k;
	      }
	    }
	  }
	
	if (alpha_neq > 0.0) {
	  const double numer = alpha_neq * gradient[u];
	  const double denom = alpha_neq * alpha_neq * H(u, u);
	  
	  if (denom > 0.0) {
	    const double improvement = (numer < denom ? (numer * numer) / denom : numer - 0.5 * denom);
	    
	    if (improvement > max_improvement) {
	      max_improvement = improvement;
	      tau = std::max(0.0, std::min(1.0, numer / denom));
	      v = -1;
	    }
	  }
	}
	
	if (v >= 0) {
	  // maximize objective, u and v
	  double update = alpha[v] * tau;
	  if (alpha[u] + update < 0.0)
	    update = - alpha[u];
	  
	  if (update != 0.0) {
	    perform_update_local = true;
	    
	    alpha[u] += update;
	    alpha[v] -= update;
	    for (int i = 0; i != model_size; ++ i)
	      if (! skipped[i])
		gradient[i] += update * (H(i, v) - H(i, u));
	  }
	} else {
	  // update via alpha_neq
	  double update = alpha_neq * tau;
	  if (alpha[u] + update < 0.0)
	    update = - alpha[u];
	  
	  if (update != 0.0) {
	    perform_update_local = true;
	    
	    alpha[u] += update;
	    alpha_neq -= update;
	    for (size_t i = 0; i != model_size; ++ i)
	      if (! skipped[i])
		gradient[i] -= update * H(i, u);
	  }
	}
      } else {
	int v = -1;
	double max_improvement = - std::numeric_limits<double>::infinity();
	double tau = 1.0;
	
	for (int k = 0; k != model_size; ++ k) 
	  if (! skipped[k] && alpha[k] > 0.0) {
	    const double numer = alpha[k] * gradient[k];
	    const double denom = alpha[k] * alpha[k] * H(k, k);
	    
	    if (denom > 0.0) {
	      const double improvement = (numer < denom ? (numer * numer) / denom : numer - 0.5 * denom);
	      
	      if (improvement > max_improvement) {
		max_improvement = improvement;
		tau = std::max(0.0, std::min(1.0, numer / denom));
		v = k;
	      }
	    }
	  }
	
	if (v >= 0) {
	  double update = alpha[v] * tau;
	  if (alpha_neq + update < 0.0)
	    update = - alpha_neq;
	  
	  if (update != 0.0) {
	    perform_update_local = true;
	    
	    alpha_neq += update;
	    alpha[v] -= update;
	    for (int i = 0; i != model_size; ++ i)
	      if (! skipped[i])
		gradient[i] += update * H(i, v);
	  }
	}
      }

      perform_update |= perform_update_local;
      
      if (! perform_update_local) break;

      // compute primal, dual
      obj_primal = 0.0;
      obj_dual   = 0.0;
      for (int k = 0; k != model_size; ++ k) 
	if (! skipped[k]) {
	  obj_primal += gradient[k] * C;
	  obj_dual   += gradient[k] * alpha[k];
	}
      
      // global tolerance
      if (obj_primal - obj_dual <= tolerance * num_instance)
	break;
    }

    if (debug >= 2)
      std::cerr << "final primal: " << obj_primal << " dual: " << obj_dual << std::endl;

    if (! perform_update) {
      features.clear();
      labels.clear();
    
      return 0.0;
    }
    
    for (int k = 0; k = model_size; ++ k)
      if (! skipped[i] && alpha[k] > 0.0) {
	// update: weights[fiter->first] += alpha[k] * fiter->second;
	
      }

    
    features.clear();
    labels.clear();
    
    return 0.0;
  }
  
  doble tolerance;
  double labmda;

  feature_map_type features;
  label_map_type   labels;
  
  sentence_unique_type sentences;
  
  alpha_type    alpha;
  gradient_type gradient;
  skipped_type  skipped;
  pos_map_type  pos_map;
};

// logistic regression base...
struct LearnLR
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  typedef hypothesis_type::feature_set_type feature_set_type;
  typedef hypothesis_type::feature_value_type feature_value_type;
  typedef std::vector<feature_set_type, std::allocator<feature_set_type> > sample_type;
  
  typedef cicada::semiring::Log<double> weight_type;
  
  struct sample_pair_type
  {
    sample_pair_type() : kbests(), oracles() {}
    
    sample_type kbests;
    sample_type oracles;
    
    template <typename Expectations>
    double encode(const weight_set_type& weights, Expectations& expectations) const
    {
      weight_type Z_oracle;
      weight_type Z_kbest; 
      
      sample_type::const_iterator oiter_end = oracles.end();
      for (sample_type::const_iterator oiter = oracles.begin(); oiter != oiter_end; ++ oiter)
	Z_oracle += cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, oiter->begin(), oiter->end(), 0.0));
      
      sample_type::const_iterator kiter_end = kbests.end();
      for (sample_type::const_iterator kiter = kbests.begin(); kiter != kiter_end; ++ kiter)
	Z_kbest += cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, kiter->begin(), kiter->end(), 0.0));
      
      for (sample_type::const_iterator oiter = oracles.begin(); oiter != oiter_end; ++ oiter) {
	const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, oiter->begin(), oiter->end(), 0.0)) / Z_oracle;
	
	hypothesis_type::feature_set_type::const_iterator fiter_end = oiter->end();
	for (hypothesis_type::feature_set_type::const_iterator fiter = oiter->begin(); fiter != fiter_end; ++ fiter)
	  expectations[fiter->first] -= weight_type(fiter->second) * weight;
      }
      
      for (sample_type::const_iterator kiter = kbests.begin(); kiter != kiter_end; ++ kiter) {
	const weight_type weight = cicada::semiring::traits<weight_type>::exp(cicada::dot_product(weights, kiter->begin(), kiter->end(), 0.0)) / Z_kbest;
	
	hypothesis_type::feature_set_type::const_iterator fiter_end = kiter->end();
	for (hypothesis_type::feature_set_type::const_iterator fiter = kiter->begin(); fiter != fiter_end; ++ fiter)
	  expectations[fiter->first] += weight_type(fiter->second) * weight;
      }
      
      return log(Z_oracle) - log(Z_kbest);
    }
  };
};

// SGDL1 learner
struct LearnSGDL1 : public LearnLR
{
  typedef utils::chunk_vector<sample_pair_type, 4096 / sizeof(sample_pair_type), std::allocator<sample_pair_type> > sample_pair_set_type;
  
  typedef cicada::WeightVector<double> penalty_set_type;
  
  // maximize...
  // L_w =  \sum \log p(y | x) - C |w|
  // 
  
  LearnSGDL1() : epoch(0), lambda(C), penalties(), penalty(0.0) {}
  
  void clear()
  {
    samples.clear();
  }
  
  std::ostream& encode(std::ostream& os)
  {
    return os;
  }
  
  std::istream& decode(std::istream& is)
  {
    return is;
  }
  
  void encode(const size_type id, const hypothesis_set_type& kbests, const hypothesis_set_type& oracles, const bool merge=false)
  {
    if (kbests.empty() || oracles.empty()) return;
    
    samples.push_back(sample_pair_type());
    
    samples.back().kbests.reserve(kbests.size());
    samples.back().oracles.reserve(oracles.size());
    
    hypothesis_set_type::const_iterator kiter_end = kbests.end();
    for (hypothesis_set_type::const_iterator kiter = kbests.begin(); kiter != kiter_end; ++ kiter)
      samples.back().kbests.push_back(kiter->features);
    
    hypothesis_set_type::const_iterator oiter_end = oracles.end();
    for (hypothesis_set_type::const_iterator oiter = oracles.begin(); oiter != oiter_end; ++ oiter)
      samples.back().oracles.push_back(oiter->features);
  }
  
  void initialize(weight_set_type& weights)
  {

  }

  void finalize(weight_set_type& weights)
  {
    
  }

  double learn(weight_set_type& weights)
  {
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > expectation_type;

    if (samples.empty()) return 0.0;
    
    //
    // C = lambda * N
    //
    
    const size_type k = samples.size();
    const double k_norm = 1.0 / k;
    const double eta = 1.0 / (lambda * (epoch + 2)); // this is an eta from pegasos
    ++ epoch;
    
    penalty += eta * lambda * k_norm;
    
    expectation_type expectations;
    
    double objective = 0.0;
    sample_pair_set_type::const_iterator siter_end = samples.end();
    for (sample_pair_set_type::const_iterator siter = samples.begin(); siter != siter_end; ++ siter)
      objective += siter->encode(weights, expectations);
    
    // update by expectations...
    expectation_type::const_iterator eiter_end = expectations.end();
    for (expectation_type::const_iterator eiter = expectations.begin(); eiter != eiter_end; ++ eiter) {
      double& x = weights[eiter->first];
      
      // update weight ... we will update "minus" value
      x += - static_cast<double>(eiter->second) * eta * k_norm;
      
      // apply penalty
      apply(x, penalties[eiter->first], penalty);
    }
    
    samples.clear();
    
    return objective;
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
  
  sample_pair_set_type samples;
  
  size_type epoch;
  double    lambda;
  
  penalty_set_type penalties;
  double penalty;
};

// SGDL2 learner
struct LearnSGDL2 : public LearnLR
{
  typedef utils::chunk_vector<sample_pair_type, 4096 / sizeof(sample_pair_type), std::allocator<sample_pair_type> > sample_pair_set_type;
    
  LearnSGDL2() : epoch(0), lambda(C), weight_scale(1.0), weight_norm(0.0) {}
  
  void clear()
  {
    samples.clear();
  }
  
  std::ostream& encode(std::ostream& os)
  {
    return os;
  }
  
  std::istream& decode(std::istream& is)
  {
    return is;
  }
  
  void encode(const size_type id, const hypothesis_set_type& kbests, const hypothesis_set_type& oracles, const bool merge=false)
  {
    if (kbests.empty() || oracles.empty()) return;
    
    samples.push_back(sample_pair_type());
    
    samples.back().kbests.reserve(kbests.size());
    samples.back().oracles.reserve(oracles.size());
    
    hypothesis_set_type::const_iterator kiter_end = kbests.end();
    for (hypothesis_set_type::const_iterator kiter = kbests.begin(); kiter != kiter_end; ++ kiter)
      samples.back().kbests.push_back(kiter->features);
    
    hypothesis_set_type::const_iterator oiter_end = oracles.end();
    for (hypothesis_set_type::const_iterator oiter = oracles.begin(); oiter != oiter_end; ++ oiter)
      samples.back().oracles.push_back(oiter->features);
  }
  
  void initialize(weight_set_type& weights)
  {
    weight_scale = 1.0;
    weight_norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
  }
  
  void finalize(weight_set_type& weights)
  {
    weights *= weight_scale;
    
    weight_scale = 1.0;
    weight_norm = std::inner_product(weights.begin(), weights.end(), weights.begin(), 0.0);
  }

  double learn(weight_set_type& weights)
  {
    typedef cicada::FeatureVector<weight_type, std::allocator<weight_type> > expectation_type;
    
    if (samples.empty()) return 0.0;
    
    const size_type k = samples.size();
    const double k_norm = 1.0 / k;
    const double eta = 1.0 / (lambda * (epoch + 2));  // this is an eta from pegasos
    ++ epoch;
    
    rescale(weights, 1.0 - eta * lambda);
    
    expectation_type expectations;
    
    // update... by eta / k
    double objective = 0.0;
    sample_pair_set_type::const_iterator siter_end = samples.end();
    for (sample_pair_set_type::const_iterator siter = samples.begin(); siter != siter_end; ++ siter)
      objective += siter->encode(weights, expectations);
    
    // update by expectations...
    expectation_type::const_iterator eiter_end = expectations.end();
    for (expectation_type::const_iterator eiter = expectations.begin(); eiter != eiter_end; ++ eiter) {
      // we will update "minus" value...
      
      double& x = weights[eiter->first];
      const double alpha = - static_cast<double>(eiter->second) * eta * k_norm;
      
      weight_norm += 2.0 * x * alpha * weight_scale + alpha * alpha;
      x += alpha / weight_scale;
    }
    
    if (weight_norm > 1.0 / lambda)
      rescale(weights, std::sqrt(1.0 / (lambda * weight_norm)));
    
    if (weight_scale < 0.01 || 100 < weight_scale)
      finalize(weights);
    
    // clear current training events..
    samples.clear();
    
    return objective;
  }

  void rescale(weight_set_type& weights, const double scaling)
  {
    weight_norm *= scaling * scaling;
    if (scaling != 0.0)
      weight_scale *= scaling;
    else {
      weight_scale = 1.0;
      std::fill(weights.begin(), weights.end(), 0.0);
    }
  }
  
  sample_pair_set_type samples;
  
  size_type epoch;
  double    lambda;
  
  double weight_scale;
  double weight_norm;
};

// LBFGS learner
struct LearnLBFGS : public LearnLR
{
  typedef std::vector<sample_pair_type, std::allocator<sample_pair_type> > sample_pair_set_type;
  typedef utils::chunk_vector<sample_pair_set_type, 4096 / sizeof(sample_pair_set_type), std::allocator<sample_pair_set_type> > sample_pair_map_type;
  
  typedef cicada::WeightVector<weight_type, std::allocator<weight_type> > expectation_type;
  
  void clear()
  {
    samples_other.clear();
  }
  
  std::ostream& encode(std::ostream& os)
  {
    for (size_t id = 0; id != samples.size(); ++ id) 
      if (! samples[id].empty()) {
	sample_pair_set_type::const_iterator siter_end = samples[id].end();
	for (sample_pair_set_type::const_iterator siter = samples[id].begin(); siter != siter_end; ++ siter) {
	  const sample_pair_type& sample = *siter;
	  
	  if (sample.oracles.empty() || sample.kbests.empty()) continue;
	  
	  sample_type::const_iterator oiter_end = sample.oracles.end();
	  for (sample_type::const_iterator oiter = sample.oracles.begin(); oiter != oiter_end; ++ oiter) {
	    os << "oracle:";
	    
	    hypothesis_type::feature_set_type::const_iterator fiter_end = oiter->end();
	    for (hypothesis_type::feature_set_type::const_iterator fiter = oiter->begin(); fiter != fiter_end; ++ fiter) {
	      os << ' ' << fiter->first << ' ';
	      utils::encode_base64(fiter->second, std::ostream_iterator<char>(os));
	    }
	    os << '\n';
	  }
	  
	  sample_type::const_iterator kiter_end = sample.kbests.end();
	  for (sample_type::const_iterator kiter = sample.kbests.begin(); kiter != kiter_end; ++ kiter) {
	    os << "kbest:";
	    
	    hypothesis_type::feature_set_type::const_iterator fiter_end = kiter->end();
	    for (hypothesis_type::feature_set_type::const_iterator fiter = kiter->begin(); fiter != fiter_end; ++ fiter) {
	      os << ' ' << fiter->first << ' ';
	      utils::encode_base64(fiter->second, std::ostream_iterator<char>(os));
	    }
	    os << '\n';
	  }
	}
      }
    
    return os;
  }

  std::istream& decode(std::istream& is)
  {
    typedef cicada::Feature feature_type;
    typedef std::pair<feature_type, double> feature_value_type;
    typedef std::vector<feature_value_type, std::allocator<feature_value_type> > feature_set_type;
    typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
    
    std::string mode = "kbest:";

    sample_type* psample;
    
    std::string line;
    feature_set_type features;
    while (std::getline(is, line)) {
      features.clear();
      
      const utils::piece line_piece(line);
      tokenizer_type tokenizer(line_piece);
      
      tokenizer_type::iterator iter     = tokenizer.begin();
      tokenizer_type::iterator iter_end = tokenizer.end();
      
      if (iter == iter_end) continue;
      
      const utils::piece mode_curr = *iter;
      ++ iter;
      
      if (iter == iter_end) continue;
      
      if (mode_curr != mode) {
	if (mode_curr == "oracle:") {
	  samples_other.push_back(sample_pair_type());
	  psample = &(samples_other.back().oracles);
	} else
	  psample = &(samples_other.back().kbests);
	
	mode = mode_curr;
      }
      
      while (iter != iter_end) {
	const utils::piece feature = *iter;
	++ iter;
	
	if (iter == iter_end) break;
	
	const utils::piece value = *iter;
	++ iter;
	
	features.push_back(feature_value_type(feature, utils::decode_base64<double>(value)));
      }
      
      if (features.empty()) continue;
      
      psample->push_back(hypothesis_type::feature_set_type(features.begin(), features.end()));
    }
    
    return is;
  }
  
  void encode(const size_type id, const hypothesis_set_type& kbests, const hypothesis_set_type& oracles, const bool merge=false)
  {
    if (kbests.empty() || oracles.empty()) return;
    
    if (id >= samples.size())
      samples.resize(id + 1);
    
    if (! merge)
      samples[id].clear();
    
    samples[id].push_back(sample_pair_type());
    
    samples[id].back().kbests.reserve(kbests.size());
    samples[id].back().oracles.reserve(oracles.size());
    
    hypothesis_set_type::const_iterator kiter_end = kbests.end();
    for (hypothesis_set_type::const_iterator kiter = kbests.begin(); kiter != kiter_end; ++ kiter)
      samples[id].back().kbests.push_back(kiter->features);
    
    hypothesis_set_type::const_iterator oiter_end = oracles.end();
    for (hypothesis_set_type::const_iterator oiter = oracles.begin(); oiter != oiter_end; ++ oiter)
      samples[id].back().oracles.push_back(oiter->features);
  }
  
  void initialize(weight_set_type& weights)
  {

  }

  void finalize(weight_set_type& weights)
  {
    
  }

  double learn(weight_set_type& __weights)
  {
    lbfgs_parameter_t param;
    lbfgs_parameter_init(&param);
    
    if (regularize_l1) {
      param.orthantwise_c = C;
      param.linesearch = LBFGS_LINESEARCH_BACKTRACKING;
    } else
      param.orthantwise_c = 0.0;
    
    double objective = 0.0;
    
    weights = __weights;
    weights.allocate();
    
    lbfgs(weights.size(), &(*weights.begin()), &objective, LearnLBFGS::evaluate, 0, this, &param);
    
    __weights = weights;
    
    return objective;
  }
  
  static lbfgsfloatval_t evaluate(void *instance,
				  const lbfgsfloatval_t *x,
				  lbfgsfloatval_t *g,
				  const int n,
				  const lbfgsfloatval_t step)
  {
    LearnLBFGS& optimizer = *((LearnLBFGS*) instance);
    
    expectation_type& expectations            = optimizer.expectations;
    weight_set_type& weights                  = optimizer.weights;
    const sample_pair_map_type& samples       = optimizer.samples;
    const sample_pair_set_type& samples_other = optimizer.samples_other;
    
    expectations.clear();
    expectations.allocate();

    double objective = 0.0;
    size_t instances = 0;

    for (size_t i = 0; i != samples_other.size(); ++ i) {
      const sample_pair_type& sample = samples_other[i];
      
      const double margin = sample.encode(weights, expectations);
      objective -= margin;
      ++ instances;
    }
    
    for (size_t id = 0; id != samples.size(); ++ id) 
      if (! samples[id].empty()) {
	sample_pair_set_type::const_iterator siter_end = samples[id].end();
	for (sample_pair_set_type::const_iterator siter = samples[id].begin(); siter != siter_end; ++ siter) {
	  const sample_pair_type& sample = *siter;
	  
	  const double margin = sample.encode(weights, expectations);
	  objective -= margin;
	  ++ instances;
	}
      }
    
    std::copy(expectations.begin(), expectations.begin() + n, g);
    
    objective /= instances;
    std::transform(g, g + n, g, std::bind2nd(std::multiplies<double>(), 1.0 / instances));
    
    // L2...
    if (regularize_l2) {
      double norm = 0.0;
      for (int i = 0; i < n; ++ i) {
	g[i] += C * x[i];
	norm += x[i] * x[i];
      }
      objective += 0.5 * C * norm;
    }
    
    return objective;
  }
  
  expectation_type     expectations;
  weight_set_type      weights;
  sample_pair_map_type samples;
  sample_pair_set_type samples_other;
};

// linear learner
struct LearnLinear
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef struct model        model_type;
  typedef struct parameter    parameter_type;
  typedef struct problem      problem_type;
  typedef struct feature_node feature_node_type;
  
  typedef size_t offset_type;
  
  typedef std::vector<feature_node_type*, std::allocator<feature_node_type*> > feature_node_map_type;
  typedef std::vector<int, std::allocator<int> > label_set_type;

  //
  // typedef for unique sentences
  //
  struct hash_sentence : public utils::hashmurmur<size_t>
  {
    typedef utils::hashmurmur<size_t> hasher_type;

    size_t operator()(const hypothesis_type::sentence_type& x) const
    {
      return hasher_type()(x.begin(), x.end(), 0);
    }
  };
#ifdef HAVE_TR1_UNORDERED_SET
  typedef std::tr1::unordered_set<hypothesis_type::sentence_type, hash_sentence, std::equal_to<hypothesis_type::sentence_type>, std::allocator<hypothesis_type::sentence_type> > sentence_unique_type;
#else
  typedef sgi::hash_set<hypothesis_type::sentence_type, hash_sentence, std::equal_to<hypothesis_type::sentence_type>, std::allocator<hypothesis_type::sentence_type> > sentence_unique_type;
#endif
  
  static void print_string_stderr(const char *s)
  {
    std::cerr << s << std::flush;
  }

  static void print_string_none(const char *s)
  {
    
  }

  struct Encoder
  {
    typedef std::vector<offset_type, std::allocator<offset_type> > offset_set_type;
    typedef std::vector<feature_node_type, std::allocator<feature_node_type> > feature_node_set_type;

    Encoder() : offsets(), features() {}

    offset_set_type       offsets;
    feature_node_set_type features;
    
    template <typename Iterator>
    void encode(Iterator first, Iterator last)
    {
      feature_node_type feature;
      
      if (first == last) return;
      
      offsets.push_back(features.size());
      
      for (/**/; first != last; ++ first) {
	feature.index = first->first.id() + 1;
	feature.value = first->second;
	
	features.push_back(feature);
      }

      if (offsets.back() == features.size())
	offsets.pop_back();
      else {
	// termination...
	feature.index = -1;
	feature.value = 0.0;
	features.push_back(feature);
      }
    }

    void encode(const hypothesis_set_type& kbests,
		const hypothesis_set_type& oracles)
    {
      feature_node_type feature;
      
      sentence_unique_type sentences;
      for (size_t o = 0; o != oracles.size(); ++ o)
	sentences.insert(oracles[o].sentence);
      
      for (size_t o = 0; o != oracles.size(); ++ o)
	for (size_t k = 0; k != kbests.size(); ++ k) {
	  const hypothesis_type& oracle = oracles[o];
	  const hypothesis_type& kbest  = kbests[k];
	  
	  if (sentences.find(kbest.sentence) != sentences.end()) continue;
	  
	  offsets.push_back(features.size());
	  
	  hypothesis_type::feature_set_type::const_iterator oiter = oracle.features.begin();
	  hypothesis_type::feature_set_type::const_iterator oiter_end = oracle.features.end();
	
	  hypothesis_type::feature_set_type::const_iterator kiter = kbest.features.begin();
	  hypothesis_type::feature_set_type::const_iterator kiter_end = kbest.features.end();
	
	  while (oiter != oiter_end && kiter != kiter_end) {
	    if (oiter->first < kiter->first) {
	      feature.index = oiter->first.id() + 1;
	      feature.value = oiter->second;
	      features.push_back(feature);
	      ++ oiter;
	    } else if (kiter->first < oiter->first) {
	      feature.index = kiter->first.id() + 1;
	      feature.value = - kiter->second;
	      features.push_back(feature);
	      ++ kiter;
	    } else {
	      feature.index = oiter->first.id() + 1;
	      feature.value = oiter->second - kiter->second;
	      if (feature.value != 0.0)
		features.push_back(feature);
	      ++ oiter;
	      ++ kiter;
	    }
	  }
	    
	  for (/**/; oiter != oiter_end; ++ oiter) {
	    feature.index = oiter->first.id() + 1;
	    feature.value = oiter->second;
	    features.push_back(feature);
	  }
	  
	  for (/**/; kiter != kiter_end; ++ kiter) {
	    feature.index = kiter->first.id() + 1;
	    feature.value = - kiter->second;
	    features.push_back(feature);
	  }
	  
	  if (offsets.back() == features.size())
	    offsets.pop_back();
	  else {
	    // termination...
	    feature.index = -1;
	    feature.value = 0.0;
	    features.push_back(feature);
	  }
	}

      shrink();
    }
    
    void clear()
    {
      offsets.clear();
      features.clear();
    }
    
    void shrink()
    {
      offset_set_type(offsets).swap(offsets);
      feature_node_set_type(features).swap(features);
    }
  };
  typedef Encoder encoder_type;
  typedef utils::chunk_vector<encoder_type, 4096 / sizeof(encoder_type), std::allocator<encoder_type> > encoder_set_type; 
  
  void encode(const size_type id, const hypothesis_set_type& kbests, const hypothesis_set_type& oracles, const bool merge=false)
  {
    if (id >= encoders.size())
      encoders.resize(id + 1);
    
    if (! merge)
      encoders[id].clear();
    
    encoders[id].encode(kbests, oracles);
  }
  
  void clear()
  {
    encoder_other.clear();
  }
  
  std::ostream& encode(std::ostream& os)
  {
    for (size_type id = 0; id != encoders.size(); ++ id)
      for (size_type pos = 0; pos != encoders[id].offsets.size(); ++ pos) {
	encoder_type::feature_node_set_type::const_iterator fiter     = encoders[id].features.begin() + encoders[id].offsets[pos];
	encoder_type::feature_node_set_type::const_iterator fiter_end = (pos + 1 < encoders[id].offsets.size()
							   ? encoders[id].features.begin() + encoders[id].offsets[pos + 1] - 1
							   : encoders[id].features.end() - 1);
	for (/**/; fiter != fiter_end; ++ fiter) {
	  os << weight_set_type::feature_type(fiter->index - 1) << ' ';
	  utils::encode_base64(fiter->value, std::ostream_iterator<char>(os));
	  os << ' ';
	}
	os << '\n';
      }
    return os;
  }
  
  std::istream& decode(std::istream& is)
  {
    typedef cicada::Feature feature_type;
    typedef std::pair<feature_type, double> feature_value_type;
    typedef std::vector<feature_value_type, std::allocator<feature_value_type> > feature_set_type;
    typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;

    std::string line;
    feature_set_type features;
    while (std::getline(is, line)) {
      features.clear();
      
      const utils::piece line_piece(line);
      tokenizer_type tokenizer(line_piece);
      
      tokenizer_type::iterator iter     = tokenizer.begin();
      tokenizer_type::iterator iter_end = tokenizer.end();
      
      while (iter != iter_end) {
	const utils::piece feature = *iter;
	++ iter;
	
	if (iter == iter_end) break;
	
	const utils::piece value = *iter;
	++ iter;
	
	features.push_back(feature_value_type(feature, utils::decode_base64<double>(value)));
      }

      if (features.empty()) continue;
      
      std::sort(features.begin(), features.end());
      encoder_other.encode(features.begin(), features.end());
    }
    return is;
  }
  
  void initialize(weight_set_type& weights)
  {

  }

  void finalize(weight_set_type& weights)
  {
    
  }

  double learn(weight_set_type& weights)
  {
    size_type data_size = encoder_other.offsets.size();
    for (size_type id = 0; id != encoders.size(); ++ id)
      data_size += encoders[id].offsets.size();
    
    label_set_type        labels(data_size, 1);
    feature_node_map_type features;
    features.reserve(data_size);

    for (size_type pos = 0; pos != encoder_other.offsets.size(); ++ pos)
      features.push_back(const_cast<feature_node_type*>(&(*encoder_other.features.begin())) + encoder_other.offsets[pos]);
    
    for (size_type id = 0; id != encoders.size(); ++ id)
      for (size_type pos = 0; pos != encoders[id].offsets.size(); ++ pos)
	features.push_back(const_cast<feature_node_type*>(&(*encoders[id].features.begin())) + encoders[id].offsets[pos]);
    
    problem_type problem;
    problem.l = labels.size();
    problem.n = feature_type::allocated();
    problem.y = &(*labels.begin());
    problem.x = &(*features.begin());
    problem.bias = -1;
    
    parameter_type parameter;
    parameter.solver_type = linear_solver;
    parameter.eps = eps;
    parameter.C = 1.0 / (C * labels.size()); // renormalize!
    parameter.nr_weight    = 0;
    parameter.weight_label = 0;
    parameter.weight       = 0;
    
    if (parameter.eps == std::numeric_limits<double>::infinity()) {
      if (parameter.solver_type == L2R_LR || parameter.solver_type == L2R_L2LOSS_SVC)
	parameter.eps = 0.01;
      else if (parameter.solver_type == L2R_L2LOSS_SVC_DUAL || parameter.solver_type == L2R_L1LOSS_SVC_DUAL || parameter.solver_type == MCSVM_CS || parameter.solver_type == L2R_LR_DUAL)
	parameter.eps = 0.1;
      else if (parameter.solver_type == L1R_L2LOSS_SVC || parameter.solver_type == L1R_LR)
	parameter.eps = 0.01;
    }
    
    if (debug >= 2)
      set_print_string_function(print_string_stderr);
    else
      set_print_string_function(print_string_none);
    
    const char* error_message = check_parameter(&problem, &parameter);
    if (error_message)
      throw std::runtime_error(std::string("error: ") + error_message);
    
    static const char* names[] = {"L2R_LR", "L2R_L2LOSS_SVC_DUAL", "L2R_L2LOSS_SVC", "L2R_L1LOSS_SVC_DUAL", "MCSVM_CS",
				  "L1R_L2LOSS_SVC", "L1R_LR", "L2R_LR_DUAL"};
    
    if (debug)
      std::cerr << "solver: " << names[parameter.solver_type] << std::endl;
    
    const model_type* model = train(&problem, &parameter);
    
    const double objective = model->objective * C;
    
    // it is an optimization...
    weights.clear();
    for (int j = 0; j != model->nr_feature; ++ j)
      weights[weight_set_type::feature_type(j)] = model->w[j];
    
    free_and_destroy_model(const_cast<model_type**>(&model));
    
    return objective;
  }
  
  encoder_set_type encoders;
  encoder_type     encoder_other;
};


struct KBestSentence
{
  typedef cicada::semiring::Logprob<double>               weight_type;
  typedef cicada::operation::sentence_feature_traversal   traversal_type;
  typedef cicada::operation::weight_function<weight_type> function_type;
  typedef cicada::operation::kbest_sentence_filter_unique filter_type;
  
  typedef cicada::KBest<traversal_type, function_type, filter_type> derivation_set_type;

  typedef traversal_type::value_type derivation_type;
  
  void operator()(operation_set_type& operations, const std::string& input, const weight_set_type& weights, hypothesis_set_type& kbests)
  {    
    kbests.clear();
    
    if (input.empty()) return;
    
    // assign weights
    operations.assign(weights);
    
    // perform translation
    operations(input);
    
    // generate kbests...
    const hypergraph_type& graph = operations.get_data().hypergraph;
    
    derivation_set_type derivations(graph, kbest_size, traversal_type(), function_type(weights), filter_type(graph));
    
    derivation_type derivation;
    weight_type     weight;
    
    for (int k = 0; k != kbest_size && derivations(k, derivation, weight); ++ k)
      kbests.push_back(hypothesis_type(boost::get<0>(derivation).begin(), boost::get<0>(derivation).end(),
				       boost::get<1>(derivation).begin(), boost::get<1>(derivation).end()));
  }
};

struct KBestAlignment
{
  typedef cicada::semiring::Logprob<double>               weight_type;
  typedef cicada::operation::alignment_feature_traversal  traversal_type;
  typedef cicada::operation::weight_function<weight_type> function_type;
  typedef cicada::operation::kbest_alignment_filter_unique filter_type;
  
  typedef cicada::KBest<traversal_type, function_type, filter_type> derivation_set_type;

  typedef traversal_type::value_type derivation_type;
  
  void operator()(operation_set_type& operations, const std::string& input, const weight_set_type& weights, hypothesis_set_type& kbests)
  {    
    kbests.clear();
    
    if (input.empty()) return;
    
    // assign weights
    operations.assign(weights);
    
    // perform translation
    operations(input);
    
    // generate kbests...
    const hypergraph_type& graph = operations.get_data().hypergraph;
    
    derivation_set_type derivations(graph, kbest_size, traversal_type(), function_type(weights), filter_type(graph));
    
    sentence_type   sentence;
    derivation_type derivation;
    weight_type     weight;
    
    for (int k = 0; k != kbest_size && derivations(k, derivation, weight); ++ k) {
      std::ostringstream os;
      os << boost::get<0>(derivation);
      sentence.assign(os.str());
      
      kbests.push_back(hypothesis_type(sentence.begin(), sentence.end(),
				       boost::get<1>(derivation).begin(), boost::get<1>(derivation).end()));
    }
  }
};

struct KBestDependency
{
  typedef cicada::semiring::Logprob<double>               weight_type;
  typedef cicada::operation::dependency_feature_traversal traversal_type;
  typedef cicada::operation::weight_function<weight_type> function_type;
  typedef cicada::operation::kbest_dependency_filter_unique filter_type;
  
  typedef cicada::KBest<traversal_type, function_type, filter_type> derivation_set_type;

  typedef traversal_type::value_type derivation_type;
  
  void operator()(operation_set_type& operations, const std::string& input, const weight_set_type& weights, hypothesis_set_type& kbests)
  {    
    kbests.clear();
    
    if (input.empty()) return;
    
    // assign weights
    operations.assign(weights);
    
    // perform translation
    operations(input);
    
    // generate kbests...
    const hypergraph_type& graph = operations.get_data().hypergraph;
    
    derivation_set_type derivations(graph, kbest_size, traversal_type(), function_type(weights), filter_type(graph));
    
    sentence_type   sentence;
    derivation_type derivation;
    weight_type     weight;
    
    for (int k = 0; k != kbest_size && derivations(k, derivation, weight); ++ k) {
      std::ostringstream os;
      os << boost::get<0>(derivation);
      sentence.assign(os.str());
      
      kbests.push_back(hypothesis_type(sentence.begin(), sentence.end(),
				       boost::get<1>(derivation).begin(), boost::get<1>(derivation).end()));
    }
  }
};


struct Oracle
{
  typedef std::vector<const hypothesis_type*, std::allocator<const hypothesis_type*> > oracle_set_type;
  typedef std::vector<oracle_set_type, std::allocator<oracle_set_type> > oracle_map_type;

  std::pair<score_ptr_type, score_ptr_type>
  operator()(const hypothesis_map_type& kbests, const scorer_document_type& scorers, hypothesis_map_type& oracles)
  {
    const bool error_metric = scorers.error_metric();
    const double score_factor = (error_metric ? - 1.0 : 1.0);

    score_ptr_type score_1best;
        
    score_ptr_type score_prev;
    score_ptr_type score_best;
    score_ptr_type score_curr;
    
    oracle_map_type oracles_prev(kbests.size());
    oracle_map_type oracles_best(kbests.size());
    oracle_map_type oracles_curr(kbests.size());
    
    // initialization...
    for (size_t id = 0; id != kbests.size(); ++ id) 
      if (! kbests[id].empty()) {
	hypothesis_set_type::const_iterator hiter_end = kbests[id].end();
	for (hypothesis_set_type::const_iterator hiter = kbests[id].begin(); hiter != hiter_end; ++ hiter) {
	  hypothesis_type& hyp = const_cast<hypothesis_type&>(*hiter);
	  
	  if (! hyp.score)
	    hyp.score = scorers[id]->score(sentence_type(hyp.sentence.begin(), hyp.sentence.end()));
	}
	
	if (! score_prev)
	  score_prev = kbests[id].front().score->clone();
	else
	  *score_prev += *(kbests[id].front().score);
	
	oracles_prev[id].push_back(&kbests[id].front());
      }
    
    if (score_prev)
      score_best = score_prev->clone();
    else
      throw std::runtime_error("no scores?");
    
    score_1best = score_best->clone();
    
    oracles_best = oracles_prev;
    
    double objective_prev = score_prev->score() * score_factor;
    double objective_best = objective_prev;
    double objective_curr = objective_prev;
    
    // 
    // 10 iteration will be fine
    //
    for (int i = 0; i < 10; ++ i) {
      score_curr     = score_prev->clone();
      objective_curr = objective_prev;
      oracles_curr   = oracles_prev;
      
      for (size_t id = 0; id != kbests.size(); ++ id) 
	if (! kbests[id].empty()) {
	  score_ptr_type score = score_curr->clone();
	  *score -= *(oracles_curr[id].front()->score);
	  
	  hypothesis_set_type::const_iterator hiter_end = kbests[id].end();
	  for (hypothesis_set_type::const_iterator hiter = kbests[id].begin(); hiter != hiter_end; ++ hiter) {
	    score_ptr_type score_sample = score->clone();
	    *score_sample += *(hiter->score);
	    
	    const double objective_sample = score_sample->score() * score_factor;
	    
	    if (objective_sample > objective_curr) {
	      oracles_curr[id].clear();
	      oracles_curr[id].push_back(&(*hiter));
	      
	      objective_curr = objective_sample;
	      score_curr     = score_sample;
	    } else if (objective_sample == objective_curr)
	      oracles_curr[id].push_back(&(*hiter));
	  }
	}
      
      if (objective_curr > objective_best) {
	score_best     = score_curr->clone();
	objective_best = objective_curr;
	oracles_best   = oracles_curr;
      }
      
      if (objective_curr <= objective_prev) break;
      
      score_prev     = score_curr->clone();
      objective_prev = objective_curr;
      oracles_prev   = oracles_curr;
    }
    
    oracles.clear();
    oracles.resize(kbests.size());
    for (size_t id = 0; id != kbests.size(); ++ id)
      for (size_t i = 0; i != oracles_best[id].size(); ++ i)
	oracles[id].push_back(*oracles_best[id][i]);

    return std::make_pair(score_1best, score_best);
  }
};


inline
void read_refset(const path_type& refset_path,
		 scorer_document_type& scorers,
		 const size_t shard_rank,
		 const size_t shard_size)
{
  typedef boost::spirit::istream_iterator iter_type;
  typedef cicada_sentence_parser<iter_type> parser_type;
  
  parser_type parser;
  id_sentence_type id_sentence;

  utils::compress_istream is(refset_path, 1024 * 1024);
  is.unsetf(std::ios::skipws);
  
  iter_type iter(is);
  iter_type iter_end;
  
  while (iter != iter_end) {
    id_sentence.second.clear();
    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, id_sentence))
      if (iter != iter_end)
	throw std::runtime_error("refset parsing failed");
    
    const size_t id = id_sentence.first;
    
    if (shard_size && (id % shard_size != shard_rank)) continue;
    
    const size_t id_rank = (shard_size == 0 ? id : id / shard_size);
    
    if (id_rank >= scorers.size())
      scorers.resize(id_rank + 1);
    
    if (! scorers[id_rank])
      scorers[id_rank] = scorers.create();
    
    scorers[id_rank]->insert(id_sentence.second);
  }
}

inline
void read_samples(const path_type& input_path,
		  sample_set_type& samples,
		  const bool directory_mode,
		  const bool id_mode,
		  const size_t shard_rank,
		  const size_t shard_size)
{
  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;

  if (directory_mode) {
    if (! boost::filesystem::is_directory(input_path))
      throw std::runtime_error("input is not directory! " + input_path.string());

    boost::spirit::qi::uint_parser<size_t, 10, 1, -1> id_parser;
    
    size_t id;
    std::string line;
    for (size_t i = 0; /**/; ++ i)
      if (shard_size <= 0 || i % shard_size == shard_rank) {
	const path_type path = input_path / (utils::lexical_cast<std::string>(i) + ".gz");
	
	if (! boost::filesystem::exists(path)) break;
	
	utils::compress_istream is(path, 1024 * 1024);
	std::getline(is, line);
	
	if (line.empty()) continue;
	
	std::string::const_iterator iter     = line.begin();
	std::string::const_iterator iter_end = line.end();
	
	if (! qi::phrase_parse(iter, iter_end, id_parser >> "|||", standard::blank, id))
	  throw std::runtime_error("id prefixed input format error");
	
	if (id != i)
	  throw std::runtime_error("id doest not match!");
	
	const size_t id_rank = (shard_size == 0 ? id : id / shard_size);
	
	if (id_rank >= samples.size())
	  samples.resize(id_rank + 1);
	
	samples[id_rank] = line;
      }
  } else if (id_mode) {
    utils::compress_istream is(input_path, 1024 * 1024);
    
    boost::spirit::qi::uint_parser<size_t, 10, 1, -1> id_parser;
    
    size_t id;
    std::string line;
    while (std::getline(is, line)) 
      if (! line.empty()) {
	std::string::const_iterator iter     = line.begin();
	std::string::const_iterator iter_end = line.end();
	
	if (! qi::phrase_parse(iter, iter_end, id_parser >> "|||", standard::blank, id))
	  throw std::runtime_error("id prefixed input format error");
	
	if (shard_size == 0 || id % shard_size == shard_rank) {
	  const size_t id_rank = (shard_size == 0 ? id : id / shard_size);
	  
	  if (id_rank >= samples.size())
	    samples.resize(id_rank + 1);
	  
	  samples[id_rank] = line;
	}
      }
  } else {
    utils::compress_istream is(input_path, 1024 * 1024);
    
    std::string line;
    for (size_t id = 0; std::getline(is, line); ++ id) 
      if (shard_size == 0 || id % shard_size == shard_rank) {
	if (! line.empty())
	  samples.push_back(utils::lexical_cast<std::string>(id) + " ||| " + line);
	else
	  samples.push_back(std::string());
      }
  }
}


inline
path_type add_suffix(const path_type& path, const std::string& suffix)
{
  bool has_suffix_gz  = false;
  bool has_suffix_bz2 = false;
  
  path_type path_added = path;
  
  if (path.extension() == ".gz") {
    path_added = path.parent_path() / path.stem();
    has_suffix_gz = true;
  } else if (path.extension() == ".bz2") {
    path_added = path.parent_path() / path.stem();
    has_suffix_bz2 = true;
  }
  
  path_added = path_added.string() + suffix;
  
  if (has_suffix_gz)
    path_added = path_added.string() + ".gz";
  else if (has_suffix_bz2)
    path_added = path_added.string() + ".bz2";
  
  return path_added;
}

#endif

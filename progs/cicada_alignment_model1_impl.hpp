//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA_ALIGNMENT_MODEL1_IMPL__HPP__
#define __CICADA_ALIGNMENT_MODEL1_IMPL__HPP__ 1

#include <set>

#include "cicada_alignment_impl.hpp"

#include "utils/vector2.hpp"
#include "utils/mathop.hpp"

#include "kuhn_munkres.hpp"
#include "itg_alignment.hpp"
#include "dependency_hybrid.hpp"
#include "dependency_degree2.hpp"
#include "dependency_mst.hpp"

struct LearnModel1 : public LearnBase
{
  LearnModel1(const LearnBase& __base)
    : LearnBase(__base) {}
  
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  
  typedef std::set<int, std::less<int>, std::allocator<int> > point_set_type;
  typedef std::vector<point_set_type, std::allocator<point_set_type> > point_map_type;
  
  void learn(const sentence_type& source,
	     const sentence_type& target,
	     const alignment_type& alignment,
	     const bool inverse,
	     const ttable_type& ttable,
	     ttable_type& counts,
	     aligned_type& aligned,
	     double& objective)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    double logsum = 0.0;
    
    probs.reserve(source_size + 1);
    probs.resize(source_size + 1);
    
    points.clear();
    points.reserve(target_size);
    points.resize(target_size);
    
    if (inverse) {
      alignment_type::const_iterator aiter_end = alignment.end();
      for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter)
	points[aiter->source].insert(aiter->target);
    } else {
      alignment_type::const_iterator aiter_end = alignment.end();
      for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter)
	points[aiter->target].insert(aiter->source);
    }
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      if (! points[trg].empty()) {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;
	
	point_set_type::const_iterator iter_end = points[trg].end();
	for (point_set_type::const_iterator iter = points[trg].begin(); iter != iter_end; ++ iter) {
	  const int src = *iter;
	  
	  probs[src] = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += probs[src];
	  
	  if (probs[src] > prob_max) {
	    prob_max = probs[src];
	    word_max = source[src];
	  }
	}
	
	logsum += utils::mathop::log(prob_sum);
	
	const double factor = 1.0 / prob_sum;
	for (point_set_type::const_iterator iter = points[trg].begin(); iter != iter_end; ++ iter)
	  counts[source[*iter]][target[trg]] += probs[*iter] * factor;
	
	aligned[word_max].insert(target[trg]);
	
      } else {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
      
	prob_set_type::iterator piter = probs.begin();
	*piter = ttable(vocab_type::EPSILON, target[trg]) * prob_null;
	prob_sum += *piter;
	
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;
	
	++ piter;
	
	for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	  *piter = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += *piter;
	  
	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = source[src];
	  }
	}
	
	logsum += utils::mathop::log(prob_sum);
	
	const double factor = 1.0 / prob_sum;
	piter = probs.begin();
	counts[vocab_type::EPSILON][target[trg]] += (*piter) * factor;
	++ piter;
	
	for (size_type src = 0; src != source_size; ++ src, ++ piter)
	  counts[source[src]][target[trg]] += (*piter) * factor;
	
	aligned[word_max].insert(target[trg]);
      }
    }
    
    objective += logsum / target_size;    
  }
  
  void learn(const sentence_type& source,
	     const sentence_type& target,
	     const ttable_type& ttable,
	     ttable_type& counts,
	     aligned_type& aligned,
	     double& objective)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    double logsum = 0.0;
    
    probs.reserve(source_size + 1);
    probs.resize(source_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      
      prob_set_type::iterator piter = probs.begin();
      *piter = ttable(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;
      
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
	
	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = source[src];
	}
      }
      
      logsum += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      piter = probs.begin();
      counts[vocab_type::EPSILON][target[trg]] += (*piter) * factor;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter)
	counts[source[src]][target[trg]] += (*piter) * factor;
      
      aligned[word_max].insert(target[trg]);
    }
    
    objective += logsum / target_size;
  }
  
  void operator()(const sentence_type& source, const sentence_type& target)
  {
    learn(source, target, ttable_source_target, ttable_counts_source_target, aligned_source_target, objective_source_target);
    learn(target, source, ttable_target_source, ttable_counts_target_source, aligned_target_source, objective_target_source);
  }

  void operator()(const sentence_type& source, const sentence_type& target, const alignment_type& alignment)
  {
    learn(source, target, alignment, false, ttable_source_target, ttable_counts_source_target, aligned_source_target, objective_source_target);
    learn(target, source, alignment, true,  ttable_target_source, ttable_counts_target_source, aligned_target_source, objective_target_source);
  }

  void shrink()
  {
    
  }
  
  prob_set_type  probs;
  point_map_type points;
};

struct LearnModel1Posterior : public LearnBase
{
  LearnModel1Posterior(const LearnBase& __base)
    : LearnBase(__base) {}  

  typedef utils::vector2<prob_type, std::allocator<prob_type> > posterior_set_type;
  typedef std::vector<prob_type, std::allocator<prob_type> > prob_set_type;

  typedef std::set<int, std::less<int>, std::allocator<int> > point_set_type;
  typedef std::vector<point_set_type, std::allocator<point_set_type> > point_map_type;
  
  void learn(const sentence_type& source,
	     const sentence_type& target,
	     const alignment_type& alignment,
	     const bool inverse,
	     const ttable_type& ttable,
	     ttable_type& counts,
	     aligned_type& aligned,
	     double& objective)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    double logsum = 0.0;
    
    posterior.clear();
    probs.clear();
    
    posterior.reserve(target_size + 1, source_size + 1);
    probs.reserve(target_size + 1, source_size + 1);
    
    posterior.resize(target_size + 1, source_size + 1, 0.0);
    probs.resize(target_size + 1, source_size + 1, 0.0);
    
    phi.clear();
    exp_phi.clear();
    
    phi.reserve(source_size + 1);
    exp_phi.reserve(source_size + 1);
    
    phi.resize(source_size + 1, 0.0);
    exp_phi.resize(source_size + 1, 1.0);
    
    points.clear();
    points.reserve(target_size);
    points.resize(target_size);

    if (inverse) {
      alignment_type::const_iterator aiter_end = alignment.end();
      for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter)
	points[aiter->source].insert(aiter->target);
    } else {
      alignment_type::const_iterator aiter_end = alignment.end();
      for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter)
	points[aiter->target].insert(aiter->source);
    }
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      if (! points[trg].empty()) {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;
	
	point_set_type::const_iterator iter_end = points[trg].end();
	for (point_set_type::const_iterator iter = points[trg].begin(); iter != iter_end; ++ iter) {
	  const int src = *iter;
	  
	  double& prob = probs(trg + 1, src + 1);
	  
	  prob = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += prob;
	  
	  if (prob > prob_max) {
	    prob_max = prob;
	    word_max = source[src];
	  }
	}
	
	logsum += utils::mathop::log(prob_sum);
	
	const double factor = 1.0 / prob_sum;
	posterior_set_type::const_iterator piter     = probs.begin(trg + 1);
	posterior_set_type::const_iterator piter_end = probs.end(trg + 1);
	posterior_set_type::iterator siter = posterior.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;
	
	aligned[word_max].insert(target[trg]);
      } else {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
      
	posterior_set_type::iterator piter     = probs.begin(trg + 1);
	posterior_set_type::iterator piter_end = probs.end(trg + 1);
	*piter = ttable(vocab_type::EPSILON, target[trg]) * prob_null;
	prob_sum += *piter;
      
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;
      
	++ piter;
      
	for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	  *piter = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += *piter;
	
	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = source[src];
	  }
	}
      
	logsum += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
	piter = probs.begin(trg + 1);
	posterior_set_type::iterator siter = posterior.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;
      
	aligned[word_max].insert(target[trg]);
      }
    }
    
    objective += logsum / target_size;
    
    for (int iter = 0; iter < 5; ++ iter) {
      // update phi.. but ignore NULL...
      
      bool updated = false;
      for (size_type src = 1; src <= source_size; ++ src) {
	double sum = 0.0;
	for (size_type trg = 1; trg <= target_size; ++ trg)
	  sum += posterior(trg, src);
	
	phi[src] += 1.0 - sum;
	if (phi[src] > 0.0)
	  phi[src] = 0.0;
	
	updated |= (phi[src] != 0.0);
	exp_phi[src] = utils::mathop::exp(phi[src]);
      }
      
      if (! updated) break;
      
      for (size_type trg = 1; trg <= target_size; ++ trg) {
	double sum = 0.0;
	for (size_type src = 0; src <= source_size; ++ src)
	  sum += probs(trg, src) * exp_phi[src];
	
	const double factor = 1.0 / sum;
	for (size_type src = 0; src <= source_size; ++ src)
	  posterior(trg, src) = probs(trg, src) * factor * exp_phi[src];
      }
    }
    
    // update...
    for (size_type trg = 1; trg <= target_size; ++ trg)
      for (size_type src = 0; src <= source_size; ++ src)
	counts[src == 0 ? vocab_type::EPSILON : source[src - 1]][target[trg - 1]] += posterior(trg, src);
  }
  
  void learn(const sentence_type& source,
	     const sentence_type& target,
	     const ttable_type& ttable,
	     ttable_type& counts,
	     aligned_type& aligned,
	     double& objective)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    double logsum = 0.0;
    
    posterior.reserve(target_size + 1, source_size + 1);
    probs.reserve(target_size + 1, source_size + 1);
    
    posterior.resize(target_size + 1, source_size + 1, 0.0);
    probs.resize(target_size + 1, source_size + 1, 0.0);
    
    phi.clear();
    exp_phi.clear();
    
    phi.reserve(source_size + 1);
    exp_phi.reserve(source_size + 1);
    
    phi.resize(source_size + 1, 0.0);
    exp_phi.resize(source_size + 1, 1.0);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      posterior_set_type::iterator piter     = probs.begin(trg + 1);
      posterior_set_type::iterator piter_end = probs.end(trg + 1);
      *piter = ttable(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;
      
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
	
	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = source[src];
	}
      }
      
      logsum += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      piter = probs.begin(trg + 1);
      posterior_set_type::iterator siter = posterior.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
      
      aligned[word_max].insert(target[trg]);
    }
    
    objective += logsum / target_size;
    
    for (int iter = 0; iter < 5; ++ iter) {
      // update phi.. but ignore NULL...
      
      bool updated = false;
      for (size_type src = 1; src <= source_size; ++ src) {
	double sum = 0.0;
	for (size_type trg = 1; trg <= target_size; ++ trg)
	  sum += posterior(trg, src);
	
	phi[src] += 1.0 - sum;
	if (phi[src] > 0.0)
	  phi[src] = 0.0;
	
	updated |= (phi[src] != 0.0);
	exp_phi[src] = utils::mathop::exp(phi[src]);
      }
      
      if (! updated) break;
      
      for (size_type trg = 1; trg <= target_size; ++ trg) {
	double sum = 0.0;
	for (size_type src = 0; src <= source_size; ++ src)
	  sum += probs(trg, src) * exp_phi[src];
	
	const double factor = 1.0 / sum;
	for (size_type src = 0; src <= source_size; ++ src)
	  posterior(trg, src) = probs(trg, src) * factor * exp_phi[src];
      }
    }
    
    // update...
    for (size_type trg = 1; trg <= target_size; ++ trg)
      for (size_type src = 0; src <= source_size; ++ src)
	counts[src == 0 ? vocab_type::EPSILON : source[src - 1]][target[trg - 1]] += posterior(trg, src);
  }

  void operator()(const sentence_type& source, const sentence_type& target)
  {
    learn(source, target, ttable_source_target, ttable_counts_source_target, aligned_source_target, objective_source_target);
    learn(target, source, ttable_target_source, ttable_counts_target_source, aligned_target_source, objective_target_source);
  }

  void operator()(const sentence_type& source, const sentence_type& target, const alignment_type& alignment)
  {
    learn(source, target, alignment, false, ttable_source_target, ttable_counts_source_target, aligned_source_target, objective_source_target);
    learn(target, source, alignment, true,  ttable_target_source, ttable_counts_target_source, aligned_target_source, objective_target_source);
  }

  void shrink()
  {
    
  }

  posterior_set_type posterior;
  posterior_set_type probs;
  point_map_type     points;
  
  prob_set_type      phi;
  prob_set_type      exp_phi;
};

struct LearnModel1Symmetric : public LearnBase
{
  LearnModel1Symmetric(const LearnBase& __base)
    : LearnBase(__base) {}
  
  typedef utils::vector2<prob_type, std::allocator<prob_type> > posterior_set_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;

  typedef std::set<int, std::less<int>, std::allocator<int> > point_set_type;
  typedef std::vector<point_set_type, std::allocator<point_set_type> > point_map_type;

  void operator()(const sentence_type& source, const sentence_type& target, const alignment_type& alignment)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!

    points_source_target.clear();
    points_target_source.clear();

    points_source_target.reserve(target_size);
    points_target_source.reserve(source_size);
    
    points_source_target.resize(target_size);
    points_target_source.resize(source_size);
    
    alignment_type::const_iterator aiter_end = alignment.end();
    for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter) {
      points_source_target[aiter->target].insert(aiter->source);
      points_target_source[aiter->source].insert(aiter->target);
    }
    
    posterior_source_target.clear();
    posterior_target_source.clear();

    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1, 0.0);
    posterior_target_source.resize(source_size + 1, target_size + 1, 0.0);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);

    double logsum_source_target = 0.0;
    double logsum_target_source = 0.0;
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      if (! points_source_target[trg].empty()) {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;
	
	std::fill(prob_source_target.begin(), prob_source_target.end(), 0.0);
	
	point_set_type::const_iterator iter_end = points_source_target[trg].end();
	for (point_set_type::const_iterator iter = points_source_target[trg].begin(); iter != iter_end; ++ iter) {
	  const int src = *iter;
	  
	  double& prob = prob_source_target[src + 1];
	  prob = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += prob;
	  
	  if (prob > prob_max) {
	    prob_max = prob;
	    word_max = source[src];
	  }
	}
	
	logsum_source_target += utils::mathop::log(prob_sum);
	
	const double factor = 1.0 / prob_sum;
	
	prob_set_type::const_iterator piter      = prob_source_target.begin();
	prob_set_type::const_iterator piter_end  = prob_source_target.end();
	posterior_set_type::iterator  siter = posterior_source_target.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;
	
	aligned_source_target[word_max].insert(target[trg]);
      } else {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
      
	prob_set_type::iterator piter     = prob_source_target.begin();
	prob_set_type::iterator piter_end = prob_source_target.end();
	*piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
	prob_sum += *piter;
      
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;

	++ piter;
      
	for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	  *piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += *piter;
	
	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = source[src];
	  }
	}
      
	logsum_source_target += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	piter = prob_source_target.begin();
	posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;
	
	aligned_source_target[word_max].insert(target[trg]);
      }
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      if (! points_target_source[src].empty()) {
	const double prob_align_norm = 1.0 / target_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;

	std::fill(prob_target_source.begin(), prob_target_source.end(), 0.0);
	
	point_set_type::const_iterator iter_end = points_target_source[src].end();
	for (point_set_type::const_iterator iter = points_target_source[src].begin(); iter != iter_end; ++ iter) {
	  const int trg = *iter;
	  
	  double& prob = prob_target_source[trg + 1];
	  prob = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	  prob_sum += prob;
	  
	  if (prob > prob_max) {
	    prob_max = prob;
	    word_max = target[trg];
	  }
	}
	
	logsum_target_source += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	prob_set_type::const_iterator piter     = prob_target_source.begin();
	prob_set_type::const_iterator piter_end = prob_target_source.end();
	posterior_set_type::iterator  titer     = posterior_target_source.begin(src + 1);
	for (/**/; piter != piter_end; ++ piter, ++ titer)
	  (*titer) = (*piter) * factor;
	
	aligned_target_source[word_max].insert(source[src]);
      } else {
	const double prob_align_norm = 1.0 / target_size;
	double prob_sum = 0.0;
      
	prob_set_type::iterator piter     = prob_target_source.begin();
	prob_set_type::iterator piter_end = prob_target_source.end();
	*piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
	prob_sum += *piter;
      
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;

	++ piter;
      
	for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	  *piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	  prob_sum += *piter;

	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = target[trg];
	  }
	}
      
	logsum_target_source += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	piter = prob_target_source.begin();
	posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
	for (/**/; piter != piter_end; ++ piter, ++ titer)
	  (*titer) = (*piter) * factor;
	
	aligned_target_source[word_max].insert(source[src]);
      }
    }
    
    // accumulate!
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = (src == 0); trg <= target_size; ++ trg) {
	double count = ((trg == 0 ? 1.0 : posterior_source_target(trg, src))
			* (src == 0 ? 1.0 : posterior_target_source(src, trg)));
	
	if (src != 0 && trg != 0)
	  count = utils::mathop::sqrt(count);
	
	const word_type& source_word = (src == 0 ? vocab_type::EPSILON : source[src - 1]);
	const word_type& target_word = (trg == 0 ? vocab_type::EPSILON : target[trg - 1]);
	
	if (trg != 0)
	  ttable_counts_source_target[source_word][target_word] += count;
	
	if (src != 0)
	  ttable_counts_target_source[target_word][source_word] += count;
      }
    
    objective_source_target += logsum_source_target / target_size;
    objective_target_source += logsum_target_source / source_size;
  } 
  
  void operator()(const sentence_type& source, const sentence_type& target)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);

    double logsum_source_target = 0.0;
    double logsum_target_source = 0.0;
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;

      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
	
	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = source[src];
	}
      }
      
      logsum_source_target += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
      
      aligned_source_target[word_max].insert(target[trg]);
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;

      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;

	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = target[trg];
	}
      }
      
      logsum_target_source += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
      
      aligned_target_source[word_max].insert(source[src]);
    }
    
    // accumulate!
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = (src == 0); trg <= target_size; ++ trg) {
	double count = ((trg == 0 ? 1.0 : posterior_source_target(trg, src))
			* (src == 0 ? 1.0 : posterior_target_source(src, trg)));
	
	if (src != 0 && trg != 0)
	  count = utils::mathop::sqrt(count);
	
	const word_type& source_word = (src == 0 ? vocab_type::EPSILON : source[src - 1]);
	const word_type& target_word = (trg == 0 ? vocab_type::EPSILON : target[trg - 1]);
	
	if (trg != 0)
	  ttable_counts_source_target[source_word][target_word] += count;
	
	if (src != 0)
	  ttable_counts_target_source[target_word][source_word] += count;
      }
    
    objective_source_target += logsum_source_target / target_size;
    objective_target_source += logsum_target_source / source_size;
  }

  void shrink()
  {
    
  }

  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;

  point_map_type points_source_target;
  point_map_type points_target_source;
};

struct LearnModel1SymmetricPosterior : public LearnBase
{
  LearnModel1SymmetricPosterior(const LearnBase& __base)
    : LearnBase(__base) {}
  
  typedef utils::vector2<prob_type, std::allocator<prob_type> > posterior_set_type;

  typedef std::set<int, std::less<int>, std::allocator<int> > point_set_type;
  typedef std::vector<point_set_type, std::allocator<point_set_type> > point_map_type;

  void operator()(const sentence_type& source, const sentence_type& target, const alignment_type& alignment)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    points_source_target.clear();
    points_target_source.clear();

    points_source_target.reserve(target_size);
    points_target_source.reserve(source_size);
    
    points_source_target.resize(target_size);
    points_target_source.resize(source_size);
    
    alignment_type::const_iterator aiter_end = alignment.end();
    for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter) {
      points_source_target[aiter->target].insert(aiter->source);
      points_target_source[aiter->source].insert(aiter->target);
    }
    
    // we do not have to clearn!
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1, 0.0);
    posterior_target_source.resize(source_size + 1, target_size + 1, 0.0);
    
    prob_source_target.clear();
    prob_target_source.clear();
    
    prob_source_target.reserve(target_size + 1, source_size + 1);
    prob_target_source.reserve(source_size + 1, target_size + 1);
    
    prob_source_target.resize(target_size + 1, source_size + 1, 0.0);
    prob_target_source.resize(source_size + 1, target_size + 1, 0.0);
    
    phi.clear();
    exp_phi.clear();

    phi.reserve(target_size + 1, source_size + 1);
    exp_phi.reserve(target_size + 1, source_size + 1);
    
    phi.resize(target_size + 1, source_size + 1, 0.0);
    exp_phi.resize(target_size + 1, source_size + 1, 1.0);
    
    double logsum_source_target = 0.0;
    double logsum_target_source = 0.0;
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      if (! points_source_target[trg].empty()) {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;
	
	point_set_type::const_iterator iter_end = points_source_target[trg].end();
	for (point_set_type::const_iterator iter = points_source_target[trg].begin(); iter != iter_end; ++ iter) {
	  const int src = *iter;
	  
	  double& prob = prob_source_target(trg + 1, src + 1);
	  prob = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += prob;
	  
	  if (prob > prob_max) {
	    prob_max = prob;
	    word_max = source[src];
	  }
	}
	
	logsum_source_target += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
	posterior_set_type::const_iterator piter     = prob_source_target.begin(trg + 1);
	posterior_set_type::const_iterator piter_end = prob_source_target.end(trg + 1);
	posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;

	aligned_source_target[word_max].insert(target[trg]);
      } else {
	const double prob_align_norm = 1.0 / source_size;
	double prob_sum = 0.0;
      
	posterior_set_type::iterator piter     = prob_source_target.begin(trg + 1);
	posterior_set_type::iterator piter_end = prob_source_target.end(trg + 1);
	*piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
	prob_sum += *piter;
      
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;

	++ piter;
      
	for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	  *piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	  prob_sum += *piter;
	
	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = source[src];
	  }
	}
      
	logsum_source_target += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	piter = prob_source_target.begin(trg + 1);
	posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
	for (/**/; piter != piter_end; ++ piter, ++ siter)
	  (*siter) = (*piter) * factor;

	aligned_source_target[word_max].insert(target[trg]);
      }
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      if (! points_target_source[src].empty()) {
	const double prob_align_norm = 1.0 / target_size;
	double prob_sum = 0.0;
	
	double prob_max    = - std::numeric_limits<double>::infinity();
	word_type word_max = vocab_type::EPSILON;
	
	point_set_type::const_iterator iter_end = points_target_source[src].end();
	for (point_set_type::const_iterator iter = points_target_source[src].begin(); iter != iter_end; ++ iter) {
	  const int trg = *iter;
	  
	  double& prob = prob_target_source(src + 1, trg + 1);
	  
	  prob = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	  prob_sum += prob;
	  
	  if (prob > prob_max) {
	    prob_max = prob;
	    word_max = target[trg];
	  }
	}
	
	logsum_target_source += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	posterior_set_type::const_iterator piter     = prob_target_source.begin(src + 1);
	posterior_set_type::const_iterator piter_end = prob_target_source.end(src + 1);
	posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
	for (/**/; piter != piter_end; ++ piter, ++ titer)
	  (*titer) = (*piter) * factor;
	
	aligned_target_source[word_max].insert(source[src]);
      } else {
	const double prob_align_norm = 1.0 / target_size;
	double prob_sum = 0.0;
      
	posterior_set_type::iterator piter     = prob_target_source.begin(src + 1);
	posterior_set_type::iterator piter_end = prob_target_source.end(src + 1);
	*piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
	prob_sum += *piter;
      
	double prob_max    = *piter;
	word_type word_max = vocab_type::EPSILON;

	++ piter;
      
	for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	  *piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	  prob_sum += *piter;
	
	  if (*piter > prob_max) {
	    prob_max = *piter;
	    word_max = target[trg];
	  }
	}
      
	logsum_target_source += utils::mathop::log(prob_sum);
      
	const double factor = 1.0 / prob_sum;
      
	piter = prob_target_source.begin(src + 1);
	posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
	for (/**/; piter != piter_end; ++ piter, ++ titer)
	  (*titer) = (*piter) * factor;
      
	aligned_target_source[word_max].insert(source[src]);
      }
    }
    
    // perplexity..
    objective_source_target += logsum_source_target / target_size;
    objective_target_source += logsum_target_source / source_size;
    
    // now we will adjust posterior..
    
    for (int iter = 0; iter != 5; ++ iter) {
      
      bool updated = false;
      
      // update phi... we do not consider null alignment!
      for (size_type src = 1; src <= source_size; ++ src)
	for (size_type trg = 1; trg <= target_size; ++ trg) {
	  const double epsi = posterior_source_target(trg, src) - posterior_target_source(src, trg);
	  const double update = - epsi;
	  
	  phi(trg, src) += update;
	  
	  updated |= (phi(trg, src) != 0.0);
	  exp_phi(trg, src) = utils::mathop::exp(phi(trg, src));
	}
      
      if (! updated) break;
      
      // recompute...
      for (size_type trg = 1; trg <= target_size; ++ trg) {
	double prob_sum = 0.0;
	for (size_type src = 0; src <= source_size; ++ src)
	  prob_sum += prob_source_target(trg, src) * exp_phi(trg, src);
	
	const double factor = 1.0 / prob_sum;
	for (size_type src = 0; src <= source_size; ++ src)
	  posterior_source_target(trg, src) = prob_source_target(trg, src) * factor *  exp_phi(trg, src);
      }
      
      for (size_type src = 1; src <= source_size; ++ src) {
	double prob_sum = 0.0;
	for (size_type trg = 0; trg <= target_size; ++ trg)
	  prob_sum += prob_target_source(src, trg) / exp_phi(trg, src);
	
	const double factor = 1.0 / prob_sum;
	for (size_type trg = 0; trg <= target_size; ++ trg)
	  posterior_target_source(src, trg) = prob_target_source(src, trg) * factor / exp_phi(trg, src);
      }
    }
    
    // since we have already adjusted posterior, we simply accumulate individual counts...
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = (src == 0); trg <= target_size; ++ trg) {
	const word_type& source_word = (src == 0 ? vocab_type::EPSILON : source[src - 1]);
	const word_type& target_word = (trg == 0 ? vocab_type::EPSILON : target[trg - 1]);
	
	if (trg != 0)
	  ttable_counts_source_target[source_word][target_word] += posterior_source_target(trg, src);
	
	if (src != 0)
	  ttable_counts_target_source[target_word][source_word] += posterior_target_source(src, trg);
      }
  }

  
  void operator()(const sentence_type& source, const sentence_type& target)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);

    prob_source_target.reserve(target_size + 1, source_size + 1);
    prob_target_source.reserve(source_size + 1, target_size + 1);
    
    prob_source_target.resize(target_size + 1, source_size + 1);
    prob_target_source.resize(source_size + 1, target_size + 1);
    
    phi.clear();
    exp_phi.clear();

    phi.reserve(target_size + 1, source_size + 1);
    exp_phi.reserve(target_size + 1, source_size + 1);
    
    phi.resize(target_size + 1, source_size + 1, 0.0);
    exp_phi.resize(target_size + 1, source_size + 1, 1.0);
    
    double logsum_source_target = 0.0;
    double logsum_target_source = 0.0;
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      posterior_set_type::iterator piter     = prob_source_target.begin(trg + 1);
      posterior_set_type::iterator piter_end = prob_source_target.end(trg + 1);
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;

      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
	
	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = source[src];
	}
      }
      
      logsum_source_target += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin(trg + 1);
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;

      aligned_source_target[word_max].insert(target[trg]);
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      posterior_set_type::iterator piter     = prob_target_source.begin(src + 1);
      posterior_set_type::iterator piter_end = prob_target_source.end(src + 1);
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      
      double prob_max    = *piter;
      word_type word_max = vocab_type::EPSILON;

      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
	
	if (*piter > prob_max) {
	  prob_max = *piter;
	  word_max = target[trg];
	}
      }
      
      logsum_target_source += utils::mathop::log(prob_sum);
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin(src + 1);
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
      
      aligned_target_source[word_max].insert(source[src]);
    }
    
    // perplexity..
    objective_source_target += logsum_source_target / target_size;
    objective_target_source += logsum_target_source / source_size;
    
    // now we will adjust posterior..
    
    for (int iter = 0; iter != 5; ++ iter) {
      
      bool updated = false;
      
      // update phi... we do not consider null alignment!
      for (size_type src = 1; src <= source_size; ++ src)
	for (size_type trg = 1; trg <= target_size; ++ trg) {
	  const double epsi = posterior_source_target(trg, src) - posterior_target_source(src, trg);
	  const double update = - epsi;
	  
	  phi(trg, src) += update;
	  
	  updated |= (phi(trg, src) != 0.0);
	  exp_phi(trg, src) = utils::mathop::exp(phi(trg, src));
	}
      
      if (! updated) break;
      
      // recompute...
      for (size_type trg = 1; trg <= target_size; ++ trg) {
	double prob_sum = 0.0;
	for (size_type src = 0; src <= source_size; ++ src)
	  prob_sum += prob_source_target(trg, src) * exp_phi(trg, src);
	
	const double factor = 1.0 / prob_sum;
	for (size_type src = 0; src <= source_size; ++ src)
	  posterior_source_target(trg, src) = prob_source_target(trg, src) * factor *  exp_phi(trg, src);
      }
      
      for (size_type src = 1; src <= source_size; ++ src) {
	double prob_sum = 0.0;
	for (size_type trg = 0; trg <= target_size; ++ trg)
	  prob_sum += prob_target_source(src, trg) / exp_phi(trg, src);
	
	const double factor = 1.0 / prob_sum;
	for (size_type trg = 0; trg <= target_size; ++ trg)
	  posterior_target_source(src, trg) = prob_target_source(src, trg) * factor / exp_phi(trg, src);
      }
    }
    
    // since we have already adjusted posterior, we simply accumulate individual counts...
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = (src == 0); trg <= target_size; ++ trg) {
	const word_type& source_word = (src == 0 ? vocab_type::EPSILON : source[src - 1]);
	const word_type& target_word = (trg == 0 ? vocab_type::EPSILON : target[trg - 1]);
	
	if (trg != 0)
	  ttable_counts_source_target[source_word][target_word] += posterior_source_target(trg, src);
	
	if (src != 0)
	  ttable_counts_target_source[target_word][source_word] += posterior_target_source(src, trg);
      }
  }

  void shrink()
  {
    
  
  }
  
  posterior_set_type prob_source_target;
  posterior_set_type prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;
  
  posterior_set_type phi;
  posterior_set_type exp_phi;
  
  point_map_type points_source_target;
  point_map_type points_target_source;
};

struct ViterbiModel1 : public ViterbiBase
{
  ViterbiModel1(const ttable_type& __ttable_source_target,
		const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}

  void viterbi(const sentence_type& source,
	       const sentence_type& target,
	       const ttable_type& ttable,
	       alignment_type& alignment)
  {
    alignment.clear();
    
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      
      double prob_max = ttable(vocab_type::EPSILON, target[trg]) * prob_null;
      int    align_max = -1;
      
      for (size_type src = 0; src != source_size; ++ src) {
	const double prob = ttable(source[src], target[trg]) * prob_align * prob_align_norm;
	
	if (prob > prob_max) {
	  prob_max = prob;
	  align_max = src;
	}
      }
      
      if (align_max >= 0)
	alignment.push_back(std::make_pair(align_max, static_cast<int>(trg)));
    }
  }

  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const span_set_type& span_source,
		  const span_set_type& span_target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    operator()(source, target, alignment_source_target, alignment_target_source);
  }
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    viterbi(source, target, ttable_source_target, alignment_source_target);
    viterbi(target, source, ttable_target_source, alignment_target_source);
  }

  void shrink()
  {
    
  }
};


struct PosteriorModel1 : public ViterbiBase
{
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  
  PosteriorModel1(const ttable_type& __ttable_source_target,
	    const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}
  
  template <typename Matrix>
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  Matrix& posterior_source_target,
		  Matrix& posterior_target_source)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      typename Matrix::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      typename Matrix::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
    }
  }

  void shrink()
  {
    prob_source_target.clear();
    prob_target_source.clear();
    
    prob_set_type(prob_source_target).swap(prob_source_target);
    prob_set_type(prob_target_source).swap(prob_target_source);
  }
  
  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
};

struct ITGModel1 : public ViterbiBase
{
  typedef utils::vector2<double, std::allocator<double> > matrix_type;
  typedef utils::vector2<double, std::allocator<double> > posterior_set_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  
  ITGModel1(const ttable_type& __ttable_source_target,
	    const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}

  class insert_align
  {
    alignment_type& alignment_source_target;
    alignment_type& alignment_target_source;
    
  public:
    insert_align(alignment_type& __alignment_source_target,
		 alignment_type& __alignment_target_source)
      : alignment_source_target(__alignment_source_target),
	alignment_target_source(__alignment_target_source) {}
    
    template <typename Edge>
    insert_align& operator=(const Edge& edge)
    {	
      alignment_source_target.push_back(edge);
      alignment_target_source.push_back(std::make_pair(edge.second, edge.first));
      
      return *this;
    }
    
    insert_align& operator*() { return *this; }
    insert_align& operator++() { return *this; }
    insert_align operator++(int) { return *this; }
  };

  const span_set_type __span_source;
  const span_set_type __span_target;
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    operator()(source, target, __span_source, __span_target, alignment_source_target, alignment_target_source);
  }
  
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const span_set_type& span_source,
		  const span_set_type& span_target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
    }

    costs.clear();
    costs.reserve(source_size + 1, target_size + 1);
    costs.resize(source_size + 1, target_size + 1, boost::numeric::bounds<double>::lowest());
    
    for (size_type src = 1; src <= source_size; ++ src)
      for (size_type trg = 1; trg <= target_size; ++ trg)
	costs(src, trg) = 0.5 * (utils::mathop::log(posterior_source_target(trg, src)) 
				 + utils::mathop::log(posterior_target_source(src, trg)));
    
    for (size_type trg = 1; trg <= target_size; ++ trg)
      costs(0, trg) = utils::mathop::log(posterior_source_target(trg, 0));
    
    for (size_type src = 1; src <= source_size; ++ src)
      costs(src, 0) = utils::mathop::log(posterior_target_source(src, 0));
    
    alignment_source_target.clear();
    alignment_target_source.clear();
    
    if (span_source.empty() && span_target.empty())
      aligner(costs, insert_align(alignment_source_target, alignment_target_source));
    else
      aligner(costs, span_source, span_target, insert_align(alignment_source_target, alignment_target_source));
    
    std::sort(alignment_source_target.begin(), alignment_source_target.end());
    std::sort(alignment_target_source.begin(), alignment_target_source.end());
  }

  void shrink()
  {
    costs.clear();
    prob_source_target.clear();
    prob_target_source.clear();
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    matrix_type(costs).swap(costs);
    
    prob_set_type(prob_source_target).swap(prob_source_target);
    prob_set_type(prob_target_source).swap(prob_target_source);

    posterior_set_type(posterior_source_target).swap(posterior_source_target);
    posterior_set_type(posterior_target_source).swap(posterior_target_source);

    aligner.shrink();
  }
  
  matrix_type costs;
  
  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;
  
  detail::ITGAlignment aligner;
};

struct MaxMatchModel1 : public ViterbiBase
{
  typedef utils::vector2<double, std::allocator<double> > matrix_type;
  typedef utils::vector2<double, std::allocator<double> > posterior_set_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;

  MaxMatchModel1(const ttable_type& __ttable_source_target,
		 const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}
  
  class insert_align
  {
    int source_size;
    int target_size;
    
    alignment_type& alignment_source_target;
    alignment_type& alignment_target_source;
    
  public:
    insert_align(const int& _source_size,
		 const int& _target_size,
		 alignment_type& __alignment_source_target,
		 alignment_type& __alignment_target_source)
      : source_size(_source_size), target_size(_target_size),
	alignment_source_target(__alignment_source_target),
	alignment_target_source(__alignment_target_source) {}
    
    template <typename Edge>
    insert_align& operator=(const Edge& edge)
    {	
      if (edge.first < source_size && edge.second < target_size) {
	alignment_source_target.push_back(edge);
	alignment_target_source.push_back(std::make_pair(edge.second, edge.first));
      }
      
      return *this;
    }
    
    insert_align& operator*() { return *this; }
    insert_align& operator++() { return *this; }
    insert_align operator++(int) { return *this; }
  };
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const span_set_type& span_source,
		  const span_set_type& span_target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    operator()(source, target, alignment_source_target, alignment_target_source);
  }

  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  alignment_type& alignment_source_target,
		  alignment_type& alignment_target_source)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
    }
    
    costs.clear();
    costs.reserve(source_size + target_size, target_size + source_size);
    costs.resize(source_size + target_size, target_size + source_size, 0.0);
    
    for (size_type src = 0; src != source_size; ++ src)
      for (size_type trg = 0; trg != target_size; ++ trg) {
	costs(src, trg) = 0.5 * (utils::mathop::log(posterior_source_target(trg + 1, src + 1))
				 + utils::mathop::log(posterior_target_source(src + 1, trg + 1)));
	
	costs(src, trg + source_size) = utils::mathop::log(posterior_target_source(src + 1, 0));
	costs(src + target_size, trg) = utils::mathop::log(posterior_source_target(trg + 1, 0));
      }
    
    alignment_source_target.clear();
    alignment_target_source.clear();
    
    kuhn_munkres_assignment(costs, insert_align(source_size, target_size, alignment_source_target, alignment_target_source));
    
    std::sort(alignment_source_target.begin(), alignment_source_target.end());
    std::sort(alignment_target_source.begin(), alignment_target_source.end());
  }
  
  void shrink()
  {
    costs.clear();
    prob_source_target.clear();
    prob_target_source.clear();
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    matrix_type(costs).swap(costs);
    
    prob_set_type(prob_source_target).swap(prob_source_target);
    prob_set_type(prob_target_source).swap(prob_target_source);

    posterior_set_type(posterior_source_target).swap(posterior_source_target);
    posterior_set_type(posterior_target_source).swap(posterior_target_source);
  }
  
  matrix_type costs;

  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;
};

struct DependencyModel1 : public ViterbiBase
{
  typedef utils::vector2<double, std::allocator<double> > matrix_type;
  typedef utils::vector2<double, std::allocator<double> > posterior_set_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  
  DependencyModel1(const ttable_type& __ttable_source_target,
		   const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const dependency_type& dependency_source,
		  const dependency_type& dependency_target)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
    }
    
    static const double lowest = - std::numeric_limits<double>::infinity();
    
    scores.clear();
    scores.reserve(source_size + 1, target_size + 1);
    scores.resize(source_size + 1, target_size + 1, lowest);
    
    for (size_type src = 1; src <= source_size; ++ src)
      for (size_type trg = 1; trg <= target_size; ++ trg)
	scores(src, trg) = 0.5 * (utils::mathop::log(posterior_source_target(trg, src))
				  + utils::mathop::log(posterior_target_source(src, trg)));
    
    if (! dependency_source.empty()) {
      if (dependency_source.size() != source_size)
	throw std::runtime_error("dependency size do not match");
      
      scores_target.clear();
      scores_target.reserve(target_size + 1, target_size + 1);
      scores_target.resize(target_size + 1, target_size + 1, lowest);
      
      // we will compute the score matrix...
      for (size_type trg_head = 1; trg_head <= target_size; ++ trg_head)
	for (size_type trg_dep = 1; trg_dep <= target_size; ++ trg_dep)
	  if (trg_head != trg_dep)
	    for (size_type src = 0; src != dependency_source.size(); ++ src) 
	      if (dependency_source[src]) {
		const size_type src_head = dependency_source[src];
		const size_type src_dep  = src + 1;
		
		const double score = scores(src_head, trg_head) + scores(src_dep, trg_dep);
		
		scores_target(trg_head, trg_dep) = utils::mathop::logsum(scores_target(trg_head, trg_dep), score);
	      }
      
      // this is for the root...
      for (size_type trg_dep = 1; trg_dep <= target_size; ++ trg_dep)
	for (size_type src = 0; src != dependency_source.size(); ++ src) 
	  if (! dependency_source[src]) {
	    const size_type trg_head = 0;
	    const size_type src_head = dependency_source[src];
	    const size_type src_dep  = src + 1;
	    
	    const double score = scores(src_dep, trg_dep);
	    
	    scores_target(trg_head, trg_dep) = utils::mathop::logsum(scores_target(trg_head, trg_dep), score);
	  }
    }
    
    if (! dependency_target.empty()) {
      if (dependency_target.size() != target_size)
	throw std::runtime_error("dependency size do not match");

      scores_source.clear();
      scores_source.reserve(source_size + 1, source_size + 1);
      scores_source.resize(source_size + 1, source_size + 1, lowest);
      
      // we will compute the score matrix...
      for (size_type src_head = 1; src_head <= source_size; ++ src_head)
	for (size_type src_dep = 1; src_dep <= source_size; ++ src_dep)
	  if (src_head != src_dep)
	    for (size_type trg = 0; trg != dependency_target.size(); ++ trg)
	      if (dependency_target[trg]) {
		const size_type trg_head = dependency_target[trg];
		const size_type trg_dep  = trg + 1;
		
		const double score = scores(src_head, trg_head) + scores(src_dep, trg_dep);
		
		scores_source(src_head, src_dep) = utils::mathop::logsum(scores_source(src_head, src_dep), score);
	      }
      
      // this is for the root.
      for (size_type src_dep = 1; src_dep <= source_size; ++ src_dep)
	for (size_type trg = 0; trg != dependency_target.size(); ++ trg)
	  if (! dependency_target[trg]) {
	    const size_type src_head = 0;
	    const size_type trg_head = dependency_target[trg];
	    const size_type trg_dep  = trg + 1;
	    
	    const double score = scores(src_dep, trg_dep);
	    
	    scores_source(src_head, src_dep) = utils::mathop::logsum(scores_source(src_head, src_dep), score);
	  }
    }
  }

  void shrink()
  {
    scores_source.clear();
    scores_target.clear();
    scores.clear();

    prob_source_target.clear();
    prob_target_source.clear();
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    matrix_type(scores_source).swap(scores_source);
    matrix_type(scores_target).swap(scores_target);
    matrix_type(scores).swap(scores);
    
    prob_set_type(prob_source_target).swap(prob_source_target);
    prob_set_type(prob_target_source).swap(prob_target_source);

    posterior_set_type(posterior_source_target).swap(posterior_source_target);
    posterior_set_type(posterior_target_source).swap(posterior_target_source);
  }
  
  matrix_type scores_source;
  matrix_type scores_target;
  matrix_type scores;
  
  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;
};

template <typename Analyzer>
struct __DependencyModel1Base : public DependencyModel1
{
  typedef Analyzer analyzer_type;
  
  __DependencyModel1Base(const ttable_type& __ttable_source_target,
			 const ttable_type& __ttable_target_source)
    : DependencyModel1(__ttable_source_target, __ttable_target_source) {}
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const dependency_type& dependency_source,
		  const dependency_type& dependency_target,
		  dependency_type& projected_source,
		  dependency_type& projected_target)
  {
    DependencyModel1::operator()(source, target, dependency_source, dependency_target);
    
    const size_type source_size = source.size();
    const size_type target_size = target.size();

    projected_source.clear();
    projected_target.clear();
    
    if (! dependency_source.empty()) {
      projected_target.resize(target_size, - 1);
      
      analyzer(scores_target, projected_target);
    }
    
    if (! dependency_target.empty()) {
      projected_source.resize(source_size, - 1);
      
      analyzer(scores_source, projected_source);
    }
  }

  void shink()
  {
    analyzer.shrink();
    DependencyModel1::shrink();
  }
  
  analyzer_type analyzer;
};

typedef __DependencyModel1Base<DependencyHybrid>            DependencyHybridModel1;
typedef __DependencyModel1Base<DependencyHybridSingleRoot>  DependencyHybridSingleRootModel1;
typedef __DependencyModel1Base<DependencyDegree2>           DependencyDegree2Model1;
typedef __DependencyModel1Base<DependencyDegree2SingleRoot> DependencyDegree2SingleRootModel1;
typedef __DependencyModel1Base<DependencyMST>               DependencyMSTModel1;
typedef __DependencyModel1Base<DependencyMSTSingleRoot>     DependencyMSTSingleRootModel1;

struct PermutationModel1 : public ViterbiBase
{
  typedef utils::vector2<double, std::allocator<double> > matrix_type;
  typedef utils::vector2<double, std::allocator<double> > posterior_set_type;
  typedef std::vector<double, std::allocator<double> > prob_set_type;
  
  
  typedef std::vector<bool, std::allocator<bool > > assigned_type;

  PermutationModel1(const ttable_type& __ttable_source_target,
		    const ttable_type& __ttable_target_source)
    : ViterbiBase(__ttable_source_target, __ttable_target_source) {}
  
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const dependency_type& dependency_source,
		  const dependency_type& dependency_target)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const double prob_null  = p0;
    const double prob_align = 1.0 - p0;
    
    // we do not have to clearn!
    posterior_source_target.reserve(target_size + 1, source_size + 1);
    posterior_target_source.reserve(source_size + 1, target_size + 1);
    
    posterior_source_target.resize(target_size + 1, source_size + 1);
    posterior_target_source.resize(source_size + 1, target_size + 1);
    
    prob_source_target.reserve(source_size + 1);
    prob_target_source.reserve(target_size + 1);
    
    prob_source_target.resize(source_size + 1);
    prob_target_source.resize(target_size + 1);
    
    for (size_type trg = 0; trg != target_size; ++ trg) {
      const double prob_align_norm = 1.0 / source_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_source_target.begin();
      prob_set_type::iterator piter_end = prob_source_target.end();
      *piter = ttable_source_target(vocab_type::EPSILON, target[trg]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type src = 0; src != source_size; ++ src, ++ piter) {
	*piter = ttable_source_target(source[src], target[trg]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_source_target.begin();
      posterior_set_type::iterator siter = posterior_source_target.begin(trg + 1);
      for (/**/; piter != piter_end; ++ piter, ++ siter)
	(*siter) = (*piter) * factor;
    }
    
    for (size_type src = 0; src != source_size; ++ src) {
      const double prob_align_norm = 1.0 / target_size;
      double prob_sum = 0.0;
      
      prob_set_type::iterator piter     = prob_target_source.begin();
      prob_set_type::iterator piter_end = prob_target_source.end();
      *piter = ttable_target_source(vocab_type::EPSILON, source[src]) * prob_null;
      prob_sum += *piter;
      ++ piter;
      
      for (size_type trg = 0; trg != target_size; ++ trg, ++ piter) {
	*piter = ttable_target_source(target[trg], source[src]) * prob_align * prob_align_norm;
	prob_sum += *piter;
      }
      
      const double factor = 1.0 / prob_sum;
      
      piter = prob_target_source.begin();
      posterior_set_type::iterator titer = posterior_target_source.begin(src + 1);
      for (/**/; piter != piter_end; ++ piter, ++ titer)
	(*titer) = (*piter) * factor;
    }
    
    static const double lowest = - std::numeric_limits<double>::infinity();
    
    scores.clear();
    scores.reserve(source_size + 1, target_size + 1);
    scores.resize(source_size + 1, target_size + 1, lowest);
    
    for (size_type src = 1; src <= source_size; ++ src)
      for (size_type trg = 1; trg <= target_size; ++ trg)
	scores(src, trg) = 0.5 * (utils::mathop::log(posterior_source_target(trg, src))
				  + utils::mathop::log(posterior_target_source(src, trg)));
    
    if (! dependency_source.empty()) {
      if (dependency_source.size() != source_size)
	throw std::runtime_error("dependency size do not match");
      
      scores_target.clear();
      scores_target.reserve(target_size + 1, target_size + 1);
      scores_target.resize(target_size + 1, target_size + 1, lowest);
      
      // checking...
      assigned.clear();
      assigned.resize(source_size, false);
      
      for (size_type src = 0; src != dependency_source.size(); ++ src) {
	if (dependency_source[src] >= static_cast<int>(source_size))
	  throw std::runtime_error("invalid permutation: out of range");
	
	if (assigned[dependency_source[src]])
	  throw std::runtime_error("invalid permutation: duplicates");
	
	assigned[dependency_source[src]] = true;
      }
      
      // we will compute the score matrix...
      for (size_type trg_head = 1; trg_head != target_size; ++ trg_head)
	for (size_type trg_dep = 1; trg_dep <= target_size; ++ trg_dep)
	  if (trg_head != trg_dep)
	    for (size_type src = 0; src != dependency_source.size(); ++ src) 
	      if (dependency_source[src]) {
		const size_type src_head = dependency_source[src];
		const size_type src_dep  = src + 1;
		
		const double score = scores(src_head, trg_head) + scores(src_dep, trg_dep);
		
		// transposed!
		scores_target(trg_dep, trg_head) = utils::mathop::logsum(scores_target(trg_dep, trg_head), score);
	      }
      
      // this is for the root...
      for (size_type trg_dep = 1; trg_dep <= target_size; ++ trg_dep)
	for (size_type src = 0; src != dependency_source.size(); ++ src) 
	  if (! dependency_source[src]) {
	    const size_type trg_head = 0;
	    const size_type src_head = dependency_source[src];
	    const size_type src_dep  = src + 1;
	    
	    const double score = scores(src_dep, trg_dep);
	    
	    scores_target(trg_dep, trg_head) = utils::mathop::logsum(scores_target(trg_dep, trg_head), score);
	  }
      
      // dummy
      scores_target(0, target_size) = 0.0;
    }
    
    if (! dependency_target.empty()) {
      if (dependency_target.size() != target_size)
	throw std::runtime_error("dependency size do not match");

      scores_source.clear();
      scores_source.reserve(source_size + 1, source_size + 1);
      scores_source.resize(source_size + 1, source_size + 1, lowest);
      
      // checking...
      assigned.clear();
      assigned.resize(target_size, false);
      
      for (size_type trg = 0; trg != dependency_target.size(); ++ trg) {
	if (dependency_target[trg] >= static_cast<int>(target_size))
	  throw std::runtime_error("invalid permutation: out of range");
	
	if (assigned[dependency_target[trg]])
	  throw std::runtime_error("invalid permutation: duplicates");
	
	assigned[dependency_target[trg]] = true;
      }
      
      // we will compute the score matrix...
      for (size_type src_head = 1; src_head != source_size; ++ src_head)
	for (size_type src_dep = 1; src_dep <= source_size; ++ src_dep)
	  if (src_head != src_dep)
	    for (size_type trg = 0; trg != dependency_target.size(); ++ trg)
	      if (dependency_target[trg]) {
		const size_type trg_head = dependency_target[trg];
		const size_type trg_dep  = trg + 1;
		
		const double score = scores(src_head, trg_head) + scores(src_dep, trg_dep);
		
		scores_source(src_dep, src_head) = utils::mathop::logsum(scores_source(src_dep, src_head), score);
	      }
      
      // this is for the root.
      for (size_type src_dep = 1; src_dep <= source_size; ++ src_dep)
	for (size_type trg = 0; trg != dependency_target.size(); ++ trg)
	  if (! dependency_target[trg]) {
	    const size_type src_head = 0;
	    const size_type trg_head = dependency_target[trg];
	    const size_type trg_dep  = trg + 1;
	    
	    const double score = scores(src_dep, trg_dep);
	    
	    scores_source(src_dep, src_head) = utils::mathop::logsum(scores_source(src_dep, src_head), score);
	  }
      
      // dummy
      scores_source(0, source_size) = 0.0;
    }
  }
  
  template <typename Dependency>
  struct insert_dependency
  {
    Dependency& dependency;
    
    insert_dependency(Dependency& __dependency) : dependency(__dependency) {}

    template <typename Edge>
    insert_dependency& operator=(const Edge& edge)
    {
      if (edge.first)
	dependency[edge.first - 1] = edge.second;
      return *this;
    }
    
    insert_dependency& operator*() { return *this; }
    insert_dependency& operator++() { return *this; }
    insert_dependency operator++(int) { return *this; }
  };

  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const dependency_type& dependency_source,
		  const dependency_type& dependency_target,
		  dependency_type& projected_source,
		  dependency_type& projected_target)
  {
    operator()(source, target, dependency_source, dependency_target);
    
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    projected_source.clear();
    projected_target.clear();
    
    if (! dependency_source.empty()) {
      projected_target.resize(target_size, - 1);

      kuhn_munkres_assignment(scores_target, insert_dependency<dependency_type>(projected_target));
    }
    
    if (! dependency_target.empty()) {
      projected_source.resize(source_size, - 1);
      
      kuhn_munkres_assignment(scores_source, insert_dependency<dependency_type>(projected_source));
    }
  }

  void shrink()
  {
    scores_source.clear();
    scores_target.clear();
    scores.clear();

    prob_source_target.clear();
    prob_target_source.clear();
    posterior_source_target.clear();
    posterior_target_source.clear();
    
    matrix_type(scores_source).swap(scores_source);
    matrix_type(scores_target).swap(scores_target);
    matrix_type(scores).swap(scores);
    
    prob_set_type(prob_source_target).swap(prob_source_target);
    prob_set_type(prob_target_source).swap(prob_target_source);

    posterior_set_type(posterior_source_target).swap(posterior_source_target);
    posterior_set_type(posterior_target_source).swap(posterior_target_source);
  }
  
  matrix_type scores_source;
  matrix_type scores_target;
  matrix_type scores;
  
  prob_set_type      prob_source_target;
  prob_set_type      prob_target_source;
  posterior_set_type posterior_source_target;
  posterior_set_type posterior_target_source;  

  assigned_type assigned;
};

#endif

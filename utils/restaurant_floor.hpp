// -*- mode: c++ -*-
//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//


#ifndef __UTILS__RESTAURANT_FLOOR__HPP__
#define __UTILS__RESTAURANT_FLOOR__HPP__ 1

#include <numeric>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>

#include <utils/unordered_map.hpp>
#include <utils/slice_sampler.hpp>
#include <utils/mathop.hpp>
#include <utils/pyp_parameter.hpp>
#include <utils/table_count.hpp>

// Chinese Restaurant_Floor Process
//
// inspired by restauraht.hh
//

//
// Chinese Restaurant Process with optional customer and table tracking
// Copyright Trevor Cohn 2009, University of Edinburgh
// Table tracking algorithm courtesy of Mark Johnson, and is described in:
//
//      A Note on the Implementation of Hierarchical Dirichlet Processes
//      Phil Blunsom, Trevor Cohn, Sharon Goldwater and Mark Johnson
//      ACL 2009 (short paper)
//

// multiple-floor CRP
//
// @inproceedings{WooTeh2009a,
// Author =        {F. Wood and Y. W. Teh},
// Title =         {A Hierarchical Nonparametric {B}ayesian Approach 
//                 to Statistical Language Model Domain Adaptation},
// Booktitle =     {Proceedings of the International Conference on 
//                 Artificial Intelligence and Statistics},
// Volume =        {12},
// Year =          {2009}}

namespace utils
{

template <size_t Floors, typename Tp, typename Hash=boost::hash<Tp>, typename Pred=std::equal_to<Tp>, typename Alloc=std::allocator<Tp> >
  class restaurant_floor
  {
  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef Tp dish_type;
    typedef pyp_parameter parameter_type;
    
  public:
    restaurant_floor()
      : tables(),
	customers(),
	parameter()
    { }

    restaurant_floor(const parameter_type& __parameter)
      : tables(),
	customers(),
	parameter(__parameter)
    { }
    
    
    restaurant_floor(const double& __discount,
		     const double& __strength)
      : tables(),
	customers(),
	parameter(__discount,
		  __strength)
    {
      parameter.verify_parameters();
    }

    restaurant_floor(const double& __discount_alpha,
		     const double& __discount_beta,
		     const double& __strength_shape,
		     const double& __strength_rate)
      : tables(),
	customers(),
	parameter(__discount_alpha,
		  __discount_beta,
		  __strength_shape,
		  __strength_rate)
    {
      parameter.verify_parameters();
    }
    
    restaurant_floor(const double& __discount,
		     const double& __strength,
		     const double& __discount_alpha,
		     const double& __discount_beta,
		     const double& __strength_shape,
		     const double& __strength_rate)
      : tables(),
	customers(),
	parameter(__discount,
		  __strength,
		  __discount_alpha,
		  __discount_beta,
		  __strength_shape,
		  __strength_rate)
    {
      parameter.verify_parameters();
    }

  private:
    struct Location
    {
      typedef typename Alloc::template rebind<size_type>::other alloc_type;

      typedef utils::table_count<size_type, Floors, alloc_type> count_set_type;
      
      typedef typename count_set_type::const_iterator const_iterator;
      
      Location() : counts() {}
      
      const_iterator begin() const { return counts.begin(); }
      const_iterator end() const { return counts.end(); }
      
      size_type size_customer() const { return counts.customers(); }
      size_type size_table() const { return counts.tables(); }
      
      bool empty() const { return counts.customers() == 0; }

      void clear()
      {
	counts.clear();
      }

      void swap(Location& x)
      {
	counts.swap(x.counts);
      }

      count_set_type counts;
    };
    typedef Location location_type;
    
    typedef typename Alloc::template rebind<std::pair<const dish_type, location_type> >::other alloc_type;
    typedef typename utils::unordered_map<dish_type, location_type, Hash, Pred, alloc_type>::type dish_set_type;

  public:
    typedef typename dish_set_type::key_type       key_type;
    typedef typename dish_set_type::mapped_type    mapped_type;
    typedef typename dish_set_type::value_type     value_type;
    typedef typename dish_set_type::const_iterator const_iterator;
    typedef typename dish_set_type::const_iterator iterator;
    
  public:
    const_iterator begin() const { return dishes.begin(); }
    const_iterator end() const { return dishes.end(); }

    bool has_discount_prior() const { return parameter.has_discount_prior(); }
    bool has_strength_prior() const { return parameter.has_strength_prior(); }

    double& discount() { return parameter.discount; }
    double& strength() { return parameter.strength; }

    const double& discount() const { return parameter.discount; }
    const double& strength() const { return parameter.strength; }

    const parameter_type& parameters() const { return parameter; }
    
    void clear()
    {
      tables = 0;
      customers = 0;
      dishes.clear();
    }

    bool empty() const { return dishes.empty(); }
    
    size_type size() const { return dishes.size(); }
    
    size_type size_customer() const { return customers; }

    size_type size_table() const { return tables; }
    
    size_type size_table(const dish_type& dish) const
    {
      typename dish_set_type::const_iterator diter = dishes.find(dish);
      
      return (diter == dishes.end() ? size_type(0) : diter->second.size_table());
    }

    size_type size_customer(const dish_type& dish) const
    {
      typename dish_set_type::const_iterator diter = dishes.find(dish);
      
      return (diter == dishes.end() ? size_type(0) : diter->second.size_customer());
    }
    
    void swap(restaurant_floor& x)
    {
      std::swap(tables, x.tables);
      std::swap(customers, x.customers);
      dishes.swap(x.dishes);
      parameter.swap(x.parameter);
    }

    template <typename PriorIterator, typename LambdaIterator, typename Sampler>
    std::pair<size_type, bool> increment(const dish_type& dish,
					 PriorIterator first,
					 PriorIterator last,
					 LambdaIterator lambda,
					 Sampler& sampler,
					 const double temperature=1.0)
    {
      location_type& loc = dishes[dish];
      
      const double p0 = std::inner_product(first, last, lambda, 0.0);
      
      bool existing = false;
      if (loc.size_customer()) {
	if (temperature == 1.0) {
	  const double p_base = (parameter.strength + tables * parameter.discount) * p0;
	  const double p_gen  = (loc.size_customer() - loc.size_table() * parameter.discount);
	  
	  existing = sampler.bernoulli(p_gen / (p_base + p_gen));
	} else {
	  const double p_base = std::pow((parameter.strength + tables * parameter.discount) * p0, 1.0 / temperature);
	  const double p_gen  = std::pow((loc.size_customer() - loc.size_table() * parameter.discount), 1.0 / temperature);
	  
	  existing = sampler.bernoulli(p_gen / (p_base + p_gen));
	}
      }

      if (! existing) {
	// sample what floor...
	
	size_type floor = 0;
	const size_type num_floor = std::distance(first, last);
	
	if (num_floor > 1) {
	  double r = sampler.uniform() * p0;
	  for (/**/; floor != num_floor; ++ floor, ++ first, ++ lambda) {
	    r -= (*first) * (*lambda);
	    
	    if (r <= 0.0) break;
	  }
	}
	
	++ customers;
	++ tables;
	return loc.counts.increment_new(floor);
      } else {
	++ customers;
	return loc.counts.increment_existing(parameter.discount, sampler);
      }
    }
    
    template <typename Sampler>
    std::pair<size_type, bool> decrement(const dish_type& dish, Sampler& sampler)
    {
      typename dish_set_type::iterator diter = dishes.find(dish);
      
      if (diter == dishes.end())
	throw std::runtime_error("restaurant_floor: dish was not inserted?");
      
      location_type& loc = diter->second;
      
      const std::pair<size_type, bool> result = loc.counts.decrement(sampler);
      
      -- customers;
      tables -= result.second;
      
      return result;
    }
    
    template <typename PriorIterator, typename LambdaIterator>
    typename std::iterator_traits<PriorIterator>::value_type prob(PriorIterator first, PriorIterator last, LambdaIterator lambda) const
    {
      typedef typename std::iterator_traits<PriorIterator>::value_type P;
      
      const P p0 = std::inner_product(first, last, lambda, P(0));
      
      return P(tables * parameter.discount + parameter.strength) * p0 / P(customers + parameter.strength);
    }

    
    template <typename PriorIterator, typename LambdaIterator>
    typename std::iterator_traits<PriorIterator>::value_type prob(const dish_type& dish, PriorIterator first, PriorIterator last, LambdaIterator lambda) const
    {
      typedef typename std::iterator_traits<PriorIterator>::value_type P;
      typename dish_set_type::const_iterator diter = dishes.find(dish);
      
      const P p0 = std::inner_product(first, last, lambda, P(0));
      
      if (diter == dishes.end())
	return P(tables * parameter.discount + parameter.strength) * p0 / P(customers + parameter.strength);
      else
	return (P(diter->second.size_customer() - parameter.discount * diter->second.size_table()) + P(tables * parameter.discount + parameter.strength) * p0) / P(customers + parameter.strength);
    }
    
    template <typename PriorIterator, typename LambdaIterator>
    std::pair<typename std::iterator_traits<PriorIterator>::value_type, bool> prob_model(const dish_type& dish, PriorIterator first, PriorIterator last, LambdaIterator lambda) const
    {
      typedef typename std::iterator_traits<PriorIterator>::value_type P;
      typename dish_set_type::const_iterator diter = dishes.find(dish);
      
      const P p0 = std::inner_product(first, last, lambda, P(0));
      
      if (diter == dishes.end())
	return std::make_pair(P(tables * parameter.discount + parameter.strength) * p0 / P(customers + parameter.strength), false);
      else
	return std::make_pair((P(diter->second.size_customer() - parameter.discount * diter->second.size_table()) + P(tables * parameter.discount + parameter.strength) * p0) / P(customers + parameter.strength), true);
    }
    
    double log_likelihood() const
    {
      return log_likelihood(parameter.discount, parameter.strength);
    }
    
    // http://en.wikipedia.org/wiki/Chinese_restaurant_process
    double log_likelihood(const double& discount, const double& strength) const
    {      
      double logprob = parameter.log_likelihood(discount, strength);
      
      if (! customers) return logprob;
      
      if (discount > 0.0) {
	if (strength == 0.0)
	  logprob += tables * std::log(discount) + utils::mathop::lgamma(tables) - utils::mathop::lgamma(customers);
	else {
	  logprob += utils::mathop::lgamma(strength) - utils::mathop::lgamma(strength + customers);
	  logprob += tables * std::log(discount) + utils::mathop::lgamma(strength / discount + tables) - utils::mathop::lgamma(strength / discount);
	}
	
	const double lg = utils::mathop::lgamma(1.0 - discount);
	
	typename dish_set_type::const_iterator diter_end = dishes.end();
	for (typename dish_set_type::const_iterator diter = dishes.begin(); diter != diter_end; ++ diter) {
	  const location_type& loc = diter->second;

	  for (size_type floor = 0; floor != Floors; ++ floor) {
	    typename location_type::count_set_type::histogram_type::const_iterator titer_end = loc.counts[floor].end();
	    for (typename location_type::count_set_type::histogram_type::const_iterator titer = loc.counts[floor].begin(); titer != titer_end; ++ titer)
	      logprob += (utils::mathop::lgamma(titer->first - discount) - lg) * titer->second;
	  }
	}
      } else if (discount == 0.0) {
	logprob += utils::mathop::lgamma(strength) + tables * std::log(strength) - utils::mathop::lgamma(strength + tables);
	
	typename dish_set_type::const_iterator diter_end = dishes.end();
	for (typename dish_set_type::const_iterator diter = dishes.begin(); diter != diter_end; ++ diter)
	  logprob += utils::mathop::lgamma(diter->second.size_table());
      } else
	throw std::runtime_error("negative discount?");
      
      return logprob;
    }

    void prune()
    {
      typename dish_set_type::iterator diter_end = dishes.end();
      for (typename dish_set_type::iterator diter = dishes.begin(); diter != diter_end; /**/) {
	if (diter->second.empty())
	  dishes.erase(diter ++);
	else
	  ++ diter;
      }
    }

  private:    
    struct DiscountSampler
    {
      DiscountSampler(const restaurant_floor& __crp) : crp(__crp) {}
      
      const restaurant_floor& crp;
      
      double operator()(const double& proposed_discount) const
      {
	return crp.log_likelihood(proposed_discount, crp.strength());
      }
    };
    
    struct StrengthSampler
    {
      StrengthSampler(const restaurant_floor& __crp) : crp(__crp) {}
      
      const restaurant_floor& crp;
      
      double operator()(const double& proposed_strength) const
      {
	return crp.log_likelihood(crp.discount(), proposed_strength);
      }
    };

  public:
    template <typename Sampler>
    double sample_log_x(Sampler& sampler, const double& discount, const double& strength) const
    {
      return std::log(sample_x(sampler, discount, strength));
    }
    
    template <typename Sampler>
    double sample_x(Sampler& sampler, const double& discount, const double& strength) const
    {
      return (customers > 1 ? sampler.beta(strength + 1, customers - 1) : 1.0);
    }

    template <typename Sampler>
    double sample_y(Sampler& sampler, const double& discount, const double& strength) const
    {
      size_type y = 0;
      
      for (size_type i = 1; i < tables; ++ i)
	y += sampler.bernoulli(strength / (strength + discount * i));
      
      return y;
    }

    template <typename Sampler>
    double sample_y_inv(Sampler& sampler, const double& discount, const double& strength) const
    {
      size_type y = 0;
      
      for (size_type i = 1; i < tables; ++ i)
	y += 1 - sampler.bernoulli(strength / (strength + discount * i));
      
      return y;
    }
    
    template <typename Sampler>
    double sample_z_inv(Sampler& sampler, const double& discount, const double& strength) const
    {
      size_type z = 0;
      
      typename dish_set_type::const_iterator diter_end = dishes.end();
      for (typename dish_set_type::const_iterator diter = dishes.begin(); diter != diter_end; ++ diter) {
	const location_type& loc = diter->second;
	
	for (size_type floor = 0; floor != Floors; ++ floor) {
	  typename location_type::count_set_type::histogram_type::const_iterator titer_end = loc.counts[floor].end();
	  for (typename location_type::count_set_type::histogram_type::const_iterator titer = loc.counts[floor].begin(); titer != titer_end; ++ titer)
	    for (size_type i = 0; i != titer->second; ++ i)
	      for (size_type j = 1; j < titer->first; ++ j)
		z += 1 - sampler.bernoulli(double(j - 1) / (j - discount));
	}
      }
      
      return z;
    }

    template <typename Sampler>
    double sample_strength(Sampler& sampler, const double& discount, const double& strength) const
    {
      if (! has_strength_prior())
	throw std::runtime_error("no strength prior");
      
      const double x = sample_log_x(sampler, discount, strength);
      const double y = sample_y(sampler, discount, strength);
      
      return sampler.gamma(parameter.strength_shape + y, parameter.strength_rate - x);
    }
    
    template <typename Sampler>
    double sample_discount(Sampler& sampler, const double& discount, const double& strength) const
    {
      if (! has_discount_prior())
	throw std::runtime_error("no discount prior");
      
      const double y = sample_y_inv(sampler, discount, strength);
      const double z = sample_z_inv(sampler, discount, strength);
      
      return sampler.beta(parameter.discount_alpha + y, parameter.discount_beta + z);
    }
    
    template <typename Sampler>
    double expectation_strength(Sampler& sampler, const double& discount, const double& strength) const
    {
      const double x = sample_log_x(sampler, discount, strength);
      const double y = sample_y(sampler, discount, strength);
      
      if (has_strength_prior())
	return (parameter.strength_shape + y) / (parameter.strength_rate - x);
      else
	return - y / x;
    }
    
    template <typename Sampler>
    double expectation_discount(Sampler& sampler, const double& discount, const double& strength) const
    {
      const double y = sample_y_inv(sampler, discount, strength);
      const double z = sample_z_inv(sampler, discount, strength);
      
      if (has_discount_prior()) {
	const double a = parameter.discount_alpha + y;
	const double b = parameter.discount_beta  + z;
	
	return a / (a + b);
      } else
	return y / (y + z);
    }
    
    bool verify_parameters()
    {
      return parameter.verify_parameters();
    }
    
    template <typename Sampler>
    void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
    {
      if (! has_discount_prior() && ! has_strength_prior()) return;
      
      for (int iter = 0; iter != num_loop; ++ iter) {
	if (has_strength_prior())
	  parameter.strength = sample_strength(sampler, parameter.discount, parameter.strength);
	
	if (has_discount_prior()) 
	  parameter.discount = sample_discount(sampler, parameter.discount, parameter.strength);
      }
      
      if (has_strength_prior())
	parameter.strength = sample_strength(sampler, parameter.discount, parameter.strength);
    }
    
    template <typename Sampler>
    void slice_sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
    {
      if (! has_discount_prior() && ! has_strength_prior()) return;
      
      DiscountSampler discount_sampler(*this);
      StrengthSampler strength_sampler(*this);

      for (int iter = 0; iter != num_loop; ++ iter) {
	if (has_strength_prior())
	  parameter.strength = slice_sampler(strength_sampler,
					     parameter.strength,
					     sampler,
					     - parameter.discount + std::numeric_limits<double>::min(),
					     std::numeric_limits<double>::infinity(),
					     0.0,
					     num_iterations,
					     32 * num_iterations);
	
	if (has_discount_prior()) 
	  parameter.discount = slice_sampler(discount_sampler,
					     parameter.discount,
					     sampler,
					     (parameter.strength < 0.0 ? - parameter.strength : 0.0) + std::numeric_limits<double>::min(),
					     1.0,
					     0.0,
					     num_iterations,
					     32 * num_iterations);
      }
      
      if (has_strength_prior())
	parameter.strength = slice_sampler(strength_sampler,
					   parameter.strength,
					   sampler,
					   - parameter.discount + std::numeric_limits<double>::min(),
					   std::numeric_limits<double>::infinity(),
					   0.0,
					   num_iterations,
					   32 * num_iterations);
    }
    
  private:
    size_type      tables;
    size_type      customers;
    dish_set_type  dishes;
    parameter_type parameter;
  };
};

namespace std
{
  template <size_t F, typename T, typename H, typename P, typename A>
  inline
  void swap(utils::restaurant_floor<F,T,H,P,A>& x, utils::restaurant_floor<F,T,H,P,A>& y)
  {
    x.swap(y);
  }
};


#endif

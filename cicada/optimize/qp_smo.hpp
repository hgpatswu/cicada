// -*- mode: c++ -*-
//
//  Copyright(C) 2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__OPTIMIZE__QP_SMO__HPP__
#define __CICADA__OPTIMIZE__QP_SMO__HPP__ 1

#include <cstdlib>
#include <vector>
#include <stdexcept>

// QP solver based on the SMO:
//
// min   x^{\top} H x + f^{\top} * x
//
// sum x = C
// 0 \leq x[i] \leq C
// 
// under a tolerance threshold
//
// TODO: add working set selections

namespace cicada
{
  namespace optimize
  {
    struct QPSMO
    {
      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      
      template <typename __X, typename __F, typename __H, typename __M>
      double operator()(__X& x,
			const __F& f,
			const __H& H,
			const __M& M,
			const double C,
			const double tolerance)
      {
	typedef std::vector<double, std::allocator<double> > d_type;
	
	if (x.size() != f.size())
	  throw std::runtime_error("size does not match");
	
	const int model_size = x.size();
	int active_size = x.size();
	
	// compute gradient...
	d_type d(f.begin(), f.end());
	
	bool found_non_zero = false;
	for (int i = 0; i != model_size; ++ i)
	  if (x[i] > 0.0) {
	    found_non_zero = true;
	    for (int j = 0; j != model_size; ++ j)
	      d[j] += H(j, i) * x[i];
	  }
	
	if (! found_non_zero) {
	  // choose the first vector...
	  x[0] = C;
	  for (int j = 0; j != model_size; ++ j)
	    d[j] += H(j, 0) * x[0];
	}
	
	
	double objective_primal = 0.0;
	for (int i = 0; i != model_size; ++ i)
	  objective_primal += 0.5 * x[i] * (f[i] + d[i]);

	for (int iter = 0; iter != 1000; ++ iter) {
	  
	  // find the most violating pair
	  double F_upper = - std::numeric_limits<double>::infinity();
	  double F_lower =   std::numeric_limits<double>::infinity();
	  
	  int u = -1;
	  int v = -1;
	  
	  for (int i = 0; i != model_size; ++ i) {
	    const double F = d[i];

	    if (x[i] < C)
	      if (F < F_lower) {
		F_lower = F;
		u = i;
	      }
	    
	    if (0.0 < x[i])
	      if (F > F_upper) {
		F_upper = F;
		v = i;
	      }
	  }
	  
	  if (F_upper - F_lower <= tolerance) break;
	  
	  // SMO update of the most violating pair...
	  const double tau_lower = std::max(  - x[u], x[v] - C);
	  const double tau_upper = std::min(C - x[u], x[v]);
	  
	  double tau = (d[v] - d[u]) / (H(u, u) - 2.0 * H(v, u) + H(v, v));
	  tau = std::min(std::max(tau, tau_lower), tau_upper);
	  
	  x[u] += tau;
	  x[v] -= tau;
	  
	  // update d..
	  // size_type size_active = 0;
	  double lambda_eq = 0.0;
	  size_type size_active = 0;
	  for (int i = 0; i != model_size; ++ i) {
	    d[i] += tau * (H(i, u) - H(i, v));
	    
	    if (0.0 < x[i] && x[i] < C) {
	      lambda_eq -= d[i];
	      ++ size_active;
	    }
	  }
	  
	  // compute shrinking and re-compute active size
	  
	}
	
	objective_primal = 0.0;
	for (int i = 0; i != model_size; ++ i)
	  objective_primal += 0.5 * x[i] * (f[i] + d[i]);
	
	return objective_primal;
      }
    };
  };
};

#endif

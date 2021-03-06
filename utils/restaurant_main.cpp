#include <iostream>
#include <vector>
#include <string>

#include "restaurant.hpp"
#include "sampler.hpp"

typedef utils::restaurant<std::string> crp_type;
typedef utils::sampler<boost::mt19937> sampler_type;

std::ostream& operator<<(std::ostream& os, const utils::restaurant<std::string>& crp)
{
  typedef utils::restaurant<std::string> crp_type;
 
  os << "PYP(discount=" << crp.discount() << ",strength=" << crp.strength() << ")" << '\n'
     << "customers = " << crp.size_customer() << '\n';
  
  crp_type::const_iterator citer_end = crp.end();
  for (crp_type::const_iterator citer = crp.begin(); citer != citer_end; ++ citer) {
    os << '\t' << citer->first << " customer: " << citer->second.size_customer() << " tables: " << citer->second.size_table() << '\n';
  }
  
  return os;
}

std::ostream& operator<<(std::ostream& os, const utils::restaurant<char>& crp)
{
  typedef utils::restaurant<char> crp_type;

  os << "PYP(discount=" << crp.discount() << ",strength=" << crp.strength() << ")" << '\n'
     << "customers = " << crp.size_customer() << '\n';
  
  crp_type::const_iterator citer_end = crp.end();
  for (crp_type::const_iterator citer = crp.begin(); citer != citer_end; ++ citer) {
    os << '\t' << std::string(1, citer->first) << " customer: " << citer->second.size_customer() << " tables: " << citer->second.size_table() << '\n';
    
  }
  
  return os;
}


int main(int argc, char** argv)
{
  sampler_type sampler;

  {
    const double base(1.0 / 5);

    crp_type rest1(0.0, 1);
    
    rest1.increment("hello", base, sampler);
    rest1.increment("world", base, sampler);
    rest1.increment("!", base, sampler);
    rest1.increment("world", base, sampler);
    rest1.increment("!", base, sampler);
    rest1.increment("!", base, sampler);
    
    std::cout << "Restaurant has " << rest1.size_customer() << " customers\n";
    std::cout << rest1 << std::flush;
    
    std::cout << "prob(\"hello\") " << rest1.prob("hello", 1.0/5) << "\n";
    std::cout << "prob(\"world\") " << rest1.prob("world", 1.0/5) << "\n";
    std::cout << "prob(\"!\") " << rest1.prob("!", 1.0/5) << "\n";
    

    std::cout << "decrement(\"hello\")\n";
    rest1.decrement("hello", sampler);
    std::cout << "prob(\"hello\") " << rest1.prob("hello", 1.0/5) << "\n";
  }

  {
    typedef utils::restaurant<char> crp_type;

    crp_type rest2(0.0, 1);
    
    for (int i = 0; i < 20; ++i) {
      if (sampler.bernoulli(0.5)) rest2.increment('a', 1.0/5, sampler);
      if (sampler.bernoulli(0.5)) rest2.increment('b', 1.0/5, sampler);
      if (sampler.bernoulli(0.5)) rest2.increment('c', 1.0/5, sampler);
    }
    
    std::cout << rest2 << std::flush;
    
    for (char c = 'a'; c <= 'c'; ++c)
      std::cout << c << " prob " << rest2.prob(c, 1.0/5) 
		<< " customers " << rest2.size_customer(c) 
		<< " tables " << rest2.size_table(c) << "\n";
  }
  
  {
    crp_type crp(0.1, 5);
    
    double base = 0.25;
    int total = 0;
    total += crp.increment("hoge", base, sampler);
    total += crp.increment("foo", base, sampler);
    total += crp.increment("foo", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
    total += crp.increment("bar", base, sampler);
     
    std::cout << "total: " << total << std::endl;
    std::cout << "  P(hoge)=" << crp.prob("hoge", base) << std::endl;
    std::cout << "  P(foo)="  << crp.prob("foo", base) << std::endl;
    std::cout << "  P(bar)="  << crp.prob("bar", base) << std::endl;
    std::cout << "  P(boom)=" << crp.prob("boom", base) << std::endl;
    std::cout << "total=" << (crp.prob("hoge", base) + crp.prob("foo", base) + crp.prob("bar", base) + crp.prob("boom", base)) << std::endl;
    std::cout << "log-likelihood=" << crp.log_likelihood() << std::endl;
    std::cout << crp << std::flush;
    
    total -= crp.decrement("hoge", sampler);
    total -= crp.decrement("bar", sampler);
    std::cout << crp << std::flush;
    total -= crp.decrement("bar", sampler);
    std::cout << crp << std::flush;
  }
  
  {
    typedef utils::restaurant<int> crp_type;
    
    crp_type crp(0.5, 1);
    
    double tot = 0;
    double xt = 0;
    int cust = 10;
    std::vector<int> hist(cust + 1, 0);
    
    for (int i = 0; i < cust; ++ i)
      crp.increment(1, 1.0, sampler);
    
    const int samples = 100000;
    const bool simulate = true;
    for (int k = 0; k < samples; ++ k) {
      if (! simulate) {
        crp.clear();
        for (int i = 0; i < cust; ++ i)
	  crp.increment(1, 1.0, sampler);
	
      } else {
        const int da = sampler() * cust;
        const bool a = sampler() < 0.5;
	
        if (a) {
          for (int i = 0; i < da; ++ i)	
	    crp.increment(1, 1.0, sampler);
          for (int i = 0; i < da; ++ i)
	    crp.decrement(1, sampler);
          xt += 1.0;
        } else {
          for (int i = 0; i < da; ++ i)
	    crp.decrement(1, sampler);
	  
          for (int i = 0; i < da; ++ i)
	    crp.increment(1, 1.0, sampler);
        }
      }
      int c = crp.size_table(1);
      ++ hist[c];
      tot += c;
    }
    std::cout << "P(a) = " << (xt / samples) << std::endl;
    std::cout << "E[num tables] = " << (tot / samples) << std::endl;
    double error = std::fabs((tot / samples) - 5.4);
    std::cout << "error = " << error << std::endl;
    for (int i = 1; i <= cust; ++ i)
      std::cout << i << ' ' << hist[i] << std::endl;
  }
  
  
  {

    const double base = 0.25;
    
    crp_type crp(0.1,5.0,1,1,1,1);

    crp.slice_sample_parameters(sampler);

    std::cout << "initial slice sampled discount: " << crp.discount() << std::endl;
    std::cout << "initial slice sampled strength: " << crp.strength() << std::endl;
    
    crp.increment("hoge", base, sampler);
    crp.increment("foo", base, sampler);
    crp.increment("foo", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    crp.increment("bar", base, sampler);
    std::cout << crp.log_likelihood() << std::endl;
    
    const double discount = crp.sample_discount(sampler, crp.discount(), crp.strength());
    const double strength = crp.sample_strength(sampler, crp.discount(), crp.strength());
    
    std::cout << "sampled discount: " << discount << std::endl;
    std::cout << "sampled strength: " << strength << std::endl;

    crp.slice_sample_parameters(sampler);

    std::cout << "slice sampled discount: " << crp.discount() << std::endl;
    std::cout << "slice sampled strength: " << crp.strength() << std::endl;
  }
}

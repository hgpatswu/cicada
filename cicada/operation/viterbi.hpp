// -*- mode: c++ -*-
//
//  Copyright(C) 2011-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__OPERATION__VITERBI__HPP__
#define __CICADA__OPERATION__VITERBI__HPP__ 1


#include <cicada/operation.hpp>

namespace cicada
{
  namespace operation
  {
    class Viterbi : public Operation
    {
    public:
      Viterbi(const std::string& parameter, const int __debug);

      void operator()(data_type& data) const;
      
      void assign(const weight_set_type& __weights);

      const weights_path_type* weights;
      const weight_set_type*   weights_assigned;
  
      bool weights_one;
      bool weights_fixed;

      feature_set_type weights_extra;

      bool semiring_tropical;
      bool semiring_logprob;
      bool semiring_log;
  
      int debug;
    };

  };
};


#endif

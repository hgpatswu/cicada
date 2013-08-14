// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__OPERATION__APPLY__HPP__
#define __CICADA__OPERATION__APPLY__HPP__ 1

#include <cicada/operation.hpp>

namespace cicada
{
  namespace operation
  {

    class Apply : public Operation
    {
    public:
      Apply(const std::string& parameter,
	    const model_type& __model,
	    const int __debug);
      
      void operator()(data_type& data) const;

      void assign(const weight_set_type& __weights);

      model_type model_local;

      const model_type& model;
      const weights_path_type* weights;
      const weight_set_type*   weights_assigned;
      int size;
      double diversity;
      bool weights_one;
      bool weights_fixed;
  
      bool exact;
      bool prune;
      bool grow;
      bool grow_coarse;
      bool incremental;
  
      bool forced;
      bool sparse;
      bool dense;
      
      bool state_less;
      bool state_full;
  
      int debug;
    };

  };
};


#endif

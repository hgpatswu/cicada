// -*- mode: c++ -*-
//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__NEURON_SQUARE__HPP__
#define __CICADA__NEURON_SQUARE__HPP__ 1

#include <cmath>

#include <cicada/neuron/layer.hpp>

namespace cicada
{
  namespace neuron
  {
    class Square : public Layer
    {
    public:
      virtual void forward(const tensor_type& data_input);
      virtual void backward(const tensor_type& data_input, const tensor_type& gradient_output);
      virtual void accumulate(const tensor_type& data_input, const tensor_type& gradient_output) {}
    };
  };
};

#endif

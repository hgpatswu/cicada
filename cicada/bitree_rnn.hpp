// -*- mode: c++ -*-
//
//  Copyright(C) 2014 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__BI_TREE_RNN__HPP__
#define __CICADA__BI_TREE_RNN__HPP__ 1

// binary tree RNN model
// this is simply a place-holder for actual feature in feature directory...
// + we will share word embedding to save memory space...

#include <stdint.h>

#include <stdexcept>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/random.hpp>
#include <boost/shared_ptr.hpp>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/word_embedding.hpp>

#include <utils/bithack.hpp>
#include <utils/mathop.hpp>

#include <Eigen/Core>

namespace cicada
{
  class BiTreeRNN
  {
  public:
    typedef Symbol                  word_type;
    typedef Vocab                   vocab_type;
    
    typedef size_t             size_type;
    typedef ptrdiff_t          difference_type;
    typedef float              logprob_type;
    typedef float              parameter_type;
    typedef double             prob_type;
    
    typedef boost::filesystem::path path_type;
    
    typedef Eigen::Matrix<parameter_type, Eigen::Dynamic, Eigen::Dynamic>    tensor_type;
    typedef Eigen::Map<tensor_type>                                          matrix_type;

    typedef WordEmbedding embedding_type;

    struct shtanh
    {
      template <typename Tp>
      Tp operator()(const Tp& x) const
      {
	return __round(std::min(std::max(x, Tp(-1)), Tp(1)) * 128) / Tp(128);
      }
      
      template <typename Tp>
      inline
      Tp __round(const Tp& x) const
      {
	return round(x);
      }
      
      inline
      float __round(const float& x) const
      {
	return roundf(x);
      }
      
      inline
      long double __round(const long double& x) const
      {
	return roundl(x);
      }
    };
    
    struct dshtanh
    {
      template <typename Tp>
      Tp operator()(const Tp& x) const
      {
	return Tp(- 1) < x && x < Tp(1);
      }
    };
    
  public:
    BiTreeRNN()
      : hidden_(0), embedding_(0), source_(0), target_(0) {}
    BiTreeRNN(const path_type& path)
    { open(path); }
    BiTreeRNN(const size_type& hidden, const size_type& embedding)
    { initialize(hidden, embedding); }
    BiTreeRNN(const size_type& hidden, const size_type& embedding, const path_type& path_source, const path_type& path_target)
    { open(hidden, embedding, path_source, path_target); }
    
    void write(const path_type& path) const;
    
    void open(const path_type& path);
    void open(const size_type& hidden, const size_type& embedding, const path_type& path_source, const path_type& path_target);

    void initialize(const size_type& hidden, const size_type& embedding);

  private:
    template <typename Gen>
    struct __randomize
    {
      __randomize(Gen& gen, const double range=0.01) : gen_(gen), range_(range) {}
      
      template <typename Tp>
      Tp operator()(const Tp& x) const
      {
	return boost::random::uniform_real_distribution<Tp>(-range_, range_)(const_cast<Gen&>(gen_));
      }
      
      Gen& gen_;
      double range_;
    };
    
  public:
    template <typename Gen>
    void random(Gen& gen)
    {
      const double range_p = std::sqrt(6.0 / (hidden_ + hidden_ + embedding_ + embedding_));
      const double range_s = std::sqrt(6.0 / (hidden_ + hidden_ + embedding_));
      const double range_t = std::sqrt(6.0 / (hidden_ + hidden_ + embedding_));
      const double range_n = std::sqrt(6.0 / (hidden_ + hidden_ + hidden_));
      
      Wp_ = Wp_.array().unaryExpr(__randomize<Gen>(gen, range_p));
      Ws_ = Ws_.array().unaryExpr(__randomize<Gen>(gen, range_s));
      Wt_ = Wt_.array().unaryExpr(__randomize<Gen>(gen, range_t));
      Wn_ = Wn_.array().unaryExpr(__randomize<Gen>(gen, range_n));
    }

    void clear()
    {
      Wp_.setZero();
      Bp_.setZero();
      
      Ws_.setZero();
      Bs_.setZero();
      
      Wt_.setZero();
      Bt_.setZero();

      Wn_.setZero();
      Bn_.setZero();
      
      Bi_.setZero();
    }

    void swap(BiTreeRNN& x)
    {
      std::swap(hidden_,    x.hidden_);
      std::swap(embedding_, x.embedding_);

      Wp_.swap(x.Wp_);
      Bp_.swap(x.Bp_);

      Ws_.swap(x.Ws_);
      Bs_.swap(x.Bs_);
      
      Wt_.swap(x.Wt_);
      Bt_.swap(x.Bt_);

      Wn_.swap(x.Wn_);
      Bn_.swap(x.Bn_);

      Bi_.swap(x.Bi_);

      std::swap(source_, x.source_);
      std::swap(target_, x.target_);
    }

  public:
    friend
    std::ostream& operator<<(std::ostream& os, const BiTreeRNN& rnn);
    friend
    std::istream& operator>>(std::istream& is, BiTreeRNN& rnn);
    
  public:
    template <typename Tp>
    BiTreeRNN& operator*=(const Tp& x)
    {
      Wp_ *= x;
      Bp_ *= x;
      
      Ws_ *= x;
      Bs_ *= x;
      
      Wt_ *= x;
      Bt_ *= x;
      
      Wn_ *= x;
      Bn_ *= x;
      
      Bi_ *= x;

      return *this;
    }

    BiTreeRNN& operator+=(const BiTreeRNN& x)
    {
      Wp_ += x.Wp_;
      Bp_ += x.Bp_;

      Ws_ += x.Ws_;
      Bs_ += x.Bs_;
      
      Wt_ += x.Wt_;
      Bt_ += x.Bt_;
      
      Wn_ += x.Wn_;
      Bn_ += x.Bn_;
      
      Bi_ += x.Bi_;

      return *this;
    }

    BiTreeRNN& operator-=(const BiTreeRNN& x)
    {
      Wp_ -= x.Wp_;
      Bp_ -= x.Bp_;

      Ws_ -= x.Ws_;
      Bs_ -= x.Bs_;
      
      Wt_ -= x.Wt_;
      Bt_ -= x.Bt_;
      
      Wn_ -= x.Wn_;
      Bn_ -= x.Bn_;
      
      Bi_ -= x.Bi_;

      return *this;
    }

  public:
    size_type hidden_;
    size_type embedding_;
    
    // binary rule for terminal-pairs
    tensor_type Wp_;
    tensor_type Bp_;

    // binary rule for source (w/o target)
    tensor_type Ws_;
    tensor_type Bs_;

    // binary rule for target (w/o source)
    tensor_type Wt_;
    tensor_type Bt_;
    
    // binary rule for non-terminal
    tensor_type Wn_;
    tensor_type Bn_;
    
    // bias for initial state
    tensor_type Bi_;
    
    const embedding_type* source_;
    const embedding_type* target_;
  };
};

namespace std
{
  inline
  void swap(cicada::BiTreeRNN& x, cicada::BiTreeRNN& y)
  {
    x.swap(y);
  }
  
};

#endif

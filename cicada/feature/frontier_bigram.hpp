// -*- mode: c++ -*-
//
//  Copyright(C) 2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__FEATURE__FRONTIER_BIGRAM__HPP__
#define __CICADA__FEATURE__FRONTIER_BIGRAM__HPP__ 1

#include <string>

#include <cicada/feature_function.hpp>

namespace cicada
{
  namespace feature
  {
    class FrontierBigramImpl;
    
    class FrontierBigram : public FeatureFunction
    {
    public:
      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      
      typedef cicada::Symbol symbol_type;
      typedef cicada::Vocab  vocab_type;
      
      
    private:
      typedef FeatureFunction base_type;
      typedef FrontierBigramImpl impl_type;
      
    public:
      // parameter = key:[key=value (delimited by ',')]*
      
      // ngram parameter = ngram:file=file-name,name=feature-name,order=5
      // "ngram" is the key for this ngram-feature
      // file: file name
      // name: name of this feature function. default to ngram
      // order: ngram's order
      
      FrontierBigram(const std::string& parameter);
      FrontierBigram(const FrontierBigram&);
      ~FrontierBigram();
      
      FrontierBigram& operator=(const FrontierBigram&);

    private:
      FrontierBigram() {}
      
    public:
      virtual void apply(state_ptr_type& state,
			 const state_ptr_set_type& states,
			 const edge_type& edge,
			 feature_set_type& features,
			 const bool final) const;
      virtual void apply_coarse(state_ptr_type& state,
				const state_ptr_set_type& states,
				const edge_type& edge,
				feature_set_type& features,
				const bool final) const;
      virtual void apply_predict(state_ptr_type& state,
				 const state_ptr_set_type& states,
				 const edge_type& edge,
				 feature_set_type& features,
				 const bool final) const;
      virtual void apply_scan(state_ptr_type& state,
			      const state_ptr_set_type& states,
			      const edge_type& edge,
			      const int dot,
			      feature_set_type& features,
			      const bool final) const;
      virtual void apply_complete(state_ptr_type& state,
				  const state_ptr_set_type& states,
				  const edge_type& edge,
				  feature_set_type& features,
				  const bool final) const;

      virtual void initialize();
      
      virtual feature_function_ptr_type clone() const { return feature_function_ptr_type(new FrontierBigram(*this)); }
      
    private:
      
      impl_type* pimpl;
    };
  };
};

#endif

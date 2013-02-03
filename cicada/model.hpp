// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

// a model composed of many feature functions


#ifndef __CICADA__MODEL__HPP__
#define __CICADA__MODEL__HPP__ 1

#include <cicada/feature_function.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/lattice.hpp>
#include <cicada/span_vector.hpp>
#include <cicada/sentence_vector.hpp>
#include <cicada/ngram_count_set.hpp>

#include <boost/shared_ptr.hpp>

#include <utils/hashmurmur3.hpp>

namespace cicada
{
  class StateAllocator;

  class Model
  {
  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef cicada::Symbol     symbol_type;
    typedef cicada::Feature    feature_type;
    typedef cicada::HyperGraph hypergraph_type;
    typedef cicada::Lattice    lattice_type;
    typedef cicada::SpanVector span_set_type;

    typedef cicada::SentenceVector sentence_set_type;
    typedef cicada::NGramCountSet  ngram_count_set_type;
    
    typedef hypergraph_type::node_type node_type;
    typedef hypergraph_type::edge_type edge_type;
    typedef hypergraph_type::feature_set_type feature_set_type;

  public:
    struct State
    {
      friend class StateAllocator;
      friend class Model;
    
      friend struct state_hash;
      friend struct state_equal;
      friend struct state_less;
    
      typedef char  value_type;
      typedef char* pointer;
      
    private:
      State(value_type* __base) : base(__base) {}
    public:
      State() : base(0) {}

      bool empty() const { return base == 0; }
    
    private:
      pointer base;
    };
    typedef State state_type;
    typedef std::vector<state_type, std::allocator<state_type> > state_set_type;
    
    typedef FeatureFunction                          feature_function_type;
    typedef boost::shared_ptr<feature_function_type> feature_function_ptr_type;

  public:
    struct state_unassigned
    {
      state_type operator()() const { return state_type(); }
    };

    struct state_hash : public utils::hashmurmur3<size_t>
    {
      typedef utils::hashmurmur3<size_t> hasher_type;
      
      state_hash(size_t __state_size)
	: state_size(__state_size) {}
    
      size_t operator()(const state_type& x) const
      {
	return (x.base == 0 ? size_t(0) : hasher_type::operator()(x.base, x.base + state_size, 0));
      }
    
      size_t state_size;
    };
  
    struct state_equal
    {
      state_equal(size_t __state_size)
	: state_size(__state_size) {}
      
      bool operator()(const state_type& x, const state_type& y) const
      {
	return x.base == y.base || (x.base && y.base && std::equal(x.base, x.base + state_size, y.base));
      }
    
      size_t state_size;
    };

    struct state_less
    {
      state_less(size_t __state_size)
	: state_size(__state_size) {}
      
      bool operator()(const state_type& x, const state_type& y) const
      {
	return (! x.base && y.base) || (x.base && y.base && std::lexicographical_compare(x.base, x.base + state_size, y.base, y.base + state_size));
      }
    
      size_t state_size;
    };


  private:
    typedef StateAllocator state_allocator_type;
    typedef std::vector<feature_function_ptr_type, std::allocator<feature_function_ptr_type > > model_set_type;

  public:
    typedef model_set_type::reference       reference;
    typedef model_set_type::const_reference const_reference;

    typedef model_set_type::const_iterator iterator;
    typedef model_set_type::const_iterator const_iterator;
    
  public:
    Model();
    Model(const feature_function_ptr_type& x);
    Model(const Model& x);
    ~Model();
    
    Model& operator=(const Model& x);

  public:
    
    // score with states
    state_type apply(const state_set_type& node_states,
		     const edge_type& edge,
		     feature_set_type& features,
		     const bool final=false) const;
    
    // score with coarse states
    state_type apply_coarse(const state_set_type& node_states,
			    const edge_type& edge,
			    feature_set_type& features,
			    const bool final=false) const;
    
    // score by incremental application
    void apply_predict(state_type& state,
		       const state_set_type& node_states,
		       const edge_type& edge,
		       feature_set_type& features,
		       const bool final=false) const;
    void apply_scan(state_type& state,
		    const state_set_type& node_states,
		    const edge_type& edge,
		    const int dot,
		    feature_set_type& features,
		    const bool final=false) const;
    void apply_complete(state_type& state,
			const state_set_type& node_states,
			const edge_type& edge,
			feature_set_type& features,
			const bool final=false) const;
    
    void deallocate(const state_type& state) const;
    state_type clone(const state_type& state) const;
    
    const_reference operator[](size_type pos) const { return models[pos]; }
    reference operator[](size_type pos) { return models[pos]; }

    const_iterator begin() const { return models.begin(); }
    iterator begin() { return models.begin(); }

    const_iterator end() const { return models.end(); }
    iterator end() { return models.end(); }
    
    // you should call this at least once, when you are going to use this model.
    void initialize();
    
    bool empty() const { return models.empty(); }
    size_type size() const { return models.size(); }
    
    void push_back(const feature_function_ptr_type& x)
    {
      models.push_back(x);
    }
    
    void clear()
    {
      models.clear();
      offsets.clear();
      states_size = 0;
    }
    

    void swap(Model& x)
    {
      models.swap(x.models);
      std::swap(allocator, x.allocator);
      
      offsets.swap(x.offsets);
      std::swap(states_size, x.states_size);
    }
    
    bool is_stateless() const { return states_size == 0; }

    size_type state_size() const { return states_size; }
    
    // a flag to control whether to apply feature already in feature set or not...
    void apply_feature(const bool mode)
    {
      model_set_type::iterator iter_end = models.end();
      for (model_set_type::iterator iter = models.begin(); iter != iter_end; ++ iter)
	(*iter)->apply_feature() = mode;
    }
    
    void assign(const size_type& id,
		const hypergraph_type& hypergraph,
		const lattice_type& lattice,
		const span_set_type& spans,
		const sentence_set_type& targets,
		const ngram_count_set_type& ngram_counts)
    {
      model_set_type::iterator iter_end = models.end();
      for (model_set_type::iterator iter = models.begin(); iter != iter_end; ++ iter)
	(*iter)->assign(id, hypergraph, lattice, spans, targets, ngram_counts);
    }
    
    Model clone() const
    {
      Model __model(*this);
      
      __model.models.clear();
      model_set_type::const_iterator iter_end = models.end();
      for (model_set_type::const_iterator iter = models.begin(); iter != iter_end; ++ iter)
	__model.push_back((*iter)->clone());
      
      return __model;
    }
    
  private:
    typedef std::vector<size_type, std::allocator<size_type> > offset_set_type;
    
  private:
    model_set_type        models;
    state_allocator_type* allocator;

    // offsets used to compute states...
    offset_set_type offsets;
    int             states_size;
  };
};

namespace std
{
  inline
  void swap(cicada::Model& x, cicada::Model& y)
  {
    x.swap(y);
  }
};


#endif

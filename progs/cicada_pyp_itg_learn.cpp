//
//  Copyright(C) 2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

// PYP-PIALIGN!
// based on

// @InProceedings{neubig-EtAl:2011:ACL-HLT2011,
//   author    = {Neubig, Graham  and  Watanabe, Taro  and  Sumita, Eiichiro  and  Mori, Shinsuke  and  Kawahara, Tatsuya},
//   title     = {An Unsupervised Model for Joint Phrase Alignment and Extraction},
//   booktitle = {Proceedings of the 49th Annual Meeting of the Association for Computational Linguistics: Human Language Technologies},
//   month     = {June},
//   year      = {2011},
//   address   = {Portland, Oregon, USA},
//   publisher = {Association for Computational Linguistics},
//   pages     = {632--641},
//   url       = {http://www.aclweb.org/anthology/P11-1064}
// }


// parsing by
//
// @InProceedings{saers-nivre-wu:2009:IWPT09,
//   author    = {Saers, Markus  and  Nivre, Joakim  and  Wu, Dekai},
//   title     = {Learning Stochastic Bracketing Inversion Transduction Grammars with a Cubic Time Biparsing Algorithm},
//   booktitle = {Proceedings of the 11th International Conference on Parsing Technologies (IWPT'09)},
//   month     = {October},
//   year      = {2009},
//   address   = {Paris, France},
//   publisher = {Association for Computational Linguistics},
//   pages     = {29--32},
//   url       = {http://www.aclweb.org/anthology/W09-3804}
// }

// an extension: we employ infinite-ITG by memorizing the permutation
//
// we use std::string (or, symbol?) as an indiction of "permutation" flag
//

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/adapted.hpp>

#include <map>
#include <deque>
#include <iterator>
#include <numeric>
#include <queue>

#include <cicada/sentence.hpp>
#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/semiring/logprob.hpp>

#include "utils/vector2.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/pyp_parameter.hpp"
#include "utils/alloc_vector.hpp"
#include "utils/chart.hpp"
#include "utils/bichart.hpp"
#include "utils/chunk_vector.hpp"
#include "utils/array_power2.hpp"
#include "utils/resource.hpp"
#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/mathop.hpp"
#include "utils/bithack.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/restaurant.hpp"
#include "utils/restaurant_vector.hpp"
#include "utils/unordered_map.hpp"
#include "utils/unordered_set.hpp"
#include "utils/dense_hash_map.hpp"
#include "utils/dense_hash_set.hpp"
#include "utils/compact_trie_dense.hpp"
#include "utils/sampler.hpp"
#include "utils/repository.hpp"
#include "utils/packed_device.hpp"
#include "utils/packed_vector.hpp"
#include "utils/succinct_vector.hpp"
#include "utils/simple_vector.hpp"
#include "utils/symbol_set.hpp"
#include "utils/unique_set.hpp"
#include "utils/rwticket.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/fusion/tuple.hpp>
#include <boost/array.hpp>

typedef cicada::Vocab      vocab_type;
typedef cicada::Sentence   sentence_type;
typedef cicada::Symbol     symbol_type;
typedef cicada::Symbol     word_type;
typedef cicada::HyperGraph hypergraph_type;

struct PYP
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  typedef enum {
    TERMINAL = 0,
    STRAIGHT,
    INVERTED,
  } itg_type;

  struct word_pair_type
  {
    word_type source;
    word_type target;
    
    word_pair_type() : source(), target() {}
    word_pair_type(const word_type& __source, const word_type& __target)
      : source(__source), target(__target) {}
    
    friend
    bool operator==(const word_pair_type& x, const word_pair_type& y)
    {
      return x.source == y.source && x.target == y.target;
    }
    
    friend
    size_t hash_value(word_pair_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      return hasher_type()(x.source.id(), x.target.id());
    }
  };

  struct span_type
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    
    size_type first;
    size_type last;
    
    span_type() : first(0), last(0) {}
    span_type(const size_type& __first, const size_type& __last)
      : first(__first), last(__last) { }

    bool empty() const { return first == last; }
    size_type size() const { return last - first; }
    
    friend
    bool operator==(const span_type& x, const span_type& y)
    {
      return x.first == y.first && x.last == y.last;
    }
    
    friend
    size_t hash_value(span_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      return hasher_type()(x.first, x.last);
    }

    friend
    bool disjoint(const span_type& x, const span_type& y)
    {
      return x.last <= y.first || y.last <= x.first;
    }

    friend
    bool adjacent(const span_type& x, const span_type& y)
    {
      return x.last == y.first || y.last == x.first;
    }
    
  };

  struct span_pair_type
  {
    typedef span_type::size_type      size_type;
    typedef span_type::difference_type difference_type;

    span_type source;
    span_type target;
    
    span_pair_type() : source(), target() {}
    span_pair_type(const span_type& __source, const span_type& __target)
      : source(__source), target(__target) {}
    span_pair_type(size_type s1, size_type s2, size_type t1, size_type t2)
      : source(s1, s2), target(t1, t2) {}
    
    bool empty() const { return source.empty() && target.empty(); }
    size_type size() const { return source.size() + target.size(); }
    
    friend
    bool operator==(const span_pair_type& x, const span_pair_type& y)
    {
      return x.source == y.source && x.target == y.target;
    }
    
    friend
    size_t hash_value(span_pair_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      return hasher_type()(x);
    }
    
    friend
    bool disjoint(const span_pair_type& x, const span_pair_type& y)
    {
      return disjoint(x.source, y.source) && disjoint(x.target, y.target);
    }

    friend
    bool adjacent(const span_pair_type& x, const span_pair_type& y)
    {
      return adjacent(x.source, y.source) && adjacent(x.target, y.target);
    }
    
  };

  // How to differentiate base or generative?
  struct rule_type
  {
    span_pair_type span;
    span_pair_type left;
    span_pair_type right;
    itg_type       itg;

    rule_type()
      : span(), left(), right(), itg() {}
    rule_type(const span_pair_type& __span, const itg_type& __itg)
      : span(__span), left(), right(), itg(__itg) {}
    rule_type(const span_pair_type& __span, const span_pair_type& __left, const span_pair_type& __right, const itg_type& __itg)
      : span(__span), left(__left), right(__right), itg(__itg) {}
    
    bool is_terminal() const { return left.empty() && right.empty(); }
    bool is_straight() const { return ! is_terminal() && left.target.last  == right.target.first; }
    bool is_inverted() const { return ! is_terminal() && left.target.first == right.target.last; }
  };

  struct phrase_type
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    
    typedef const word_type* iterator;
    typedef const word_type* const_iterator;
    
    phrase_type() : first(0), last(0) {}
    phrase_type(const_iterator __first, const_iterator __last)
      : first(__first), last(__last) { }
    template <typename Iterator>
    phrase_type(Iterator __first, Iterator __last)
      : first(&(*__first)), last(&(*__last)) { }
    
    bool empty() const { return first == last; }
    size_type size() const { return last - first; }
    
    const_iterator begin() const { return first; }
    const_iterator end() const { return last; }
    
    friend
    bool operator==(const phrase_type& x, const phrase_type& y)
    {
      return (x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin()));
    }
    
    friend
    size_t hash_value(phrase_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      return hasher_type()(x.begin(), x.end(), 0);
    }
    
    friend
    std::ostream& operator<<(std::ostream& os, const phrase_type& x)
    {
      if (! x.empty()) {
	std::copy(x.begin(), x.end() - 1, std::ostream_iterator<word_type>(os, " "));
	os << *(x.end() - 1);
      }
      return os;
    }
    
    const_iterator first;
    const_iterator last;
  };

  struct phrase_pair_type
  {
    phrase_type source;
    phrase_type target;
    
    phrase_pair_type() : source(), target() {}
    phrase_pair_type(const phrase_type& __source, const phrase_type& __target)
      : source(__source), target(__target) {}
    template <typename IteratorSource, typename IteratorTarget>
    phrase_pair_type(IteratorSource source_first, IteratorSource source_last,
		     IteratorTarget target_first, IteratorTarget target_last)
      : source(source_first, source_last), target(target_first, target_last) {}
    
    friend
    bool operator==(const phrase_pair_type& x, const phrase_pair_type& y)
    {
      return x.source == y.source && x.target == y.target;
    }
    
    friend
    size_t hash_value(phrase_pair_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
      
      return hasher_type()(x.source.begin(), x.source.end(), hasher_type()(x.target.begin(), x.target.end(), 0));
    }
  };
  
  struct non_terminal_trie_type
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef uint32_t  id_type;
    
    typedef std::vector<id_type, std::allocator<id_type> > id_set_type;
    
    //
    // multiple of 4: L, R, l, r
    //
    
    void initialize(const size_type depth)
    {
      parent_.clear();
      child_.clear();
      last_.clear();
      
      // 4, 16, 64, 256, 1024 ...
      
      // compute parent_ and last_
      parent_.resize(4, id_type(-1));
      last_.push_back(parent_.size());
      id_type prev = 0;
      for (size_type order = 0; order != depth; ++ order) {
	id_type last = parent_.size();
	for (id_type id = prev; id != last; ++ id) {
	  child_.push_back(parent_.size());
	  parent_.resize(parent_.size() + 4, id);
	}
	
	prev = last;
	last_.push_back(parent_.size());
      }
    }
    
    id_set_type parent_;
    id_set_type child_;
    id_set_type last_;
  };
};

struct PYPLexicon
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;
  
  typedef cicada::semiring::Logprob<double> logprob_type;
  typedef double prob_type;
  
  typedef PYP::phrase_type      phrase_type;
  typedef PYP::phrase_pair_type phrase_pair_type;
  typedef PYP::word_pair_type word_pair_type;

  typedef utils::pyp_parameter parameter_type;
  
  typedef utils::restaurant<word_pair_type, boost::hash<word_pair_type>, std::equal_to<word_pair_type>,
			    std::allocator<word_pair_type > > table_type;
  
  PYPLexicon(const parameter_type& parameter,
	     const double __p0)
    : table(parameter), p0(__p0), counts0(0)
  { }
  
  template <typename Sampler>
  void increment(const phrase_type& source, const phrase_type& target, Sampler& sampler, const double temperature=1.0)
  {
    if (source.empty() && target.empty())
      throw std::runtime_error("invalid phrase pair");
    
    if (source.empty()) {
      phrase_type::const_iterator titer_end = target.end();
      for (phrase_type::const_iterator titer = target.begin(); titer != titer_end; ++ titer)
	counts0 += table.increment(word_pair_type(vocab_type::EPSILON, *titer), p0, sampler, temperature);
      
    } else if (target.empty()) {
      phrase_type::const_iterator siter_end = source.end();
      for (phrase_type::const_iterator siter = source.begin(); siter != siter_end; ++ siter)
	counts0 += table.increment(word_pair_type(*siter, vocab_type::EPSILON), p0, sampler, temperature);
      
    } else {
      phrase_type::const_iterator siter_end = source.end();
      for (phrase_type::const_iterator siter = source.begin(); siter != siter_end; ++ siter) {
	phrase_type::const_iterator titer_end = target.end();
	for (phrase_type::const_iterator titer = target.begin(); titer != titer_end; ++ titer)
	  counts0 += table.increment(word_pair_type(*siter, *titer), p0, sampler, temperature);
      }
    }
  }

  template <typename Sampler>
  void decrement(const phrase_type& source, const phrase_type& target, Sampler& sampler)
  {
    if (source.empty() && target.empty())
      throw std::runtime_error("invalid phrase pair");

    if (source.empty()) {
      phrase_type::const_iterator titer_end = target.end();
      for (phrase_type::const_iterator titer = target.begin(); titer != titer_end; ++ titer)
	counts0 -= table.decrement(word_pair_type(vocab_type::EPSILON, *titer), sampler);
      
    } else if (target.empty()) {
      phrase_type::const_iterator siter_end = source.end();
      for (phrase_type::const_iterator siter = source.begin(); siter != siter_end; ++ siter)
	counts0 -= table.decrement(word_pair_type(*siter, vocab_type::EPSILON), sampler);
      
    } else {
      phrase_type::const_iterator siter_end = source.end();
      for (phrase_type::const_iterator siter = source.begin(); siter != siter_end; ++ siter) {
	phrase_type::const_iterator titer_end = target.end();
	for (phrase_type::const_iterator titer = target.begin(); titer != titer_end; ++ titer)
	  counts0 -= table.decrement(word_pair_type(*siter, *titer), sampler);
      }
    }
  }

  double prob(const word_type& source, const word_type& target) const
  {
    return table.prob(word_pair_type(source, target), p0);
  }
  
  double log_likelihood() const
  {
    return std::log(p0) * counts0 + table.log_likelihood();
  }

  template <typename Sampler>
  void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    table.sample_parameters(sampler, num_loop, num_iterations);
  }

  template <typename Sampler>
  void slice_sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    table.slice_sample_parameters(sampler, num_loop, num_iterations);
  }
  
  table_type table;
  double    p0;
  size_type counts0;
};

struct PYPRule
{
  // rule selection probabilities
  // terminal, stright, inversion
  
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;
  
  
  typedef PYP::phrase_type      phrase_type;
  typedef PYP::phrase_pair_type phrase_pair_type;

  typedef PYP::rule_type rule_type;
  
  typedef PYP::itg_type itg_type;

  typedef uint32_t id_type;
  
  typedef cicada::semiring::Logprob<double> logprob_type;
  typedef double prob_type;
  
  typedef utils::pyp_parameter parameter_type;
  typedef utils::restaurant_vector<> table_type;

  struct node_type
  {
    node_type() : table(), parent(id_type(-1)), order(0) {}
    
    table_type table;
    id_type    parent;
    int        order;
  };
  
  typedef utils::compact_trie_dense<char, node_type, boost::hash<char>, std::equal_to<char>,
				    std::allocator<std::pair<const char, node_type> > > trie_type;

  typedef std::vector<parameter_type, std::allocator<parameter_type> > parameter_set_type;

  PYPRule(const parameter_type& parameter)
    : table(parameter), p0(1.0 / 3), counts0(0) {}

  template <typename Sampler>
  void increment(const rule_type& rule, Sampler& sampler, const double temperature=1.0)
  {
    const itg_type itg = (rule.is_terminal() ? PYP::TERMINAL : (rule.is_straight() ? PYP::STRAIGHT : PYP::INVERTED));
    
    if (table.increment(itg, p0, sampler, temperature))
      ++ counts0;
  }
  
  template <typename Sampler>
  void decrement(const rule_type& rule, Sampler& sampler)
  {
    const itg_type itg = (rule.is_terminal() ? PYP::TERMINAL : (rule.is_straight() ? PYP::STRAIGHT : PYP::INVERTED));
    
    if (table.decrement(itg, sampler))
      -- counts0;
  }
  
  double prob(const rule_type& rule) const
  {
    const itg_type itg = (rule.is_terminal() ? PYP::TERMINAL : (rule.is_straight() ? PYP::STRAIGHT : PYP::INVERTED));
    
    return table.prob(itg, p0);
  }
  
  double prob_terminal() const
  {
    return table.prob(PYP::TERMINAL, p0);
  }

  double prob_straight() const
  {
    return table.prob(PYP::STRAIGHT, p0);
  }

  double prob_inverted() const
  {
    return table.prob(PYP::INVERTED, p0);
  }
  
  double log_likelihood() const
  {
    return table.log_likelihood() + std::log(p0) * counts0;
  }
  
  template <typename Sampler>
  void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    table.sample_parameters(sampler, num_loop, num_iterations);
  }
  
  template <typename Sampler>
  void slice_sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    table.slice_sample_parameters(sampler, num_loop, num_iterations);
  }
  
  table_type table;
  double     p0;
  size_type  counts0;
};

struct PYPITG
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;

  typedef PYP::phrase_type      phrase_type;
  typedef PYP::phrase_pair_type phrase_pair_type;
  
  typedef PYP::rule_type rule_type;
  typedef PYP::itg_type  itg_type;

  typedef cicada::semiring::Logprob<double> logprob_type;
  typedef double prob_type;

  typedef utils::rwticket mutex_type;
  
  PYPITG(const PYPRule&   __rule,
	 const PYPLexicon& __lexicon)
    : rule(__rule), lexicon(__lexicon) {}

  template <typename Sampler>
  void increment(const sentence_type& source, const sentence_type& target, const rule_type& r, Sampler& sampler, const double temperature)
  {
    rule.increment(r, sampler, temperature);
    
    lexicon.increment(phrase_type(source.begin() + r.span.source.first, source.begin() + r.span.source.last),
		      phrase_type(target.begin() + r.span.target.first, target.begin() + r.span.target.last),
		      sampler,
		      temperature);
  }

  template <typename Sampler>
  void decrement(const sentence_type& source, const sentence_type& target, const rule_type& r, Sampler& sampler)
  {
    rule.decrement(r, sampler);
    
    lexicon.decrement(phrase_type(source.begin() + r.span.source.first, source.begin() + r.span.source.last),
		      phrase_type(target.begin() + r.span.target.first, target.begin() + r.span.target.last),
		      sampler);
  }

  double log_likelihood() const
  {
    return rule.log_likelihood() + lexicon.log_likelihood();
  }
  
  template <typename Sampler>
  void sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    rule.sample_parameters(sampler, num_loop, num_iterations);

    lexicon.sample_parameters(sampler, num_loop, num_iterations);
  }
  
  template <typename Sampler>
  void slice_sample_parameters(Sampler& sampler, const int num_loop = 2, const int num_iterations = 8)
  {
    rule.slice_sample_parameters(sampler, num_loop, num_iterations);
    
    lexicon.slice_sample_parameters(sampler, num_loop, num_iterations);
  }
  
  PYPRule   rule;
  PYPLexicon lexicon;
  
  mutex_type mutex;
};

struct PYPGraph
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;

  typedef PYP::phrase_type      phrase_type;
  typedef PYP::phrase_pair_type phrase_pair_type;
  
  typedef PYP::span_type      span_type;
  typedef PYP::span_pair_type span_pair_type;
  
  typedef PYP::rule_type rule_type;
  
  typedef std::vector<rule_type, std::allocator<rule_type> > derivation_type;

  typedef PYPLexicon::logprob_type logprob_type;
  typedef PYPLexicon::prob_type    prob_type;
  
  typedef std::vector<prob_type, std::allocator<prob_type> >       prob_set_type;
  
  typedef utils::bichart<logprob_type, std::allocator<logprob_type> > chart_type;
  typedef utils::bichart<logprob_type, std::allocator<logprob_type> > base_type;
  
  struct edge_type
  {
    rule_type    rule;
    logprob_type prob;
    
    edge_type() : rule(), prob() {}
    edge_type(const rule_type& __rule, const logprob_type& __prob)
      : rule(__rule), prob(__prob) {}
  };
  
  typedef std::vector<edge_type, std::allocator<edge_type> > edge_set_type;
  typedef utils::bichart<edge_set_type, std::allocator<edge_set_type> > edge_chart_type;
  
  typedef std::vector<span_pair_type, std::allocator<span_pair_type> > span_pair_set_type;
  typedef std::vector<span_pair_set_type, std::allocator<span_pair_set_type> > agenda_type;
  
  typedef std::vector<span_pair_type, std::allocator<span_pair_type> > stack_type;

  typedef std::pair<span_pair_type, span_pair_type> span_pairs_type;
  typedef utils::dense_hash_set<span_pairs_type, utils::hashmurmur<size_t>, std::equal_to<span_pairs_type>,
				std::allocator<span_pairs_type> >::type span_pairs_unique_type;

  typedef utils::chart<logprob_type, std::allocator<logprob_type> > chart_mono_type;
  typedef std::vector<logprob_type, std::allocator<logprob_type> > alpha_type;
  typedef std::vector<logprob_type, std::allocator<logprob_type> > beta_type;

  typedef utils::vector2<logprob_type, std::allocator<logprob_type> > matrix_type;

  typedef std::pair<logprob_type, span_pair_type> score_span_pair_type;
  typedef std::vector<score_span_pair_type, std::allocator<score_span_pair_type> > heap_type;

  
  void initialize(const sentence_type& source,
		  const sentence_type& target,
		  const PYPITG& model)
  {
    //std::cerr << "initialize" << std::endl;
    matrix.clear();
    matrix.reserve(source.size() + 1, target.size() + 1);
    matrix.resize(source.size() + 1, target.size() + 1);
    
    logprob_term = model.rule.prob_terminal();
    logprob_str  = model.rule.prob_straight();
    logprob_inv  = model.rule.prob_inverted();

    for (size_type src = 0; src <= source.size(); ++ src)
      for (size_type trg = (src == 0); trg <= target.size(); ++ trg)
	matrix(src, trg) = model.lexicon.prob(src == 0 ? vocab_type::EPSILON : source[src - 1],
					      trg == 0 ? vocab_type::EPSILON : target[trg - 1]);
  }
  
  void forward_backward(const sentence_type& sentence, const chart_mono_type& chart, alpha_type& alpha, beta_type& beta)
  {
    // forward...
    alpha[0] = 1.0;
    for (size_type last = 1; last <= sentence.size(); ++ last)
      for (size_type first = 0; first != last; ++ first)
	alpha[last] = std::max(alpha[last], alpha[first] * chart(first, last));
    
    // backward...
    beta[sentence.size()] = 1.0;
    for (difference_type first = sentence.size() - 1; first >= 0; -- first)
      for (size_type last = first + 1; last <= sentence.size(); ++ last)
	beta[first] = std::max(beta[first], chart(first, last) * beta[last]);
  }

  
  // sort by less so that we can pop from a greater item
  struct heap_compare
  {
    bool operator()(const score_span_pair_type& x, const score_span_pair_type& y) const
    {
      return x.first < y.first;
    }
  };
  
  // forward filtering
  std::pair<logprob_type, bool> forward(const sentence_type& source,
					const sentence_type& target,
					const logprob_type beam)
  {
    //std::cerr << "forward" << std::endl;

    
    // initialize...
    chart.clear();
    chart.reserve(source.size() + 1, target.size() + 1);
    chart.resize(source.size() + 1, target.size() + 1);
    
    edges.clear();
    edges.reserve(source.size() + 1, target.size() + 1);
    edges.resize(source.size() + 1, target.size() + 1);
    
    agenda.clear();
    agenda.reserve(source.size() + target.size() + 1);
    agenda.resize(source.size() + target.size() + 1);

    chart_source.clear();
    chart_source.reserve(source.size() + 1);
    chart_source.resize(source.size() + 1);

    chart_target.clear();
    chart_target.reserve(target.size() + 1);
    chart_target.resize(target.size() + 1);

    alpha_source.clear();
    alpha_target.clear();
    beta_source.clear();
    beta_target.clear();
    
    alpha_source.resize(source.size() + 1);
    alpha_target.resize(target.size() + 1);
    beta_source.resize(source.size() + 1);
    beta_target.resize(target.size() + 1);
    
    for (size_type src = 0; src <= source.size(); ++ src)
      for (size_type trg = 0; trg <= target.size(); ++ trg) {
	const size_type source_first = src;
	const size_type target_first = trg;

	
	if (src < source.size() && trg < target.size()) {
	  // one-to-one
	  {
	    const logprob_type prob = matrix(source_first + 1, target_first + 1);
	    
	    const size_type source_last = source_first + 1;
	    const size_type target_last = target_first + 1;
	    
	    const span_pair_type span_pair(source_first, source_last, target_first, target_last);
	    
	    edges(source_first, source_last, target_first, target_last).push_back(edge_type(rule_type(span_pair, PYP::TERMINAL), prob));
	    
	    chart(source_first, source_last, target_first, target_last) = prob;
	    
	    chart_source(source_first, source_last) = std::max(chart_source(source_first, source_last), prob);
	    chart_target(target_first, target_last) = std::max(chart_target(target_first, target_last), prob);
	    
	    agenda[span_pair.size()].push_back(span_pair);
	  }
	  
	  // one-to-many
	  {
	    logprob_type prob = matrix(source_first + 1, target_first + 1);

	    for (size_type source_last = source_first + 2; source_last <= source.size(); ++ source_last) {
	      const size_type target_last = target_first + 1;
	      
	      prob *= matrix(source_last, target_last);
	      
	      const span_pair_type span_pair(source_first, source_last, target_first, target_last);
	    
	      edges(source_first, source_last, target_first, target_last).push_back(edge_type(rule_type(span_pair, PYP::TERMINAL), prob));
	      
	      chart(source_first, source_last, target_first, target_last) = prob;
	      
	      chart_source(source_first, source_last) = std::max(chart_source(source_first, source_last), prob);
	      chart_target(target_first, target_last) = std::max(chart_target(target_first, target_last), prob);
	      
	      agenda[span_pair.size()].push_back(span_pair);
	    }
	  }

	  // one-to-many
	  {
	    logprob_type prob = matrix(source_first + 1, target_first + 1);
	    
	    for (size_type target_last = target_first + 2; target_last <= target.size(); ++ target_last) {
	      const size_type source_last = source_first + 1;
	      
	      prob *= matrix(source_last, target_last);
	      
	      const span_pair_type span_pair(source_first, source_last, target_first, target_last);
	      
	      edges(source_first, source_last, target_first, target_last).push_back(edge_type(rule_type(span_pair, PYP::TERMINAL), prob));
	      
	      chart(source_first, source_last, target_first, target_last) = prob;
	      
	      chart_target(target_first, target_last) = std::max(chart_target(target_first, target_last), prob);
	      
	      agenda[span_pair.size()].push_back(span_pair);
	    }
	  }
	}
	
	// epsilon-to-many
	if (src < source.size()) {
	  logprob_type prob = 1.0;
	  
	  for (size_type source_last = source_first + 1; source_last <= source.size(); ++ source_last) {
	    const size_type target_last = target_first;
	    
	    prob *= matrix(source_last, 0);
	    
	    const span_pair_type span_pair(source_first, source_last, target_first, target_last);
	    
	    edges(source_first, source_last, target_first, target_last).push_back(edge_type(rule_type(span_pair, PYP::TERMINAL), prob));
	    
	    chart(source_first, source_last, target_first, target_last) = prob;
	    
	    chart_source(source_first, source_last) = std::max(chart_source(source_first, source_last), prob);
	    
	    agenda[span_pair.size()].push_back(span_pair);
	  }
	  
	}
	
	// epsilon-to-many
	if (trg < target.size()) {
	  logprob_type prob = 1.0;
	  
	  for (size_type target_last = target_first + 1; target_last <= target.size(); ++ target_last) {
	    const size_type source_last = source_first;
	    
	    prob *= matrix(0, target_last);
	    
	    const span_pair_type span_pair(source_first, source_last, target_first, target_last);
	    
	    edges(source_first, source_last, target_first, target_last).push_back(edge_type(rule_type(span_pair, PYP::TERMINAL), prob));
	    
	    chart(source_first, source_last, target_first, target_last) = prob;
	    
	    chart_source(source_first, source_last) = std::max(chart_source(source_first, source_last), prob);
	    chart_target(target_first, target_last) = std::max(chart_target(target_first, target_last), prob);
	    
	    agenda[span_pair.size()].push_back(span_pair);
	  }
	}
      }
    
    // forward-backward to compute estiamtes...
    forward_backward(source, chart_source, alpha_source, beta_source);
    forward_backward(target, chart_target, alpha_target, beta_target);
      
    
    // start parsing...
    
    span_pairs_unique_type spans_unique;
    spans_unique.set_empty_key(span_pairs_type(span_pair_type(size_type(-1), size_type(-1), size_type(-1), size_type(-1)),
					       span_pair_type(size_type(-1), size_type(-1), size_type(-1), size_type(-1))));
    
    // traverse agenda, smallest first...
    const size_type length_max = source.size() + target.size();

    for (size_type length = 1; length != length_max; ++ length) 
      if (! agenda[length].empty()) {
	span_pair_set_type& spans = agenda[length];

	// construct heap...
	heap.clear();
	heap.reserve(spans.size());
	span_pair_set_type::const_iterator siter_end = spans.end();
	for (span_pair_set_type::const_iterator siter = spans.begin(); siter != siter_end; ++ siter)  {
	  const logprob_type score = (chart(siter->source.first, siter->source.last, siter->target.first, siter->target.last)
				      * std::min(alpha_source[siter->source.first] * beta_source[siter->source.last],
						 alpha_target[siter->target.first] * beta_target[siter->target.last]));
	  
	  heap.push_back(score_span_pair_type(score, *siter));
	  std::push_heap(heap.begin(), heap.end(), heap_compare());
	}

	heap_type::iterator hiter_begin = heap.begin();
	heap_type::iterator hiter       = heap.end();
	heap_type::iterator hiter_end   = heap.end();
	
	const logprob_type logprob_threshold = hiter_begin->first * beam;
	for (/**/; hiter_begin != hiter && hiter_begin->first > logprob_threshold; -- hiter)
	  std::pop_heap(hiter_begin, hiter, heap_compare());
	
	// we will process from hiter to hiter_end...
	spans_unique.clear();
	
	for (heap_type::iterator iter = hiter ; iter != hiter_end; ++ iter) {
	  const span_pair_type& span_pair = iter->second;
	  
	  // we borrow the notation...
	  
	  const difference_type l = length;
	  const difference_type s = span_pair.source.first;
	  const difference_type t = span_pair.source.last;
	  const difference_type u = span_pair.target.first;
	  const difference_type v = span_pair.target.last;
	  
	  const difference_type T = source.size();
	  const difference_type V = target.size();

	  // remember, we have processed only upto length l. thus, do not try to combine with spans greather than l!
	  // also, keep used pair of spans in order to remove duplicates.
	  
	  for (difference_type S = utils::bithack::max(s - l, difference_type(0)); S <= s; ++ S) {
	    const difference_type L = l - (s - S);
	    
	    // straight
	    for (difference_type U = utils::bithack::max(u - L, difference_type(0)); U <= u - (S == s); ++ U) {
	      // parent span: StUv
	      // span1: SsUu
	      // span2: stuv

	      if (edges(S, s, U, u).empty()) continue;
	      
	      const span_pair_type  span1(S, s, U, u);
	      const span_pair_type& span2(span_pair);

	      if (! spans_unique.insert(std::make_pair(span1, span2)).second) continue;
	      
	      const logprob_type logprob = (logprob_str
					    * chart(span1.source.first, span1.source.last, span1.target.first, span1.target.last)
					    * chart(span2.source.first, span2.source.last, span2.target.first, span2.target.last));

	      const span_pair_type span_head(S, t, U, v);
	      
	      chart(S, t, U, v) += logprob;
	      edges(S, t, U, v).push_back(edge_type(rule_type(span_head, span1, span2, PYP::STRAIGHT), logprob));
	      
	      if (edges(S, t, U, v).size() == 1)
		agenda[span_head.size()].push_back(span_head);
	    }
	    
	    // inversion
	    for (difference_type U = v + (S == s); U <= utils::bithack::min(v + L, V); ++ U) {
	      // parent span: StuU
	      // span1: SsvU
	      // span2: stuv
	      
	      if (edges(S, s, v, U).empty()) continue;
	      
	      const span_pair_type  span1(S, s, v, U);
	      const span_pair_type& span2(span_pair);

	      if (! spans_unique.insert(std::make_pair(span1, span2)).second) continue;
	      
	      const logprob_type logprob = (logprob_inv
					    * chart(span1.source.first, span1.source.last, span1.target.first, span1.target.last)
					    * chart(span2.source.first, span2.source.last, span2.target.first, span2.target.last));
	      
	      const span_pair_type span_head(S, t, u, U);

	      chart(S, t, u, U) += logprob;
	      edges(S, t, u, U).push_back(edge_type(rule_type(span_head, span1, span2, PYP::INVERTED), logprob));

	      if (edges(S, t, u, U).size() == 1)
		agenda[span_head.size()].push_back(span_head);
	    }
	  }
	  
	  for (difference_type S = t; S <= utils::bithack::min(t + l, T); ++ S) {
	    const difference_type L = l - (S - t);
	    
	    // inversion
	    for (difference_type U = utils::bithack::max(u - L, difference_type(0)); U <= u - (S == t); ++ U) {
	      // parent span: sSUv
	      // span1: stuv
	      // span2: tSUu
	      
	      if (edges(t, S, U, u).empty()) continue;
	      
	      const span_pair_type& span1(span_pair);
	      const span_pair_type  span2(t, S, U, u);

	      if (! spans_unique.insert(std::make_pair(span1, span2)).second) continue;
	      
	      const logprob_type logprob = (logprob_inv
					    * chart(span1.source.first, span1.source.last, span1.target.first, span1.target.last)
					    * chart(span2.source.first, span2.source.last, span2.target.first, span2.target.last));

	      const span_pair_type span_head(s, S, U, v);
	      
	      chart(s, S, U, v) += logprob;
	      edges(s, S, U, v).push_back(edge_type(rule_type(span_head, span1, span2, PYP::INVERTED), logprob));

	      if (edges(s, S, U, v).size() == 1)
		agenda[span_head.size()].push_back(span_head);
	    }
	    
	    // straight
	    for (difference_type U = v + (S == t); U <= utils::bithack::min(v + L, V); ++ U) {
	      // parent span: sSuU
	      // span1: stuv
	      // span2: tSvU
	      
	      if (edges(t, S, v, U).empty()) continue;
	      
	      const span_pair_type& span1(span_pair);
	      const span_pair_type  span2(t, S, v, U);
	      
	      if (! spans_unique.insert(std::make_pair(span1, span2)).second) continue;
	      
	      const logprob_type logprob = (logprob_str
					    * chart(span1.source.first, span1.source.last, span1.target.first, span1.target.last)
					    * chart(span2.source.first, span2.source.last, span2.target.first, span2.target.last));
	      
	      const span_pair_type span_head(s, S, u, U);
	      
	      chart(s, S, u, U) += logprob;
	      edges(s, S, u, U).push_back(edge_type(rule_type(span_head, span1, span2, PYP::STRAIGHT), logprob));
	      
	      if (edges(s, S, u, U).size() == 1)
		agenda[span_head.size()].push_back(span_head);
	    }
	  }
	}
      }
    
    return std::make_pair(chart(0, source.size(), 0, target.size()), ! edges(0, source.size(), 0, target.size()).empty());
  }
  
  // backward sampling
  template <typename Sampler>
  logprob_type backward(const sentence_type& source,
			const sentence_type& target,
			derivation_type& derivation,
			Sampler& sampler,
			const double temperature)
  {
    //std::cerr << "backward" << std::endl;

    derivation.clear();
    logprob_type prob_derivation = cicada::semiring::traits<logprob_type>::one(); 
    
    // top-down sampling of the hypergraph...
    
    stack.clear();
    stack.push_back(span_pair_type(0, source.size(), 0, target.size()));

    
    while (! stack.empty()) {
      const span_pair_type span_pair = stack.back();
      stack.pop_back();
      
      const edge_set_type& edges_span = edges(span_pair.source.first, span_pair.source.last, span_pair.target.first, span_pair.target.last);
      
      if (edges_span.empty()) {
	// no derivation???
	derivation.clear();
	return logprob_type();
      }
      
      logprob_type logsum;
      edge_set_type::const_iterator eiter_end = edges_span.end();
      for (edge_set_type::const_iterator eiter = edges_span.begin(); eiter != eiter_end; ++ eiter)
	logsum += eiter->prob;
      
      probs.clear();
      for (edge_set_type::const_iterator eiter = edges_span.begin(); eiter != eiter_end; ++ eiter)
	probs.push_back(eiter->prob / logsum);
      
      const size_type pos_sampled = sampler.draw(probs.begin(), probs.end(), temperature) - probs.begin();
      
      // we will push in a right first manner, so that when popped, we will derive left-to-right traversal.
      
      const rule_type& rule = edges_span[pos_sampled].rule;
      logprob_type prob = edges_span[pos_sampled].prob;
      
      if (! rule.right.empty()) {
	prob /= chart(rule.right.source.first, rule.right.source.last, rule.right.target.first, rule.right.target.last);
	
	stack.push_back(rule.right);
      }
      
      if (! rule.left.empty()) {
	prob /= chart(rule.left.source.first, rule.left.source.last, rule.left.target.first, rule.left.target.last);
	
	stack.push_back(rule.left);
      }
      
      prob_derivation *= prob;
      
      derivation.push_back(rule);
    }

    return prob_derivation;
  }

  logprob_type logprob_term;
  logprob_type logprob_str;
  logprob_type logprob_inv;
  matrix_type  matrix;

  chart_type      chart;
  edge_chart_type edges;
  
  agenda_type     agenda;
  stack_type      stack;
  heap_type       heap;
  
  chart_mono_type chart_source;
  chart_mono_type chart_target;
  
  alpha_type alpha_source;
  alpha_type alpha_target;
  beta_type  beta_source;
  beta_type  beta_target;
  
  prob_set_type probs;
};


typedef boost::filesystem::path path_type;
typedef utils::sampler<boost::mt19937> sampler_type;

typedef std::vector<sentence_type, std::allocator<sentence_type> > sentence_set_type;

typedef PYP::size_type size_type;
typedef PYPGraph::derivation_type derivation_type;
typedef std::vector<derivation_type, std::allocator<derivation_type> > derivation_set_type;
typedef std::vector<size_type, std::allocator<size_type> > position_set_type;

struct Task
{
  typedef PYP::size_type       size_type;
  typedef PYP::difference_type difference_type;  
  
  typedef utils::lockfree_list_queue<size_type, std::allocator<size_type > > queue_type;
  
  typedef PYPITG::logprob_type logprob_type;
  typedef PYPITG::prob_type    prob_type;

  Task(queue_type& __mapper,
       queue_type& __reducer,
       const sentence_set_type& __sources,
       const sentence_set_type& __targets,
       derivation_set_type& __derivations,
       PYPITG& __model,
       const sampler_type& __sampler,
       const logprob_type& __beam)
    : mapper(__mapper),
      reducer(__reducer),
      sources(__sources),
      targets(__targets),
      derivations(__derivations),
      model(__model),
      sampler(__sampler),
      beam(__beam) {}

  void operator()()
  {
    PYPGraph graph;
    
    size_type pos;
    
    for (;;) {
      mapper.pop(pos);
      
      if (pos == size_type(-1)) break;

      if (! derivations[pos].empty()) {
	PYPITG::mutex_type::scoped_writer_lock lock(model.mutex);
	
	derivation_type::const_iterator diter_end = derivations[pos].end();
	for (derivation_type::const_iterator diter = derivations[pos].begin(); diter != diter_end; ++ diter)
	  model.decrement(sources[pos], targets[pos], *diter, sampler);
      }
      
      {
	PYPITG::mutex_type::scoped_reader_lock lock(model.mutex);
	
	graph.initialize(sources[pos], targets[pos], model);
      }
      
      graph.forward(sources[pos], targets[pos], beam);
      
      graph.backward(sources[pos], targets[pos], derivations[pos], sampler, temperature);
      
      {
	PYPITG::mutex_type::scoped_writer_lock lock(model.mutex);
	
	derivation_type::const_iterator diter_end = derivations[pos].end();
	for (derivation_type::const_iterator diter = derivations[pos].begin(); diter != diter_end; ++ diter)
	  model.increment(sources[pos], targets[pos], *diter, sampler, temperature);
      }
      
      reducer.push(pos);
    }
  }
  
  queue_type& mapper;
  queue_type& reducer;
  
  const sentence_set_type& sources;
  const sentence_set_type& targets;
  derivation_set_type& derivations;
  
  PYPITG&  model;
  sampler_type sampler;
  
  logprob_type beam;
  
  double temperature;
};


struct less_size
{
  less_size(const sentence_set_type& __sources,
	    const sentence_set_type& __targets)
    : sources(__sources), targets(__targets) {}

  bool operator()(const size_type& x, const size_type& y) const
  {
    return (sources[x].size() + targets[x].size()) < (sources[y].size() + targets[y].size());
  }
  
  const sentence_set_type& sources;
  const sentence_set_type& targets;
};

inline
path_type add_suffix(const path_type& path, const std::string& suffix)
{
  bool has_suffix_gz  = false;
  bool has_suffix_bz2 = false;
  
  path_type path_added = path;
  
  if (path.extension() == ".gz") {
    path_added = path.parent_path() / path.stem();
    has_suffix_gz = true;
  } else if (path.extension() == ".bz2") {
    path_added = path.parent_path() / path.stem();
    has_suffix_bz2 = true;
  }
  
  path_added = path_added.string() + suffix;
  
  if (has_suffix_gz)
    path_added = path_added.string() + ".gz";
  else if (has_suffix_bz2)
    path_added = path_added.string() + ".bz2";
  
  return path_added;
}

path_type train_source_file = "-";
path_type train_target_file = "-";

path_type output_sample_file;
path_type output_model_file;

double beam = 1e-4;

int samples = 1;
int burns = 10;
int baby_steps = 1;
int anneal_steps = 1;
int resample_rate = 1;
int resample_iterations = 2;
bool slice_sampling = false;
bool sample_hypergraph = false;

double rule_discount_alpha = 1.0;
double rule_discount_beta  = 1.0;
double rule_strength_shape = 1.0;
double rule_strength_rate  = 1.0;

double lexicon_discount_alpha = 1.0;
double lexicon_discount_beta  = 1.0;
double lexicon_strength_shape = 1.0;
double lexicon_strength_rate  = 1.0;

int threads = 1;
int debug = 0;

void options(int argc, char** argv);

size_t read_data(const path_type& path, sentence_set_type& sentences);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    threads = utils::bithack::max(threads, 1);
    
    if (samples < 0)
      throw std::runtime_error("# of samples must be positive");
    
    if (resample_rate <= 0)
      throw std::runtime_error("resample rate must be >= 1");
    
    if (beam < 0.0 || beam > 1.0)
      throw std::runtime_error("invalid beam width");
    
    sentence_set_type   sources;
    sentence_set_type   targets;
    
    const size_type source_vocab_size = read_data(train_source_file, sources);
    const size_type target_vocab_size = read_data(train_target_file, targets);

    if (sources.empty())
      throw std::runtime_error("no source sentence data?");

    if (targets.empty())
      throw std::runtime_error("no target sentence data?");
    
    if (sources.size() != targets.size())
      throw std::runtime_error("source/target side do not match!");

    PYPRule model_rule(PYPRule::parameter_type(rule_discount_alpha,
					       rule_discount_beta,
					       rule_strength_shape,
					       rule_strength_rate));

    PYPLexicon model_lexicon(PYPLexicon::parameter_type(lexicon_discount_alpha,
							lexicon_discount_beta,
							lexicon_strength_shape,
							lexicon_strength_rate),
			     1.0 / (double(source_vocab_size) * double(target_vocab_size)));

    
    PYPITG model(model_rule, model_lexicon);
    
    derivation_set_type derivations(sources.size());
    position_set_type positions;
    for (size_t i = 0; i != sources.size(); ++ i)
      if (! sources[i].empty() && ! targets[i].empty())
	positions.push_back(i);
    position_set_type(positions).swap(positions);
    
    sampler_type sampler;
    
    // sample parameters, first...
    if (slice_sampling)
      model.slice_sample_parameters(sampler, resample_iterations);
    else
      model.sample_parameters(sampler, resample_iterations);
    
    if (debug >= 2)
      std::cerr << "rule: discount=" << model.rule.table.discount() << " strength=" << model.rule.table.strength() << std::endl
		<< "terminal=" << model.rule.prob_terminal() << " straight=" << model.rule.prob_straight() << " inverted=" << model.rule.prob_inverted() << std::endl
		<< "lexicon: discount=" << model.lexicon.table.discount() << " strength=" << model.lexicon.table.strength() << std::endl;
    
    Task::queue_type queue_mapper;
    Task::queue_type queue_reducer;
    
    std::vector<Task, std::allocator<Task> > tasks(threads, Task(queue_mapper,
								 queue_reducer,
								 sources,
								 targets,
								 derivations,
								 model,
								 sampler,
								 beam));
    
    boost::thread_group workers;
    for (int i = 0; i != threads; ++ i)
      workers.add_thread(new boost::thread(boost::ref(tasks[i])));
    
    size_t anneal_iter = 0;
    const size_t anneal_last = utils::bithack::branch(anneal_steps > 0, anneal_steps, 0);
    
    size_t baby_iter = 0;
    const size_t baby_last = utils::bithack::branch(baby_steps > 0, baby_steps, 0);
    
    size_t burn_iter = 0;
    const size_t burn_last = utils::bithack::branch(burns > 0, burns, 0);
    
    bool sampling = false;
    int sample_iter = 0;
    
    // then, learn!
    for (size_t iter = 0; sample_iter != samples; ++ iter, sample_iter += sampling) {
      
      double temperature = 1.0;
      bool anneal_finished = true;
      if (anneal_iter != anneal_last) {
	anneal_finished = false;
	temperature = double(anneal_last - anneal_iter) + 1;
	
	++ anneal_iter;

	if (debug >= 2)
	  std::cerr << "temperature: " << temperature << std::endl;
      }
      
      bool baby_finished = true;
      if (baby_iter != baby_last) {
	++ baby_iter;
	baby_finished = false;
      }
      
      bool burn_finished = true;
      if (burn_iter != burn_last) {
	++ burn_iter;
	burn_finished = false;
      }
      
      sampling = anneal_finished && baby_finished && burn_finished;
      
      if (debug) {
	if (sampling)
	  std::cerr << "sampling iteration: " << (iter + 1) << std::endl;
	else
	  std::cerr << "burn-in iteration: " << (iter + 1) << std::endl;
      }

      // assign temperature...
      for (size_type i = 0; i != tasks.size(); ++ i)
	tasks[i].temperature = temperature;
      
      // shuffle
      boost::random_number_generator<sampler_type::generator_type> gen(sampler.generator());
      std::random_shuffle(positions.begin(), positions.end(), gen);
      
      if (! baby_finished)
	std::sort(positions.begin(), positions.end(), less_size(sources, targets));
      
      position_set_type::const_iterator piter_end = positions.end();
      for (position_set_type::const_iterator piter = positions.begin(); piter != piter_end; ++ piter)
	queue_mapper.push(*piter);
      
      for (size_type reduced = 0; reduced != positions.size(); ++ reduced) {
	size_type pos = 0;
	queue_reducer.pop(pos);
	
	if (debug >= 3) {
	  std::cerr << "training=" << pos << std::endl
		    << "source=" << sources[pos] << std::endl
		    << "target=" << targets[pos] << std::endl;
	  
	  derivation_type::const_iterator diter_end = derivations[pos].end();
	  for (derivation_type::const_iterator diter = derivations[pos].begin(); diter != diter_end; ++ diter) {
	      
	    std::cerr << "derivation: ";
	    switch (diter->itg) {
	    case PYP::TERMINAL:   std::cerr << "ter"; break;
	    case PYP::STRAIGHT:   std::cerr << "str"; break;
	    case PYP::INVERTED:   std::cerr << "inv"; break;
	    default: std::cerr << "UNK";
	    }
	      
	    std::cerr << " source: " << diter->span.source.first << "..." << diter->span.source.last
		      << " target: " << diter->span.target.first << "..." << diter->span.target.last;
	      
	    if (diter->itg == PYP::TERMINAL)
	      std::cerr << " pair: "
			<< PYP::phrase_type(sources[pos].begin() + diter->span.source.first, sources[pos].begin() + diter->span.source.last)
			<< " ||| "
			<< PYP::phrase_type(targets[pos].begin() + diter->span.target.first, targets[pos].begin() + diter->span.target.last);
	      
	    std::cerr << std::endl;
	  }
	}
	  
	if (debug) {
	  if ((reduced + 1) % 10000 == 0)
	    std::cerr << '.';
	  if ((reduced + 1) % 1000000 == 0)
	    std::cerr << '\n';
	}
      }
      
      if (debug && positions.size() >= 10000 && positions.size() % 1000000 != 0)
	std::cerr << std::endl;
      
      if (static_cast<int>(iter) % resample_rate == resample_rate - 1) {
	if (slice_sampling)
	  model.slice_sample_parameters(sampler, resample_iterations);
	else
	  model.sample_parameters(sampler, resample_iterations);
	
	if (debug >= 2)
	  std::cerr << "rule: discount=" << model.rule.table.discount() << " strength=" << model.rule.table.strength() << std::endl
		    << "terminal=" << model.rule.prob_terminal() << " straight=" << model.rule.prob_straight() << " inverted=" << model.rule.prob_inverted() << std::endl
		    << "lexicon: discount=" << model.lexicon.table.discount() << " strength=" << model.lexicon.table.strength() << std::endl;
      }
      
      if (debug)
	std::cerr << "log-likelihood: " << model.log_likelihood() << std::endl;
      
      if (sampling && ! output_sample_file.empty()) {
	// dump derivations..!
	
	const path_type path = add_suffix(output_sample_file, "." + utils::lexical_cast<std::string>(sample_iter + 1));
	
	utils::compress_ostream os(path, 1024 * 1024);

	if (sample_hypergraph) {
	  typedef std::vector<hypergraph_type::id_type, std::allocator<hypergraph_type::id_type> > stack_type;
	  typedef hypergraph_type::rule_type     rule_type;
	  typedef hypergraph_type::rule_ptr_type rule_ptr_type;

	  stack_type stack;
	  
	  hypergraph_type graph_source;
	  hypergraph_type graph_target;
	  
	  rule_type::symbol_set_type straight(2);
	  rule_type::symbol_set_type inverted(2);
	  
	  straight[0] = vocab_type::X1;
	  straight[1] = vocab_type::X2;
	  
	  inverted[0] = vocab_type::X2;
	  inverted[1] = vocab_type::X1;

	  hypergraph_type::edge_type::node_set_type tails(2);
	  
	  const rule_ptr_type rule_straight(rule_type::create(rule_type(vocab_type::X, straight)));
	  const rule_ptr_type rule_inverted(rule_type::create(rule_type(vocab_type::X, inverted)));
	  
	  for (size_type pos = 0; pos != derivations.size(); ++ pos) {
	    graph_source.clear();
	    graph_target.clear();
	    
	    if (! derivations[pos].empty()) {
	      // construct pair of hypergrpahs
	      
	      graph_source.goal = graph_source.add_node().id;
	      graph_target.goal = graph_target.add_node().id;
	      
	      stack.clear();
	      stack.push_back(0);
	      
	      derivation_type::const_iterator diter_end = derivations[pos].end();
	      for (derivation_type::const_iterator diter = derivations[pos].begin(); diter != diter_end; ++ diter) {
		const hypergraph_type::id_type head = stack.back();
		stack.pop_back();
		
		if (diter->is_terminal()) {
		  hypergraph_type::edge_type& edge_source = graph_source.add_edge();
		  hypergraph_type::edge_type& edge_target = graph_target.add_edge();
		  

		  if (diter->span.source.empty())
		    edge_source.rule = rule_type::create(rule_type(vocab_type::X, &vocab_type::EPSILON, (&vocab_type::EPSILON) + 1));
		  else
		    edge_source.rule = rule_type::create(rule_type(vocab_type::X,
								   sources[pos].begin() + diter->span.source.first,
								   sources[pos].begin() + diter->span.source.last));

		  if (diter->span.target.empty())
		    edge_target.rule = rule_type::create(rule_type(vocab_type::X, &vocab_type::EPSILON, (&vocab_type::EPSILON) + 1));
		  else
		    edge_target.rule = rule_type::create(rule_type(vocab_type::X,
								   targets[pos].begin() + diter->span.target.first,
								   targets[pos].begin() + diter->span.target.last));
		  
		  graph_source.connect_edge(edge_source.id, head);
		  graph_target.connect_edge(edge_target.id, head);
		  
		} else {
		  hypergraph_type::node_type& node_source1 = graph_source.add_node();
		  hypergraph_type::node_type& node_source2 = graph_source.add_node();

		  hypergraph_type::node_type& node_target1 = graph_target.add_node();
		  hypergraph_type::node_type& node_target2 = graph_target.add_node();
		  
		  // push in reverse order!
		  stack.push_back(node_source2.id);
		  stack.push_back(node_source1.id);
		  
		  tails[0] = node_source1.id;
		  tails[1] = node_source2.id;
		  
		  hypergraph_type::edge_type& edge_source = graph_source.add_edge(tails.begin(), tails.end());
		  hypergraph_type::edge_type& edge_target = graph_target.add_edge(tails.begin(), tails.end());

		  if (diter->is_straight()) {
		    edge_source.rule = rule_straight;
		    edge_target.rule = rule_straight;
		  } else {
		    edge_source.rule = rule_inverted;
		    edge_target.rule = rule_inverted;
		  }
		  
		  graph_source.connect_edge(edge_source.id, head);
		  graph_target.connect_edge(edge_target.id, head);
		}
	      }
	      
	      graph_source.topologically_sort();
	      graph_target.topologically_sort();
	    }
	    
	    os << graph_source << " ||| " << graph_target << '\n';
	  }
	} else {
	  typedef std::vector<std::string, std::allocator<std::string> > stack_type;
	  
	  stack_type stack;
	  
	  for (size_type pos = 0; pos != derivations.size(); ++ pos) {
	    if (! derivations[pos].empty()) {
	      // we need to transform the stack-structure into tree-struct... HOW?
	    
	      stack.clear();
	      derivation_type::const_iterator diter_end = derivations[pos].end();
	      for (derivation_type::const_iterator diter = derivations[pos].begin(); diter != diter_end; ++ diter) {
		if (diter->is_terminal()) {
		  os << "((( "
		     << PYP::phrase_type(sources[pos].begin() + diter->span.source.first, sources[pos].begin() + diter->span.source.last)
		     << " ||| "
		     << PYP::phrase_type(targets[pos].begin() + diter->span.target.first, targets[pos].begin() + diter->span.target.last)
		     << " )))";
		
		  while (! stack.empty() && stack.back() != " ") {
		    os << stack.back();
		    stack.pop_back();
		  }
		
		  if (! stack.empty() && stack.back() == " ") {
		    os << stack.back();
		    stack.pop_back();
		  }
		
		} else if (diter->is_straight()) {
		  os << "[ ";
		  stack.push_back(" ]");
		  stack.push_back(" ");
		} else {
		  os << "< ";
		  stack.push_back(" >");
		  stack.push_back(" ");
		}
	      }
	    }
	    os << '\n';
	  }
	}
      }
    }
    
    for (int i = 0; i != threads; ++ i)
      queue_mapper.push(size_type(-1));
    
    workers.join_all();
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}



size_t read_data(const path_type& path, sentence_set_type& sentences)
{
  typedef utils::dense_hash_set<word_type, boost::hash<word_type>, std::equal_to<word_type>, std::allocator<word_type> >::type word_set_type;

  word_set_type words;
  words.set_empty_key(word_type());

  sentences.clear();
  
  utils::compress_istream is(path, 1024 * 1024);
  
  sentence_type sentence;
  while (is >> sentence) {
    sentences.push_back(sentence);
    
    words.insert(sentence.begin(), sentence.end());
  }
  
  sentence_set_type(sentences).swap(sentences);

  return words.size();
}

void options(int argc, char** argv)
{
  namespace po = boost::program_options;
  
  po::variables_map variables;
  
  po::options_description desc("options");
  desc.add_options()
    ("train-source", po::value<path_type>(&train_source_file), "source train file")
    ("train-target", po::value<path_type>(&train_target_file), "target train file")
    
    ("output-sample", po::value<path_type>(&output_sample_file), "output sample file")
    ("output-model",  po::value<path_type>(&output_model_file),  "output model file (or phrase table)")
    
    ("beam",                po::value<double>(&beam)->default_value(beam),                            "beam threshold")
    
    ("samples",             po::value<int>(&samples)->default_value(samples),                         "# of samples")
    ("burns",               po::value<int>(&burns)->default_value(burns),                             "# of burn-ins")
    ("baby-steps",          po::value<int>(&baby_steps)->default_value(baby_steps),                   "# of baby steps")
    ("anneal-steps",        po::value<int>(&anneal_steps)->default_value(anneal_steps),               "# of anneal steps")
    ("resample",            po::value<int>(&resample_rate)->default_value(resample_rate),             "hyperparameter resample rate")
    ("resample-iterations", po::value<int>(&resample_iterations)->default_value(resample_iterations), "hyperparameter resample iterations")
    
    ("slice",               po::bool_switch(&slice_sampling),                                         "slice sampling for hyperparameters")
    ("hypergraph",          po::bool_switch(&sample_hypergraph),                                      "dump sampled derivation in hypergraph")
    
    ("rule-discount-alpha", po::value<double>(&rule_discount_alpha)->default_value(rule_discount_alpha), "discount ~ Beta(alpha,beta)")
    ("rule-discount-beta",  po::value<double>(&rule_discount_beta)->default_value(rule_discount_beta),   "discount ~ Beta(alpha,beta)")

    ("rule-strength-shape", po::value<double>(&rule_strength_shape)->default_value(rule_strength_shape), "strength ~ Gamma(shape,rate)")
    ("rule-strength-rate",  po::value<double>(&rule_strength_rate)->default_value(rule_strength_rate),   "strength ~ Gamma(shape,rate)")

    
    ("lexicon-discount-alpha", po::value<double>(&lexicon_discount_alpha)->default_value(lexicon_discount_alpha), "discount ~ Beta(alpha,beta)")
    ("lexicon-discount-beta",  po::value<double>(&lexicon_discount_beta)->default_value(lexicon_discount_beta),   "discount ~ Beta(alpha,beta)")

    ("lexicon-strength-shape", po::value<double>(&lexicon_strength_shape)->default_value(lexicon_strength_shape), "strength ~ Gamma(shape,rate)")
    ("lexicon-strength-rate",  po::value<double>(&lexicon_strength_rate)->default_value(lexicon_strength_rate),   "strength ~ Gamma(shape,rate)")

        
    ("threads", po::value<int>(&threads), "# of threads")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");

  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}


//
//  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA_LEXICON_IMPL__HPP__
#define __CICADA_LEXICON_IMPL__HPP__ 1

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>

#include <boost/fusion/tuple.hpp>
#include <boost/fusion/adapted.hpp>

#include <cicada/sentence.hpp>
#include <cicada/alignment.hpp>
#include <cicada/dependency.hpp>
#include <cicada/span_vector.hpp>
#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>

#include <boost/filesystem.hpp>

#include <utils/compact_map.hpp>
#include <utils/compact_set.hpp>
#include <utils/alloc_vector.hpp>
#include <utils/bithack.hpp>
#include <utils/unordered_map.hpp>
#include <utils/hashmurmur.hpp>
#include <utils/mathop.hpp>
#include <utils/simple_vector.hpp>

typedef cicada::Symbol     word_type;
typedef cicada::Sentence   sentence_type;
typedef cicada::Alignment  alignment_type;
typedef cicada::Dependency dependency_type;
typedef cicada::SpanVector span_set_type;
typedef cicada::Vocab      vocab_type;
typedef boost::filesystem::path path_type;

typedef double count_type;
typedef double prob_type;
typedef double logprob_type;

struct classes_type
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef std::vector<word_type, std::allocator<word_type> > map_type;
  
  classes_type(const bool __unknown=false, const bool __surface=false) : classes(), unknown(__unknown), surface(__surface) {}
  
  void clear() { classes.clear(); }
  void shrink() { map_type(classes).swap(classes); }
  
  word_type operator[](const word_type& word) const
  {
    if (unknown)
      return (word == vocab_type::BOS || word == vocab_type::EOS ? word : vocab_type::UNK);
    else if (surface)
      return word;
    else
      return (word.id() >= classes.size() ? vocab_type::UNK : classes[word.id()]);
  }
  
  word_type& operator[](const word_type& word)
  {
    if (word.id() >= classes.size())
      classes.resize(word.id() + 1, vocab_type::UNK);
    
    return classes[word.id()];
  }

  map_type classes;
  
  bool unknown;
  bool surface;
};

struct atable_type
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef int       index_type;

  class difference_map_type
  {
  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef int       index_type;
    
    typedef utils::simple_vector<count_type, std::allocator<count_type> > difference_set_type;
    
    difference_map_type& operator+=(const difference_map_type& x)
    {
      reserve(x.min(), x.max());
      
      for (index_type i = x.min(); i <= x.max(); ++ i)
	operator[](i) += x[i];
      return *this;
    }

    count_type& operator[](const index_type& diff)
    {
      const size_type pos = utils::bithack::branch(diff >= 0, diff, - diff - 1);
      difference_set_type& diffs = (diff >= 0 ? positives : negatives);
      
      if (pos >= diffs.size())
	diffs.resize(pos + 1, 0.0);
      return diffs[pos];
    }
    
    count_type operator[](const index_type& diff) const
    {
      const size_type pos = utils::bithack::branch(diff >= 0, diff, - diff - 1);
      const difference_set_type& diffs = (diff >= 0 ? positives : negatives);
      
      return (pos >= diffs.size() ? 0.0 : diffs[pos]);
    }
    
    void reserve(const index_type& min, const index_type& max)
    {
      operator[](min);
      operator[](max);
    }
    
    void initialize()
    {
      std::fill(positives.begin(), positives.end(), 0.0);
      std::fill(negatives.begin(), negatives.end(), 0.0);
    }

    bool empty() const { return positives.empty() && negatives.empty(); }
    
    index_type min() const { return - negatives.size(); }
    index_type max() const { return positives.size() - 1; }

    void shrink()
    {
      //difference_set_type(positives).swap(positives);
      //difference_set_type(negatives).swap(negatives);
    }

    void swap(difference_map_type& x)
    {
      positives.swap(x.positives);
      negatives.swap(x.negatives);
    }
    
    difference_set_type positives;
    difference_set_type negatives;
  };

  typedef std::pair<word_type, word_type> class_pair_type;
  typedef std::pair<index_type, index_type> range_type;
  
  typedef utils::unordered_map<class_pair_type, difference_map_type, utils::hashmurmur<size_t>, std::equal_to<class_pair_type>,
			       std::allocator<std::pair<const class_pair_type, difference_map_type> > >::type count_dict_type;

  typedef difference_map_type mapped_type;

  typedef std::pair<class_pair_type, range_type> class_range_type;
  
  typedef utils::unordered_map<class_range_type, difference_map_type, utils::hashmurmur<size_t>, std::equal_to<class_range_type>,
			       std::allocator<std::pair<const class_range_type, difference_map_type> > >::type cache_type;

  atable_type(const double __prior=0.1, const double __smooth=1e-20) : prior(__prior), smooth(__smooth) {}
  
  prob_type operator()(const word_type& source,
		       const word_type& target,
		       const index_type& source_size,
		       const index_type& target_size,
		       const index_type& i_prev,
		       const index_type& i) const
  {
    if (atable.empty()) return 1.0 / source_size;
    
    // i_prev < 0 implies BOS
    // i >= souce_size implies EOS
    //
    // we will cache wrt class_pair_type and diff's range
    //
    
    if (source == vocab_type::BOS) {
      // 0 <= i < source_size
      // which implies: 1 <= diff < source_size + 1
      //
      
      return estimate(class_pair_type(source, target), range_type(1, source_size + 1))[i - i_prev];
    } else if (target == vocab_type::EOS) {
      // which implies: 1 <= diff < source_size - i_prev + 1
      // 
      
      return estimate(class_pair_type(source, target), range_type(1, source_size - i_prev + 1))[i - i_prev];
    } else {
      // 0 <= i < source_size
      // which implies: 0 - i_prev <= diff < source_size - i_prev
      //
      
      return estimate(class_pair_type(source, target), range_type(0 - i_prev, source_size - i_prev))[i - i_prev];
    }
  }
  
  const difference_map_type& estimate(const class_pair_type& classes, const range_type& range) const
  {
    difference_map_type& diffs = const_cast<cache_type&>(caches)[std::make_pair(classes, range)];
    if (diffs.empty()) {
      // reserve first...
      diffs.reserve(range.first, range.second - 1);
      
      double sum = 0.0;
      
      count_dict_type::const_iterator aiter = atable.find(classes);
      
      for (index_type i = range.first; i != range.second; ++ i) {
	const double count = (aiter != atable.end() ? aiter->second[i] + prior : prior);
	
	diffs[i] = count;
	sum += count;
      }
      
      const double sum_digamma = utils::mathop::digamma(sum);
      for (index_type i = range.first; i != range.second; ++ i)
	diffs[i] = std::max(utils::mathop::exp(utils::mathop::digamma(diffs[i]) - sum_digamma), smooth);
      
      //diffs.shrink();
    }
    
    return diffs;
  }

  difference_map_type& operator[](const class_pair_type& x)
  {
    return atable[x];
  }

  difference_map_type& operator()(const word_type& source, const word_type& target)
  {
    return atable[class_pair_type(source, target)];
  }

  count_type& operator()(const word_type& source, const word_type& target, const index_type& diff)
  {
    return atable[class_pair_type(source, target)][diff];
  }
  
  void clear() { atable.clear(); caches.clear(); }
  void swap(atable_type& x)
  {
    atable.swap(x.atable);
    caches.swap(x.caches);
    std::swap(prior,  x.prior);
    std::swap(smooth, x.smooth);
  }

  void estimate_unk()
  {
    if (atable.empty()) return;

    difference_map_type atable_source_target;
    count_dict_type atable_source;
    count_dict_type atable_target;
    
    count_dict_type::const_iterator aiter_end = atable.end();
    for (count_dict_type::const_iterator aiter = atable.begin(); aiter != aiter_end; ++ aiter) {
      const class_pair_type& pair = aiter->first;

      if (pair.first != vocab_type::BOS && pair.first != vocab_type::EOS)
	atable_source[class_pair_type(vocab_type::UNK, pair.second)] += aiter->second;
      
      if (pair.second != vocab_type::BOS && pair.second != vocab_type::EOS)
	atable_target[class_pair_type(pair.first, vocab_type::UNK)] += aiter->second;
      
      if (pair.first != vocab_type::BOS && pair.first != vocab_type::EOS
	  && pair.second != vocab_type::BOS && pair.second != vocab_type::EOS)
	atable_source_target += aiter->second;
    }
    
    count_dict_type::const_iterator siter_end = atable_source.end();
    for (count_dict_type::const_iterator siter = atable_source.begin(); siter != siter_end; ++ siter)
      atable[siter->first] = siter->second;
    
    count_dict_type::const_iterator titer_end = atable_target.end();
    for (count_dict_type::const_iterator titer = atable_target.begin(); titer != titer_end; ++ titer)
      atable[titer->first] = titer->second;
    
    atable[class_pair_type(vocab_type::UNK, vocab_type::UNK)] = atable_source_target;
  }

  void shrink()
  {
#if 0
    count_dict_type::iterator aiter_end = atable.end();
    for (count_dict_type::iterator aiter = atable.begin(); aiter != aiter_end; ++ aiter)
      aiter->second.shrink();
#endif
  }
  
  void initialize()
  {
    count_dict_type::iterator aiter_end = atable.end();
    for (count_dict_type::iterator aiter = atable.begin(); aiter != aiter_end; ++ aiter)
      aiter->second.initialize();

    caches.clear();
  }

  atable_type& operator+=(const atable_type& x)
  {
    count_dict_type::const_iterator aiter_end = x.atable.end();
    for (count_dict_type::const_iterator aiter = x.atable.begin(); aiter != aiter_end; ++ aiter)
      atable[aiter->first] += aiter->second;
    
    return *this;
  }
  
  count_dict_type atable;
  cache_type      caches;
  double prior;
  double smooth;
};

struct ttable_type
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  struct real_precision : boost::spirit::karma::real_policies<double>
  {
    static unsigned int precision(double) 
    { 
      return 20;
    }
  };

  struct count_map_type
  {
    typedef utils::compact_map<word_type, count_type,
			       utils::unassigned<word_type>, utils::unassigned<word_type>,
			       boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<std::pair<const word_type, count_type> > > counts_type;

    typedef counts_type::value_type      value_type;
    typedef counts_type::size_type       size_type;
    typedef counts_type::difference_type difference_type;
      
    typedef counts_type::mapped_type     mapped_type;
    typedef counts_type::key_type        key_type;
  
    typedef counts_type::const_iterator const_iterator;
    typedef counts_type::iterator       iterator;
  
    typedef counts_type::const_reference const_reference;
    typedef counts_type::reference       reference;
  
    count_map_type() {  }

    inline const_iterator begin() const { return counts.begin(); }
    inline       iterator begin()       { return counts.begin(); }
    inline const_iterator end() const { return counts.end(); }
    inline       iterator end()       { return counts.end(); }

    inline const_iterator find(const key_type& x) const { return counts.find(x); }
    inline       iterator find(const key_type& x)       { return counts.find(x); }
    
    mapped_type& operator[](const key_type& key) { return counts[key]; }
    
    size_type size() const { return counts.size(); }
    bool empty() const { return counts.empty(); }

    void swap(count_map_type& x) { counts.swap(x.counts); }
    void clear()
    {
      counts.clear();
    }

    void rehash(size_t hint) { counts.rehash(hint); }

    count_map_type& operator+=(const count_map_type& x)
    {
      counts.rehash(counts.size() + x.counts.size());

      const_iterator citer_end = x.counts.end();
      for (const_iterator citer = x.counts.begin(); citer != citer_end; ++ citer)
	counts[citer->first] += citer->second;
      return *this;
    }
    
    count_map_type& operator|=(const count_map_type& x)
    {
      counts.rehash(counts.size() + x.counts.size());
      
      counts.insert(x.begin(), x.end());
      return *this;
    }
    
    counts_type counts;
  };
  
  typedef utils::alloc_vector<count_map_type, std::allocator<count_map_type> > count_dict_type;
  
  ttable_type(const double __prior=0.1, const double __smooth=1e-20) : ttable(), prior(__prior), smooth(__smooth) {}
  
  count_map_type& operator[](const word_type& word)
  {
    return ttable[word.id()];
  }

  const count_map_type& operator[](const word_type& word) const
  {
    return ttable[word.id()];
  }

  
  double operator()(const word_type& source, const word_type& target) const
  {
    if (source == vocab_type::BOS || source == vocab_type::EOS || target == vocab_type::BOS || target == vocab_type::EOS)
      return source == target;
    
    if (! ttable.exists(source.id())) return smooth;
    
    const count_map_type& counts = ttable[source.id()];
    count_map_type::const_iterator citer = counts.find(target);
    
    return (citer == counts.end() ? smooth : std::max(citer->second, smooth));
  }
  
  
  void shrink() { ttable.shrink(); }
  void clear() { ttable.clear(); }
  
  void clear(const word_type& word)
  {
    if (! ttable.exists(word.id())) return;
    
    ttable.erase(ttable.begin() + word.id());
  }

  void swap(ttable_type& x)
  {
    ttable.swap(x.ttable);
    std::swap(prior,  x.prior);
    std::swap(smooth, x.smooth);
  }

  size_type size() const { return ttable.size(); }
  bool empty() const { return ttable.empty(); }
  bool exists(size_type pos) const { return ttable.exists(pos); }
  
  void resize(size_type __size) { ttable.resize(__size); }
  void reserve(size_type __size) { ttable.reserve(__size); }

  void initialize()
  {
    for (size_type i = 0; i != ttable.size(); ++ i)
      if (ttable.exists(i))
        ttable[i].clear();

    shrink();
  }

  ttable_type& operator+=(const ttable_type& x)
  {
    for (size_type i = 0; i != x.ttable.size(); ++ i) 
      if (x.ttable.exists(i))
	ttable[i] += x.ttable[i];
    
    return *this;
  }

  ttable_type& operator|=(const ttable_type& x)
  {
    for (size_type i = 0; i != x.ttable.size(); ++ i) 
      if (x.ttable.exists(i))
	ttable[i] |= x.ttable[i];
    
    return *this;
  }
  
  count_dict_type ttable;
  double prior;
  double smooth;
};

struct aligned_type
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  struct aligned_map_type
  {
    typedef utils::compact_set<word_type,
			       utils::unassigned<word_type>, utils::unassigned<word_type>,
			       boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<word_type> > map_type;

    typedef map_type::value_type      value_type;
    typedef map_type::size_type       size_type;
    typedef map_type::difference_type difference_type;
    
    typedef map_type::const_iterator const_iterator;
    typedef map_type::iterator       iterator;
  
    typedef map_type::const_reference const_reference;
    typedef map_type::reference       reference;

    aligned_map_type() { }
    
    inline const_iterator begin() const { return aligned.begin(); }
    inline       iterator begin()       { return aligned.begin(); }
    inline const_iterator end() const { return aligned.end(); }
    inline       iterator end()       { return aligned.end(); }
    
    inline const_iterator find(const value_type& x) const { return aligned.find(x); }
    inline       iterator find(const value_type& x)       { return aligned.find(x); }
    
    std::pair<iterator, bool> insert(const value_type& x) { return aligned.insert(x); }
    
    size_type size() const { return aligned.size(); }
    bool empty() const { return aligned.empty(); }
    
    void clear() { aligned.clear(); }
    void swap(aligned_map_type& x)
    {
      aligned.swap(x.aligned);
    }

    aligned_map_type& operator=(const aligned_map_type& x)
    {
      aligned = x.aligned;
      return *this;
    }
    
    aligned_map_type& operator+=(const aligned_map_type& x)
    {
      aligned.rehash(aligned.size() + x.aligned.size());
      
      const_iterator citer_end = x.aligned.end();
      for (const_iterator citer = x.aligned.begin(); citer != citer_end; ++ citer)
	aligned.insert(*citer);
      
      return *this;
    }
    
    map_type aligned;
  };
  
  typedef utils::alloc_vector<aligned_map_type, std::allocator<aligned_map_type> > aligned_set_type;

  aligned_type() : aligned() {}
  
  aligned_map_type& operator[](const word_type& word)
  {
    return aligned[word.id()];
  }
  
  const aligned_map_type& operator[](const word_type& word) const
  {
    return aligned[word.id()];
  }
  
  size_type size() const { return aligned.size(); }
  bool empty() const { return aligned.empty(); }
  bool exists(size_type pos) const { return aligned.exists(pos); }
  bool exists(const word_type& word) const { return aligned.exists(word.id()); }
  
  void resize(size_type __size) { aligned.resize(__size); }
  void reserve(size_type __size) { aligned.reserve(__size); }

  void swap(aligned_type& x) { aligned.swap(x.aligned); }
  
  void shrink() { aligned.shrink(); }
  void clear() { aligned.clear(); }
  void clear(const word_type& word)
  {
    if (! aligned.exists(word.id())) return;
    
    aligned.erase(aligned.begin() + word.id());
  }
  
  void initialize()
  {
    for (size_type i = 0; i != aligned.size(); ++ i)
      if (aligned.exists(i))
        aligned[i].clear();
    
    shrink();
  }
  
  aligned_set_type aligned;
};

struct LearnBase
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef int       index_type;

  static const classes_type& __classes()
  {
    static classes_type __tmp;
    return __tmp;
  }
  
  LearnBase(const ttable_type& __ttable_source_target,
	    const ttable_type& __ttable_target_source)
    : ttable_source_target(__ttable_source_target),
      ttable_target_source(__ttable_target_source),
      ttable_counts_source_target(__ttable_source_target.prior, __ttable_source_target.smooth),
      ttable_counts_target_source(__ttable_target_source.prior, __ttable_target_source.smooth),
      classes_source(__classes()),
      classes_target(__classes()),
      objective_source_target(0),
      objective_target_source(0)
  {}

  LearnBase(const ttable_type& __ttable_source_target,
	    const ttable_type& __ttable_target_source,
	    const atable_type& __atable_source_target,
	    const atable_type& __atable_target_source,
	    const classes_type& __classes_source,
	    const classes_type& __classes_target)
    : ttable_source_target(__ttable_source_target),
      ttable_target_source(__ttable_target_source),
      ttable_counts_source_target(__ttable_source_target.prior, __ttable_source_target.smooth),
      ttable_counts_target_source(__ttable_target_source.prior, __ttable_target_source.smooth),
      atable_source_target(__atable_source_target),
      atable_target_source(__atable_target_source),
      atable_counts_source_target(__atable_source_target.prior, __atable_source_target.smooth),
      atable_counts_target_source(__atable_target_source.prior, __atable_target_source.smooth),
      classes_source(__classes_source),
      classes_target(__classes_target),
      objective_source_target(0),
      objective_target_source(0)
  {}

  void initialize()
  {
    ttable_counts_source_target.clear();
    ttable_counts_target_source.clear();
    ttable_counts_source_target.reserve(word_type::allocated());
    ttable_counts_target_source.reserve(word_type::allocated());
    ttable_counts_source_target.resize(word_type::allocated());
    ttable_counts_target_source.resize(word_type::allocated());
    
    atable_counts_source_target.initialize();
    atable_counts_target_source.initialize();
    
    aligned_source_target.clear();
    aligned_target_source.clear();
    aligned_source_target.reserve(word_type::allocated());
    aligned_target_source.reserve(word_type::allocated());
    aligned_source_target.resize(word_type::allocated());
    aligned_target_source.resize(word_type::allocated());
    
    objective_source_target = 0.0;
    objective_target_source = 0.0;
  }

  void shrink()
  {
    atable_counts_source_target.shrink();
    atable_counts_target_source.shrink();
  };
  
  const ttable_type& ttable_source_target;
  const ttable_type& ttable_target_source;
  ttable_type ttable_counts_source_target;
  ttable_type ttable_counts_target_source;

  atable_type atable_source_target;
  atable_type atable_target_source;
  atable_type atable_counts_source_target;
  atable_type atable_counts_target_source;

  const classes_type& classes_source;
  const classes_type& classes_target;
  
  aligned_type aligned_source_target;
  aligned_type aligned_target_source;
  
  double objective_source_target;
  double objective_target_source;
};

struct ViterbiBase
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef int       index_type;

  static const classes_type& __classes()
  {
    static classes_type __tmp;
    return __tmp;
  }
  
  ViterbiBase(const ttable_type& __ttable_source_target,
	      const ttable_type& __ttable_target_source)
    : ttable_source_target(__ttable_source_target),
      ttable_target_source(__ttable_target_source),
      classes_source(__classes()),
      classes_target(__classes())
  {}
  ViterbiBase(const ttable_type& __ttable_source_target,
	      const ttable_type& __ttable_target_source,
	      const atable_type& __atable_source_target,
	      const atable_type& __atable_target_source,
	      const classes_type& __classes_source,
	      const classes_type& __classes_target)
    : ttable_source_target(__ttable_source_target),
      ttable_target_source(__ttable_target_source),
      atable_source_target(__atable_source_target),
      atable_target_source(__atable_target_source),
      classes_source(__classes_source),
      classes_target(__classes_target)
  {}
  
  const ttable_type& ttable_source_target;
  const ttable_type& ttable_target_source;
  
  atable_type atable_source_target;
  atable_type atable_target_source;
  
  const classes_type& classes_source;
  const classes_type& classes_target;
};

void read_lexicon(const path_type& path, ttable_type& lexicon)
{
  typedef boost::fusion::tuple<std::string, std::string, double > lexicon_parsed_type;
  typedef boost::spirit::istream_iterator iterator_type;

  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;
  
  qi::rule<iterator_type, std::string(), standard::blank_type>         word;
  qi::rule<iterator_type, lexicon_parsed_type(), standard::blank_type> parser; 
  
  word   %= qi::lexeme[+(standard::char_ - standard::space)];
  parser %= word >> word >> qi::double_ >> (qi::eol | qi::eoi);
  
  lexicon.clear();
  
  utils::compress_istream is(path, 1024 * 1024);
  is.unsetf(std::ios::skipws);
  
  iterator_type iter(is);
  iterator_type iter_end;
  
  lexicon_parsed_type lexicon_parsed;
  
  while (iter != iter_end) {
    boost::fusion::get<0>(lexicon_parsed).clear();
    boost::fusion::get<1>(lexicon_parsed).clear();
    
    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, lexicon_parsed))
      if (iter != iter_end)
	throw std::runtime_error("global lexicon parsing failed");
    
    const word_type target(boost::fusion::get<0>(lexicon_parsed));
    const word_type source(boost::fusion::get<1>(lexicon_parsed));
    const double&   prob(boost::fusion::get<2>(lexicon_parsed));
    
    lexicon[source][target] = prob;
  }

  lexicon.shrink();
}

struct ttable_greater_second
{
  template <typename Tp>
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x->second > y->second;
  }
};

void write_lexicon(const path_type& path, const ttable_type& lexicon, const aligned_type& aligned, const double threshold)
{
  typedef ttable_type::count_map_type::value_type value_type;
  typedef std::vector<const value_type*, std::allocator<const value_type*> > sorted_type;
  typedef std::ostream_iterator<char> oiterator_type;
  
  namespace karma = boost::spirit::karma;
  namespace standard = boost::spirit::standard;

  utils::compress_ostream os(path, 1024 * 1024);
  os.precision(20);
  
  oiterator_type oiter(os);
  karma::real_generator<double, ttable_type::real_precision> real;

  const aligned_type::aligned_map_type __empty;
  sorted_type sorted;
  
  ttable_type::count_dict_type::const_iterator siter_begin = lexicon.ttable.begin();
  ttable_type::count_dict_type::const_iterator siter_end   = lexicon.ttable.end();
  for (ttable_type::count_dict_type::const_iterator siter = siter_begin; siter != siter_end; ++ siter) 
    if (*siter) {
      const word_type source(word_type::id_type(siter - siter_begin));
      const ttable_type::count_map_type& dict = *(*siter);
      
      if (dict.empty()) continue;
      
      sorted.clear();
      sorted.reserve(dict.size());
      
      ttable_type::count_map_type::const_iterator titer_end = dict.end();
      for (ttable_type::count_map_type::const_iterator titer = dict.begin(); titer != titer_end; ++ titer)
	if (titer->second >= 0.0)
	  sorted.push_back(&(*titer));
      
      std::sort(sorted.begin(), sorted.end(), ttable_greater_second());
      
      if (threshold > 0.0) {
	const double prob_max       = sorted.front()->second;
	const double prob_threshold = prob_max * threshold;
	
	const aligned_type::aligned_map_type& viterbi = (aligned.exists(source) ? aligned[source] : __empty);
	
	sorted_type::const_iterator iter_end = sorted.end();
	for (sorted_type::const_iterator iter = sorted.begin(); iter != iter_end; ++ iter)
	  if ((*iter)->second >= prob_threshold || viterbi.find((*iter)->first) != viterbi.end())
	    if (! karma::generate(oiter, standard::string << ' ' << standard::string << ' ' << real << '\n', (*iter)->first, source, (*iter)->second))
	      throw std::runtime_error("generation failed");
      } else {
	sorted_type::const_iterator iter_end = sorted.end();
	for (sorted_type::const_iterator iter = sorted.begin(); iter != iter_end; ++ iter)
	  if (! karma::generate(oiter, standard::string << ' ' << standard::string << ' ' << real << '\n', (*iter)->first, source, (*iter)->second))
	    throw std::runtime_error("generation failed");
      }
    }
}

void read_alignment(const path_type& path, atable_type& align)
{
  typedef boost::fusion::tuple<std::string, std::string, int, double > align_parsed_type;
  typedef boost::spirit::istream_iterator iterator_type;

  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;
  
  qi::rule<iterator_type, std::string(), standard::blank_type>       word;
  qi::rule<iterator_type, align_parsed_type(), standard::blank_type> parser; 
  
  word   %= qi::lexeme[+(standard::char_ - standard::space)];
  parser %= word >> word >> qi::int_ >> qi::double_ >> (qi::eol | qi::eoi);
  
  align.clear();
  
  utils::compress_istream is(path, 1024 * 1024);
  is.unsetf(std::ios::skipws);
  
  iterator_type iter(is);
  iterator_type iter_end;
  
  align_parsed_type align_parsed;
  
  while (iter != iter_end) {
    boost::fusion::get<0>(align_parsed).clear();
    boost::fusion::get<1>(align_parsed).clear();
    
    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, align_parsed))
      if (iter != iter_end)
	throw std::runtime_error("global lexicon parsing failed");
    
    const word_type source(boost::fusion::get<0>(align_parsed));
    const word_type target(boost::fusion::get<1>(align_parsed));
    const int&      index(boost::fusion::get<2>(align_parsed));
    const double&   prob(boost::fusion::get<3>(align_parsed));
    
    align[std::make_pair(source, target)][index] = prob;
  }
}

void write_alignment(const path_type& path, const atable_type& align)
{
  utils::compress_ostream os(path, 1024 * 1024);
  os.precision(20);
  
  atable_type::count_dict_type::const_iterator citer_end = align.atable.end();
  for (atable_type::count_dict_type::const_iterator citer = align.atable.begin(); citer != citer_end; ++ citer)
    for (int diff = citer->second.min(); diff <= citer->second.max(); ++ diff)
      os << citer->first.first << ' ' << citer->first.second << ' ' << diff << ' ' << citer->second[diff] << '\n';
}


void read_classes(const path_type& path, classes_type& classes)
{
  typedef boost::spirit::istream_iterator iterator_type;
  typedef std::pair<std::string, std::string> word_pair_type;
  
  namespace qi = boost::spirit::qi;
  namespace standard = boost::spirit::standard;
  
  qi::rule<iterator_type, std::string(), standard::blank_type>    word;
  qi::rule<iterator_type, word_pair_type(), standard::blank_type> parser; 
  
  word   %= qi::lexeme[+(standard::char_ - standard::space)];
  parser %= word >> word >> (qi::eol | qi::eoi);

  classes.clear();
  
  utils::compress_istream is(path, 1024 * 1024);
  is.unsetf(std::ios::skipws);
  
  iterator_type iter(is);
  iterator_type iter_end;
  
  word_pair_type word_pair;
  
  while (iter != iter_end) {
    word_pair.first.clear();
    word_pair.second.clear();
    
    if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, word_pair))
      if (iter != iter_end)
	throw std::runtime_error("cluster parsing failed");
    
    const word_type cluster(word_pair.first);
    const word_type word(word_pair.second);
    
    classes[word] = cluster;
  }
  
  classes[vocab_type::BOS] = vocab_type::BOS;
  classes[vocab_type::EOS] = vocab_type::EOS;

  classes.shrink();
}

#endif

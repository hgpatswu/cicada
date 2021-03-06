//
//  Copyright(C) 2010-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <set>
#include <map>

#include "cicada_impl.hpp"

#include "cicada/stemmer.hpp"
#include "cicada/matcher.hpp"
#include "cicada/eval/ter.hpp"

#include "utils/unordered_map.hpp"
#include "utils/unordered_set.hpp"
#include "utils/program_options.hpp"
#include "utils/resource.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/compact_set.hpp"
#include "utils/getline.hpp"

#include <boost/program_options.hpp>

struct TER
{
  struct weights_type
  {
    double match;
    double substitution;
    double insertion;
    double deletion;
    double shift;
    
    weights_type() : match(0.2), substitution(1.0), insertion(1.0), deletion(1.0), shift(1.0) {}
  };

  struct TRANSITION
  {
    enum transition_type {
      epsilon,
      match,
      approximation,
      substitution,
      insertion,
      deletion,
    };
  };
  
  
  static const int max_shift_size = 10;
  static const int max_shift_dist = 50;
};

typedef cicada::Matcher matcher_type;

struct Normalizer
{
  template <typename Word>
  Word operator()(const Word& x) const
  {
    return x;
  }
};

struct NormalizerLower
{
  NormalizerLower()
    : lower(&cicada::Stemmer::create("lower")) {}
  
  cicada::Stemmer* lower;

  template <typename Word>
  Word operator()(const Word& x) const
  {
    return lower->operator()(x);
  }
};

template <typename M>
struct MinimumEditDistance : public M
{
  typedef cicada::Sentence sentence_type;
  typedef cicada::Symbol   word_type;
  
  typedef TER::TRANSITION::transition_type operation_type;
  typedef std::vector<operation_type, std::allocator<operation_type> > operation_set_type;
  
  typedef utils::vector2<operation_type, std::allocator<operation_type> > matrix_operation_type;
  typedef utils::vector2<double, std::allocator<double> > matrix_cost_type;

  typedef utils::unordered_set<word_type, boost::hash<word_type>, std::equal_to<word_type>, std::allocator<word_type> >::type  word_set_type;
  typedef std::vector<word_set_type, std::allocator<word_set_type> > word_map_type;

  MinimumEditDistance(const TER::weights_type& __weights,
		      const matcher_type* __matcher,
		      const int __debug)
    : weights(__weights), matcher(__matcher), debug(__debug) {}
  
  matrix_cost_type      costs;
  matrix_operation_type ops;

  word_map_type refs;


  void build(const lattice_type& ref)
  {
    refs.clear();
    refs.reserve(ref.size());
    refs.resize(ref.size());

    for (int j = 1; j <= static_cast<int>(ref.size()); ++ j) {
      lattice_type::arc_set_type::const_iterator aiter_begin   = ref[j - 1].begin();
      lattice_type::arc_set_type::const_iterator aiter_end     = ref[j - 1].end();
      
      lattice_type::arc_set_type::const_iterator aiter_epsilon = aiter_end;
      for (lattice_type::arc_set_type::const_iterator aiter = aiter_begin; aiter != aiter_end; ++ aiter)
	refs[j - 1].insert(M::operator()(aiter->label));
    }
  }
  
  double operator()(const sentence_type& hyp, operation_set_type& operations)
  {
    utils::resource start;
    
    costs.clear();
    ops.clear();
    
    costs.reserve(hyp.size() + 1, refs.size() + 1);
    ops.reserve(hyp.size() + 1, refs.size() + 1);
    
    costs.resize(hyp.size() + 1, refs.size() + 1, 0.0);
    ops.resize(hyp.size() + 1, refs.size() + 1);
    
    for (int i = 1; i <= static_cast<int>(hyp.size()); ++ i) {
      costs(i, 0) = costs(i - 1, 0) + weights.insertion;
      ops(i, 0) = TER::TRANSITION::insertion;
    }
    
    for (int j = 1; j <= static_cast<int>(refs.size()); ++ j)
      if (refs[j - 1].find(vocab_type::EPSILON) == refs[j - 1].end()) {
	costs(0, j) = costs(0, j - 1) + weights.deletion;
	ops(0, j) = TER::TRANSITION::deletion;
      } else {
	costs(0, j) = costs(0, j - 1);
	ops(0, j) = TER::TRANSITION::epsilon;
      }
    
    for (int i = 1; i <= static_cast<int>(hyp.size()); ++ i)
      for (int j = 1; j <= static_cast<int>(refs.size()); ++ j) {
	double&         cur_cost = costs(i, j);
	operation_type& cur_op   = ops(i, j);
	
	word_set_type::const_iterator riter_end     = refs[j - 1].end();
	word_set_type::const_iterator riter_match   = refs[j - 1].find(M::operator()(hyp[i - 1]));
	word_set_type::const_iterator riter_epsilon = refs[j - 1].find(vocab_type::EPSILON);

	word_set_type::const_iterator riter_approx = riter_end;
	if (matcher && riter_match == riter_end)
	  for (word_set_type::const_iterator witer = refs[j - 1].begin(); witer != riter_end && riter_approx == riter_end; ++ witer)
	    if (*witer != vocab_type::EPSILON && matcher->operator()(M::operator()(hyp[i - 1]), *witer))
	      riter_approx = witer;
	
	if (riter_match != riter_end) {
	  cur_cost = costs(i - 1, j - 1);
	  cur_op   = TER::TRANSITION::match;
	} else if (riter_approx != riter_end) {
	  cur_cost = costs(i - 1, j - 1) + weights.match;
	  cur_op   = TER::TRANSITION::approximation;
	} else {
	  cur_cost = costs(i - 1, j - 1) + weights.substitution;
	  cur_op   = TER::TRANSITION::substitution;
	}
	
	if (riter_epsilon != riter_end) {
	  const double eps = costs(i, j - 1);
	  if (cur_cost > eps) {
	    cur_cost = eps;
	    cur_op   = TER::TRANSITION::epsilon;
	  }
	} else {
	  const double del = costs(i, j - 1) + weights.deletion;
	  if (cur_cost > del) {
	    cur_cost = del;
	    cur_op   = TER::TRANSITION::deletion;
	  }
	}
	
	const double ins = costs(i - 1, j) + weights.insertion;
	if (cur_cost > ins) {
	  cur_cost = ins;
	  cur_op   = TER::TRANSITION::insertion;
	}
      }
    
    operations.clear();
    int i = hyp.size();
    int j = refs.size();
    while (i > 0 || j > 0) {
      const operation_type& op = ops(i, j);
      operations.push_back(op);
      
      switch (op) {
      case TER::TRANSITION::approximation:
      case TER::TRANSITION::substitution:
      case TER::TRANSITION::match:        -- i; -- j; break;
      case TER::TRANSITION::insertion:    -- i; break;
      case TER::TRANSITION::epsilon:
      case TER::TRANSITION::deletion:     -- j; break;
      }
    }

    utils::resource end;

    if (debug >= 3)
      std::cerr << "minimum edit distance cpu time: " << (end.cpu_time() - start.cpu_time())
		<< " user time: " << (end.user_time() - start.user_time())
		<< std::endl;
    
    return costs(hyp.size(), refs.size());
  }

  TER::weights_type   weights;
  const matcher_type* matcher;
  int debug;
};


template <typename M>
struct TranslationErrorRate : public TER, public M
{
  typedef cicada::Sentence sentence_type;
  typedef cicada::Symbol   word_type;
  
  typedef sentence_type ngram_type;

  typedef std::set<int, std::less<int>, std::allocator<int> > index_set_type;
  
  typedef utils::unordered_map<word_type, index_set_type, boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<std::pair<const word_type, index_set_type> > >::type arc_unique_set_type;
  typedef std::vector<arc_unique_set_type, std::allocator<arc_unique_set_type> > lattice_unique_type;

  typedef utils::compact_set<word_type,
			     utils::unassigned<word_type>, utils::unassigned<word_type>,
			     boost::hash<word_type>, std::equal_to<word_type>,
			     std::allocator<word_type> > word_set_type;  


  typedef MinimumEditDistance<M> med_type;
  typedef typename med_type::operation_type     operation_type;
  typedef typename med_type::operation_set_type operation_set_type;
  typedef operation_set_type path_type;

  typedef std::vector<bool, std::allocator<bool> > error_set_type;
  typedef std::vector<int, std::allocator<int> > align_set_type;
  

  struct Score
  {
    double score;
    int match;
    int insertion;
    int deletion;
    int substitution;
    int shift;
	
    Score() : score(), match(0), insertion(0), deletion(0), substitution(0), shift(0) {}
  };
  
  struct Shift
  {
    Shift() : begin(0), end(0), moveto(0), reloc(0) {}
    Shift(const int __begin, const int __end, const int __moveto, const int __reloc)
      : begin(__begin), end(__end), moveto(__moveto), reloc(__reloc) {}
	
    int begin;
    int end;
    int moveto;
    int reloc;

    friend
    bool operator<(const Shift& x, const Shift& y)
    {
      return (x.begin < y.begin
	      || (x.begin == y.begin
		  && (x.end < y.end
		      || (x.end == y.end
			  && x.reloc < y.reloc))));
    }
  };
  
  typedef Score value_type;
  typedef Shift shift_type;
  
  typedef std::set<shift_type, std::less<shift_type>, std::allocator<shift_type> > shift_set_type;
  typedef std::vector<shift_set_type, std::allocator<shift_set_type> > shift_matrix_type;

  TranslationErrorRate(const TER::weights_type& __weights,
		       const matcher_type* __matcher,
		       const int __debug)
    : minimum_edit_distance(__weights, __matcher, __debug),
      weights(__weights),
      matcher(__matcher),
      debug(__debug) {}
  
  med_type minimum_edit_distance;
  TER::weights_type   weights;
  const matcher_type* matcher;
  int debug;

  double operator()(const lattice_type& ref, const sentence_type& hyp, value_type& value, path_type& path, sentence_type& hyp_reordered)
  {
    hyp_reordered = hyp;

    return calculate_best_shifts(ref, hyp_reordered, value, path);
  }
  
  
  double calculate_best_shifts(const lattice_type& ref, sentence_type& hyp, value_type& value, path_type& path)
  {
    minimum_edit_distance.build(ref);

    value = value_type();
    path.clear();
    double        cost = minimum_edit_distance(hyp, path);
    
    sentence_type hyp_new;
    path_type     path_new;
    double        cost_new;
    
    lattice_unique_type lattice_unique(ref.size() + 1);
    
    build_unique_lattice(ref, hyp, lattice_unique);

    while (1) {
      hyp_new.clear();
      path_new.clear();
      cost_new = 0;
      
      if (! calculate_best_shift(ref, hyp, path, cost, hyp_new, path_new, cost_new, lattice_unique))
	break;
      
      value.score += weights.shift;
      ++ value.shift;
      
      hyp.swap(hyp_new);
      path.swap(path_new);
      cost = cost_new;
    }
    
#if 0 
    // we do not need this...
    typename path_type::const_iterator piter_end = path.end();
    for (typename path_type::const_iterator piter = path.begin(); piter != piter_end; ++ piter) {
      switch (*piter) {
      case TRANSITION::match:        ++ value.match; break;
      case TRANSITION::substitution: ++ value.substitution; break;
      case TRANSITION::insertion:    ++ value.insertion; break;
      case TRANSITION::deletion:     ++ value.deletion; break;
      }
    }
#endif
    
    value.score += cost;
    
    return value.score;
  }
  
  void find_alignment_error(const path_type& path,
			    error_set_type& herr,
			    error_set_type& rerr,
			    align_set_type& ralign) const
  {
    int hpos = -1;
    int rpos = -1;
    
    //std::cerr << "edit distance: ";
    typename path_type::const_iterator piter_end = path.end();
    for (typename path_type::const_iterator piter = path.begin(); piter != piter_end; ++ piter) {
      switch (*piter) {
      case TRANSITION::match:
	//std::cerr << " M";
	++ hpos;
	++ rpos;
	herr.push_back(false);
	rerr.push_back(false);
	ralign.push_back(hpos);
	break;
      case TRANSITION::approximation:
	//std::cerr << " A";
	++ hpos;
	++ rpos;
	herr.push_back(false);
	rerr.push_back(false);
	ralign.push_back(hpos);
	break;
      case TRANSITION::substitution:
	//std::cerr << " S";
	++ hpos;
	++ rpos;
	herr.push_back(true);
	rerr.push_back(true);
	ralign.push_back(hpos);
	break;
      case TRANSITION::insertion:
	//std::cerr << " I";
	++ hpos;
	herr.push_back(true);
	break;
      case TRANSITION::deletion:
	//std::cerr << " D";
	++ rpos;
	rerr.push_back(true);
	ralign.push_back(hpos);
	break;
      case TRANSITION::epsilon:
	//std::cerr << " E";
	++ rpos;
	rerr.push_back(false); // ???
	ralign.push_back(hpos);
	break;
      }
    }
    //std::cerr << std::endl;
  }
  
  bool calculate_best_shift(const lattice_type& ref,
			    const sentence_type& hyp,
			    const path_type& path,
			    const double cost,
			    sentence_type& hyp_best,
			    path_type& path_best,
			    double& cost_best,
			    const lattice_unique_type& lattice_unique)
  {
    
    error_set_type herr;
    error_set_type rerr;
    align_set_type ralign;
    
    herr.reserve(path.size());
    rerr.reserve(path.size());
    ralign.reserve(path.size());
    
    find_alignment_error(path, herr, rerr, ralign);
    
    shift_matrix_type shifts(max_shift_size + 1);
    
    gather_all_possible_shifts(hyp, ralign, herr, rerr, lattice_unique, shifts);
    
    double cost_shift_best = 0;
    cost_best = cost;
    bool found = false;
    
    // enumerate from max-shifts
    sentence_type hyp_shifted(hyp.size());
    path_type     path_shifted;
    
    for (int i = shifts.size() - 1; i >= 0; -- i) 
      if (! shifts[i].empty()) {
	if (debug >= 2)
	  std::cerr << "shift phrase: " << (i + 1) << " # of shifts: " << shifts[i].size() << std::endl;

	const double curfix = cost - (cost_shift_best + cost_best);
	const double maxfix = 2.0 * (i + 1);
	
	if (curfix > maxfix || (cost_shift_best != 0 && curfix == maxfix)) break;
	
	typename shift_set_type::const_iterator siter_end = shifts[i].end();
	for (typename shift_set_type::const_iterator siter = shifts[i].begin(); siter != siter_end; ++ siter) {
	  const shift_type& shift = *siter;
	      
	  const double curfix = cost - (cost_shift_best + cost_best);
	      
	  if (curfix > maxfix || (cost_shift_best != 0 && curfix == maxfix)) break;
	  
	  //std::cerr << "candidate shift: [" << shift.begin << ", " << shift.end << "]: " << shift.reloc << std::endl;
	  
	  perform_shift(hyp, shift, hyp_shifted);
	  
	  const double cost_shifted = minimum_edit_distance(hyp_shifted, path_shifted);
	  const double gain = (cost_best + cost_shift_best) - (cost_shifted + weights.shift);
	      
	  //std::cerr << "hyp original: " << hyp << std::endl;
	  //std::cerr << "hyp shifted:  " << hyp_shifted << std::endl;
	  //std::cerr << "cost shifted: " << cost_shifted << " gain: " << gain << std::endl;
	    
	  if (gain > 0 || (cost_shift_best == 0 && gain == 0)) {
	    cost_best       = cost_shifted;
	    cost_shift_best = weights.shift;
		
	    path_best.swap(path_shifted);
	    hyp_best.swap(hyp_shifted);
	    found = true;
	    
	    //std::cerr << "better shift: [" << shift.begin << ", " << shift.end << "]: " << shift.reloc << std::endl;
	  }
	}
      }
    
    return found;
  }

  void perform_shift(const sentence_type& sentence,
		     const shift_type& shift,
		     sentence_type& shifted) const
  {
    shifted.clear();
	
    std::back_insert_iterator<sentence_type> oiter(shifted);
	
    sentence_type::const_iterator siter_begin = sentence.begin();
    sentence_type::const_iterator siter_end = sentence.end();

    if (shift.reloc == -1) {
      std::copy(siter_begin + shift.begin, siter_begin + shift.end + 1, oiter);
      std::copy(siter_begin, siter_begin + shift.begin, oiter);
      std::copy(siter_begin + shift.end + 1, siter_end, oiter);
    } else if (shift.reloc < shift.begin) {
      std::copy(siter_begin, siter_begin + shift.reloc + 1, oiter);
      std::copy(siter_begin + shift.begin, siter_begin + shift.end + 1, oiter);
      std::copy(siter_begin + shift.reloc + 1, siter_begin + shift.begin, oiter);
      std::copy(siter_begin + shift.end + 1, siter_end, oiter);
    } else if (shift.end < shift.reloc) {
      std::copy(siter_begin, siter_begin + shift.begin, oiter);
      std::copy(siter_begin + shift.end + 1, siter_begin + shift.reloc + 1, oiter);
      std::copy(siter_begin + shift.begin, siter_begin + shift.end + 1, oiter);
      std::copy(siter_begin + shift.reloc + 1, siter_end, oiter);
    } else {
      std::copy(siter_begin, siter_begin + shift.begin, oiter);
      std::copy(siter_begin + shift.end + 1, std::min(siter_end, siter_begin + shift.end + shift.reloc - shift.begin + 1), oiter);
      std::copy(siter_begin + shift.begin, siter_begin + shift.end + 1, oiter);
      std::copy(siter_begin + shift.end + shift.reloc - shift.begin + 1, siter_end, oiter);
    }

    if (sentence.size() != shifted.size())
      throw std::runtime_error(std::string("size do not match:")
			       + " original: " + utils::lexical_cast<std::string>(sentence.size())
			       + " shifted: "  + utils::lexical_cast<std::string>(shifted.size()));
  }

  void gather_all_possible_shifts(const sentence_type& hyp,
				  const align_set_type& ralign,
				  const error_set_type& herr,
				  const error_set_type& rerr,
				  const lattice_unique_type& lattice_unique,
				  shift_matrix_type& shifts) const
  {
    if (debug >= 2)
      std::cerr << "gather possible shifts:"
		<< " hyp: " << hyp.size()
		<< " ref: " << lattice_unique.size()
		<< " raligh: " << ralign.size()
		<< " herr: " << herr.size()
		<< " rerr: " << rerr.size()
		<< std::endl;

    utils::resource start;

    index_set_type indices;
    index_set_type indices_next;
    
    for (int start = 0; start != static_cast<int>(hyp.size()); ++ start)
      for (int moveto = 0; moveto != static_cast<int>(lattice_unique.size()); ++ moveto) 
	if (ralign[moveto] != start && (ralign[moveto] - start <= max_shift_dist) && (start - ralign[moveto] - 1 <= max_shift_dist)) {
	  
	  if (debug >= 4)
	    std::cerr << "start: " << start
		      << " moveto: " << moveto
		      << " ralign[moveto]: " << ralign[moveto]
		      << std::endl;
	  
	  indices.clear();
	  indices_next.clear();
	  indices.insert(moveto);
	  
	  const int last = utils::bithack::min(start + max_shift_size, static_cast<int>(hyp.size()));
	  for (int end = start; end != last; ++ end) {
	    indices_next.clear();
	    {
	      index_set_type::const_iterator iiter_end = indices.end();
	      for (index_set_type::const_iterator iiter = indices.begin(); iiter != iiter_end; ++ iiter) {
		arc_unique_set_type::const_iterator aiter = lattice_unique[*iiter].find(M::operator()(hyp[end]));
		if (aiter == lattice_unique[*iiter].end()) continue;
		
		indices_next.insert(aiter->second.begin(), aiter->second.end());
	      }
	    }
	    indices.swap(indices_next);
	    indices_next.clear();
	    
	    if (indices.empty()) break;
	    
	    if (! (ralign[moveto] < start || end < ralign[moveto])) continue;
	    
	    // we will shift to position where we expect error (and then, recovered after shift)
	    error_set_type::const_iterator hiter_begin = herr.begin() + start;
	    error_set_type::const_iterator hiter_end   = herr.begin() + end + 1;
	    if (std::find(hiter_begin, hiter_end, true) == hiter_end)
	      continue;
	    
	    index_set_type::const_iterator iiter_end = indices.end();
	    for (index_set_type::const_iterator iiter = indices.begin(); iiter != iiter_end; ++ iiter) {
	      
	      error_set_type::const_iterator riter_begin = rerr.begin() + moveto;
	      error_set_type::const_iterator riter_end   = rerr.begin() + *iiter;
	      //error_set_type::const_iterator riter_end   = rerr.begin() + end - start + 1 + moveto;
	      
	      if (std::find(riter_begin, riter_end, true) == riter_end)
		continue;
	      
	      shift_set_type& sshifts = shifts[end - start];
	      
	      for (int roff = -1; roff <= *iiter - moveto - 1; ++ roff) {
		if (roff == -1 && moveto == 0)
		  sshifts.insert(shift_type(start, end, -1, -1));
		else if (start != ralign[moveto + roff] && (roff == 0 || ralign[moveto + roff] != ralign[moveto]))
		  sshifts.insert(shift_type(start, end, moveto + roff, ralign[moveto + roff]));
	      }
	    }
	  }
	}
    
    utils::resource end;
    
    if (debug >= 2)
      std::cerr << "gather all possible shifts cpu time: " << (end.cpu_time() - start.cpu_time())
		<< " user time: " << (end.user_time() - start.user_time())
		<< std::endl;
  }

  void build_unique_lattice(const lattice_type& ref, const sentence_type& hyp, lattice_unique_type& lattice_unique) const
  {
    //
    // we will build unique-reduced-lattice, lattice without epsilon | without words not in hyp
    // here, we do not care the weights associated with each edge...
    //

    typedef std::vector<int, std::allocator<int> > precedent_set_type;
    
    word_set_type words_intersect;
    for (sentence_type::const_iterator hiter = hyp.begin(); hiter != hyp.end(); ++ hiter)
      words_intersect.insert(M::operator()(*hiter));
    

    for (int start = 0; start != static_cast<int>(ref.size()); ++ start) {
      
      const int max_length = utils::bithack::min(max_shift_size, static_cast<int>(ref.size() - start));
      
      bool has_next = true;
      
      for (int length = 0; has_next && length != max_length; ++ length) {
	
	has_next = false;
	for (size_t pos = 0; pos != ref[start + length].size(); ++ pos) {
	  if (ref[start + length][pos].label == vocab_type::EPSILON)
	    has_next = true;
	  else if (words_intersect.find(M::operator()(ref[start + length][pos].label)) != words_intersect.end())
	    lattice_unique[start][M::operator()(ref[start + length][pos].label)].insert(start + length + 1);
	}
      }
    }
  }
};

template <typename M>
struct TERAligner : public TER, public M
{
  // given the alignment, merge the reordered-hyp to the lattice...
  
  typedef TranslationErrorRate<M> ter_type;

  typedef typename ter_type::path_type  path_type;
  typedef typename ter_type::value_type value_type;
  
  TERAligner(const TER::weights_type& weights,
	     const matcher_type* matcher,
	     const int __debug) : ter(weights, matcher, __debug) {}
  
  double operator()(const lattice_type& ref, const sentence_type& hyp, lattice_type& merged, const feature_set_type& features)
  {
    merged.clear();
    
    if (ref.empty()) {
      sentence_type::const_iterator siter_end = hyp.end();
      for (sentence_type::const_iterator siter = hyp.begin(); siter != siter_end; ++ siter)
	merged.push_back(lattice_type::arc_set_type(1, lattice_type::arc_type(*siter, features, 1)));

      return 0.0;
    } else {
      sentence_type shifted;
      path_type     path;
      value_type    value;
      
      ter(ref, hyp, value, path, shifted);
      
      int hpos = 0;
      int rpos = 0;

      typename path_type::const_iterator piter_end = path.end();
      for (typename path_type::const_iterator piter = path.begin(); piter != piter_end; ++ piter) {
	switch (*piter) {
	case TRANSITION::approximation:
	case TRANSITION::match:
	case TRANSITION::substitution:
	  merged.push_back(ref[rpos]);
	  insert_unique_node(merged.back(), shifted[hpos], features);
	  ++ hpos;
	  ++ rpos;
	  break;
	case TRANSITION::epsilon:
	case TRANSITION::deletion:
	  merged.push_back(ref[rpos]);
	  insert_unique_epsilon(merged.back());
	  ++ rpos;
	  break;
	case TRANSITION::insertion:
	  merged.push_back(lattice_type::arc_set_type());
	  merged.back().push_back(lattice_type::arc_type(vocab_type::EPSILON));
	  merged.back().push_back(lattice_type::arc_type(shifted[hpos], features, 1));
	  ++ hpos;
	  break;
	}
      }

      return value.score;
    }
  }

  template <typename ArcSet, typename Word>
  void insert_unique_node(ArcSet& arcs, const Word& word, const feature_set_type& features)
  {
    typename ArcSet::iterator aiter_end = arcs.end();
    typename ArcSet::iterator aiter_find = aiter_end;
    for (typename ArcSet::iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
      if (aiter->label == word) {
	aiter_find = aiter;
	break;
      }
    
    if (aiter_find == aiter_end)
      arcs.push_back(typename ArcSet::value_type(word, features, 1));
    else
      aiter_find->features += features;
  }
  
  template <typename ArcSet>
  void insert_unique_epsilon(ArcSet& arcs)
  {
    typename ArcSet::iterator aiter_end = arcs.end();
    typename ArcSet::iterator aiter_find = aiter_end;
    for (typename ArcSet::iterator aiter = arcs.begin(); aiter != aiter_end; ++ aiter)
      if (aiter->label == vocab_type::EPSILON) {
	aiter_find = aiter;
	break;
      }
    
    if (aiter_find == aiter_end)
      arcs.push_back(typename ArcSet::value_type(vocab_type::EPSILON));
  }
  
  ter_type ter;
};

typedef std::vector<path_type, std::allocator<path_type> > path_set_type;
typedef std::vector<feature_type, std::allocator<feature_type> > feature_list_type;

path_set_type input_files;
path_type output_file = "-";
path_type confidence_feature_file;
path_type count_feature_file;

bool merge_all = false;
bool match_lower = false;

std::string       matcher;
TER::weights_type weights;

std::string confidence;
std::string count;
double count_weight = 1.0;

bool multiple_mode = false;

int debug = 0;

// input mode... use of one-line lattice input or sentence input?
void options(int argc, char** argv);


void merge_sentences_all(const sentence_set_type& sentences,
			 const feature_list_type& features_confidence,
			 const feature_list_type& features_count,
			 const feature_type& feature_confidence,
			 const feature_type& feature_count,
			 lattice_type& merged_all)
{
  if (! features_confidence.empty() && sentences.size() != features_confidence.size())
    throw std::runtime_error("confidence feature list size does not match # of sentences");
  if (! features_count.empty() && sentences.size() != features_count.size())
    throw std::runtime_error("count feature list does not match # of sentences");
  
  const matcher_type* __matcher = 0;
  if (! matcher.empty())
    __matcher = &matcher_type::create(matcher);
  
  
  typedef std::multimap<double, int, std::less<double>, std::allocator<std::pair<const double, int> > > ter_sent_type;

  // we will merge sentences in lower-ter order...

  merged_all.clear();
  lattice_type  merged;
  lattice_type  merged_new;
  
  for (size_t sent = 0; sent != sentences.size(); ++ sent) 
    if (! sentences[sent].empty()) {

      // perform merging...
      if (debug)
	std::cerr << "merging with bed sentence: " << sentences[sent] << std::endl;
      
      merged.clear();
      merged_new.clear();
      
      ter_sent_type ters;
	  
      boost::shared_ptr<cicada::eval::Scorer> scorer(match_lower
						     ? cicada::eval::Scorer::create("ter:tokenizer=lower")
						     : cicada::eval::Scorer::create("ter"));
      scorer->insert(sentences[sent]);
	  
      for (size_t id = 0; id != sentences.size(); ++ id)
	if (id != sent && ! sentences[id].empty()) 
	  ters.insert(std::make_pair(scorer->score(sentences[id])->score(), id));
	  
      int rank = 1;
      const double conf = 1.0 / (1.0 + rank);
	
      feature_set_type features;
      
      if (! features_confidence.empty())
	features[features_confidence[sent]] = conf;
      if (! features_count.empty())
	features[features_count[sent]] = count_weight;
      if (! feature_confidence.empty())
	features[feature_confidence] = conf;
      if (! feature_count.empty())
	features[feature_count] = count_weight;
	  
      if (match_lower) {
	TERAligner<NormalizerLower> aligner(weights, __matcher, debug);
	aligner(merged, sentences[sent], merged_new, features);
      } else {
	TERAligner<Normalizer> aligner(weights, __matcher, debug);
	aligner(merged, sentences[sent], merged_new, features);
      }
	
      merged.swap(merged_new);
      merged_new.clear();
      
      double score = 0.0;
      
      ++ rank;
	  
      ter_sent_type::const_iterator titer_end = ters.end();
      for (ter_sent_type::const_iterator titer = ters.begin(); titer != titer_end; ++ titer, ++ rank) {
	const int id = titer->second;

	if (debug)
	  std::cerr << "merging: " << sentences[id] << std::endl;

	const double conf = 1.0 / (1.0 + rank);
	    
	feature_set_type features;
	
	if (! features_confidence.empty())
	  features[features_confidence[id]] = conf;
	if (! features_count.empty())
	  features[features_count[id]] = count_weight;
	if (! feature_confidence.empty())
	  features[feature_confidence] = conf;
	if (! feature_count.empty())
	  features[feature_count] = count_weight;
	    
	double score_local = 0.0;
	if (match_lower) {
	  TERAligner<NormalizerLower> aligner(weights, __matcher, debug);
	  score_local = aligner(merged, sentences[id], merged_new, features);
	} else {
	  TERAligner<Normalizer> aligner(weights, __matcher, debug);
	  score_local = aligner(merged, sentences[id], merged_new, features);
	}
	if (debug)
	  std::cerr << "TER: " << score_local << std::endl;
	    
	score += score_local;
	    
	merged.swap(merged_new);
	merged_new.clear();
      }
	  
      if (debug)
	std::cerr << "lattice size: " << merged.size() << std::endl;
	  
      // merged is now merged into meregd_all

      if (merged_all.empty()) {
	merged_all.push_back(lattice_type::arc_set_type());
	merged_all.back().push_back(lattice_type::arc_type(vocab_type::EPSILON, feature_set_type(), 1));
	merged_all.back().back().features["edit-distance"] = score / merged.size();
	  
	for (size_t i = 0; i != merged.size(); ++ i)
	  merged_all.push_back(merged[i]);
      } else {
	merged_new.clear();
	merged_new.push_back(lattice_type::arc_set_type());
	merged_new.back().push_back(lattice_type::arc_type(vocab_type::EPSILON, feature_set_type(), 1));
	merged_new.back().push_back(lattice_type::arc_type(vocab_type::EPSILON, feature_set_type(), merged_all.size() + 2));
	merged_new.back().back().features["edit-distance"] = score / merged.size();
	
	for (size_t i = 0; i != merged_all.size(); ++ i)
	  merged_new.push_back(merged_all[i]);
	
	merged_new.push_back(lattice_type::arc_set_type());
	merged_new.back().push_back(lattice_type::arc_type(vocab_type::EPSILON, feature_set_type(), merged.size() + 1));
	  
	for (size_t i = 0; i != merged.size(); ++ i)
	  merged_new.push_back(merged[i]);
	  
	merged_all.swap(merged_new);
	merged_new.clear();
      }
    }
}

void merge_sentences(const sentence_set_type& sentences,
		     const feature_list_type& features_confidence,
		     const feature_list_type& features_count,
		     const feature_type& feature_confidence,
		     const feature_type& feature_count,
		     lattice_type& merged)
{
  if (! features_confidence.empty() && sentences.size() != features_confidence.size())
    throw std::runtime_error("confidence feature list size does not match # of sentences");
  if (! features_count.empty() && sentences.size() != features_count.size())
    throw std::runtime_error("count feature list does not match # of sentences");
  
  const matcher_type* __matcher = 0;
  if (! matcher.empty())
    __matcher = &matcher_type::create(matcher);
  
  merged.clear();
  lattice_type merged_new;
  sentence_type sentence;
  
  int rank = 1;
  for (size_t id = 0; id != sentences.size(); ++ id, ++ rank) {
    const sentence_type& sentence = sentences[id];
    
    if (sentence.empty()) continue;
    
    // perform merging...
    if (debug)
      std::cerr << "merging: " << sentence << std::endl;
    
    const double conf = 1.0 / (1.0 + rank);
    
    feature_set_type features;
    
    if (! features_confidence.empty())
      features[features_confidence[id]] = conf;
    if (! features_count.empty())
      features[features_count[id]] = count_weight;
    if (! feature_confidence.empty())
      features[feature_confidence] = conf;
    if (! feature_count.empty())
      features[feature_count] = count_weight;
    
    if (match_lower) {
      TERAligner<NormalizerLower> aligner(weights, __matcher, debug);
      aligner(merged, sentence, merged_new, features);
    } else {
      TERAligner<Normalizer> aligner(weights, __matcher, debug);
      aligner(merged, sentence, merged_new, features);
    }
    
    merged.swap(merged_new);
    merged_new.clear();
    
    if (debug)
      std::cerr << "lattice size: " << merged.size() << std::endl;
    
    if (debug >= 2)
      std::cerr << merged << std::endl;
  }
}

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);

    feature_list_type features_confidence;
    feature_list_type features_count;

    if (! confidence_feature_file.empty()) {
      if (confidence_feature_file != "-" && ! boost::filesystem::exists(confidence_feature_file))
	throw std::runtime_error("no confidence feature file? " + confidence_feature_file.string());
      
      utils::compress_istream is(confidence_feature_file);
      std::string feature;
      while (is >> feature)
	features_confidence.push_back(feature);
    }
    
    if (! count_feature_file.empty()) {
      if (count_feature_file != "-" && ! boost::filesystem::exists(count_feature_file))
	throw std::runtime_error("no count feature file? " + count_feature_file.string());
      
      utils::compress_istream is(count_feature_file);
      std::string feature;
      while (is >> feature)
	features_count.push_back(feature);
    }

    const bool flush_output = (output_file == "-"
                               || (boost::filesystem::exists(output_file)
                                   && ! boost::filesystem::is_regular_file(output_file)));

    cicada::Feature feature_confidence(confidence);
    cicada::Feature feature_count(count);
    
    if (input_files.empty())
      input_files.push_back("-");
    
    if (multiple_mode) {
      utils::compress_ostream os(output_file, 1024 * 1024 * (! flush_output));
      
      lattice_type merged;
      
      sentence_set_type sentences;
      
      for (path_set_type::const_iterator iter = input_files.begin(); iter != input_files.end(); ++ iter) {
	utils::compress_istream is(input_files.front(), 1024 * 1024);
	std::string line;
	
	while (utils::getline(is, line)) {
	  sentences.assign(line);
	  
	  if (merge_all)
	    merge_sentences_all(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
	  else
	    merge_sentences(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
	  
	  os << merged << '\n';
	}
      }
      
    } else if (input_files.size() == 1) {
      
      sentence_set_type sentences;
      {
	sentence_type sentence;
	std::string   line;
	
	utils::compress_istream is(input_files.front(), 1024 * 1024);
	while (utils::getline(is, line)) {
	  std::string::const_iterator iter = line.begin();
	  std::string::const_iterator end = line.end();
	  
	  if (! sentence.assign(iter, end))
	    throw std::runtime_error("invalid sentence...");
	  
	  sentences.push_back(sentence);
	}
      }
      
      lattice_type merged;
      
      if (merge_all)
	merge_sentences_all(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
      else
	merge_sentences(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
      
      utils::compress_ostream os(output_file, 1024 * 1024 * (! flush_output));
      
      os << merged << '\n';
    } else {
      if (! features_confidence.empty())
	if (input_files.size() != features_confidence.size())
	  throw std::runtime_error("input file do not match with # of confidence feature");
      
      if (! features_count.empty())
	if (input_files.size() != features_count.size())
	  throw std::runtime_error("input file do not match with # of count feature");
      
      typedef std::vector<std::istream*, std::allocator<std::istream*> > istream_set_type;
      
      istream_set_type istreams(input_files.size());
      for (size_t i = 0; i != input_files.size(); ++ i)
	istreams[i] = new utils::compress_istream(input_files[i], 1024 * 1024);
      
      utils::compress_ostream os(output_file, 1024 * 1024 * (! flush_output));
      
      lattice_type merged;

      sentence_set_type sentences;
      sentence_type     sentence;
      std::string line;
      
      for (;;) {
	int rank = 1;
	
	sentences.clear();
	size_t num_failed = 0;
	for (size_t id = 0; id != istreams.size(); ++ id, ++ rank) {
	  if (utils::getline(*istreams[id], line)) {
	    std::string::const_iterator iter = line.begin();
	    std::string::const_iterator end = line.end();
	    
	    if (! sentence.assign(iter, end))
	      throw std::runtime_error("invalid sentence...");
	    
	    sentences.push_back(sentence);
	  } else
	    ++ num_failed;
	}
	
	if (num_failed) {
	  if (num_failed != istreams.size())
	    throw std::runtime_error("# of lines do not match");
	  break;
	}
	
	if (merge_all)
	  merge_sentences_all(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
	else
	  merge_sentences(sentences, features_confidence, features_count, feature_confidence, feature_count, merged);
	
	os << merged << '\n';
      }
      
      for (size_t i = 0; i != istreams.size(); ++ i)
	delete istreams[i];
    }
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}


void options(int argc, char** argv)
{
  namespace po = boost::program_options;

  po::variables_map variables;
  
  po::options_description desc("options");
  desc.add_options()
    ("input",  po::value<path_set_type>(&input_files)->multitoken(),   "input sentences")
    ("output", po::value<path_type>(&output_file)->default_value("-"), "output merged lattice")
    
    ("confidence-feature-file", po::value<path_type>(&confidence_feature_file), "confidence feature file")
    ("count-feature-file",      po::value<path_type>(&count_feature_file),      "count feature file")
    
    ("merge",   po::bool_switch(&merge_all),      "merge all the lattice")
    ("lower",   po::bool_switch(&match_lower),    "lower-casing")
    ("matcher", po::value<std::string>(&matcher), "approximate matcher")
    
    ("match",        po::value<double>(&weights.match),        "approximated matching cost")
    ("substitution", po::value<double>(&weights.substitution), "substitution cost")
    ("insertion",    po::value<double>(&weights.insertion),    "insertion cost")
    ("deletion",     po::value<double>(&weights.deletion),     "deletion cost")
    ("shift",        po::value<double>(&weights.shift),        "shift cost")
    
    ("confidence",   po::value<std::string>(&confidence),    "add confidence weight feature name")
    ("count",        po::value<std::string>(&count),         "add count weight feature name")
    ("count-weight", po::value<double>(&count_weight),       "count weight")

    ("multiple", po::bool_switch(&multiple_mode), "multiple forest in one line")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");

  po::positional_options_description pos;
  pos.add("input", -1); // all the files

  po::command_line_parser parser(argc, argv);
  parser.style(po::command_line_style::unix_style & (~po::command_line_style::allow_guessing));
  parser.options(desc);
  parser.positional(pos);
  
  po::store(parser.run(), variables);
  po::notify(variables); 
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}

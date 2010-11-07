#ifndef __CICADA__EXTRACT_PHRASE_IMPL__HPP__
#define __CICADA__EXTRACT_PHRASE_IMPL__HPP__ 1

#include <string>
#include <vector>

#include <boost/array.hpp>

#include "cicada/sentence.hpp"
#include "cicada/alignment.hpp"

#include "utils/sgi_hash_set.hpp"
#include "utils/chart.hpp"

struct ExtractPhrase
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  typedef cicada::Sentence  sentence_type;
  typedef cicada::Alignment alignment_type;
  
  typedef std::string phrase_type;
  typedef boost::array<double, 5> counts_type;

  typedef std::pair<int, int> span_type;

  struct span_pair_type
  {
    span_type source;
    span_type target;
    
    span_pair_type() : source(), target() {}
    span_pair_type(const span_type& __source, const span_type& __target) : source(__source), target(__target) {}
  };
  
  struct phrase_pair_type
  {
    phrase_type    source;
    phrase_type    target;
    alignment_type alignment;
    counts_type    counts;

    phrase_pair_type() {}

    friend
    size_t hash_value(phrase_pair_type const& x)
    {
      typedef utils::hashmurmur<size_t> hasher_type;
    
      return hasher_type()(x.source.begin(), x.source.end(),
			   hasher_type()(x.target.begin(), x.target.end(),
					 hasher_type()(x.alignment.begin(), x.alignment.end(), 0)));
    }

    friend
    bool operator==(const phrase_pair_type& x, const phrase_pair_type& y) 
    {
      return x.source == y.source && x.target == y.target && x.alignment == y.alignment;
    }
  
    friend
    bool operator!=(const phrase_pair_type& x, const phrase_pair_type& y) 
    {
      return x.source != y.source || x.target != y.target || x.alignment != y.alignment;
    }
  
    friend
    bool operator<(const phrase_pair_type& x, const phrase_pair_type& y)
    {
      return (x.source < y.source
	      || (!(y.source < x.source)
		  && (x.target < y.target
		      || (!(y.target < x.target)
			  && x.alignment < y.alignment))));
    }

    friend
    bool operator>(const phrase_pair_type& x, const phrase_pair_type& y)
    {
      return y < x;
    }
  };
  

#ifdef HAVE_TR1_UNORDERED_SET
  typedef std::tr1::unordered_set<phrase_pair_type, boost::hash<phrase_pair_type>, std::equal_to<phrase_pair_type>,
				  std::allocator<phrase_pair_type> > phrase_pair_set_type;
#else
  typedef sgi::hash_set<phrase_pair_type, boost::hash<phrase_pair_type>, std::equal_to<phrase_pair_type>,
			std::allocator<phrase_pair_type> > phrase_pair_set_type;
#endif
  
  typedef utils::chart<phrase_type, std::allocator<phrase_type> >      phrase_chart_type;
  typedef utils::chart<span_type, std::allocator<span_type> >          span_chart_type;
  typedef std::vector<int, std::allocator<int> >                       alignment_count_set_type;
  typedef std::vector<span_type, std::allocator<span_type> >           span_set_type;
  
  typedef std::vector<int, std::allocator<int> > point_set_type;
  typedef std::vector<point_set_type, std::allocator<point_set_type> > alignment_multiple_type;

  phrase_chart_type phrases_source;
  phrase_chart_type phrases_target;
  
  alignment_multiple_type alignment_source_target;
  alignment_multiple_type alignment_target_source;
  
  span_chart_type span_source_chart;
  span_chart_type span_target_chart;
  
  alignment_count_set_type alignment_count_source;
  alignment_count_set_type alignment_count_target;
  
  void operator()(const sentence_type& source,
		  const sentence_type& target,
		  const alignment_type& alignment,
		  phrase_pair_set_type& phrase_pairs)
  {
    // first, extract spans...
    const size_type source_size = source.size();
    const size_type target_size = target.size();

    phrases_source.clear();
    phrases_target.clear();
    
    phrases_source.resize(source_size + 1);
    phrases_target.resize(target_size + 1);
    
    alignment_source_target.clear();
    alignment_target_source.clear();
    alignment_source_target.resize(source_size);
    alignment_target_source.resize(target_size);
    
    alignment_count_source.clear();
    alignment_count_target.clear();
    alignment_count_source.resize(source_size + 1, 0);
    alignment_count_target.resize(target_size + 1, 0);
    
    alignment_type::const_iterator aiter_end = alignment.end();
    for (alignment_type::const_iterator aiter = alignment.begin(); aiter != aiter_end; ++ aiter) {
      alignment_source_target[aiter->source].push_back(aiter->target);
      alignment_target_source[aiter->target].push_back(aiter->source);
    }
    
    for (int src = 0; src < source_size; ++ src) {
      std::sort(alignment_source_target[src].begin(), alignment_source_target[src].end());
      alignment_count_source[src + 1] = alignment_count_source[src] + alignment_source_target[src].size();
    }
    
    for (int trg = 0; trg < target_size; ++ trg) {
      std::sort(alignment_target_source[trg].begin(), alignment_target_source[trg].end());
      alignment_count_target[trg + 1] = alignment_count_target[trg] + alignment_target_source[trg].size();
    }
    
    span_target_chart.clear();
    span_target_chart.resize(target_size + 1, span_type(source_size, 0));
    
    
    for (int target_first = 0; target_first < target_size; ++ target_first) {
      span_type span_source(source_size, 0);
	
      for (int target_last = target_first + 1; target_last <= target_size; ++ target_last) {
	
	if (! alignment_target_source[target_last - 1].empty()) {
	  span_source.first  = utils::bithack::min(int(span_source.first),  int(alignment_target_source[target_last - 1].front()));
	  span_source.second = utils::bithack::max(int(span_source.second), int(alignment_target_source[target_last - 1].back()) + 1);
	}
	  
	span_target_chart(target_first, target_last) = span_source;
      }
    }
    
    phrase_pair_type phrase_pair;
    
    for (int source_first = 0; source_first < source_size; ++ source_first) {
      span_type span_target(target_size, 0);
	
      for (int source_last = source_first + 1; source_last <= source_size; ++ source_last) {
	  
	if (! alignment_source_target[source_last - 1].empty()) {
	  span_target.first  = utils::bithack::min(int(span_target.first),  int(alignment_source_target[source_last - 1].front()));
	  span_target.second = utils::bithack::max(int(span_target.second), int(alignment_source_target[source_last - 1].back()) + 1);
	}
	  
	const int span_count_source = alignment_count_source[source_last] - alignment_count_source[source_first];
	
	if (span_count_source > 0 && span_target.second - span_target.first > 0) {
	  
	  const int span_count_target = alignment_count_target[span_target.second] - alignment_count_target[span_target.first];
	  
	  if (span_count_source == span_count_target) {
	    
	    // unique span-pair
	    const span_type& span_source = span_target_chart(span_target.first, span_target.second);
	    
	    
	    // enlarge the target-span...
	    for (int target_first = span_target.first; target_first >= 0; -- target_first)
	      for (int target_last = span_target.second; target_last <= target_size; ++ target_last) {
		  
		const int span_count_target = alignment_count_target[target_last] - alignment_count_target[target_first];
		
		if (span_count_source != span_count_target)
		  break;

		if (phrases_source(source_first, source_last).empty()) {
		  phrase_type& phrase = phrases_source(source_first, source_last);
		  for (int i = source_first; i != source_last - 1; ++ i)
		    phrase += static_cast<const std::string&>(source[i]) + ' ';
		  phrase += static_cast<const std::string&>(source[source_last - 1]);
		}
		
		if (phrases_target(target_first, target_last).empty()) {
		  phrase_type& phrase = phrases_target(target_first, target_last);
		  for (int i = target_first; i != target_last - 1; ++ i)
		    phrase += static_cast<const std::string&>(target[i]) + ' ';
		  phrase += static_cast<const std::string&>(target[target_last - 1]);
		}
		
		
		// work with this span!
		phrase_pair.source = phrases_source(source_first, source_last);
		phrase_pair.target = phrases_target(target_first, target_last);
		
		phrase_pair.alignment.clear();
		for (int src = source_first; src != source_last; ++ src) {
		  point_set_type::const_iterator titer_end = alignment_source_target[src].end();
		  for (point_set_type::const_iterator titer = alignment_source_target[src].begin(); titer != titer_end; ++ titer)
		    phrase_pair.alignment.push_back(std::make_pair(src - source_first, *titer - target_first));
		}
		
		const bool connected_left_top     = is_aligned(source_first - 1, target_first - 1);
		const bool connected_right_top    = is_aligned(source_last,      target_first - 1);
		const bool connected_left_bottom  = is_aligned(source_first - 1, target_last);
		const bool connected_right_bottom = is_aligned(source_last,      target_last);

		counts_type& counts = const_cast<counts_type&>(phrase_pairs.insert(phrase_pair).first->counts);
		
		counts[0] += 1;
		counts[1] += (  connected_left_top && ! connected_right_top);
		counts[2] += (! connected_left_top &&   connected_right_top);
		counts[3] += (  connected_left_bottom && ! connected_right_bottom);
		counts[4] += (! connected_left_bottom &&   connected_right_bottom);
		
		//spans.push_back(span_pair_type(span_type(source_first, source_last), span_type(target_first, target_last)));
	      }
	  }
	}
      }
    }
  }
  
  bool is_aligned(const int source, const int target) const
  {
    const int source_size = alignment_source_target.size();
    const int target_size = alignment_target_source.size();
    
    if (source == -1 && target == -1) return true; // aligned at BOS
    if (source <= -1 || target <= -1) return false;
    if (source == source_size && target == target_size) return true; // aligned at EOS
    if (source >= source_size || target >= target_size) return false;
    
    point_set_type::const_iterator aiter_begin = alignment_source_target[source].begin();
    point_set_type::const_iterator aiter_end   = alignment_source_target[source].end();
    
    // check if there exists alignment point!
    return std::find(aiter_begin, aiter_end, target) != aiter_end;
  }
  
};

#endif

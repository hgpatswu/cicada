//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

//
// refset format:
// 0 |||  reference translatin for source sentence 0
// 0 |||  another reference
// 1 |||  reference translation for source sentence 1
//

#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <stdexcept>
#include <numeric>
#include <set>

#include "cicada/sentence.hpp"
#include "cicada/lattice.hpp"
#include "cicada/hypergraph.hpp"
#include "cicada/inside_outside.hpp"

#include "cicada/feature_function.hpp"
#include "cicada/weight_vector.hpp"
#include "cicada/semiring.hpp"
#include "cicada/viterbi.hpp"

#include "cicada/eval.hpp"

#include "cicada/optimize/line_search.hpp"
#include "cicada/optimize/powell.hpp"

#include "cicada/operation/functional.hpp"
#include "cicada/operation/traversal.hpp"

#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/resource.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/random_seed.hpp"
#include "utils/unordered_set.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random.hpp>
#include <boost/thread.hpp>

#include "cicada_text_impl.hpp"
#include "cicada_kbest_impl.hpp"
#include "cicada_mert_kbest_impl.hpp"

typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

typedef cicada::Symbol   symbol_type;
typedef cicada::Vocab    vocab_type;
typedef cicada::Sentence sentence_type;

typedef cicada::HyperGraph hypergraph_type;
typedef cicada::Rule       rule_type;

typedef hypergraph_type::feature_set_type    feature_set_type;
typedef cicada::WeightVector<double>   weight_set_type;
typedef feature_set_type::feature_type feature_type;

typedef std::vector<weight_set_type, std::allocator<weight_set_type> > weight_set_collection_type;

typedef cicada::eval::Scorer         scorer_type;
typedef cicada::eval::ScorerDocument scorer_document_type;

path_set_type tstset_files;
path_set_type refset_files;
path_type     output_file = "-";

path_type bound_lower_file;
path_type bound_upper_file;

double value_lower = -100;
double value_upper =  100;

path_set_type feature_weights_files;

std::string scorer_name = "bleu:order=4";
bool scorer_list = false;

int iteration = 10;
int samples_restarts   = 4;
int samples_directions = 10;

bool initial_average = false;
bool iterative = false;

double tolerance = 1e-4;

bool regularize_l1 = false;
bool regularize_l2 = false;
double C = 1.0; // inverse of C == 1.0 / C : where C is a constant of SVM^{light}

bool weight_normalize_l1 = false;
bool weight_normalize_l2 = false;

int threads = 4;

int debug = 0;


template <typename Iterator, typename Generator>
inline
void randomize(Iterator first, Iterator last, Iterator lower, Iterator upper, Generator& generator)
{
  boost::uniform_01<double> uniform;

  for (/**/; first != last; ++ first, ++ lower, ++ upper) {
    if (*lower == *upper)
      *first = 0.0;
    else
      *first = *lower + uniform(generator) * std::min(double(*upper - *lower), 1.0);
  }
}

template <typename Iterator>
inline
void normalize_l2(Iterator first, Iterator last, const double radius)
{
  const double sum = std::inner_product(first, last, first, 0.0);
  
  if (sum != 0.0)
    std::transform(first, last, first, std::bind2nd(std::multiplies<double>(), radius / std::sqrt(sum)));
}

template <typename Iterator>
void normalize_l1(Iterator first, Iterator last, const double radius)
{
  double sum = 0.0;
  for (Iterator iter = first; iter != last; ++ iter)
    sum += std::fabs(*iter);
  
  if (sum != 0.0)
    std::transform(first, last, first, std::bind2nd(std::multiplies<double>(), radius / std::sqrt(sum)));
}

template <typename Iterator, typename BoundIterator>
inline
bool valid_bounds(Iterator first, Iterator last, BoundIterator lower, BoundIterator upper)
{
  for (/**/; first != last; ++ first, ++ lower, ++ upper)
    if (*lower != *upper && (*first < *lower || *upper < *first))
      return false;
  return true;
}

void read_tstset(const path_set_type& files, hypothesis_map_type& kbests, const size_t scorers_size);
void read_refset(const path_set_type& file, scorer_document_type& scorers);

void initialize_score(hypothesis_map_type& hypotheses,
		      const scorer_document_type& scorers);

void options(int argc, char** argv);

struct EnvelopeComputer
{
  typedef cicada::optimize::LineSearch line_search_type;
  
  typedef line_search_type::segment_type          segment_type;
  typedef line_search_type::segment_set_type      segment_set_type;
  typedef line_search_type::segment_document_type segment_document_type;

  EnvelopeComputer(const scorer_document_type& __scorers,
		   const hypothesis_map_type&  __kbests)
    : scorers(__scorers),
      kbests(__kbests) {}

  void operator()(segment_document_type& segments, const weight_set_type& origin, const weight_set_type& direction) const;

  const scorer_document_type& scorers;
  const hypothesis_map_type&  kbests;
};

struct ViterbiComputer
{
  ViterbiComputer(const scorer_document_type& __scorers,
		  const hypothesis_map_type&  __kbests)
    : scorers(__scorers),
      kbests(__kbests) {}
  
  double operator()(const weight_set_type& weights) const;

  const scorer_document_type& scorers;
  const hypothesis_map_type&  kbests;
};

template <typename Regularizer, typename Generator>
bool powell(const scorer_document_type& scorers,
	    const hypothesis_map_type& kbests,
	    const weight_set_type& bound_lower,
	    const weight_set_type& bound_upper,
	    Regularizer regularizer,
	    Generator& generator,
	    const double tolerance,
	    const int samples,
	    double& score,
	    weight_set_type& weights)
{
  cicada::optimize::Powell<EnvelopeComputer, ViterbiComputer, Regularizer, Generator> optimizer(EnvelopeComputer(scorers, kbests),
												ViterbiComputer(scorers, kbests),
												regularizer,
												generator,
												bound_lower,
												bound_upper,
												tolerance,
												samples,
												debug);
  
  return optimizer(score, weights);
}

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);

    cicada::optimize::LineSearch::value_min = value_lower;
    cicada::optimize::LineSearch::value_max = value_upper;

    if (scorer_list) {
      std::cout << cicada::eval::Scorer::lists();
      return 0;
    }    
    
    if (regularize_l1 && regularize_l2)
      throw std::runtime_error("you cannot use both of L1 and L2...");
    
    if (regularize_l1 || regularize_l2) {
      if (C <= 0.0)
	throw std::runtime_error("the scaling for L1/L2 must be positive");
    }
    
    if (weight_normalize_l1 && weight_normalize_l2)
      throw std::runtime_error("you cannot use both of L1 and L2 for weight normalization...");


    threads = utils::bithack::max(threads, 1);
    
    // read reference set
    scorer_document_type scorers(scorer_name);
    read_refset(refset_files, scorers);

    const size_t scorers_size = scorers.size();
    
    if (iterative && tstset_files.size() > 1) {
      scorer_document_type scorers_iterative(scorer_name);
      scorers_iterative.resize(scorers.size() * tstset_files.size());
      
      for (size_t i = 0; i != tstset_files.size(); ++ i)
	std::copy(scorers.begin(), scorers.end(), scorers_iterative.begin() + scorers.size() * i);
      
      scorers.swap(scorers_iterative);
    }

    if (debug)
      std::cerr << "# of references: " << scorers.size() << std::endl;


    if (debug)
      std::cerr << "reading kbests" << std::endl;

    hypothesis_map_type kbests(scorers.size());
    
    read_tstset(tstset_files, kbests, scorers_size);

    initialize_score(kbests, scorers);

    // collect initial weights
    weight_set_collection_type weights;
    
    if (! feature_weights_files.empty()) {

      for (path_set_type::const_iterator fiter = feature_weights_files.begin(); fiter != feature_weights_files.end(); ++ fiter) {
	if (*fiter != "-" && ! boost::filesystem::exists(*fiter))
	  throw std::runtime_error("no file? " + fiter->string());
	
	utils::compress_istream is(*fiter);
	
	weights.push_back(weight_set_type());
	is >> weights.back();
      }
      
      if (initial_average && weights.size() > 1) {
	weight_set_type weight;
	
	weight_set_collection_type::const_iterator witer_end = weights.end();
	for (weight_set_collection_type::const_iterator witer = weights.begin(); witer != witer_end; ++ witer)
	  weight += *witer;
	
	weight *= (1.0 / weights.size());
	
	weights.push_back(weight);
      }
      
      std::set<weight_set_type, std::less<weight_set_type>, std::allocator<weight_set_type> > uniques;
      uniques.insert(weights.begin(), weights.end());
      weights.clear();
      weights.insert(weights.end(), uniques.begin(), uniques.end());
    } else {
      weights.push_back(weight_set_type());
      
      // all one weight...
      for (feature_type::id_type id = 0; id < feature_type::allocated(); ++ id)
	if (! feature_type(id).empty())
	  weights.back()[feature_type(id)] = 1.0;
    }
    
    // collect lower/upper bounds
    weight_set_type bound_lower;
    weight_set_type bound_upper;
    
    if (! bound_lower_file.empty()) {
      if (bound_lower_file == "-" || boost::filesystem::exists(bound_lower_file)) {
	typedef cicada::FeatureVector<double> feature_vector_type;
	
	feature_vector_type bounds;
	  
	utils::compress_istream is(bound_lower_file);
	is >> bounds;
	
	bound_lower.allocate(cicada::optimize::LineSearch::value_min);
	for (feature_vector_type::const_iterator biter = bounds.begin(); biter != bounds.end(); ++ biter)
	  bound_lower[biter->first] = biter->second;
      } else
	throw std::runtime_error("no lower-bound file?" + bound_lower_file.string());
    }
    
    if (! bound_upper_file.empty()) {
      if (bound_upper_file == "-" || boost::filesystem::exists(bound_upper_file)) {
	typedef cicada::FeatureVector<double> feature_vector_type;
	
	feature_vector_type bounds;
	  
	utils::compress_istream is(bound_upper_file);
	is >> bounds;
	
	bound_upper.allocate(cicada::optimize::LineSearch::value_max);
	for (feature_vector_type::const_iterator biter = bounds.begin(); biter != bounds.end(); ++ biter)
	  bound_upper[biter->first] = biter->second;
      } else
	throw std::runtime_error("no upper-bound file?" + bound_upper_file.string());
    }
    
    cicada::optimize::LineSearch::initialize_bound(bound_lower, bound_upper);
    
    double          optimum_objective = std::numeric_limits<double>::infinity();
    weight_set_type optimum_weights;
    
    boost::mt19937 generator;
    generator.seed(utils::random_seed());
    
    if (debug)
      std::cerr << "start optimization" << std::endl;
    
    int sample = 0;
    for (weight_set_collection_type::const_iterator witer = weights.begin(); witer != weights.end(); ++ witer, ++ sample) {
      typedef cicada::optimize::LineSearch line_search_type;

      double          sample_objective = std::numeric_limits<double>::infinity();
      weight_set_type sample_weights = *witer;
      
      utils::resource opt_start;
      
      bool moved = false;
      if (regularize_l1)
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeL1(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      else if (regularize_l2)
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeL2(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      else
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeNone(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      
      utils::resource opt_end;
      
      if (debug)
	std::cerr << "cpu time: " << (opt_end.cpu_time() - opt_start.cpu_time()) << '\n'
		  << "user time: " << (opt_end.user_time() - opt_start.user_time()) << '\n';
      
      if (debug)
	std::cerr << "sample: " << (sample + 1) << " objective: " << sample_objective << std::endl
		  << sample_weights;
      
      if ((moved && sample_objective < optimum_objective) || optimum_objective == std::numeric_limits<double>::infinity()) {
	optimum_objective = sample_objective;
	optimum_weights = sample_weights;
      }
    }
    
    for (/**/; sample < static_cast<int>(samples_restarts + weights.size()); ++ sample) {
      typedef cicada::optimize::LineSearch line_search_type;
      
      double          sample_objective = std::numeric_limits<double>::infinity();
      weight_set_type sample_weights = weights.back();
      
      if (sample > 0) {
	// perform randomize...
	sample_weights = optimum_weights;
	
	while (1) {
	  randomize(sample_weights.begin(), sample_weights.end(), bound_lower.begin(), bound_upper.begin(), generator);
	  
	  if (weight_normalize_l1 || regularize_l1)
	    normalize_l1(sample_weights.begin(), sample_weights.end(), 1.0);
	  else
	    normalize_l2(sample_weights.begin(), sample_weights.end(), 1.0);
	  
	  if (valid_bounds(sample_weights.begin(), sample_weights.end(), bound_lower.begin(), bound_upper.begin()))
	    break;
	}
	
	// re-assign original weights...
	for (feature_type::id_type id = 0; id < feature_type::allocated(); ++ id)
	  if (! feature_type(id).empty())
	    if (bound_lower[feature_type(id)] == bound_upper[feature_type(id)])
	      sample_weights[feature_type(id)] = optimum_weights[feature_type(id)];
      }
      
      utils::resource opt_start;
      
      bool moved = false;
      if (regularize_l1)
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeL1(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      else if (regularize_l2)
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeL2(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      else
	moved = powell(scorers,
		       kbests,
		       bound_lower,
		       bound_upper,
		       line_search_type::RegularizeNone(C),
		       generator,
		       tolerance,
		       samples_directions,
		       sample_objective,
		       sample_weights);
      
      utils::resource opt_end;
      
      if (debug)
	std::cerr << "cpu time: " << (opt_end.cpu_time() - opt_start.cpu_time()) << '\n'
		  << "user time: " << (opt_end.user_time() - opt_start.user_time()) << '\n';
      
      if (debug)
	std::cerr << "sample: " << (sample + 1) << " objective: " << sample_objective << std::endl
		  << sample_weights;
      
      if ((moved && sample_objective < optimum_objective) || optimum_objective == std::numeric_limits<double>::infinity()) {
	optimum_objective = sample_objective;
	optimum_weights = sample_weights;
      }
    }

    if (debug)
      std::cerr << "objective: " << optimum_objective << std::endl;
    
    if (weight_normalize_l1)
      normalize_l1(optimum_weights.begin(), optimum_weights.end(), std::sqrt(feature_type::allocated()));
    else if (weight_normalize_l2)
      normalize_l2(optimum_weights.begin(), optimum_weights.end(), std::sqrt(feature_type::allocated()));
    
    utils::compress_ostream os(output_file);
    os.precision(20);
    os << optimum_weights;
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}


struct EnvelopeTask
{
  typedef cicada::optimize::LineSearch line_search_type;
  
  typedef line_search_type::segment_type          segment_type;
  typedef line_search_type::segment_set_type      segment_set_type;
  typedef line_search_type::segment_document_type segment_document_type;

  typedef cicada::semiring::Envelope envelope_type;
  typedef std::vector<envelope_type, std::allocator<envelope_type> >  envelope_set_type;

  typedef utils::lockfree_list_queue<int, std::allocator<int> >  queue_type;

    
  EnvelopeTask(queue_type& __queue,
	       segment_document_type&      __segments,
	       const weight_set_type&      __origin,
	       const weight_set_type&      __direction,
	       const scorer_document_type& __scorers,
	       const hypothesis_map_type&  __kbests)
    : queue(__queue),
      segments(__segments),
      origin(__origin),
      direction(__direction),
      scorers(__scorers),
      kbests(__kbests) {}
  
  void operator()()
  {
    EnvelopeKBest::line_set_type lines;
    int seg;

    EnvelopeKBest envelopes(origin, direction);
    
    while (1) {
      queue.pop(seg);
      if (seg < 0) break;
      
      envelopes(kbests[seg], lines);
      
      EnvelopeKBest::line_set_type::const_iterator liter_end = lines.end();
      for (EnvelopeKBest::line_set_type::const_iterator liter = lines.begin(); liter != liter_end; ++ liter) {
	const EnvelopeKBest::line_type& line = *liter;
	
	if (debug >= 4)
	  std::cerr << "segment: " << seg << " x: " << line.x << std::endl;
	
	segments[seg].push_back(std::make_pair(line.x, line.hypothesis->score));
      }
    }
  }
  
  queue_type& queue;

  segment_document_type& segments;

  const weight_set_type& origin;
  const weight_set_type& direction;
  
  const scorer_document_type& scorers;
  const hypothesis_map_type&  kbests;
};

void EnvelopeComputer::operator()(segment_document_type& segments, const weight_set_type& origin, const weight_set_type& direction) const
{
  typedef EnvelopeTask task_type;
  typedef task_type::queue_type queue_type;

  segments.clear();
  segments.resize(kbests.size());
  
  queue_type queue;
  
  boost::thread_group workers;
  for (int i = 0; i < threads; ++ i)
    workers.add_thread(new boost::thread(task_type(queue, segments, origin, direction, scorers, kbests)));
  
  for (size_t seg = 0; seg != kbests.size(); ++ seg)
    if (! kbests[seg].empty())
      queue.push(seg);
  
  for (int i = 0; i < threads; ++ i)
    queue.push(-1);
  
  workers.join_all();
}


struct ViterbiTask
{
  typedef cicada::semiring::Logprob<double> weight_type;

  typedef utils::lockfree_list_queue<int, std::allocator<int> >  queue_type;
  typedef std::vector<scorer_type::score_ptr_type, std::allocator<scorer_type::score_ptr_type> > score_set_type;
  
  ViterbiTask(queue_type&                 __queue,
	      const weight_set_type&      __weights,
	      const scorer_document_type& __scorers,
	      const hypothesis_map_type&  __kbests,
	      score_set_type& __scores)
    : queue(__queue),
      weights(__weights),
      scorers(__scorers),
      kbests(__kbests),
      scores(__scores) {}
  
  void operator()()
  {
    for (;;) {
      int id = 0;
      queue.pop(id);
      if (id < 0) break;
      
      double score_viterbi = - std::numeric_limits<double>::infinity();
      
      hypothesis_set_type::const_iterator kiter_end = kbests[id].end();
      for (hypothesis_set_type::const_iterator kiter = kbests[id].begin(); kiter != kiter_end; ++ kiter) {
	const hypothesis_type& hyp(*kiter);
	const double score = cicada::dot_product(weights, hyp.features.begin(), hyp.features.end(), 0.0);
	
	if (score > score_viterbi) {
	  score_viterbi = score;
	  scores[id] = hyp.score;
	}
      }
    }
  }
  
  queue_type&                 queue;
  const weight_set_type&      weights;
  const scorer_document_type& scorers;
  const hypothesis_map_type&  kbests;
  score_set_type&             scores;
};

double ViterbiComputer::operator()(const weight_set_type& weights) const
{
  typedef ViterbiTask task_type;
  
  typedef task_type::queue_type queue_type;
  typedef task_type::score_set_type score_set_type;
  
  queue_type     queue;
  score_set_type scores(kbests.size());
  boost::thread_group workers;
  for (int i = 0; i < threads; ++ i)
    workers.add_thread(new boost::thread(task_type(queue, weights, scorers, kbests, scores)));
  
  for (size_t id = 0; id != kbests.size(); ++ id)
    if (! kbests[id].empty())
      queue.push(id);
  
  for (int i = 0; i < threads; ++ i)
    queue.push(-1);
  
  workers.join_all();
  
  scorer_type::score_ptr_type score;
  for (size_t id = 0; id != kbests.size(); ++ id)
    if (! score)
      score = scores[id]->clone();
    else
      *score += *scores[id];
  
  return score->loss();
}

struct TaskInit
{
  typedef utils::lockfree_list_queue<int, std::allocator<int> > queue_type;
  
  template <typename Tp>
  struct hashp : boost::hash<Tp>
  {
    typedef boost::hash<Tp> hasher_type;
    
    size_t operator()(const Tp* x) const
    {
      return x ? hasher_type::operator()(*x) : size_t(0);
    }
  };
  
  template <typename Tp>
  struct equalp : std::equal_to<Tp>
  {
    typedef std::equal_to<Tp> equal_type;
    
    bool operator()(const Tp* x, const Tp* y) const
    {
      return (x == y) || (x && y && equal_type::operator()(*x, *y));
    }
  };
  
  typedef const hypothesis_type* value_type;
  typedef utils::unordered_set<value_type, hashp<hypothesis_type>, equalp<hypothesis_type>,
			       std::allocator<value_type> >::type hypothesis_unique_type;

  TaskInit(queue_type&                 __queue,
	   hypothesis_map_type&        __hypotheses,
	   const scorer_document_type& __scorers)
    : queue(__queue), hypotheses(__hypotheses), scorers(__scorers) {}

  void operator()()
  {
    hypothesis_unique_type uniques;

    for (;;) {
      int id = 0;
      queue.pop(id);
      if (id < 0) break;
      
      uniques.clear();
      hypothesis_set_type::const_iterator hiter_end = hypotheses[id].end();
      for (hypothesis_set_type::const_iterator hiter = hypotheses[id].begin(); hiter != hiter_end; ++ hiter)
	uniques.insert(&(*hiter));
      
      hypothesis_set_type merged;
      merged.reserve(uniques.size());
      
      hypothesis_unique_type::const_iterator uiter_end = uniques.end();
      for (hypothesis_unique_type::const_iterator uiter = uniques.begin(); uiter != uiter_end; ++ uiter) {
	merged.push_back(*(*uiter));
	
	merged.back().score = scorers[id]->score(sentence_type(merged.back().sentence.begin(), merged.back().sentence.end()));
      }
      
      uniques.clear();
      
      hypotheses[id].swap(merged);
    }
  }

  queue_type&                 queue;
  hypothesis_map_type&        hypotheses;
  const scorer_document_type& scorers;
};

void initialize_score(hypothesis_map_type& hypotheses,
		      const scorer_document_type& scorers)
{
  typedef TaskInit task_type;
  typedef task_type::queue_type queue_type;

  queue_type queue;
  
  boost::thread_group workers;
  for (int i = 0; i < threads; ++ i)
    workers.add_thread(new boost::thread(task_type(queue, hypotheses, scorers)));
  
  for (size_t id = 0; id != hypotheses.size(); ++ id)
    queue.push(id);
  
  for (int i = 0; i < threads; ++ i)
    queue.push(-1);
  
  workers.join_all();
}

void read_tstset(const path_set_type& files,
		 hypothesis_map_type& hypotheses,
		 const size_t scorers_size)
{
  typedef boost::spirit::istream_iterator iter_type;
  typedef kbest_feature_parser<iter_type> parser_type;
  
  if (files.empty())
    throw std::runtime_error("no files?");

  parser_type parser;
  kbest_feature_type kbest_feature;
  
  size_t iter = 0;
  for (path_set_type::const_iterator fiter = files.begin(); fiter != files.end(); ++ fiter, ++ iter) {
    if (! boost::filesystem::exists(*fiter) && *fiter != "-")
      throw std::runtime_error("no file: " + fiter->string());

    const size_t id_offset = size_t(iterative) * scorers_size * iter;

    if (boost::filesystem::is_directory(*fiter)) {
      for (size_t i = 0; /**/; ++ i) {
	const path_type path = (*fiter) / (utils::lexical_cast<std::string>(i) + ".gz");

	if (! boost::filesystem::exists(path)) break;
	
	utils::compress_istream is(path, 1024 * 1024);
	is.unsetf(std::ios::skipws);
	
	iter_type iter(is);
	iter_type iter_end;
	
	while (iter != iter_end) {
	  boost::fusion::get<1>(kbest_feature).clear();
	  boost::fusion::get<2>(kbest_feature).clear();
	  
	  if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest_feature))
	    if (iter != iter_end)
	      throw std::runtime_error("kbest parsing failed");
	  
	  const size_t id = boost::fusion::get<0>(kbest_feature) + id_offset;
	  
	  if (id >= hypotheses.size())
	    throw std::runtime_error("invalid id: " + utils::lexical_cast<std::string>(id));
	  if (id != i + id_offset)
	    throw std::runtime_error("invalid id: " + utils::lexical_cast<std::string>(id));
	  
	  hypotheses[id].push_back(hypothesis_type(kbest_feature));
	}
      }
    } else {
      utils::compress_istream is(*fiter, 1024 * 1024);
      is.unsetf(std::ios::skipws);
      
      iter_type iter(is);
      iter_type iter_end;
      
      while (iter != iter_end) {
	boost::fusion::get<1>(kbest_feature).clear();
	boost::fusion::get<2>(kbest_feature).clear();
	
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, kbest_feature))
	  if (iter != iter_end)
	    throw std::runtime_error("kbest parsing failed");
	
	const size_t id = boost::fusion::get<0>(kbest_feature) + id_offset;
	
	if (id >= hypotheses.size())
	  throw std::runtime_error("invalid id: " + utils::lexical_cast<std::string>(id));
	
	hypotheses[id].push_back(hypothesis_type(kbest_feature));
      }
    }
  }
}

void read_refset(const path_set_type& files, scorer_document_type& scorers)
{
  typedef boost::spirit::istream_iterator iter_type;
  typedef cicada_sentence_parser<iter_type> parser_type;

  if (files.empty())
    throw std::runtime_error("no reference files?");
    
  scorers.clear();

  parser_type parser;
  id_sentence_type id_sentence;
  
  for (path_set_type::const_iterator fiter = files.begin(); fiter != files.end(); ++ fiter) {
    
    if (! boost::filesystem::exists(*fiter) && *fiter != "-")
      throw std::runtime_error("no reference file: " + fiter->string());

    utils::compress_istream is(*fiter, 1024 * 1024);
    is.unsetf(std::ios::skipws);
    
    iter_type iter(is);
    iter_type iter_end;
    
    while (iter != iter_end) {
      id_sentence.second.clear();
      if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, boost::spirit::standard::blank, id_sentence))
	if (iter != iter_end)
	  throw std::runtime_error("refset parsing failed");
      
      const int& id = id_sentence.first;
      
      if (id >= static_cast<int>(scorers.size()))
	scorers.resize(id + 1);
      if (! scorers[id])
	scorers[id] = scorers.create();
      
      scorers[id]->insert(id_sentence.second);
    }
  }
}

void options(int argc, char** argv)
{
  namespace po = boost::program_options;

  po::options_description opts_config("configuration options");
  
  opts_config.add_options()
    ("tstset",  po::value<path_set_type>(&tstset_files)->multitoken(), "test set file(s) (in kbest format)")
    ("refset",  po::value<path_set_type>(&refset_files)->multitoken(), "reference set file(s)")
    
    ("output", po::value<path_type>(&output_file)->default_value(output_file), "output file")

    ("bound-lower", po::value<path_type>(&bound_lower_file),                     "lower bounds definition for feature weights")
    ("bound-upper", po::value<path_type>(&bound_upper_file),                     "upper bounds definition for feature weights")

    ("value-lower", po::value<double>(&value_lower)->default_value(value_lower), "default lower bounds")
    ("value-upper", po::value<double>(&value_upper)->default_value(value_upper),  "default upper_bounds")
    
    // feature weight files
    ("feature-weights",  po::value<path_set_type>(&feature_weights_files)->multitoken(), "feature weights file(s)")

    ("scorer",      po::value<std::string>(&scorer_name)->default_value(scorer_name), "error metric")
    ("scorer-list", po::bool_switch(&scorer_list),                                    "list of error metric")
    
    ("iteration",          po::value<int>(&iteration),          "# of mert iteration")
    ("samples-restarts",   po::value<int>(&samples_restarts),   "# of random sampling for initial starting point")
    ("samples-directions", po::value<int>(&samples_directions), "# of ramdom sampling for directions")
    ("initial-average",    po::bool_switch(&initial_average),   "averaged initial parameters")
    ("iterative",          po::bool_switch(&iterative),         "iterative training of MERT")
    
    ("tolerance", po::value<double>(&tolerance)->default_value(tolerance), "tolerance")
    
    ("regularize-l1",    po::bool_switch(&regularize_l1),         "regularization via L1")
    ("regularize-l2",    po::bool_switch(&regularize_l2),         "regularization via L2")
    ("C",                po::value<double>(&C)->default_value(C), "scaling for regularizer")
    
    ("normalize-l1",    po::bool_switch(&weight_normalize_l1), "weight normalization via L1 (not a regularizer...)")
    ("normalize-l2",    po::bool_switch(&weight_normalize_l2), "weight normalization via L2 (not a regularizer...)")

    ("threads", po::value<int>(&threads), "# of threads")
    ;
  
  po::options_description opts_command("command line options");
  opts_command.add_options()
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::options_description desc_config;
  po::options_description desc_command;
  
  desc_config.add(opts_config);
  desc_command.add(opts_config).add(opts_command);
  
  po::variables_map variables;

  po::store(po::parse_command_line(argc, argv, desc_command, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);

  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc_command << std::endl;
    exit(0);
  }
}

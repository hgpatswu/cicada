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
#include <iterator>
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
#include "utils/mpi.hpp"
#include "utils/mpi_device.hpp"
#include "utils/mpi_device_bcast.hpp"
#include "utils/mpi_stream.hpp"
#include "utils/mpi_stream_simple.hpp"
#include "utils/mpi_traits.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"
#include "utils/space_separator.hpp"
#include "utils/base64.hpp"
#include "utils/piece.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/random_seed.hpp"
#include "utils/unordered_set.hpp"

#include <boost/tokenizer.hpp>
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

void bcast_weights(const int rank, weight_set_type& weights);

enum {
  envelope_tag = 1000,
  viterbi_tag,
  
  envelope_notify_tag,
  viterbi_notify_tag,

  envelope_termination_tag,
  viterbi_termination_tag,
  
};

inline
int loop_sleep(bool found, int non_found_iter)
{
  if (! found) {
    boost::thread::yield();
    ++ non_found_iter;
  } else
    non_found_iter = 0;
    
  if (non_found_iter >= 64) {
    struct timespec tm;
    tm.tv_sec = 0;
    tm.tv_nsec = 2000001; // above 2ms
    nanosleep(&tm, NULL);
    
    non_found_iter = 0;
  }
  return non_found_iter;
}

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
  utils::mpi_world mpi_world(argc, argv);
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  try {
    
    options(argc, argv);

    cicada::optimize::LineSearch::value_min = value_lower;
    cicada::optimize::LineSearch::value_max = value_upper;

    if (mpi_size < 2)
      throw std::runtime_error("you should run at least two ranks!");

    if (scorer_list) {
      if (mpi_rank == 0)
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
    
    if (debug && mpi_rank == 0)
      std::cerr << "# of references: " << scorers.size() << std::endl;
    
    
    if (debug && mpi_rank == 0)
      std::cerr << "reading kbests" << std::endl;
    
    hypothesis_map_type kbests(scorers.size());
    
    read_tstset(tstset_files, kbests, scorers_size);
    
    initialize_score(kbests, scorers);
    
    // collect and share feature names!
    for (int rank = 0; rank < mpi_size; ++ rank) {
      weight_set_type weights;
      weights.allocate();
      
      for (feature_type::id_type id = 0; id != feature_type::allocated(); ++ id)
	if (! feature_type(feature_type::id_type(id)).empty())
	  weights[feature_type(id)] = 1.0;
      
      bcast_weights(rank, weights);
    }
    
    // collect initial weights
    weight_set_collection_type weights;

    if (mpi_rank == 0) {
    
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
    }
    
    // collect lower/upper bounds
    weight_set_type bound_lower;
    weight_set_type bound_upper;
    
    if (mpi_rank == 0) {

      typedef cicada::FeatureVector<double> feature_vector_type;
      
      if (! bound_lower_file.empty()) {
	if (bound_lower_file == "-" || boost::filesystem::exists(bound_lower_file)) {
	  
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
    }
    
    boost::mt19937 generator;
    generator.seed(utils::random_seed());
    
    if (mpi_rank == 0) {
      double          optimum_objective = std::numeric_limits<double>::infinity();
      weight_set_type optimum_weights;
      
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
	
	if (sample > 0 && mpi_rank == 0) {
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
      
      for (int rank = 1; rank < mpi_size; ++ rank)
	MPI::COMM_WORLD.Send(0, 0, utils::mpi_traits<int>::data_type(), rank, envelope_termination_tag);
      
      for (int rank = 1; rank < mpi_size; ++ rank)
	MPI::COMM_WORLD.Send(0, 0, utils::mpi_traits<int>::data_type(), rank, viterbi_termination_tag);

      
      if (debug)
	std::cerr << "objective: " << optimum_objective << std::endl;
      
      if (weight_normalize_l1)
	normalize_l1(optimum_weights.begin(), optimum_weights.end(), std::sqrt(feature_type::allocated()));
      else if (weight_normalize_l2)
	normalize_l2(optimum_weights.begin(), optimum_weights.end(), std::sqrt(feature_type::allocated()));
      
      utils::compress_ostream os(output_file);
      os.precision(20);
      os << optimum_weights;
    } else {
      
      enum {
	ENVELOPE_NOTIFY = 0,
	ENVELOPE_TERMINATION,
	VITERBI_NOTIFY,
	VITERBI_TERMINATION,
      };

      MPI::Prequest requests[4];
      
      requests[ENVELOPE_NOTIFY]      = MPI::COMM_WORLD.Recv_init(0, 0, utils::mpi_traits<int>::data_type(), 0, envelope_notify_tag);
      requests[ENVELOPE_TERMINATION] = MPI::COMM_WORLD.Recv_init(0, 0, utils::mpi_traits<int>::data_type(), 0, envelope_termination_tag);

      requests[VITERBI_NOTIFY]       = MPI::COMM_WORLD.Recv_init(0, 0, utils::mpi_traits<int>::data_type(), 0, viterbi_notify_tag);
      requests[VITERBI_TERMINATION]  = MPI::COMM_WORLD.Recv_init(0, 0, utils::mpi_traits<int>::data_type(), 0, viterbi_termination_tag);

      for (int i = 0; i < 4; ++ i)
	requests[i].Start();
      
      // we are idle... when notified, perform computation!
      
      EnvelopeComputer::segment_document_type segments;

      EnvelopeComputer envelope(scorers, kbests);
      ViterbiComputer  viterbi(scorers, kbests);

      weight_set_type origin;
      weight_set_type direction;
      weight_set_type weights;

      bool envelope_terminated = false;
      bool viterbi_terminated = false;

      while (! envelope_terminated || ! viterbi_terminated) {
	switch (MPI::Request::Waitany(4, requests)) {
	case ENVELOPE_NOTIFY:
	  requests[ENVELOPE_NOTIFY].Start();
	  envelope(segments, origin, direction);
	  break;
	case ENVELOPE_TERMINATION:
	  envelope_terminated = true;
	  break;
	case VITERBI_NOTIFY:
	  requests[VITERBI_NOTIFY].Start();
	  viterbi(weights);
	  break;
	case VITERBI_TERMINATION:
	  viterbi_terminated = true;
	  break;
	}
      }
      
      if (requests[ENVELOPE_NOTIFY].Test())
	requests[ENVELOPE_NOTIFY].Cancel();

      if (requests[VITERBI_NOTIFY].Test())
	requests[VITERBI_NOTIFY].Cancel();
    }
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    MPI::COMM_WORLD.Abort(1);
    return 1;
  }
  return 0;
}

void EnvelopeComputer::operator()(segment_document_type& segments, const weight_set_type& __origin, const weight_set_type& __direction) const
{
  typedef utils::mpi_device_source idevice_type;
  typedef utils::mpi_device_sink   odevice_type;
  
  typedef boost::iostreams::filtering_ostream ostream_type;
  typedef boost::iostreams::filtering_istream istream_type;

  typedef boost::shared_ptr<ostream_type> ostream_ptr_type;
  typedef boost::shared_ptr<istream_type> istream_ptr_type;

  typedef boost::shared_ptr<odevice_type> odevice_ptr_type;
  typedef boost::shared_ptr<idevice_type> idevice_ptr_type;
  
  typedef std::vector<ostream_ptr_type, std::allocator<ostream_ptr_type> > ostream_ptr_set_type;
  typedef std::vector<istream_ptr_type, std::allocator<istream_ptr_type> > istream_ptr_set_type;

  typedef std::vector<odevice_ptr_type, std::allocator<odevice_ptr_type> > odevice_ptr_set_type;
  typedef std::vector<idevice_ptr_type, std::allocator<idevice_ptr_type> > idevice_ptr_set_type;

  typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;
  
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();

  weight_set_type origin(__origin);
  weight_set_type direction(__direction);
  
  if (mpi_rank == 0) {
    
    // send notification tag...
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, utils::mpi_traits<int>::data_type(), rank, envelope_notify_tag);
    
    bcast_weights(0, origin);

    bcast_weights(0, direction);

    segments.clear();
    segments.resize(scorers.size());
    
    istream_ptr_set_type is(mpi_size);
    idevice_ptr_set_type dev(mpi_size);
    for (int rank = 1; rank < mpi_size; ++ rank) {
      dev[rank].reset(new idevice_type(rank, envelope_tag, 4096));
      is[rank].reset(new istream_type());
      
      is[rank]->push(boost::iostreams::zlib_decompressor());
      is[rank]->push(*dev[rank]);

      dev[rank]->test();
    }
    
    EnvelopeKBest::line_set_type lines;
    EnvelopeKBest envelopes(origin, direction);
    
    for (size_t id = 0; id != kbests.size(); ++ id) 
      if (! kbests[id].empty()) {
	envelopes(kbests[id], lines);
	
	EnvelopeKBest::line_set_type::const_iterator liter_end = lines.end();
	for (EnvelopeKBest::line_set_type::const_iterator liter = lines.begin(); liter != liter_end; ++ liter)
	  segments[id].push_back(std::make_pair(liter->x, liter->hypothesis->score));
      }
    
    std::string line;
    
    int non_found_iter = 0;
    while (1) {
      bool found = false;
      
      for (int rank = 1; rank < mpi_size; ++ rank) 
	while (is[rank] && dev[rank] && dev[rank]->test()) {
	  if (std::getline(*is[rank], line)) {
	    const utils::piece line_piece(line);
	    tokenizer_type tokenizer(line_piece);
	    
	    tokenizer_type::iterator iter = tokenizer.begin();
	    if (iter == tokenizer.end()) continue;
	    const utils::piece id_str = *iter; 
	    
	    ++ iter;
	    if (iter == tokenizer.end()) continue;
	    const utils::piece x_str = *iter;
	    
	    ++ iter;
	    if (iter == tokenizer.end()) continue;
	    const utils::piece score_str = *iter;
	    
	    const int id = utils::lexical_cast<int>(id_str);
	    
	    if (id >= static_cast<int>(segments.size()))
	      segments.resize(id + 1);
	    
	    segments[id].push_back(std::make_pair(utils::decode_base64<double>(x_str),
						  scorer_type::score_type::decode(score_str)));
	  } else {
	    is[rank].reset();
	    dev[rank].reset();
	  }
	  
	  found = true;
	}
      
      if (std::count(dev.begin(), dev.end(), idevice_ptr_type()) == mpi_size) break;
      
      non_found_iter = loop_sleep(found, non_found_iter);
    }
    
  } else {
    const int mpi_rank_size = mpi_size - 1;
    
    bcast_weights(0, origin);
    
    bcast_weights(0, direction);

    EnvelopeKBest::line_set_type lines;
    EnvelopeKBest envelopes(origin, direction);
    
    ostream_type os;
    os.push(boost::iostreams::zlib_compressor());
    os.push(odevice_type(0, envelope_tag, 4096));
    
    // revise this......
    for (size_t id = 0; id != kbests.size(); ++ id) 
      if (! kbests[id].empty()) {
	envelopes(kbests[id], lines);
	
	EnvelopeKBest::line_set_type::const_iterator liter_end = lines.end();
	for (EnvelopeKBest::line_set_type::const_iterator liter = lines.begin(); liter != liter_end; ++ liter) {
	  const EnvelopeKBest::line_type& line = *liter;
	  
	  os << id << ' ';
	  utils::encode_base64(line.x, std::ostream_iterator<char>(os));
	  os << ' ' << line.hypothesis->score->encode() << '\n';
	}
      }
  }
}

double ViterbiComputer::operator()(const weight_set_type& __weights) const
{
  typedef cicada::semiring::Logprob<double> weight_type;
 
  typedef utils::mpi_device_source idevice_type;
  typedef utils::mpi_device_sink   odevice_type;
  
  typedef boost::iostreams::filtering_ostream ostream_type;
  typedef boost::iostreams::filtering_istream istream_type;

  typedef boost::shared_ptr<ostream_type> ostream_ptr_type;
  typedef boost::shared_ptr<istream_type> istream_ptr_type;

  typedef boost::shared_ptr<odevice_type> odevice_ptr_type;
  typedef boost::shared_ptr<idevice_type> idevice_ptr_type;
  
  typedef std::vector<ostream_ptr_type, std::allocator<ostream_ptr_type> > ostream_ptr_set_type;
  typedef std::vector<istream_ptr_type, std::allocator<istream_ptr_type> > istream_ptr_set_type;

  typedef std::vector<odevice_ptr_type, std::allocator<odevice_ptr_type> > odevice_ptr_set_type;
  typedef std::vector<idevice_ptr_type, std::allocator<idevice_ptr_type> > idevice_ptr_set_type;

  typedef boost::tokenizer<utils::space_separator, utils::piece::const_iterator, utils::piece> tokenizer_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  weight_set_type weights(__weights);
  
  if (mpi_rank == 0) {
    
    for (int rank = 1; rank < mpi_size; ++ rank)
      MPI::COMM_WORLD.Send(0, 0, utils::mpi_traits<int>::data_type(), rank, viterbi_notify_tag);

    bcast_weights(0, weights);
    

    istream_ptr_set_type is(mpi_size);
    idevice_ptr_set_type dev(mpi_size);
    for (int rank = 1; rank < mpi_size; ++ rank) {
      dev[rank].reset(new idevice_type(rank, viterbi_tag, 4096));
      is[rank].reset(new istream_type());
      
      is[rank]->push(boost::iostreams::zlib_decompressor());
      is[rank]->push(*dev[rank]);
      
      dev[rank]->test();
    }

    scorer_type::score_ptr_type score;
    
    for (size_t id = 0; id != kbests.size(); ++ id)
      if (! kbests[id].empty()) {
	double score_viterbi = - std::numeric_limits<double>::infinity();
	scorer_type::score_ptr_type score_ptr;
	
	hypothesis_set_type::const_iterator kiter_end = kbests[id].end();
	for (hypothesis_set_type::const_iterator kiter = kbests[id].begin(); kiter != kiter_end; ++ kiter) {
	  const hypothesis_type& hyp(*kiter);
	  const double score_curr = cicada::dot_product(weights, hyp.features);
	  
	  if (score_curr > score_viterbi) {
	    score_viterbi = score_curr;
	    score_ptr = hyp.score;
	  }
	}
	
	if (! score)
	  score = score_ptr;
	else
	  *score += *score_ptr;
      }
    
    std::string line;
    
    int non_found_iter = 0;
    while (1) {
      bool found = false;
      
      for (int rank = 1; rank < mpi_size; ++ rank) 
	while (is[rank] && dev[rank] && dev[rank]->test()) {
	  if (std::getline(*is[rank], line)) {
	    const utils::piece line_piece(line);
	    tokenizer_type tokenizer(line_piece);
	    
	    tokenizer_type::iterator iter = tokenizer.begin();
	    if (iter == tokenizer.end()) continue;
	    const utils::piece id_str = *iter;
	    
	    ++ iter;
	    if (iter == tokenizer.end()) continue;
	    const utils::piece score_str = *iter;
	    
	    if (! score)
	      score = scorer_type::score_type::decode(score_str);
	    else
	      *score += *scorer_type::score_type::decode(score_str);
	  } else {
	    is[rank].reset();
	    dev[rank].reset();
	  }
	  
	  found = true;
	}
      
      if (std::count(dev.begin(), dev.end(), idevice_ptr_type()) == mpi_size) break;
      
      non_found_iter = loop_sleep(found, non_found_iter);
    }
    
    return score->loss();
  } else {
    bcast_weights(0, weights);
    
    ostream_type os;
    os.push(boost::iostreams::zlib_compressor());
    os.push(odevice_type(0, viterbi_tag, 4096));
    
    for (size_t id = 0; id != kbests.size(); ++ id)
      if (! kbests[id].empty()) {
	double score_viterbi = - std::numeric_limits<double>::infinity();
	scorer_type::score_ptr_type score_ptr;
	
	hypothesis_set_type::const_iterator kiter_end = kbests[id].end();
	for (hypothesis_set_type::const_iterator kiter = kbests[id].begin(); kiter != kiter_end; ++ kiter) {
	  const hypothesis_type& hyp(*kiter);
	  const double score = cicada::dot_product(weights, hyp.features);
	  
	  if (score > score_viterbi) {
	    score_viterbi = score;
	    score_ptr = hyp.score;
	  }
	}
	
	os << id << ' ' << score_ptr->encode() << '\n';
      }
  }
  
  return 0.0;
}

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

void initialize_score(hypothesis_map_type& hypotheses,
		      const scorer_document_type& scorers)
{
  typedef const hypothesis_type* value_type;
  typedef utils::unordered_set<value_type, hashp<hypothesis_type>, equalp<hypothesis_type>,
			       std::allocator<value_type> >::type hypothesis_unique_type;
  
  hypothesis_unique_type uniques;

  for (size_t id = 0; id != hypotheses.size(); ++ id)
    if (! hypotheses[id].empty()) {
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

void read_tstset(const path_set_type& files,
		 hypothesis_map_type& hypotheses,
		 const size_t scorers_size)
{
  typedef boost::spirit::istream_iterator iter_type;
  typedef kbest_feature_parser<iter_type> parser_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  if (files.empty())
    throw std::runtime_error("no files?");

  parser_type parser;
  kbest_feature_type kbest_feature;
  
  size_t iter = 0;
  for (path_set_type::const_iterator fiter = files.begin(); fiter != files.end(); ++ fiter, ++ iter) {
    if (! boost::filesystem::exists(*fiter) && *fiter != "-")
      throw std::runtime_error("no file: " + fiter->string());

    if (debug && mpi_rank == 0)
      std::cerr << "file: " << *fiter << std::endl;
    
    const size_t id_offset = size_t(iterative) * scorers_size * iter;

    if (boost::filesystem::is_directory(*fiter)) {
      for (size_t i = 0; /**/; ++ i) {
	if ((i + id_offset) % mpi_size != mpi_rank) continue;
	
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
	
	if (static_cast<int>(id % mpi_size)  == mpi_rank)
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

void bcast_weights(const int rank, weight_set_type& weights)
{
  typedef std::vector<char, std::allocator<char> > buffer_type;

  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  if (mpi_rank == rank) {
    boost::iostreams::filtering_ostream os;
    os.push(boost::iostreams::zlib_compressor());
    os.push(utils::mpi_device_bcast_sink(rank, 4096));
    
    static const weight_set_type::feature_type __empty;
    
    weight_set_type::const_iterator witer_begin = weights.begin();
    weight_set_type::const_iterator witer_end = weights.end();
    
    for (weight_set_type::const_iterator witer = witer_begin; witer != witer_end; ++ witer)
      if (*witer != 0.0) {
	const weight_set_type::feature_type feature(witer - witer_begin);
	if (feature != __empty) {
	  os << feature << ' ';
	  utils::encode_base64(*witer, std::ostream_iterator<char>(os));
	  os << '\n';
	}
      }
  } else {
    weights.clear();
    weights.allocate();
    
    boost::iostreams::filtering_istream is;
    is.push(boost::iostreams::zlib_decompressor());
    is.push(utils::mpi_device_bcast_source(rank, 4096));
    
    std::string feature;
    std::string value;
    
    while ((is >> feature) && (is >> value))
      weights[feature] = utils::decode_base64<double>(value);
  }
}


void options(int argc, char** argv)

{
  const int mpi_rank = MPI::COMM_WORLD.Get_rank();
  const int mpi_size = MPI::COMM_WORLD.Get_size();
  
  namespace po = boost::program_options;

  po::options_description opts_config("configuration options");
  
  opts_config.add_options()
    ("tstset",  po::value<path_set_type>(&tstset_files)->multitoken(), "test set file(s) (in kbest format)")
    ("refset",  po::value<path_set_type>(&refset_files)->multitoken(), "reference set file(s)")
    
    ("output", po::value<path_type>(&output_file)->default_value(output_file), "output file")

    ("bound-lower", po::value<path_type>(&bound_lower_file),                    "lower bounds definition for feature weights")
    ("bound-upper", po::value<path_type>(&bound_upper_file),                    "upper bounds definition for feature weights")

    ("value-lower", po::value<double>(&value_lower)->default_value(value_lower), "default lower bounds")
    ("value-upper", po::value<double>(&value_upper)->default_value(value_upper), "default upper_bounds")
    
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
    
    if (mpi_rank == 0)
      std::cout << argv[0] << " [options]\n"
		<< desc_command << std::endl;

    MPI::Finalize();
    exit(0);
  }
}

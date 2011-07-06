//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "cicada_lexicon_impl.hpp"

#include "utils/resource.hpp"
#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

path_type source_file = "-";
path_type target_file = "-";
path_type alignment_file;
path_type span_source_file;
path_type span_target_file;
path_type classes_source_file;
path_type classes_target_file;
path_type lexicon_source_target_file;
path_type lexicon_target_source_file;
path_type alignment_source_target_file;
path_type alignment_target_source_file;
path_type output_lexicon_source_target_file;
path_type output_lexicon_target_source_file;
path_type output_alignment_source_target_file;
path_type output_alignment_target_source_file;
path_type viterbi_source_target_file;
path_type viterbi_target_source_file;

int iteration_model1 = 5;
int iteration_hmm = 5;

bool symmetric_mode = false;
bool posterior_mode = false;
bool variational_bayes_mode = false;

bool moses_mode = false;
bool itg_mode = false;
bool max_match_mode = false;

// parameter...
double p0    = 0.01;
double prior_lexicon = 0.01;
double smooth_lexicon = 1e-20;
double prior_alignment = 0.01;
double smooth_alignment = 1e-20;

double threshold = 0.0;

int threads = 2;

int debug = 0;

#include "cicada_lexicon_maximize_impl.hpp"
#include "cicada_lexicon_model1_impl.hpp"
#include "cicada_lexicon_hmm_impl.hpp"

template <typename Learner, typename Maximizer>
void learn(const int iteration,
	   ttable_type& ttable_source_target,
	   ttable_type& ttable_target_source,
	   atable_type& atable_source_target,
	   atable_type& atalbe_target_source,
	   const classes_type& classes_source,
	   const classes_type& classes_target,
	   aligned_type& aligned_source_target,
	   aligned_type& aligned_target_source);

template <typename Aligner>
void viterbi(const ttable_type& ttable_source_target,
	     const ttable_type& ttable_target_source,
	     const atable_type& atable_source_target,
	     const atable_type& atalbe_target_source,
	     const classes_type& classes_source,
	     const classes_type& classes_target);

void options(int argc, char** argv);

int main(int argc, char ** argv)
{
  try {
    options(argc, argv);
    
    if (itg_mode && max_match_mode)
      throw std::runtime_error("you cannot specify both of ITG and max-match for Viterbi alignment");
    
    threads = utils::bithack::max(threads, 1);
    
    ttable_type ttable_source_target(prior_lexicon, smooth_lexicon);
    ttable_type ttable_target_source(prior_lexicon, smooth_lexicon);

    atable_type atable_source_target(prior_alignment, smooth_alignment);
    atable_type atable_target_source(prior_alignment, smooth_alignment);

    classes_type classes_source;
    classes_type classes_target;
    
    aligned_type aligned_source_target;
    aligned_type aligned_target_source;
    
    if (! lexicon_source_target_file.empty())
      if (lexicon_source_target_file != "-" && ! boost::filesystem::exists(lexicon_source_target_file))
	throw std::runtime_error("no file: " + lexicon_source_target_file.string());

    if (! lexicon_target_source_file.empty())
      if (lexicon_target_source_file != "-" && ! boost::filesystem::exists(lexicon_target_source_file))
	throw std::runtime_error("no file: " + lexicon_target_source_file.string());

    if (! alignment_source_target_file.empty())
      if (alignment_source_target_file != "-" && ! boost::filesystem::exists(alignment_source_target_file))
	throw std::runtime_error("no file: " + alignment_source_target_file.string());

    if (! alignment_target_source_file.empty())
      if (alignment_target_source_file != "-" && ! boost::filesystem::exists(alignment_target_source_file))
	throw std::runtime_error("no file: " + alignment_target_source_file.string());

    if (! classes_source_file.empty())
      if (classes_source_file != "-" && ! boost::filesystem::exists(classes_source_file))
	throw std::runtime_error("no file: " + classes_source_file.string());

    if (! classes_target_file.empty())
      if (classes_target_file != "-" && ! boost::filesystem::exists(classes_target_file))
	throw std::runtime_error("no file: " + classes_target_file.string());
    
    boost::thread_group workers_read;
    
    // read lexicon
    if (! lexicon_source_target_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_lexicon, boost::cref(lexicon_source_target_file), boost::ref(ttable_source_target))));
    if (! lexicon_target_source_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_lexicon, boost::cref(lexicon_target_source_file), boost::ref(ttable_target_source))));
    
    // read alignment
    if (! alignment_source_target_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_alignment, boost::cref(alignment_source_target_file), boost::ref(atable_source_target))));
    if (! alignment_target_source_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_alignment, boost::cref(alignment_target_source_file), boost::ref(atable_target_source))));

    // read classes
    if (! classes_source_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_classes, boost::cref(classes_source_file), boost::ref(classes_source))));
    if (! classes_target_file.empty())
      workers_read.add_thread(new boost::thread(boost::bind(read_classes, boost::cref(classes_target_file), boost::ref(classes_target))));
    
    workers_read.join_all();
    
    if (iteration_model1 > 0) {
      if (variational_bayes_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnModel1SymmetricPosterior, MaximizeBayes>(iteration_model1,
								ttable_source_target,
								ttable_target_source,
								atable_source_target,
								atable_target_source,
								classes_source,
								classes_target,
								aligned_source_target,
								aligned_target_source);
	  else
	    learn<LearnModel1Symmetric, MaximizeBayes>(iteration_model1,
						       ttable_source_target,
						       ttable_target_source,
						       atable_source_target,
						       atable_target_source,
						       classes_source,
						       classes_target,
						       aligned_source_target,
						       aligned_target_source);
	} else {
	  if (posterior_mode)
	    learn<LearnModel1Posterior, MaximizeBayes>(iteration_model1,
						       ttable_source_target,
						       ttable_target_source,
						       atable_source_target,
						       atable_target_source,
						       classes_source,
						       classes_target,
						       aligned_source_target,
						       aligned_target_source);
	  else
	    learn<LearnModel1, MaximizeBayes>(iteration_model1,
					      ttable_source_target,
					      ttable_target_source,
					      atable_source_target,
					      atable_target_source,
					      classes_source,
					      classes_target,
					      aligned_source_target,
					      aligned_target_source);
	}
	
      } else {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnModel1SymmetricPosterior, Maximize>(iteration_model1,
							   ttable_source_target,
							   ttable_target_source,
							   atable_source_target,
							   atable_target_source,
							   classes_source,
							   classes_target,
							   aligned_source_target,
							   aligned_target_source);
	  else
	    learn<LearnModel1Symmetric, Maximize>(iteration_model1,
						  ttable_source_target,
						  ttable_target_source,
						  atable_source_target,
						  atable_target_source,
						  classes_source,
						  classes_target,
						  aligned_source_target,
						  aligned_target_source);
	} else {
	  if (posterior_mode)
	    learn<LearnModel1Posterior, Maximize>(iteration_model1,
						  ttable_source_target,
						  ttable_target_source,
						  atable_source_target,
						  atable_target_source,
						  classes_source,
						  classes_target,
						  aligned_source_target,
						  aligned_target_source);
	  else
	    learn<LearnModel1, Maximize>(iteration_model1,
					 ttable_source_target,
					 ttable_target_source,
					 atable_source_target,
					 atable_target_source,
					 classes_source,
					 classes_target,
					 aligned_source_target,
					 aligned_target_source);
	}
      }
    }
    
    if (iteration_hmm > 0) {
      if (variational_bayes_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnHMMSymmetricPosterior, MaximizeBayes>(iteration_hmm,
							     ttable_source_target,
							     ttable_target_source,
							     atable_source_target,
							     atable_target_source,
							     classes_source,
							     classes_target,
							     aligned_source_target,
							     aligned_target_source);
	  else
	    learn<LearnHMMSymmetric, MaximizeBayes>(iteration_hmm,
						    ttable_source_target,
						    ttable_target_source,
						    atable_source_target,
						    atable_target_source,
						    classes_source,
						    classes_target,
						    aligned_source_target,
						    aligned_target_source);
	} else {
	  if (posterior_mode)
	    learn<LearnHMMPosterior, MaximizeBayes>(iteration_hmm,
						    ttable_source_target,
						    ttable_target_source,
						    atable_source_target,
						    atable_target_source,
						    classes_source,
						    classes_target,
						    aligned_source_target,
						    aligned_target_source);
	  else
	    learn<LearnHMM, MaximizeBayes>(iteration_hmm,
					   ttable_source_target,
					   ttable_target_source,
					   atable_source_target,
					   atable_target_source,
					   classes_source,
					   classes_target,
					   aligned_source_target,
					   aligned_target_source);
	}
	
      } else {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnHMMSymmetricPosterior, Maximize>(iteration_hmm,
							ttable_source_target,
							ttable_target_source,
							atable_source_target,
							atable_target_source,
							classes_source,
							classes_target,
							aligned_source_target,
							aligned_target_source);
	  else
	    learn<LearnHMMSymmetric, Maximize>(iteration_hmm,
					       ttable_source_target,
					       ttable_target_source,
					       atable_source_target,
					       atable_target_source,
					       classes_source,
					       classes_target,
					       aligned_source_target,
					       aligned_target_source);
	} else {
	  if (posterior_mode)
	    learn<LearnHMMPosterior, Maximize>(iteration_hmm,
					       ttable_source_target,
					       ttable_target_source,
					       atable_source_target,
					       atable_target_source,
					       classes_source,
					       classes_target,
					       aligned_source_target,
					       aligned_target_source);
	  else
	    learn<LearnHMM, Maximize>(iteration_hmm,
				      ttable_source_target,
				      ttable_target_source,
				      atable_source_target,
				      atable_target_source,
				      classes_source,
				      classes_target,
				      aligned_source_target,
				      aligned_target_source);
	}
      }
    }
    
    if (! viterbi_source_target_file.empty() || ! viterbi_target_source_file.empty()) {
      if (itg_mode)
	viterbi<ITGHMM>(ttable_source_target,
			ttable_target_source,
			atable_source_target,
			atable_target_source,
			classes_source,
			classes_target);
      else if (max_match_mode)
	viterbi<MaxMatchHMM>(ttable_source_target,
			     ttable_target_source,
			     atable_source_target,
			     atable_target_source,
			     classes_source,
			     classes_target);
      else
	viterbi<ViterbiHMM>(ttable_source_target,
			    ttable_target_source,
			    atable_source_target,
			    atable_target_source,
			    classes_source,
			    classes_target);
    }
    
    // final writing
    boost::thread_group workers_write;
    
    // write lexicon
    if (! output_lexicon_source_target_file.empty())
      workers_write.add_thread(new boost::thread(boost::bind(write_lexicon,
							     boost::cref(output_lexicon_source_target_file),
							     boost::cref(ttable_source_target),
							     boost::cref(aligned_source_target),
							     threshold)));
    
    if (! output_lexicon_target_source_file.empty())
      workers_write.add_thread(new boost::thread(boost::bind(write_lexicon,
							     boost::cref(output_lexicon_target_source_file),
							     boost::cref(ttable_target_source),
							     boost::cref(aligned_target_source),
							     threshold)));

    // write alignment
    if (! output_alignment_source_target_file.empty())
      workers_write.add_thread(new boost::thread(boost::bind(write_alignment,
							     boost::cref(output_alignment_source_target_file),
							     boost::cref(atable_source_target))));
    
    if (! output_alignment_target_source_file.empty())
      workers_write.add_thread(new boost::thread(boost::bind(write_alignment,
							     boost::cref(output_alignment_target_source_file),
							     boost::cref(atable_target_source))));
    
    
    workers_write.join_all();
  }
  catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}

struct greater_second
{
  template <typename Tp>
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x->second > y->second;
  }
};


template <typename LearnerSet, typename Maximizer>
struct TaskMaximize : public Maximizer
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  TaskMaximize(LearnerSet& __learners,
	       const int __id,
	       ttable_type& __ttable_source_target,
	       ttable_type& __ttable_target_source,
	       aligned_type& __aligned_source_target,
	       aligned_type& __aligned_target_source)
    : learners(__learners),
      id(__id),
      ttable_source_target(__ttable_source_target),
      ttable_target_source(__ttable_target_source),
      aligned_source_target(__aligned_source_target),
      aligned_target_source(__aligned_target_source) {}
  
  void operator()()
  {
    for (word_type::id_type source_id = id; source_id < ttable_source_target.size(); source_id += learners.size()) {
      for (size_t i = 0; i != learners.size(); ++ i) {
	if (learners[i].ttable_counts_source_target.exists(source_id)) {
	  ttable_source_target[source_id] += learners[i].ttable_counts_source_target[source_id];
	  learners[i].ttable_counts_source_target[source_id].clear();
	}
	
	if (learners[i].aligned_source_target.exists(source_id)) {
	  aligned_source_target[source_id] += learners[i].aligned_source_target[source_id];
	  learners[i].aligned_source_target[source_id].clear();
	}
      }
      
      if (ttable_source_target.exists(source_id))
	Maximizer::operator()(ttable_source_target[source_id], ttable_source_target.prior);
    }
    
    for (word_type::id_type target_id = id; target_id < ttable_target_source.size(); target_id += learners.size()) {
      for (size_t i = 0; i != learners.size(); ++ i) {
	if (learners[i].ttable_counts_target_source.exists(target_id)) {
	  ttable_target_source[target_id] += learners[i].ttable_counts_target_source[target_id];
	  learners[i].ttable_counts_target_source[target_id].clear();
	}
	
	if (learners[i].aligned_target_source.exists(target_id)) {
	  aligned_target_source[target_id] += learners[i].aligned_target_source[target_id];
	  learners[i].aligned_target_source[target_id].clear();
	}
      }
      
      if (ttable_target_source.exists(target_id))
	Maximizer::operator()(ttable_target_source[target_id], ttable_target_source.prior);
    }
  }
  
  LearnerSet& learners;
  const int id;

  ttable_type& ttable_source_target;
  ttable_type& ttable_target_source;
  
  aligned_type& aligned_source_target;
  aligned_type& aligned_target_source;
};

template <typename Learner>
struct TaskLearn : public Learner
{
  struct bitext_type
  {
    bitext_type() : source(), target(), alignment() {}
    bitext_type(const sentence_type& __source, const sentence_type& __target)
      : source(__source), target(__target), alignment() {}
    bitext_type(const sentence_type& __source, const sentence_type& __target, const alignment_type& __alignment)
      : source(__source), target(__target), alignment(__alignment) {}
    
    sentence_type source;
    sentence_type target;
    alignment_type alignment;
  };
  
  typedef std::vector<bitext_type, std::allocator<bitext_type> > bitext_set_type;
  typedef utils::lockfree_list_queue<bitext_set_type, std::allocator<bitext_set_type> > queue_type;
  
  TaskLearn(queue_type& __queue,
	    const LearnBase& __base)
    : Learner(__base),
      queue(__queue) {}
  
  void operator()()
  {
    Learner::initialize();
    
    bitext_set_type bitexts;
    
    for (;;) {
      bitexts.clear();
      queue.pop_swap(bitexts);
      if (bitexts.empty()) break;
      
      typename bitext_set_type::const_iterator biter_end = bitexts.end();
      for (typename bitext_set_type::const_iterator biter = bitexts.begin(); biter != biter_end; ++ biter) {
	if (biter->alignment.empty())
	  Learner::operator()(biter->source, biter->target);
	else
	  Learner::operator()(biter->source, biter->target, biter->alignment);
      }
    }
  }
  
  queue_type& queue;
};

template <typename Learner, typename Maximizer>
void learn(const int iteration,
	   ttable_type& ttable_source_target,
	   ttable_type& ttable_target_source,
	   atable_type& atable_source_target,
	   atable_type& atable_target_source,
	   const classes_type& classes_source,
	   const classes_type& classes_target,
	   aligned_type& aligned_source_target,
	   aligned_type& aligned_target_source)
{
  typedef TaskLearn<Learner> learner_type;
  
  typedef typename learner_type::bitext_type     bitext_type;
  typedef typename learner_type::bitext_set_type bitext_set_type;
  typedef typename learner_type::queue_type      queue_type;
  
  typedef std::vector<learner_type, std::allocator<learner_type> > learner_set_type;
  
  typedef TaskMaximize<learner_set_type, Maximizer> maximizer_type;
  
  queue_type       queue(threads * 64);
  learner_set_type learners(threads, learner_type(queue,
						  LearnBase(ttable_source_target, ttable_target_source,
							    atable_source_target, atable_target_source,
							    classes_source, classes_target)));
  
  for (int iter = 0; iter < iteration; ++ iter) {
    if (debug)
      std::cerr << "iteration: " << iter << std::endl;

    utils::resource accumulate_start;
    
    boost::thread_group workers_learn;
    for (size_t i = 0; i != learners.size(); ++ i)
      workers_learn.add_thread(new boost::thread(boost::ref(learners[i])));
    
    utils::compress_istream is_src(source_file, 1024 * 1024);
    utils::compress_istream is_trg(target_file, 1024 * 1024);
    std::auto_ptr<std::istream> is_align(! alignment_file.empty()
					 ? new utils::compress_istream(alignment_file, 1024 * 1024) : 0);
    
    bitext_type     bitext;
    bitext_set_type bitexts;
    
    size_t num_bitext = 0;
    
    for (;;) {
      is_src >> bitext.source;
      is_trg >> bitext.target;
      if (is_align.get())
	*is_align >> bitext.alignment;
      
      if (! is_src || ! is_trg || (is_align.get() && ! *is_align)) break;
      
      if (bitext.source.empty() || bitext.target.empty()) continue;
      
      bitexts.push_back(bitext);

      ++ num_bitext;
      if (debug) {
	if (num_bitext % 10000 == 0)
	  std::cerr << '.';
	if (num_bitext % 1000000 == 0)
	  std::cerr << '\n';
      }
      
      if (bitexts.size() == 64) {
	queue.push_swap(bitexts);
	bitexts.clear();
      }
    }

    if (! bitexts.empty())
      queue.push_swap(bitexts);
    
    if (debug && num_bitext >= 10000)
      std::cerr << std::endl;
    if (debug)
      std::cerr << "# of bitexts: " << num_bitext << std::endl;

    if (is_src || is_trg || (is_align.get() && *is_align))
      throw std::runtime_error("# of samples do not match");
        
    for (size_t i = 0; i != learners.size(); ++ i) {
      bitexts.clear();
      queue.push_swap(bitexts);
    }
    
    workers_learn.join_all();
    
    // merge and normalize...! 
    ttable_source_target.initialize();
    ttable_target_source.initialize();
    aligned_source_target.initialize();
    aligned_target_source.initialize();

    ttable_source_target.reserve(word_type::allocated());
    ttable_target_source.reserve(word_type::allocated());
    aligned_source_target.reserve(word_type::allocated());
    aligned_target_source.reserve(word_type::allocated());
    
    ttable_source_target.resize(word_type::allocated());
    ttable_target_source.resize(word_type::allocated());
    aligned_source_target.resize(word_type::allocated());
    aligned_target_source.resize(word_type::allocated());
    
    double objective_source_target = 0;
    double objective_target_source = 0;
    
    boost::thread_group workers_maximize;
    for (size_t i = 0; i != learners.size(); ++ i) {
      objective_source_target += learners[i].objective_source_target;
      objective_target_source += learners[i].objective_target_source;
      
      workers_maximize.add_thread(new boost::thread(maximizer_type(learners, i,
								   ttable_source_target, ttable_target_source,
								   aligned_source_target, aligned_target_source)));
    }
    
    if (debug)
      std::cerr << "perplexity for P(target | source): " << objective_source_target << '\n'
		<< "perplexity for P(source | target): " << objective_target_source << '\n';
    
    // merge atable counts... (we will dynamically create probability table!)
    atable_source_target.initialize();
    atable_target_source.initialize();
    for (size_t i = 0; i != learners.size(); ++ i) {
      atable_source_target += learners[i].atable_counts_source_target;
      atable_target_source += learners[i].atable_counts_target_source;
    }
    
    for (size_t i = 0; i != learners.size(); ++ i) {
      learners[i].atable_source_target = atable_source_target;
      learners[i].atable_target_source = atable_target_source;
    }
    
    workers_maximize.join_all();
    
    utils::resource accumulate_end;
    
    if (debug)
      std::cerr << "cpu time:  " << accumulate_end.cpu_time() - accumulate_start.cpu_time() << std::endl
		<< "user time: " << accumulate_end.user_time() - accumulate_start.user_time() << std::endl;
    
  }
}

struct ViterbiMapReduce
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  struct bitext_type
  {
    bitext_type() : id(size_type(-1)), source(), target(), alignment() {}
    bitext_type(const size_type& __id, const sentence_type& __source, const sentence_type& __target)
      : id(__id), source(__source), target(__target), alignment() {}
    bitext_type(const size_type& __id, const sentence_type& __source, const sentence_type& __target, const alignment_type& __alignment)
      : id(__id), source(__source), target(__target), alignment(__alignment) {}
    
    bitext_type(const size_type& __id,
		const sentence_type& __source, const sentence_type& __target,
		const span_set_type& __span_source, const span_set_type& __span_target,
		const alignment_type& __alignment)
      : id(__id),
	source(__source), target(__target),
	span_source(__span_source), span_target(__span_target),
	alignment(__alignment) {}
    
    size_type     id;
    sentence_type source;
    sentence_type target;
    span_set_type span_source;
    span_set_type span_target;
    alignment_type alignment;

    void clear()
    {
      id = size_type(-1);
      source.clear();
      target.clear();
      span_source.clear();
      span_target.clear();
      alignment.clear();
    }
    
    void swap(bitext_type& x)
    {
      std::swap(id, x.id);
      source.swap(x.source);
      target.swap(x.target);
      span_source.swap(x.span_source);
      span_target.swap(x.span_target);
      alignment.swap(x.alignment);
    }
  };
  
  typedef utils::lockfree_list_queue<bitext_type, std::allocator<bitext_type> > queue_type;
};

namespace std
{
  inline
  void swap(ViterbiMapReduce::bitext_type& x, ViterbiMapReduce::bitext_type& y)
  {
    x.swap(y);
  }
};

template <typename Aligner>
struct ViterbiMapper : public ViterbiMapReduce, public Aligner
{
  queue_type& mapper;
  queue_type& reducer_source_target;
  queue_type& reducer_target_source;
  
  ViterbiMapper(const Aligner& __aligner, queue_type& __mapper, queue_type& __reducer_source_target, queue_type& __reducer_target_source)
    : Aligner(__aligner),
      mapper(__mapper),
      reducer_source_target(__reducer_source_target),
      reducer_target_source(__reducer_target_source)
  {}

  void operator()()
  {
    bitext_type bitext;
    alignment_type alignment_source_target;
    alignment_type alignment_target_source;
    
    for (;;) {
      mapper.pop_swap(bitext);
      if (bitext.id == size_type(-1)) break;
      
      alignment_source_target.clear();
      alignment_target_source.clear();
      
      if (! bitext.source.empty() && ! bitext.target.empty())
	Aligner::operator()(bitext.source, bitext.target, bitext.span_source, bitext.span_target, alignment_source_target, alignment_target_source);
      
      reducer_source_target.push(bitext_type(bitext.id, bitext.source, bitext.target, alignment_source_target));
      reducer_target_source.push(bitext_type(bitext.id, bitext.target, bitext.source, alignment_target_source));
    }
  }
};

struct ViterbiReducer : public ViterbiMapReduce
{
  struct less_bitext
  {
    bool operator()(const bitext_type& x, const bitext_type& y) const
    {
      return x.id < y.id;
    }
  };
  typedef std::set<bitext_type, less_bitext, std::allocator<bitext_type> > bitext_set_type;

  path_type   path;
  queue_type& queue;
  
  ViterbiReducer(const path_type& __path, queue_type& __queue) : path(__path), queue(__queue) {}

  typedef int index_type;
  typedef std::vector<index_type, std::allocator<index_type> > index_set_type;
  typedef std::vector<index_set_type, std::allocator<index_set_type> > align_set_type;
  typedef std::set<index_type, std::less<index_type>, std::allocator<index_type> > align_none_type;
  
  align_set_type  aligns;
  align_none_type aligns_none;
  
  void write(std::ostream& os, const bitext_type& bitext)
  {
    if (moses_mode)
      os << bitext.alignment << '\n';
    else {
      os << "# Sentence pair (" << (bitext.id + 1) << ')'
	 << " source length " << bitext.source.size()
	 << " target length " << bitext.target.size()
	 << " alignment score : " << 0 << '\n';
      os << bitext.target << '\n';
    
      if (bitext.alignment.empty() || bitext.source.empty() || bitext.target.empty()) {
	os << "NULL ({ })";
	sentence_type::const_iterator siter_end = bitext.source.end();
	for (sentence_type::const_iterator siter = bitext.source.begin(); siter != siter_end; ++ siter)
	  os << ' ' << *siter << " ({ })";
	os << '\n';
      } else {
	aligns.clear();
	aligns.resize(bitext.source.size());
      
	aligns_none.clear();
	for (size_type trg = 0; trg != bitext.target.size(); ++ trg)
	  aligns_none.insert(trg + 1);
      
	alignment_type::const_iterator aiter_end = bitext.alignment.end();
	for (alignment_type::const_iterator aiter = bitext.alignment.begin(); aiter != aiter_end; ++ aiter) {
	  aligns[aiter->source].push_back(aiter->target + 1);
	  aligns_none.erase(aiter->target + 1);
	}
      
	os << "NULL";
	os << " ({ ";
	std::copy(aligns_none.begin(), aligns_none.end(), std::ostream_iterator<index_type>(os, " "));
	os << "})";
      
	for (size_type src = 0; src != bitext.source.size(); ++ src) {
	  os << ' ' << bitext.source[src];
	  os << " ({ ";
	  std::copy(aligns[src].begin(), aligns[src].end(), std::ostream_iterator<index_type>(os, " "));
	  os << "})";
	}
	os << '\n';
      }
    }
  }
  
  void operator()()
  {
    if (path.empty()) {
      bitext_type bitext;
      for (;;) {
	queue.pop_swap(bitext);
	if (bitext.id == size_type(-1)) break;
      }
    } else {
      bitext_set_type bitexts;

      const bool flush_output = (path == "-"
				 || (boost::filesystem::exists(path)
				     && ! boost::filesystem::is_regular_file(path)));
      
      utils::compress_ostream os(path, 1024 * 1024 * (! flush_output));
      
      size_type id = 0;
      bitext_type bitext;
      for (;;) {
	queue.pop_swap(bitext);
	if (bitext.id == size_type(-1)) break;
	
	if (bitext.id == id) {
	  write(os, bitext);
	  ++ id;
	} else
	  bitexts.insert(bitext);
	
	while (! bitexts.empty() && bitexts.begin()->id == id) {
	  write(os, *bitexts.begin());
	  bitexts.erase(bitexts.begin());
	  ++ id;
	}
      }
      
      while (! bitexts.empty() && bitexts.begin()->id == id) {
	write(os, *bitexts.begin());
	bitexts.erase(bitexts.begin());
	++ id;
      }
      
      if (! bitexts.empty())
	throw std::runtime_error("error while writeing viterbi output?");
    }
  }
};

template <typename Aligner>
void viterbi(const ttable_type& ttable_source_target,
	     const ttable_type& ttable_target_source,
	     const atable_type& atable_source_target,
	     const atable_type& atable_target_source,
	     const classes_type& classes_source,
	     const classes_type& classes_target)
{
  typedef ViterbiReducer         reducer_type;
  typedef ViterbiMapper<Aligner> mapper_type;
  
  typedef reducer_type::bitext_type bitext_type;
  typedef reducer_type::queue_type  queue_type;
  
  typedef std::vector<mapper_type, std::allocator<mapper_type> > mapper_set_type;

  if (debug)
    std::cerr << "Viterbi alignment" << std::endl;
  
  queue_type queue(threads * 4096);
  queue_type queue_source_target(threads * 4096);
  queue_type queue_target_source(threads * 4096);
  
  boost::thread_group mapper;
  for (int i = 0; i != threads; ++ i)
    mapper.add_thread(new boost::thread(mapper_type(Aligner(ttable_source_target, ttable_target_source,
							    atable_source_target, atable_target_source,
							    classes_source, classes_target),
						    queue,
						    queue_source_target,
						    queue_target_source)));
  
  boost::thread_group reducer;
  reducer.add_thread(new boost::thread(reducer_type(viterbi_source_target_file, queue_source_target)));
  reducer.add_thread(new boost::thread(reducer_type(viterbi_target_source_file, queue_target_source)));
  
  bitext_type bitext;
  bitext.id = 0;
  
  utils::resource viterbi_start;
  
  utils::compress_istream is_src(source_file, 1024 * 1024);
  utils::compress_istream is_trg(target_file, 1024 * 1024);

  std::auto_ptr<std::istream> is_span_src(! span_source_file.empty()
					  ? new utils::compress_istream(span_source_file, 1024 * 1024) : 0);
  std::auto_ptr<std::istream> is_span_trg(! span_target_file.empty()
					  ? new utils::compress_istream(span_target_file, 1024 * 1024) : 0);
  
  for (;;) {
    is_src >> bitext.source;
    is_trg >> bitext.target;
    
    if (is_span_src.get())
      *is_span_src >> bitext.span_source;
    if (is_span_trg.get())
      *is_span_trg >> bitext.span_target;
    
    if (! is_src || ! is_trg || (is_span_src.get() && ! *is_span_src) || (is_span_trg.get() && ! *is_span_trg)) break;
    
    queue.push(bitext);
    
    ++ bitext.id;
    
    if (debug) {
      if (bitext.id % 10000 == 0)
	std::cerr << '.';
      if (bitext.id % 1000000 == 0)
	std::cerr << '\n';
    }
  }
  
  if (debug && bitext.id >= 10000)
    std::cerr << std::endl;
  if (debug)
    std::cerr << "# of bitexts: " << bitext.id << std::endl;
  
  if (is_src || is_trg || (is_span_src.get() && *is_span_src) || (is_span_trg.get() && *is_span_trg))
    throw std::runtime_error("# of samples do not match");
  
  for (int i = 0; i != threads; ++ i) {
    bitext.clear();
    queue.push_swap(bitext);
  }

  mapper.join_all();

  bitext.clear();
  queue_source_target.push_swap(bitext);
  bitext.clear();
  queue_target_source.push_swap(bitext);
  
  reducer.join_all();

  utils::resource viterbi_end;
  
  if (debug)
    std::cerr << "cpu time:  " << viterbi_end.cpu_time() - viterbi_start.cpu_time() << std::endl
	      << "user time: " << viterbi_end.user_time() - viterbi_start.user_time() << std::endl;
}

void options(int argc, char** argv)
{
  namespace po = boost::program_options;
  
  po::variables_map variables;
  
  po::options_description desc("options");
  desc.add_options()
    ("source",    po::value<path_type>(&source_file),    "source file")
    ("target",    po::value<path_type>(&target_file),    "target file")
    ("alignment", po::value<path_type>(&alignment_file), "alignment file")
    
    ("span-source", po::value<path_type>(&span_source_file), "source span file")
    ("span-target", po::value<path_type>(&span_target_file), "target span file")

    ("classes-source", po::value<path_type>(&classes_source_file), "source classes file")
    ("classes-target", po::value<path_type>(&classes_target_file), "target classes file")
    
    ("lexicon-source-target", po::value<path_type>(&lexicon_source_target_file), "lexicon model for P(target | source)")
    ("lexicon-target-source", po::value<path_type>(&lexicon_target_source_file), "lexicon model for P(source | target)")
    ("alignment-source-target", po::value<path_type>(&alignment_source_target_file), "alignment model for P(target | source)")
    ("alignment-target-source", po::value<path_type>(&alignment_target_source_file), "alignment model for P(source | target)")

    ("output-lexicon-source-target", po::value<path_type>(&output_lexicon_source_target_file), "lexicon model output for P(target | source)")
    ("output-lexicon-target-source", po::value<path_type>(&output_lexicon_target_source_file), "lexicon model output for P(source | target)")
    ("output-alignment-source-target", po::value<path_type>(&output_alignment_source_target_file), "alignment model output for P(target | source)")
    ("output-alignment-target-source", po::value<path_type>(&output_alignment_target_source_file), "alignment model output for P(source | target)")
    
    ("viterbi-source-target", po::value<path_type>(&viterbi_source_target_file), "viterbi for P(target | source)")
    ("viterbi-target-source", po::value<path_type>(&viterbi_target_source_file), "viterbi for P(source | target)")
    
    ("iteration-model1", po::value<int>(&iteration_model1)->default_value(iteration_model1), "max Model1 iteration")
    ("iteration-hmm", po::value<int>(&iteration_hmm)->default_value(iteration_hmm), "max HMM iteration")
    
    ("symmetric",  po::bool_switch(&symmetric_mode),  "symmetric training")
    ("posterior",  po::bool_switch(&posterior_mode),  "posterior constrained training")
    ("variational-bayes", po::bool_switch(&variational_bayes_mode), "variational Bayes estimates")
    
    ("itg",       po::bool_switch(&itg_mode),       "ITG alignment")
    ("max-match", po::bool_switch(&max_match_mode), "maximum matching alignment")
    ("moses",     po::bool_switch(&moses_mode),     "Moses alignment foramt")

    ("p0",             po::value<double>(&p0)->default_value(p0),                               "parameter for NULL alignment")
    ("prior-lexicon",  po::value<double>(&prior_lexicon)->default_value(prior_lexicon),         "Dirichlet prior for variational Bayes")
    ("smooth-lexicon", po::value<double>(&smooth_lexicon)->default_value(smooth_lexicon),       "smoothing parameter for uniform distribution")
    ("prior-alignment",  po::value<double>(&prior_alignment)->default_value(prior_alignment),   "Dirichlet prior for variational Bayes")
    ("smooth-alignment", po::value<double>(&smooth_alignment)->default_value(smooth_alignment), "smoothing parameter for uniform distribution")
    
    ("threshold", po::value<double>(&threshold)->default_value(threshold), "write with beam-threshold (<= 0.0 implies no beam)")

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

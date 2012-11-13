//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include "cicada_lexicon_impl.hpp"

#include "utils/resource.hpp"
#include "utils/program_options.hpp"
#include "utils/compress_stream.hpp"
#include "utils/lockfree_list_queue.hpp"
#include "utils/bithack.hpp"
#include "utils/mathop.hpp"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

path_type source_file = "-";
path_type target_file = "-";
path_type alignment_file;
path_type dependency_source_file;
path_type dependency_target_file;
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
path_type projected_source_file;
path_type projected_target_file;
path_type posterior_source_target_file;
path_type posterior_target_source_file;
path_type posterior_combined_file;

int iteration_model1 = 5;
int iteration_hmm = 5;

bool symmetric_mode = false;
bool posterior_mode = false;
bool variational_bayes_mode = false;
bool pgd_mode = false;

bool moses_mode = false;
bool itg_mode = false;
bool max_match_mode = false;

bool permutation_mode = false;
bool hybrid_mode = false;
bool degree2_mode = false;
bool mst_mode = false;
bool single_root_mode = false;

// parameter...
double p0    = 0.01;
double prior_lexicon = 0.01;
double smooth_lexicon = 1e-20;
double prior_alignment = 0.01;
double smooth_alignment = 1e-20;

double l0_alpha = 100;
double l0_beta = 0.01;

double threshold = 0.0;

int threads = 2;

int debug = 0;

#include "cicada_lexicon_maximize_impl.hpp"
#include "cicada_lexicon_model1_impl.hpp"
#include "cicada_lexicon_hmm_impl.hpp"

template <typename Learner, typename Maximizer>
void learn(const Maximizer& maximier,
	   const int iteration,
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

template <typename Analyzer>
void project_dependency(const ttable_type& ttable_source_target,
			const ttable_type& ttable_target_source,
			const atable_type& atable_source_target,
			const atable_type& atalbe_target_source,
			const classes_type& classes_source,
			const classes_type& classes_target);

template <typename Infer>
void posterior(const ttable_type& ttable_source_target,
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
    
    if (variational_bayes_mode && pgd_mode)
      throw std::runtime_error("either variational-bayes, pgd or none");

    if (! projected_target_file.empty())    
      if (dependency_source_file != "-" && ! boost::filesystem::exists(dependency_source_file))
	throw std::runtime_error("no source side dependency");
    
    if (! projected_source_file.empty())
      if (dependency_target_file != "-" && ! boost::filesystem::exists(dependency_target_file))
	throw std::runtime_error("no target side dependency");
    
    if (int(hybrid_mode) + degree2_mode + mst_mode + permutation_mode > 1)
      throw std::runtime_error("you cannot specify both of Hybrid, Degree2 and MST dependency, permutation parsing");
    
    if (int(hybrid_mode) + degree2_mode + mst_mode + permutation_mode == 0)
      hybrid_mode = true;
    
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
      if (debug)
	std::cerr << "start Model1 training" << std::endl;
      
      if (variational_bayes_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnModel1SymmetricPosterior, MaximizeBayes>(MaximizeBayes(),
								iteration_model1,
								ttable_source_target,
								ttable_target_source,
								atable_source_target,
								atable_target_source,
								classes_source,
								classes_target,
								aligned_source_target,
								aligned_target_source);
	  else
	    learn<LearnModel1Symmetric, MaximizeBayes>(MaximizeBayes(),
						       iteration_model1,
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
	    learn<LearnModel1Posterior, MaximizeBayes>(MaximizeBayes(),
						       iteration_model1,
						       ttable_source_target,
						       ttable_target_source,
						       atable_source_target,
						       atable_target_source,
						       classes_source,
						       classes_target,
						       aligned_source_target,
						       aligned_target_source);
	  else
	    learn<LearnModel1, MaximizeBayes>(MaximizeBayes(),
					      iteration_model1,
					      ttable_source_target,
					      ttable_target_source,
					      atable_source_target,
					      atable_target_source,
					      classes_source,
					      classes_target,
					      aligned_source_target,
					      aligned_target_source);
	}

      } else if (pgd_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnModel1SymmetricPosterior, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
							     iteration_model1,
							     ttable_source_target,
							     ttable_target_source,
							     atable_source_target,
							     atable_target_source,
							     classes_source,
							     classes_target,
							     aligned_source_target,
							     aligned_target_source);
	  else
	    learn<LearnModel1Symmetric, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
						    iteration_model1,
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
	    learn<LearnModel1Posterior, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
						    iteration_model1,
						    ttable_source_target,
						    ttable_target_source,
						    atable_source_target,
						    atable_target_source,
						    classes_source,
						    classes_target,
						    aligned_source_target,
						    aligned_target_source);
	  else
	    learn<LearnModel1, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
					   iteration_model1,
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
	    learn<LearnModel1SymmetricPosterior, Maximize>(Maximize(),
							   iteration_model1,
							   ttable_source_target,
							   ttable_target_source,
							   atable_source_target,
							   atable_target_source,
							   classes_source,
							   classes_target,
							   aligned_source_target,
							   aligned_target_source);
	  else
	    learn<LearnModel1Symmetric, Maximize>(Maximize(),
						  iteration_model1,
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
	    learn<LearnModel1Posterior, Maximize>(Maximize(),
						  iteration_model1,
						  ttable_source_target,
						  ttable_target_source,
						  atable_source_target,
						  atable_target_source,
						  classes_source,
						  classes_target,
						  aligned_source_target,
						  aligned_target_source);
	  else
	    learn<LearnModel1, Maximize>(Maximize(),
					 iteration_model1,
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
      if (debug)
	std::cerr << "start HMM training" << std::endl;

      if (variational_bayes_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnHMMSymmetricPosterior, MaximizeBayes>(MaximizeBayes(),
							     iteration_hmm,
							     ttable_source_target,
							     ttable_target_source,
							     atable_source_target,
							     atable_target_source,
							     classes_source,
							     classes_target,
							     aligned_source_target,
							     aligned_target_source);
	  else
	    learn<LearnHMMSymmetric, MaximizeBayes>(MaximizeBayes(),
						    iteration_hmm,
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
	    learn<LearnHMMPosterior, MaximizeBayes>(MaximizeBayes(),
						    iteration_hmm,
						    ttable_source_target,
						    ttable_target_source,
						    atable_source_target,
						    atable_target_source,
						    classes_source,
						    classes_target,
						    aligned_source_target,
						    aligned_target_source);
	  else
	    learn<LearnHMM, MaximizeBayes>(MaximizeBayes(),
					   iteration_hmm,
					   ttable_source_target,
					   ttable_target_source,
					   atable_source_target,
					   atable_target_source,
					   classes_source,
					   classes_target,
					   aligned_source_target,
					   aligned_target_source);
	}

      } else if (pgd_mode) {
	if (symmetric_mode) {
	  if (posterior_mode)
	    learn<LearnHMMSymmetricPosterior, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
							  iteration_hmm,
							  ttable_source_target,
							  ttable_target_source,
							  atable_source_target,
							  atable_target_source,
							  classes_source,
							  classes_target,
							  aligned_source_target,
							  aligned_target_source);
	  else
	    learn<LearnHMMSymmetric, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
						 iteration_hmm,
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
	    learn<LearnHMMPosterior, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
						 iteration_hmm,
						 ttable_source_target,
						 ttable_target_source,
						 atable_source_target,
						 atable_target_source,
						 classes_source,
						 classes_target,
						 aligned_source_target,
						 aligned_target_source);
	  else
	    learn<LearnHMM, MaximizeL0>(MaximizeL0(l0_alpha, l0_beta),
					iteration_hmm,
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
	    learn<LearnHMMSymmetricPosterior, Maximize>(Maximize(),
							iteration_hmm,
							ttable_source_target,
							ttable_target_source,
							atable_source_target,
							atable_target_source,
							classes_source,
							classes_target,
							aligned_source_target,
							aligned_target_source);
	  else
	    learn<LearnHMMSymmetric, Maximize>(Maximize(),
					       iteration_hmm,
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
	    learn<LearnHMMPosterior, Maximize>(Maximize(),
					       iteration_hmm,
					       ttable_source_target,
					       ttable_target_source,
					       atable_source_target,
					       atable_target_source,
					       classes_source,
					       classes_target,
					       aligned_source_target,
					       aligned_target_source);
	  else
	    learn<LearnHMM, Maximize>(Maximize(),
				      iteration_hmm,
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
      if (itg_mode) {
	if (debug)
	  std::cerr << "ITG alignment" << std::endl;

	viterbi<ITGHMM>(ttable_source_target,
			ttable_target_source,
			atable_source_target,
			atable_target_source,
			classes_source,
			classes_target);
      } else if (max_match_mode) {
	if (debug)
	  std::cerr << "Max-Match alignment" << std::endl;
	
	viterbi<MaxMatchHMM>(ttable_source_target,
			     ttable_target_source,
			     atable_source_target,
			     atable_target_source,
			     classes_source,
			     classes_target);
      } else {
	if (debug)
	  std::cerr << "Viterbi alignment" << std::endl;
	
	viterbi<ViterbiHMM>(ttable_source_target,
			    ttable_target_source,
			    atable_source_target,
			    atable_target_source,
			    classes_source,
			    classes_target);
      }
    }

    // dependency parsing projection
    if (! projected_source_file.empty() || ! projected_target_file.empty()) {
      if (hybrid_mode) {
	if (debug)
	  std::cerr << "hybrid projective dependency" << std::endl;
	
	if (single_root_mode)
	  project_dependency<DependencyHybridSingleRootHMM>(ttable_source_target,
							    ttable_target_source,
							    atable_source_target,
							    atable_target_source,
							    classes_source,
							    classes_target);
	else
	  project_dependency<DependencyHybridHMM>(ttable_source_target,
						  ttable_target_source,
						  atable_source_target,
						  atable_target_source,
						  classes_source,
						  classes_target);

      } else if (degree2_mode) {
	if (debug)
	  std::cerr << "degree2 non-projective dependency" << std::endl;
	
	if (single_root_mode)
	  project_dependency<DependencyDegree2SingleRootHMM>(ttable_source_target,
							     ttable_target_source,
							     atable_source_target,
							     atable_target_source,
							     classes_source,
							     classes_target);
	else
	  project_dependency<DependencyDegree2HMM>(ttable_source_target,
						   ttable_target_source,
						   atable_source_target,
						   atable_target_source,
						   classes_source,
						   classes_target);
      } else if (mst_mode) {
	if (debug)
	  std::cerr << "MST non-projective dependency" << std::endl;
	
	if (single_root_mode)
	  project_dependency<DependencyMSTSingleRootHMM>(ttable_source_target,
							 ttable_target_source,
							 atable_source_target,
							 atable_target_source,
							 classes_source,
							 classes_target);
	else
	  project_dependency<DependencyMSTHMM>(ttable_source_target,
					       ttable_target_source,
					       atable_source_target,
					       atable_target_source,
					       classes_source,
					       classes_target);
      } else if (permutation_mode) {
	if (debug)
	  std::cerr << "permutation" << std::endl;
	
	project_dependency<PermutationHMM>(ttable_source_target,
					   ttable_target_source,
					   atable_source_target,
					   atable_target_source,
					   classes_source,
					   classes_target);
      } else
	throw std::runtime_error("no dependency algorithm?");
    }
    
    if (! posterior_source_target_file.empty() || ! posterior_target_source_file.empty() || ! posterior_combined_file.empty()) {
      if (debug)
	std::cerr << "compute posterior" << std::endl;
      
      posterior<PosteriorHMM>(ttable_source_target,
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

struct LearnMapReduce
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
  
  struct ttable_counts_type
  {
    word_type                      word;
    ttable_type::count_map_type    counts;
    aligned_type::aligned_map_type aligned;
    
    ttable_counts_type() : word(), counts(), aligned() {}
    
    void swap(ttable_counts_type& x)
    {
      word.swap(x.word);
      counts.swap(x.counts);
      aligned.swap(x.aligned);
    }
  };

  
  typedef utils::lockfree_list_queue<bitext_set_type, std::allocator<bitext_set_type> >       queue_bitext_type;
  typedef utils::lockfree_list_queue<ttable_counts_type, std::allocator<ttable_counts_type> > queue_ttable_type;
  typedef std::vector<queue_ttable_type, std::allocator<queue_ttable_type> >                  queue_ttable_set_type;
};

namespace std
{
  inline
  void swap(LearnMapReduce::ttable_counts_type& x, LearnMapReduce::ttable_counts_type& y)
  {
    x.swap(y);
  }
};

template <typename Maximizer>
struct LearnReducer : public Maximizer
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef LearnMapReduce map_reduce_type;
  
  typedef map_reduce_type::ttable_counts_type ttable_counts_type;
  typedef map_reduce_type::queue_ttable_type  queue_ttable_type;
  
  LearnReducer(queue_ttable_type& __queue,
	       const ttable_type& __ttable,
	       const aligned_type& __aligned,
	       ttable_type& __ttable_new,
	       aligned_type& __aligned_new,
	       const Maximizer& __base)
    : Maximizer(__base),
      queue(__queue),
      ttable(__ttable),
      aligned(__aligned),
      ttable_new(__ttable_new),
      aligned_new(__aligned_new) {}
  
  void operator()()
  {
    ttable_type  ttable_reduced;
    aligned_type aligned_reduced;
    
    for (;;) {
      ttable_counts_type counts;
      
      queue.pop_swap(counts);
      
      if (counts.counts.empty() && counts.aligned.empty()) break;
      
      if (! counts.counts.empty())
	ttable_reduced[counts.word] += counts.counts;
      if (! counts.aligned.empty())
	aligned_reduced[counts.word] += counts.aligned;
    }
    
    for (word_type::id_type word_id = 0; word_id < aligned_reduced.size(); ++ word_id)
      if (aligned_reduced.exists(word_id)) {
	aligned_new[word_id].swap(aligned_reduced[word_id]);
	
	aligned_reduced.clear(word_id);
      }

    ttable_type::count_map_type ttable_empty;
    
    for (word_type::id_type word_id = 0; word_id < ttable_reduced.size(); ++ word_id)
      if (ttable_reduced.exists(word_id)) { 
	if (ttable.exists(word_id))
	  Maximizer::operator()(ttable_reduced[word_id], ttable[word_id], ttable_new[word_id], ttable.prior, ttable.smooth);
	else
	  Maximizer::operator()(ttable_reduced[word_id], ttable_empty, ttable_new[word_id], ttable.prior, ttable.smooth);
	
	ttable_reduced.clear(word_id);
      }
  }
  
  queue_ttable_type& queue;
  
  const ttable_type&  ttable;
  const aligned_type& aligned;

  ttable_type&  ttable_new;
  aligned_type& aligned_new;
};

template <typename Learner>
struct LearnMapper : public Learner
{
  typedef LearnMapReduce map_reduce_type;
  
  typedef map_reduce_type::bitext_set_type    bitext_set_type;
  typedef map_reduce_type::ttable_counts_type ttable_counts_type;
  
  typedef map_reduce_type::queue_bitext_type      queue_bitext_type;
  typedef map_reduce_type::queue_ttable_type      queue_ttable_type;
  typedef map_reduce_type::queue_ttable_set_type  queue_ttable_set_type;
  
  LearnMapper(queue_bitext_type& __queue_bitext,
	      queue_ttable_set_type& __queue_ttable_source_target,
	      queue_ttable_set_type& __queue_ttable_target_source,
	      const LearnBase& __base)
    : Learner(__base),
      queue_bitext(__queue_bitext),
      queue_ttable_source_target(__queue_ttable_source_target),
      queue_ttable_target_source(__queue_ttable_target_source) {}
  
  void operator()()
  {
    Learner::initialize();

    bitext_set_type    bitexts;
    
    const int iter_mask = (1 << 5) - 1;
    
    for (int iter = 0;; ++ iter) {
      bitexts.clear();
      queue_bitext.pop_swap(bitexts);
      if (bitexts.empty()) break;
      
      typename bitext_set_type::const_iterator biter_end = bitexts.end();
      for (typename bitext_set_type::const_iterator biter = bitexts.begin(); biter != biter_end; ++ biter) {
	if (biter->alignment.empty())
	  Learner::operator()(biter->source, biter->target);
	else
	  Learner::operator()(biter->source, biter->target, biter->alignment);
      }

      if ((iter & iter_mask) == iter_mask)
	dump();
    }
    
    dump();
  }

  void dump()
  {
    const word_type::id_type source_max = utils::bithack::max(Learner::ttable_counts_source_target.size(),
							      Learner::aligned_source_target.size());
    const word_type::id_type target_max = utils::bithack::max(Learner::ttable_counts_target_source.size(),
							      Learner::aligned_target_source.size());
    
    for (word_type::id_type source_id = 0; source_id != source_max; ++ source_id) {
      ttable_counts_type counts;
	  
      if (Learner::ttable_counts_source_target.exists(source_id) && ! Learner::ttable_counts_source_target[source_id].empty())
	counts.counts.swap(Learner::ttable_counts_source_target[source_id]);
	  
      if (Learner::aligned_source_target.exists(source_id) && ! Learner::aligned_source_target[source_id].empty())
	counts.aligned.swap(Learner::aligned_source_target[source_id]);
	  
      if (! counts.counts.empty() || ! counts.aligned.empty()) {
	counts.word = word_type(source_id);
	
	queue_ttable_source_target[source_id % queue_ttable_source_target.size()].push_swap(counts);
      }
    }
	
    for (word_type::id_type target_id = 0; target_id != target_max; ++ target_id) {
      ttable_counts_type counts;
      
      if (Learner::ttable_counts_target_source.exists(target_id) && ! Learner::ttable_counts_target_source[target_id].empty())
	counts.counts.swap(Learner::ttable_counts_target_source[target_id]);
      
      if (Learner::aligned_target_source.exists(target_id) && ! Learner::aligned_target_source[target_id].empty())
	counts.aligned.swap(Learner::aligned_target_source[target_id]);
	  
      if (! counts.counts.empty() || ! counts.aligned.empty()) {
	counts.word = word_type(target_id);
	
	queue_ttable_target_source[target_id % queue_ttable_target_source.size()].push_swap(counts);
      }
    }
	
    Learner::ttable_counts_source_target.clear();
    Learner::ttable_counts_target_source.clear();
    Learner::aligned_source_target.clear();
    Learner::aligned_target_source.clear();
  }
  
  queue_bitext_type& queue_bitext;
  queue_ttable_set_type& queue_ttable_source_target;
  queue_ttable_set_type& queue_ttable_target_source;
  
  utils::hashmurmur<size_t> hasher;
};

template <typename TableSet, typename Table>
inline
void merge_tables(TableSet& tables, Table& merged)
{
  merged.clear();

  size_t size_max = 0;
  for (size_t i = 0; i != tables.size(); ++ i)
    size_max = utils::bithack::max(size_max, tables[i].size());

  merged.reserve(size_max);
  merged.resize(size_max);
  
  for (size_t i = 0; i != tables.size(); ++ i)
    for (word_type::id_type word_id = 0; word_id != tables[i].size(); ++ word_id)
      if (tables[i].exists(word_id))
	merged[word_type(word_id)].swap(tables[i][word_type(word_id)]);
}

template <typename Learner, typename Maximizer>
void learn(const Maximizer& maximizer,
	   const int iteration,
	   ttable_type& ttable_source_target,
	   ttable_type& ttable_target_source,
	   atable_type& atable_source_target,
	   atable_type& atable_target_source,
	   const classes_type& classes_source,
	   const classes_type& classes_target,
	   aligned_type& aligned_source_target,
	   aligned_type& aligned_target_source)
{
  typedef LearnMapReduce map_reduce_type;
  typedef LearnMapper<Learner> mapper_type;
  typedef LearnReducer<Maximizer> reducer_type;
  
  typedef map_reduce_type::bitext_type     bitext_type;
  typedef map_reduce_type::bitext_set_type bitext_set_type;
  
  typedef map_reduce_type::queue_bitext_type      queue_bitext_type;
  typedef map_reduce_type::queue_ttable_type      queue_ttable_type;
  typedef map_reduce_type::queue_ttable_set_type  queue_ttable_set_type;
  
  typedef std::vector<mapper_type, std::allocator<mapper_type> > mapper_set_type;
  
  queue_bitext_type queue_bitext(threads * 64);
  queue_ttable_set_type queue_ttable_source_target(utils::bithack::max(1, threads / 2));
  queue_ttable_set_type queue_ttable_target_source(utils::bithack::max(1, threads / 2));
  
  mapper_set_type mappers(threads, mapper_type(queue_bitext,
					       queue_ttable_source_target,
					       queue_ttable_target_source,
					       LearnBase(ttable_source_target, ttable_target_source,
							 atable_source_target, atable_target_source,
							 classes_source, classes_target)));
  
  for (int iter = 0; iter < iteration; ++ iter) {
    if (debug)
      std::cerr << "iteration: " << (iter + 1) << std::endl;
    
    utils::resource accumulate_start;

    std::vector<ttable_type, std::allocator<ttable_type> > ttable_source_target_new(threads, ttable_type(ttable_source_target.prior,
													 ttable_source_target.smooth));
    std::vector<ttable_type, std::allocator<ttable_type> > ttable_target_source_new(threads, ttable_type(ttable_target_source.prior,
													 ttable_target_source.smooth));
    
    std::vector<aligned_type, std::allocator<aligned_type> > aligned_source_target_new(threads);
    std::vector<aligned_type, std::allocator<aligned_type> > aligned_target_source_new(threads);
    
    boost::thread_group workers_mapper;
    boost::thread_group workers_reducer_source_target;
    boost::thread_group workers_reducer_target_source;
    
    for (size_t i = 0; i != mappers.size(); ++ i)
      workers_mapper.add_thread(new boost::thread(boost::ref(mappers[i])));
    
    for (size_t i = 0; i != queue_ttable_source_target.size(); ++ i)
      workers_reducer_source_target.add_thread(new boost::thread(reducer_type(queue_ttable_source_target[i],
									      ttable_source_target,
									      aligned_source_target,
									      ttable_source_target_new[i],
									      aligned_source_target_new[i],
									      maximizer)));
    for (size_t i = 0; i != queue_ttable_target_source.size(); ++ i)
      workers_reducer_target_source.add_thread(new boost::thread(reducer_type(queue_ttable_target_source[i],
									      ttable_target_source,
									      aligned_target_source,
									      ttable_target_source_new[i],
									      aligned_target_source_new[i],
									      maximizer)));
    
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
	queue_bitext.push_swap(bitexts);
	bitexts.clear();
      }
    }

    if (! bitexts.empty())
      queue_bitext.push_swap(bitexts);
    
    if (debug && num_bitext >= 10000)
      std::cerr << std::endl;
    if (debug)
      std::cerr << "# of bitexts: " << num_bitext << std::endl;

    if (is_src || is_trg || (is_align.get() && *is_align))
      throw std::runtime_error("# of samples do not match");
    
    for (size_t i = 0; i != mappers.size(); ++ i) {
      bitexts.clear();
      queue_bitext.push_swap(bitexts);
    }
    
    workers_mapper.join_all();
    
    // send termination to reducer by sending nulls
    for (size_t i = 0; i != queue_ttable_source_target.size(); ++ i)
      queue_ttable_source_target[i].push(queue_ttable_type::value_type());
    
    for (size_t i = 0; i != queue_ttable_target_source.size(); ++ i)
      queue_ttable_target_source[i].push(queue_ttable_type::value_type());
    
    double objective_source_target = 0;
    double objective_target_source = 0;
    
    for (size_t i = 0; i != mappers.size(); ++ i) {
      objective_source_target += mappers[i].objective_source_target;
      objective_target_source += mappers[i].objective_target_source;
    }
    
    if (debug)
      std::cerr << "log-likelihood for P(target | source): " << objective_source_target << '\n'
		<< "log-likelihood for P(source | target): " << objective_target_source << '\n';
    
    // merge atable counts... (we will dynamically create probability table!)
    atable_source_target.initialize();
    atable_target_source.initialize();
    for (size_t i = 0; i != mappers.size(); ++ i) {
      atable_source_target += mappers[i].atable_counts_source_target;
      atable_target_source += mappers[i].atable_counts_target_source;
    }
    
    atable_source_target.estimate_unk();
    atable_target_source.estimate_unk();
    
    for (size_t i = 0; i != mappers.size(); ++ i) {
      mappers[i].atable_source_target = atable_source_target;
      mappers[i].atable_target_source = atable_target_source;
    }
    
    workers_reducer_source_target.join_all();
    workers_reducer_target_source.join_all();
    
    // merge ttable and aligned...
    merge_tables(ttable_source_target_new, ttable_source_target);
    merge_tables(ttable_target_source_new, ttable_target_source);
    merge_tables(aligned_source_target_new, aligned_source_target);
    merge_tables(aligned_target_source_new, aligned_target_source);
    
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

    const int iter_mask = (1 << 12) - 1;
    
    for (int iter = 0;; ++ iter) {
      mapper.pop_swap(bitext);
      if (bitext.id == size_type(-1)) break;
      
      alignment_source_target.clear();
      alignment_target_source.clear();
      
      if (! bitext.source.empty() && ! bitext.target.empty())
	Aligner::operator()(bitext.source, bitext.target, bitext.span_source, bitext.span_target, alignment_source_target, alignment_target_source);
      
      reducer_source_target.push(bitext_type(bitext.id, bitext.source, bitext.target, alignment_source_target));
      reducer_target_source.push(bitext_type(bitext.id, bitext.target, bitext.source, alignment_target_source));

      if ((iter & iter_mask) == iter_mask)
	Aligner::shrink();
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

  typedef boost::shared_ptr<std::ostream> ostream_ptr_type;
  
  ostream_ptr_type os;
  queue_type& queue;
  
  ViterbiReducer(const path_type& path, queue_type& __queue) : os(), queue(__queue)
  {
    if (! path.empty()) {
      const bool flush_output = (path == "-"
				 || (boost::filesystem::exists(path)
				     && ! boost::filesystem::is_regular_file(path)));
      
      os.reset(new utils::compress_ostream(path, 1024 * 1024 * (! flush_output)));
    }
  }
  
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
    if (! os) {
      bitext_type bitext;
      for (;;) {
	queue.pop_swap(bitext);
	if (bitext.id == size_type(-1)) break;
      }
    } else {
      bitext_set_type bitexts;
      size_type id = 0;
      bitext_type bitext;
      for (;;) {
	queue.pop_swap(bitext);
	if (bitext.id == size_type(-1)) break;
	
	if (bitext.id == id) {
	  write(*os, bitext);
	  ++ id;
	} else
	  bitexts.insert(bitext);
	
	while (! bitexts.empty() && bitexts.begin()->id == id) {
	  write(*os, *bitexts.begin());
	  bitexts.erase(bitexts.begin());
	  ++ id;
	}
      }
      
      while (! bitexts.empty() && bitexts.begin()->id == id) {
	write(*os, *bitexts.begin());
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

  queue_type queue(threads * 4096);
  queue_type queue_source_target;
  queue_type queue_target_source;
  
  boost::thread_group reducer;
  reducer.add_thread(new boost::thread(reducer_type(viterbi_source_target_file, queue_source_target)));
  reducer.add_thread(new boost::thread(reducer_type(viterbi_target_source_file, queue_target_source)));

  boost::thread_group mapper;
  for (int i = 0; i != threads; ++ i)
    mapper.add_thread(new boost::thread(mapper_type(Aligner(ttable_source_target, ttable_target_source,
							    atable_source_target, atable_target_source,
							    classes_source, classes_target),
						    queue,
						    queue_source_target,
						    queue_target_source)));
    
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

    bitext.span_source.clear();
    bitext.span_target.clear();
    
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

struct ProjectionMapReduce
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  struct bitext_type
  {
    size_type       id;
    sentence_type   source;
    sentence_type   target;
    dependency_type dependency_source;
    dependency_type dependency_target;

    bitext_type() : id(size_type(-1)), source(), target(), dependency_source(), dependency_target() {}
    
    void clear()
    {
      id = size_type(-1);
      source.clear();
      target.clear();
      dependency_source.clear();
      dependency_target.clear();
    }
    
    void swap(bitext_type& x)
    {
      std::swap(id, x.id);
      source.swap(x.source);
      target.swap(x.target);
      dependency_source.swap(x.dependency_source);
      dependency_target.swap(x.dependency_target);
    }
  };
  
  struct projected_type
  {
    size_type id;
    dependency_type dependency;
    
    projected_type() : id(size_type(-1)), dependency() {}
    
    void clear()
    {
      id = size_type(-1);
      dependency.clear();
    }
    
    void swap(projected_type& x)
    {
      std::swap(id, x.id);
      dependency.swap(x.dependency);
    }
  };
  

  typedef utils::lockfree_list_queue<bitext_type, std::allocator<bitext_type> >       queue_mapper_type;
  typedef utils::lockfree_list_queue<projected_type, std::allocator<projected_type> > queue_reducer_type;
};

namespace std
{
  inline
  void swap(ProjectionMapReduce::bitext_type& x, ProjectionMapReduce::bitext_type& y)
  {
    x.swap(y);
  }
  
  inline
  void swap(ProjectionMapReduce::projected_type& x, ProjectionMapReduce::projected_type& y)
  {
    x.swap(y);
  }
};

template <typename Analyzer>
struct ProjectionMapper : public ProjectionMapReduce, public Analyzer
{
  queue_mapper_type& mapper;
  queue_reducer_type& reducer_source;
  queue_reducer_type& reducer_target;
  
  ProjectionMapper(const Analyzer& __analyzer,
		   queue_mapper_type& __mapper,
		   queue_reducer_type& __reducer_source,
		   queue_reducer_type& __reducer_target)
    : Analyzer(__analyzer),
      mapper(__mapper),
      reducer_source(__reducer_source),
      reducer_target(__reducer_target) {}
  
  void operator()()
  {
    bitext_type bitext;
    projected_type projected_source;
    projected_type projected_target;
    
    const int iter_mask = (1 << 12) - 1;
    
    for (int iter = 0; /**/; ++ iter) {
      mapper.pop_swap(bitext);
      if (bitext.id == size_type(-1)) break;

      projected_source.clear();
      projected_target.clear();
      
      if (! bitext.source.empty() && ! bitext.target.empty())
	Analyzer::operator()(bitext.source,
			     bitext.target,
			     bitext.dependency_source,
			     bitext.dependency_target,
			     projected_source.dependency,
			     projected_target.dependency);
      
      projected_source.id = bitext.id;
      projected_target.id = bitext.id;
      
      reducer_source.push_swap(projected_source);
      reducer_target.push_swap(projected_target);
      
      if ((iter & iter_mask) == iter_mask)
	Analyzer::shrink();
    }
  }
  
};

struct ProjectionReducer : public ProjectionMapReduce
{
  struct less_projected
  {
    bool operator()(const projected_type& x, const projected_type& y) const
    {
      return x.id < y.id;
    }
  };
  typedef std::set<projected_type, less_projected, std::allocator<projected_type> > projected_set_type;
  
  typedef boost::shared_ptr<std::ostream> ostream_ptr_type;
  
  ostream_ptr_type    os;
  queue_reducer_type& queue;
  
  ProjectionReducer(const path_type& path, queue_reducer_type& __queue) : os(), queue(__queue)
  {
    if (! path.empty()) {
      const bool flush_output = (path == "-"
				|| (boost::filesystem::exists(path)
				    && ! boost::filesystem::is_regular_file(path)));
      
      os.reset(new utils::compress_ostream(path, 1024 * 1024 * (! flush_output)));
    }
  }
  
  void operator()()
  {
    if (os) {
      projected_type projected;
      for (;;) {
	queue.pop_swap(projected);
	if (projected.id == size_type(-1)) break;
      }
    } else { 
      size_type id = 0;
      projected_type     projected;
      projected_set_type buffer;
      for (;;) {
	queue.pop_swap(projected);
	if (projected.id == size_type(-1)) break;
       
	if (projected.id == id) {
	  *os << projected.dependency << '\n';
	  ++ id;
	} else
	  buffer.insert(projected);
       
	while (! buffer.empty() && buffer.begin()->id == id) {
	  *os << buffer.begin()->dependency << '\n';
	  buffer.erase(buffer.begin());
	  ++ id;
	}
      }
     
      while (! buffer.empty() && buffer.begin()->id == id) {
	*os << buffer.begin()->dependency << '\n';
	buffer.erase(buffer.begin());
	++ id;
      }
     
      if (! buffer.empty())
	throw std::runtime_error("error while writing dependency output?");
    }
  }
};

template <typename Analyzer>
void project_dependency(const ttable_type& ttable_source_target,
			const ttable_type& ttable_target_source,
			const atable_type& atable_source_target,
			const atable_type& atable_target_source,
			const classes_type& classes_source,
			const classes_type& classes_target)
{
  typedef ProjectionMapper<Analyzer> mapper_type;
  typedef ProjectionReducer          reducer_type;
  
  typedef reducer_type::bitext_type    bitext_type;
  typedef reducer_type::projected_type projected_type;
  
  typedef reducer_type::queue_mapper_type  queue_mapper_type;
  typedef reducer_type::queue_reducer_type queue_reducer_type;

  typedef std::vector<mapper_type, std::allocator<mapper_type> > mapper_set_type;
  
  queue_mapper_type  queue(threads * 4096);
  queue_reducer_type queue_source;
  queue_reducer_type queue_target;
  
  boost::thread_group reducer;
  reducer.add_thread(new boost::thread(reducer_type(projected_source_file, queue_source)));
  reducer.add_thread(new boost::thread(reducer_type(projected_target_file, queue_target)));

  boost::thread_group mapper;
  for (int i = 0; i != threads; ++ i)
    mapper.add_thread(new boost::thread(mapper_type(Analyzer(ttable_source_target, ttable_target_source,
							     atable_source_target, atable_target_source,
							     classes_source, classes_target),
						    queue,
						    queue_source,
						    queue_target)));
    
  bitext_type bitext;
  bitext.id = 0;

  utils::resource projection_start;
  
  utils::compress_istream is_src(source_file, 1024 * 1024);
  utils::compress_istream is_trg(target_file, 1024 * 1024);
  
  std::auto_ptr<std::istream> is_dep_src(! dependency_source_file.empty()
					 ? new utils::compress_istream(dependency_source_file, 1024 * 1024) : 0);
  std::auto_ptr<std::istream> is_dep_trg(! dependency_target_file.empty()
					 ? new utils::compress_istream(dependency_target_file, 1024 * 1024) : 0);
  
  for (;;) {
    is_src >> bitext.source;
    is_trg >> bitext.target;
    
    bitext.dependency_source.clear();
    bitext.dependency_target.clear();

    if (is_dep_src.get())
      *is_dep_src >> bitext.dependency_source;
    if (is_dep_trg.get())
      *is_dep_trg >> bitext.dependency_target;
    
    if (! is_src || ! is_trg || (is_dep_src.get() && ! *is_dep_src) || (is_dep_trg.get() && ! *is_dep_trg)) break;
    
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

  if (is_src || is_trg || (is_dep_src.get() && *is_dep_src) || (is_dep_trg.get() && *is_dep_trg))
    throw std::runtime_error("# of samples do not match");
  
  for (int i = 0; i != threads; ++ i) {
    bitext.clear();
    queue.push_swap(bitext);
  }
  mapper.join_all();
  
  queue_source.push(projected_type());
  queue_target.push(projected_type());
  reducer.join_all();

  utils::resource projection_end;
  
  if (debug)
    std::cerr << "cpu time:  " << projection_end.cpu_time() - projection_start.cpu_time() << std::endl
	      << "user time: " << projection_end.user_time() - projection_start.user_time() << std::endl;
}


struct PosteriorMapReduce
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  typedef utils::vector2<double, std::allocator<double> > matrix_type;
  
  struct bitext_type
  {
    size_type       id;
    sentence_type   source;
    sentence_type   target;

    bitext_type() : id(size_type(-1)), source(), target() {}
    
    void clear()
    {
      id = size_type(-1);
      source.clear();
      target.clear();
    }
    
    void swap(bitext_type& x)
    {
      std::swap(id, x.id);
      source.swap(x.source);
      target.swap(x.target);
    }
  };
  
  struct posterior_type
  {
    size_type id;
    matrix_type matrix;
    
    posterior_type() : id(size_type(-1)), matrix() {}
    
    void clear()
    {
      id = size_type(-1);
      matrix.clear();
    }
    
    void swap(posterior_type& x)
    {
      std::swap(id, x.id);
      matrix.swap(x.matrix);
    }
  };
  
  typedef utils::lockfree_list_queue<bitext_type, std::allocator<bitext_type> >       queue_mapper_type;
  typedef utils::lockfree_list_queue<posterior_type, std::allocator<posterior_type> > queue_reducer_type;
};

namespace std
{
  inline
  void swap(PosteriorMapReduce::bitext_type& x, PosteriorMapReduce::bitext_type& y)
  {
    x.swap(y);
  }
  
  inline
  void swap(PosteriorMapReduce::posterior_type& x, PosteriorMapReduce::posterior_type& y)
  {
    x.swap(y);
  }
};

template <typename Infer>
struct PosteriorMapper : public PosteriorMapReduce, public Infer
{
  queue_mapper_type&  mapper;
  queue_reducer_type& reducer_source_target;
  queue_reducer_type& reducer_target_source;
  queue_reducer_type& reducer_combined;
  
  PosteriorMapper(const Infer& __infer,
		  queue_mapper_type& __mapper,
		  queue_reducer_type& __reducer_source_target,
		  queue_reducer_type& __reducer_target_source,
		  queue_reducer_type& __reducer_combined)
    : Infer(__infer),
      mapper(__mapper),
      reducer_source_target(__reducer_source_target),
      reducer_target_source(__reducer_target_source),
      reducer_combined(__reducer_combined)
  {}
  
  void operator()()
  {
    bitext_type    bitext;
    posterior_type posterior_source_target;
    posterior_type posterior_target_source;
    posterior_type posterior_combined;
    
    const int iter_mask = (1 << 12) - 1;
    
    for (int iter = 0; /**/; ++ iter) {
      mapper.pop_swap(bitext);
      if (bitext.id == size_type(-1)) break;
      
      posterior_source_target.clear();
      posterior_target_source.clear();
      posterior_combined.clear();
      
      if (! bitext.source.empty() && ! bitext.target.empty()) {
	
	Infer::operator()(bitext.source, bitext.target, posterior_source_target.matrix, posterior_target_source.matrix);
	
	// merging...
	const size_type source_size = bitext.source.size();
	const size_type target_size = bitext.target.size();
	
	posterior_combined.matrix.resize(target_size + 1, source_size + 1);
	
	for (size_type src = 1; src <= source_size; ++ src)
	  for (size_type trg = 1; trg <= target_size; ++ trg)
	    posterior_combined.matrix(trg, src) = utils::mathop::sqrt(posterior_source_target.matrix(trg, src) * posterior_target_source.matrix(src, trg));
	
	for (size_type trg = 1; trg <= target_size; ++ trg)
	  posterior_combined.matrix(trg, 0) = posterior_source_target.matrix(trg, 0);
	
	for (size_type src = 1; src <= source_size; ++ src)
	  posterior_combined.matrix(0, src) = posterior_target_source.matrix(src, 0);
      }

      posterior_source_target.id = bitext.id;
      posterior_target_source.id = bitext.id;
      posterior_combined.id      = bitext.id;
      
      reducer_source_target.push_swap(posterior_source_target);
      reducer_target_source.push_swap(posterior_target_source);
      reducer_combined.push_swap(posterior_combined);
      
      if ((iter & iter_mask) == iter_mask)
	Infer::shrink();
    }
  }
};

struct PosteriorReducer : public PosteriorMapReduce
{
  struct less_posterior
  {
    bool operator()(const posterior_type& x, const posterior_type& y) const
    {
      return x.id < y.id;
    }
  };
  typedef std::set<posterior_type, less_posterior, std::allocator<posterior_type> > posterior_set_type;

  typedef boost::shared_ptr<std::ostream> ostream_ptr_type;
  
  ostream_ptr_type os;
  queue_reducer_type& queue;
  
  PosteriorReducer(const path_type& path, queue_reducer_type& __queue) : os(), queue(__queue)
  {
    if (! path.empty()) {
      const bool flush_output = (path == "-"
				 || (boost::filesystem::exists(path)
				     && ! boost::filesystem::is_regular_file(path)));
      
      os.reset(new utils::compress_ostream(path, 1024 * 1024 * (! flush_output)));
      os->precision(20);
    }
  }
  
  void operator()() throw()
  {
    if (! os) {
      posterior_type posterior;
      for (;;) {
	queue.pop_swap(posterior);
	if (posterior.id == size_type(-1)) break;
      }
    } else {
      posterior_set_type posteriors;
      size_type      id = 0;
      posterior_type posterior;
      
      for (;;) {
	queue.pop_swap(posterior);
	if (posterior.id == size_type(-1)) break;

	if (posterior.id == id) {
	  write(*os, posterior);
	  ++ id;
	} else
	  posteriors.insert(posterior);
	
	while (! posteriors.empty() && posteriors.begin()->id == id) {
	  write(*os, *posteriors.begin());
	  posteriors.erase(posteriors.begin());
	  ++ id;
	}
      }
      
      while (! posteriors.empty() && posteriors.begin()->id == id) {
	write(*os, *posteriors.begin());
	posteriors.erase(posteriors.begin());
	++ id;
      }
      
      if (! posteriors.empty())
	throw std::runtime_error("error while writeing posterior output?");
    }
  }
  
  void write(std::ostream& os, const posterior_type& posterior)
  {
    const matrix_type& matrix = posterior.matrix;
    
    if (matrix.empty())
      os << '\n';
    else {
      os << '(';
      for (size_type i = 0; i != matrix.size1(); ++ i) {
	if (i)
	  os << ", ";
	os << '(';
	matrix_type::const_iterator iter_begin = matrix.begin(i);
	matrix_type::const_iterator iter_end   = matrix.end(i);
	if (iter_begin != iter_end) {
	  std::copy(iter_begin, iter_end - 1, std::ostream_iterator<double>(os, ", "));
	  os << *(iter_end - 1);
	}
	os << ')';
      }
      os << ')' << '\n';
    }
  }
};

template <typename Infer>
void posterior(const ttable_type& ttable_source_target,
	       const ttable_type& ttable_target_source,
	       const atable_type& atable_source_target,
	       const atable_type& atable_target_source,
	       const classes_type& classes_source,
	       const classes_type& classes_target)
{
  typedef PosteriorMapper<Infer> mapper_type;
  typedef PosteriorReducer       reducer_type;
  
  typedef reducer_type::bitext_type    bitext_type;
  typedef reducer_type::posterior_type posterior_type;
  
  typedef reducer_type::queue_mapper_type  queue_mapper_type;
  typedef reducer_type::queue_reducer_type queue_reducer_type;
  
  typedef std::vector<mapper_type, std::allocator<mapper_type> > mapper_set_type;
  
  queue_mapper_type  queue(threads * 4096);
  queue_reducer_type queue_source_target;
  queue_reducer_type queue_target_source;
  queue_reducer_type queue_combined;
  
  boost::thread_group reducer;
  reducer.add_thread(new boost::thread(reducer_type(posterior_source_target_file, queue_source_target)));
  reducer.add_thread(new boost::thread(reducer_type(posterior_target_source_file, queue_target_source)));
  reducer.add_thread(new boost::thread(reducer_type(posterior_combined_file,      queue_combined)));

  boost::thread_group mapper;
  for (int i = 0; i != threads; ++ i)
    mapper.add_thread(new boost::thread(mapper_type(Infer(ttable_source_target, ttable_target_source,
							  atable_source_target, atable_target_source,
							  classes_source, classes_target),
						    queue,
						    queue_source_target,
						    queue_target_source,
						    queue_combined)));  
  
  bitext_type bitext;
  bitext.id = 0;
  
  utils::resource posterior_start;
  
  utils::compress_istream is_src(source_file, 1024 * 1024);
  utils::compress_istream is_trg(target_file, 1024 * 1024);
  
  for (;;) {
    is_src >> bitext.source;
    is_trg >> bitext.target;
    
    if (! is_src || ! is_trg) break;
    
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

  if (is_src || is_trg)
    throw std::runtime_error("# of samples do not match");
  
  for (int i = 0; i != threads; ++ i) {
    bitext.clear();
    queue.push_swap(bitext);
  }
  mapper.join_all();
  
  queue_source_target.push(posterior_type());
  queue_target_source.push(posterior_type());
  queue_combined.push(posterior_type());

  reducer.join_all();

  utils::resource posterior_end;
  
  if (debug)
    std::cerr << "cpu time:  " << posterior_end.cpu_time() - posterior_start.cpu_time() << std::endl
	      << "user time: " << posterior_end.user_time() - posterior_start.user_time() << std::endl;
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
    
    ("dependency-source", po::value<path_type>(&dependency_source_file), "source dependency file")
    ("dependency-target", po::value<path_type>(&dependency_target_file), "target dependency file")
    
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

    ("projected-source", po::value<path_type>(&projected_source_file), "source dependnecy projected from target")
    ("projected-target", po::value<path_type>(&projected_target_file), "target dependency projected from source")
    
    ("posterior-source-target", po::value<path_type>(&posterior_source_target_file), "posterior for P(target | source)")
    ("posterior-target-source", po::value<path_type>(&posterior_target_source_file), "posterior for P(source | target)")
    ("posterior-combined",      po::value<path_type>(&posterior_combined_file),      "posterior for P(source | target) P(target | source)")

    ("iteration-model1", po::value<int>(&iteration_model1)->default_value(iteration_model1), "max Model1 iteration")
    ("iteration-hmm", po::value<int>(&iteration_hmm)->default_value(iteration_hmm), "max HMM iteration")
    
    ("symmetric",  po::bool_switch(&symmetric_mode),  "symmetric training")
    ("posterior",  po::bool_switch(&posterior_mode),  "posterior constrained training")
    ("variational-bayes", po::bool_switch(&variational_bayes_mode), "variational Bayes estimates")
    ("pgd",               po::bool_switch(&pgd_mode),               "projected gradient descent")
    
    ("itg",       po::bool_switch(&itg_mode),       "ITG alignment")
    ("max-match", po::bool_switch(&max_match_mode), "maximum matching alignment")
    ("moses",     po::bool_switch(&moses_mode),     "Moses alignment foramt")

    ("permutation", po::bool_switch(&permutation_mode), "permutation")
    ("hybrid",      po::bool_switch(&hybrid_mode),      "hybrid projective dependency parsing")
    ("degree2",     po::bool_switch(&degree2_mode),     "degree2 non-projective dependency parsing")
    ("mst",         po::bool_switch(&mst_mode),         "MST non-projective dependency parsing")
    ("single-root", po::bool_switch(&single_root_mode), "single root dependency")

    ("p0",             po::value<double>(&p0)->default_value(p0),                               "parameter for NULL alignment")
    ("prior-lexicon",  po::value<double>(&prior_lexicon)->default_value(prior_lexicon),         "Dirichlet prior for variational Bayes")
    ("smooth-lexicon", po::value<double>(&smooth_lexicon)->default_value(smooth_lexicon),       "smoothing parameter for uniform distribution")
    ("prior-alignment",  po::value<double>(&prior_alignment)->default_value(prior_alignment),   "Dirichlet prior for variational Bayes")
    ("smooth-alignment", po::value<double>(&smooth_alignment)->default_value(smooth_alignment), "smoothing parameter for uniform distribution")
    
    ("l0-alpha", po::value<double>(&l0_alpha)->default_value(l0_alpha), "L0 regularization")
    ("l0-beta",  po::value<double>(&l0_beta)->default_value(l0_beta),   "L0 regularization")
    
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

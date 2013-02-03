//
//  Copyright(C) 2011-2012 Taro Watanabe <taro.watanabe@nict.go.jp>
//

//
// learn latent annotation grammar from treebank
//
// First, we will implement via EM-algorithm
// 1. read all the parse tree
// 2. left-binarization (or, all/right binarization...?)
// 3. initialize table by maximum-likelihood estimates...
// 5. Iterate
//    6.  Split
//    7.  EM-iterations
//    8.  Merge
//    9.  EM-iterations
//
//
// TODO: construct dedicated forest structure....
// 

#include <stdexcept>
#include <vector>
#include <deque>
#include <numeric>

#include <cicada/vocab.hpp>
#include <cicada/hypergraph.hpp>
#include <cicada/inside_outside.hpp>
#include <cicada/semiring.hpp>
#include <cicada/sort.hpp>
#include <cicada/binarize.hpp>
#include <cicada/signature.hpp>
#include <cicada/tokenizer.hpp>
#include <cicada/sentence.hpp>

#include <boost/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/random.hpp>
#include <boost/xpressive/xpressive.hpp>
#include <boost/math/special_functions/expm1.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/array.hpp>

#include <utils/bithack.hpp>
#include <utils/hashmurmur3.hpp>
#include <utils/unordered_map.hpp>
#include <utils/compress_stream.hpp>
#include <utils/resource.hpp>
#include <utils/mathop.hpp>
#include <utils/lexical_cast.hpp>
#include <utils/lockfree_list_queue.hpp>
#include <utils/array_power2.hpp>
#include <utils/chunk_vector.hpp>
#include <utils/vertical_coded_vector.hpp>
#include <utils/packed_vector.hpp>
#include <utils/random_seed.hpp>
#include <utils/compact_map.hpp>
#include <utils/compact_set.hpp>

typedef cicada::HyperGraph hypergraph_type;
typedef hypergraph_type::rule_type     rule_type;
typedef hypergraph_type::rule_ptr_type rule_ptr_type;

typedef rule_type::symbol_type     symbol_type;
typedef rule_type::symbol_set_type symbol_set_type;

typedef hypergraph_type::feature_set_type   feature_set_type;
typedef hypergraph_type::attribute_set_type attribute_set_type;

typedef feature_set_type::feature_type     feature_type;
typedef attribute_set_type::attribute_type attribute_type;

typedef cicada::Signature signature_type;
typedef cicada::Tokenizer tokenizer_type;

typedef boost::filesystem::path path_type;
typedef std::vector<path_type, std::allocator<path_type> > path_set_type;

class Treebank
{
public:
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  typedef hypergraph_type::id_type id_type;
  
  typedef std::vector<symbol_type, std::allocator<symbol_type> > label_set_type;
  typedef std::vector<size_type, std::allocator<size_type> > offset_set_type;

  hypergraph_type treebank;
  label_set_type  labels;
  offset_set_type offsets;
  
  Treebank(const hypergraph_type& __treebank)
    : treebank(__treebank),
      labels(__treebank.nodes.size()),
      offsets(__treebank.nodes.size() + 1)
  {
    hypergraph_type::node_set_type::const_iterator niter_end = treebank.nodes.end();
    for (hypergraph_type::node_set_type::const_iterator niter = treebank.nodes.begin(); niter != niter_end; ++ niter) {
      const hypergraph_type::node_type& node = *niter;
      const hypergraph_type::edge_type& edge = treebank.edges[node.edges.front()];
      
      labels[node.id] = edge.rule->lhs;
      offsets[node.id] = node.id;
    }
    offsets.back() = treebank.nodes.size();
  }
};

typedef Treebank treebank_type;

typedef std::deque<treebank_type, std::allocator<treebank_type> > treebank_set_type;

template <typename Tp>
struct ptr_hash : public boost::hash<Tp>
{
  typedef boost::hash<Tp> hasher_type;

  size_t operator()(const Tp* x) const
  {
    return (x ? hasher_type::operator()(*x) : size_t(0));
  }
  
  size_t operator()(const boost::shared_ptr<Tp>& x) const
  {
    return (x ? hasher_type::operator()(*x) : size_t(0));
  }
};

template <typename Tp>
struct ptr_equal
{
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x == y || (x && y && *x == *y);
  }
  
  bool operator()(const boost::shared_ptr<Tp>& x, const boost::shared_ptr<Tp>& y) const
  {
    return x == y || (x && y && *x == *y);
  }
};

struct rule_ptr_unassigned
{
  rule_ptr_type operator()() const { return rule_ptr_type(); }
};


typedef cicada::semiring::Logprob<double> weight_type;

class Grammar : public utils::compact_map<rule_ptr_type, weight_type,
					  rule_ptr_unassigned, rule_ptr_unassigned,
					  ptr_hash<rule_type>, ptr_equal<rule_type>,
					  std::allocator<std::pair<const rule_ptr_type, weight_type> > >
{
public:
  typedef utils::compact_map<rule_ptr_type, weight_type,
			     rule_ptr_unassigned, rule_ptr_unassigned,
			     ptr_hash<rule_type>, ptr_equal<rule_type>,
			     std::allocator<std::pair<const rule_ptr_type, weight_type> > > count_set_type;
  
public:
  Grammar() : count_set_type() { }

  Grammar& operator+=(const Grammar& x)
  {
    count_set_type::const_iterator iter_end = x.end();
    for (count_set_type::const_iterator iter = x.begin(); iter != iter_end; ++ iter)
      count_set_type::operator[](iter->first) += iter->second;
    
    return *this;
  }
};

typedef Grammar grammar_type;

typedef utils::unordered_map<symbol_type, grammar_type, boost::hash<symbol_type>, std::equal_to<symbol_type>,
			     std::allocator<std::pair<const symbol_type, grammar_type> > >::type count_set_type;

typedef symbol_set_type ngram_type;

struct ngram_unassigned
{
  ngram_type operator()() const { return ngram_type(); }
};

class NGramCounts : public utils::compact_map<ngram_type, weight_type,
					      ngram_unassigned, ngram_unassigned,
					      boost::hash<ngram_type>, std::equal_to<ngram_type>,
					      std::allocator<std::pair<const ngram_type, weight_type> > >
{
public:
  typedef utils::compact_map<ngram_type, weight_type,
			     ngram_unassigned, ngram_unassigned,
			     boost::hash<ngram_type>, std::equal_to<ngram_type>,
			     std::allocator<std::pair<const ngram_type, weight_type> > > count_set_type;
  
  NGramCounts() : count_set_type() {  }

  NGramCounts& operator+=(const NGramCounts& x)
  {
    count_set_type::const_iterator iter_end = x.end();
    for (count_set_type::const_iterator iter = x.begin(); iter != iter_end; ++ iter)
      count_set_type::operator[](iter->first) += iter->second;
    
    return *this;
  }
};
typedef NGramCounts ngram_count_set_type;

class WordCounts : public utils::compact_map<symbol_type, weight_type,
					     utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
					     boost::hash<symbol_type>, std::equal_to<symbol_type>,
					     std::allocator<std::pair<const symbol_type, weight_type> > >
{
public:
  typedef utils::compact_map<symbol_type, weight_type,
			     utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			     boost::hash<symbol_type>, std::equal_to<symbol_type>,
			     std::allocator<std::pair<const symbol_type, weight_type> > > count_set_type;
  
  WordCounts() : count_set_type() { }

  WordCounts& operator+=(const WordCounts& x)
  {
    count_set_type::const_iterator iter_end = x.end();
    for (count_set_type::const_iterator iter = x.begin(); iter != iter_end; ++ iter)
      count_set_type::operator[](iter->first) += iter->second;
    
    return *this;
  }
  
  WordCounts& operator*=(const WordCounts& x)
  {
    count_set_type::const_iterator iter_end = x.end();
    for (count_set_type::const_iterator iter = x.begin(); iter != iter_end; ++ iter) {
      std::pair<count_set_type::iterator, bool> result = insert(*iter);
      if (! result.second)
	result.first->second *= iter->second;
    }
    
    return *this;
  }
  
};
typedef WordCounts word_count_set_type;
typedef WordCounts label_count_set_type;

path_set_type input_files;
path_type     output_grammar_file = "-";
path_type     output_lexicon_file;
path_type     output_pos_file;
path_type     output_character_file;

symbol_type goal = "[ROOT]";

int max_iteration = 6;         // max split-merge iterations
int max_iteration_split = 50;  // max EM-iterations for split
int max_iteration_merge = 20;  // max EM-iterations for merge
int min_iteration_split = 30;  // max EM-iterations for split
int min_iteration_merge = 15;  // max EM-iterations for merge

bool binarize_left = false;
bool binarize_right = false;
bool binarize_all = false;

double prior_rule      = 0.01;
double prior_lexicon   = 0.01;
double prior_unknown   = 0.01;
double prior_signature = 0.01;
double prior_character = 0.01;

double merge_ratio = 0.5;
double unknown_ratio = 0.5;
double unknown_threshold = 20;

std::string signature = "";
bool signature_list = false;

double cutoff_rule = 1e-30;
double cutoff_lexicon = 1e-40;
double cutoff_character = 0;

int threads = 1;

int debug = 0;

template <typename Generator, typename Maximizer>
void grammar_merge(treebank_set_type& treebanks,
		   label_count_set_type& labels,
		   grammar_type& grammar,
		   const int bits,
		   Generator& generator,
		   Maximizer maximizer);

template <typename Generator, typename Maximizer>
void grammar_split(treebank_set_type& treebanks,
		   label_count_set_type& labels,
		   grammar_type& grammar,
		   const int bits,
		   Generator& generator,
		   Maximizer maximizer);

template <typename Function, typename Maximizer>
double grammar_learn(const treebank_set_type& treebanks,
		     label_count_set_type& labels,
		     grammar_type& grammar,
		     Function function,
		     Maximizer maximier);

template <typename Function>
void lexicon_learn(const treebank_set_type& treebanks,
		   grammar_type& lexicon,
		   Function function);

template <typename Function>
void characters_learn(const treebank_set_type& treebanks,
		      ngram_count_set_type& model,
		      ngram_count_set_type& backoff,
		      Function function);
void pos_learn(const label_count_set_type& labels,
	       grammar_type& grammar_pos);

template <typename Maximizer>
void grammar_maximize(const count_set_type& counts,
		      grammar_type& grammar,
		      Maximizer maximizer);

void write_characters(const path_type& file,
		      const ngram_count_set_type& model,
		      const ngram_count_set_type& backoff,
		      const double cutoff);
void write_grammar(const path_type& file,
		   const grammar_type& grammar);

void read_treebank(const path_set_type& files,
		   treebank_set_type& treebanks);

void grammar_prune(grammar_type& grammar, const double cutoff);
void lexicon_prune(grammar_type& grammar, const double cutoff);

void options(int argc, char** argv);

struct zero_function
{
  typedef weight_type value_type;
  
  weight_type operator()(const rule_ptr_type& rule) const
  {
    return cicada::semiring::traits<weight_type>::exp(0.0);
  }
};

struct weight_function
{
  typedef weight_type value_type;

  weight_function(const grammar_type& __grammar) : grammar(__grammar) {}
  
  weight_type operator()(const rule_ptr_type& rule) const
  {
    grammar_type::const_iterator giter = grammar.find(rule);
    if (giter == grammar.end())
      throw std::runtime_error("invalid rule: " + utils::lexical_cast<std::string>(*rule));
    
    return giter->second;
  }
  
  const grammar_type& grammar;
};

struct Maximize
{
  void operator()(const grammar_type& counts, grammar_type& grammar) const
  {
    // simle maximizer...
    weight_type sum;
    grammar_type::const_iterator citer_end = counts.end();
    for (grammar_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer)
      sum += citer->second;
    
    for (grammar_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer)
      grammar[citer->first] = citer->second / sum;
  }
};

struct MaximizeBayes : public utils::hashmurmur3<size_t>
{
  typedef utils::hashmurmur3<size_t> hasher_type;
  
  MaximizeBayes(const grammar_type& __base) : base(__base) {}

  typedef std::vector<weight_type, std::allocator<weight_type> > logprob_set_type;
  typedef std::vector<rule_ptr_type, std::allocator<rule_ptr_type> > rule_ptr_set_type;

  typedef utils::compact_map<rule_ptr_type, int,
			     rule_ptr_unassigned, rule_ptr_unassigned,
			     ptr_hash<rule_type>, ptr_equal<rule_type>,
			     std::allocator<std::pair<const rule_ptr_type, int> > > rule_count_set_type;
  
  
  logprob_set_type  __logprobs;
  rule_ptr_set_type __rules;
  const grammar_type& base;
  
  void operator()(const grammar_type& counts, grammar_type& grammar) const
  {
    using namespace boost::math::policies;
    typedef policy<domain_error<errno_on_error>,
      pole_error<errno_on_error>,
      overflow_error<errno_on_error>,
      rounding_error<errno_on_error>,
      evaluation_error<errno_on_error> > policy_type;
    
    if (counts.empty()) return;
    
    logprob_set_type& logprobs = const_cast<logprob_set_type&>(__logprobs);
    rule_ptr_set_type& rules = const_cast<rule_ptr_set_type&>(__rules);
    
    logprobs.resize(counts.size());
    rules.resize(counts.size());

    rule_count_set_type rule_counts;

    weight_type sum;
        
    logprob_set_type::iterator piter = logprobs.begin();
    rule_ptr_set_type::iterator riter = rules.begin();
    grammar_type::const_iterator citer_end = counts.end();
    for (grammar_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer, ++ piter, ++ riter) {
      const symbol_type lhs = citer->first->lhs.coarse();
      
      symbol_set_type rhs(citer->first->rhs);
      symbol_set_type::iterator siter_end = rhs.end();
      for (symbol_set_type::iterator siter = rhs.begin(); siter != siter_end; ++ siter)
	*siter = siter->coarse();
      
      const rule_ptr_type rule_coarse(rule_type::create(rule_type(lhs, rhs)));
      
      grammar_type::const_iterator biter = base.find(rule_coarse);
      if (biter == base.end())
	throw std::runtime_error("no base?");
      
      *piter = biter->second;
      *riter = biter->first;
      
      sum += citer->second;
      ++ rule_counts[biter->first];
    }
    
    weight_type logprob_sum;
    
    logprob_set_type::iterator piter_end = logprobs.end();
    riter = rules.begin();
    for (logprob_set_type::iterator piter = logprobs.begin(); piter != piter_end; ++ piter, ++ riter) {
      *piter /= weight_type(static_cast<double>(rule_counts.find(*riter)->second));
      
      logprob_sum += *piter;
    }
  
    const bool is_terminal = counts.begin()->first->rhs.front().is_terminal();
    const double prior = (is_terminal ? prior_lexicon : prior_rule) * counts.size();
    const weight_type logprior(prior);
  
    double total = 0.0;
    for (logprob_set_type::iterator piter = logprobs.begin(); piter != piter_end; ++ piter)
      sum += logprior * ((*piter) / logprob_sum);
    total = sum;
    
    for (;;) {
      weight_type sum;
      //const weight_type logtotal(total);
      const double logtotal = utils::mathop::digamma(total);
      
      logprob_set_type::iterator piter = logprobs.begin();
      for (grammar_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer, ++ piter) {
	const double logprob = utils::mathop::digamma(static_cast<double>(citer->second + logprior * (*piter) / logprob_sum)) - logtotal;
	
	grammar[citer->first] = cicada::semiring::traits<weight_type>::exp(logprob);
	sum += cicada::semiring::traits<weight_type>::exp(logprob);
	
	//const weight_type logprob = (citer->second + logprior * (*piter)) / logtotal;
	
	//grammar[citer->first] = logprob;
	//sum += logprob;
      }
      
      const double discount = - boost::math::expm1(cicada::semiring::log(sum), policy_type());
      if (discount > 0.0) break;
      
      ++ total;
    }
  }
};

int main(int argc, char** argv)
{
  try {
    options(argc, argv);
    
    if (signature_list) {
      std::cout << signature_type::lists();
      return 0;
    }
    
    if (merge_ratio <= 0.0 || 1.0 <= merge_ratio)
      throw std::runtime_error("invalid merge ratio");
      
    if (int(binarize_left) + binarize_right + binarize_all > 1)
      throw std::runtime_error("specify either binarize-{left,right,all}");

    const signature_type* sig = (! signature.empty() ? &signature_type::create(signature) : 0);
    
    if (! output_character_file.empty()) {
      if (! sig)
	throw std::runtime_error("character estimation requires signature");
      
      if (output_lexicon_file.empty())
	throw std::runtime_error("we will dump character file, but no lexicon file");
    }
    
    min_iteration_split = utils::bithack::min(min_iteration_split, max_iteration_split);
    min_iteration_merge = utils::bithack::min(min_iteration_merge, max_iteration_merge);
    
    if (int(binarize_left) + binarize_right + binarize_all == 0)
      binarize_left = true;

    if (input_files.empty())
      input_files.push_back("-");
    
    threads = utils::bithack::max(threads, 1);
    
    treebank_set_type treebanks;
    read_treebank(input_files, treebanks);
    
    label_count_set_type labels;
    grammar_type grammar;
    grammar_learn(treebanks, labels, grammar, zero_function(), Maximize());
    
    boost::mt19937 generator;
    generator.seed(utils::random_seed());
    
    if (debug)
      std::cerr << "grammar size: " << grammar.size() << std::endl;
    
    grammar_type base(grammar);
    
    for (int iter = 0; iter < max_iteration; ++ iter) {
      
      if (debug)
	std::cerr << "iteration: " << (iter + 1) << std::endl;
      
      // split...
      {
	// for splitting, we will simply compute by maximization...
	const utils::resource split_start;
	grammar_split(treebanks, labels, grammar, iter, generator, MaximizeBayes(base));
	const utils::resource split_end;
	
	if (debug)
	  std::cerr << "split: " << "grammar size: " << grammar.size() << std::endl;

	if (debug)
	  std::cerr << "cpu time: " << (split_end.cpu_time() - split_start.cpu_time())
		    << " user time: " << (split_end.user_time() - split_start.user_time())
		    << std::endl;
	
	double logprob = 0.0;
	for (int i = 0; i < max_iteration_split; ++ i) {
	  if (debug)
	    std::cerr << "split iteration: " << (i + 1) << std::endl;
	  
	  const utils::resource learn_start;
	  const double logprob_curr = grammar_learn(treebanks, labels, grammar, weight_function(grammar), MaximizeBayes(base));
	  const utils::resource learn_end;

	  if (debug)
	    std::cerr << "cpu time: " << (learn_end.cpu_time() - learn_start.cpu_time())
		      << " user time: " << (learn_end.user_time() - learn_start.user_time())
		      << std::endl;
	  
	  if (i >= min_iteration_split && logprob_curr < logprob) break;
	  
	  logprob = logprob_curr;
	}
      }
      
      // merge..
      {
	const utils::resource merge_start;
	grammar_merge(treebanks, labels, grammar, iter, generator, MaximizeBayes(base));
	const utils::resource merge_end;
	
	if (debug)
	  std::cerr << "merge: " << "grammar size: " << grammar.size() << std::endl;

	if (debug)
	  std::cerr << "cpu time: " << (merge_end.cpu_time() - merge_start.cpu_time())
		    << " user time: " << (merge_end.user_time() - merge_start.user_time())
		    << std::endl;
	
	double logprob = 0.0;
	for (int i = 0; i < max_iteration_merge; ++ i) {
	  if (debug)
	    std::cerr << "merge iteration: " << (i + 1) << std::endl;
	  
	  const utils::resource learn_start;
	  const double logprob_curr = grammar_learn(treebanks, labels, grammar, weight_function(grammar), MaximizeBayes(base));
	  const utils::resource learn_end;
	  
	  if (debug)
	    std::cerr << "cpu time: " << (learn_end.cpu_time() - learn_start.cpu_time())
		      << " user time: " << (learn_end.user_time() - learn_start.user_time())
		      << std::endl;
	  
	  if (i >= min_iteration_merge && logprob_curr < logprob) break;
	  
	  logprob = logprob_curr;
	}
      }
    }

    if (! output_lexicon_file.empty()) {
      // first, split into two
      grammar_type rules;
      grammar_type lexicon;
      
      grammar_type::const_iterator giter_end = grammar.end();
      for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter) {
	if (giter->first->rhs.size() == 1 && giter->first->rhs.front().is_terminal())
	  lexicon.insert(*giter);
	else
	  rules.insert(*giter);
      }

      
      
      if (! output_character_file.empty()) {
	ngram_count_set_type model;
	ngram_count_set_type backoff;
	
	characters_learn(treebanks, model, backoff, weight_function(grammar));
	
	write_characters(output_character_file, model, backoff, cutoff_character);
      } 
      
      if (sig)
	lexicon_learn(treebanks, lexicon, weight_function(grammar));

      if (0.0 < cutoff_rule && cutoff_rule < 1.0)
	grammar_prune(rules, cutoff_rule);
      if (0.0 < cutoff_lexicon && cutoff_lexicon < 1.0)
	lexicon_prune(lexicon, cutoff_lexicon);
      
      write_grammar(output_grammar_file, rules);
      write_grammar(output_lexicon_file, lexicon);
    } else {
      if (0.0 < cutoff_rule && cutoff_rule < 1.0)
	grammar_prune(grammar, cutoff_rule);
      
      write_grammar(output_grammar_file, grammar);
    }
    
    if (! output_pos_file.empty()) {
      grammar_type rules;
      
      pos_learn(labels, rules);
      
      write_grammar(output_pos_file, rules);
    }
    
  }
  catch (std::exception& err) {
    std::cerr << "error: " << err.what() << std::endl;
    return 1;
  }
  return 0;
}

template <typename Function>
void treebank_apply(const treebank_type& treebank,
		    Function function)
{
  typedef std::vector<int, std::allocator<int> > index_set_type;
  
  index_set_type j;
  index_set_type j_end;
  rule_ptr_type  rule_annotated(new rule_type());
  
  hypergraph_type::node_set_type::const_iterator niter_end = treebank.treebank.nodes.end();
  for (hypergraph_type::node_set_type::const_iterator niter = treebank.treebank.nodes.begin(); niter != niter_end; ++ niter) {
    const hypergraph_type::node_type& node = *niter;
    
    hypergraph_type::node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
    for (hypergraph_type::node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
      const hypergraph_type::edge_type& edge = treebank.treebank.edges[*eiter];
      const rule_ptr_type& rule = edge.rule;
      
      j.clear();
      j.resize(rule->rhs.size() + 1, 0);
      j_end.resize(rule->rhs.size() + 1);
      
      rule_annotated->lhs = rule->lhs;
      rule_annotated->rhs = rule->rhs;
      
      hypergraph_type::edge_type::node_set_type tails(edge.tails);
      
      j_end.front() = treebank.offsets[edge.head + 1] - treebank.offsets[edge.head];
      size_t pos = 0;
      for (size_t i = 1; i != j_end.size(); ++ i)
	if (rule->rhs[i - 1].is_non_terminal()) {
	  j_end[i] = treebank.offsets[edge.tails[pos] + 1] - treebank.offsets[edge.tails[pos]];
	  ++ pos;
	} else
	  j_end[i] = 0;
      
      for (;;) {
	const hypergraph_type::id_type head = treebank.offsets[edge.head] + j[0];
	rule_annotated->lhs = treebank.labels[head];
	size_t pos = 0;
	for (size_t i = 1; i != j_end.size(); ++ i)
	  if (j_end[i]) {
	    const hypergraph_type::id_type tail = treebank.offsets[edge.tails[pos]] + j[i];
	    rule_annotated->rhs[i - 1] = treebank.labels[tail];
	    tails[pos] = tail;
	    ++ pos;
	  } 
	
	function(rule_annotated, head, tails);
	
	size_t index = 0;
	for (/**/; index != j.size(); ++ index) 
	  if (j_end[index]) {
	    ++ j[index];
	    if (j[index] < j_end[index]) break;
	    j[index] = 0;
	  }
	
	if (index == j.size()) break;
      }
    }
  }
}

template <typename Function>
void treebank_apply_reverse(const treebank_type& treebank,
			    Function function)
{
  typedef std::vector<int, std::allocator<int> > index_set_type;
  
  index_set_type j;
  index_set_type j_end;
  rule_ptr_type  rule_annotated(new rule_type());
  
  hypergraph_type::node_set_type::const_reverse_iterator niter_end = treebank.treebank.nodes.rend();
  for (hypergraph_type::node_set_type::const_reverse_iterator niter = treebank.treebank.nodes.rbegin(); niter != niter_end; ++ niter) {
    const hypergraph_type::node_type& node = *niter;
    
    hypergraph_type::node_type::edge_set_type::const_iterator eiter_end = node.edges.end();
    for (hypergraph_type::node_type::edge_set_type::const_iterator eiter = node.edges.begin(); eiter != eiter_end; ++ eiter) {
      const hypergraph_type::edge_type& edge = treebank.treebank.edges[*eiter];
      const rule_ptr_type& rule = edge.rule;
      
      j.clear();
      j.resize(rule->rhs.size() + 1, 0);
      j_end.resize(rule->rhs.size() + 1);
      
      rule_annotated->lhs = rule->lhs;
      rule_annotated->rhs = rule->rhs;
      
      hypergraph_type::edge_type::node_set_type tails(edge.tails);
      
      j_end.front() = treebank.offsets[edge.head + 1] - treebank.offsets[edge.head];
      size_t pos = 0;
      for (size_t i = 1; i != j_end.size(); ++ i)
	if (rule->rhs[i - 1].is_non_terminal()) {
	  j_end[i] = treebank.offsets[edge.tails[pos] + 1] - treebank.offsets[edge.tails[pos]];
	  ++ pos;
	} else
	  j_end[i] = 0;
      
      for (;;) {
	const hypergraph_type::id_type head = treebank.offsets[edge.head] + j[0];
	rule_annotated->lhs = treebank.labels[head];
	size_t pos = 0;
	for (size_t i = 1; i != j_end.size(); ++ i)
	  if (j_end[i]) {
	    const hypergraph_type::id_type tail = treebank.offsets[edge.tails[pos]] + j[i];
	    rule_annotated->rhs[i - 1] = treebank.labels[tail];
	    tails[pos] = tail;
	    ++ pos;
	  } 
	
	function(rule_annotated, head, tails);
	
	size_t index = 0;
	for (/**/; index != j.size(); ++ index) 
	  if (j_end[index]) {
	    ++ j[index];
	    if (j[index] < j_end[index]) break;
	    j[index] = 0;
	  }
	
	if (index == j.size()) break;
      }
    }
  }
}

template <typename Weights, typename Function>
struct InsideFunction
{
  typedef typename Weights::value_type weight_type;
  
  InsideFunction(Weights& __inside,
		 Function __function)
    : inside(__inside), function(__function) {}

  template <typename Head, typename Tails>
  void operator()(const rule_ptr_type& rule,
		  const Head& head,
		  const Tails& tails)
  {
    weight_type weight = function(rule);
    
    typename Tails::const_iterator titer_end = tails.end();
    for (typename Tails::const_iterator titer = tails.begin(); titer != titer_end; ++ titer)
      weight *= inside[*titer];
    
    inside[head] += weight;
  }
  
  Weights& inside;
  Function function;
};

template <typename Weights, typename Function>
struct OutsideFunction
{
  OutsideFunction(const Weights& __inside,
		  Weights& __outside,
		  Function __function)
    : inside(__inside), outside(__outside), function(__function) {}
  
  template <typename Head, typename Tails>
  void operator()(const rule_ptr_type& rule,
		  const Head& head,
		  const Tails& tails)
  {
    weight_type weight_outside = function(rule) * outside[head];
    
    typename Tails::const_iterator titer_begin = tails.begin();
    typename Tails::const_iterator titer_end   = tails.end();
    for (typename Tails::const_iterator titer = titer_begin; titer != titer_end; ++ titer) {
      weight_type weight = weight_outside;
      
      typename Tails::const_iterator niter_end = titer_end;
      for (typename Tails::const_iterator niter = titer_begin; niter != niter_end; ++ niter)
	if (titer != niter)
	  weight *= inside[*niter];
      
      outside[*titer] += weight;
    }
  }

  const Weights& inside;
  Weights& outside;
  Function function;
};


bool is_fixed_non_terminal(const symbol_type& symbol)
{ 
  return symbol.is_non_terminal() && symbol == goal;
};

symbol_type annotate_symbol(const symbol_type& symbol, const int bitpos, const bool bit)
{
  if (symbol.is_non_terminal()) {
    if (is_fixed_non_terminal(symbol)) return symbol;

    namespace xpressive = boost::xpressive;
    
    typedef xpressive::basic_regex<utils::piece::const_iterator> pregex;
    typedef xpressive::match_results<utils::piece::const_iterator> pmatch;
    
    static pregex re = (xpressive::s1= +(~xpressive::_s)) >> '@' >> (xpressive::s2= -+xpressive::_d);
    
    const utils::piece piece = symbol.non_terminal_strip();
    const int mask = 1 << bitpos;
    
    pmatch what;
    if (xpressive::regex_match(piece, what, re)) {
      const int value = (utils::lexical_cast<int>(what[2]) & (~mask)) | (-bit & mask);
      return '[' + what[1] + '@' + utils::lexical_cast<std::string>(value) + ']';
    } else
      return '[' + piece + '@' + utils::lexical_cast<std::string>(-bit & mask) + ']';
  } else
    return symbol;
}

struct Annotator : public utils::hashmurmur3<size_t>
{
  typedef utils::hashmurmur3<size_t> hasher_type;
  
  Annotator(const int __bits) : bits(__bits) {}
  
  struct Cache
  {
    symbol_type symbol;
    symbol_type annotated;
    bool bit;
    
    Cache() : symbol(), annotated(), bit(false) {}
  };
  typedef Cache cache_type;
  typedef utils::array_power2<cache_type, 1024 * 8, std::allocator<cache_type> > cache_set_type;
  
  const symbol_type& annotate(const symbol_type& symbol, const bool bit)
  {
    const size_t cache_pos = hasher_type::operator()(symbol.id(), bit) & (caches.size() - 1);
    cache_type& cache = caches[cache_pos];
    if (cache.symbol != symbol || cache.bit != bit) {
      cache.symbol = symbol;
      cache.bit = bit;
      cache.annotated = annotate_symbol(symbol, bits, bit);
    }
    return cache.annotated;
  }
  
  cache_set_type caches;
  const int bits;
};

template <typename Tp>
struct greater_ptr_second
{
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x->second > y->second;
  }
};

template <typename Tp>
struct less_ptr_second
{
  bool operator()(const Tp* x, const Tp* y) const
  {
    return x->second < y->second;
  }
};

struct filter_pruned
{
  typedef std::vector<bool, std::allocator<bool> > removed_type;

  const removed_type& removed;
  
  filter_pruned(const removed_type& __removed) : removed(__removed) {}
  
  template <typename Edge>
  bool operator()(const Edge& edge) const
  {
    return removed[edge.id];
  }
};

template <typename Scale>
struct TaskMergeScale
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;
  typedef Scale scale_set_type;
  
  TaskMergeScale(const grammar_type& __grammar,
		 queue_type& __queue)
    : grammar(__grammar),
      queue(__queue),
      scale() {}
  
  void operator()()
  {
    // we will statistics that are requried for P(A_1 | A) and P(A_2 | A)
    
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      const treebank_type& treebank (*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
      
      treebank_apply(treebank, InsideFunction<weight_set_type, weight_function>(inside, weight_function(grammar)));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, weight_function>(inside, outside, weight_function(grammar)));
      
      const weight_type weight_total = inside.back();
      for (size_t i = 0; i != treebank.labels.size(); ++ i)
	scale[treebank.labels[i]] += inside[i] * outside[i] / weight_total;
    }
  }
  
  const grammar_type& grammar;
  queue_type& queue;

  scale_set_type scale;
};

template <typename Loss, typename Scale>
struct TaskMergeLoss : public Annotator
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;

  typedef Loss loss_set_type;
  typedef Scale scale_set_type;

  TaskMergeLoss(const grammar_type& __grammar,
		const scale_set_type& __scale,
		const int& __bits,
		queue_type& __queue)
    : Annotator(__bits),
      grammar(__grammar),
      scale(__scale),
      queue(__queue),
      loss() {}
  
  void operator()()
  {
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      const treebank_type& treebank(*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
    
      treebank_apply(treebank, InsideFunction<weight_set_type, weight_function>(inside, weight_function(grammar)));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, weight_function>(inside, outside, weight_function(grammar)));
      
      const weight_type weight_total = inside.back();
      
      hypergraph_type::node_set_type::const_iterator niter_end = treebank.treebank.nodes.end();
      for (hypergraph_type::node_set_type::const_iterator niter = treebank.treebank.nodes.begin(); niter != niter_end; ++ niter) {
	const hypergraph_type::node_type& node = *niter;
	
	const size_t first = treebank.offsets[node.id];
	const size_t last  = treebank.offsets[node.id + 1];
	
	if (last - first == 1) continue;
	
	for (size_t pos = first; pos < last; pos += 2) {
	  // pos, pos + 1
	  
	  typename scale_set_type::const_iterator witer1 = scale.find(treebank.labels[pos]);
	  typename scale_set_type::const_iterator witer2 = scale.find(treebank.labels[pos + 1]);
	  
	  if (witer1 == scale.end())
	    throw std::runtime_error("no scale? " + static_cast<const std::string&>(treebank.labels[pos]));
	  if (witer2 == scale.end())
	    throw std::runtime_error("no scale? " + static_cast<const std::string&>(treebank.labels[pos + 1]));
	  
	  const weight_type prob_split    = inside[pos] * outside[pos] + inside[pos + 1] * outside[pos + 1];
	  const weight_type inside_merge  = inside[pos] * witer1->second + inside[pos + 1] * witer2->second;
	  const weight_type outside_merge = outside[pos] + outside[pos + 1];
	  const weight_type scale_norm    = witer1->second + witer2->second;
	  
	  const weight_type loss_node = (inside_merge * outside_merge / scale_norm) / prob_split;
	  
	  std::pair<typename loss_set_type::iterator, bool> result = loss.insert(std::make_pair(treebank.labels[pos + 1], loss_node));
	  if (! result.second)
	    result.first->second *= loss_node;
	}
      }
    }
  }
  
  const grammar_type& grammar;
  const scale_set_type& scale;
  
  queue_type& queue;
  
  loss_set_type loss;
};

template <typename Merged>
struct TaskMergeTreebank
{
  typedef utils::lockfree_list_queue<treebank_type*, std::allocator<treebank_type*> > queue_type;
  
  TaskMergeTreebank(const Merged& __merged,
		    queue_type& __queue)
    : merged(__merged),
      queue(__queue) {}
  
  void operator()()
  {
    treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      treebank_type& treebank(*__treebank);
      
      treebank_type::label_set_type labels;
      labels.reserve(treebank.labels.size());
      
      treebank_type::offset_set_type  offsets(treebank.offsets);
      offsets[0] = 0;
      
      hypergraph_type::node_set_type::const_iterator niter_end = treebank.treebank.nodes.end();
      for (hypergraph_type::node_set_type::const_iterator niter = treebank.treebank.nodes.begin(); niter != niter_end; ++ niter) {
	const hypergraph_type::node_type& node = *niter;
	
	const size_t first = treebank.offsets[node.id];
	const size_t last  = treebank.offsets[node.id + 1];
	
	if (last - first == 1)
	  labels.push_back(treebank.labels[first]);
	else {
	  for (size_t pos = first; pos < last; pos += 2) {
	    const symbol_type& symbol = treebank.labels[pos + 1];
	    
	    labels.push_back(treebank.labels[pos]);
	    if (merged.find(symbol) == merged.end())
	      labels.push_back(treebank.labels[pos + 1]);
	  }
	}
	
	offsets[node.id + 1] = labels.size();
      }
      
      treebank_type::label_set_type(labels).swap(labels);
      treebank.labels.swap(labels);
      treebank.offsets.swap(offsets);
    }
  }
  
  const Merged& merged;
  queue_type& queue;
};

template <typename Merged>
struct TaskMergeGrammar : public Annotator
{
  typedef utils::lockfree_list_queue<const grammar_type::value_type*, std::allocator<const grammar_type::value_type*> > queue_type;
  
  TaskMergeGrammar(const int __bits,
		   const Merged& __merged,
		   const label_count_set_type& __labels,
		   queue_type& __queue)
    : Annotator(__bits),
      merged(__merged),
      labels(__labels),
      queue(__queue) {}

  void operator()()
  {
    const grammar_type::value_type* ptr = 0;
    for (;;) {
      queue.pop(ptr);
      if (! ptr) break;
      
      const rule_ptr_type& rule = ptr->first;
      
      label_count_set_type::const_iterator liter = labels.find(rule->lhs);
      if (liter == labels.end())
	throw std::runtime_error("no lhs count...? " + static_cast<const std::string&>(rule->lhs));
      
      bool annotated = false;
      symbol_type lhs = rule->lhs;
      if (merged.find(lhs) != merged.end()) {
	lhs = annotate(lhs, false);
	annotated = true;
      }
      
      symbol_set_type symbols(rule->rhs);
      
      symbol_set_type::iterator siter_end = symbols.end();
      for (symbol_set_type::iterator siter = symbols.begin(); siter != siter_end; ++ siter)
	if (siter->is_non_terminal() && merged.find(*siter) != merged.end()) {
	  *siter = annotate(*siter, false);
	  annotated = true;
	}
      
      if (annotated)
	counts[lhs][rule_type::create(rule_type(lhs, symbols))] += ptr->second * liter->second;
      else
	counts[rule->lhs][rule] += ptr->second * liter->second;
    }
  }
  
  const Merged& merged;
  const label_count_set_type& labels;
  queue_type& queue;
  count_set_type counts;
};

inline
double round(double number)
{
  return number < 0.0 ? std::ceil(number - 0.5) : std::floor(number + 0.5);
}

template <typename Generator, typename Maximizer>
void grammar_merge(treebank_set_type& treebanks,
		   label_count_set_type& labels,
		   grammar_type& grammar,
		   const int bits,
		   Generator& generator,
		   Maximizer maximizer)
{
  typedef utils::compact_set<symbol_type,
			     utils::unassigned<symbol_type>, utils::unassigned<symbol_type>,
			     boost::hash<symbol_type>, std::equal_to<symbol_type>,
			     std::allocator<symbol_type> > merged_set_type;
  typedef word_count_set_type scale_set_type;
  typedef word_count_set_type loss_set_type;

  typedef TaskMergeScale<scale_set_type>               task_scale_type;
  typedef TaskMergeLoss<loss_set_type, scale_set_type> task_loss_type;
  typedef TaskMergeTreebank<merged_set_type>           task_treebank_type;
  typedef TaskMergeGrammar<merged_set_type>            task_grammar_type;
  
  typedef std::vector<task_scale_type, std::allocator<task_scale_type> >       task_scale_set_type;
  typedef std::vector<task_loss_type, std::allocator<task_loss_type> >         task_loss_set_type;
  typedef std::vector<task_treebank_type, std::allocator<task_treebank_type> > task_treebank_set_type;
  typedef std::vector<task_grammar_type, std::allocator<task_grammar_type> >   task_grammar_set_type;
  
  typedef typename task_scale_type::queue_type    queue_scale_type;
  typedef typename task_loss_type::queue_type     queue_loss_type;
  typedef typename task_treebank_type::queue_type queue_treebank_type;
  typedef typename task_grammar_type::queue_type  queue_grammar_type;
  
  typedef std::vector<const loss_set_type::value_type*, std::allocator<const loss_set_type::value_type*> > sorted_type;
  

  // MapReduce to compute scaling
  queue_scale_type queue_scale;
  task_scale_set_type tasks_scale(threads, task_scale_type(grammar, queue_scale));
  
  boost::thread_group workers_scale;
  for (int i = 0; i != threads; ++ i)
    workers_scale.add_thread(new boost::thread(boost::ref(tasks_scale[i])));
  
  treebank_set_type::iterator titer_end = treebanks.end();
  for (treebank_set_type::iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_scale.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_scale.push(0);
  
  workers_scale.join_all();
  
  scale_set_type scale;
  
  for (int i = 0; i != threads; ++ i) {
    if (scale.empty())
      scale.swap(tasks_scale[i].scale);
    else
      scale += tasks_scale[i].scale;
  }
  
  // MapReduce to compute loss
  queue_loss_type queue_loss;
  task_loss_set_type tasks_loss(threads, task_loss_type(grammar, scale, bits, queue_loss));
  
  boost::thread_group workers_loss;
  for (int i = 0; i != threads; ++ i)
    workers_loss.add_thread(new boost::thread(boost::ref(tasks_loss[i])));
  
  for (treebank_set_type::iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_loss.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_loss.push(0);
  
  workers_loss.join_all();
  
  loss_set_type loss;
  for (int i = 0; i != threads; ++ i) {
    if (loss.empty())
      loss.swap(tasks_loss[i].loss);
    else
      loss *= tasks_loss[i].loss;
  }
  
  // sort wrt gain of merging == loss of splitting...
  sorted_type sorted;
  sorted.reserve(loss.size());
  
  loss_set_type::const_iterator liter_end = loss.end();
  for (loss_set_type::const_iterator liter = loss.begin(); liter != liter_end; ++ liter)
    sorted.push_back(&(*liter));
  
  const size_t sorted_size = utils::bithack::min(utils::bithack::max(size_t(1),
								     size_t(round(merge_ratio * sorted.size()))),
						 size_t(sorted.size() - 1));
  std::nth_element(sorted.begin(), sorted.begin() + sorted_size, sorted.end(), greater_ptr_second<loss_set_type::value_type>());
  
  const weight_type threshold = sorted[sorted_size]->second;
  
  merged_set_type merged;
  
  if (debug >= 2)
    std::cerr << "threshold: " << threshold << std::endl;
  
  // insert nth elements + label sharing the same threshold
  sorted_type::const_iterator siter = sorted.begin();
  sorted_type::const_iterator siter_end = sorted.end();
  sorted_type::const_iterator siter_last = siter + sorted_size;

  bool found_equal = false;
  for (/**/; siter != siter_last; ++ siter) {
    if (debug >= 2)
      std::cerr << "merge: " << (*siter)->first << " gain: " << (*siter)->second << std::endl;
    merged.insert((*siter)->first);

    found_equal |= ((*siter)->second == threshold);
  }
  if (found_equal)
    for (/**/; siter != siter_end && (*siter)->second == threshold; ++ siter) {
      if (debug >= 2)
	std::cerr << "merge: " << (*siter)->first << " gain: " << (*siter)->second << std::endl;
      merged.insert((*siter)->first);
    }
  
  if (debug)
    std::cerr << "merged: " << merged.size() << " split: " << (sorted.size() - merged.size()) << std::endl;
  
  // MapReduce to merge treeebanks
  queue_treebank_type queue_treebank;

  boost::thread_group workers_treebank;
  for (int i = 0; i != threads; ++ i)
    workers_treebank.add_thread(new boost::thread(task_treebank_type(merged, queue_treebank)));
  
  for (treebank_set_type::iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_treebank.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_treebank.push(0);

  workers_treebank.join_all();
  
  // MapReduce to merge grammar
  queue_grammar_type queue_grammar;
  task_grammar_set_type tasks_grammar(threads, task_grammar_type(bits, merged, labels, queue_grammar));
  
  boost::thread_group workers_grammar;
  for (int i = 0; i != threads; ++ i)
    workers_grammar.add_thread(new boost::thread(boost::ref(tasks_grammar[i])));
  
  grammar_type::const_iterator giter_end = grammar.end();
  for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter)
    queue_grammar.push(&(*giter));
  
  for (int i = 0; i != threads; ++ i)
    queue_grammar.push(0);
  
  workers_grammar.join_all();
  
  count_set_type counts;
  
  for (int i = 0; i != threads; ++ i) {
    if (counts.empty())
      counts.swap(tasks_grammar[i].counts);
    else {
      count_set_type::const_iterator citer_end = tasks_grammar[i].counts.end();
      for (count_set_type::const_iterator citer = tasks_grammar[i].counts.begin(); citer != citer_end; ++ citer)
	counts[citer->first] += citer->second;
    }
  }
  
  if (debug)
    std::cerr << "# of symbols: " << counts.size() << std::endl;
  
  grammar_maximize(counts, grammar, maximizer);
}

struct TaskSplitTreebank : public Annotator
{
  typedef utils::lockfree_list_queue<treebank_type*, std::allocator<treebank_type*> > queue_type;
  
  TaskSplitTreebank(const int __bits,
		    queue_type& __queue,
		    const grammar_type& __grammar)
    : Annotator(__bits),
      queue(__queue),
      grammar(__grammar)
  {}
  
  void operator()()
  {
    treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      treebank_type& treebank(*__treebank);
      
      treebank_type::label_set_type labels;
      labels.reserve(treebank.labels.size() * 2);
      
      treebank_type::offset_set_type  offsets(treebank.offsets);
      offsets[0] = 0;
      
      hypergraph_type::node_set_type::const_iterator niter_end = treebank.treebank.nodes.end();
      for (hypergraph_type::node_set_type::const_iterator niter = treebank.treebank.nodes.begin(); niter != niter_end; ++ niter) {
	const hypergraph_type::node_type& node = *niter;
	
	const size_t first = treebank.offsets[node.id];
	const size_t last  = treebank.offsets[node.id + 1];
	
	if (node.id == treebank.treebank.goal)
	  labels.push_back(treebank.labels[first]);
	else {
	  for (size_t pos = first; pos != last; ++ pos) {
	    labels.push_back(annotate(treebank.labels[pos], false));
	    labels.push_back(annotate(treebank.labels[pos], true));
	  }
	}
	
	offsets[node.id + 1] = labels.size();
      }
      
      treebank_type::label_set_type(labels).swap(labels);
      treebank.labels.swap(labels);
      treebank.offsets.swap(offsets);
    }
  }
  
  queue_type& queue;
  const grammar_type& grammar;
};

template <typename Generator>
struct TaskSplitGrammar : public Annotator
{
  typedef utils::lockfree_list_queue<const grammar_type::value_type*, std::allocator<const grammar_type::value_type*> > queue_type;

  TaskSplitGrammar(Generator __generator,
		   const int& __bits,
		   const label_count_set_type& __labels,
		   queue_type& __queue)
    : Annotator(__bits),
      generator(__generator),
      labels(__labels),
      queue(__queue) {}

  void operator()()
  {
    typedef std::vector<int, std::allocator<int> > index_set_type;
    typedef std::vector<symbol_type, std::allocator<symbol_type> > symbol_set_type;
    typedef std::vector<double, std::allocator<double> > random_set_type;
    
    index_set_type  j;
    index_set_type  j_end;
    symbol_set_type symbols;
    symbol_set_type symbols_new;
    
    const grammar_type::value_type* ptr = 0;
    
    grammar_type grammar_local;
    random_set_type randoms;
    
    for (;;) {
      queue.pop(ptr);
      if (! ptr) break;
      
      const rule_ptr_type& rule = ptr->first;

      label_count_set_type::const_iterator liter = labels.find(rule->lhs);
      if (liter == labels.end())
	throw std::runtime_error("no lhs count...? " + static_cast<const std::string&>(rule->lhs));
      
      symbols.clear();
      symbols.push_back(rule->lhs);
      symbols.insert(symbols.end(), rule->rhs.begin(), rule->rhs.end());
      
      symbols_new.clear();
      symbols_new.insert(symbols_new.end(), symbols.begin(), symbols.end());
      
      j.clear();
      j.resize(rule->rhs.size() + 1, 0);
      j_end.resize(rule->rhs.size() + 1);

      grammar_local.clear();
      
      for (size_t i = 0; i != symbols.size(); ++ i)
	j_end[i] = utils::bithack::branch(symbols[i].is_non_terminal(), utils::bithack::branch(is_fixed_non_terminal(symbols[i]), 1, 2), 0);
    
      for (;;) {
	for (size_t i = 0; i != symbols.size(); ++ i)
	  if (j_end[i])
	    symbols_new[i] = annotate(symbols[i], j[i]);
	
	const rule_ptr_type rule = rule_type::create(rule_type(symbols_new.front(), symbols_new.begin() + 1, symbols_new.end()));
	
	grammar_local[rule] = ptr->second;
	
	size_t index = 0;
	for (/**/; index != j.size(); ++ index) 
	  if (j_end[index]) {
	    ++ j[index];
	    if (j[index] < j_end[index]) break;
	    j[index] = 0;
	  }
	
	if (index == j.size()) break;
      }
      
      // transform grammar_local to counts...
      const weight_type count = liter->second * ptr->second;
      
      // random partition...
      // we will add 1% of randomness...
      randoms.clear();
      for (size_t i = 0; i != grammar_local.size(); ++ i)
	randoms.push_back(boost::uniform_real<double>(0.99, 1.01)(generator));
      const double random_sum = std::accumulate(randoms.begin(), randoms.end(), double(0.0));
      
      random_set_type::const_iterator riter = randoms.begin();
      grammar_type::const_iterator giter_end = grammar_local.end();
      for (grammar_type::const_iterator giter = grammar_local.begin(); giter != giter_end; ++ giter, ++ riter)
	counts[giter->first->lhs][giter->first] += count * weight_type(*riter / random_sum);

      grammar_local.clear();
    }
  }
  
  Generator generator;
  const label_count_set_type& labels;
  queue_type& queue;
  count_set_type counts;
};

template <typename Generator, typename Maximizer>
void grammar_split(treebank_set_type& treebanks,
		   label_count_set_type& labels,
		   grammar_type& grammar,
		   const int bits,
		   Generator& generator,
		   Maximizer maximizer)
{
  typedef TaskSplitTreebank           task_treebank_type;
  typedef TaskSplitGrammar<Generator> task_grammar_type;

  typedef std::vector<task_grammar_type, std::allocator<task_grammar_type> > task_grammar_set_type;
  
  typedef typename task_treebank_type::queue_type queue_treebank_type;
  typedef typename task_grammar_type::queue_type  queue_grammar_type;
  
  queue_grammar_type  queue_grammar;
  task_grammar_set_type tasks_grammar(threads, task_grammar_type(generator, bits, labels, queue_grammar));
  
  boost::thread_group workers_grammar;
  for (int i = 0; i != threads; ++ i)
    workers_grammar.add_thread(new boost::thread(boost::ref(tasks_grammar[i])));
  
  grammar_type::const_iterator giter_end = grammar.end();
  for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter)
    queue_grammar.push(&(*giter));
  
  for (int i = 0; i != threads; ++ i)
    queue_grammar.push(0);
  
  workers_grammar.join_all();

  // split grammar...
  count_set_type counts;
  
  for (int i = 0; i != threads; ++ i) {
    if (counts.empty())
      counts.swap(tasks_grammar[i].counts);
    else {
      count_set_type::const_iterator citer_end = tasks_grammar[i].counts.end();
      for (count_set_type::const_iterator citer = tasks_grammar[i].counts.begin(); citer != citer_end; ++ citer)
	counts[citer->first] += citer->second;
    }
  }

  generator = tasks_grammar.front().generator;
  
  tasks_grammar.clear();
  
  if (debug)
    std::cerr << "# of symbols: " << counts.size() << std::endl;
  
  // maximization
  grammar_maximize(counts, grammar, maximizer);
  
  counts.clear();
  
  queue_treebank_type queue_treebank;
  
  boost::thread_group workers_treebank;
  for (int i = 0; i != threads; ++ i)
    workers_treebank.add_thread(new boost::thread(task_treebank_type(bits, queue_treebank, grammar)));
  
  treebank_set_type::iterator titer_end = treebanks.end();
  for (treebank_set_type::iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_treebank.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_treebank.push(0);

  workers_treebank.join_all();
}


template <typename Function>
struct TaskLearn
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;
  
  TaskLearn(queue_type& __queue, Function __function)
    : queue(__queue),
      function(__function),
      logprob(cicada::semiring::traits<weight_type>::one()),
      counts() {}
  
  template <typename Weights>
  struct Accumulated
  {
    Accumulated(const Weights& __inside,
		const Weights& __outside,
		const weight_type& __total,
		count_set_type& __counts,
		Function __function)
      : inside(__inside), outside(__outside), total(__total), counts(__counts), function(__function) {}
    
    template <typename Head, typename Tails>
    void operator()(const rule_ptr_type& rule,
		    const Head& head,
		    const Tails& tails)
    {
      weight_type weight = function(rule) * outside[head] / total;
      
      typename Tails::const_iterator titer_end = tails.end();
      for (typename Tails::const_iterator titer = tails.begin(); titer != titer_end; ++ titer)
	weight *= inside[*titer];
      
      counts[rule->lhs][rule_type::create(*rule)] += weight;
    }
    
    const Weights& inside;
    const Weights& outside;
    const weight_type total;
    count_set_type& counts;
    Function function;
  };


  void operator()()
  {
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      const treebank_type& treebank(*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
      
      treebank_apply(treebank, InsideFunction<weight_set_type, Function>(inside, function));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, Function>(inside, outside, function));
      treebank_apply(treebank, Accumulated<weight_set_type>(inside, outside, inside.back(), counts, function));
      
      if (debug >= 3)
	std::cerr << "inside: " << cicada::semiring::log(inside.back()) << std::endl;
      
      logprob *= inside.back();
    }
    
    count_set_type::const_iterator liter_end = counts.end();
    for (count_set_type::const_iterator liter = counts.begin(); liter != liter_end; ++ liter) {
      weight_type& count = labels[liter->first];
      
      grammar_type::const_iterator giter_end = liter->second.end();
      for (grammar_type::const_iterator giter = liter->second.begin(); giter != giter_end; ++ giter)
	count += giter->second;
    }
  }
  
  queue_type&    queue;
  Function       function;
  
  weight_type          logprob;
  label_count_set_type labels;
  count_set_type       counts;
};

template <typename Function, typename Maximizer>
double grammar_learn(const treebank_set_type& treebanks,
		     label_count_set_type& labels,
		     grammar_type& grammar,
		     Function function,
		     Maximizer maximizer)
{
  typedef TaskLearn<Function> task_type;
  typedef std::vector<task_type, std::allocator<task_type> > task_set_type;
  typedef typename task_type::queue_type queue_type;

  queue_type queue;
  task_set_type tasks(threads, task_type(queue, function));

  boost::thread_group workers;
  for (int i = 0; i != threads; ++ i)
    workers.add_thread(new boost::thread(boost::ref(tasks[i])));

  treebank_set_type::const_iterator titer_end = treebanks.end();
  for (treebank_set_type::const_iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue.push(0);
  
  workers.join_all();
  
  weight_type logprob(cicada::semiring::traits<weight_type>::one());
  labels.clear();
  count_set_type counts;
  for (int i = 0; i != threads; ++ i) {
    logprob *= tasks[i].logprob;

    if (counts.empty())
      counts.swap(tasks[i].counts);
    else {
      count_set_type::const_iterator liter_end = tasks[i].counts.end();
      for (count_set_type::const_iterator liter = tasks[i].counts.begin(); liter != liter_end; ++ liter)
	counts[liter->first] += liter->second;
    }
    
    if (labels.empty())
      labels.swap(tasks[i].labels);
    else
      labels += tasks[i].labels;
  }
  
  if (debug)
    std::cerr << "log-likelihood: " << cicada::semiring::log(logprob)
	      << " # of symbols: " << counts.size()
	      << std::endl;
  
  // maximization
  grammar_maximize(counts, grammar, maximizer);
  
  return cicada::semiring::log(logprob);
}

template <typename Function>
struct TaskLexiconFrequency
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;

  TaskLexiconFrequency(queue_type& __queue, Function __function)
    : queue(__queue),
      function(__function),
      counts() { }

  template <typename Weights>
  struct Accumulated
  {
    Accumulated(const Weights& __inside,
		const Weights& __outside,
		const weight_type& __total,
		word_count_set_type& __counts,
		Function __function)
      : inside(__inside), outside(__outside), total(__total), counts(__counts), function(__function) {}
    
    template <typename Head, typename Tails>
    void operator()(const rule_ptr_type& rule,
		    const Head& head,
		    const Tails& tails)
    {
      if (! tails.empty()) return;
      
      counts[rule->rhs.front()] += function(rule) * outside[head] / total;
    }
    
    const Weights& inside;
    const Weights& outside;
    const weight_type total;
    word_count_set_type& counts;
    Function function;
  };

  void operator()()
  {
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    ngram_type trigram(3);
    ngram_type bigram(2);

    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      const treebank_type& treebank(*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
      
      treebank_apply(treebank, InsideFunction<weight_set_type, Function>(inside, function));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, Function>(inside, outside, function));
      treebank_apply(treebank, Accumulated<weight_set_type>(inside, outside, inside.back(), counts, function));
    }
  }
  
  queue_type&    queue;
  Function       function;
  
  word_count_set_type counts;
};

template <typename Function>
struct TaskLexiconCount
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;
  
  // we will collect, tag-signature-word, signature-word, word
  
  TaskLexiconCount(const word_count_set_type& __word_counts, queue_type& __queue, Function __function)
    : word_counts(__word_counts),
      queue(__queue),
      function(__function),
      counts(),
      counts_sig() { }
  
  template <typename Weights>
  struct Accumulated
  {
    Accumulated(const word_count_set_type& __word_counts,
		const Weights& __inside,
		const Weights& __outside,
		const weight_type& __total,
		const weight_type& __threshold,
		const signature_type& __signature,
		ngram_count_set_type& __counts,
		ngram_count_set_type& __counts_sig,
		ngram_count_set_type& __counts_unknown,
		Function __function)
      : trigram(3), bigram(2),
	word_counts(__word_counts),
	inside(__inside), outside(__outside), total(__total), threshold(__threshold), 
	signature(__signature),
	counts(__counts), counts_sig(__counts_sig), counts_unknown(__counts_unknown),
	function(__function) {}
    
    template <typename Head, typename Tails>
    void operator()(const rule_ptr_type& rule,
		    const Head& head,
		    const Tails& tails)
    {
      if (! tails.empty()) return;
      
      bigram[0] = rule->lhs;
      bigram[1] = signature(rule->rhs.front());
      
      trigram[0] = bigram[1];
      trigram[1] = bigram[0];
      trigram[2] = rule->rhs.front();
      
      const weight_type count = function(rule) * outside[head] / total;
      
      counts[trigram] += count;
      counts[ngram_type(trigram.begin() + 1, trigram.end())] += count;
      counts[ngram_type(trigram.begin() + 2, trigram.end())] += count;
      
      counts_sig[bigram] += count;
      counts_sig[ngram_type(bigram.begin() + 1, bigram.end())] += count;
      
      word_count_set_type::const_iterator witer = word_counts.find(rule->rhs.front());
      if (witer == word_counts.end())
	throw std::runtime_error("invalid word???");
      
      if (witer->second <= threshold)
	counts_unknown[bigram] += count;
    }
    
    ngram_type trigram;
    ngram_type bigram;
    
    const word_count_set_type& word_counts;

    const Weights& inside;
    const Weights& outside;
    const weight_type total;
    const weight_type threshold;
    const signature_type& signature;
    ngram_count_set_type& counts;
    ngram_count_set_type& counts_sig;
    ngram_count_set_type& counts_unknown;
    Function function;
  };
  

  void operator()()
  {
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    const signature_type& __signature = signature_type::create(signature);
    
    const weight_type log_unknown_threshold(unknown_threshold);
    
    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;

      const treebank_type& treebank(*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
      
      treebank_apply(treebank, InsideFunction<weight_set_type, Function>(inside, function));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, Function>(inside, outside, function));
      treebank_apply(treebank, Accumulated<weight_set_type>(word_counts,
							    inside, outside, inside.back(), log_unknown_threshold,
							    __signature,
							    counts, counts_sig, counts_unknown,
							    function));
    }
  }
  
  const word_count_set_type& word_counts;
  queue_type&    queue;
  Function       function;
  
  ngram_count_set_type counts; // counts of tag-signature-word
  ngram_count_set_type counts_sig; // counts of tag-signature
  ngram_count_set_type counts_unknown; // count of unknown pair of tag-signature
};

struct LexiconEstimate
{
  typedef std::vector<weight_type, std::allocator<weight_type> > logprob_set_type;
  
  typedef std::vector<const ngram_count_set_type::value_type*, std::allocator<const ngram_count_set_type::value_type*> > ngram_set_type;
  typedef utils::unordered_map<ngram_type, ngram_set_type,
			       boost::hash<ngram_type>, std::equal_to<ngram_type>,
			       std::allocator<std::pair<const ngram_type, ngram_set_type> > >::type ngram_count_map_type;
  
  LexiconEstimate(const double& __prior, const int __order) : prior(__prior), order(__order) {}
  
  weight_type operator()(const ngram_count_set_type& counts, ngram_count_set_type& model, ngram_count_set_type& backoff)
  {
    using namespace boost::math::policies;
    typedef policy<domain_error<errno_on_error>,
		   pole_error<errno_on_error>,
		   overflow_error<errno_on_error>,
		   rounding_error<errno_on_error>,
		   evaluation_error<errno_on_error> > policy_type;
  
    double total = 0.0;
    size_t vocab_size = 0;
    
    ngram_set_type ngrams_local;
    {
      weight_type sum;
      ngram_count_set_type::const_iterator niter_end = counts.end();
      for (ngram_count_set_type::const_iterator niter = counts.begin(); niter != niter_end; ++ niter)
	if (niter->first.size() == 1) {
	  sum += niter->second;
	  ++ vocab_size;
	  ngrams_local.push_back(&(*niter));
	}
      total = sum;
    }
    
    logprob_set_type logprobs_local(ngrams_local.size());
    const weight_type logprior(prior);
    
    // we will loop, increment total until we have enough mass discounted...
    double discount = 0.0;
    for (;;) {
      discount = 0.0;
      
      weight_type logprob_sum;
      const double lognorm = utils::mathop::digamma(prior * vocab_size + total);
      //const weight_type lognorm(prior * vocab_size + total);
      
      logprob_set_type::iterator liter = logprobs_local.begin();
      ngram_set_type::const_iterator niter_end = ngrams_local.end();
      for (ngram_set_type::const_iterator niter = ngrams_local.begin(); niter != niter_end; ++ niter, ++ liter) {
	const double logprob = utils::mathop::digamma(static_cast<double>(logprior + (*niter)->second)) - lognorm;
	
	logprob_sum += cicada::semiring::traits<weight_type>::exp(logprob);
	*liter = cicada::semiring::traits<weight_type>::exp(logprob);
	
	//const weight_type logprob = (logprior + (*niter)->second) / lognorm;
	//logprob_sum += logprob;
	//*liter = logprob;
      }
      
      discount = - boost::math::expm1(cicada::semiring::log(logprob_sum), policy_type());
      
      if (discount > 0.0) break;
      ++ total;
    }
    
    {
      // copy logprob into actual storage..
      logprob_set_type::iterator liter = logprobs_local.begin();
      ngram_set_type::const_iterator niter_end = ngrams_local.end();
      for (ngram_set_type::const_iterator niter = ngrams_local.begin(); niter != niter_end; ++ niter, ++ liter)
	model[(*niter)->first] = *liter;
    }
    
    const weight_type logprob_unk(discount);

    ngram_count_map_type ngrams;
    
    for (int n = 2; n <= order; ++ n) {
      ngrams.clear();
      
      {
	ngram_count_set_type::const_iterator niter_end = counts.end();
	for (ngram_count_set_type::const_iterator niter = counts.begin(); niter != niter_end; ++ niter)
	  if (static_cast<int>(niter->first.size()) == n) 
	    ngrams[ngram_type(niter->first.begin(), niter->first.begin() + n - 1)].push_back(&(*niter));
      }
      
      ngram_count_map_type::const_iterator citer_end = ngrams.end();
      for (ngram_count_map_type::const_iterator citer = ngrams.begin(); citer != citer_end; ++ citer) {
	const ngram_set_type& ngrams_local = citer->second;
	logprobs_local.resize(ngrams_local.size());
	
	double total = 0.0;
	weight_type logsum;
	weight_type logsum_lower;
	ngram_set_type::const_iterator niter_end = ngrams_local.end();
	for (ngram_set_type::const_iterator niter = ngrams_local.begin(); niter != niter_end; ++ niter) {
	  logsum += (*niter)->second;
	  
	  ngram_count_set_type::const_iterator liter = model.find(ngram_type((*niter)->first.end() - n + 1, (*niter)->first.end()));
	  if (liter == model.end())
	    throw std::runtime_error("invalid lower order count: " + utils::lexical_cast<std::string>((*niter)->first));
	  
	  logsum_lower += liter->second;
	}
	
	total = logsum;
	
	const weight_type logprior(prior);
	const double discount_lower = - boost::math::expm1(cicada::semiring::log(logsum_lower), policy_type());
	double discount = 0.0;
	
	for (;;) {
	  discount = 0.0;
	  
	  weight_type logprob_sum;
	  const double lognorm = utils::mathop::digamma(prior * ngrams_local.size() + total);
	  //const weight_type lognorm(prior * ngrams_local.size() + total);
	  
	  logprob_set_type::iterator liter = logprobs_local.begin();
	  for (ngram_set_type::const_iterator niter = ngrams_local.begin(); niter != niter_end; ++ niter, ++ liter) {
	    const double logprob = utils::mathop::digamma(static_cast<double>(logprior + (*niter)->second)) - lognorm;
	    
	    logprob_sum += cicada::semiring::traits<weight_type>::exp(logprob);
	    *liter = cicada::semiring::traits<weight_type>::exp(logprob);
	    
	    //const weight_type logprob = (logprior + (*niter)->second) / lognorm;
	    //logprob_sum += logprob;
	    //*liter = logprob;
	  }
	  
	  discount = - boost::math::expm1(cicada::semiring::log(logprob_sum), policy_type());
	  
	  if (discount > 0.0) break;
	  ++ total;
	}
	
	// copy logprob into actual storage..
	logprob_set_type::const_iterator liter = logprobs_local.begin();
	for (ngram_set_type::const_iterator niter = ngrams_local.begin(); niter != niter_end; ++ niter, ++ liter)
	  model[(*niter)->first] = *liter;
	
	backoff[citer->first] = weight_type(discount) / weight_type(discount_lower);
      }
    }
    
    return logprob_unk;
  }
  
  const double prior;
  const int order;
};

template <typename Function>
void lexicon_learn(const treebank_set_type& treebanks,
		   grammar_type& lexicon,
		   Function function)
{
  // we will learn a trigram of tag-signature-word, but dump tag-signature only...
  // 
  // a trick is: since we are learning OOV probability, tag-signature-word trigram
  // will always backoff (with tag-signature backoff penalty) to signature-word which will
  // always backoff (with signature backoff penalty) to uniform distribution...
  //
  // Thus, we simply preserve tag-signature bigram probability with the penalties 
  // consisting of tag-sinature backoff + signarue backoff + uniform distribution
  
  typedef TaskLexiconFrequency<Function> task_frequency_type;
  typedef TaskLexiconCount<Function>     task_count_type;
  
  typedef std::vector<task_frequency_type, std::allocator<task_frequency_type> > task_frequency_set_type;
  typedef std::vector<task_count_type, std::allocator<task_count_type> >         task_count_set_type;
  
  typedef typename task_frequency_type::queue_type queue_frequency_type;
  typedef typename task_count_type::queue_type     queue_count_type;

  queue_frequency_type queue_frequency;
  task_frequency_set_type tasks_frequency(threads, task_frequency_type(queue_frequency, function));
  
  boost::thread_group workers_frequency;
  for (int i = 0; i != threads; ++ i)
    workers_frequency.add_thread(new boost::thread(boost::ref(tasks_frequency[i])));
  
  treebank_set_type::const_iterator titer_end = treebanks.end();
  for (treebank_set_type::const_iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_frequency.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_frequency.push(0);
  
  workers_frequency.join_all();
  
  word_count_set_type word_counts;
  for (int i = 0; i != threads; ++ i) {
    if (word_counts.empty())
      word_counts.swap(tasks_frequency[i].counts);
    else
      word_counts += tasks_frequency[i].counts;
  }

  queue_count_type queue_count;
  task_count_set_type tasks_count(threads, task_count_type(word_counts, queue_count, function));
  
  boost::thread_group workers_count;
  for (int i = 0; i != threads; ++ i)
    workers_count.add_thread(new boost::thread(boost::ref(tasks_count[i])));
  
  for (treebank_set_type::const_iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue_count.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue_count.push(0);
  
  workers_count.join_all();
  
  ngram_count_set_type counts;
  ngram_count_set_type counts_sig;
  ngram_count_set_type counts_unknown;
  for (int i = 0; i != threads; ++ i) {
    if (counts.empty())
      counts.swap(tasks_count[i].counts);
    else
      counts += tasks_count[i].counts;
    
    if (counts_sig.empty())
      counts_sig.swap(tasks_count[i].counts_sig);
    else
      counts_sig += tasks_count[i].counts_sig;
    
    if (counts_unknown.empty())
      counts_unknown.swap(tasks_count[i].counts_unknown);
    else
      counts_unknown += tasks_count[i].counts_unknown;
  }

  ngram_count_set_type model;
  ngram_count_set_type backoff;

  ngram_count_set_type model_sig;
  ngram_count_set_type backoff_sig;
  
  // estimate for tag-sig-word
  const weight_type logprob_unk = LexiconEstimate(prior_unknown, 3)(counts, model, backoff);
  
  //std::cerr << "logprob-unk: " << logprob_unk << std::endl;
  
  // estimate for tag-sig
  const weight_type logprob_unk_sig = LexiconEstimate(prior_signature, 2)(counts_sig, model_sig, backoff_sig);

  //std::cerr << "logprob-unk-sig: " << logprob_unk_sig << std::endl;
  
  // finished computation...
  // actually, we do not need full-trigram!
  // we need: tag-signature-<UNK>
  // which will be computed by bakoff(tag-signature) + backoff(signature) + unk which is stored in backoffs!
  
  // also, we need tag-signature probability
  // and probability mass left out from KNOWN lexicon...
  
  //
  // From lexicon, compute discounted mass in fully observed counts, that is (probably) available in lexicon...
  //
  
  // TODO handle weight_type, not mixed logprob/count etc..

  ngram_count_set_type model_tag;
  grammar_type::const_iterator liter_end = lexicon.end();
  for (grammar_type::const_iterator liter = lexicon.begin(); liter != liter_end; ++ liter) {
    const symbol_type& lhs = liter->first->lhs;
    
    model_tag[ngram_type(1, lhs)] += liter->second;
  }

  ngram_type bigram(2);
  
  ngram_count_set_type::const_iterator biter_end = backoff.end();
  for (ngram_count_set_type::const_iterator biter = backoff.begin(); biter != biter_end; ++ biter) 
    if (biter->first.size() == 2) {
      using namespace boost::math::policies;
      typedef policy<domain_error<errno_on_error>,
	pole_error<errno_on_error>,
	overflow_error<errno_on_error>,
	rounding_error<errno_on_error>,
	evaluation_error<errno_on_error> > policy_type;
      
      // swap backoff context..!
      bigram[0] = biter->first[1];
      bigram[1] = biter->first[0];

      // check if this is really unknown rule...
      if (counts_unknown.find(bigram) == counts_unknown.end()) continue;

      //std::cerr << "bigram: " << bigram << std::endl;
      
      ngram_count_set_type::const_iterator tag_iter = model_tag.find(ngram_type(1, bigram.front()));
      if (tag_iter == model_tag.end())
	throw std::runtime_error("invalid tag model!?");
      
      ngram_count_set_type::const_iterator sig_iter = model_sig.find(bigram);
      if (sig_iter == model_sig.end())
	throw std::runtime_error("invalid signature model!?");
      
      ngram_count_set_type::const_iterator siter = backoff.find(ngram_type(1, bigram.front()));
      if (siter == backoff.end())
	throw std::runtime_error("invalid backoffs!");
      
      const weight_type score_backoff = - boost::math::expm1(cicada::semiring::log(tag_iter->second), policy_type());
      const weight_type score_bigram  = sig_iter->second;
      const weight_type score_trigram = biter->second * siter->second * logprob_unk;
      
      //std::cerr << "backoff: " << score_backoff << " bigram: " << score_bigram << " trigram: " << score_trigram << std::endl;
      //std::cerr << "biter: " << biter->second << " siter: " << siter->second << std::endl;
      
      const weight_type score = score_trigram * score_bigram * score_backoff;
      
      lexicon.insert(std::make_pair(rule_type::create(rule_type(bigram.front(), rule_type::symbol_set_type(1, bigram.back()))), score));
    }
}

template <typename Function>
struct TaskCharacterCount
{
  typedef utils::lockfree_list_queue<const treebank_type*, std::allocator<const treebank_type*> > queue_type;
  
  // we will collect, tag-signature-word, signature-word, word
  
  TaskCharacterCount(queue_type& __queue, Function __function)
    : queue(__queue),
      function(__function),
      counts() { }
  
  template <typename Weights>
  struct Accumulated
  {
    typedef cicada::Sentence phrase_type;

    Accumulated(const Weights& __inside,
		const Weights& __outside,
		const weight_type& __total,
		const signature_type& __signature,
		const tokenizer_type& __tokenizer,
		ngram_count_set_type& __counts,
		Function __function)
      : ngram(3), phrase(1), tokenized(),
	inside(__inside), outside(__outside), total(__total),
	signature(__signature), tokenizer(__tokenizer),
	counts(__counts), 
	function(__function) {}
    
    template <typename Head, typename Tails>
    void operator()(const rule_ptr_type& rule,
		    const Head& head,
		    const Tails& tails)
    {
      typedef cicada::Sentence phrase_type;
      
      if (! tails.empty()) return;
      
      const weight_type count = function(rule) * outside[head] / total;
      
      ngram[0] = signature(rule->rhs.front());
      ngram[1] = rule->lhs;
      
      phrase.front() = rule->rhs.front();
      
      tokenizer(phrase, tokenized);
      phrase_type::const_iterator titer_end = tokenized.end();
      for (phrase_type::const_iterator titer = tokenized.begin(); titer != titer_end; ++ titer) {
	ngram[2] = *titer;
	
	counts[ngram] += count;
	counts[ngram_type(ngram.begin() + 1, ngram.end())] += count;
	counts[ngram_type(ngram.begin() + 2, ngram.end())] += count;
      }
    }
    
    ngram_type ngram;
    phrase_type phrase;
    phrase_type tokenized;

    const Weights& inside;
    const Weights& outside;
    const weight_type total;
    const signature_type& signature;
    const tokenizer_type& tokenizer;
    ngram_count_set_type& counts;
    Function function;
  };

  
  void operator()()
  {
    typedef std::vector<weight_type, std::allocator<weight_type> > weight_set_type;
    
    weight_set_type inside;
    weight_set_type outside;
    
    const signature_type& __signature = signature_type::create(signature);
    const tokenizer_type& __tokenizer = tokenizer_type::create("character");

    const treebank_type* __treebank = 0;
    for (;;) {
      queue.pop(__treebank);
      if (! __treebank) break;
      
      const treebank_type& treebank(*__treebank);
      
      inside.clear();
      outside.clear();
      inside.resize(treebank.labels.size());
      outside.resize(treebank.labels.size());
      outside.back() = cicada::semiring::traits<weight_type>::one();
      
      treebank_apply(treebank, InsideFunction<weight_set_type, Function>(inside, function));
      treebank_apply_reverse(treebank, OutsideFunction<weight_set_type, Function>(inside, outside, function));
      treebank_apply(treebank, Accumulated<weight_set_type>(inside, outside, inside.back(),
							    __signature, __tokenizer,
							    counts, 
							    function));
    }
  }
  
  queue_type&    queue;
  Function       function;
  
  ngram_count_set_type counts; // counts of tag-signature-word
};

template <typename Function>
void characters_learn(const treebank_set_type& treebanks,
		      ngram_count_set_type& model,
		      ngram_count_set_type& backoff,
		      Function function)
{
  typedef cicada::Vocab vocab_type;

  typedef TaskCharacterCount<Function> task_type;
  typedef std::vector<task_type, std::allocator<task_type> > task_set_type;
  typedef typename task_type::queue_type queue_type;

  queue_type queue;
  task_set_type tasks(threads, task_type(queue, function));

  boost::thread_group workers;
  for (int i = 0; i != threads; ++ i)
    workers.add_thread(new boost::thread(boost::ref(tasks[i])));
  
  treebank_set_type::const_iterator titer_end = treebanks.end();
  for (treebank_set_type::const_iterator titer = treebanks.begin(); titer != titer_end; ++ titer)
    queue.push(&(*titer));
  
  for (int i = 0; i != threads; ++ i)
    queue.push(0);
  
  workers.join_all();
  
  ngram_count_set_type counts;
  for (int i = 0; i != threads; ++ i) {
    if (counts.empty())
      counts.swap(tasks[i].counts);
    else
      counts += tasks[i].counts;
  }
  
  // estimate for tag-sig-word
  const weight_type logprob_unk = LexiconEstimate(prior_character, 3)(counts, model, backoff);
  
  model[ngram_type(1, vocab_type::UNK)] = logprob_unk;
}

void pos_learn(const label_count_set_type& labels,
	       grammar_type& grammar)
{
  // we use the estiamte of P(fine | coarse) for Pr(fine -> coarse)
  count_set_type counts;
  
  label_count_set_type::const_iterator liter_end = labels.end();
  for (label_count_set_type::const_iterator liter = labels.begin(); liter != liter_end; ++ liter) {
    const symbol_type coarse = liter->first.coarse();
    
    if (coarse != liter->first)
      counts[coarse][rule_type::create(rule_type(liter->first, &coarse, (&coarse) + 1))] += liter->second;
  }

  grammar_maximize(counts, grammar, Maximize());
}

template <typename Maximizer>
struct TaskMaximize
{
  typedef utils::lockfree_list_queue<const count_set_type::value_type*, std::allocator<const count_set_type::value_type*> > queue_type;
  
  TaskMaximize(queue_type& __queue,
	       Maximizer __maximizer)
    : queue(__queue),
      maximizer(__maximizer) {}
  
  void operator()()
  {
    const count_set_type::value_type* ptr = 0;
    
    for (;;) {
      queue.pop(ptr);
      if (! ptr) break;
      
      maximizer(ptr->second, grammar);
    }
  }
  
  queue_type& queue;
  grammar_type grammar;
  Maximizer    maximizer;
};


template <typename Maximizer>
void grammar_maximize(const count_set_type& counts,
		      grammar_type& grammar,
		      Maximizer maximizer)
{
  typedef TaskMaximize<Maximizer> task_type;
  typedef typename task_type::queue_type queue_type;
  
  typedef std::vector<task_type, std::allocator<task_type> > task_set_type;
  
  queue_type queue;
  task_set_type tasks(threads, task_type(queue, maximizer));
  
  boost::thread_group workers;
  for (int i = 0; i != threads; ++ i)
    workers.add_thread(new boost::thread(boost::ref(tasks[i])));
  
  count_set_type::const_iterator citer_end = counts.end();
  for (count_set_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer)
    queue.push(&(*citer));
  
  for (int i = 0; i != threads; ++ i)
    queue.push(0);
  
  workers.join_all();
  
  grammar.clear();
  for (int i = 0; i != threads; ++ i) {
    if (grammar.empty())
      grammar.swap(tasks[i].grammar);
    else
      grammar.insert(tasks[i].grammar.begin(), tasks[i].grammar.end());
  }
}

inline
bool is_sgml_tag(const symbol_type& symbol)
{
  const size_t size = symbol.size();
  return size != 0 && symbol[0] == '<' && symbol[size - 1] == '>';
}

void write_characters(const path_type& file,
		      const ngram_count_set_type& model,
		      const ngram_count_set_type& backoff,
		      const double cutoff)
{
  typedef std::vector<const ngram_count_set_type::value_type*, std::allocator<const ngram_count_set_type::value_type*> > sorted_type;
  typedef utils::unordered_map<ngram_type, sorted_type, boost::hash<ngram_type>, std::equal_to<ngram_type>,
			       std::allocator<std::pair<const ngram_type, sorted_type> > >::type sorted_map_type;
  
  utils::compress_ostream os(file, 1024 * 1024);
  os.precision(10);
  
  sorted_map_type sorted;
  
  ngram_count_set_type::const_iterator biter_end = backoff.end();
  for (ngram_count_set_type::const_iterator biter = backoff.begin(); biter != biter_end; ++ biter)
    sorted[ngram_type(biter->first.begin(), biter->first.end() - 1)].push_back(&(*biter));
  
  for (int order = 1; order <= 2; ++ order) {
    sorted_map_type::iterator biter_end = sorted.end();
    for (sorted_map_type::iterator biter = sorted.begin(); biter != biter_end; ++ biter)
      if (static_cast<int>(biter->first.size()) == order - 1) {
	std::sort(biter->second.begin(), biter->second.end(), greater_ptr_second<ngram_count_set_type::value_type>());
	
	sorted_type::const_iterator siter_end = biter->second.end();
	for (sorted_type::const_iterator siter = biter->second.begin(); siter != siter_end; ++ siter)
	  os << "backoff: " << (*siter)->first << ' ' << (*siter)->second << '\n';
      }
  }
  
  sorted.clear();
  ngram_count_set_type::const_iterator miter_end = model.end();
  for (ngram_count_set_type::const_iterator miter = model.begin(); miter != miter_end; ++ miter)
    sorted[ngram_type(miter->first.begin(), miter->first.end() - 1)].push_back(&(*miter));
  
  for (int order = 1; order <= 3; ++ order) {
    sorted_map_type::iterator miter_end = sorted.end();
    for (sorted_map_type::iterator miter = sorted.begin(); miter != miter_end; ++ miter)
      if (static_cast<int>(miter->first.size()) == order - 1) {
	std::sort(miter->second.begin(), miter->second.end(), greater_ptr_second<ngram_count_set_type::value_type>());
	
	sorted_type::const_iterator siter_end = miter->second.end();
	for (sorted_type::const_iterator siter = miter->second.begin(); siter != siter_end; ++ siter)
	  os << "model: " << (*siter)->first << ' ' << (*siter)->second << '\n';
      }
  }
}

void grammar_prune(grammar_type& grammar, const double cutoff)
{
  typedef std::vector<const grammar_type::value_type*, std::allocator<const grammar_type::value_type*> > sorted_type;
  
  typedef std::pair<rule_ptr_type, weight_type> rule_logprob_type;
  typedef utils::unordered_map<symbol_type, rule_logprob_type, boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, rule_logprob_type> > >::type reachable_set_type;
  // we will first compute "reachable" rules...
  // reachable label -> rule mapping
  
  count_set_type counts;
  grammar_type::const_iterator giter_end = grammar.end();
  for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter)
    counts[giter->first->lhs].insert(*giter);
  
  reachable_set_type reachables;
  reachable_set_type reachables_next;
  
  reachables[goal];
  
  for (;;) {
    bool equilibrate = true;
    
    reachables_next.clear();
    reachables_next = reachables;
    
    reachable_set_type::const_iterator riter_end = reachables.end();
    for (reachable_set_type::const_iterator riter = reachables.begin(); riter != riter_end; ++ riter) {
      count_set_type::const_iterator citer = counts.find(riter->first);
      if (citer == counts.end()) continue; // ignore lexical rule, preterminals
      
      const grammar_type& grammar = citer->second;
      
      grammar_type::const_iterator giter_end = grammar.end();
      for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter) {
      
	symbol_set_type::const_iterator siter_end = giter->first->rhs.end();
	for (symbol_set_type::const_iterator siter = giter->first->rhs.begin(); siter != siter_end; ++ siter)
	  if (siter->is_non_terminal()) {
	    // we will keep the best rule....
	    std::pair<reachable_set_type::iterator, bool> result = reachables_next.insert(std::make_pair(*siter, *giter));
	    if (result.second)
	      equilibrate = false;
	    else if (giter->second > result.first->second.second)
	      result.first->second = *giter;
	  }
      }
    }
    
    reachables.swap(reachables_next);
    
    if (equilibrate) break;
  }
  
  // reachables are set of rules we "must" preserve, and should not be pruned away...
  
  grammar.clear();
  reachable_set_type::const_iterator riter_end = reachables.end();
  for (reachable_set_type::const_iterator riter = reachables.begin(); riter != riter_end; ++ riter)
    if (riter->second.first != rule_ptr_type())
      grammar.insert(riter->second);
  
  const weight_type logcutoff(cutoff);
  sorted_type sorted;
  
  count_set_type::const_iterator citer_end = counts.end();
  for (count_set_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer) {
    const grammar_type& grammar_local = citer->second;
    
    sorted.clear();
    grammar_type::const_iterator giter_end = grammar_local.end();
    for (grammar_type::const_iterator giter = grammar_local.begin(); giter != giter_end; ++ giter)
      sorted.push_back(&(*giter));
    
    std::sort(sorted.begin(), sorted.end(), greater_ptr_second<grammar_type::value_type>());
    
    const weight_type logprob_max = sorted.front()->second;
    const weight_type logprob_threshold = logprob_max * logcutoff;
    
    sorted_type::const_iterator siter_end = sorted.end();
    for (sorted_type::const_iterator siter = sorted.begin(); siter != siter_end && (*siter)->second >= logprob_threshold; ++ siter)
      grammar.insert(*(*siter));
  }
}

void lexicon_prune(grammar_type& grammar, const double cutoff)
{
  typedef std::vector<const grammar_type::value_type*, std::allocator<const grammar_type::value_type*> > sorted_type;

  typedef std::pair<rule_ptr_type, weight_type> rule_logprob_type;
  typedef utils::unordered_map<symbol_type, rule_logprob_type, boost::hash<symbol_type>, std::equal_to<symbol_type>,
			       std::allocator<std::pair<const symbol_type, rule_logprob_type> > >::type reachable_set_type;
  
  // compute reachables...
  reachable_set_type reachables;
  
  count_set_type counts;
  grammar_type::const_iterator giter_end = grammar.end();
  for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter) {
    counts[giter->first->lhs].insert(*giter);
    
    const symbol_type& terminal = giter->first->rhs.front();
    std::pair<reachable_set_type::iterator, bool> result = reachables.insert(std::make_pair(terminal, *giter));
    if (! result.second && giter->second > result.first->second.second)
      result.first->second = *giter;
  }
  
  grammar.clear();
  reachable_set_type::const_iterator riter_end = reachables.end();
  for (reachable_set_type::const_iterator riter = reachables.begin(); riter != riter_end; ++ riter)
    if (riter->second.first != rule_ptr_type())
      grammar.insert(riter->second);
  
  const weight_type logcutoff(cutoff);
  sorted_type sorted;
  
  count_set_type::const_iterator citer_end = counts.end();
  for (count_set_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer) {
    const grammar_type& grammar_local = citer->second;
    
    sorted.clear();
    grammar_type::const_iterator giter_end = grammar_local.end();
    for (grammar_type::const_iterator giter = grammar_local.begin(); giter != giter_end; ++ giter)
      sorted.push_back(&(*giter));
    
    std::sort(sorted.begin(), sorted.end(), greater_ptr_second<grammar_type::value_type>());
    
    const weight_type logprob_max = sorted.front()->second;
    const weight_type logprob_threshold = logprob_max * logcutoff;
    
    sorted_type::const_iterator siter_end = sorted.end();
    for (sorted_type::const_iterator siter = sorted.begin(); siter != siter_end && (*siter)->second >= logprob_threshold; ++ siter)
      grammar.insert(*(*siter));
  }
}

void write_grammar(const path_type& file,
		   const grammar_type& grammar)
{
  typedef std::vector<const grammar_type::value_type*, std::allocator<const grammar_type::value_type*> > sorted_type;

  if (grammar.empty()) return;
  
  count_set_type counts;
  sorted_type sorted;
    
  grammar_type::const_iterator giter_end = grammar.end();
  for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter)
    counts[giter->first->lhs][giter->first] = giter->second;

  utils::compress_ostream os(file, 1024 * 1024);
  os.precision(10);
    
  count_set_type::const_iterator citer_end = counts.end();
  for (count_set_type::const_iterator citer = counts.begin(); citer != citer_end; ++ citer) {
    const grammar_type& grammar = citer->second;
      
    sorted.clear();
      
    grammar_type::const_iterator giter_end = grammar.end();
    for (grammar_type::const_iterator giter = grammar.begin(); giter != giter_end; ++ giter)
      sorted.push_back(&(*giter));
      
    std::sort(sorted.begin(), sorted.end(), greater_ptr_second<grammar_type::value_type>());
    
    sorted_type::const_iterator siter_end = sorted.end();
    for (sorted_type::const_iterator siter = sorted.begin(); siter != siter_end; ++ siter)
      os << *((*siter)->first) << " ||| ||| " << (*siter)->second << '\n';
  }
}


void read_treebank(const path_set_type& files,
		   treebank_set_type& treebanks)
{
  hypergraph_type treebank;
  
  path_set_type::const_iterator fiter_end = files.end();
  for (path_set_type::const_iterator fiter = files.begin(); fiter != fiter_end; ++ fiter) {
    utils::compress_istream is(*fiter, 1024 * 1024);
    
    while (is >> treebank) {
      if (! treebank.is_valid()) continue;
      
      if (binarize_left)
	cicada::binarize_left(treebank, 0);
      else if (binarize_right)
	cicada::binarize_right(treebank, 0);
      else if (binarize_all)
	cicada::binarize_all(treebank);
      
      treebanks.push_back(treebank_type(treebank));
    }
  }
  
  if (debug)
    std::cerr << "# of treebank: " << treebanks.size() << std::endl;

}

void options(int argc, char** argv)
{
  namespace po = boost::program_options;
  
  po::options_description desc("options");
  
  desc.add_options()
    ("input",  po::value<path_set_type>(&input_files), "input treebank")
    ("output-grammar",   po::value<path_type>(&output_grammar_file),   "output grammar")
    ("output-lexicon",   po::value<path_type>(&output_lexicon_file),   "output lexical rules")
    ("output-pos",       po::value<path_type>(&output_pos_file),       "output pos rules")
    ("output-character", po::value<path_type>(&output_character_file), "output character model")

    ("goal", po::value<symbol_type>(&goal)->default_value(goal), "goal")
    
    ("max-iteration",       po::value<int>(&max_iteration)->default_value(max_iteration),             "maximum split/merge iterations")
    ("max-iteration-split", po::value<int>(&max_iteration_split)->default_value(max_iteration_split), "maximum EM iterations after split")
    ("max-iteration-merge", po::value<int>(&max_iteration_merge)->default_value(max_iteration_merge), "maximum EM iterations after merge")
    ("min-iteration-split", po::value<int>(&min_iteration_split)->default_value(min_iteration_split), "minimum EM iterations after split")
    ("min-iteration-merge", po::value<int>(&min_iteration_merge)->default_value(min_iteration_merge), "minimum EM iterations after merge")
    
    ("binarize-left",  po::bool_switch(&binarize_left),  "left binarization")
    ("binarize-right", po::bool_switch(&binarize_right), "right binarization")
    ("binarize-all",   po::bool_switch(&binarize_all),   "all binarization")
    
    ("prior-rule",      po::value<double>(&prior_rule)->default_value(prior_rule),           "Dirichlet prior for rules")
    ("prior-lexicon",   po::value<double>(&prior_lexicon)->default_value(prior_lexicon),     "Dirichlet prior for lexical rule")
    ("prior-unknown",   po::value<double>(&prior_unknown)->default_value(prior_unknown),     "Dirichlet prior for unknown")
    ("prior-signature", po::value<double>(&prior_signature)->default_value(prior_signature), "Dirichlet prior for signature")
    ("prior-character", po::value<double>(&prior_character)->default_value(prior_character), "Dirichlet prior for character")

    ("cutoff-rule",      po::value<double>(&cutoff_rule)->default_value(cutoff_rule),           "cutoff for rules")
    ("cutoff-lexicon",   po::value<double>(&cutoff_lexicon)->default_value(cutoff_lexicon),     "cutoff for lexical rule")
    ("cutoff-character", po::value<double>(&cutoff_character)->default_value(cutoff_character), "cutoff for character")
    
    ("merge-ratio",   po::value<double>(&merge_ratio)->default_value(merge_ratio),     "merging ratio")
    
    ("unknown-ratio",     po::value<double>(&unknown_ratio)->default_value(unknown_ratio),         "unknown word ratio")
    ("unknown-threshold", po::value<double>(&unknown_threshold)->default_value(unknown_threshold), "unknown word threshold")
    
    ("signature",      po::value<std::string>(&signature), "signature for unknown word")
    ("signature-list", po::bool_switch(&signature_list),   "list of signatures")
    
    ("threads", po::value<int>(&threads), "# of threads")
    
    ("debug", po::value<int>(&debug)->implicit_value(1), "debug level")
    ("help", "help message");
  
  po::variables_map variables;
  
  po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style & (~po::command_line_style::allow_guessing)), variables);
  
  po::notify(variables);
  
  if (variables.count("help")) {
    std::cout << argv[0] << " [options]\n"
	      << desc << std::endl;
    exit(0);
  }
}

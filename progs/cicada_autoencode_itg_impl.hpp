//
//  Copyright(C) 2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA_AUTOENCOE_ITG_IMPL__HPP__
#define __CICADA_AUTOENCOE_ITG_IMPL__HPP__ 1

#include <cstdlib>
#include <cmath>
#include <climits>

#define BOOST_SPIRIT_THREADSAFE
#define PHOENIX_THREADSAFE


#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/phoenix_core.hpp>

#include <Eigen/Core>

#include "cicada/symbol.hpp"
#include "cicada/sentence.hpp"
#include "cicada/vocab.hpp"
#include "cicada/alignment.hpp"
#include "cicada/bitext.hpp"

#include "utils/alloc_vector.hpp"
#include "utils/lexical_cast.hpp"
#include "utils/bichart.hpp"
#include "utils/bithack.hpp"
#include "utils/compact_map.hpp"
#include "utils/compact_set.hpp"
#include "utils/mathop.hpp"
#include "utils/unordered_map.hpp"
#include "utils/repository.hpp"
#include "utils/compress_stream.hpp"
#include "utils/vector2.hpp"

#include "codec/lz4.hpp"

#include <boost/random.hpp>

struct Average
{
  Average() : average_(0), count_(0) {}
  Average(const double& x) : average_(x), count_(1) {}
  
  Average& operator+=(const double& x)
  {
    average_ += (x - average_) / (++ count_);
    return *this;
  }
  
  Average& operator+=(const Average& x)
  {
    const uint64_t total = count_ + x.count_;
    
    average_ = average_ * (double(count_) / total) + x.average_ * (double(x.count_) / total);
    count_ = total;
    
    return *this;
  }
  
  operator const double&() const { return average_; }
  
  double   average_;
  uint64_t count_;
};

struct Gradient
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;

  // we use float for the future compatibility with GPU :)
  typedef float parameter_type;
  typedef Eigen::Matrix<parameter_type, Eigen::Dynamic, Eigen::Dynamic> tensor_type;
  
  typedef cicada::Sentence  sentence_type;
  typedef cicada::Alignment alignment_type;
  typedef cicada::Bitext    bitext_type;
  typedef cicada::Symbol    word_type;
  typedef cicada::Vocab     vocab_type;
  
  typedef utils::unordered_map<word_type, tensor_type,
			       boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<std::pair<const word_type, tensor_type> > >::type embedding_type;
  
  Gradient() : embedding_(0), hidden_(0), window_(0), count_(0), shared_(0) {}
  Gradient(const size_type& embedding,
	   const size_type& hidden,
	   const size_type& window) 
    : embedding_(embedding),
      hidden_(hidden),
      window_(window),
      count_(0),
      shared_(0)
  { initialize(embedding, hidden, window); }

  Gradient& operator-=(const Gradient& x)
  {
    decrement(source_, x.source_);
    decrement(target_, x.target_);

    if (! Wc_.rows())
      Wc_ = tensor_type::Zero(x.Wc_.rows(), x.Wc_.cols());
    if (! bc_.rows())
      bc_ = tensor_type::Zero(x.bc_.rows(), x.bc_.cols());

    if (! Ws1_.rows())
      Ws1_ = tensor_type::Zero(x.Ws1_.rows(), x.Ws1_.cols());
    if (! bs1_.rows())
      bs1_ = tensor_type::Zero(x.bs1_.rows(), x.bs1_.cols());
    if (! Ws2_.rows())
      Ws2_ = tensor_type::Zero(x.Ws2_.rows(), x.Ws2_.cols());
    if (! bs2_.rows())
      bs2_ = tensor_type::Zero(x.bs2_.rows(), x.bs2_.cols());

    if (! Wi1_.rows())
      Wi1_ = tensor_type::Zero(x.Wi1_.rows(), x.Wi1_.cols());
    if (! bi1_.rows())
      bi1_ = tensor_type::Zero(x.bi1_.rows(), x.bi1_.cols());
    if (! Wi2_.rows())
      Wi2_ = tensor_type::Zero(x.Wi2_.rows(), x.Wi2_.cols());
    if (! bi2_.rows())
      bi2_ = tensor_type::Zero(x.bi2_.rows(), x.bi2_.cols());

    if (! Wt1_.rows())
      Wt1_ = tensor_type::Zero(x.Wt1_.rows(), x.Wt1_.cols());
    if (! bt1_.rows())
      bt1_ = tensor_type::Zero(x.bt1_.rows(), x.bt1_.cols());
    if (! Wt2_.rows())
      Wt2_ = tensor_type::Zero(x.Wt2_.rows(), x.Wt2_.cols());
    if (! bt2_.rows())
      bt2_ = tensor_type::Zero(x.bt2_.rows(), x.bt2_.cols());
    
    Wc_ -= x.Wc_;
    bc_ -= x.bc_;

    Ws1_ -= x.Ws1_;
    bs1_ -= x.bs1_;
    Ws2_ -= x.Ws2_;
    bs2_ -= x.bs2_;
    
    Wi1_ -= x.Wi1_;
    bi1_ -= x.bi1_;
    Wi2_ -= x.Wi2_;
    bi2_ -= x.bi2_;
    
    Wt1_ -= x.Wt1_;
    bt1_ -= x.bt1_;    
    Wt2_ -= x.Wt2_;
    bt2_ -= x.bt2_;    
    
    count_ -= x.count_;

    return *this;
  }
  
  Gradient& operator+=(const Gradient& x)
  {
    increment(source_, x.source_);
    increment(target_, x.target_);

    if (! Wc_.rows())
      Wc_ = tensor_type::Zero(x.Wc_.rows(), x.Wc_.cols());
    if (! bc_.rows())
      bc_ = tensor_type::Zero(x.bc_.rows(), x.bc_.cols());

    if (! Ws1_.rows())
      Ws1_ = tensor_type::Zero(x.Ws1_.rows(), x.Ws1_.cols());
    if (! bs1_.rows())
      bs1_ = tensor_type::Zero(x.bs1_.rows(), x.bs1_.cols());
    if (! Ws2_.rows())
      Ws2_ = tensor_type::Zero(x.Ws2_.rows(), x.Ws2_.cols());
    if (! bs2_.rows())
      bs2_ = tensor_type::Zero(x.bs2_.rows(), x.bs2_.cols());

    if (! Wi1_.rows())
      Wi1_ = tensor_type::Zero(x.Wi1_.rows(), x.Wi1_.cols());
    if (! bi1_.rows())
      bi1_ = tensor_type::Zero(x.bi1_.rows(), x.bi1_.cols());
    if (! Wi2_.rows())
      Wi2_ = tensor_type::Zero(x.Wi2_.rows(), x.Wi2_.cols());
    if (! bi2_.rows())
      bi2_ = tensor_type::Zero(x.bi2_.rows(), x.bi2_.cols());

    if (! Wt1_.rows())
      Wt1_ = tensor_type::Zero(x.Wt1_.rows(), x.Wt1_.cols());
    if (! bt1_.rows())
      bt1_ = tensor_type::Zero(x.bt1_.rows(), x.bt1_.cols());
    if (! Wt2_.rows())
      Wt2_ = tensor_type::Zero(x.Wt2_.rows(), x.Wt2_.cols());
    if (! bt2_.rows())
      bt2_ = tensor_type::Zero(x.bt2_.rows(), x.bt2_.cols());


    Wc_ += x.Wc_;
    bc_ += x.bc_;

    Ws1_ += x.Ws1_;
    bs1_ += x.bs1_;
    Ws2_ += x.Ws2_;
    bs2_ += x.bs2_;
    
    Wi1_ += x.Wi1_;
    bi1_ += x.bi1_;
    Wi2_ += x.Wi2_;
    bi2_ += x.bi2_;
    
    Wt1_ += x.Wt1_;
    bt1_ += x.bt1_;    
    Wt2_ += x.Wt2_;
    bt2_ += x.bt2_;    
    
    count_ += x.count_;
    
    return *this;
  }
  
  void clear()
  {
    // embedding
    source_.clear();
    target_.clear();

    Wc_.setZero();
    bc_.setZero();

    Ws1_.setZero();
    bs1_.setZero();
    Ws2_.setZero();
    bs2_.setZero();
    
    Wi1_.setZero();
    bi1_.setZero();
    Wi2_.setZero();
    bi2_.setZero();

    Wt1_.setZero();
    bt1_.setZero();
    Wt2_.setZero();
    bt2_.setZero();
    
    count_ = 0;
    shared_ = 0;
  }

  
  tensor_type& source(const word_type& word)
  {
    tensor_type& embedding = source_[word];
    if (! embedding.cols())
      embedding = tensor_type::Zero(embedding_, 1);
    
    return embedding;
  }

  tensor_type& target(const word_type& word)
  {
    tensor_type& embedding = target_[word];
    if (! embedding.cols())
      embedding = tensor_type::Zero(embedding_, 1);
    
    return embedding;
  }
  
  void initialize(const size_type embedding, const size_type hidden, const size_type window)
  {
    if (hidden <= 0)
      throw std::runtime_error("invalid dimension");
    if (embedding <= 0)
      throw std::runtime_error("invalid dimension");
    
    embedding_ = embedding;
    hidden_    = hidden;
    window_    = window;

    const size_type leaf_size = embedding * (window * 2 + 1) * 2;
    
    clear();
    
    Wc_ = tensor_type::Zero(embedding, hidden_);
    bc_ = tensor_type::Zero(embedding, 1);
    
    Ws1_ = tensor_type::Zero(hidden_, hidden_ + hidden_);
    bs1_ = tensor_type::Zero(hidden_, 1);

    Ws2_ = tensor_type::Zero(hidden_ + hidden_, hidden_);
    bs2_ = tensor_type::Zero(hidden_ + hidden_, 1);

    Wi1_ = tensor_type::Zero(hidden_, hidden_ + hidden_);
    bi1_ = tensor_type::Zero(hidden_, 1);
    
    Wi2_ = tensor_type::Zero(hidden_ + hidden_,  hidden_);
    bi2_ = tensor_type::Zero(hidden_ + hidden_, 1);
    
    Wt1_ = tensor_type::Zero(hidden_, leaf_size);
    bt1_ = tensor_type::Zero(hidden_, 1);

    Wt2_ = tensor_type::Zero(leaf_size, hidden_);
    bt2_ = tensor_type::Zero(leaf_size, 1);
    
    count_ = 0;
    shared_ = 0;
  }
  
private:
  void increment(embedding_type& embedding, const embedding_type& x)
  {
    embedding_type::const_iterator siter_end = x.end();
    for (embedding_type::const_iterator siter = x.begin(); siter != siter_end; ++ siter) {
      tensor_type& matrix = embedding[siter->first];
      
      if (! matrix.rows())
	matrix = siter->second;
      else
	matrix += siter->second;
    }
  }
  
  void decrement(embedding_type& embedding, const embedding_type& x)
  {
    embedding_type::const_iterator siter_end = x.end();
    for (embedding_type::const_iterator siter = x.begin(); siter != siter_end; ++ siter) {
      tensor_type& matrix = embedding[siter->first];
      
      if (! matrix.rows())
	matrix = - siter->second;
      else
	matrix += siter->second;
    }
  }
  
public:
  void increment()
  {
    utils::atomicop::add_and_fetch(shared_, size_type(1));
  }

  size_type shared() const
  {
    const size_type ret = shared_;
    utils::atomicop::memory_barrier();
    return ret;
  }

public:
  
  typedef std::vector<char, std::allocator<char> > buffer_type;

  buffer_type buffer_;
  
  void encode(std::string& encoded) const
  {
    buffer_type& buffer = const_cast<buffer_type&>(buffer_);
    buffer.clear();
    
    {
      boost::iostreams::filtering_ostream os;
      os.push(codec::lz4_compressor());
      os.push(boost::iostreams::back_insert_device<buffer_type>(buffer));
      
      write(os);
    }

    encoded = std::string(buffer.begin(), buffer.end());
  }

  void decode(const std::string& encoded) 
  {
    boost::iostreams::filtering_istream is;
    is.push(codec::lz4_decompressor());
    is.push(boost::iostreams::array_source(&(*encoded.begin()), encoded.size()));
    
    read(is);
  }

  friend
  std::ostream& operator<<(std::ostream& os, const Gradient& x)
  {
    x.write(os);
    return os;
  }

  friend
  std::istream& operator>>(std::istream& is, Gradient& x)
  {
    x.read(is);
    return is;
  }

private:
  void write(std::ostream& os) const
  {
    os.write((char*) &embedding_, sizeof(size_type));
    os.write((char*) &hidden_,    sizeof(size_type));
    os.write((char*) &window_,    sizeof(size_type));
    os.write((char*) &count_,     sizeof(size_type));
    
    write(os, Wc_);
    write(os, bc_);

    write(os, Ws1_);
    write(os, bs1_);
    write(os, Ws2_);
    write(os, bs2_);

    write(os, Wi1_);
    write(os, bi1_);
    write(os, Wi2_);
    write(os, bi2_);

    write(os, Wt1_);
    write(os, bt1_);
    write(os, Wt2_);
    write(os, bt2_);
    
    write(os, source_);
    write(os, target_);
  }
  
  void read(std::istream& is)
  {
    clear();
    
    is.read((char*) &embedding_, sizeof(size_type));
    is.read((char*) &hidden_,    sizeof(size_type));
    is.read((char*) &window_,    sizeof(size_type));
    is.read((char*) &count_,     sizeof(size_type));

    read(is, Wc_);
    read(is, bc_);

    read(is, Ws1_);
    read(is, bs1_);
    read(is, Ws2_);
    read(is, bs2_);

    read(is, Wi1_);
    read(is, bi1_);
    read(is, Wi2_);
    read(is, bi2_);

    read(is, Wt1_);
    read(is, bt1_);
    read(is, Wt2_);
    read(is, bt2_);
    
    read(is, source_);
    read(is, target_);
  }

  void write(std::ostream& os, const embedding_type& embedding) const
  {
    const size_type size = embedding.size();
    
    os.write((char*) &size, sizeof(size_type));
    
    embedding_type::const_iterator eiter_end = embedding.end();
    for (embedding_type::const_iterator eiter = embedding.begin(); eiter != eiter_end; ++ eiter) {
      const size_type word_size = eiter->first.size();
      
      os.write((char*) &word_size, sizeof(size_type));
      os.write((char*) &(*eiter->first.begin()), word_size);
      os.write((char*) eiter->second.data(), sizeof(tensor_type::Scalar) * eiter->second.rows());
    }
  }

  void read(std::istream& is, embedding_type& embedding)
  {
    buffer_type& buffer = const_cast<buffer_type&>(buffer_);
    
    embedding.clear();
    
    size_type size = 0;
    
    is.read((char*) &size, sizeof(size_type));
    
    for (size_type i = 0; i != size; ++ i) {
      size_type word_size = 0;
      is.read((char*) &word_size, sizeof(size_type));
      
      buffer.resize(word_size);
      is.read((char*) &(*buffer.begin()), word_size);
      
      tensor_type& matrix = embedding[word_type(buffer.begin(), buffer.end())];
      
      matrix.resize(embedding_, 1);
      
      is.read((char*) matrix.data(), sizeof(tensor_type::Scalar) * matrix.rows());
    }
  }

  void write(std::ostream& os, const tensor_type& matrix) const
  {
    const tensor_type::Index rows = matrix.rows();
    const tensor_type::Index cols = matrix.cols();

    os.write((char*) &rows, sizeof(tensor_type::Index));
    os.write((char*) &cols, sizeof(tensor_type::Index));
    
    os.write((char*) matrix.data(), sizeof(tensor_type::Scalar) * rows * cols);
  }
  
  void read(std::istream& is, tensor_type& matrix)
  {
    tensor_type::Index rows = 0;
    tensor_type::Index cols = 0;
    
    is.read((char*) &rows, sizeof(tensor_type::Index));
    is.read((char*) &cols, sizeof(tensor_type::Index));
    
    matrix.resize(rows, cols);
    
    is.read((char*) matrix.data(), sizeof(tensor_type::Scalar) * rows * cols);
  }
  
public:
  // dimension...
  size_type embedding_;
  size_type hidden_;
  size_type window_;
  
  // embedding
  embedding_type source_;
  embedding_type target_;

  // classification
  tensor_type Wc_;
  tensor_type bc_;

  // straight
  tensor_type Ws1_;
  tensor_type bs1_;
  tensor_type Ws2_;
  tensor_type bs2_;

  // inversion
  tensor_type Wi1_;
  tensor_type bi1_;  
  tensor_type Wi2_;
  tensor_type bi2_;  
  
  // terminal
  tensor_type Wt1_;
  tensor_type bt1_;
  tensor_type Wt2_;
  tensor_type bt2_;
  
  size_type count_;
  size_type shared_;
};


struct Model
{
  // model parameters
  
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  // we use float for the future compatibility with GPU :)
  typedef float parameter_type;
  typedef Eigen::Matrix<parameter_type, Eigen::Dynamic, Eigen::Dynamic> tensor_type;
  
  typedef Gradient gradient_type;
  
  typedef cicada::Sentence  sentence_type;
  typedef cicada::Alignment alignment_type;
  typedef cicada::Bitext    bitext_type;
  typedef cicada::Symbol    word_type;
  typedef cicada::Vocab     vocab_type;
  
  typedef boost::filesystem::path path_type;

  typedef std::vector<bool, std::allocator<bool> > word_unique_type;
  
  Model() : embedding_(0), hidden_(0), window_(0), scale_(1) {}  
  template <typename Words, typename Gen>
  Model(const size_type& embedding,
	const size_type& hidden,
	const size_type& window,
	Words& words_source,
	Words& words_target,
	Gen& gen) 
    : embedding_(embedding),
      hidden_(hidden),
      window_(window),
      scale_(1)
  { initialize(embedding, hidden, window, words_source, words_target, gen); }
  
  void clear()
  {
    // embedding
    source_.setZero();
    target_.setZero();

    Wc_.setZero();
    bc_.setZero();

    Ws1_.setZero();
    bs1_.setZero();
    Ws2_.setZero();
    bs2_.setZero();
    
    Wi1_.setZero();
    bi1_.setZero();
    Wi2_.setZero();
    bi2_.setZero();

    Wt1_.setZero();
    bt1_.setZero();
    Wt2_.setZero();
    bt2_.setZero();
    
    scale_ = 1.0;
  }
  
  
  template <typename Gen>
  struct randomize
  {
    randomize(Gen& gen, const double range=0.01) : gen_(gen), range_(range) {}
    
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return boost::random::uniform_real_distribution<Tp>(-range_, range_)(const_cast<Gen&>(gen_));
    }
    
    Gen& gen_;
    double range_;
  };
  
  template <typename Words, typename Gen>
  void initialize(const size_type embedding,
		  const size_type hidden,
		  const size_type window,
		  Words& words_source,
		  Words& words_target,
		  Gen& gen)
  {
    if (hidden <= 0)
      throw std::runtime_error("invalid dimension");
    if (embedding <= 0)
      throw std::runtime_error("invalid dimension");
    
    embedding_ = embedding;
    hidden_    = hidden;
    window_    = window;
    
    clear();
    
    const size_type vocabulary_size = word_type::allocated();
    const size_type leaf_size = embedding * (window * 2 + 1) * 2;
    
    const double range_e = std::sqrt(6.0 / (embedding_ + 1));
    const double range_c = std::sqrt(6.0 / (embedding_ + hidden_));
    const double range_s = std::sqrt(6.0 / (hidden_ + hidden_ + hidden_));
    const double range_i = std::sqrt(6.0 / (hidden_ + hidden_ + hidden_));
    const double range_t = std::sqrt(6.0 / (hidden + leaf_size));

    source_ = tensor_type::Zero(embedding_, vocabulary_size).array().unaryExpr(randomize<Gen>(gen, range_e));
    target_ = tensor_type::Zero(embedding_, vocabulary_size).array().unaryExpr(randomize<Gen>(gen, range_e));

    Wc_ = tensor_type::Zero(embedding, hidden_).array().unaryExpr(randomize<Gen>(gen, range_c));
    bc_ = tensor_type::Zero(embedding, 1);
    
    Ws1_ = tensor_type::Zero(hidden_, hidden_ + hidden_).array().unaryExpr(randomize<Gen>(gen, range_s));
    bs1_ = tensor_type::Zero(hidden_, 1);
    Ws2_ = tensor_type::Zero(hidden_ + hidden_,  hidden_).array().unaryExpr(randomize<Gen>(gen, range_s));
    bs2_ = tensor_type::Zero(hidden_ + hidden_, 1);
    
    Wi1_ = tensor_type::Zero(hidden_, hidden_ + hidden_).array().unaryExpr(randomize<Gen>(gen, range_i));
    bi1_ = tensor_type::Zero(hidden_, 1);
    Wi2_ = tensor_type::Zero(hidden_ + hidden_, hidden_).array().unaryExpr(randomize<Gen>(gen, range_i));
    bi2_ = tensor_type::Zero(hidden_ + hidden_, 1);
    
    Wt1_ = tensor_type::Zero(hidden_, leaf_size).array().unaryExpr(randomize<Gen>(gen, range_t));
    bt1_ = tensor_type::Zero(hidden_, 1);
    Wt2_ = tensor_type::Zero(leaf_size, hidden_).array().unaryExpr(randomize<Gen>(gen, range_t));
    bt2_ = tensor_type::Zero(leaf_size, 1);
    
    words_source_.clear();
    words_target_.clear();
    words_source_.resize(vocabulary_size, false);
    words_target_.resize(vocabulary_size, false);

    words_source_[vocab_type::EPSILON.id()] = true;
    words_target_[vocab_type::EPSILON.id()] = true;
    words_source_[vocab_type::BOS.id()] = true;
    words_target_[vocab_type::BOS.id()] = true;
    words_source_[vocab_type::EOS.id()] = true;
    words_target_[vocab_type::EOS.id()] = true;

    for (typename Words::const_iterator siter = words_source.begin(); siter != words_source.end(); ++ siter)
      words_source_[siter->id()] = true;
    for (typename Words::const_iterator titer = words_target.begin(); titer != words_target.end(); ++ titer)
      words_target_[titer->id()] = true;
    
    scale_ = 1.0;
  }

  void finalize()
  {
    if (scale_ == 1.0) return;
    
    source_ *= scale_;
    target_ *= scale_;

    scale_ = 1.0;
  }

public:
  typedef std::vector<char, std::allocator<char> > buffer_type;

  buffer_type buffer_;

  void encode(std::string& encoded) const
  {
    buffer_type& buffer = const_cast<buffer_type&>(buffer_);
    buffer.clear();
    
    {
      boost::iostreams::filtering_ostream os;
      os.push(codec::lz4_compressor());
      os.push(boost::iostreams::back_insert_device<buffer_type>(buffer));
      
      write(os);
    }
    
    encoded = std::string(buffer.begin(), buffer.end());
  }

  void decode(const std::string& encoded) 
  {
    boost::iostreams::filtering_istream is;
    is.push(codec::lz4_decompressor());
    is.push(boost::iostreams::array_source(&(*encoded.begin()), encoded.size()));
    
    read(is);
  }

  friend
  std::ostream& operator<<(std::ostream& os, const Model& x)
  {
    x.write(os);
    return os;
  }

  friend
  std::istream& operator>>(std::istream& is, Model& x)
  {
    x.read(is);
    return is;
  }

private:
  void write(std::ostream& os) const
  {
    os.write((char*) &embedding_, sizeof(size_type));
    os.write((char*) &hidden_,    sizeof(size_type));
    os.write((char*) &window_,    sizeof(size_type));
    os.write((char*) &scale_,     sizeof(double));
    
    write(os, Wc_);
    write(os, bc_);

    write(os, Ws1_);
    write(os, bs1_);
    write(os, Ws2_);
    write(os, bs2_);

    write(os, Wi1_);
    write(os, bi1_);
    write(os, Wi2_);
    write(os, bi2_);

    write(os, Wt1_);
    write(os, bt1_);
    write(os, Wt2_);
    write(os, bt2_);
    
    write_embedding(os, source_);
    write_embedding(os, target_);
  }

  void read(std::istream& is)
  {
    clear();
    
    is.read((char*) &embedding_, sizeof(size_type));
    is.read((char*) &hidden_,    sizeof(size_type));
    is.read((char*) &window_,    sizeof(size_type));
    is.read((char*) &scale_,     sizeof(double));
    
    read(is, Wc_);
    read(is, bc_);
    
    read(is, Ws1_);
    read(is, bs1_);
    read(is, Ws2_);
    read(is, bs2_);

    read(is, Wi1_);
    read(is, bi1_);
    read(is, Wi2_);
    read(is, bi2_);

    read(is, Wt1_);
    read(is, bt1_);
    read(is, Wt2_);
    read(is, bt2_);
    
    read_embedding(is, source_);
    read_embedding(is, target_);
    
    // checking...
  }

  void write_embedding(std::ostream& os, const tensor_type& embedding) const
  {
    const size_type rows = embedding.rows();
    const size_type cols = embedding.cols();
    
    os.write((char*) &cols, sizeof(size_type));
    
    for (word_type::id_type id = 0; id != cols; ++ id) {
      const word_type word(id);
      
      const size_type word_size = word.size();
      
      os.write((char*) &word_size, sizeof(size_type));
      os.write((char*) &(*word.begin()), word_size);
      os.write((char*) embedding.col(id).data(), sizeof(tensor_type::Scalar) * rows);
    }
  }

  void read_embedding(std::istream& is, tensor_type& embedding)
  {
    buffer_type& buffer = const_cast<buffer_type&>(buffer_);
    
    size_type cols = 0;
    is.read((char*) &cols, sizeof(size_type));
    
    if (cols > embedding.cols())
      embedding.conservativeResize(embedding_, cols);
    
    for (size_type i = 0; i != cols; ++ i) {
      size_type word_size = 0;
      is.read((char*) &word_size, sizeof(size_type));
      
      buffer.resize(word_size);
      is.read((char*) &(*buffer.begin()), word_size);
      
      const word_type word(buffer.begin(), buffer.end());

      if (word.id() >= embedding.cols())
	embedding.conservativeResize(embedding_, word.id() + 1);
      
      is.read((char*) embedding.col(word.id()).data(), sizeof(tensor_type::Scalar) * (embedding_));
    }
  }

  void write(std::ostream& os, const tensor_type& matrix) const
  {
    const tensor_type::Index rows = matrix.rows();
    const tensor_type::Index cols = matrix.cols();

    os.write((char*) &rows, sizeof(tensor_type::Index));
    os.write((char*) &cols, sizeof(tensor_type::Index));
    
    os.write((char*) matrix.data(), sizeof(tensor_type::Scalar) * rows * cols);
  }

  void read(std::istream& is, tensor_type& matrix)
  {
    tensor_type::Index rows = 0;
    tensor_type::Index cols = 0;
    
    is.read((char*) &rows, sizeof(tensor_type::Index));
    is.read((char*) &cols, sizeof(tensor_type::Index));
    
    matrix.resize(rows, cols);
    
    is.read((char*) matrix.data(), sizeof(tensor_type::Scalar) * rows * cols);
  }
  
  struct real_policy : boost::spirit::karma::real_policies<parameter_type>
  {
    static unsigned int precision(parameter_type)
    {
      return 10;
    }
  };

public:
  void read_embedding(const path_type& source_file, const path_type& target_file)
  {
    // we will overwrite embedding... thus we will not clear embedding_
    
    namespace qi = boost::spirit::qi;
    namespace standard = boost::spirit::standard;
    
    typedef std::vector<parameter_type, std::allocator<parameter_type> > parameter_set_type;
    typedef boost::fusion::tuple<std::string, parameter_set_type > embedding_parsed_type;
    typedef boost::spirit::istream_iterator iterator_type;
    
    qi::rule<iterator_type, std::string(), standard::blank_type>           word;
    qi::rule<iterator_type, embedding_parsed_type(), standard::blank_type> parser; 
    
    word   %= qi::lexeme[+(standard::char_ - standard::space)];
    parser %= word >> *qi::double_ >> (qi::eol | qi::eoi);

    if (! source_file.empty()) {
      
      if (source_file != "-" && ! boost::filesystem::exists(source_file))
	throw std::runtime_error("no embedding: " + source_file.string());
      
      utils::compress_istream is(source_file, 1024 * 1024);
      is.unsetf(std::ios::skipws);
    
      iterator_type iter(is);
      iterator_type iter_end;
      
      embedding_parsed_type parsed;
      
      while (iter != iter_end) {
	boost::fusion::get<0>(parsed).clear();
	boost::fusion::get<1>(parsed).clear();
	
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, parsed))
	  if (iter != iter_end)
	    throw std::runtime_error("embedding parsing failed");
	
	if (boost::fusion::get<1>(parsed).size() != embedding_)
	  throw std::runtime_error("invalid embedding size");

	const word_type word = boost::fusion::get<0>(parsed);
	
	if (word.id() < source_.cols())
	  source_.col(word.id()).block(0, 0, embedding_, 1)
	    = Eigen::Map<const tensor_type>(&(*boost::fusion::get<1>(parsed).begin()), embedding_, 1);
      }
    }

    if (! target_file.empty()) {
      
      if (target_file != "-" && ! boost::filesystem::exists(target_file))
	throw std::runtime_error("no embedding: " + target_file.string());
      
      utils::compress_istream is(target_file, 1024 * 1024);
      is.unsetf(std::ios::skipws);
    
      iterator_type iter(is);
      iterator_type iter_end;
      
      embedding_parsed_type parsed;
      
      while (iter != iter_end) {
	boost::fusion::get<0>(parsed).clear();
	boost::fusion::get<1>(parsed).clear();
	
	if (! boost::spirit::qi::phrase_parse(iter, iter_end, parser, standard::blank, parsed))
	  if (iter != iter_end)
	    throw std::runtime_error("embedding parsing failed");
	
	if (boost::fusion::get<1>(parsed).size() != embedding_)
	  throw std::runtime_error("invalid embedding size");
	
	const word_type word = boost::fusion::get<0>(parsed);

	if (word.id() < target_.cols())
	  target_.col(word.id()).block(0, 0, embedding_, 1)
	    = Eigen::Map<const tensor_type>(&(*boost::fusion::get<1>(parsed).begin()), embedding_, 1);
      }
    }
  }
    
  void write(const path_type& path) const
  {
    // we use a repository structure...
    typedef utils::repository repository_type;
    
    namespace karma = boost::spirit::karma;
    namespace standard = boost::spirit::standard;
    
    repository_type rep(path, repository_type::write);
    
    rep["embedding"] = utils::lexical_cast<std::string>(embedding_);
    rep["hidden"]    = utils::lexical_cast<std::string>(hidden_);
    rep["window"]    = utils::lexical_cast<std::string>(window_);
    rep["scale"]     = utils::lexical_cast<std::string>(scale_);
    
    write_embedding(rep.path("source.gz"), rep.path("source.bin"), source_, words_source_);
    write_embedding(rep.path("target.gz"), rep.path("target.bin"), target_, words_target_);

    write(rep.path("Wc.txt.gz"), rep.path("Wc.bin"), Wc_);
    write(rep.path("bc.txt.gz"), rep.path("bc.bin"), bc_);

    write(rep.path("Ws1.txt.gz"), rep.path("Ws1.bin"), Ws1_);
    write(rep.path("bs1.txt.gz"), rep.path("bs1.bin"), bs1_);
    write(rep.path("Ws2.txt.gz"), rep.path("Ws2.bin"), Ws2_);
    write(rep.path("bs2.txt.gz"), rep.path("bs2.bin"), bs2_);
    
    write(rep.path("Wi1.txt.gz"), rep.path("Wi1.bin"), Wi1_);
    write(rep.path("bi1.txt.gz"), rep.path("bi1.bin"), bi1_);
    write(rep.path("Wi2.txt.gz"), rep.path("Wi2.bin"), Wi2_);
    write(rep.path("bi2.txt.gz"), rep.path("bi2.bin"), bi2_);
    
    write(rep.path("Wt1.txt.gz"), rep.path("Wt1.bin"), Wt1_);
    write(rep.path("bt1.txt.gz"), rep.path("bt1.bin"), bt1_);
    write(rep.path("Wt2.txt.gz"), rep.path("Wt2.bin"), Wt2_);
    write(rep.path("bt2.txt.gz"), rep.path("bt2.bin"), bt2_);
    
    // vocabulary...
    vocab_type vocab;

    const word_type::id_type vocabulary_size = utils::bithack::max(words_source_.size(), words_target_.size());
    
    vocab.open(rep.path("vocab"), vocabulary_size >> 1);
    
    for (word_type::id_type id = 0; id != vocabulary_size; ++ id)
      if ((id < words_source_.size() && words_source_[id]) || (id < words_target_.size() && words_target_[id])) {
	const word_type word(id);
	
	vocab.insert(word);
      }
    
    vocab.close();
  }
  
private:
  void write_embedding(const path_type& path_text, const path_type& path_binary, const tensor_type& matrix, const word_unique_type& words) const
  {
    namespace karma = boost::spirit::karma;
    namespace standard = boost::spirit::standard;

    karma::real_generator<double, real_policy> float10;

    const word_type::id_type rows = matrix.rows();
    const word_type::id_type cols = std::min(static_cast<size_type>(matrix.cols()), words.size());
    
    utils::compress_ostream os_txt(path_text, 1024 * 1024);
    utils::compress_ostream os_bin(path_binary, 1024 * 1024);
    std::ostream_iterator<char> iter(os_txt);
    
    for (word_type::id_type id = 0; id != cols; ++ id)  
      if (words[id]) {
	const word_type word(id);
	
	karma::generate(iter, standard::string, word);
	
	for (difference_type j = 0; j != rows; ++ j)
	  karma::generate(iter, karma::lit(' ') << float10, matrix(j, id));
	
	karma::generate(iter, karma::lit('\n'));
	
	os_bin.write((char*) matrix.col(id).data(), sizeof(tensor_type::Scalar) * rows);
      }
  }

  void write(const path_type& path_text, const path_type& path_binary, const tensor_type& matrix) const
  {
    {
      utils::compress_ostream os(path_text, 1024 * 1024);
      os.precision(10);
      os << matrix;
    }
    
    {
      utils::compress_ostream os(path_binary, 1024 * 1024);
      
      const tensor_type::Index rows = matrix.rows();
      const tensor_type::Index cols = matrix.cols();
      
      os.write((char*) matrix.data(), sizeof(tensor_type::Scalar) * rows * cols);
    }
  }
  
public:
  // dimension...
  size_type embedding_;
  size_type hidden_;
  size_type window_;

  word_unique_type words_source_;
  word_unique_type words_target_;
  
  // embedding
  tensor_type source_;
  tensor_type target_;

  tensor_type Wc_;
  tensor_type bc_;

  tensor_type Ws1_;
  tensor_type bs1_;
  tensor_type Ws2_;
  tensor_type bs2_;

  tensor_type Wi1_;
  tensor_type bi1_;
  tensor_type Wi2_;
  tensor_type bi2_;
  
  tensor_type Wt1_;
  tensor_type bt1_;
  tensor_type Wt2_;
  tensor_type bt2_;

  double scale_;
};


struct Dictionary
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef Model    model_type;
  
  typedef uint64_t count_type;
  
  typedef cicada::Sentence  sentence_type;
  typedef cicada::Alignment alignment_type;
  typedef cicada::Symbol    word_type;
  typedef cicada::Vocab     vocab_type;
  
  struct Dict
  {
    typedef utils::compact_map<word_type, double,
			       utils::unassigned<word_type>, utils::unassigned<word_type>,
			       boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<std::pair<const word_type, double> > > logprob_set_type;
    typedef utils::compact_map<word_type, count_type,
			       utils::unassigned<word_type>, utils::unassigned<word_type>,
			       boost::hash<word_type>, std::equal_to<word_type>,
			       std::allocator<std::pair<const word_type, count_type> > > count_set_type;
    
    typedef std::vector<word_type, std::allocator<word_type> >   word_set_type;
    typedef boost::random::discrete_distribution<>               distribution_type;
    
    Dict() {}

    template <typename Tp>
    struct compare_pair
    {
      bool operator()(const Tp& x, const Tp& y) const
      {
	return (x.second > y.second
		|| (x.second == y.second
		    && static_cast<const std::string&>(x.first) < static_cast<const std::string&>(y.first)));
      }
    };
    
    void initialize()
    {
      typedef std::pair<word_type, double> word_prob_type;
      typedef std::vector<word_prob_type, std::allocator<word_prob_type> > word_prob_set_type;
      typedef std::vector<double, std::allocator<double> > prob_set_type;
      
      logprobs_.clear();
      words_.clear();
      
      word_prob_set_type word_probs(counts_.begin(), counts_.end());
      std::sort(word_probs.begin(), word_probs.end(), compare_pair<word_prob_type>());
      
      prob_set_type probs;
      words_.reserve(word_probs.size());
      probs.reserve(word_probs.size());
      
      word_prob_set_type::const_iterator witer_end = word_probs.end();
      for (word_prob_set_type::const_iterator witer = word_probs.begin(); witer != witer_end; ++ witer) {
	words_.push_back(witer->first);
	probs.push_back(witer->second);
	logprobs_[witer->first] = std::log(witer->second);
      }
      
      // initialize distribution
      distribution_ = distribution_type(probs.begin(), probs.end());
    }
    
    double logprob(const word_type& word) const
    {
      logprob_set_type::const_iterator liter = logprobs_.find(word);
      if (liter != logprobs_.end())
	return liter->second;
      else
	return - std::numeric_limits<double>::infinity();
    }
    
    template <typename Gen>
    word_type draw(Gen& gen) const
    {
      return words_[distribution_(gen)];
    }

    count_type& operator[](const word_type& word) { return counts_[word]; }
    
    count_set_type    counts_;
    logprob_set_type  logprobs_;
    word_set_type     words_;
    distribution_type distribution_;
  };

  typedef Dict dict_type;
  typedef utils::alloc_vector<dict_type, std::allocator<dict_type> > dict_set_type;
  
  Dictionary() {}

  dict_type& operator[](const word_type& word) { return dicts_[word.id()]; }

  void swap(Dictionary& x) { dicts_.swap(x.dicts_); }

  void initialize()
  {
    for (size_type i = 0; i != dicts_.size(); ++ i)
      if (dicts_.exists(i))
	dicts_[i].initialize();
  }
  
  void clear()
  {
    dicts_.clear();
  }
  
  double logprob(const word_type& source, const word_type& target) const
  {
    if (dicts_.exists(source.id()))
      return dicts_[source.id()].logprob(target);
    else
      return dicts_[vocab_type::UNK.id()].logprob(target);
  }
  
  template <typename Gen>
  word_type draw(const word_type& source, Gen& gen) const
  {
    if (dicts_.exists(source.id()))
      return dicts_[source.id()].draw(gen);
    else
      return dicts_[vocab_type::UNK.id()].draw(gen);
  }

  dict_set_type dicts_;
};

struct ITG
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef Model    model_type;
  typedef Gradient gradient_type;

  typedef Dictionary dictionary_type;

  typedef model_type::parameter_type parameter_type;
  typedef model_type::tensor_type tensor_type;

  typedef cicada::Bitext    bitext_type;
  typedef cicada::Alignment alignment_type;
  typedef cicada::Vocab     vocab_type;
  
  typedef bitext_type::word_type     word_type;
  typedef bitext_type::sentence_type sentence_type;

  typedef std::vector<parameter_type, std::allocator<parameter_type> > buffer_type;
  
  struct Span
  {
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;

    typedef uint32_t index_type;
    typedef index_type value_type;
    
    Span(const index_type& first, const index_type& last) : first_(first), last_(last) {}
    template <typename _Index>
    Span(const std::pair<_Index, _Index>& x) : first_(x.first), last_(x.second) {}
    Span() : first_(0), last_(0) {}
    
    bool empty() const { return first_ == last_; }
    size_type size() const { return last_ - first_; }

    friend
    bool operator==(const Span& x, const Span& y)
    {
      return x.first_ == y.first_ && x.last_ == y.last_;
    }

    friend
    bool operator!=(const Span& x, const Span& y)
    {
      return x.first_ != y.first_ || x.last_ != y.last_;
    }

    friend
    std::ostream& operator<<(std::ostream& os, const Span& x)
    {
      os << x.first_ << ".." << x.last_;
      return os;
    }
    
    index_type first_;
    index_type last_;
  };
  
  typedef Span span_type;
  
  struct SpanPair
  {
    typedef Span span_type;
    typedef span_type value_type;
    
    typedef span_type::index_type index_type;
    
    SpanPair(const span_type& source, const span_type& target)
      : source_(source), target_(target) {}
    SpanPair(const index_type& w, const index_type& x, const index_type& y, const index_type& z)
      : source_(w, x), target_(y, z) {}
    template <typename _Span>
    SpanPair(const std::pair<_Span, _Span>& x)
      : source_(x.first), target_(x.second) {}
    SpanPair() : source_(), target_() {}

    bool empty() const { return source_.empty() && target_.empty(); }
    size_type size() const { return source_.size() + target_.size(); }

    friend
    bool operator==(const SpanPair& x, const SpanPair& y)
    {
      return x.source_ == y.source_ && x.target_ == y.target_;
    }

    friend
    bool operator!=(const SpanPair& x, const SpanPair& y)
    {
      return x.source_ != y.source_ || x.target_ != y.target_;
    }

    friend
    std::ostream& operator<<(std::ostream& os, const SpanPair& x)
    {
      os << x.source_ << "-" << x.target_;
      return os;
    }
    
    span_type source_;
    span_type target_;
  };
  
  typedef SpanPair span_pair_type;

  struct Hyperedge
  {
    span_pair_type span_;
    span_pair_type left_;
    span_pair_type right_;

    Hyperedge()
      : span_(), left_(), right_() {}
    Hyperedge(const span_pair_type& span)
      : span_(span), left_(), right_() {}
    Hyperedge(const span_pair_type& span, const span_pair_type& left, const span_pair_type& right)
      : span_(span), left_(left), right_(right) {}
    
    bool aligned() const { return ! span_.source_.empty() && ! span_.target_.empty(); }
    bool terminal() const { return left_.empty() && right_.empty(); }
    bool straight() const { return ! terminal() && left_.target_.last_  == right_.target_.first_; }
    bool inverted() const { return ! terminal() && left_.target_.first_ == right_.target_.last_; }
  };

  typedef Hyperedge hyperedge_type;

  struct State
  {
    typedef State state_type;
    
    State() : span_(),
	      left_(),
	      right_(),
	      loss_(std::numeric_limits<double>::infinity()) {}
    State(const span_pair_type& span)
      : span_(span),
	left_(),
	right_(),
	loss_(std::numeric_limits<double>::infinity()) {}
    State(const span_pair_type& span, const span_pair_type left, const span_pair_type& right)
      : span_(span),
	left_(left),
	right_(right),
	loss_(std::numeric_limits<double>::infinity()) {}
    
    bool aligned()  const { return ! span_.source_.empty() && ! span_.target_.empty(); }
    bool terminal() const { return left_.empty() && right_.empty(); }
    bool straight() const { return ! terminal() && left_.target_.last_  == right_.target_.first_; }
    bool inverted() const { return ! terminal() && left_.target_.first_ == right_.target_.last_; }
    
    span_pair_type span_;
    span_pair_type left_;
    span_pair_type right_;
    
    // other state related data
    double loss_;

    tensor_type input_;
    tensor_type layer_;
    tensor_type layer_norm_;
    tensor_type output_;
    tensor_type reconstruction_;
    tensor_type delta_;
  };

  typedef State state_type;
    
  typedef utils::vector2<state_type, std::allocator<state_type> > terminal_set_type;
  typedef utils::bichart<state_type, std::allocator<state_type> > chart_type;

  typedef std::pair<double, span_pair_type> loss_span_pair_type;
  typedef std::vector<loss_span_pair_type, std::allocator<loss_span_pair_type> > heap_type;  
  
  struct heap_compare
  {
    // sort by greater item so that we can pop from less items
    bool operator()(const loss_span_pair_type& x, const loss_span_pair_type& y) const
    {
      return x.first > y.first;
    }
  };
  
  typedef std::vector<span_pair_type, std::allocator<span_pair_type> > span_pair_set_type;
  typedef std::vector<span_pair_set_type, std::allocator<span_pair_set_type> > agenda_type;

  typedef std::vector<hyperedge_type, std::allocator<hyperedge_type> > derivation_type;
  typedef std::vector<span_pair_type, std::allocator<span_pair_type> > stack_type;

  typedef std::pair<span_pair_type, span_pair_type> tail_set_type;
  
  struct tail_set_unassigned
  {
    tail_set_type operator()() const
    {
      return tail_set_type(span_pair_type(span_type::index_type(-1), span_type::index_type(-1),
					  span_type::index_type(-1), span_type::index_type(-1)),
			   span_pair_type(span_type::index_type(-1), span_type::index_type(-1),
					  span_type::index_type(-1), span_type::index_type(-1)));
    }
  };
  
  typedef utils::compact_set<tail_set_type,
			     tail_set_unassigned, tail_set_unassigned,
			     utils::hashmurmur3<size_t>, std::equal_to<tail_set_type>,
			     std::allocator<tail_set_type> > tail_set_unique_type;

  struct RestCost
  {
    double cost_;
    double alpha_;
    double beta_;
    
    RestCost()
      : cost_(std::numeric_limits<double>::infinity()),
	alpha_(std::numeric_limits<double>::infinity()),
	beta_(std::numeric_limits<double>::infinity()) {}
  };
  
  typedef RestCost rest_cost_type;
  typedef std::vector<rest_cost_type, std::allocator<rest_cost_type> > rest_cost_set_type;
  
  
  ITG(const dictionary_type& dict_source_target,
      const dictionary_type& dict_target_source,
      const size_type beam)
    : dict_source_target_(dict_source_target),
      dict_target_source_(dict_target_source),
      beam_(beam) {}
  
  const dictionary_type& dict_source_target_;
  const dictionary_type& dict_target_source_;

  size_type beam_;
  
  chart_type  chart_;
  agenda_type agenda_;
  heap_type   heap_;

  terminal_set_type terminals_;

  rest_cost_set_type costs_source_;
  rest_cost_set_type costs_target_;
  
  tail_set_unique_type uniques_;
  stack_type stack_;

  buffer_type buffer_layer_;
  buffer_type buffer_layer_norm_;

  buffer_type buffer_output_;
  buffer_type buffer_reconstruction_;

  buffer_type buffer_delta_;

  struct tanh
  {
    double operator()(const double& x) const
    {
      return std::tanh(x);
    }
  };
  
  struct dtanh
  {
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return Tp(1) - x * x;
    }
  };

  struct htanh
  {
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return std::min(std::max(x, Tp(- 1)), Tp(1));
    }
  };
  
  struct dhtanh
  {
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return Tp(- 1) < x && x < Tp(1);
    }
  };
  
  struct hinge
  {
    // 50 for numerical stability...
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return std::min(std::max(x, Tp(0)), Tp(50));
    }
  };
  
  struct dhinge
  {
    // 50 for numerical stability...
    
    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return Tp(0) < x && x < Tp(50);
    }
  };

  void clear()
  {
    chart_.clear();
    agenda_.clear();
    heap_.clear();

    terminals_.clear();
    costs_source_.clear();
    costs_target_.clear();
    
    uniques_.clear();
    stack_.clear();
  }
  
  double forward(const sentence_type& source,
		 const sentence_type& target,
		 const model_type& theta)
  {
    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
#if 0
    std::cerr << "forward source: " << source << std::endl
	      << "forward target: " << target << std::endl;
#endif


    clear();
    
    chart_.reserve(source_size + 1, target_size + 1);
    chart_.resize(source_size + 1, target_size + 1);
    
    agenda_.reserve(source_size + target_size + 1);
    agenda_.resize(source_size + target_size + 1);
    
    terminals_.reserve(source_size + 1, target_size + 1);
    terminals_.resize(source_size + 1, target_size + 1);
    
    costs_source_.reserve(source_size + 1);
    costs_target_.reserve(target_size + 1);

    costs_source_.resize(source_size + 1);
    costs_target_.resize(target_size + 1);

    // initialize terminals...
    forward_terminals(source, target, theta);
    
    // initialize chart...
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = 0; trg <= target_size; ++ trg)
	if (src < source_size || trg < target_size) {
	  // epsilon at target
	  if (src < source_size)
	    forward(source, target, span_pair_type(span_type(src, src + 1), span_type(trg, trg)), theta);
	  
	  // epsilon at source
	  if (trg < target_size)
	    forward(source, target, span_pair_type(span_type(src, src), span_type(trg, trg + 1)), theta);
	  
	  // word-pair
	  if (src < source_size && trg < target_size)
	    forward(source, target, span_pair_type(span_type(src, src + 1), span_type(trg, trg + 1)), theta);
	}

#if 0
    std::cerr << "biparsing source: " << source << std::endl
	      << "biparsing target: " << target << std::endl;
#endif

    // forward actual forward path
    const double infty = std::numeric_limits<double>::infinity();
    const size_type length_max = source_size + target_size;
    
    for (size_type length = 1; length != length_max; ++ length) 
      if (! agenda_[length].empty()) {
	
	//std::cerr << "length: " << length << std::endl;

	span_pair_set_type& spans = agenda_[length];
	
	heap_.clear();
	heap_.reserve(spans.size());

	span_pair_set_type::const_iterator siter_end = spans.end();
	for (span_pair_set_type::const_iterator siter = spans.begin(); siter != siter_end; ++ siter) {
	  const double loss = (chart_(siter->source_.first_, siter->source_.last_,
				      siter->target_.first_, siter->target_.last_).loss_
			       + std::max(costs_source_[siter->source_.first_].alpha_
					  + costs_source_[siter->source_.last_].beta_,
					  costs_target_[siter->target_.first_].alpha_
					  + costs_target_[siter->target_.last_].beta_));
	  
	  heap_.push_back(loss_span_pair_type(loss, *siter));
	  std::push_heap(heap_.begin(), heap_.end(), heap_compare());
	}
	
	heap_type::iterator hiter_begin = heap_.begin();
	heap_type::iterator hiter       = heap_.end();
	heap_type::iterator hiter_end   = heap_.end();
	
	if (length > 2 && std::distance(hiter_begin, hiter_end) > beam_) {
	  std::make_heap(hiter_begin, hiter_end, heap_compare());
	  
	  for (/**/; hiter_begin != hiter && std::distance(hiter, hiter_end) != beam_; -- hiter)
	    std::pop_heap(hiter_begin, hiter, heap_compare());
	} else
	  hiter = hiter_begin;
	
	// clear uniques
	uniques_.clear();
	
	// then, enumerate new hypotheses
	for (heap_type::iterator iter = hiter ; iter != hiter_end; ++ iter) {
	  //std::cerr << "length: " << length << " loss: " << iter->first << std::endl;
	  
	  const span_pair_type& span = iter->second;
	  
	  // we borrow the notation...
	  const difference_type l = length;
	  const difference_type s = span.source_.first_;
	  const difference_type t = span.source_.last_;
	  const difference_type u = span.target_.first_;
	  const difference_type v = span.target_.last_;
	  
	  const difference_type T = source_size;
	  const difference_type V = target_size;
	    
	  // remember, we have processed only upto length l. thus, do not try to combine with spans greather than l!
	  // also, keep used pair of spans in order to remove duplicates.
	    
	  for (difference_type S = utils::bithack::max(s - l, difference_type(0)); S <= s; ++ S) {
	    const difference_type L = l - (s - S);
	      
	    // straight
	    for (difference_type U = utils::bithack::max(u - L, difference_type(0)); U <= u - (S == s); ++ U) {
	      // parent span: StUv
	      // span1: SsUu
	      // span2: stuv
	      
	      if (chart_(S, s, U, u).loss_ == infty) continue;
	      
	      const span_pair_type  span1(S, s, U, u);
	      const span_pair_type& span2(span);
	      
	      if (! uniques_.insert(std::make_pair(span1, span2)).second) continue;
	      
	      forward(source, target, span_pair_type(S, t, U, v), span1, span2, theta, true);
	    }

	    // inversion
	    for (difference_type U = v + (S == s); U <= utils::bithack::min(v + L, V); ++ U) {
	      // parent span: StuU
	      // span1: SsvU
	      // span2: stuv

	      if (chart_(S, s, v, U).loss_ == infty) continue;
	      
	      const span_pair_type  span1(S, s, v, U);
	      const span_pair_type& span2(span);
	      
	      if (! uniques_.insert(std::make_pair(span1, span2)).second) continue;
	      
	      forward(source, target, span_pair_type(S, t, u, U), span1, span2, theta, false);
	    }
	  }
	  
	  for (difference_type S = t; S <= utils::bithack::min(t + l, T); ++ S) {
	    const difference_type L = l - (S - t);
	    
	    // inversion
	    for (difference_type U = utils::bithack::max(u - L, difference_type(0)); U <= u - (S == t); ++ U) {
	      // parent span: sSUv
	      // span1: stuv
	      // span2: tSUu
	      
	      if (chart_(t, S, U, u).loss_ == infty) continue;
	      
	      const span_pair_type& span1(span);
	      const span_pair_type  span2(t, S, U, u);
	      
	      if (! uniques_.insert(std::make_pair(span1, span2)).second) continue;
	      
	      forward(source, target, span_pair_type(s, S, U, v), span1, span2, theta, false);
	    }
	    
	    // straight
	    for (difference_type U = v + (S == t); U <= utils::bithack::min(v + L, V); ++ U) {
	      // parent span: sSuU
	      // span1: stuv
	      // span2: tSvU
		
	      if (chart_(t, S, v, U).loss_ == infty) continue;

	      const span_pair_type& span1(span);
	      const span_pair_type  span2(t, S, v, U);
	      
	      if (! uniques_.insert(std::make_pair(span1, span2)).second) continue;
	      
	      forward(source, target, span_pair_type(s, S, u, U), span1, span2, theta, true);
	    }
	  }
	}
      }
    
    return chart_(0, source_size, 0, target_size).loss_;
  }
  
  template <typename Embedding>
  void copy_embedding(const sentence_type& source,
		      const sentence_type& target,
		      const difference_type src,
		      const difference_type trg,
		      const model_type& theta,
		      Eigen::MatrixBase<Embedding>& embedding)
  {
    const difference_type source_size    = source.size();
    const difference_type target_size    = target.size();
    const difference_type window_size    = theta.window_;
    const difference_type embedding_size = theta.embedding_;
    
    const difference_type offset_source = 0;
    const difference_type offset_target = embedding_size * (window_size * 2 + 1);
    
    if (src == 0) {
      for (difference_type i = 0; i != window_size * 2 + 1; ++ i)
	embedding.block(offset_source + embedding_size * i, 0, embedding_size, 1)
	  = theta.source_.col(vocab_type::EPSILON.id()) * theta.scale_;
    } else {
      for (difference_type i = 0; i != window_size * 2 + 1; ++ i) {
	const difference_type shift = i - window_size;
	const word_type& word = (src + shift <= 0
				 ? vocab_type::BOS
				 : (src + shift > source_size
				    ? vocab_type::EOS
				    : source[src + shift - 1]));
	
	embedding.block(offset_source + embedding_size * i, 0, embedding_size, 1)
	  = theta.source_.col(word.id()) * theta.scale_;
      }
    }
    
    if (trg == 0) {
      for (difference_type i = 0; i != window_size * 2 + 1; ++ i)
	embedding.block(offset_target + embedding_size * i, 0, embedding_size, 1)
	  = theta.target_.col(vocab_type::EPSILON.id()) * theta.scale_;
    } else {
      for (difference_type i = 0; i != window_size * 2 + 1; ++ i) {
	const difference_type shift = i - window_size;
	const word_type& word = (trg + shift <= 0
				 ? vocab_type::BOS
				 : (trg + shift > target_size
				    ? vocab_type::EOS
				    : target[trg + shift - 1]));
	
	embedding.block(offset_target + embedding_size * i, 0, embedding_size, 1)
	  = theta.target_.col(word.id()) * theta.scale_;
      }
    }
  }
  
  void forward_terminals(const sentence_type& source,
			 const sentence_type& target,
			 const model_type& theta)
  {
    typedef Eigen::Map<tensor_type> matrix_type;

#if 0
    std::cerr << "terminals source: " << source << std::endl
	      << "terminals target: " << target << std::endl;
#endif
    
    const size_type embedding_size = theta.embedding_;
    const size_type hidden_size    = theta.hidden_;
    const size_type window_size    = theta.window_;

    const size_type offset_source = 0;
    const size_type offset_target = embedding_size;
    
    const size_type source_size = source.size();
    const size_type target_size = target.size();

    const size_type leaf_size = embedding_size * (window_size * 2 + 1) * 2;
    
    for (size_type src = 0; src <= source_size; ++ src)
      for (size_type trg = (src == 0); trg <= target_size; ++ trg) {
	state_type& state = terminals_(src, trg);

	state.input_.resize(leaf_size, 1);
	
	copy_embedding(source, target, src, trg, theta, state.input_);
	
	//const word_type& word_source = (src == 0 ? vocab_type::EPSILON : source[src - 1]);
	//const word_type& word_target = (trg == 0 ? vocab_type::EPSILON : target[trg - 1]);

	state.layer_ = theta.Wt1_ * state.input_ + theta.bt1_;
	
	state.layer_norm_ = state.layer_.normalized();
	
	state.output_ = (theta.Wt2_ * state.layer_norm_ + theta.bt2_).array().unaryExpr(htanh());
	
	state.reconstruction_.resize(embedding_size * 2, 1);

	for (size_type i = 0; i != (window_size * 2 + 1) * 2; ++ i)
	  state.reconstruction_.block(i * embedding_size, 0, embedding_size, 1)
	    = (state.output_.block(i * embedding_size, 0, embedding_size, 1).normalized()
	       - state.input_.block(i * embedding_size, 0, embedding_size, 1));
	
	state.loss_ = 0.5 * state.reconstruction_.squaredNorm();
	
	if (src)
	  costs_source_[src - 1].cost_ = std::min(costs_source_[src - 1].cost_, state.loss_);
	if (trg)
	  costs_target_[trg - 1].cost_ = std::min(costs_target_[trg - 1].cost_, state.loss_);
      }

    // estimate rest-costs
    forward_backward(costs_source_);
    forward_backward(costs_target_);
  }

  void forward_backward(rest_cost_set_type& costs)
  {
    const size_type sentence_size = costs.size() - 1;
    
    // forward...
    costs[0].alpha_ = 0;
    for (size_type last = 1; last <= sentence_size; ++ last) {
      const size_type first = last - 1;
      
      costs[last].alpha_ = std::min(costs[last].alpha_, costs[first].alpha_ + costs[first].cost_);
    }
    
    // backward...
    costs[sentence_size].beta_ = 0;
    for (difference_type first = sentence_size - 1; first >= 0; -- first) {
      const size_type last = first + 1;
      
      costs[first].beta_ = std::min(costs[first].beta_, costs[first].cost_ + costs[last].beta_);
    }
  }
  
  // terminal rules
  void forward(const sentence_type& source,
	       const sentence_type& target,
	       const span_pair_type& span,
	       const model_type& theta)
  {

    state_type& state = chart_(span.source_.first_, span.source_.last_, span.target_.first_, span.target_.last_);
    
    state = terminals_(span.source_.empty() ? 0 : span.source_.first_ + 1, span.target_.empty() ? 0 : span.target_.first_ + 1);
    
    state.span_ = span;
    
    agenda_[span.size()].push_back(span);
  }
  
  // binary rules
  void forward(const sentence_type& source,
	       const sentence_type& target,
	       const span_pair_type& span,
	       const span_pair_type& span1,
	       const span_pair_type& span2,
	       const model_type& theta,
	       const bool straight)
  {
    typedef Eigen::Map<tensor_type> matrix_type;
    
    const size_type hidden_size    = theta.hidden_;
    const size_type embedding_size = theta.embedding_;

    const size_type offset_left  = 0;
    const size_type offset_right = hidden_size;
    
    //std::cerr << "span: " << span << " left: " << span1 << " right: " << span2 << std::endl;

    /**/  state_type& state  = chart_(span.source_.first_, span.source_.last_, span.target_.first_, span.target_.last_);
    const state_type& state1 = chart_(span1.source_.first_, span1.source_.last_, span1.target_.first_, span1.target_.last_);
    const state_type& state2 = chart_(span2.source_.first_, span2.source_.last_, span2.target_.first_, span2.target_.last_);
    
    const tensor_type& Wr1 = (straight ? theta.Ws1_ : theta.Wi1_);
    const tensor_type& br1 = (straight ? theta.bs1_ : theta.bi1_);
    const tensor_type& Wr2 = (straight ? theta.Ws2_ : theta.Wi2_);
    const tensor_type& br2 = (straight ? theta.bs2_ : theta.bi2_);
    
    buffer_layer_.resize(hidden_size);
    buffer_layer_norm_.resize(hidden_size);
    buffer_output_.resize(hidden_size * 2);
    buffer_reconstruction_.resize(hidden_size * 2);
    
    matrix_type layer(&(*buffer_layer_.begin()), hidden_size, 1);
    matrix_type layer_norm(&(*buffer_layer_norm_.begin()), hidden_size, 1);
    matrix_type output(&(*buffer_output_.begin()), hidden_size * 2, 1);
    matrix_type reconstruction(&(*buffer_reconstruction_.begin()), hidden_size * 2, 1);
    
    layer = (br1
	     + Wr1.block(0, offset_left, hidden_size, hidden_size) * state1.layer_
	     + Wr1.block(0, offset_right, hidden_size, hidden_size) * state2.layer_).array().unaryExpr(htanh());
    
    layer_norm = layer.normalized();
    
    output = (Wr2 * layer_norm + br2).array().unaryExpr(htanh());
    
    reconstruction.block(offset_left, 0, hidden_size, 1)  = (output.block(offset_left, 0, hidden_size, 1).normalized()
							     - state1.layer_);
    reconstruction.block(offset_right, 0, hidden_size, 1) = (output.block(offset_right, 0, hidden_size, 1).normalized()
							     - state2.layer_);
    
    const double loss = 0.5 * reconstruction.squaredNorm() + state1.loss_ + state2.loss_;
    
    if (loss < state.loss_) {
      if (state.loss_ == std::numeric_limits<double>::infinity())
	agenda_[span.size()].push_back(span);

      state.span_  = span;
      state.left_  = span1;
      state.right_ = span2;
      
      state.loss_ = loss;
      
      state.layer_          = layer;
      state.layer_norm_     = layer_norm;
      state.output_         = output;
      state.reconstruction_ = reconstruction;
    }
  }
  
  template <typename Gen>
  void backward(const sentence_type& source,
		const sentence_type& target,
		const model_type& theta,
		gradient_type& gradient,
		Gen& gen)
  {
    typedef Eigen::Map<tensor_type> matrix_type;
    
#if 0
    std::cerr << "backward source: " << source << std::endl
	      << "backward target: " << target << std::endl;
#endif

    const size_type source_size = source.size();
    const size_type target_size = target.size();
    
    const size_type embedding_size = theta.embedding_;
    const size_type hidden_size    = theta.hidden_;
    const size_type window_size    = theta.window_;
    
    const size_type offset_source = 0;
    const size_type offset_target = embedding_size;
    
    const size_type offset_left  = 0;
    const size_type offset_right = hidden_size;

    ++ gradient.count_;

    state_type& root = chart_(0, source_size, 0, target_size);
    
    root.delta_ = tensor_type::Zero(hidden_size, 1);
    
    stack_.clear();
    stack_.push_back(span_pair_type(0, source_size, 0, target_size));

    buffer_delta_.resize(utils::bithack::max(hidden_size, embedding_size) * 2);
    
    while (! stack_.empty()) {
      const span_pair_type span = stack_.back();
      stack_.pop_back();
      
      state_type& state = chart_(span.source_.first_, span.source_.last_, span.target_.first_, span.target_.last_);
      
      if (state.terminal()) {
	//const word_type& word_source = (span.source_.empty() ? vocab_type::EPSILON : source[span.source_.first_]);
	//const word_type& word_target = (span.target_.empty() ? vocab_type::EPSILON : target[span.target_.first_]);

	// increment delta from reconstruction
	matrix_type delta_reconstruction(&(*buffer_delta_.begin()), embedding_size * 2, 1);
	
	delta_reconstruction = state.output_.array().unaryExpr(dhtanh()) * state.reconstruction_.array();
	
	gradient.Wt2_ += delta_reconstruction * state.layer_norm_.transpose();
	gradient.bt2_ += delta_reconstruction;
	
	state.delta_.array() += (state.layer_.array().unaryExpr(dhtanh())
				 * (theta.Wt2_.transpose() * delta_reconstruction).array());
	
	gradient.Wt1_ += state.delta_ * state.input_.transpose();
	gradient.bt1_ += state.delta_;

	if (span.source_.empty()) {
	  tensor_type& dsource = gradient.source(vocab_type::EPSILON);
	  
	  for (size_type i = 0; i != window_size * 2 + 1; ++ i)
	    dsource
	      += (theta.Wt1_.block(0, offset_source + i * embedding_size, hidden_size, embedding_size).transpose()
		  * state.delta_
		  - state.reconstruction_.block(offset_source + i * embedding_size, 0, embedding_size, 1));
	} else {
	  const difference_type src = span.source_.last_;
	  
	  for (difference_type i = 0; i != window_size * 2 + 1; ++ i) {
	    const difference_type shift = i - static_cast<difference_type>(window_size);
	    const word_type& word = (src + shift <= 0
				     ? vocab_type::BOS
				     : (src + shift > source_size
					? vocab_type::EOS
					: source[src + shift - 1]));
	    
	    gradient.source(word.id()) +=
	      (theta.Wt1_.block(0, offset_source + i * embedding_size, hidden_size, embedding_size).transpose()
	       * state.delta_
	       - state.reconstruction_.block(offset_source + i * embedding_size, 0, embedding_size, 1));
	  }
	}
	
	if (span.target_.empty()) {
	  tensor_type& dtarget = gradient.target(vocab_type::EPSILON);
	  
	  for (size_type i = 0; i != window_size * 2 + 1; ++ i)
	    dtarget
	      += (theta.Wt1_.block(0, offset_target + i * embedding_size, hidden_size, embedding_size).transpose()
		  * state.delta_
		  - state.reconstruction_.block(offset_target + i * embedding_size, 0, embedding_size, 1));
	} else {
	  const difference_type trg = span.target_.last_;
	  
	  for (difference_type i = 0; i != window_size * 2 + 1; ++ i) {
	    const difference_type shift = i - static_cast<difference_type>(window_size);
	    const word_type& word = (trg + shift <= 0
				     ? vocab_type::BOS
				     : (trg + shift > target_size
					? vocab_type::EOS
					: target[trg + shift - 1]));
	    
	    gradient.target(word.id()) +=
	      (theta.Wt1_.block(0, offset_target + i * embedding_size, hidden_size, embedding_size).transpose()
	       * state.delta_
	       - state.reconstruction_.block(offset_target + i * embedding_size, 0, embedding_size, 1));
	  }
	}
      } else {
	stack_.push_back(state.left_);
	stack_.push_back(state.right_);
	
	const bool straight = state.straight();

	const tensor_type& Wr1 = (straight ? theta.Ws1_ : theta.Wi1_);
	const tensor_type& Wr2 = (straight ? theta.Ws2_ : theta.Wi2_);
	
	tensor_type& dWr1 = (straight ? gradient.Ws1_ : gradient.Wi1_);
	tensor_type& dbr1 = (straight ? gradient.bs1_ : gradient.bi1_);
	tensor_type& dWr2 = (straight ? gradient.Ws2_ : gradient.Wi2_);
	tensor_type& dbr2 = (straight ? gradient.bs2_ : gradient.bi2_);

	// increment delta from reconstruction
	matrix_type delta_reconstruction(&(*buffer_delta_.begin()), hidden_size * 2, 1);
	
	delta_reconstruction = state.output_.array().unaryExpr(dhtanh()) * state.reconstruction_.array();
	
	dWr2 += delta_reconstruction * state.layer_norm_.transpose();
	dbr2 += delta_reconstruction;
	
	state.delta_.array() += (state.layer_.array().unaryExpr(dhtanh())
				 * (Wr2.transpose() * delta_reconstruction).array());
	
	state_type& state_left  = chart_(state.left_.source_.first_, state.left_.source_.last_,
					 state.left_.target_.first_, state.left_.target_.last_);
	state_type& state_right = chart_(state.right_.source_.first_, state.right_.source_.last_,
					 state.right_.target_.first_, state.right_.target_.last_);
	
	dWr1.block(0, offset_left,  hidden_size, hidden_size) += state.delta_ * state_left.layer_.transpose();
	dWr1.block(0, offset_right, hidden_size, hidden_size) += state.delta_ * state_right.layer_.transpose();
	dbr1 += state.delta_;
	
	tensor_type& delta_left  = state_left.delta_;
	tensor_type& delta_right = state_right.delta_;
	
	delta_left  = (state_left.layer_.array().unaryExpr(dhtanh())
		       * (Wr1.block(0, offset_left, hidden_size, hidden_size).transpose() * state.delta_
			  - state.reconstruction_.block(offset_left, 0, hidden_size, 1)).array());
	delta_right = (state_right.layer_.array().unaryExpr(dhtanh())
		       * (Wr1.block(0, offset_right, hidden_size, hidden_size).transpose() * state.delta_
			  - state.reconstruction_.block(offset_right, 0, hidden_size, 1)).array());
      }
    }
  }
  
  void derivation(const sentence_type& source,
		  const sentence_type& target,
		  derivation_type& d)
  {
    d.clear();
    
    stack_.clear();
    stack_.push_back(span_pair_type(0, source.size(), 0, target.size()));
    
    while (! stack_.empty()) {
      const span_pair_type span = stack_.back();
      stack_.pop_back();

      const state_type& state = chart_(span.source_.first_, span.source_.last_, span.target_.first_, span.target_.last_);
      
      if (state.terminal())
	d.push_back(hyperedge_type(span));
      else {
	d.push_back(hyperedge_type(span, state.left_, state.right_));
	
	// we will push in right-to-left order...!
	stack_.push_back(state.right_);
	stack_.push_back(state.left_);
      }
    }
  }

};

struct LearnAdaGrad
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef Model     model_type;
  typedef Gradient  gradient_type;

  typedef cicada::Symbol   word_type;
  typedef cicada::Vocab    vocab_type;
  
  typedef model_type::tensor_type tensor_type;
  
  LearnAdaGrad(const size_type& embedding,
	       const size_type& hidden,
	       const size_type& window,
	       const double& lambda,
	       const double& eta0)
    : embedding_(embedding),
      hidden_(hidden),
      window_(window),
      lambda_(lambda),
      eta0_(eta0)
  {
    if (lambda_ < 0.0)
      throw std::runtime_error("invalid regularization");
    
    if (eta0_ <= 0.0)
      throw std::runtime_error("invalid learning rate");

    const size_type vocabulary_size = word_type::allocated();
    const size_type leaf_size = embedding * (window * 2 + 1) * 2;
    
    source_ = tensor_type::Zero(embedding_, vocabulary_size);
    target_ = tensor_type::Zero(embedding_, vocabulary_size);
    
    Wc_ = tensor_type::Zero(embedding, hidden_);
    bc_ = tensor_type::Zero(embedding, 1);
    
    Ws1_ = tensor_type::Zero(hidden_, hidden_ + hidden_);
    bs1_ = tensor_type::Zero(hidden_, 1);

    Ws2_ = tensor_type::Zero(hidden_ + hidden_, hidden_);
    bs2_ = tensor_type::Zero(hidden_ + hidden_, 1);

    Wi1_ = tensor_type::Zero(hidden_, hidden_ + hidden_);
    bi1_ = tensor_type::Zero(hidden_, 1);
    
    Wi2_ = tensor_type::Zero(hidden_ + hidden_,  hidden_);
    bi2_ = tensor_type::Zero(hidden_ + hidden_, 1);
    
    Wt1_ = tensor_type::Zero(hidden_, leaf_size);
    bt1_ = tensor_type::Zero(hidden_, 1);

    Wt2_ = tensor_type::Zero(leaf_size, hidden_);
    bt2_ = tensor_type::Zero(leaf_size, 1);
  }

  
  void operator()(model_type& theta,
		  const gradient_type& gradient) const
  {
    typedef gradient_type::embedding_type gradient_embedding_type;
    
    const double scale = 1.0 / gradient.count_;

    gradient_embedding_type::const_iterator siter_end = gradient.source_.end();
    for (gradient_embedding_type::const_iterator siter = gradient.source_.begin(); siter != siter_end; ++ siter)
      update(siter->first,
	     theta.source_,
	     const_cast<tensor_type&>(source_),
	     siter->second,
	     scale);
    
    gradient_embedding_type::const_iterator titer_end = gradient.target_.end();
    for (gradient_embedding_type::const_iterator titer = gradient.target_.begin(); titer != titer_end; ++ titer)
      update(titer->first,
	     theta.target_,
	     const_cast<tensor_type&>(target_),
	     titer->second,
	     scale);

    update(theta.Wc_, const_cast<tensor_type&>(Wc_), gradient.Wc_, scale, lambda_ != 0.0);
    update(theta.bc_, const_cast<tensor_type&>(bc_), gradient.bc_, scale, false);
    
    update(theta.Ws1_, const_cast<tensor_type&>(Ws1_), gradient.Ws1_, scale, lambda_ != 0.0);
    update(theta.bs1_, const_cast<tensor_type&>(bs1_), gradient.bs1_, scale, false);
    update(theta.Ws2_, const_cast<tensor_type&>(Ws2_), gradient.Ws2_, scale, lambda_ != 0.0);
    update(theta.bs2_, const_cast<tensor_type&>(bs2_), gradient.bs2_, scale, false);

    update(theta.Wi1_, const_cast<tensor_type&>(Wi1_), gradient.Wi1_, scale, lambda_ != 0.0);
    update(theta.bi1_, const_cast<tensor_type&>(bi1_), gradient.bi1_, scale, false);
    update(theta.Wi2_, const_cast<tensor_type&>(Wi2_), gradient.Wi2_, scale, lambda_ != 0.0);
    update(theta.bi2_, const_cast<tensor_type&>(bi2_), gradient.bi2_, scale, false);
    
    update(theta.Wt1_, const_cast<tensor_type&>(Wt1_), gradient.Wt1_, scale, lambda_ != 0.0);
    update(theta.bt1_, const_cast<tensor_type&>(bt1_), gradient.bt1_, scale, false);
    update(theta.Wt2_, const_cast<tensor_type&>(Wt2_), gradient.Wt2_, scale, lambda_ != 0.0);
    update(theta.bt2_, const_cast<tensor_type&>(bt2_), gradient.bt2_, scale, false);
  }

  template <typename Theta, typename GradVar, typename Grad>
  struct update_visitor_regularize
  {
    update_visitor_regularize(Eigen::MatrixBase<Theta>& theta,
			      Eigen::MatrixBase<GradVar>& G,
			      const Eigen::MatrixBase<Grad>& g,
			      const double& scale,
			      const double& lambda,
			      const double& eta0)
      : theta_(theta), G_(G), g_(g), scale_(scale), lambda_(lambda), eta0_(eta0) {}
    
    void init(const tensor_type::Scalar& value, tensor_type::Index i, tensor_type::Index j)
    {
      operator()(value, i, j);
    }
    
    void operator()(const tensor_type::Scalar& value, tensor_type::Index i, tensor_type::Index j)
    {
      if (g_(i, j) == 0) return;
      
      G_(i, j) += g_(i, j) * g_(i, j) * scale_ * scale_;
      
      const double rate = eta0_ / std::sqrt(double(1.0) + G_(i, j));
      const double f = theta_(i, j) - rate * scale_ * g_(i, j);

      theta_(i, j) = utils::mathop::sgn(f) * std::max(0.0, std::fabs(f) - rate * lambda_);
    }
    
    Eigen::MatrixBase<Theta>&      theta_;
    Eigen::MatrixBase<GradVar>&    G_;
    const Eigen::MatrixBase<Grad>& g_;
    
    const double scale_;
    const double lambda_;
    const double eta0_;
  };

  struct learning_rate
  {
    learning_rate(const double& eta0) : eta0_(eta0) {}

    template <typename Tp>
    Tp operator()(const Tp& x) const
    {
      return (x == 0.0 ? 0.0 : eta0_ / std::sqrt(double(1.0) + x));
    }

    const double& eta0_;
  };
  
  template <typename Theta, typename GradVar, typename Grad>
  void update(Eigen::MatrixBase<Theta>& theta,
	      Eigen::MatrixBase<GradVar>& G,
	      const Eigen::MatrixBase<Grad>& g,
	      const double scale,
	      const bool regularize=true) const
  {
    if (regularize) {
      update_visitor_regularize<Theta, GradVar, Grad> visitor(theta, G, g, scale, lambda_, eta0_);
      
      theta.visit(visitor);
    } else {
      G.array() += g.array().square() * scale * scale;
      theta.array() -= scale * g.array() * G.array().unaryExpr(learning_rate(eta0_));
    }
  }

  template <typename Theta, typename GradVar, typename Grad>
  void update(const word_type& word,
	      Eigen::MatrixBase<Theta>& theta,
	      Eigen::MatrixBase<GradVar>& G,
	      const Eigen::MatrixBase<Grad>& g,
	      const double scale) const
  {
    for (int row = 0; row != g.rows(); ++ row) 
      if (g(row, 0) != 0) {
	G(row, word.id()) +=  g(row, 0) * g(row, 0) * scale * scale;
	
	const double rate = eta0_ / std::sqrt(double(1.0) + G(row, word.id()));
	const double f = theta(row, word.id()) - rate * scale * g(row, 0);
	
	theta(row, word.id()) = utils::mathop::sgn(f) * std::max(0.0, std::fabs(f) - rate * lambda_);
      }
  }
  
  size_type embedding_;
  size_type hidden_;
  size_type window_;
  
  double lambda_;
  double eta0_;
  
  // embedding
  tensor_type source_;
  tensor_type target_;

  // classification
  tensor_type Wc_;
  tensor_type bc_;

  // straight
  tensor_type Ws1_;
  tensor_type bs1_;
  tensor_type Ws2_;
  tensor_type bs2_;

  // inversion
  tensor_type Wi1_;
  tensor_type bi1_;  
  tensor_type Wi2_;
  tensor_type bi2_;  
  
  // terminal
  tensor_type Wt1_;
  tensor_type bt1_;
  tensor_type Wt2_;
  tensor_type bt2_;
};

struct LearnSGD
{
  typedef size_t    size_type;
  typedef ptrdiff_t difference_type;
  
  typedef Model     model_type;
  typedef Gradient  gradient_type;

  typedef cicada::Symbol   word_type;
  typedef cicada::Vocab    vocab_type;
  
  typedef model_type::tensor_type tensor_type;

  LearnSGD(const double& lambda,
	   const double& eta0)
    : lambda_(lambda),
      eta0_(eta0),
      epoch_(0)
  {
    if (lambda_ < 0.0)
      throw std::runtime_error("invalid regularization");
    
    
    if (eta0_ <= 0.0)
      throw std::runtime_error("invalid learning rate");
  }
  
  void operator()(model_type& theta,
		  const gradient_type& gradient) const
  {
    typedef gradient_type::embedding_type gradient_embedding_type;

    //++ const_cast<size_type&>(epoch_);

    const double scale = 1.0 / gradient.count_;
    
    if (lambda_ != 0.0) {
      const double eta = eta0_ / (epoch_ + 1);
      
      theta.scale_ *= 1.0 - eta * lambda_;
    }
    
    gradient_embedding_type::const_iterator siter_end = gradient.source_.end();
    for (gradient_embedding_type::const_iterator siter = gradient.source_.begin(); siter != siter_end; ++ siter)
      update(siter->first,
	     theta.source_,
	     siter->second,
	     scale,
	     theta.scale_);
    
    gradient_embedding_type::const_iterator titer_end = gradient.target_.end();
    for (gradient_embedding_type::const_iterator titer = gradient.target_.begin(); titer != titer_end; ++ titer)
      update(titer->first,
	     theta.target_,
	     titer->second,
	     scale,
	     theta.scale_);

    update(theta.Wc_, gradient.Wc_, scale, lambda_ != 0.0);
    update(theta.bc_, gradient.bc_, scale, false);

    update(theta.Ws1_, gradient.Ws1_, scale, lambda_ != 0.0);
    update(theta.bs1_, gradient.bs1_, scale, false);
    update(theta.Ws2_, gradient.Ws2_, scale, lambda_ != 0.0);
    update(theta.bs2_, gradient.bs2_, scale, false);

    update(theta.Wi1_, gradient.Wi1_, scale, lambda_ != 0.0);
    update(theta.bi1_, gradient.bi1_, scale, false);
    update(theta.Wi2_, gradient.Wi2_, scale, lambda_ != 0.0);
    update(theta.bi2_, gradient.bi2_, scale, false);
    
    update(theta.Wt1_, gradient.Wt1_, scale, lambda_ != 0.0);
    update(theta.bt1_, gradient.bt1_, scale, false);
    update(theta.Wt2_, gradient.Wt2_, scale, lambda_ != 0.0);
    update(theta.bt2_, gradient.bt2_, scale, false);
  }

  template <typename Theta, typename Grad>
  void update(Eigen::MatrixBase<Theta>& theta,
	      const Eigen::MatrixBase<Grad>& g,
	      const double scale,
	      const bool regularize=true) const
  {
    const double eta = eta0_ / (epoch_ + 1);

    if (regularize)
      theta *= 1.0 - eta * lambda_;
    
    theta.noalias() -= (eta * scale) * g;
  }

  template <typename Theta, typename Grad>
  void update(const word_type& word,
	      Eigen::MatrixBase<Theta>& theta,
	      const Eigen::MatrixBase<Grad>& g,
	      const double scale,
	      const double theta_scale) const
  {
    const double eta = eta0_ / (epoch_ + 1);
    
    theta.col(word.id()).noalias() -= (eta * scale / theta_scale) * g;
  }
  
  double lambda_;
  double eta0_;

  size_type epoch_;
};

#endif

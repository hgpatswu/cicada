// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__EVAL__SCORE__HPP__
#define __CICADA__EVAL__SCORE__HPP__ 1

// base-class for scoring...
#include <string>
#include <vector>
#include <utility>
#include <ostream>

#include <cicada/sentence.hpp>
#include <cicada/sentence_vector.hpp>
#include <cicada/tokenizer.hpp>
#include <cicada/vocab.hpp>

#include <utils/piece.hpp>

#include <boost/shared_ptr.hpp>

namespace cicada
{
  namespace eval
  {
    class Score
    {
    public:
      typedef Score score_type;
      typedef boost::shared_ptr<score_type>  score_ptr_type;
      
    public:
      virtual ~Score() {}
      
      
      double operator()() const { return score(); }

      virtual double score() const = 0;
      virtual double loss() const = 0;
      virtual double reward() const = 0;
      virtual bool error_metric() const = 0;

      virtual bool equal(const score_type& score) const = 0;
      virtual void assign(const score_type& score) = 0;
      
      virtual void plus_equal(const score_type& score) = 0;
      virtual void minus_equal(const score_type& score) = 0;
      
      virtual void multiplies_equal(const double& scale) = 0;
      virtual void divides_equal(const double& scale) = 0;

      virtual score_ptr_type zero() const = 0;
      virtual score_ptr_type clone() const = 0;
      
      virtual std::string description() const = 0;
      virtual std::string encode() const = 0;

      static score_ptr_type decode(utils::piece::const_iterator& iter, utils::piece::const_iterator end);
      static score_ptr_type decode(std::string::const_iterator& iter, std::string::const_iterator end);
      static score_ptr_type decode(const utils::piece& encoded);
      
    public:      
      friend
      bool operator==(const score_type& x, const score_type& y)
      {
	return x.equal(y);
      }
      friend
      bool operator!=(const score_type& x, const score_type& y)
      {
	return ! x.equal(y);
      }
      
      score_type& operator=(const score_type& score)
      {
	this->assign(score);
	return *this;
      }
      
      score_type& operator+=(const score_type& score)
      {
	this->plus_equal(score);
	return *this;
      }
      
      score_type& operator-=(const score_type& score)
      {
	this->minus_equal(score);
	return *this;
      }
      
      score_type& operator*=(const double& scale)
      {
	this->multiplies_equal(scale);
	return *this;
      }
      
      score_type& operator/=(const double& scale)
      {
	this->divides_equal(scale);
	return *this;
      }
      
    };
    
    inline
    std::ostream& operator<<(std::ostream& os, const Score& x)
    {
      os << x.description();
      return os;
    }
    
    class Scorer
    {
    public:
      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      
      typedef cicada::Symbol         word_type;
      typedef cicada::Sentence       sentence_type;
      typedef cicada::SentenceVector sentence_set_type;
      typedef cicada::Vocab          vocab_type;
      
      typedef Score score_type;
      typedef Scorer scorer_type;
      
      typedef boost::shared_ptr<score_type>  score_ptr_type;
      typedef boost::shared_ptr<scorer_type> scorer_ptr_type;

      typedef cicada::Tokenizer tokenizer_type;
      
    public:
      Scorer() : tokenizer(0), skip_sgml_tag(false) {}
      Scorer(const Scorer& x)
	: tokenizer(0), skip_sgml_tag(x.skip_sgml_tag)
      {
	if (x.tokenizer)
	  tokenizer = &tokenizer_type::create(x.tokenizer->algorithm());
      }
      
      virtual ~Scorer() {}

      Scorer& operator=(const Scorer& x)
      {
	tokenizer = 0;
	if (x.tokenizer)
	  tokenizer = &tokenizer_type::create(x.tokenizer->algorithm());
	skip_sgml_tag = x.skip_sgml_tag;
	return *this;
      }
      
      // insert a sentence for scoring
      virtual void clear() = 0;
      virtual void insert(const sentence_type& sentence) = 0;

      void insert(const sentence_set_type& sentences)
      {
	insert(sentences.begin(), sentences.end());
      }
      
      template <typename Iterator>
      void insert(Iterator first, Iterator last)
      {
	for (/**/; first != last; ++ first)
	  insert(*first);
      }

      score_ptr_type operator()(const sentence_type& sentence) const { return score(sentence); }
      virtual score_ptr_type score(const sentence_type& sentence) const = 0;
      virtual bool error_metric() const = 0;
      virtual scorer_ptr_type clone() const = 0;
      
      static const char*     lists();
      static scorer_ptr_type create(const utils::piece& parameter);

      void tokenize(const sentence_type& source, sentence_type& tokenized) const
      {
	if (skip_sgml_tag) {
	  sentence_type skipped;
	  sentence_type::const_iterator siter_end = source.end();
	  for (sentence_type::const_iterator siter = source.begin(); siter != siter_end; ++ siter)
	    if (*siter != vocab_type::EPSILON && *siter != vocab_type::BOS && *siter != vocab_type::EOS && ! siter->is_sgml_tag())
	      skipped.push_back(*siter);
	  
	  if (tokenizer)
	    tokenizer->operator()(skipped, tokenized);
	  else
	    tokenized = skipped;
	} else {
	  if (tokenizer)
	    tokenizer->operator()(source, tokenized);
	  else
	    tokenized = source;
	}
      }
      
    protected:
      const tokenizer_type* tokenizer;
      bool skip_sgml_tag;
    };    
    
    class ScorerDocument
    {
    public:
      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      
      typedef cicada::Sentence sentence_type;
      typedef cicada::Symbol   word_type;

      typedef Scorer scorer_type;

      typedef scorer_type::score_type      score_type;
      typedef scorer_type::score_ptr_type  score_ptr_type;
      typedef scorer_type::scorer_ptr_type scorer_ptr_type;

      
    private:
      typedef std::vector<scorer_ptr_type, std::allocator<scorer_ptr_type> > scorer_set_type;

    public:
      typedef scorer_set_type::const_iterator const_iterator;
      typedef scorer_set_type::iterator       iterator;
      
    public:
      ScorerDocument()
	: m_parameter() {}
      ScorerDocument(const std::string& __parameter)
	: m_parameter(__parameter) {}
      
      inline       scorer_ptr_type& operator[](size_type pos)       { return m_scorers[pos]; }
      inline const scorer_ptr_type& operator[](size_type pos) const { return m_scorers[pos]; }

      inline const_iterator begin() const { return m_scorers.begin(); }
      inline       iterator begin()       { return m_scorers.begin(); }
      
      inline const_iterator end() const { return m_scorers.end(); }
      inline       iterator end()       { return m_scorers.end(); }

      void erase(iterator first, iterator last)
      {
	m_scorers.erase(first, last);
      }

      void erase(iterator iter)
      {
	m_scorers.erase(iter);
      }
      
      void push_back(const scorer_ptr_type& x) { m_scorers.push_back(x); }
      
      bool error_metric() const
      {
	return scorer_type::create(m_parameter)->error_metric();
      }

      void resize(size_type x) { m_scorers.resize(x); }
      void clear() { m_scorers.clear(); }
      
      size_type size() const { return m_scorers.size(); }
      bool empty() const { return m_scorers.empty(); }
      
      scorer_ptr_type create() const { return scorer_type::create(m_parameter); }
      const std::string& parameter() const { return m_parameter; }
      
      void swap(ScorerDocument& x)
      {
	m_scorers.swap(x.m_scorers);
	m_parameter.swap(x.m_parameter);
      }
      
    private:
      scorer_set_type m_scorers;
      std::string     m_parameter;
    };
    
  };
};

namespace std
{
  
  inline
  void swap(cicada::eval::ScorerDocument& x,
	    cicada::eval::ScorerDocument& y)
  {
    x.swap(y);
  }

};

#endif

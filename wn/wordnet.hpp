// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __WN__WORDNET__HPP__
#define __WN__WORDNET__HPP__ 1

#include <string>
#include <vector>

#include <utils/hashmurmur3.hpp>

namespace wn
{
  class WordNet
  {
  public:
    struct SynSet
    {
      std::string pos;
      std::string word;
      int sense;
      
      SynSet() {}
    };
    typedef SynSet synset_type;
    typedef std::vector<synset_type, std::allocator<synset_type> > synset_set_type;

    typedef std::vector<std::string, std::allocator<std::string> > morph_set_type;
    
  public:
    WordNet()  { initialize(""); }
    WordNet(const std::string& path) { initialize(path); }
    
  public:
    void operator()(const std::string& word, synset_set_type& synsets) const;
    void operator()(const std::string& word, morph_set_type& morphs) const;
    
  private:
    static void initialize(const std::string& path);
  };
  
  inline
  size_t hash_value(const WordNet::synset_type& x)
  {
    typedef utils::hashmurmur3<size_t> hasher_type;
    
    return hasher_type()(x.pos.begin(), x.pos.end(), hasher_type()(x.word.begin(), x.word.end(), x.sense));
  }

  inline
  bool operator==(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return x.pos == y.pos && x.word == y.word && x.sense == y.sense;
  }
  
  inline
  bool operator!=(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return x.pos != y.pos || x.word != y.word || x.sense != y.sense;
  }
  
  inline
  bool operator<(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return (x.pos < y.pos
	    || (!(y.pos < x.pos) 
		&& (x.word < y.word
		    || (!(y.word < x.word)
			&& x.sense < y.sense))));
  }
  
  inline
  bool operator>(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return y < x;
  }
  
  inline
  bool operator<=(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return ! (y < x);
  }

  inline
  bool operator>=(const WordNet::synset_type& x, const WordNet::synset_type& y)
  {
    return ! (x < y);
  }

};

#endif

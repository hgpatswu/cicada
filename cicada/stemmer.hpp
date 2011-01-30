// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__STEMMER__HPP__
#define __CICADA__STEMMER__HPP__ 1

#include <string>
#include <algorithm>

#include <cicada/symbol.hpp>
#include <cicada/vocab.hpp>

#include <utils/piece.hpp>

namespace cicada
{
  class Stemmer
  {
  public:
    typedef Symbol    symbol_type;
    typedef Vocab     vocab_type;
    
    typedef symbol_type          word_type;
    typedef symbol_type::id_type id_type;
    
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    
  public:
    Stemmer() {}
    virtual ~Stemmer() {}
    
  private:
    // we do not allow copy/construct
    Stemmer& operator=(const Stemmer& x) { return *this; }
    Stemmer(const Stemmer& x) {}
    
  public:
    static Stemmer&    create(const utils::piece& parameter);
    static const char* lists();
    
  public:
    symbol_type operator()(const symbol_type& x) const { return operator[](x); }
    virtual symbol_type operator[](const symbol_type& x) const = 0;
    const std::string& algorithm() const { return __algorithm; }

  private:
    std::string __algorithm;
  };
};

#endif

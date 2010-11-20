
#include "stemmer/latin.hpp"

#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/bytestream.h>
#include <unicode/translit.h>

#include <boost/thread/mutex.hpp>

#include <utils/atomicop.hpp>

namespace cicada
{

  namespace stemmer
  {
    struct LatinImpl
    {
    private:
      Transliterator* trans;
      
    public:
      LatinImpl() : trans(0) { open(); }
      ~LatinImpl() { close(); }
      
    public:
      void operator()(UnicodeString& data) { trans->transliterate(data); }
    
    private:
      void open()
      {
	close();
      
	__initialize();
      
	UErrorCode status = U_ZERO_ERROR;
	trans = Transliterator::createInstance(UnicodeString::fromUTF8("AnyLatinNoAccents"),
					       UTRANS_FORWARD,
					       status);
	if (U_FAILURE(status))
	  throw std::runtime_error(std::string("transliterator::create_instance(): ") + u_errorName(status));
      
      }
    
      void close()
      {
	if (trans) 
	  delete trans;
	trans = 0;
      }
    
    private:
      typedef boost::mutex              mutex_type;
      typedef boost::mutex::scoped_lock lock_type;

      static mutex_type __mutex;

      void __initialize()
      {
	static bool __initialized = false;

	volatile bool tmp = __initialized;
	
	utils::atomicop::memory_barrier();
	
	if (! __initialized) {
	  
	  lock_type lock(__mutex);
	  
	  if (! __initialized) {
	    
	    // Any-Latin, NFKD, remove accents, NFKC
	    UErrorCode status = U_ZERO_ERROR;
	    UParseError status_parse;
	    Transliterator* __trans = Transliterator::createFromRules(UnicodeString::fromUTF8("AnyLatinNoAccents"),
								      UnicodeString::fromUTF8(":: Any-Latin; :: NFKD; [[:Z:][:M:][:C:]] > ; :: NFKC;"),
								      UTRANS_FORWARD, status_parse, status);
	    if (U_FAILURE(status))
	      throw std::runtime_error(std::string("transliterator::create_from_rules(): ") + u_errorName(status));
	    
	    // register here...
	    Transliterator::registerInstance(__trans);
	    
	    tmp = true;
	    utils::atomicop::memory_barrier();
	    __initialized = tmp;
	  }
	}
      }
    };
    
    LatinImpl::mutex_type LatinImpl::__mutex;

    Latin::Latin() : pimpl(new impl_type()) {}
    Latin::~Latin() { std::auto_ptr<impl_type> tmp(pimpl); }

    Stemmer::symbol_type Latin::operator[](const symbol_type& word) const
    {
      if (! pimpl)
	throw std::runtime_error("no latin normalizer?");

      if (word == vocab_type::EMPTY || word.is_non_terminal()) return word;
    
      const size_type word_size = word.size();
    
      // SGML-like symbols are not prefixed
      if (word_size >= 3 && word[0] == '<' && word[word_size - 1] == '>')
	return word;
    
      symbol_set_type& __cache = const_cast<symbol_set_type&>(cache);
    
      if (word.id() >= __cache.size())
	__cache.resize(word.id() + 1, vocab_type::EMPTY);
      
      if (__cache[word.id()] == vocab_type::EMPTY) {
	
	UnicodeString uword = UnicodeString::fromUTF8(static_cast<const std::string&>(word));
	
	pimpl->operator()(uword);
      
	if (! uword.isEmpty()) {
	  std::string word_latin;
	  StringByteSink<std::string> __sink(&word_latin);
	  uword.toUTF8(__sink);
	
	  __cache[word.id()] = word_latin;
	} else
	  __cache[word.id()] = word;
      }
    
      return __cache[word.id()];
    }

  };
};

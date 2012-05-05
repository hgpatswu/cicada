//
//  Copyright(C) 2010-2011 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#include <utils/config.hpp>
#include <utils/thread_specific_ptr.hpp>

#include "attribute.hpp"

namespace cicada
{
  struct AttributeImpl
  {
    typedef Attribute::attribute_map_type attribute_map_type;
  };
  
  Attribute::ticket_type    Attribute::__mutex;
  
#ifdef HAVE_TLS
  static __thread AttributeImpl::attribute_map_type* attribute_maps_tls = 0;
  static boost::thread_specific_ptr<AttributeImpl::attribute_map_type> attribute_maps;
#else
  static utils::thread_specific_ptr<AttributeImpl::attribute_map_type> attribute_maps;
#endif
  
  Attribute::attribute_map_type& Attribute::__attribute_maps()
  {

#ifdef HAVE_TLS
    if (! attribute_maps_tls) {
      attribute_maps.reset(new attribute_map_type());
      attribute_maps->reserve(allocated());
      
      attribute_maps_tls = attribute_maps.get();
    }
    
    return *attribute_maps_tls;
#else
    if (! attribute_maps.get()) {
      attribute_maps.reset(new attribute_map_type());
      attribute_maps->reserve(allocated());
    }
    
    return *attribute_maps;
#endif
  }
};

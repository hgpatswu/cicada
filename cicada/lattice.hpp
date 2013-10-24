// -*- mode: c++ -*-
//
//  Copyright(C) 2010-2013 Taro Watanabe <taro.watanabe@nict.go.jp>
//

#ifndef __CICADA__LATTICE__HPP__
#define __CICADA__LATTICE__HPP__ 1

#include <vector>
#include <iostream>

#include <cicada/symbol.hpp>
#include <cicada/sentence.hpp>
#include <cicada/feature_vector.hpp>
#include <cicada/attribute_vector.hpp>

#include <utils/vector2.hpp>
#include <utils/piece.hpp>

namespace cicada
{
  // lattice class.. actually, this will also serve as base class for sentences......
  // currently, we will use this term for future extention to actual lattice..
  
  // lattice format:
  // JSON lattice format (recommended format!)
  // arc = ["double-quoted label", {"double-quoted-feature-name": 0.4, "another": 0.5}, 1] %%% label, feature(s), distance
  // arc-set = [arc, arc, ..., arc]
  // lattice = [arc-set, arc-set, ..., arc-set]
  
  // Python lattice format:
  // arc = ('single-quoted label', 0.4, 1) %%% label, cost, distance. cost is mapped to "lattice-cost" feature
  // arc-set = (arc, arc, arc, ..., arc,)
  // lattice = (arc-set, arc-set, ..., arc-set,)
  
  // The use of "shortest path" is motivated by:
  //
  // @InProceedings{dyer-muresan-resnik:2008:ACLMain,
  //   author    = {Dyer, Christopher  and  Muresan, Smaranda  and  Resnik, Philip},
  //   title     = {Generalizing Word Lattice Translation},
  //   booktitle = {Proceedings of ACL-08: HLT},
  //   month     = {June},
  //   year      = {2008},
  //   address   = {Columbus, Ohio},
  //   publisher = {Association for Computational Linguistics},
  //   pages     = {1012--1020},
  //   url       = {http://www.aclweb.org/anthology/P/P08/P08-1115}
  //  }


  
  class Lattice
  {
  public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    
    typedef cicada::Symbol symbol_type;
    typedef cicada::FeatureVector<double, std::allocator<double> > feature_set_type;
    typedef cicada::AttributeVector                                attribute_set_type;
    
    struct arc_type
    {
      arc_type() : features(), attributes(), label(), distance(0) {}
      arc_type(const symbol_type& __label) : features(), attributes(), label(__label), distance(1) {}
      arc_type(const symbol_type& __label,
	       const feature_set_type& __features,
	       const int& __distance)
	: features(__features), attributes(), label(__label), distance(__distance) {}
      arc_type(const symbol_type& __label,
	       const feature_set_type& __features,
	       const attribute_set_type& __attributes,
	       const int& __distance)
	: features(__features), attributes(__attributes), label(__label), distance(__distance) {}

      void swap(arc_type& x)
      {
	features.swap(x.features);
	attributes.swap(x.attributes);
	label.swap(x.label);
	std::swap(distance, x.distance);
      }
      
      feature_set_type   features;
      attribute_set_type attributes;
      symbol_type        label;
      int                distance;
    };
    
    typedef std::vector<arc_type, std::allocator<arc_type> > arc_set_type;
    typedef std::vector<arc_set_type, std::allocator<arc_set_type> >  lattice_type;

  private:
    typedef utils::vector2<int, std::allocator<int> >     distance_type;

  public:
    typedef lattice_type::value_type      value_type;
    
    typedef lattice_type::reference       reference;
    typedef lattice_type::const_reference const_reference;
    
    typedef lattice_type::iterator       iterator;
    typedef lattice_type::const_iterator const_iterator;
    
  public:
    Lattice() : lattice(), dist_short(), dist_long(), num_edge(0) {}
    Lattice(size_type size) : lattice(size), dist_short(), dist_long(), num_edge(0) {}
    Lattice(const utils::piece& x) { assign(x); }
    Lattice(const Sentence& x) : lattice(x.size()), dist_short(), dist_long(), num_edge(0)
    {
      iterator liter = lattice.begin();
      Sentence::const_iterator iter_end = x.end();
      for (Sentence::const_iterator iter = x.begin(); iter != iter_end; ++ iter, ++ liter)
	*liter = arc_set_type(1, arc_type(*iter));
    }
    
    void assign(const utils::piece& x);
    bool assign(std::string::const_iterator& iter, std::string::const_iterator end);
    bool assign(utils::piece::const_iterator& iter, utils::piece::const_iterator end);

    void resize(size_type size) { lattice.resize(size); }
    
    size_type size() const { return lattice.size(); }
    bool empty() const { return lattice.empty(); }
    
    size_type node_size() const { return lattice.size() + 1; }
    size_type edge_size() const { return (dist_short.empty() ? lattice.size() : num_edge); }
    
    inline const_reference operator[](size_type x) const { return lattice[x]; }
    inline       reference operator[](size_type x)       { return lattice[x]; }
    
    inline const_reference front() const { return lattice.front(); }
    inline       reference front()       { return lattice.front(); }

    inline const_reference back() const { return lattice.back(); }
    inline       reference back()       { return lattice.back(); }

    inline const_iterator begin() const { return lattice.begin(); }
    inline       iterator begin()       { return lattice.begin(); }
    
    inline const_iterator end() const { return lattice.end(); }
    inline       iterator end()       { return lattice.end(); }
    
    void push_back(const arc_set_type& arcs) { lattice.push_back(arcs); }
    
    difference_type shortest_distance() const
    {
      // shortest distance from begining to end of lattice..
      return shortest_distance(0, size());
    }
    
    difference_type longest_distance() const
    {
      // shortest distance from begining to end of lattice..
      return longest_distance(0, size());
    }
    
    difference_type shortest_distance(difference_type begin, difference_type end) const 
    {
      return (dist_short.empty() ? end - begin : dist_short(begin, end));
    }
    
    difference_type longest_distance(difference_type begin, difference_type end) const 
    {
      return (dist_long.empty() ? end - begin : dist_long(begin, end));
    }
    
    void swap(Lattice& x)
    {
      lattice.swap(x.lattice);
      dist_short.swap(x.dist_short);
      dist_long.swap(x.dist_long);
      std::swap(num_edge, x.num_edge);
    }

    void clear() { lattice.clear(); dist_short.clear(); dist_long.clear(); num_edge = 0; }

    void initialize_distance();
    
    void unite(const Lattice& x);
    
  public:
    friend
    std::ostream& operator<<(std::ostream& os, const Lattice& x);
    friend
    std::istream& operator>>(std::istream& is, Lattice& x);

    friend
    bool operator==(const Lattice::arc_type& x, const Lattice::arc_type& y);
    friend
    bool operator!=(const Lattice::arc_type& x, const Lattice::arc_type& y);
        
    friend
    bool operator==(const Lattice& x, const Lattice& y);
    friend
    bool operator!=(const Lattice& x, const Lattice& y);

  private:
    lattice_type  lattice;
    distance_type dist_short;
    distance_type dist_long;
    size_type     num_edge;
  };

  inline
  bool operator==(const Lattice::arc_type& x, const Lattice::arc_type& y)
  {
    return x.label == y.label && x.distance == y.distance && x.features == y.features && x.attributes == y.attributes;
  }
  inline
  bool operator!=(const Lattice::arc_type& x, const Lattice::arc_type& y)
  {
    return !(x == y);
  }

  inline
  bool operator==(const Lattice& x, const Lattice& y) { return x.lattice == y.lattice; }
  inline
  bool operator!=(const Lattice& x, const Lattice& y) { return x.lattice != y.lattice; }
  
};

namespace std
{
  inline
  void swap(cicada::Lattice& x, cicada::Lattice& y)
  {
    x.swap(y);
  }
  
};

#endif

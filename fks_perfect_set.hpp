/* PoC of an FKS-based perfect set.
 * https://en.wikipedia.org/wiki/Static_hashing#FKS_Hashing
 *
 * Copyright 2023 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef FKS_PERFECT_SET_HPP
#define FKS_PERFECT_SET_HPP

#include <algorithm>
#include <boost/config.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/core/bit.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/unordered/detail/foa/core.hpp> /* BOOST_UNORDERED_ASSUME */
#include <boost/unordered/detail/mulx.hpp>
#include <boost/unordered/detail/xmx.hpp>
#include <climits>
#include <numeric>
#include <utility>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "mulxp_hash.hpp"

namespace fks{

struct construction_failure:std::runtime_error
{
  construction_failure():
    std::runtime_error("could not construct the container"){}
};

struct duplicate_element:std::runtime_error
{
  duplicate_element():
    std::runtime_error("duplicate elements found"){}
};

struct duplicate_hash:std::runtime_error
{
  duplicate_hash():
    std::runtime_error("duplicate hash values found"){}
};

struct pow2_lower_size_policy
{
  static constexpr inline std::size_t size_index(std::size_t n)
  {
    auto exp=n<=2?1:((std::size_t)(boost::core::bit_width(n-1)));
    return (std::size_t(1)<<exp)-1;
  }

  static constexpr  inline std::size_t size(std::size_t size_index_)
  {
     return size_index_+1;
  }
    
  static constexpr std::size_t min_size(){return 2;}

  static constexpr inline std::size_t position(std::size_t hash,std::size_t size_index_)
  {
    return hash&size_index_;
  }
};

struct pow2_upper_size_policy
{
  static constexpr inline std::size_t size_index(std::size_t n)
  {
    return sizeof(std::size_t)*CHAR_BIT-
      (n<=2?1:((std::size_t)(boost::core::bit_width(n-1))));
  }

  static constexpr inline std::size_t size(std::size_t size_index_)
  {
     return std::size_t(1)<<(sizeof(std::size_t)*CHAR_BIT-size_index_);  
  }
    
  static constexpr std::size_t min_size(){return 2;}

  static constexpr inline std::size_t position(std::size_t hash,std::size_t size_index_)
  {
    return hash>>size_index_;
  }
};

template<
  typename T,typename Hash=boost::hash<T>,typename Pred=std::equal_to<T>
>
class perfect_set
{
  using element_array=std::vector<T>;
  using jump_size_policy=pow2_upper_size_policy;

public:
  static constexpr std::size_t default_lambda=4;
  using key_type=T;
  using value_type=T;
  using hasher=Hash;
  using key_equal=Pred;
  using iterator=typename element_array::const_iterator;

  template<typename FwdIterator>
  perfect_set(
    FwdIterator first,FwdIterator last,std::size_t lambda=default_lambda)
  {
    while(lambda){
      if(construct(first,last,lambda))return;
      lambda/=2;
    }
    throw construction_failure{};
  }

  iterator begin()const{return elements.begin();}
  iterator end()const{return elements.begin()+size_;}

  template<typename Key>
  BOOST_FORCEINLINE iterator find(const Key& x)const
  {
    auto pos=size_;
    if(pos){
      auto hash=h(x);
      auto jpos=jump_position(hash);
      pos=element_position(hash,positions[jpos],jumps[jpos]);
      if(!pred(x,elements[pos]))pos=size_;
    }
    return elements.begin()+pos;
  }

private:
  struct jump_info
  {
    void set(std::size_t shift,std::size_t width)
    {
      shl=64-shift-width;
      shr=64-width;
    }

    unsigned char shl=0,shr=64;
  };
  template<typename FwdIterator>
  struct bucket_node
  {
    FwdIterator  it;
    std::size_t  hash;
    bucket_node *next=nullptr;
  };
  template<typename FwdIterator>
  struct bucket_entry
  {
    bucket_node<FwdIterator> *begin=nullptr;
    std::size_t               size=0;
  };

  template<typename FwdIterator>
  bool construct(FwdIterator first,FwdIterator last,std::size_t lambda)
  {
    using bucket_node_array=std::vector<bucket_node<FwdIterator>>;
    using bucket_array=std::vector<bucket_entry<FwdIterator>>;

    size_=static_cast<std::size_t>(std::distance(first,last));
    jsize_index=jump_size_policy::size_index(size_/lambda);
    positions.resize(jump_size_policy::size(jsize_index));
    positions.shrink_to_fit();
    jumps.resize(positions.size());
    jumps.shrink_to_fit();

    elements.resize(size_);
    elements.shrink_to_fit();

    bucket_node_array bucket_nodes;
    bucket_array      buckets(jumps.size());
    bucket_nodes.reserve(size_);
    for(auto it=first;it!=last;++it){
      auto   hash=h(*it);
      auto  &root=buckets[jump_position(hash)];
      auto **ppnode=&root.begin;
      while(*ppnode){
        if((*ppnode)->hash==hash){
          if(pred(*((*ppnode)->it),*(it)))throw duplicate_element{};
          else                            throw duplicate_hash{};
        }
        ppnode=&(*ppnode)->next;
      }
      bucket_nodes.push_back({it,hash});
      *ppnode=&bucket_nodes.back();
      ++root.size;
    }

    std::vector<std::size_t> sorted_bucket_indices(buckets.size());
    std::iota(sorted_bucket_indices.begin(),sorted_bucket_indices.end(),0u);
    std::sort(
      sorted_bucket_indices.begin(),sorted_bucket_indices.end(),
      [&](std::size_t i1,std::size_t i2){
        return buckets[i1].size>buckets[i2].size;
      });

    boost::dynamic_bitset<>  mask;
    mask.resize(size_,true); /* true --> available */
    std::vector<std::size_t> offsets;

    for(std::size_t i=0;i<buckets.size();++i){
      const auto& bucket=buckets[sorted_bucket_indices[i]];
      if(!bucket.size)break; /* remaining buckets also empty*/

      std::size_t max_off=boost::core::bit_ceil(bucket.size)-1,
                  min_wd=boost::core::popcount(max_off);

      for(unsigned char sh=0;sh<=64-min_wd;++sh){
        for(unsigned char wd=min_wd;wd<64;++wd){
          jump_info jmp;
          jmp.set(sh,wd);

          offsets.clear();
          for(auto pnode=bucket.begin;pnode;pnode=pnode->next){
            auto off=element_offset(pnode->hash,jmp);
            if(std::find(offsets.begin(),offsets.end(),off)!=offsets.end()){
              goto next_wd;
            }
            offsets.push_back(off);
          }

          for(std::size_t pos=0;pos+max_off<size_;++pos){
            for(auto off:offsets){
              if(pos+off>=size_||!mask[pos+off])goto next_pos;
            }
            {
              auto pnode=bucket.begin;
              for(auto off:offsets){
                elements[pos+off]=*(pnode->it);
                mask[pos+off]=false;
                pnode=pnode->next;
              }
              positions[sorted_bucket_indices[i]]=pos;
              jumps[sorted_bucket_indices[i]]=jmp;
              goto next_jmp;
            }
          next_pos:;
          }
          goto next_wd;
        next_wd:;
        }
      }
      return false;
    next_jmp:;
    }

    return true;
  }

  std::size_t inline jump_position(std::size_t hash)const
  {
    return jump_size_policy::position(hash,jsize_index);
  }

  static inline std::size_t element_offset(std::size_t hash,const jump_info& jmp)
  {
    return (hash<<jmp.shl)>>jmp.shr;
  }

  static inline std::size_t element_position(
    std::size_t hash,std::size_t pos,const jump_info& jmp)
  {
    return pos+element_offset(hash,jmp);
  }

  hasher                   h;
  key_equal                pred;
  std::size_t              size_;
  std::size_t              jsize_index;
  std::vector<std::size_t> positions;
  std::vector<jump_info>   jumps;
  element_array            elements;
};

} /* namespace fks */

#endif


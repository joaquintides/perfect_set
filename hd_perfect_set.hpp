/* PoC of an HD(C)-based perfect set.
 * https://cmph.sourceforge.net/papers/esa09.pdf
 *
 * Copyright 2023 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef HD_PERFECT_SET_HPP
#define HD_PERFECT_SET_HPP

#include <algorithm>
#include <boost/config.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/core/bit.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/unordered/detail/mulx.hpp>
#include <boost/unordered/detail/xmx.hpp>
#include <climits>
#include <numeric>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>
#include "mulxp_hash.hpp"

namespace hd{

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
  static inline std::size_t size_index(std::size_t n)
  {
    auto exp=n<=2?1:((std::size_t)(boost::core::bit_width(n-1)));
    return (std::size_t(1)<<exp)-1;
  }

  static inline std::size_t size(std::size_t size_index_)
  {
     return size_index_+1;
  }
    
  static constexpr std::size_t min_size(){return 2;}

  static inline std::size_t position(std::size_t hash,std::size_t size_index_)
  {
    return hash&size_index_;
  }
};

struct pow2_upper_size_policy
{
  static inline std::size_t size_index(std::size_t n)
  {
    return sizeof(std::size_t)*CHAR_BIT-
      (n<=2?1:((std::size_t)(boost::core::bit_width(n-1))));
  }

  static inline std::size_t size(std::size_t size_index_)
  {
     return std::size_t(1)<<(sizeof(std::size_t)*CHAR_BIT-size_index_);  
  }
    
  static constexpr std::size_t min_size(){return 2;}

  static inline std::size_t position(std::size_t hash,std::size_t size_index_)
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
  using displacement_size_policy=pow2_lower_size_policy;
  using element_size_policy=pow2_upper_size_policy;

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
  iterator end()const{return end_;}

  template<typename Key>
  BOOST_FORCEINLINE iterator find(const Key& x)const
  {
    auto hash=h(x);
    auto pos=element_position(hash,displacements[displacement_position(hash)]);
    if(pos>=size_||!pred(x,elements[pos]))pos=size_;
    return elements.begin()+pos;
  }

private:
  using displacement_info=std::pair<std::size_t,std::size_t>;
  template<typename FwdIterator>
  struct bucket_entry
  {
    FwdIterator it;
    std::size_t hash;
  };

  template<typename FwdIterator>
  bool construct(FwdIterator first,FwdIterator last,std::size_t lambda)
  {
    using bucket_array=std::vector<std::vector<bucket_entry<FwdIterator>>>;

    size_=static_cast<std::size_t>(std::distance(first,last));
    dsize_index=displacement_size_policy::size_index(size_/lambda);
    displacements.resize(displacement_size_policy::size(dsize_index));
    displacements.shrink_to_fit();

    /* extended_size is a power of two strictly larger than the element array
     * size. Construction and lookup work as if with a virtual extended array
     * whose positions from size_ are taken up. 
     */

    size_index=element_size_policy::size_index(size_+1);
    auto extended_size=element_size_policy::size(size_index);
    elements.resize(size_);
    elements.shrink_to_fit();
    end_=elements.end();
    bucket_array buckets(displacements.size());
    for(auto it=first;it!=last;++it){
      auto hash=h(*it);
      buckets[displacement_position(hash)].push_back({it,hash});
    }

    std::vector<std::size_t> sorted_bucket_indices(buckets.size());
    std::iota(sorted_bucket_indices.begin(),sorted_bucket_indices.end(),0u);
    std::sort(
      sorted_bucket_indices.begin(),sorted_bucket_indices.end(),
      [&](std::size_t i1,std::size_t i2){
        return buckets[i1].size()>buckets[i2].size();
      });

    boost::dynamic_bitset<> mask(size_),mask1(size_);

    for(std::size_t i=0;i<buckets.size();++i){
      const auto& bucket=buckets[sorted_bucket_indices[i]];
      if(bucket.empty())return true; /* remaining buckets also empty */

      for(std::size_t j=0;j<bucket.size();++j){
        for(std::size_t k=0;k<j;++k){
          if(bucket[j].hash==bucket[k].hash){
            if(pred(*(bucket[j].it),*(bucket[k].it)))throw duplicate_element{};
            else                                     throw duplicate_hash{};
          }
        }
      }

      for(std::size_t d0=0;d0<extended_size;++d0){
        for(std::size_t d1=0;d1<extended_size;++d1){
          /* this calculation critically depends on displacement_size_policy */
          displacement_info d={d0<<size_index,(d1<<32)+1};

          mask1=mask;
          for(std::size_t j=0;j<bucket.size();++j){
            auto pos=element_position(bucket[j].hash,d);
            if(pos>=size_||mask1[pos])goto next_displacement;
            mask1[pos]=1;
          }
          displacements[i]=d;
          mask=mask1;
          for(std::size_t j=0;j<bucket.size();++j){
            auto pos=element_position(bucket[j].hash,d);
            elements[pos]=*(bucket[j].it);
          }
          goto next_bucket;
          next_displacement:;
        }
      }
      return false;
    next_bucket:;
    }
    return true;
  }

  std::size_t displacement_position(std::size_t hash)const
  {
    return displacement_size_policy::position(hash,dsize_index);
  }

  std::size_t element_position(
    std::size_t hash,const displacement_info& d)const
  {
    return element_size_policy::position(d.first+d.second*hash,size_index);
  }

  hasher                         h;
  key_equal                      pred;
  std::size_t                    size_;
  std::size_t                    dsize_index;
  std::vector<displacement_info> displacements;
  std::size_t                    size_index;
  element_array                  elements;
  iterator                       end_;
};

/* some mixers */

struct mulx_hash
{
  std::size_t operator()(std::size_t x)const
  {
    return boost::unordered::detail::mulx(x);
  }
};

struct xmx_hash
{
  std::size_t operator()(std::size_t x)const
  {
    return boost::unordered::detail::xmx(x);
  }
};

struct xm_hash
{
  std::size_t operator()(std::size_t x)const
  {
    x ^= x >> 23;
    x *= 0xff51afd7ed558ccdull;

    return x;
  }
};

struct m_hash
{
  std::size_t operator()(std::size_t x)const
  {
    x *= 0xff51afd7ed558ccdull;
    return x;
  }
};

struct mbs_hash
{
  std::size_t operator()(std::size_t x)const
  {
    x *= 0xff51afd7ed558ccdull;
    return boost::core::byteswap(x);
  }
};

struct mulxp3_string_hash
{
  inline std::uint64_t operator()(const std::string& x,std::uint64_t seed = 0) const
  {
      unsigned char const * p = reinterpret_cast<unsigned char const *>(x.data());
      std::size_t n = x.size();

      std::uint64_t const q = 0x9e3779b97f4a7c15ULL;
      std::uint64_t const k = q * q;

      std::uint64_t w = seed;
      std::uint64_t h = w ^ n;

      while( n >= 16 )
      {
          std::uint64_t v1 = read64le( p + 0 );
          std::uint64_t v2 = read64le( p + 8 );

          w += q;
          h ^= mulx( v1 + w, v2 + w + k );

          p += 16;
          n -= 16;
      }

      {
          std::uint64_t v1 = 0;
          std::uint64_t v2 = 0;

          if( n > 8 )
          {
              v1 = read64le( p );
              v2 = read64le( p + n - 8 ) >> ( 16 - n ) * 8;
          }
          else if( n >= 4 )
          {
              v1 = (std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8 | read32le( p );
          }
          else if( n >= 1 )
          {
              std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
              std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

              v1 = (std::uint64_t)p[ x1 ] << x1 * 8 | (std::uint64_t)p[ x2 ] << x2 * 8 | (std::uint64_t)p[ 0 ];
          }

          w += q;
          h ^= mulx( v1 + w, v2 + w + k );
      }

      return h;
  }
};

} /* namespace hd */

#endif


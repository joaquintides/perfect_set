/* Measuring lookup performance of hd::perfect_set.
 *
 * Copyright 2023 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>

std::chrono::high_resolution_clock::time_point measure_start,measure_pause;

template<typename F>
double measure(F f)
{
  using namespace std::chrono;

  static const int              num_trials=10;
  static const milliseconds     min_time_per_trial(200);
  std::array<double,num_trials> trials;

  for(int i=0;i<num_trials;++i){
    int                               runs=0;
    high_resolution_clock::time_point t2;
    volatile decltype(f())            res; /* to avoid optimizing f() away */

    measure_start=high_resolution_clock::now();
    do{
      res=f();
      ++runs;
      t2=high_resolution_clock::now();
    }while(t2-measure_start<min_time_per_trial);
    trials[i]=duration_cast<duration<double>>(t2-measure_start).count()/runs;
  }

  std::sort(trials.begin(),trials.end());
  return std::accumulate(
    trials.begin()+2,trials.end()-2,0.0)/(trials.size()-4);
}

void pause_timing()
{
  measure_pause=std::chrono::high_resolution_clock::now();
}

void resume_timing()
{
  measure_start+=std::chrono::high_resolution_clock::now()-measure_pause;
}

#include <algorithm>
#include <boost/bind/bind.hpp>
#include <boost/core/detail/splitmix64.hpp>
#include <boost/unordered/unordered_set.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <iostream>
#include <random>
#include <string>
#include "hd_perfect_set.hpp"

struct splitmix64_urng:boost::detail::splitmix64
{
  using boost::detail::splitmix64::splitmix64;
  using result_type=boost::uint64_t;

  static constexpr result_type (min)(){return 0u;}
  static constexpr result_type(max)()
  {return (std::numeric_limits<result_type>::max)();}
};

struct find_all
{
  using result_type=std::size_t;

  template<typename FwdIterator,typename Container>
  BOOST_NOINLINE result_type operator()(
    FwdIterator first,FwdIterator last,const Container& c)const
  {
    std::size_t res=0;
    while(first!=last){
      if(c.find(*first++)!=c.end())++res;
    }
    return res;
  }
};

template<
  typename Container1,typename Container2,typename Container3,
  typename Data,typename Input
>
void test(
  const char* title,const Data& data,const Input& input,
  const char* name1,const char* name2,const char* name3)
{
  std::cout<<title<<":"<<std::endl;
  std::cout<<name1<<";"<<name2<<";"<<name3<<std::endl;

  unsigned int n0=10,dn=10;
  double       fdn=1.1;
  for(unsigned int n=n0;n<=data.size();n+=dn,dn=(unsigned int)(dn*fdn)){
    auto  first=data.begin(),last=data.begin()+n;
    Input expanded_input;
    auto  m=std::max(1,(int)(1000/n));
    for(auto i=m;i--;){
      expanded_input.insert(expanded_input.end(),input.begin(),input.begin()+n);
    }
    std::shuffle(
      expanded_input.begin(),expanded_input.end(),splitmix64_urng{31321});
    auto ifirst=expanded_input.begin(),ilast=expanded_input.end();

    std::cout<<n<<";";
    {
      Container1 s(first,last);
      std::cout<<measure(boost::bind(find_all{},ifirst,ilast,boost::cref(s)))*1E9/m/n<<";";
    }
    {
      Container2 s(first,last);
      std::cout<<measure(boost::bind(find_all{},ifirst,ilast,boost::cref(s)))*1E9/m/n<<";";
    }
    {
      Container3 s(first,last);
      std::cout<<measure(boost::bind(find_all{},ifirst,ilast,boost::cref(s)))*1E9/m/n<<"\n";
    }
  }
}
  
static std::string make_string(std::size_t x)
{
  char buffer[128];
  std::snprintf(buffer,sizeof(buffer),"pfx_%zu_sfx",x);
  return buffer;
}

int main()
{
  static constexpr std::size_t N=100'000;
  {
    using value_type=std::size_t;

    std::mt19937                               gen(0);
    std::uniform_int_distribution<std::size_t> dist;
    std::vector<value_type>                    data;

    for(std::size_t i=0;i<N;++i)data.push_back(dist(gen));

    using container_t1=boost::unordered_set<value_type>;
    using container_t2=boost::unordered_flat_set<value_type>;
    using container_t3=hd::perfect_set<value_type,hd::mbs_hash>;

    test<container_t1,container_t2,container_t3>
    (
      "Successful find, integers",
      data,data,
      "boost::unordered_set",
      "boost::unordered_flat_set",
      "hd::perfect_set"
    );
  }
  {
    using value_type=std::string;

    std::mt19937                               gen(0);
    std::uniform_int_distribution<std::size_t> dist;
    std::vector<value_type>                    data;

    for(std::size_t i=0;i<N;++i)data.push_back(make_string(dist(gen)));

    using container_t1=boost::unordered_set<value_type>;
    using container_t2=boost::unordered_flat_set<value_type>;
    using container_t3=hd::perfect_set<value_type,hd::mulxp3_string_hash>;

    test<container_t1,container_t2,container_t3>
    (
      "Successful find, strings",
      data,data,
      "boost::unordered_set",
      "boost::unordered_flat_set",
      "hd::perfect_set"
    );
  }
}

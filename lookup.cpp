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
#include <boost/mp11/algorithm.hpp>
#include <boost/unordered/unordered_set.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <initializer_list>
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

template<typename T>
struct type_identity{using type=T;};

template<typename Containers,typename Data,typename Input>
void test(
  const char* title,std::initializer_list<const char*> names,
  const Data& data,const Input& input)
{
  std::cout<<title<<":"<<std::endl;
  for(const auto& name:names)std::cout<<name<<";";
  std::cout<<std::endl;

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

    boost::mp11::mp_for_each<
      boost::mp11::mp_transform<type_identity,Containers>
    >([&](auto t_){
      using Container=typename decltype(t_)::type;
      Container s(first,last);
      std::cout
        <<measure(boost::bind(find_all{},ifirst,ilast,boost::cref(s)))*1E9/m/n
        <<";";
    });
    std::cout<<std::endl;
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

    using containers=boost::mp11::mp_list<
      boost::unordered_set<value_type>,
      boost::unordered_flat_set<value_type>,
      hd::perfect_set<value_type,hd::mbs_hash>,
      hd::perfect_set<value_type,hd::mulx_hash>,
      hd::perfect_set<value_type,hd::xm_hash>,
      hd::perfect_set<value_type,hd::m_hash>
    >;
    auto names={
      "boost::unordered_set",
      "boost::unordered_flat_set",
      "hd::perfect_set mbs",
      "hd::perfect_set mulx",
      "hd::perfect_set xm",
      "hd::perfect_set m",
    };

    test<containers>("Successful find, integers",names,data,data);

    auto input=data;
    for(std::size_t i=0;i<input.size();i+=2)input[i]+=1;

    test<containers>("50/50 find, integers",names,data,input);

    for(std::size_t i=1;i<input.size();i+=2)input[i]+=1;

    test<containers>("Unsuccessful find, integers",names,data,input);
  }
  {
    using value_type=std::string;

    std::mt19937                               gen(0);
    std::uniform_int_distribution<std::size_t> dist;
    std::vector<value_type>                    data;

    for(std::size_t i=0;i<N;++i)data.push_back(make_string(dist(gen)));

    using containers=boost::mp11::mp_list<
      boost::unordered_set<value_type>,
      boost::unordered_flat_set<value_type>,
      hd::perfect_set<value_type,hd::mulxp3_string_hash>
    >;
    auto names={
      "boost::unordered_set",
      "boost::unordered_flat_set",
      "hd::perfect_set"
    };

    test<containers>("Successful find, strings",names,data,data);

    auto input=data;
    for(std::size_t i=0;i<input.size();i+=2)input[i][input[i].size()/2]='*';

    test<containers>("50/50 find, strings",names,data,input);

    for(std::size_t i=1;i<input.size();i+=2)input[i][input[i].size()/2]='*';

    test<containers>("Unsuccessful find, strings",names,data,input);
  }
}

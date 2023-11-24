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

#include <boost/bind/bind.hpp>
#include <boost/core/detail/splitmix64.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#define FROZEN_LETITGO_HAS_STRING_VIEW
#include <frozen/unordered_set.h>
#include <frozen/string.h>
#include <iostream>
#include <string_view>
#include "hd_perfect_set.hpp"
#include "hd_constexpr_perfect_set.hpp"

static constexpr frozen::string entitiesf[]
{
#include "html_entities"
};
static constexpr auto entities_size=sizeof(entitiesf)/sizeof(entitiesf[0]);

static constexpr std::string_view entitiesv[] 
{
#include "html_entities"
};

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

struct default_constructible_frozen_string:frozen::string
{
  using frozen::string::basic_string;
  constexpr default_constructible_frozen_string():frozen::string{"",0}{}
  constexpr default_constructible_frozen_string(const frozen::string& x):
    frozen::string(x){}
};

constexpr auto cfs=frozen::make_unordered_set(entitiesf);
constexpr hd::constexpr_perfect_set<
  std::string_view,entities_size,hd::mulxp3_string_hash
> ccps(entitiesv);

int main()
{
  hd::constexpr_perfect_set<
    std::string_view,
    entities_size,
    hd::mulxp3_string_hash>  cps(entitiesv);
  hd::perfect_set<
    std::string_view,
    hd::mulxp3_string_hash>  ps(&entitiesv[0],&entitiesv[entities_size]);
  boost::unordered_flat_set<
    std::string_view,
    hd::mulxp3_string_hash>  ufsm(&entitiesv[0],&entitiesv[entities_size]);
  boost::unordered_flat_set<
    std::string_view>        ufs(&entitiesv[0],&entitiesv[entities_size]);

  std::vector<std::string> input;
  for(int i=0;i<10;++i){
    input.insert(input.end(),&entitiesv[0],&entitiesv[entities_size]);
  }
  std::shuffle(input.begin(),input.end(),splitmix64_urng{312811});
  std::vector<std::string_view> input_view(input.begin(),input.end());
  auto first=input_view.begin(),last=input_view.end();
  auto n=input_view.size();

  std::cout<<find_all{}(first,last,cfs)<<"\n";
  std::cout<<find_all{}(first,last,ccps)<<"\n";
  std::cout<<find_all{}(first,last,cps)<<"\n";
  std::cout<<find_all{}(first,last,ps)<<"\n";
  std::cout<<find_all{}(first,last,ufsm)<<"\n";
  std::cout<<find_all{}(first,last,ufs)<<"\n";

  std::cout<<"cfs;ccps;cps;ps;ufsm;ufs;\n";

  auto run_measures=[&]{
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(cfs)))*1E9/n<<";";
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(ccps)))*1E9/n<<";";
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(cps)))*1E9/n<<";";
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(ps)))*1E9/n<<";";
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(ufsm)))*1E9/n<<";";
    std::cout<<measure(boost::bind(find_all{},first,last,boost::cref(ufs)))*1E9/n<<";";
    std::cout<<std::endl;
  };

  run_measures();

  for(std::size_t i=0;i<n;i+=2)input[i][input[i].size()/2]='*';
  run_measures();

  for(std::size_t i=1;i<n;i+=2)input[i][input[i].size()/2]='*';
  run_measures();
}


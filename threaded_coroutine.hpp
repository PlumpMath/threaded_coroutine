#include <thread>
#include <condition_variable>
#include <memory>
#include <tuple>
#include <functional>
#include <atomic>

namespace blocks
{

template<int... I> struct intSequence{};

template<int N, int... T>
struct makeIntSequence 
{
 typedef typename makeIntSequence<N-1,N-1,T...>::seq seq; 
};

template<int... T>
struct makeIntSequence<0,T...> 
{
   typedef intSequence<T...> seq; 
};

template<class Routine, class... RoutineArguments, int... UnpackTuple > 
auto callWithTupleI(Routine routine, 
         std::tuple<RoutineArguments...> arguments, 
         intSequence<UnpackTuple...> pattenMatch) 
-> decltype(routine(std::get<UnpackTuple>(arguments)...))
{
   pattenMatch; // stop compiler unused variable warning
   return routine(std::get<UnpackTuple>(arguments)...);
}

template<class F, class... A> 
auto callWithTuple(F fn, std::tuple<A...> t) 
-> decltype(callWithTupleI(fn,t,typename makeIntSequence<sizeof...(A)>::seq()))
{
   return callWithTupleI(fn,t,typename makeIntSequence<sizeof...(A)>::seq());
}


template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto yield_rtn_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)> block, Block_Args... args)
-> std::tuple<bool,Yield_Args...>
{

  return std::tuple<bool,Yield_Args...>();
}

template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto yield_rtn_type(void(*block)(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...), Block_Args... block_args)
->decltype(block_rtn_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block),block_args...))
{
   return block_rtn_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block),block_args...);
}



// Compliers I am using will not substitue first then match template parameters.
#if 0
template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
using Block_Type_Functor = std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>;

template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
using Block_Type_Function = void(*)(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...);
#endif

template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto make(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)> block, Block_Args... args)
-> std::shared_ptr<std::function<std::tuple<bool,Yield_Args...>(Yield_Rtn...)>>
{

   auto yield_rtn = std::make_shared<std::tuple<bool,Yield_Rtn...>>();
   auto yield_args = std::make_shared<std::tuple<bool,Yield_Args...>>();
   auto coroutine_alive = std::make_shared<bool>(true);
   auto main_alive = std::make_shared<bool>(true);
   auto yield_rtn_read = std::make_shared<bool>(true); // Must be true to avoid deadlocks
   auto yield_args_read = std::make_shared<bool>(true); // Must be true to avoid deadlocks
  
	auto condition_var_read = std::make_shared<std::condition_variable>();
   auto condition_var_write = std::make_shared<std::condition_variable>();
   
   auto mutex = std::make_shared<std::mutex>();
  
   auto read_yield_rtn = [=](std::unique_lock<std::mutex> &&unique_lock) {
      std::tuple<bool,Yield_Rtn...> tmp;
      START:
      if(!*yield_rtn_read) {
         tmp = *yield_rtn;
         *yield_rtn_read = true;
         condition_var_write->notify_one();   
      }else {
         condition_var_read->wait(unique_lock);
         goto START;
      }
      
      return tmp;
   };
  
   // Duplicating to avoid functions with 6 arguments as VC2013 doesn't support
   // generic lambdas.
   auto read_yield_args = [=](std::unique_lock<std::mutex> &&unique_lock) {
      std::tuple<bool,Yield_Args...> tmp;

      START:
      if(!*yield_args_read) {
         tmp = *yield_args;
         *yield_args_read = true;
         condition_var_write->notify_one();   
      }else {
         condition_var_read->wait(unique_lock);
         goto START;
      }
      
      return tmp;
   };
   
   auto write_yield_args = [=](std::unique_lock<std::mutex> &&unique_lock, std::tuple<bool,Yield_Args...> tmp ) {
      START:
      if(*yield_args_read) {
         *yield_args = tmp;
         *yield_args_read = false;
         condition_var_read->notify_one();   
      }else {
         condition_var_write->wait(unique_lock);
         goto START;
      }
   };   
   
   auto write_yield_rtn = [=](std::unique_lock<std::mutex> &&unique_lock, std::tuple<bool,Yield_Rtn...> tmp ) {
      START:
      if(*yield_rtn_read) {
         *yield_rtn = tmp;
         *yield_rtn_read = false;
         condition_var_read->notify_one();   
      }else {
         condition_var_write->wait(unique_lock);
         goto START;
      }
   };
    
   auto yield = [=](Yield_Args... a) {
      std::tuple<bool,Yield_Rtn...> tmp;
      std::unique_lock<std::mutex> unique_lock(*mutex);
      
      if (*main_alive) {
         tmp = read_yield_rtn(std::move(unique_lock));
         write_yield_args(std::move(unique_lock),std::make_tuple(true,a...));
         return tmp;
      }else {
         return *yield_rtn;
      }
   }; 

   auto coroutine = [=](Block_Args... args) {
      block(yield,args...);
      std::unique_lock<std::mutex> unique_lock(*mutex);
      std::get<0>(*yield_args) = false;
      *coroutine_alive = false;
      if(*main_alive) {
         // Order of functions is important
         read_yield_rtn(std::move(unique_lock));
         write_yield_args(std::move(unique_lock),*yield_args);
      }
   };
 
   std::thread(coroutine,args...).detach();
  
   std::function<std::tuple<bool,Yield_Args...>(Yield_Rtn...)> main = [=](Yield_Rtn... a)-> std::tuple<bool,Yield_Args...>
   {
      std::tuple<bool,Yield_Args...> tmp;
      std::unique_lock<std::mutex> unique_lock(*mutex);
      
      if (*coroutine_alive) {
         // Order of functions is important
         write_yield_rtn(std::move(unique_lock),std::make_tuple(true,a...)); 
         tmp = read_yield_args(std::move(unique_lock));      
         return tmp;
      }else {
         return *yield_args;
      }     
  
  
   };

  
   auto main_destructor = [=](decltype(main) * ptr) {

      std::unique_lock<std::mutex> unique_lock(*mutex);
      std::get<0>(*yield_rtn) = false;
      *main_alive = false;
      if(*coroutine_alive) {
         write_yield_rtn(std::move(unique_lock),*yield_rtn); 
         read_yield_args(std::move(unique_lock));
      }

      delete ptr;
   };
    
    return std::shared_ptr<decltype(main)>( new decltype(main)(main),main_destructor);

}


template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto make(void(*block)(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...), Block_Args... block_args)
->decltype(make(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block),block_args...))
{
   return make(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block),block_args...);
}

template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto block_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)> block)
-> std::shared_ptr<std::function<std::tuple<bool,Yield_Args...>(Yield_Rtn...)>>
{
   return std::shared_ptr<std::function<std::tuple<bool,Yield_Args...>(Yield_Rtn...)>>();
}

template<class... Yield_Rtn, class... Yield_Args, class... Block_Args>
auto block_type(void(*block)(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...))
->decltype(block_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block)))
{
   return block_type(std::function<void(std::function<std::tuple<bool,Yield_Rtn...>(Yield_Args...)>,Block_Args...)>(block));
}

}


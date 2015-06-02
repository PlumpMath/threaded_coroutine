#include "threaded_coroutine.hpp"
#include <iostream>
#include <cstdint>

void generate_nat(std::function<std::tuple<bool>(std::uint64_t)> yield, std::uint64_t c) {

   bool valid = true;

   while(valid) {

      std::tie(valid) = yield(c);
      ++c; 
            
   }
}

std::function<void(std::function<std::tuple<bool>(std::uint64_t)>, decltype(blocks::block_type(generate_nat)))> filter_n(std::uint64_t n) {

return [=](std::function<std::tuple<bool>(std::uint64_t)> yield, decltype(blocks::block_type(generate_nat)) co) {   
   bool valid;
   std::uint64_t c;
   
   std::tie(valid,c) = (*co)();
   
   while(valid) {
      if( c % n) {
         yield(c);
      }
      std::tie(valid,c) = (*co)();
   }
   };
}


void print_prime(std::uint64_t n,  decltype(blocks::block_type(generate_nat)) co) {
   
   bool valid = true;
   while(valid) {
   std::cout << n << '\n';

   std::tie(valid,n) = (*co)();
   co = blocks::make(filter_n(std::uint64_t(n)),co);
   }
} 

int main() {
   auto nat = blocks::make(generate_nat,std::uint64_t(2));
   auto odd = blocks::make(filter_n(std::uint64_t(2)),nat);
   print_prime(std::uint64_t(2),odd);    
	return 0;
}
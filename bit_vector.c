#include "bit_vector.h"

#define BITS_COUNT (sizeof(unsigned int)*8)

//bitmap for used process control blocks
static unsigned int bitmap = 0; //32 bits in one int

//toggle bit
static int toggle(int bit){
	int mask = 1 << bit;
	bitmap ^= mask;
	return (bitmap & mask);
}

//test bit
static int test(int b){
	return (bitmap & (1 << b)) >> b;
}

//find unset bit in bitmap
int search_bitvector(const int max_bit){
  int bit;
  for(bit=0; bit < max_bit; bit++){
    if(test(bit) == 0){
      toggle(bit);
      return bit;
    }
  }
  return -1;
}

void unset_bit(const int bit){
  toggle(bit);
}

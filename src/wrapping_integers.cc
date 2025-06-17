#include "wrapping_integers.hh"
#include <algorithm>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32 { static_cast<uint32_t>((n + zero_point.raw_value_) % (1UL << 32))};
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  constexpr uint64_t u32 = 1UL << 32;
  const uint32_t offset = raw_value_ - zero_point.raw_value_;

  // Start with a candidate in the same "big window" as the checkpoint.
  uint64_t candidate_curr = ( checkpoint & 0xFFFFFFFF00000000 ) + offset;

  auto dist = []( uint64_t a, uint64_t b ) { return std::max( a, b ) - std::min( a, b ); };

  uint64_t ret = candidate_curr;
  uint64_t min_dist = dist( ret, checkpoint );

  // Check the candidate in the previous 2^32 window, if it exists.
  if ( candidate_curr >= u32 ) {
    uint64_t candidate_prev = candidate_curr - u32;
    uint64_t dist_prev = dist( candidate_prev, checkpoint );
    if ( dist_prev < min_dist ) {
      min_dist = dist_prev;
      ret = candidate_prev;
    }
  }

  // Check the candidate in the next 2^32 window.
  uint64_t candidate_next = candidate_curr + u32;
  uint64_t dist_next = dist( candidate_next, checkpoint );
  if ( dist_next < min_dist ) {
    ret = candidate_next;
  }

  return ret;
}

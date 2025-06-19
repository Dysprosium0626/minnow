#include "reassembler.hh"
#include <vector>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( ( first_index == 0 && data.empty() ) || ( first_index + data.size() < output_.writer().bytes_pushed() ) ) {
    if ( is_last_substring ) {
      output_.writer().close();
    }
    return;
  }

  if ( output_.writer().available_capacity() == 0 ) {
    return;
  }

  if ( is_last_substring ) {
    last_index_ = first_index + data.size();
  }

  // if duplicate, truncate the data
  if ( first_index < output_.writer().bytes_pushed() ) {
    data = data.substr( output_.writer().bytes_pushed() - first_index,
                        data.size() - ( output_.writer().bytes_pushed() - first_index ) );
    first_index = output_.writer().bytes_pushed();
  }

  // push immediately
  if ( first_index == output_.writer().bytes_pushed() ) {
    output_.writer().push( data );
    // search any avaliable data
    auto next_index = first_index + data.size();
    while ( !buffer_first_index_set_.empty() && next_index >= *buffer_first_index_set_.begin() ) {
      auto next_in_buffer_index = *buffer_first_index_set_.begin();
      auto next_data = buffer_[next_in_buffer_index];
      if ( next_in_buffer_index + next_data.size() < output_.writer().bytes_pushed() ) {
        bytes_pending_ -= next_data.size();
        buffer_.erase( next_in_buffer_index );
        buffer_first_index_set_.erase( next_in_buffer_index );
        continue;
      }
      bytes_pending_ -= next_data.size();
      next_data = next_data.substr( next_index - next_in_buffer_index,
                                    next_data.size() - ( next_index - next_in_buffer_index ) );
      output_.writer().push( next_data );
      buffer_.erase( next_in_buffer_index );
      buffer_first_index_set_.erase( next_in_buffer_index );
      next_index = next_index + next_data.size();
    }
    if ( output_.writer().bytes_pushed() == last_index_ ) {
      output_.writer().close();
      return;
    }
  } else if ( first_index < output_.writer().bytes_pushed() + output_.writer().available_capacity() ) {
    if ( is_last_substring ) {
      last_index_ = first_index + data.size();
    }
    data = data.substr(
      0,
      std::min( data.size(),
                output_.writer().available_capacity() - ( first_index - output_.writer().bytes_pushed() ) ) );
    uint64_t delta_size = 0;
    std::vector<uint64_t> erase_indexes;
    bool should_insert = true;
    for ( const auto& [index, buffer_data] : buffer_ ) {
      // for head data
      if ( index < first_index ) {
        if ( index + buffer_data.size() <= first_index ) {
          continue;
        } else if ( index + buffer_data.size() > first_index
                    && index + buffer_data.size() < first_index + data.size() ) {
          data = data.substr( index + buffer_data.size() - first_index,
                              data.size() - ( index + buffer_data.size() - first_index ) );
          first_index = index + buffer_data.size();
        } else if ( index + buffer_data.size() >= first_index + data.size() ) {
          should_insert = false;
        }
      }
      // for middle data
      if ( index >= first_index && index < first_index + data.size() ) {
        if ( index + buffer_data.size() <= first_index + data.size() ) {
          erase_indexes.emplace_back( index );
          delta_size += buffer_data.size();
        } else if ( index + buffer_data.size() > first_index + data.size() ) {
          data = data.substr( 0, index - first_index );
        }
      }
      // for tail data
      if ( index >= first_index + data.size() ) {
        continue;
      }
    }
    for ( const auto index : erase_indexes ) {
      buffer_.erase( index );
    }
    if ( should_insert && data.size() > 0 ) {
      buffer_[first_index] = data;
      buffer_first_index_set_.insert( first_index );
      bytes_pending_ += data.size() - delta_size;
    }
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  return bytes_pending_;
}

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), total_count_( 0 ), size_( 0 ), close_( false ), buffer_() {}

void Writer::push( string data )
{
  if ( is_closed() ) {
    return;
  }

  if ( data.empty() || available_capacity() == 0 ) {
    return;
  }
  
  std::size_t push_size = std::min( data.length(), available_capacity() );
  buffer_ += data.substr( 0, push_size );
  total_count_ += push_size;
  size_ += push_size;
}

void Writer::close()
{
  close_ = true;
}

bool Writer::is_closed() const
{
  return close_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - size_;
}

uint64_t Writer::bytes_pushed() const
{
  return total_count_;
}

string_view Reader::peek() const
{
  return std::string_view( buffer_ );
}

void Reader::pop( uint64_t len )
{
  std::size_t pop_size = std::min( len, size_ );
  buffer_.erase( 0, pop_size );
  size_ -= pop_size;
}

bool Reader::is_finished() const
{
  return close_ && size_ == 0;
}

uint64_t Reader::bytes_buffered() const
{
  return size_;
}

uint64_t Reader::bytes_popped() const
{
  return total_count_ - size_;
}

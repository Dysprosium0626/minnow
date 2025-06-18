#include "tcp_receiver.hh"
#include "debug.hh"
#include <cstdint>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }
  if ( !isn_set_ && message.SYN ) {
    isn_ = message.seqno;
    isn_set_ = true;
  }
  if ( !isn_set_ ) {
    return;
  }

  uint64_t insert_index
    = message.seqno.unwrap( isn_, reassembler_.writer().bytes_pushed() ) - ( message.SYN ? 0 : 1 );
  reassembler_.insert( insert_index, message.payload, message.FIN );
  next_seqno_ = isn_ + 1 + reassembler_.writer().bytes_pushed();
  if ( reassembler_.writer().is_closed() ) {
    next_seqno_ = next_seqno_ + 1;
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  uint16_t window_size = static_cast<uint16_t>(min( reassembler_.writer().available_capacity(), static_cast<uint64_t>(UINT16_MAX) ));
  auto rst = reassembler_.writer().has_error();
  if ( isn_set_ ) {
    return { next_seqno_, window_size, rst };
  } else {
    return { {}, window_size, rst };
  }
}

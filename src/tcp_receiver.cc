#include "tcp_receiver.hh"
#include "debug.hh"
#include <cstdint>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    rst_set_ = true;
    return;
  }
  if ( !isn_set_ && message.SYN ) {
    isn_ = message.seqno;
    isn_set_ = true;
    next_seqno_ = message.seqno + message.sequence_length();
  }
  if ( !isn_set_ ) {
    return;
  }
  auto first_unassembled = reassembler_.writer().bytes_pushed();
  if ( message.FIN ) {
    reassembler_.insert( message.seqno.unwrap( isn_, first_unassembled ), message.payload, message.FIN );
    return;
  }
  
  reassembler_.insert( message.seqno.unwrap( isn_, first_unassembled ) - 1, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg {};
  msg.window_size = reassembler_.writer().available_capacity() < UINT16_MAX
                      ? reassembler_.writer().available_capacity()
                      : UINT16_MAX;
  if ( rst_set_ ) {
    msg.RST = true;
    return msg;
  }
  if ( isn_set_ ) {
    msg.ackno = next_seqno_;
  }
  return msg;
}

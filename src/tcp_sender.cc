#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return next_seqno_.unwrap( isn_, reader().bytes_buffered() )
         - last_ackno_.unwrap( isn_, reader().bytes_buffered() );
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  auto current_window_size_ = window_size_ == 0 ? 1 : window_size_;

  // TCPSenderMessage seqno, SYN, payload, FIN, RST
  while ( current_window_size_ > sequence_numbers_in_flight() ) {
    // send a data segment
    TCPSenderMessage message;
    Wrap32 seqno = next_seqno_;
    message.seqno = seqno;
    if ( !is_syn_sent_ ) {
      message.SYN = true;
      is_syn_sent_ = true;
      next_seqno_ = next_seqno_ + 1;
    }

    std::string_view segment = reader().peek();
    uint64_t length = min( current_window_size_ - sequence_numbers_in_flight(),
                           min( segment.length(), TCPConfig::MAX_PAYLOAD_SIZE ) );
    std::string data = std::string( segment.substr( 0, length ) );
    next_seqno_ = next_seqno_ + length;
    message.payload = data;
    reader().pop( length );
    if ( !is_fin_sent_ && reader().is_finished() && current_window_size_ > sequence_numbers_in_flight() ) {
      is_fin_sent_ = message.FIN = true;
      next_seqno_ = next_seqno_ + 1;
    }

    if ( message.sequence_length() == 0 ) {
      break;
    }

    // do not store empty segments
    if ( message.sequence_length() > 0 ) {
      unacknowledged_segments_[seqno.unwrap( isn_, reader().bytes_buffered() )] = message;
      retransmission_timeout_ms_[seqno.unwrap( isn_, reader().bytes_buffered() )] = current_ms_ + initial_RTO_ms_;
    }
    message.RST = reader().has_error();
    transmit( message );

    // if the stream is finished, send a FIN segment
    if ( is_fin_sent_ || reader().is_finished() ) {
      break;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage( next_seqno_, false, "", false, reader().has_error() );
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    reader().set_error();
    return;
  }
  window_size_ = msg.window_size;
  bool is_ack_valid = false;
  if ( msg.ackno == isn_ + 1 && is_syn_sent_ && last_ackno_ == isn_ ) {
    is_syn_acked_ = true;
    last_ackno_ = msg.ackno.value();
  }

  for ( auto it = unacknowledged_segments_.begin(); it != unacknowledged_segments_.end(); ) {
    if ( it->first + it->second.sequence_length() == msg.ackno.value().unwrap( isn_, reader().bytes_buffered() ) ) {
      retransmission_timeout_ms_.erase( it->first );
      // reset the RTO and consecutive retransmissions
      current_RTO_ms_ = initial_RTO_ms_;
      consecutive_retransmissions_ = 0;
      is_ack_valid = true;
      it = unacknowledged_segments_.erase( it );
    } else {
      ++it;
    }
  }
  if ( is_ack_valid ) {
    last_ackno_ = msg.ackno.value();
    for ( auto& [seqno, timeout] : retransmission_timeout_ms_ ) {
      timeout = current_ms_ + current_RTO_ms_;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // update the current time
  current_ms_ += ms_since_last_tick;
  if ( !retransmission_timeout_ms_.empty() ) {
    auto earliest_seqno = retransmission_timeout_ms_.begin()->first;
    auto earliest_timeout = retransmission_timeout_ms_.begin()->second;
    // if the earliest timeout has been reached, retransmit the segment
    if ( current_ms_ >= earliest_timeout ) {
      // retransmit the segment
      transmit( unacknowledged_segments_[earliest_seqno] );
      if ( window_size_ > 0 ) {
        consecutive_retransmissions_++;
        current_RTO_ms_ *= 2;
      }
      retransmission_timeout_ms_[earliest_seqno] = current_ms_ + current_RTO_ms_;
    }
  }
}

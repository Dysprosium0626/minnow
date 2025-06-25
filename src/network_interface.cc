#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
  , queued_frames_()
  , arp_table_()
  , arp_table_broadcast_timestamps_()
  , arp_requests_()
  , arp_request_timestamps_()
  , current_time_ms_( 0 )
  , arp_request_timeout_ms_( 5000 )
  , arp_table_broadcast_timeout_ms_( 30000 )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // If the destination IP address is in the ARP table, send the datagram to the corresponding Ethernet address
  if ( arp_table_.find( next_hop.ipv4_numeric() ) != arp_table_.end() ) {
    EthernetFrame frame;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.header.dst = arp_table_[next_hop.ipv4_numeric()];
    frame.header.src = ethernet_address_;
    Serializer s;
    dgram.serialize( s );
    frame.payload = s.finish();
    port_->transmit( *this, frame );
  } else {
    // If the destination IP address is not in the ARP table, send an ARP request

    // Check if the ARP request is already in the queue
    bool need_arp_request = true;
    if ( arp_requests_.find( next_hop.ipv4_numeric() ) != arp_requests_.end() ) {
      // If the ARP request is in the queue, update the timestamp
      auto arp_time = arp_request_timestamps_[next_hop.ipv4_numeric()];
      if ( current_time_ms_ - arp_time >= arp_request_timeout_ms_ ) {
        // If the ARP request has timed out, send a new one
        arp_requests_.erase( next_hop.ipv4_numeric() );
        arp_request_timestamps_.erase( next_hop.ipv4_numeric() );
        need_arp_request = true;
      } else {
        // If the ARP request has not timed out, queue the datagram
        need_arp_request = false;
      }
    }

    // If the ARP request is not in the queue, create a new one
    if ( need_arp_request ) {
      ARPMessage arp;
      arp.opcode = ARPMessage::OPCODE_REQUEST;
      arp.sender_ethernet_address = ethernet_address_;
      arp.sender_ip_address = ip_address_.ipv4_numeric();
      arp.target_ip_address = next_hop.ipv4_numeric();

      // Send the ARP request
      EthernetFrame frame;
      frame.header.type = EthernetHeader::TYPE_ARP;
      frame.header.src = ethernet_address_;
      frame.header.dst = ETHERNET_BROADCAST;
      Serializer s;
      arp.serialize( s );
      frame.payload = s.finish();
      port_->transmit( *this, frame );

      // Add the ARP request to the queue
      arp_requests_[next_hop.ipv4_numeric()] = arp;
      arp_request_timestamps_[next_hop.ipv4_numeric()] = current_time_ms_;
    }

    // Add the IPv4 datagram to the queue
    EthernetFrame ipv4_frame;
    ipv4_frame.header.type = EthernetHeader::TYPE_IPv4;
    ipv4_frame.header.src = ethernet_address_;
    Serializer ipv4_s;
    dgram.serialize( ipv4_s );
    ipv4_frame.payload = ipv4_s.finish();
    queued_frames_[next_hop.ipv4_numeric()].emplace_back( ipv4_frame );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  // ignore frames that are not for us
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    Parser p( frame.payload );
    dgram.parse( p );
    if ( !p.has_error() ) {
      datagrams_received_.push( dgram );
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    Parser p( frame.payload );
    arp.parse( p );
    if ( arp.opcode == ARPMessage::OPCODE_REPLY ) {
      arp_table_[arp.sender_ip_address] = arp.sender_ethernet_address;
      arp_table_broadcast_timestamps_[arp.sender_ip_address] = current_time_ms_;
      if ( queued_frames_.find( arp.sender_ip_address ) != queued_frames_.end() ) {
        for ( EthernetFrame& f : queued_frames_[arp.sender_ip_address] ) {
          f.header.dst = arp.sender_ethernet_address;
          port_->transmit( *this, f );
        }
        queued_frames_.erase( arp.sender_ip_address );
      }
    } else if ( arp.opcode == ARPMessage::OPCODE_REQUEST ) {
      if ( arp.target_ip_address != ip_address_.ipv4_numeric() ) {
        return;
      }

      arp_table_[arp.sender_ip_address] = arp.sender_ethernet_address;
      arp_table_broadcast_timestamps_[arp.sender_ip_address] = current_time_ms_;

      ARPMessage reply;
      reply.opcode = ARPMessage::OPCODE_REPLY;
      reply.sender_ethernet_address = ethernet_address_;
      reply.sender_ip_address = ip_address_.ipv4_numeric();
      reply.target_ip_address = arp.sender_ip_address;
      reply.target_ethernet_address = arp.sender_ethernet_address;
      EthernetFrame reply_frame;
      reply_frame.header.type = EthernetHeader::TYPE_ARP;
      reply_frame.header.src = ethernet_address_;
      reply_frame.header.dst = arp.sender_ethernet_address;
      Serializer s;
      reply.serialize( s );
      reply_frame.payload = s.finish();
      port_->transmit( *this, reply_frame );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time_ms_ += ms_since_last_tick;

  // Check if any ARP requests have timed out
  for ( auto it = arp_requests_.begin(); it != arp_requests_.end(); ) {
    if ( current_time_ms_ - arp_request_timestamps_[it->first] >= arp_request_timeout_ms_ ) {
      arp_request_timestamps_.erase( it->first );
      queued_frames_.erase( it->first );
      it = arp_requests_.erase( it );
    } else {
      ++it;
    }
  }
  // Check if any ARP table broadcasts have timed out
  for ( auto it = arp_table_.begin(); it != arp_table_.end(); ) {
    if ( current_time_ms_ - arp_table_broadcast_timestamps_[it->first] >= arp_table_broadcast_timeout_ms_ ) {
      arp_table_broadcast_timestamps_.erase( it->first );
      it = arp_table_.erase( it );
    } else {
      ++it;
    }
  }
}

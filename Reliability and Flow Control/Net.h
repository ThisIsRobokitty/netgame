/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_H
#define NET_H

// platform detection

#define PLATFORM_WINDOWS  1
#define PLATFORM_MAC      2
#define PLATFORM_UNIX     3

#if defined(_WIN32)
#define PLATFORM PLATFORM_WINDOWS
#elif defined(__APPLE__)
#define PLATFORM PLATFORM_MAC
#else
#define PLATFORM PLATFORM_UNIX
#endif

#if PLATFORM == PLATFORM_WINDOWS

	#include <winsock2.h>
	#pragma comment( lib, "wsock32.lib" )

#elif PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX

	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <fcntl.h>

#else

	#error unknown platform!

#endif

#include <assert.h>
#include <vector>
#include <map>
#include <stack>
#include <list>
#include <algorithm>
#include <functional>

namespace net
{
	// platform independent wait for n seconds

#if PLATFORM == PLATFORM_WINDOWS

	void wait( float seconds )
	{
		Sleep( (int) ( seconds * 1000.0f ) );
	}

#else

	#include <unistd.h>
	void wait( float seconds ) { usleep( (int) ( seconds * 1000000.0f ) ); }

#endif

	// internet address

	class Address
	{
	public:
	
		Address()
		{
			address = 0;
			port = 0;
		}
	
		Address( unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned short port )
		{
			this->address = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
			this->port = port;
		}
	
		Address( unsigned int address, unsigned short port )
		{
			this->address = address;
			this->port = port;
		}
	
		unsigned int GetAddress() const
		{
			return address;
		}
	
		unsigned char GetA() const
		{
			return ( unsigned char ) ( address >> 24 );
		}
	
		unsigned char GetB() const
		{
			return ( unsigned char ) ( address >> 16 );
		}
	
		unsigned char GetC() const
		{
			return ( unsigned char ) ( address >> 8 );
		}
	
		unsigned char GetD() const
		{
			return ( unsigned char ) ( address );
		}
	
		unsigned short GetPort() const
		{ 
			return port;
		}
	
		bool operator == ( const Address & other ) const
		{
			return address == other.address && port == other.port;
		}
	
		bool operator != ( const Address & other ) const
		{
			return ! ( *this == other );
		}
		
		bool operator < ( const Address & other ) const
		{
			// note: this is so we can use address as a key in std::map
			if ( address < other.address )
				return true;
			if ( address > other.address )
				return false;
			else
				return port < other.port;
		}
	
	private:
	
		unsigned int address;
		unsigned short port;
	};

	// sockets

	inline bool InitializeSockets()
	{
		#if PLATFORM == PLATFORM_WINDOWS
	    WSADATA WsaData;
		return WSAStartup( MAKEWORD(2,2), &WsaData ) != NO_ERROR;
		#else
		return true;
		#endif
	}

	inline void ShutdownSockets()
	{
		#if PLATFORM == PLATFORM_WINDOWS
		WSACleanup();
		#endif
	}

	class Socket
	{
	public:
	
		Socket()
		{
			socket = 0;
		}
	
		~Socket()
		{
			Close();
		}
	
		bool Open( unsigned short port )
		{
			assert( !IsOpen() );
		
			// create socket

			socket = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

			if ( socket <= 0 )
			{
				printf( "failed to create socket\n" );
				socket = 0;
				return false;
			}

			// bind to port

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = INADDR_ANY;
			address.sin_port = htons( (unsigned short) port );
		
			if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
			{
				printf( "failed to bind socket\n" );
				Close();
				return false;
			}

			// set non-blocking io

			#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
		
				int nonBlocking = 1;
				if ( fcntl( socket, F_SETFL, O_NONBLOCK, nonBlocking ) == -1 )
				{
					printf( "failed to set non-blocking socket\n" );
					Close();
					return false;
				}
			
			#elif PLATFORM == PLATFORM_WINDOWS
		
				DWORD nonBlocking = 1;
				if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
				{
					printf( "failed to set non-blocking socket\n" );
					Close();
					return false;
				}

			#endif
		
			return true;
		}
	
		void Close()
		{
			if ( socket != 0 )
			{
				#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
				close( socket );
				#elif PLATFORM == PLATFORM_WINDOWS
				closesocket( socket );
				#endif
				socket = 0;
			}
		}
	
		bool IsOpen() const
		{
			return socket != 0;
		}
	
		bool Send( const Address & destination, const void * data, int size )
		{
			assert( data );
			assert( size > 0 );
		
			if ( socket == 0 )
				return false;
		
			assert( destination.GetAddress() != 0 );
			assert( destination.GetPort() != 0 );
		
			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl( destination.GetAddress() );
			address.sin_port = htons( (unsigned short) destination.GetPort() );

			int sent_bytes = sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

			return sent_bytes == size;
		}
	
		int Receive( Address & sender, void * data, int size )
		{
			assert( data );
			assert( size > 0 );
		
			if ( socket == 0 )
				return false;
			
			#if PLATFORM == PLATFORM_WINDOWS
			typedef int socklen_t;
			#endif
			
			sockaddr_in from;
			socklen_t fromLength = sizeof( from );

			int received_bytes = recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

			if ( received_bytes <= 0 )
				return 0;

			unsigned int address = ntohl( from.sin_addr.s_addr );
			unsigned short port = ntohs( from.sin_port );

			sender = Address( address, port );

			return received_bytes;
		}
		
	private:
	
		int socket;
	};
	
	// connection
	
	class Connection
	{
	public:
		
		enum Mode
		{
			None,	
			Client,
			Server
		};
		
		Connection( unsigned int protocolId, float timeout )
		{
			this->protocolId = protocolId;
			this->timeout = timeout;
			mode = None;
			running = false;
			ClearData();
		}
		
		virtual ~Connection()
		{
			if ( IsRunning() )
				Stop();
		}
		
		bool Start( int port )
		{
			assert( !running );
			printf( "start connection on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			OnStart();
			return true;
		}
		
		void Stop()
		{
			assert( running );
			printf( "stop connection\n" );
			bool connected = IsConnected();
			ClearData();
			socket.Close();
			running = false;
			if ( connected )
				OnDisconnect();
			OnStop();
		}
		
		bool IsRunning() const
		{
			return running;
		}
		
		void Listen()
		{
			printf( "server listening for connection\n" );
			bool connected = IsConnected();
			ClearData();
			if ( connected )
				OnDisconnect();
			mode = Server;
			state = Listening;
		}
		
		void Connect( const Address & address )
		{
			printf( "client connecting to %d.%d.%d.%d:%d\n", 
				address.GetA(), address.GetB(), address.GetC(), address.GetD(), address.GetPort() );
			bool connected = IsConnected();
			ClearData();
			if ( connected )
				OnDisconnect();
			mode = Client;
			state = Connecting;
			this->address = address;
		}
		
		bool IsConnecting() const
		{
			return state == Connecting;
		}
		
		bool ConnectFailed() const
		{
			return state == ConnectFail;
		}
		
		bool IsConnected() const
		{
			return state == Connected;
		}
		
		bool IsListening() const
		{
			return state == Listening;
		}
		
		Mode GetMode() const
		{
			return mode;
		}
		
		virtual void Update( float deltaTime )
		{
			assert( running );
			timeoutAccumulator += deltaTime;
			if ( timeoutAccumulator > timeout )
			{
				if ( state == Connecting )
				{
					printf( "connect timed out\n" );
					ClearData();
					state = ConnectFail;
					OnDisconnect();
				}
				else if ( state == Connected )
				{
					printf( "connection timed out\n" );
					ClearData();
					if ( state == Connecting )
						state = ConnectFail;
					OnDisconnect();
				}
			}
		}
		
		virtual bool SendPacket( const unsigned char data[], int size )
		{
			assert( running );
			if ( address.GetAddress() == 0 )
				return false;
			unsigned char packet[size+4];
			packet[0] = (unsigned char) ( protocolId >> 24 );
			packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
			packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
			packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
			memcpy( &packet[4], data, size );
			return socket.Send( address, packet, size + 4 );
		}
		
		virtual int ReceivePacket( unsigned char data[], int size )
		{
			assert( running );
			unsigned char packet[size+4];
			Address sender;
			int bytes_read = socket.Receive( sender, packet, size + 4 );
			if ( bytes_read == 0 )
				return 0;
			if ( bytes_read <= 4 )
				return 0;
			if ( packet[0] != (unsigned char) ( protocolId >> 24 ) || 
				 packet[1] != (unsigned char) ( ( protocolId >> 16 ) & 0xFF ) ||
				 packet[2] != (unsigned char) ( ( protocolId >> 8 ) & 0xFF ) ||
				 packet[3] != (unsigned char) ( protocolId & 0xFF ) )
				return 0;
			if ( mode == Server && !IsConnected() )
			{
				printf( "server accepts connection from client %d.%d.%d.%d:%d\n", 
					sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort() );
				state = Connected;
				address = sender;
				OnConnect();
			}
			if ( sender == address )
			{
				if ( mode == Client && state == Connecting )
				{
					printf( "client completes connection with server\n" );
					state = Connected;
					OnConnect();
				}
				timeoutAccumulator = 0.0f;
				memcpy( data, &packet[4], bytes_read - 4 );
				return bytes_read - 4;
			}
			return 0;
		}
		
		int GetHeaderSize() const
		{
			return 4;
		}
		
	protected:
		
		virtual void OnStart()		{}
		virtual void OnStop()		{}
		virtual void OnConnect()    {}
		virtual void OnDisconnect() {}
			
	private:
		
		void ClearData()
		{
			state = Disconnected;
			timeoutAccumulator = 0.0f;
			address = Address();
		}
	
		enum State
		{
			Disconnected,
			Listening,
			Connecting,
			ConnectFail,
			Connected
		};

		unsigned int protocolId;
		float timeout;
		
		bool running;
		Mode mode;
		State state;
		Socket socket;
		float timeoutAccumulator;
		Address address;
	};

	// connection with reliability (seq/ack)

	class ReliableConnection : public Connection
	{
	public:
		
		ReliableConnection( unsigned int protocolId, float timeout )
			: Connection( protocolId, timeout )
		{
			ClearData();
			#ifdef NET_UNIT_TEST
			packet_loss_mask = 0;
			#endif
		}
	
		~ReliableConnection()
		{
			if ( IsRunning() )
				Stop();
		}
		
		// overriden functions from "Connection"
				
		bool SendPacket( const unsigned char data[], int size )
		{
			#ifdef NET_UNIT_TEST
			if ( local_sequence & packet_loss_mask )
			{
				AddSentPacketToQueues( local_sequence, size );
				sent_packets++;
				local_sequence++;
				return true;
			}
			#endif
			const int header = 12;
			unsigned char packet[header+size];
			unsigned int ack = remote_sequence;
			unsigned int ack_bits = 0;
			GenerateAckBits( ack, ack_bits );
			WriteHeader( packet, local_sequence, ack, ack_bits );
			memcpy( packet + header, data, size );
 			if ( !Connection::SendPacket( packet, size + header ) )
				return false;
			AddSentPacketToQueues( local_sequence, size );
			sent_packets++;
			local_sequence++;
			return true;
		}	
		
		int ReceivePacket( unsigned char data[], int size )
		{
			const int header = 12;
			if ( size <= header )
				return false;
			unsigned char packet[header+size];
			int received_bytes = Connection::ReceivePacket( packet, size + header );
			if ( received_bytes == 0 )
				return false;
			if ( received_bytes <= header )
				return false;
			unsigned int packet_sequence = 0;
			unsigned int packet_ack = 0;
			unsigned int packet_ack_bits = 0;
			ReadHeader( packet, packet_sequence, packet_ack, packet_ack_bits );
			ProcessAcks( packet_ack, packet_ack_bits );
			AddReceivedPacketToQueue( packet_sequence, received_bytes - header );
			if ( packet_sequence > remote_sequence )
				remote_sequence = packet_sequence;
			recv_packets++;
			memcpy( data, packet + header, received_bytes - header );
			return received_bytes - header;
		}
		
		void Update( float deltaTime )
		{
			acks.clear();
			Connection::Update( deltaTime );
			UpdateQueues( deltaTime );
			UpdateStats();
		}
		
		// unit test controls
		
		#ifdef NET_UNIT_TEST
		void SetPacketLossMask( unsigned int mask )
		{
			packet_loss_mask = mask;
		}
		#endif

		// data accessors
				
		unsigned int GetLocalSequence() const
		{
			return local_sequence;
		}

		unsigned int GetRemoteSequence() const
		{
			return remote_sequence;
		}
				
 		void GetAcks( unsigned int ** acks, int & count )
		{
			*acks = &this->acks[0];
			count = (int) this->acks.size();
		}
		
		unsigned int GetSentPackets() const
		{
			return sent_packets;
		}
		
		unsigned int GetReceivedPackets() const
		{
			return recv_packets;
		}

		unsigned int GetLostPackets() const
		{
			return lost_packets;
		}

		unsigned int GetAckedPackets() const
		{
			return acked_packets;
		}

		float GetSentBandwidth() const
		{
			return sent_bandwidth;
		}

		float GetAckedBandwidth() const
		{
			return acked_bandwidth;
		}

		float GetRoundTripTime() const
		{
			return rtt;
		}
		
		int GetHeaderSize() const
		{
			return 12 + Connection::GetHeaderSize();
		}
		
	protected:		
		
		void WriteInteger( unsigned char * data, unsigned int value )
		{
			data[0] = (unsigned char) ( value >> 24 );
			data[1] = (unsigned char) ( ( value >> 16 ) & 0xFF );
			data[2] = (unsigned char) ( ( value >> 8 ) & 0xFF );
			data[3] = (unsigned char) ( value & 0xFF );
		}

		void WriteHeader( unsigned char * header, unsigned int sequence, unsigned int ack, unsigned int ack_bits )
		{
			WriteInteger( header, sequence );
			WriteInteger( header + 4, ack );
			WriteInteger( header + 8, ack_bits );
		}
		
		void ReadInteger( const unsigned char * data, unsigned int & value )
		{
 			value = ( ( (unsigned int)data[0] << 24 ) | ( (unsigned int)data[1] << 16 ) | 
				      ( (unsigned int)data[2] << 8 )  | ( (unsigned int)data[3] ) );				
		}
		
		void ReadHeader( const unsigned char * header, unsigned int & sequence, unsigned int & ack, unsigned int & ack_bits )
		{
			ReadInteger( header, sequence );
			ReadInteger( header + 4, ack );
			ReadInteger( header + 8, ack_bits );
		}
		
		void GenerateAckBits( unsigned int ack, unsigned int & ack_bits )
		{
			assert( ack_bits == 0 );
			const int steps = ack >= 32 ? 32 : ( ack > 0 ? ack : 0 ); 
			for ( int i = 0; i < steps; ++i )
			{
				unsigned int sequence = ack - 1 - i;
				// note: this bit could be made faster if we always enforce receive queue to be sorted in sequence order!
				for ( PacketQueue::iterator itor = receivedQueue.begin(); itor != receivedQueue.end(); ++itor )
				{
					if ( itor->sequence == sequence )
					{
						ack_bits |= 1 << i;
						break;
					}
				}
			}
		}
		
		void ProcessAcks( unsigned int ack, unsigned int ack_bits )
		{			
			if ( pendingAckQueue.empty() )
				return;
				
			const unsigned int minimum_ack = ack > 32 ? ack - 32 : 0;
			
			PacketQueue::iterator itor = pendingAckQueue.begin();
			while ( itor != pendingAckQueue.end() )
			{
				if ( itor->sequence < minimum_ack )
				{
					itor++;
					continue;
				}
				
				bool acked = false;
				
				if ( itor->sequence == ack )
				{
					acked = true;
				}
				else if ( itor->sequence < ack )
				{
					assert( ack >= 1 );
					assert( itor->sequence <= ack - 1 );
					int bit_index = (int) ( ack - 1 - itor->sequence );
					assert( bit_index >= 0 && bit_index <= 31 );
					acked = ( ack_bits >> bit_index ) & 1;
				}
				
				if ( acked )
				{
					rtt += ( itor->time - rtt ) * 0.01f;

					PacketData data;
					data.sequence = itor->sequence;
					data.time = itor->time;
					data.size = itor->size;
					ackedQueue.sorted_insert( data );

					acked_packets++;
					
					acks.push_back( itor->sequence );

					itor = pendingAckQueue.erase( itor );
				}
				else
					++itor;
			}
		}
		
		void AddSentPacketToQueues( unsigned int sequence, int size )
		{
			PacketData p;
			p.sequence = sequence;
			p.time = 0.0f;
			p.size = size;
			sentQueue.push_back( p );				// note: sent packet and pending ack queue are always in sequence order, by definition
			pendingAckQueue.push_back( p );
		}
		
		void AddReceivedPacketToQueue( unsigned int sequence, int size )
		{
			PacketData p;
			p.sequence = sequence;
			p.time = 0.0f;
			p.size = size;
			receivedQueue.sorted_insert( p );
		}
		
		void UpdateQueues( float deltaTime )
		{
			// advance time for queue entries (time values are small and bounded above)
			
			for ( PacketQueue::iterator itor = sentQueue.begin(); itor != sentQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = receivedQueue.begin(); itor != receivedQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); itor++ )
				itor->time += deltaTime;
			
			// update queues
			
			const float epsilon = 0.001f;
			
			while ( sentQueue.size() && sentQueue.front().time > rtt_maximum + epsilon )
				sentQueue.pop_front();
				
			const unsigned int latest_recv_sequence = receivedQueue.back().sequence;
			const unsigned int minimum_sequence = ( latest_recv_sequence > 33 ) ? latest_recv_sequence - 33 : 0;
			while ( receivedQueue.size() && receivedQueue.front().sequence < minimum_sequence )
				receivedQueue.pop_front();

			while ( ackedQueue.size() && ackedQueue.front().time > rtt_maximum * 2 - epsilon )
				ackedQueue.pop_front();

			while ( pendingAckQueue.size() && pendingAckQueue.front().time > rtt_maximum + epsilon )
			{
				pendingAckQueue.pop_front();
				lost_packets++;
			}
		}
		
		void UpdateStats()
		{
			assert( rtt_maximum == 1.0f );
			int sent_bytes_per_second = 0;
			for ( PacketQueue::iterator itor = sentQueue.begin(); itor != sentQueue.end(); ++itor )
				sent_bytes_per_second += itor->size;
			int acked_packets_per_second = 0;
			int acked_bytes_per_second = 0;
			for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor )
			{
				if ( itor->time >= rtt_maximum )
				{
					acked_packets_per_second++;
					acked_bytes_per_second += itor->size;
				}
			}
			sent_bandwidth = sent_bytes_per_second * ( 8 / 1000.0f );
			acked_bandwidth = acked_bytes_per_second * ( 8 / 1000.0f );
		}
		
		virtual void OnStop()
		{
			ClearData();
		}
		
		virtual void OnDisconnect()
		{
			ClearData();
		}
		
	private:

		void ClearData()
		{
			sentQueue.clear();
			receivedQueue.clear();
			ackedQueue.clear();
			pendingAckQueue.clear();
			acks.clear();
 			local_sequence = 0;
			remote_sequence = 0;
			sent_packets = 0;
			recv_packets = 0;
			lost_packets = 0;
			acked_packets = 0;
			sent_bandwidth = 0.0f;
			acked_bandwidth = 0.0f;
			rtt = 0.0f;
			rtt_maximum = 1.0f;
		}

		std::vector<unsigned int> acks;		// acked packets from last set of packet receives. cleared each update!

		unsigned int local_sequence;		// current sequence number for sent packets
		unsigned int remote_sequence;		// most recently received sequence number from other computer
		
		unsigned int sent_packets;			// total number of packets sent
		unsigned int recv_packets;			// total number of packets received
		unsigned int lost_packets;			// total number of packets lost
		unsigned int acked_packets;			// total number of packets acked

		float sent_bandwidth;				// approximate sent bandwidth over the last second
		float acked_bandwidth;				// approximate acked bandwidth over the last second
		float rtt;							// estimated round trip time
		float rtt_maximum;					// maximum expected round trip time
		
		#ifdef NET_UNIT_TEST
		unsigned int packet_loss_mask;		// mask sequence number, if non-zero, drop packet - for unit test only
		#endif

		// packet queue definition

		struct PacketData
		{
			unsigned int sequence;			// packet sequence number
			float time;					    // time offset since packet was sent or received (depending on context)
			int size;						// packet size in bytes
			bool operator < ( const PacketData & other ) const { return sequence < other.sequence; }
		};

		class PacketQueue : public std::list<PacketData>
		{
		public:
			void sorted_insert( const PacketData & p )
			{
				if ( empty() )
				{
					push_back( p );
				}
				else
				{
					if ( p.sequence < this->front().sequence )
					{
						push_front( p );
					}
					else if ( p.sequence > this->back().sequence )
					{
						push_back( p );
					}
					else
					{
						for ( PacketQueue::iterator itor = begin(); itor != end(); itor++ )
						{
							assert( itor->sequence != p.sequence );
							if ( itor->sequence > p.sequence )
							{
								insert( itor, p );
								break;
							}
						}
					}
				}
			}
		};

		PacketQueue sentQueue;				// sent packets (kept until rtt_maximum)
		PacketQueue receivedQueue;			// received packets for determining acks (32+1 entries kept only)
		PacketQueue ackedQueue;				// acked packets (kept until rtt_maximum * 2)
		PacketQueue pendingAckQueue;		// sent packets which have not been acked yet
	};
}

#endif
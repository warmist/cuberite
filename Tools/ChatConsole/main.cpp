
#include <memory>
#include <vector>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <stdarg.h>

#define SharedPtr std::shared_ptr
void PrintStackTrace() {}

typedef std::string AString;
typedef std::vector<std::string> AStringVector;
typedef signed long long Int64;
typedef signed int       Int32;
typedef signed short     Int16;
typedef signed char      Int8;

typedef unsigned long long UInt64;
typedef unsigned int       UInt32;
typedef unsigned short     UInt16;
typedef unsigned char      UInt8;

void LOGERROR(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

void LOGWARN(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

void LOG(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

#include "Globals.h"
#include "../../src/OSSupport/Network.h"
#include "../../src/ByteBuffer.h"


class ConnectionCallbacks : public cNetwork::cConnectCallbacks
{
	virtual void OnConnected(cTCPLink & a_Link)
	{
		std::cout << "connected" << std::endl;
	}

	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg)
	{
		std::cout << "error" << std::endl;
	}

};

class LinkCallbacks : public cTCPLink::cCallbacks
{

	enum States
	{
		LOGIN,
		GAME
	} m_State;

	std::condition_variable & m_cv;
	cTCPLinkPtr & m_Link;

public:
	LinkCallbacks(std::condition_variable & a_cv, cTCPLinkPtr & a_Link) : m_State(LOGIN), recivedData(102400), m_cv(a_cv), m_Link(a_Link)  {}

	virtual void OnLinkCreated(cTCPLinkPtr a_Link)
	{
		m_Link = a_Link;
		cByteBuffer p(400);
		// Handshake
		p.WriteVarInt32(15);
		p.WriteVarInt32(0x0);
		p.WriteVarInt32(5);
		p.WriteVarUTF8String("Localhost");
		p.WriteBEUInt16(25565);
		p.WriteVarInt32(2);
		// Login Start

		std::string idstring = "ConsoleChat";

		cByteBuffer payload(100);
		payload.WriteVarInt32(0x0);
		payload.WriteVarUTF8String(idstring);
		p.WriteVarInt32(payload.GetUsedSpace());
		payload.ReadToByteBuffer(p, payload.GetUsedSpace());
		std::string data;
		p.ReadAll(data);
		p.CommitRead();
		a_Link->Send(data);
	}
	virtual void OnReceivedData(const char * a_Data, size_t a_Length)
	{
		//std::cout << "recived " << a_Length << " bytes" << std::endl;
		if (!recivedData.WriteBuf(a_Data, a_Length))
		{
			std::cout << "Write failed" << std::endl;
			abort();
		}
		switch (m_State)
		{
			case LOGIN:
				HandleLoginPackets();
				break;
			case GAME:
				HandleGamePackets();
				break;
		}
	}

#define IGNORE_PAYLOAD recivedData.SkipRead(length - typeLength);
#define CHECK_CALL(x) if (!(x)) {abort();}
	void HandleLoginPackets()
	{
		for(;;) 
		{
			UInt32 length, type;
			if (!recivedData.ReadVarInt32(length))
			{
				recivedData.ResetRead();
				break;
			}
			if (!recivedData.CanReadBytes(length))
			{
				recivedData.ResetRead();
				break;
			}
			int typeLength = recivedData.GetReadableSpace();
			CHECK_CALL(recivedData.ReadVarInt32(type))
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x2:
				{
					std::cout << ": LoginSuccess" << std::endl;
					IGNORE_PAYLOAD
					recivedData.CommitRead();
					m_State = GAME;
					m_cv.notify_one();
					HandleGamePackets();
					return;
					break;
				}
				default:
				{
					std::cout << ": Unknown: Ignoring: " << type << std::endl;
					IGNORE_PAYLOAD
					break;
				}
			}
			recivedData.CommitRead();
		}
	}


	void HandleGamePackets()
	{
		for(;;) 
		{
			UInt32 length, type;
			if (!recivedData.ReadVarInt32(length))
			{
				recivedData.ResetRead();
				return;
			}
			if (length == 0)
			{
				std::cout << "recived a zero length packet" << std::endl;
				recivedData.ResetRead();
				return;
			}
			if (!recivedData.CanReadBytes(length))
			{
				//std::cout << "Do not have enough bytes, need " << length << " have " << recivedData.GetReadableSpace() << std::endl;
				recivedData.ResetRead();
				return;
			}
			int typeLength = recivedData.GetReadableSpace();
			CHECK_CALL(recivedData.ReadVarInt32(type));
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x0:
				{
					//std::cout << "KeepAlive" << std::endl;
					int32_t id;
					CHECK_CALL(recivedData.ReadBEInt32(id));
					cByteBuffer packet(100);
					cByteBuffer payload(100);
					payload.WriteVarInt32(0x0);
					payload.WriteBEInt32(id);
					packet.WriteVarInt32(payload.GetUsedSpace());
					payload.ReadToByteBuffer(packet, payload.GetUsedSpace());
					std::string data;
					packet.ReadAll(data);
					packet.CommitRead();
					recivedData.CommitRead();
					m_Link->Send(data);
					break;	
				}
				case 0x2:
				{
					std::string message;
					CHECK_CALL(recivedData.ReadVarUTF8String(message));
					std::cout << "Chat: " << message << std::endl;
					break;
				}
				case 0x21:
				{
					//std::cout << "Chunk Data: ";
					int32_t ChunkX, ChunkZ;
					recivedData.ReadBEInt32(ChunkX);
					recivedData.ReadBEInt32(ChunkZ);
					bool groundUp;
					recivedData.ReadBool(groundUp);
					uint16_t bitmask1;
					recivedData.ReadBEUInt16(bitmask1);
					uint16_t bitmask2;
					recivedData.ReadBEUInt16(bitmask2);
					uint32_t Size;
					recivedData.ReadBEUInt32(Size);
					recivedData.SkipRead(Size);
					//std::cout << std::dec << ChunkX << ", " << ChunkZ << " groundUp: " << groundUp << std::endl;
					break;
				}
				case 0x46:
				{
					//std::cout << "compression enabled" << std::endl;
					IGNORE_PAYLOAD
					break;
				}
				default:
				{
					//std::cout << "Unknown: Ignoring: " << std::hex << type << std::endl;
					IGNORE_PAYLOAD
					break;
				}
			}
			recivedData.CommitRead();
		}
	}

	virtual void OnRemoteClosed(void)
	{
		std::cout << "closed" << std::endl;
	}
	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg)
	{
		std::cout << "error" << std::endl;
	}

private:
	cByteBuffer recivedData;

};


int main()
{

	std::cout << "Connecting" << std::endl;
	std::mutex mut;
	std::condition_variable cv;
	cTCPLinkPtr Link;
	cNetwork::Connect("localhost", 25565, std::make_shared<ConnectionCallbacks>(), std::make_shared<LinkCallbacks>(cv, Link));
	std::unique_lock<std::mutex> lock(mut);
	cv.wait(lock);
	for(;;)
	{
		std::string message;
		std::getline(std::cin, message);
		cByteBuffer packet(4000);
		// Handshake
		cByteBuffer payload(1000);
		payload.WriteVarInt32(0x01);
		payload.WriteVarUTF8String(message);
		packet.WriteVarInt32(payload.GetUsedSpace());
		payload.ReadToByteBuffer(packet, payload.GetUsedSpace());
		std::string data;
		packet.ReadAll(data);
		packet.CommitRead();
		Link->Send(data);
	}
}

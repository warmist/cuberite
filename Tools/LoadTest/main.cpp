
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
#include "../../src/FastRandom.h"


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

	int id;
	bool m_teleport, m_portal;
	cTCPLinkPtr m_Link;

public:
	LinkCallbacks(int i, bool a_teleport, bool a_portal) : m_State(LOGIN), id(i), m_teleport(a_teleport), m_portal(a_portal), recivedData(10240) {}
	~LinkCallbacks() { std::cout << "Link Deleted" << std::endl; }

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

		std::stringstream sstm;
		sstm << "LoadTest" << id;
		std::string idstring = sstm.str();

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

#define CHECK_CALL(x) if (!(x)) {m_Link->Close(); m_Link = nullptr; return; }
	virtual void OnReceivedData(const char * a_Data, size_t a_Length)
	{
		CHECK_CALL(recivedData.WriteBuf(a_Data, a_Length));
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




	void Teleport()
	{
		std::stringstream messageStream;
		cFastRandom rd;
		messageStream << "/tp " << rd.GenerateRandomInteger(-100000, 100000) << " " << rd.GenerateRandomInteger(0, 255) << " " << rd.GenerateRandomInteger(-100000, 100000);
		std::cout << "teleporting" << std::endl;
		Message(messageStream.str());
	}




	void Portal()
	{
		Message("/portal second");
	}


	
	void Message(std::string a_message)
	{
		
		cByteBuffer packet(4000);
		// Handshake
		cByteBuffer payload(1000);
		payload.WriteVarInt32(0x01);
		payload.WriteVarUTF8String(a_message);
		packet.WriteVarInt32(payload.GetUsedSpace());
		payload.ReadToByteBuffer(packet, payload.GetUsedSpace());
		std::string data;
		packet.ReadAll(data);
		packet.CommitRead();
		m_Link->Send(data);
	}


#define IGNORE_PAYLOAD recivedData.SkipRead(length - typeLength);
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
			CHECK_CALL(recivedData.ReadVarInt32(type));
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x2:
				{
					std::cout << id << ": LoginSuccess" << std::endl;
					IGNORE_PAYLOAD
					recivedData.CommitRead();
					m_State = GAME;
					if (m_portal) Portal();
					if (m_teleport) Teleport();
					HandleGamePackets();
					break;
				}
				default:
				{
					std::cout << id << ": Unknown Login: Ignoring: " << std::hex << type << std::endl;
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
				break;
			}
			if (length == 0)
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
			CHECK_CALL(recivedData.ReadVarInt32(type));
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x0:
				{
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
					std::cout << id << ": Chat" << std::endl;
					IGNORE_PAYLOAD
					break;
				}
				default:
				{
					std::cout << id << ": Unknown game: Ignoring: " << std::hex << type << std::endl;
					IGNORE_PAYLOAD
					break;
				}
			}
			recivedData.CommitRead();
		}
	}

	virtual void OnRemoteClosed(void)
	{
		m_Link = nullptr;
		std::cout << id << ": closed" << std::endl;
	}
	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg)
	{
		m_Link = nullptr;
		std::cout << id << ": error" << std::endl;
	}

private:
	cByteBuffer recivedData;

};


int main(int argc, char** argv)
{
	bool teleport = false;
	bool portal = false;
	if (argc > 1)
	{
		if (std::string{argv[1]} == "-t") teleport = true;
		if (std::string{argv[1]} == "-p") portal = true;
	}
	std::vector<std::shared_ptr<LinkCallbacks>> links;
	for(int i = 0;i < 200; i++)
	{
		std::cout << "Creating new Player" << std::endl;
		auto callback = std::make_shared<LinkCallbacks>(i, teleport, portal);
		links.push_back(callback);
		cNetwork::Connect("localhost", 25565, std::make_shared<ConnectionCallbacks>(), callback);
		usleep(100000);
	}
	for(;;);
}

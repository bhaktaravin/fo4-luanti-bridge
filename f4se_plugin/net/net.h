// Networking + config for FO4LuantiBridge.
// Kept in its own library with only standard C++ types in this header, so the
// Windows/Winsock headers it needs never meet CommonLibF4's headers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace net
{
	struct Config
	{
		std::string   host = "127.0.0.1";
		int           port = 8765;
		int           pollMs = 100;
		std::uint32_t blockFormID = 0;
		float         blockScale = 1.0f;
	};

	// Reads <game>\Data\F4SE\Plugins\FO4LuantiBridge.ini (missing keys keep defaults).
	Config LoadConfig();

	struct BlockEvent
	{
		bool  place;       // true = place, false = dig
		int   lx, ly, lz;  // Luanti block coords (used as the block's identity)
		float fx, fy, fz;  // matching Fallout world position
	};

	class Client
	{
	public:
		Client(const std::string& a_host, int a_port);
		~Client();

		// Sends the Fallout player's position. Returns false if the bridge is unreachable.
		bool SendState(float a_x, float a_y, float a_z, float a_angle);

		// Fetches block events newer than a_lastSeq and advances a_lastSeq.
		bool Poll(long long& a_lastSeq, std::vector<BlockEvent>& a_out);

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};
}

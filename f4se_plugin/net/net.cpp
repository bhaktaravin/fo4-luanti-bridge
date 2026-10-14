#include "net.h"

#include <cmath>
#include <string>

#include <httplib.h>  // pulls in winsock2.h / windows.h
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace net
{
	Config LoadConfig()
	{
		// The ".\" prefix matters: a bare relative path makes Windows look in C:\Windows.
		const char* ini = ".\\Data\\F4SE\\Plugins\\FO4LuantiBridge.ini";
		Config      c;
		char        buf[256]{};

		GetPrivateProfileStringA("Bridge", "Host", "127.0.0.1", buf, sizeof(buf), ini);
		c.host = buf;
		c.port = static_cast<int>(GetPrivateProfileIntA("Bridge", "Port", 8765, ini));
		c.pollMs = static_cast<int>(GetPrivateProfileIntA("Bridge", "PollMs", 100, ini));

		GetPrivateProfileStringA("Blocks", "BlockBaseFormID", "0", buf, sizeof(buf), ini);
		try {
			c.blockFormID = static_cast<std::uint32_t>(std::stoul(buf, nullptr, 16));
		} catch (...) {
			c.blockFormID = 0;
		}

		GetPrivateProfileStringA("Blocks", "BlockScale", "1.0", buf, sizeof(buf), ini);
		try {
			c.blockScale = std::stof(buf);
		} catch (...) {
			c.blockScale = 1.0f;
		}
		return c;
	}

	struct Client::Impl
	{
		httplib::Client cli;
		Impl(const std::string& h, int p) :
			cli(h, p)
		{
			cli.set_connection_timeout(0, 300'000);  // 300 ms
			cli.set_read_timeout(1, 0);
			cli.set_keep_alive(true);
		}
	};

	Client::Client(const std::string& a_host, int a_port) :
		_impl(std::make_unique<Impl>(a_host, a_port))
	{}

	Client::~Client() = default;

	bool Client::SendState(float a_x, float a_y, float a_z, float a_angle)
	{
		json body = { { "x", a_x }, { "y", a_y }, { "z", a_z }, { "angle", a_angle } };
		auto res = _impl->cli.Post("/fallout/state", body.dump(), "application/json");
		return res && res->status == 200;
	}

	bool Client::Poll(long long& a_lastSeq, std::vector<BlockEvent>& a_out)
	{
		auto res = _impl->cli.Get("/fallout/poll?since=" + std::to_string(a_lastSeq));
		if (!res || res->status != 200) {
			return false;
		}
		auto data = json::parse(res->body, nullptr, false);
		if (data.is_discarded()) {
			return false;
		}
		a_lastSeq = data.value("last_seq", a_lastSeq);
		if (!data.contains("events")) {
			return true;
		}
		for (const auto& ev : data["events"]) {
			const auto type = ev.value("type", "");
			if ((type != "place" && type != "dig") || !ev.contains("pos") || !ev.contains("fo_pos")) {
				continue;
			}
			a_out.push_back(BlockEvent{
				type == "place",
				static_cast<int>(std::lround(ev["pos"].value("x", 0.0))),
				static_cast<int>(std::lround(ev["pos"].value("y", 0.0))),
				static_cast<int>(std::lround(ev["pos"].value("z", 0.0))),
				ev["fo_pos"].value("x", 0.0f),
				ev["fo_pos"].value("y", 0.0f),
				ev["fo_pos"].value("z", 0.0f) });
		}
		return true;
	}
}

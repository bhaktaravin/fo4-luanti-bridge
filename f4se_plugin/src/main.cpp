// FO4LuantiBridge - Fallout 4 side of the Fallout 4 <-> Luanti bridge.
//
// A background thread talks to bridge.py (see net/). Everything that touches the
// game runs on the game thread through F4SE's task queue:
//   * every PollMs: read the player's position and send it to the bridge
//   * for each Luanti "place" event: PlaceAtMe the configured block object on the
//     player, then SetPosition/SetScale it to the matching spot
//   * for each Luanti "dig" event: Disable + Delete that object
//
// Objects are spawned and moved through the game's own Papyrus functions
// (ObjectReference.PlaceAtMe / SetPosition / SetScale / Disable / Delete), which
// is the most version-proof way to do it.
//
// Written against libxse/commonlibf4 (runtime 1.11.240).

#include "net.h"

// CommonLibF4 declares this destructor but doesn't define it; our callback class
// below needs a definition to link. If a future CommonLibF4 adds one, delete this.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace
{
	net::Config g_cfg;

	// ------------------------------------------------------------ shared state
	struct PlayerPos
	{
		float x, y, z, angle;
	};
	std::mutex        g_posLock;
	PlayerPos         g_playerPos{};
	std::atomic_bool  g_havePos{ false };
	std::atomic_bool  g_running{ false };
	std::atomic<long> g_generation{ 0 };  // bumped on every save load, invalidates old blocks

	// Luanti block coord -> spawned object. Touched only on the game thread.
	using BlockKey = std::tuple<int, int, int>;
	struct Block
	{
		RE::ObjectRefHandle handle;
		bool                pending = true;   // PlaceAtMe sent, result not back yet
		bool                cancelled = false; // dug before it finished spawning
	};
	std::map<BlockKey, Block> g_blocks;

	void RunOnGameThread(std::function<void()> a_fn)
	{
		if (const auto tasks = F4SE::GetTaskInterface()) {
			tasks->AddTask(std::move(a_fn));
		}
	}

	void Notify(const char* a_msg)
	{
		RunOnGameThread([a_msg] { RE::SendHUDMessage::ShowHUDMessage(a_msg, nullptr, false, false); });
	}

	// ------------------------------------------------------------ Papyrus calls
	using CallbackPtr = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;

	// Calls ObjectReference.<a_func>(args...) on a reference. Game thread only.
	template <class... Args>
	bool CallOnRef(RE::TESObjectREFR* a_ref, const char* a_func, const CallbackPtr& a_callback, Args... a_args)
	{
		const auto game = RE::GameVM::GetSingleton();
		auto       vm = game ? game->GetVM() : nullptr;
		if (!vm || !a_ref) {
			return false;
		}
		auto&      policy = vm->GetObjectHandlePolicy();
		const auto handle = policy.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), a_ref);
		if (handle == policy.EmptyHandle()) {
			return false;
		}
		return vm->DispatchMethodCall(handle, RE::BSFixedString("ObjectReference"), RE::BSFixedString(a_func), a_callback, a_args...);
	}

	void DeleteRef(RE::TESObjectREFR* a_ref)
	{
		CallOnRef(a_ref, "Disable", CallbackPtr{}, false);
		CallOnRef(a_ref, "Delete", CallbackPtr{});
	}

	void OnBlockSpawned(BlockKey a_key, RE::ObjectRefHandle a_handle, long a_gen, RE::NiPoint3 a_pos);

	// Receives the ObjectReference returned by PlaceAtMe.
	class PlaceCallback : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		PlaceCallback(BlockKey a_key, RE::NiPoint3 a_pos, long a_gen) :
			_key(a_key), _pos(a_pos), _gen(a_gen)
		{}

		void CallQueued() override {}
		void CallCanceled() override {}
		void StartMultiDispatch() override {}
		void EndMultiDispatch() override {}

		void operator()(RE::BSScript::Variable a_result) override
		{
			auto* ref = RE::BSScript::UnpackVariable<RE::TESObjectREFR>(a_result);
			RE::ObjectRefHandle handle = ref ? ref->GetHandle() : RE::ObjectRefHandle{};
			const auto key = _key;
			const auto pos = _pos;
			const auto gen = _gen;
			RunOnGameThread([key, handle, gen, pos] { OnBlockSpawned(key, handle, gen, pos); });
		}

	private:
		BlockKey     _key;
		RE::NiPoint3 _pos;
		long         _gen;
	};

	// ------------------------------------------------------------ game-thread work
	void ReadPlayerPos()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		const auto pos = player->GetPosition();
		std::lock_guard lk(g_posLock);
		g_playerPos = { pos.x, pos.y, pos.z, player->data.angle.z };
		g_havePos = true;
	}

	void PlaceBlock(BlockKey a_key, RE::NiPoint3 a_pos)
	{
		if (g_cfg.blockFormID == 0) {
			return;  // nothing configured to spawn
		}
		if (auto it = g_blocks.find(a_key); it != g_blocks.end()) {
			it->second.cancelled = false;  // re-placed while still spawning
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto base = RE::TESForm::GetFormByID(g_cfg.blockFormID);
		if (!player || !base) {
			REX::ERROR("BlockBaseFormID {:08X} not found", g_cfg.blockFormID);
			return;
		}
		CallbackPtr cb{ new PlaceCallback(a_key, a_pos, g_generation.load()) };
		// PlaceAtMe(akFormToPlace, aiCount, abForcePersist, abInitiallyDisabled, abDeleteWhenAble)
		if (CallOnRef(player, "PlaceAtMe", cb, base, 1, false, false, true)) {
			g_blocks[a_key] = Block{};
		}
	}

	void OnBlockSpawned(BlockKey a_key, RE::ObjectRefHandle a_handle, long a_gen, RE::NiPoint3 a_pos)
	{
		auto ref = a_handle.get();
		auto it = g_blocks.find(a_key);
		const bool stale = a_gen != g_generation.load() || it == g_blocks.end() || it->second.cancelled;
		if (!ref) {
			if (it != g_blocks.end()) {
				g_blocks.erase(it);
			}
			return;
		}
		if (stale) {
			DeleteRef(ref.get());
			if (it != g_blocks.end() && it->second.cancelled) {
				g_blocks.erase(it);
			}
			return;
		}
		CallOnRef(ref.get(), "SetPosition", CallbackPtr{}, a_pos.x, a_pos.y, a_pos.z);
		CallOnRef(ref.get(), "SetAngle", CallbackPtr{}, 0.0f, 0.0f, 0.0f);
		if (g_cfg.blockScale != 1.0f) {
			CallOnRef(ref.get(), "SetScale", CallbackPtr{}, g_cfg.blockScale);
		}
		it->second.handle = a_handle;
		it->second.pending = false;
	}

	void RemoveBlock(BlockKey a_key)
	{
		auto it = g_blocks.find(a_key);
		if (it == g_blocks.end()) {
			return;
		}
		if (it->second.pending) {
			it->second.cancelled = true;  // OnBlockSpawned will clean it up
			return;
		}
		if (auto ref = it->second.handle.get()) {
			DeleteRef(ref.get());
		}
		g_blocks.erase(it);
	}

	// ------------------------------------------------------------ network thread
	void NetworkLoop()
	{
		net::Client client(g_cfg.host, g_cfg.port);
		long long   lastSeq = 0;
		bool        connected = false;
		REX::INFO("Bridge thread started, talking to {}:{}", g_cfg.host, g_cfg.port);

		while (g_running) {
			RunOnGameThread(ReadPlayerPos);

			if (g_havePos) {
				PlayerPos p;
				{
					std::lock_guard lk(g_posLock);
					p = g_playerPos;
				}
				const bool ok = client.SendState(p.x, p.y, p.z, p.angle);
				if (ok != connected) {
					connected = ok;
					REX::INFO("Bridge {}", ok ? "connected" : "disconnected");
					Notify(ok ? "Luanti bridge connected" : "Luanti bridge disconnected");
				}

				std::vector<net::BlockEvent> events;
				if (ok && client.Poll(lastSeq, events)) {
					for (const auto& ev : events) {
						const BlockKey key{ ev.lx, ev.ly, ev.lz };
						if (ev.place) {
							const RE::NiPoint3 pos{ ev.fx, ev.fy, ev.fz };
							RunOnGameThread([key, pos] { PlaceBlock(key, pos); });
						} else {
							RunOnGameThread([key] { RemoveBlock(key); });
						}
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(g_cfg.pollMs));
		}
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			// A different save means our old object handles are meaningless.
			++g_generation;
			RunOnGameThread([] { g_blocks.clear(); });
			if (!g_running.exchange(true)) {
				std::thread(NetworkLoop).detach();
			}
			break;
		default:
			break;
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	g_cfg = net::LoadConfig();
	REX::INFO("FO4LuantiBridge loaded. BlockBaseFormID={:08X} scale={}", g_cfg.blockFormID, g_cfg.blockScale);
	if (g_cfg.blockFormID == 0) {
		REX::INFO("BlockBaseFormID is 0: positions will sync, but no blocks will spawn");
	}

	if (const auto messaging = F4SE::GetMessagingInterface()) {
		messaging->RegisterListener(OnF4SEMessage);
	}
	return true;
}

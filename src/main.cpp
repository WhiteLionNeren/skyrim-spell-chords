// Spell Chords: 12 extra favorites slots on Ctrl/Shift/Alt + 1-4, used exactly like vanilla hotkeys.
//
// Input: a hook on the engine's input dispatch (BSInputDeviceManager::PollInputDevices -> SendEvent) sees every keyboard
// event before any handler. A chord modifier (LCtrl/LShift/LAlt by default) is held back when pressed:
//   - a number key 1-4 while it's held  -> chord: both keys are swallowed (the game never sees them);
//   - released before anything else      -> tap: the game gets press + release late (sneak toggles on release);
//   - held for HoldMs, or any other key / mouse button pressed (movement keys included) -> the game gets the press then.
// Gameplay only (no pausing menu open), plus the Favorites menu, where a chord assigns the highlighted entry (SkyUI's
// itemList.selectedEntry.formId) to that slot; the same chord on the same item clears it.
// Use: the slot's form temporarily takes vanilla hotkey 8 (MagicFavorites::hotkeys[7] or the item's ExtraHotkey), a
// synthetic "Hotkey8" key press/release is injected so the engine's own FavoritesHandler runs, then hotkey 8 is restored.
// Slots live in the SKSE co-save (per save). Settings: SKSE/Plugins/SpellChords.ini. Log: SKSE/SpellChords.log.
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "shared.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_set>

using namespace std::literals;

namespace
{
	// ---------- settings ----------
	struct Settings
	{
		bool enabled = true;
		float holdSecs = 0.15f;
		std::array<std::uint32_t, 3> mods{ 0x1D, 0x2A, 0x38 };  // LCtrl, LShift, LAlt (DirectInput scan codes)
		std::array<std::uint32_t, 4> nums{ 0x02, 0x03, 0x04, 0x05 };  // 1-4
		bool notify = true;
		bool logMouse = false;  // [Debug] LogMouseState
	} g_set;

	std::string IniPath()
	{
		return SpellChords::DataPath("SKSE\\Plugins\\SpellChords.ini");
	}

	void LoadSettings()
	{
		auto path = IniPath();
		auto geti = [&](const char* sec, const char* key, int def) { return static_cast<int>(GetPrivateProfileIntA(sec, key, def, path.c_str())); };
		g_set.enabled = geti("General", "Enabled", 1) != 0;
		g_set.holdSecs = geti("General", "HoldMs", 150) / 1000.0f;
		g_set.notify = geti("General", "Notify", 1) != 0;
		g_set.logMouse = geti("Debug", "LogMouseState", 0) != 0;
		g_set.mods[0] = geti("Keys", "Modifier1", 0x1D);
		g_set.mods[1] = geti("Keys", "Modifier2", 0x2A);
		g_set.mods[2] = geti("Keys", "Modifier3", 0x38);
		for (int i = 0; i < 4; i++)
			g_set.nums[i] = geti("Keys", ("Number" + std::to_string(i + 1)).c_str(), 0x02 + i);
		SKSE::log::info("settings: enabled {}, hold {} s, modifiers {:#x}/{:#x}/{:#x}, numbers {:#x}-{:#x}", g_set.enabled,
			g_set.holdSecs, g_set.mods[0], g_set.mods[1], g_set.mods[2], g_set.nums[0], g_set.nums[3]);
	}

	void SaveGeneral()  // [General] values the MCM changes
	{
		auto path = IniPath();
		auto put = [&](const char* key, const std::string& v) { WritePrivateProfileStringA("General", key, v.c_str(), path.c_str()); };
		put("Enabled", g_set.enabled ? "1" : "0");
		put("HoldMs", std::to_string(static_cast<int>(std::lround(g_set.holdSecs * 1000.0f))));
		put("Notify", g_set.notify ? "1" : "0");
	}

	// ---------- Papyrus natives for the MCM (script SpellChords, all global) ----------
	bool GetChordsEnabled(RE::StaticFunctionTag*) { return g_set.enabled; }
	void SetChordsEnabled(RE::StaticFunctionTag*, bool a_on)
	{
		g_set.enabled = a_on;
		SaveGeneral();
		SKSE::log::info("MCM: chords {}", a_on ? "on" : "off");
	}
	std::int32_t GetHoldMs(RE::StaticFunctionTag*) { return static_cast<std::int32_t>(std::lround(g_set.holdSecs * 1000.0f)); }
	void SetHoldMs(RE::StaticFunctionTag*, std::int32_t a_ms)
	{
		g_set.holdSecs = std::clamp(a_ms, 50, 1000) / 1000.0f;
		SaveGeneral();
		SKSE::log::info("MCM: hold {} s", g_set.holdSecs);
	}
	bool GetNotify(RE::StaticFunctionTag*) { return g_set.notify; }
	void SetNotify(RE::StaticFunctionTag*, bool a_on)
	{
		g_set.notify = a_on;
		SaveGeneral();
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("GetChordsEnabled", "SpellChords", GetChordsEnabled, true);
		a_vm->RegisterFunction("SetChordsEnabled", "SpellChords", SetChordsEnabled);
		a_vm->RegisterFunction("GetHoldMs", "SpellChords", GetHoldMs, true);
		a_vm->RegisterFunction("SetHoldMs", "SpellChords", SetHoldMs);
		a_vm->RegisterFunction("GetNotify", "SpellChords", GetNotify, true);
		a_vm->RegisterFunction("SetNotify", "SpellChords", SetNotify);
		return SpellBar::RegisterPapyrus(a_vm);
	}

	int ModIndex(std::uint32_t key)
	{
		for (int i = 0; i < 3; i++)
			if (g_set.mods[i] == key) return i;
		return -1;
	}
	int NumIndex(std::uint32_t key)
	{
		for (int i = 0; i < 4; i++)
			if (g_set.nums[i] == key) return i;
		return -1;
	}
	const char* ModName(int m) { return m == 0 ? "Ctrl" : m == 1 ? "Shift" : "Alt"; }

	// ---------- slots (12 = 3 modifiers x 4 numbers), co-saved ----------
	std::array<RE::FormID, 12> g_slots{};
	std::mutex g_slotLock;

	// ---------- synthetic button events (game-allocated once, re-initialised per use) ----------
	struct Synth
	{
		RE::INPUT_DEVICE device;
		RE::BSFixedString userEvent;
		std::uint32_t id;
		float value;
		float held;
	};
	std::vector<RE::ButtonEvent*> g_pool;
	RE::ButtonEvent* PoolEvent(std::size_t i)
	{
		while (g_pool.size() <= i)
			g_pool.push_back(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ""sv, 0, 0.0f, 0.0f));
		return g_pool[i];
	}

	// ---------- hotkey 8 borrowing ----------
	enum class Borrow { kIdle, kPressSent, kReleaseSent };
	struct BorrowState
	{
		Borrow phase = Borrow::kIdle;
		RE::TESForm* oldMagic = nullptr;     // MagicFavorites::hotkeys[7] before
		RE::ExtraHotkey* setOn = nullptr;     // the ExtraHotkey we set to slot 8 (restored to its old value)
		RE::ExtraHotkey::Hotkey setOld = RE::ExtraHotkey::Hotkey::kUnbound;
		RE::ExtraHotkey* clearedOn = nullptr; // the item that had slot 8 (temporarily unbound)
		bool magicSet = false;
		std::uint32_t key = 0x09;             // the key mapped to "Hotkey8"
	} g_borrow;

	RE::ExtraHotkey* FindItemHotkey(RE::TESForm* a_form, bool a_withHotkey8)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto changes = player ? player->GetInventoryChanges() : nullptr;
		if (!changes || !changes->entryList) return nullptr;
		for (auto* entry : *changes->entryList) {
			if (!entry || !entry->extraLists) continue;
			if (a_form && entry->object != a_form) continue;
			for (auto* xList : *entry->extraLists) {
				if (!xList) continue;
				auto hk = xList->GetByType<RE::ExtraHotkey>();
				if (!hk) continue;
				if (a_withHotkey8 && hk->hotkey.get() != RE::ExtraHotkey::Hotkey::kSlot8) continue;
				return hk;
			}
		}
		return nullptr;
	}

	bool BeginBorrow(RE::TESForm* a_form)
	{
		if (g_borrow.phase != Borrow::kIdle || !a_form) return false;
		auto mf = RE::MagicFavorites::GetSingleton();
		bool magic = a_form->Is(RE::FormType::Spell, RE::FormType::Shout);
		if (magic) {
			if (!mf) return false;
			while (mf->hotkeys.size() < 8) mf->hotkeys.push_back(nullptr);
			g_borrow.oldMagic = mf->hotkeys[7];
			mf->hotkeys[7] = a_form;
			g_borrow.magicSet = true;
			g_borrow.clearedOn = FindItemHotkey(nullptr, true);  // an item holding 8 would compete
			if (g_borrow.clearedOn) g_borrow.clearedOn->hotkey = RE::ExtraHotkey::Hotkey::kUnbound;
		} else {
			auto hk = FindItemHotkey(a_form, false);
			if (!hk) {
				SKSE::log::warn("slot item {:08X} isn't a favorite in the inventory any more", a_form->GetFormID());
				return false;
			}
			g_borrow.clearedOn = FindItemHotkey(nullptr, true);
			if (g_borrow.clearedOn == hk) g_borrow.clearedOn = nullptr;
			if (g_borrow.clearedOn) g_borrow.clearedOn->hotkey = RE::ExtraHotkey::Hotkey::kUnbound;
			g_borrow.setOn = hk;
			g_borrow.setOld = hk->hotkey.get();
			hk->hotkey = RE::ExtraHotkey::Hotkey::kSlot8;
			if (mf && mf->hotkeys.size() >= 8 && mf->hotkeys[7]) {  // a spell holding 8 would compete
				g_borrow.oldMagic = mf->hotkeys[7];
				mf->hotkeys[7] = nullptr;
				g_borrow.magicSet = true;
			}
		}
		auto cm = RE::ControlMap::GetSingleton();
		auto key = cm ? cm->GetMappedKey("Hotkey8"sv, RE::INPUT_DEVICE::kKeyboard) : 0xFF;
		g_borrow.key = (key == 0xFF || key == 0) ? 0x09 : key;
		g_borrow.phase = Borrow::kPressSent;
		return true;
	}

	void EndBorrow()
	{
		auto mf = RE::MagicFavorites::GetSingleton();
		if (g_borrow.magicSet && mf && mf->hotkeys.size() >= 8) mf->hotkeys[7] = g_borrow.oldMagic;
		if (g_borrow.setOn) g_borrow.setOn->hotkey = g_borrow.setOld;
		if (g_borrow.clearedOn) g_borrow.clearedOn->hotkey = RE::ExtraHotkey::Hotkey::kSlot8;
		g_borrow = BorrowState{};
	}

	// ---------- chord state ----------
	struct Pending
	{
		int mod = -1;               // index into g_set.mods, -1 = none
		RE::BSFixedString userEvent; // the modifier's own user event (Sneak / Sprint / Run…)
		bool decided = false;       // passthrough: the game has the press
		bool chordUsed = false;     // swallowed until released
	} g_pending;
	std::unordered_set<std::uint32_t> g_swallow;  // number keys of fired chords: drop their held / release events
	std::atomic<int> g_heldMod{ -1 };             // modifier physically held (for the spell bar), whatever the game sees
	std::deque<Synth> g_nextFrame;                // synthetic events for the next dispatch (tap releases)

	std::string FormName(RE::TESForm* f)
	{
		if (!f) return "nothing";
		auto n = f->GetName();
		return n && *n ? n : fmt::format("{:08X}", f->GetFormID());
	}

	void Notify(const std::string& s)
	{
		if (g_set.notify) RE::SendHUDMessage::ShowHUDMessage(s.c_str());
	}

	void Assign(int a_mod, int a_num)
	{
		auto ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu(RE::FavoritesMenu::MENU_NAME) : nullptr;
		if (!menu || !menu->uiMovie) return;
		RE::GFxValue v;
		RE::FormID id = 0;
		for (auto path : { "_root.MenuHolder.Menu_mc.itemList.selectedEntry.formId", "_root.Menu_mc.itemList.selectedEntry.formId" }) {
			if (menu->uiMovie->GetVariable(&v, path) && v.IsNumber()) {
				id = static_cast<RE::FormID>(static_cast<std::uint64_t>(v.GetNumber()) & 0xFFFFFFFF);
				break;
			}
		}
		int slot = a_mod * 4 + a_num;
		auto form = id ? RE::TESForm::LookupByID(id) : nullptr;
		std::lock_guard lk(g_slotLock);
		if (!form) {
			SKSE::log::info("assign {}+{}: no selected entry", ModName(a_mod), a_num + 1);
			return;
		}
		if (g_slots[slot] == id) {
			g_slots[slot] = 0;
			Notify(fmt::format("{}+{}: cleared", ModName(a_mod), a_num + 1));
		} else {
			g_slots[slot] = id;
			Notify(fmt::format("{}+{}: {}", ModName(a_mod), a_num + 1, FormName(form)));
		}
		SKSE::log::info("slot {}+{} = {:08X} {}", ModName(a_mod), a_num + 1, g_slots[slot], FormName(form));
	}

	void Fire(int a_mod, int a_num)
	{
		RE::FormID id;
		{
			std::lock_guard lk(g_slotLock);
			id = g_slots[a_mod * 4 + a_num];
		}
		auto form = id ? RE::TESForm::LookupByID(id) : nullptr;
		if (!form) {
			SKSE::log::info("{}+{}: empty slot", ModName(a_mod), a_num + 1);
			return;
		}
		if (!BeginBorrow(form)) return;
		SKSE::log::info("{}+{}: using {:08X} {} through hotkey 8 (key {:#x})", ModName(a_mod), a_num + 1, id, FormName(form), g_borrow.key);
	}

	enum class Mode { kOff, kGameplay, kFavorites };
	Mode CurrentMode()
	{
		auto ui = RE::UI::GetSingleton();
		if (!g_set.enabled || !ui) return Mode::kOff;
		if (ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) return Mode::kFavorites;
		if (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) return Mode::kOff;
		auto main = RE::Main::GetSingleton();
		if (main && !main->GetRuntimeData().gameActive) return Mode::kOff;
		return Mode::kGameplay;
	}

	// ---------- mouse-input diagnostic (off by default) ----------
	// For hunting "mouse clicks do nothing in gameplay" bugs caused by other plugins' in-memory state (keys and menu clicks
	// still work, a restart fixes it). With [Debug] LogMouseState=1 every mouse button press in gameplay (no cursor menu,
	// not paused) logs what can block it, rate-limited: the attack handler's own CanProcess verdict for this click, byte 08
	// of every player input handler (on/off; compare with a normal line), ControlMap text entry / ignore keyboard+mouse /
	// enabled controls / input-context stack, MenuControls remap mode, open menus and the weapon state. Input hooks
	// installed after this one (e.g. Community Shaders') still see the events later.
	void LogMouseState(RE::ButtonEvent* a_button)
	{
		static std::chrono::steady_clock::time_point last{};
		static int lines = 0;
		const auto now = std::chrono::steady_clock::now();
		if (!g_set.logMouse || lines >= 300 || now - last < std::chrono::milliseconds(1500)) return;
		auto ui = RE::UI::GetSingleton();
		if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)) return;  // a click meant for a menu
		last = now;
		lines++;
		std::string handlers = "n/a", attack = "n/a";
		if (auto pc = RE::PlayerControls::GetSingleton()) {
			auto b = [](auto* h) { return h ? fmt::format("{}", *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(h) + 8)) : std::string("-"); };
			handlers = fmt::format("move {} look {} sprint {} ready {} automove {} togglerun {} activate {} jump {} shout {} attack {} run {} sneak {} pov {}",
				b(pc->movementHandler), b(pc->lookHandler), b(pc->sprintHandler), b(pc->readyWeaponHandler), b(pc->autoMoveHandler),
				b(pc->toggleRunHandler), b(pc->activateHandler), b(pc->jumpHandler), b(pc->shoutHandler), b(pc->attackBlockHandler),
				b(pc->runHandler), b(pc->sneakHandler), b(pc->togglePOVHandler));
			if (pc->attackBlockHandler) attack = pc->attackBlockHandler->CanProcess(a_button) ? "yes" : "NO";
		}
		std::string control = "n/a";
		if (auto cm = RE::ControlMap::GetSingleton()) {
			auto& r = cm->GetRuntimeData();
			std::string stack;
			for (auto c : r.contextPriorityStack) stack += fmt::format("{} ", static_cast<int>(c));
			control = fmt::format("textEntry {} ignoreKbMouse {} enabled {:#x} stored {:#x} contexts [ {}]", static_cast<int>(r.textEntryCount),
				r.ignoreKeyboardMouse, r.enabledControls.underlying(), r.storedControls.underlying(), stack);
		}
		std::string menuControls = "n/a";
		if (auto mc = RE::MenuControls::GetSingleton()) {
			auto& r = mc->GetRuntimeData();
			menuControls = fmt::format("processing {} remap {} beast {}", r.isProcessing, r.remapMode, r.beastForm);
		}
		std::string menus;
		for (auto& [name, entry] : ui->menuMap)
			if (entry.menu && ui->IsMenuOpen(name)) menus += fmt::format("{}, ", name.c_str());
		auto player = RE::PlayerCharacter::GetSingleton();
		SKSE::log::info("mouse {} '{}': attack handler CanProcess {} | handlers(byte08) {} | ControlMap {} | MenuControls {} | menus {}| weaponDrawn {} attackState {}",
			a_button->GetIDCode(), a_button->QUserEvent().c_str(), attack, handlers, control, menuControls, menus,
			player && player->AsActorState()->IsWeaponDrawn(), player ? static_cast<int>(player->AsActorState()->GetAttackState()) : -1);
	}

	// ---------- the hook ----------
	struct InputHook
	{
		static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
		{
			std::vector<RE::InputEvent*> out;
			std::size_t used = 0;
			auto synth = [&](const Synth& s) {
				auto e = PoolEvent(used++);
				e->Init(s.device, static_cast<std::int32_t>(s.id), s.value, s.held, s.userEvent);
				out.push_back(e);
			};
			for (auto& s : g_nextFrame) synth(s);
			g_nextFrame.clear();
			bool endBorrowAfter = false;

			// hotkey 8 borrowing runs regardless of mode: press this frame, release next, restore after
			switch (g_borrow.phase) {
			case Borrow::kPressSent:
				synth({ RE::INPUT_DEVICE::kKeyboard, "Hotkey8", g_borrow.key, 1.0f, 0.0f });
				g_borrow.phase = Borrow::kReleaseSent;
				break;
			case Borrow::kReleaseSent:
				synth({ RE::INPUT_DEVICE::kKeyboard, "Hotkey8", g_borrow.key, 0.0f, 0.05f });
				endBorrowAfter = true;  // the engine handles it inside this dispatch: restore hotkey 8 afterwards
				break;
			default:
				break;
			}

			auto mode = CurrentMode();
			auto releasePending = [&](bool a_sendPress) {
				if (a_sendPress && g_pending.mod >= 0 && !g_pending.decided && !g_pending.chordUsed)
					synth({ RE::INPUT_DEVICE::kKeyboard, g_pending.userEvent, g_set.mods[g_pending.mod], 1.0f, 0.0f });
				g_pending.decided = true;
			};

			for (auto e = a_events ? *a_events : nullptr; e; e = e->next) {
				auto b = e->AsButtonEvent();
				if (b && b->GetDevice() == RE::INPUT_DEVICE::kMouse && b->IsDown()) LogMouseState(b);
				if (b && b->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
					int hm = ModIndex(b->GetIDCode());
					if (hm >= 0 && b->IsDown()) g_heldMod = hm;
					else if (hm >= 0 && b->IsUp() && g_heldMod == hm) g_heldMod = -1;
				}
				if (mode == Mode::kOff || !b) {
					if (b && g_pending.mod >= 0 && !g_pending.decided && b->IsDown()) releasePending(true);
					out.push_back(e);
					continue;
				}
				bool keyboard = b->GetDevice() == RE::INPUT_DEVICE::kKeyboard;
				std::uint32_t key = b->GetIDCode();
				int m = keyboard ? ModIndex(key) : -1;
				int n = keyboard ? NumIndex(key) : -1;

				if (keyboard && g_swallow.contains(key)) {  // number key of a fired chord
					if (b->IsUp()) g_swallow.erase(key);
					continue;
				}
				if (m >= 0) {
					if (g_pending.mod < 0) {
						if (b->IsDown()) {
							g_pending = Pending{ m, b->QUserEvent(), false, false };
							continue;  // held back
						}
						out.push_back(e);
						continue;
					}
					if (m == g_pending.mod) {
						if (g_pending.decided) {  // the game has the press (also after a chord): it gets the release too
							out.push_back(e);
							if (b->IsUp()) g_pending = Pending{};
							continue;
						}
						if (g_pending.chordUsed) {
							if (b->IsUp()) g_pending = Pending{};
							continue;
						}
						if (b->IsUp()) {  // tap: press now, release next frame
							synth({ RE::INPUT_DEVICE::kKeyboard, g_pending.userEvent, key, 1.0f, 0.0f });
							g_nextFrame.push_back({ RE::INPUT_DEVICE::kKeyboard, g_pending.userEvent, key, 0.0f, b->HeldDuration() });
							g_pending = Pending{};
							continue;
						}
						if (b->HeldDuration() >= g_set.holdSecs) {  // held: hand it to the game
							releasePending(true);
							continue;
						}
						continue;  // still undecided
					}
					// a second modifier while one is pending: the pending one is a normal key
					releasePending(true);
					out.push_back(e);
					continue;
				}
				// a number while the modifier is held is a chord, also after the game got the modifier (held past HoldMs or
				// another key pressed first): 1.1.1 and earlier let the number through then, so it fired vanilla hotkey 1-4
				if (n >= 0 && g_pending.mod >= 0) {
					if (b->IsDown()) {
						if (g_pending.decided)
							SKSE::log::info("{}+{}: chord after the game got {} ({:.2f} s held)", ModName(g_pending.mod), n + 1,
								ModName(g_pending.mod), b->HeldDuration());
						g_pending.chordUsed = true;
						g_swallow.insert(key);
						if (mode == Mode::kFavorites)
							Assign(g_pending.mod, n);
						else
							Fire(g_pending.mod, n);
					}
					continue;
				}
				// any other key or button while a modifier is pending: it isn't a chord
				if (g_pending.mod >= 0 && !g_pending.decided && !g_pending.chordUsed && b->IsDown())
					releasePending(true);
				out.push_back(e);
			}
			if (mode == Mode::kOff && g_pending.mod >= 0 && !g_pending.decided && !g_pending.chordUsed)
				releasePending(true);

			for (std::size_t i = 0; i < out.size(); i++)
				out[i]->next = i + 1 < out.size() ? out[i + 1] : nullptr;
			RE::InputEvent* head = out.empty() ? nullptr : out[0];
			func(a_dispatcher, &head);
			if (endBorrowAfter) EndBorrow();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Resolved in SKSEPlugin_Load: CommonLib opens the Address Library through a relative path on the first lookup, and
	// by kDataLoaded another plugin may have changed the working directory (SkyrimNet does: "failed to open address
	// library file").
	REL::Relocation<std::uintptr_t> g_inputTarget;

	bool InstallHook()
	{
		auto& target = g_inputTarget;
		auto op = *reinterpret_cast<std::uint8_t*>(target.address());
		if (op != 0xE8) {
			SKSE::log::error("input dispatch call not found at {:#x} (byte {:#04x}); chords disabled", target.offset(), op);
			return false;
		}
		auto& t = SKSE::GetTrampoline();
		InputHook::func = t.write_call<5>(target.address(), InputHook::thunk);
		SKSE::log::info("input hook installed at {:#x}", target.offset());
		return true;
	}

	// ---------- co-save ----------
	constexpr std::uint32_t kRecord = 'SLOT';
	void OnSave(SKSE::SerializationInterface* a)
	{
		std::lock_guard lk(g_slotLock);
		if (a->OpenRecord(kRecord, 1)) a->WriteRecordData(g_slots.data(), static_cast<std::uint32_t>(sizeof(g_slots)));
	}
	void OnLoad(SKSE::SerializationInterface* a)
	{
		std::uint32_t type, version, length;
		std::lock_guard lk(g_slotLock);
		g_slots.fill(0);
		while (a->GetNextRecordInfo(type, version, length)) {
			if (type != kRecord || length != sizeof(g_slots)) continue;
			std::array<RE::FormID, 12> raw{};
			a->ReadRecordData(raw.data(), static_cast<std::uint32_t>(sizeof(raw)));
			for (int i = 0; i < 12; i++) {
				RE::FormID id = 0;
				if (raw[i] && a->ResolveFormID(raw[i], id)) g_slots[i] = id;
			}
		}
		int n = 0;
		for (auto id : g_slots) n += id != 0;
		SKSE::log::info("co-save: {} slot(s) loaded", n);
	}
	void OnRevert(SKSE::SerializationInterface*)
	{
		std::lock_guard lk(g_slotLock);
		g_slots.fill(0);
	}

	void SetupLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) return;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "SpellChords.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}
}

RE::FormID SpellChords::Slot(int a_mod, int a_num)
{
	if (a_mod < 0 || a_mod > 2 || a_num < 0 || a_num > 3) return 0;
	std::lock_guard lk(g_slotLock);
	return g_slots[a_mod * 4 + a_num];
}
int SpellChords::HeldModifier() { return g_heldMod.load(); }
const char* SpellChords::ModifierName(int a_mod) { return ModName(a_mod); }
std::string SpellChords::DataPath(std::string_view a_rel)
{
	static const std::string root = [] {
		wchar_t buf[MAX_PATH]{};
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return (std::filesystem::path(buf).parent_path() / "Data").string();
	}();
	return a_rel.empty() ? root : root + "\\" + std::string(a_rel);
}

extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(REL::Version(1, 2, 0, 0));
	v.PluginName("SpellChords");
	v.AuthorName("WhiteLionNeren & Claude");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
	return v;
}();

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "SpellChords";
	a_info->version = 1;
	return true;
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, false);
	g_inputTarget = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(67315, 68617), REL::Relocate(0x7B, 0x7B, 0x81) };
	SpellChords::DataPath("");
	SetupLog();
	SKSE::log::info("Spell Chords 1.2.0");
	LoadSettings();
	SKSE::AllocTrampoline(32);
	auto ser = SKSE::GetSerializationInterface();
	ser->SetUniqueID('SCHD');
	ser->SetSaveCallback(OnSave);
	ser->SetLoadCallback(OnLoad);
	ser->SetRevertCallback(OnRevert);
	SKSE::GetPapyrusInterface()->Register(RegisterPapyrus);
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* m) {
		if (m->type == SKSE::MessagingInterface::kDataLoaded) {
			InstallHook();
			SpellBar::Install();
		}
	});
	return true;
}

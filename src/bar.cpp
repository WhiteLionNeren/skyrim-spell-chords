// Spell bar (part of Spell Chords, 1.1.0): an MMO-style action bar on the HUD showing what keys 1-4 do, plain (vanilla
// favorites hotkeys 1-4) and with each chord modifier (Spell Chords' Ctrl/Shift/Alt slots). Drawn through SKSE Menu Framework
// 3's HUD element API (ImGui). While a chord modifier is held, its row lights up and the others dim; Layout=1 shows one row
// that pages to the held modifier's row instead (WoW-style).
// Per slot: an icon by kind (FontAwesome 6 glyph, or our SVGs for weapon types) tinted by school / element / potion type,
// the key, a stack count for consumables, a red wash when a spell costs more magicka than you have, a dark sweep + seconds
// while the shout cooldown runs, and a gold frame on what's equipped (either hand, or the selected power/shout).
// Hidden in menus (except Favorites, where you assign chords), while the HUD is hidden, and per ShowWhen.
// Settings: [Bar] in SpellChords.ini, editable live in the F1 menu (Spell Chords > Spell Bar), saved back to the ini. The
// MCM (SpellChordsMCM.psc) picks one of the position presets through the Papyrus natives at the end of this file.
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "SKSEMenuFramework.h"
#include "shared.h"

#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>

using namespace ImGuiMCP;

namespace
{
	// ---------- settings ----------
	struct BarSettings
	{
		bool enabled = true;
		int layout = 0;          // 0 = all rows (plain row + every modifier row that has a slot), 1 = one row, paged by the held modifier
		int showWhen = 0;        // 0 always, 1 weapon/magic drawn or in combat, 2 in combat only
		float iconSize = 46.0f;  // px at 1080p
		float gap = 5.0f;
		int anchor = 0;          // which point of the bar PosX places: 0 left edge, 1 centre, 2 right edge
		float posX = 0.015f;     // that point, fraction of screen width
		int anchorY = 0;         // 0: PosY is the bar's bottom edge, rows stack upward; 1: PosY is its top edge, rows stack downward
		float posY = 0.97f;      // that edge, fraction of screen height
		float opacity = 0.9f;
		float dimInactive = 0.45f;
		bool showNames = false;
		bool showEmpty = true;   // empty slots of shown rows as faint frames
		bool hideEmptyRows = true;
	} g_bar;

	const char* Ini()
	{
		static const std::string s = SpellChords::DataPath("SKSE\\Plugins\\SpellChords.ini");
		return s.c_str();
	}

	float GetF(const char* key, float def)
	{
		char buf[64]{};
		GetPrivateProfileStringA("Bar", key, "", buf, sizeof(buf), Ini());
		return buf[0] ? static_cast<float>(std::atof(buf)) : def;
	}
	int GetI(const char* key, int def) { return static_cast<int>(GetPrivateProfileIntA("Bar", key, def, Ini())); }

	void LoadBarSettings()
	{
		g_bar.enabled = GetI("Enabled", 1) != 0;
		g_bar.layout = GetI("Layout", 0);
		g_bar.showWhen = GetI("ShowWhen", 0);
		g_bar.iconSize = GetF("IconSize", 46.0f);
		g_bar.gap = GetF("Gap", 5.0f);
		g_bar.anchor = std::clamp(GetI("Anchor", 0), 0, 2);
		g_bar.posX = GetF("PosX", 0.015f);
		g_bar.anchorY = std::clamp(GetI("AnchorY", 0), 0, 1);
		g_bar.posY = GetF("PosY", 0.97f);
		g_bar.opacity = GetF("Opacity", 0.9f);
		g_bar.dimInactive = GetF("DimInactive", 0.45f);
		g_bar.showNames = GetI("ShowNames", 0) != 0;
		g_bar.showEmpty = GetI("ShowEmpty", 1) != 0;
		g_bar.hideEmptyRows = GetI("HideEmptyRows", 1) != 0;
	}

	void SaveBarSettings()
	{
		auto put = [](const char* key, const std::string& v) { WritePrivateProfileStringA("Bar", key, v.c_str(), Ini()); };
		put("Enabled", g_bar.enabled ? "1" : "0");
		put("Layout", std::to_string(g_bar.layout));
		put("ShowWhen", std::to_string(g_bar.showWhen));
		put("IconSize", fmt::format("{:.0f}", g_bar.iconSize));
		put("Gap", fmt::format("{:.0f}", g_bar.gap));
		put("Anchor", std::to_string(g_bar.anchor));
		put("PosX", fmt::format("{:.3f}", g_bar.posX));
		put("AnchorY", std::to_string(g_bar.anchorY));
		put("PosY", fmt::format("{:.3f}", g_bar.posY));
		put("Opacity", fmt::format("{:.2f}", g_bar.opacity));
		put("DimInactive", fmt::format("{:.2f}", g_bar.dimInactive));
		put("ShowNames", g_bar.showNames ? "1" : "0");
		put("ShowEmpty", g_bar.showEmpty ? "1" : "0");
		put("HideEmptyRows", g_bar.hideEmptyRows ? "1" : "0");
		SKSE::log::info("spell bar settings saved");
	}

	// ---------- position presets (MCM and F1 page) ----------
	struct Preset
	{
		const char* name;
		int anchor;
		float posX;
		int anchorY;
		float posY;
	};
	// Bottom center sits above the vanilla health/magicka/stamina bars, top center below the compass.
	constexpr Preset kPresets[] = {
		{ "Bottom left", 0, 0.015f, 0, 0.97f },
		{ "Bottom center", 1, 0.5f, 0, 0.86f },
		{ "Bottom right", 2, 0.985f, 0, 0.97f },
		{ "Top left", 0, 0.015f, 1, 0.03f },
		{ "Top center", 1, 0.5f, 1, 0.09f },
		{ "Top right", 2, 0.985f, 1, 0.03f },
	};
	constexpr int kPresetCount = static_cast<int>(std::size(kPresets));

	int CurrentPreset()  // index into kPresets, or -1 when the position was set by hand (F1 sliders / ini)
	{
		for (int i = 0; i < kPresetCount; i++) {
			auto& p = kPresets[i];
			if (g_bar.anchor == p.anchor && g_bar.anchorY == p.anchorY && std::abs(g_bar.posX - p.posX) < 0.0015f &&
				std::abs(g_bar.posY - p.posY) < 0.0015f)
				return i;
		}
		return -1;
	}

	void ApplyPreset(int a_index)
	{
		if (a_index < 0 || a_index >= kPresetCount) return;
		auto& p = kPresets[a_index];
		g_bar.anchor = p.anchor;
		g_bar.posX = p.posX;
		g_bar.anchorY = p.anchorY;
		g_bar.posY = p.posY;
		SKSE::log::info("spell bar position: {}", p.name);
	}

	bool g_barAvailable = false;  // SKSE Menu Framework found and the HUD element registered

	// ---------- icons ----------
	struct Icon
	{
		std::uint32_t glyph = 0;   // FontAwesome solid codepoint (PUA), or 0 when svg is set
		const char* svg = nullptr; // our weapon icons: Data/SKSE/Plugins/SpellChords/<name>.svg
		ImU32 color = IM_COL32(230, 230, 230, 255);
	};

	constexpr ImU32 Rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }

	Icon SpellIcon(RE::MagicItem* a_item)
	{
		auto eff = a_item ? a_item->GetCostliestEffectItem() : nullptr;
		auto mgef = eff ? eff->baseEffect : nullptr;
		if (!mgef) return { 0xe2ca, nullptr, Rgb(224, 224, 255) };  // wand-magic-sparkles
		auto resist = mgef->data.resistVariable;
		auto arch = mgef->GetArchetype();
		switch (mgef->GetMagickSkill()) {
		case RE::ActorValue::kDestruction:
			if (resist == RE::ActorValue::kResistFire) return { 0xf06d, nullptr, Rgb(255, 122, 42) };      // fire
			if (resist == RE::ActorValue::kResistFrost) return { 0xf2dc, nullptr, Rgb(143, 211, 255) };    // snowflake
			if (resist == RE::ActorValue::kResistShock) return { 0xf0e7, nullptr, Rgb(180, 156, 255) };    // bolt
			return { 0xe4e9, nullptr, Rgb(255, 96, 64) };                                                    // explosion
		case RE::ActorValue::kRestoration:
			if (arch == RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude) return { 0xe574, nullptr, Rgb(255, 214, 94) };  // ward
			if (arch == RE::EffectArchetypes::ArchetypeID::kTurnUndead) return { 0xf185, nullptr, Rgb(255, 214, 94) };          // sun
			return { 0xf21e, nullptr, Rgb(255, 214, 94) };                                                   // heart-pulse
		case RE::ActorValue::kAlteration:
			if (arch == RE::EffectArchetypes::ArchetypeID::kLight) return { 0xf0eb, nullptr, Rgb(111, 224, 176) };  // lightbulb
			return { 0xf3ed, nullptr, Rgb(111, 224, 176) };                                                  // shield-halved
		case RE::ActorValue::kIllusion:
			if (arch == RE::EffectArchetypes::ArchetypeID::kInvisibility) return { 0xf6fa, nullptr, Rgb(201, 139, 255) };  // mask
			return { 0xf06e, nullptr, Rgb(201, 139, 255) };                                                  // eye
		case RE::ActorValue::kConjuration:
			if (arch == RE::EffectArchetypes::ArchetypeID::kBoundWeapon) return { 0, "sword", Rgb(122, 168, 255) };
			return { 0xf6e2, nullptr, Rgb(122, 168, 255) };                                                  // ghost
		default:
			return { 0xe2ca, nullptr, Rgb(224, 224, 255) };
		}
	}

	Icon IconFor(RE::TESForm* f)
	{
		if (!f) return {};
		switch (f->GetFormType()) {
		case RE::FormType::Spell:
			{
				auto sp = f->As<RE::SpellItem>();
				auto t = sp->GetSpellType();
				if (t == RE::MagicSystem::SpellType::kPower || t == RE::MagicSystem::SpellType::kLesserPower)
					return { 0xf005, nullptr, Rgb(255, 207, 74) };  // star
				return SpellIcon(sp);
			}
		case RE::FormType::Shout:
			return { 0xf72e, nullptr, Rgb(191, 232, 255) };  // wind
		case RE::FormType::Scroll:
			return { 0xf70e, nullptr, Rgb(232, 211, 154) };  // scroll
		case RE::FormType::AlchemyItem:
			{
				auto al = f->As<RE::AlchemyItem>();
				if (al->IsFood()) return { 0xf0f5, nullptr, Rgb(217, 160, 102) };    // utensils
				if (al->IsPoison()) return { 0xf714, nullptr, Rgb(123, 214, 74) };   // skull-crossbones
				auto eff = al->GetCostliestEffectItem();
				auto av = eff && eff->baseEffect ? eff->baseEffect->data.primaryAV : RE::ActorValue::kNone;
				if (av == RE::ActorValue::kHealth) return { 0xf0c3, nullptr, Rgb(255, 77, 77) };
				if (av == RE::ActorValue::kMagicka) return { 0xf0c3, nullptr, Rgb(77, 141, 255) };
				if (av == RE::ActorValue::kStamina) return { 0xf0c3, nullptr, Rgb(77, 217, 107) };
				return { 0xf0c3, nullptr, Rgb(211, 139, 255) };  // flask
			}
		case RE::FormType::Weapon:
			{
				auto w = f->As<RE::TESObjectWEAP>();
				constexpr ImU32 steel = Rgb(208, 212, 220);
				switch (w->GetWeaponType()) {
				case RE::WEAPON_TYPE::kOneHandSword:
				case RE::WEAPON_TYPE::kTwoHandSword:
					return { 0, "sword", steel };
				case RE::WEAPON_TYPE::kOneHandDagger:
					return { 0, "dagger", steel };
				case RE::WEAPON_TYPE::kOneHandAxe:
					return { 0, "axe", steel };
				case RE::WEAPON_TYPE::kTwoHandAxe:
					if (w->HasKeywordString("WeapTypeWarhammer")) return { 0xf6e3, nullptr, steel };  // hammer
					return { 0, "axe", steel };
				case RE::WEAPON_TYPE::kOneHandMace:
					return { 0, "mace", steel };
				case RE::WEAPON_TYPE::kBow:
					return { 0, "bow", steel };
				case RE::WEAPON_TYPE::kCrossbow:
					return { 0, "crossbow", steel };
				case RE::WEAPON_TYPE::kStaff:
					return { 0xe2ca, nullptr, Rgb(198, 168, 255) };  // wand-magic-sparkles
				case RE::WEAPON_TYPE::kHandToHandMelee:
					return { 0xf6de, nullptr, steel };  // hand-fist
				default:
					return { 0, "sword", steel };
				}
			}
		case RE::FormType::Armor:
			{
				auto a = f->As<RE::TESObjectARMO>();
				if (a->IsShield()) return { 0xf132, nullptr, Rgb(208, 212, 220) };  // shield
				return { 0xf553, nullptr, Rgb(208, 212, 220) };                     // shirt
			}
		case RE::FormType::Light:
			return { 0xf7e4, nullptr, Rgb(255, 179, 71) };  // fire-flame-curved (torch)
		case RE::FormType::Ingredient:
			return { 0xf06c, nullptr, Rgb(150, 210, 120) };  // leaf
		case RE::FormType::SoulGem:
			return { 0xf3a5, nullptr, Rgb(190, 160, 255) };  // gem
		case RE::FormType::Book:
			return { 0xf02d, nullptr, Rgb(232, 211, 154) };  // book
		case RE::FormType::Ammo:
			return { 0xf05b, nullptr, Rgb(208, 212, 220) };  // crosshairs
		default:
			return { 0xf059, nullptr, Rgb(200, 200, 200) };  // circle-question
		}
	}

	std::string Utf8(std::uint32_t cp)
	{
		std::string s;
		if (cp < 0x80) s += static_cast<char>(cp);
		else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
		else { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
		return s;
	}

	std::unordered_map<std::string, ImTextureID> g_tex;
	ImTextureID Svg(const char* name)
	{
		auto it = g_tex.find(name);
		if (it != g_tex.end()) return it->second;
		auto t = SKSEMenuFramework::LoadTexture(SpellChords::DataPath(fmt::format("SKSE\\Plugins\\SpellChords\\{}.svg", name)), { 128, 128 });
		g_tex[name] = t;
		return t;
	}

	// ---------- slot snapshot (refreshed a few times per second) ----------
	struct SlotInfo
	{
		RE::TESForm* form = nullptr;
		Icon icon;
		std::string name;
		int count = -1;  // stack count for consumables, -1 = don't show
	};
	std::array<std::array<SlotInfo, 4>, 4> g_rows;  // row 0 = plain 1-4, rows 1-3 = Ctrl/Shift/Alt chords
	std::chrono::steady_clock::time_point g_nextRefresh{};

	RE::TESForm* VanillaHotkey(int a_num)
	{
		if (auto mf = RE::MagicFavorites::GetSingleton(); mf && mf->hotkeys.size() > static_cast<std::uint32_t>(a_num) && mf->hotkeys[a_num])
			return mf->hotkeys[a_num];
		auto player = RE::PlayerCharacter::GetSingleton();
		auto changes = player ? player->GetInventoryChanges() : nullptr;
		if (!changes || !changes->entryList) return nullptr;
		for (auto* entry : *changes->entryList) {
			if (!entry || !entry->extraLists || !entry->object) continue;
			for (auto* xList : *entry->extraLists) {
				auto hk = xList ? xList->GetByType<RE::ExtraHotkey>() : nullptr;
				if (hk && static_cast<int>(hk->hotkey.get()) == a_num) return entry->object;
			}
		}
		return nullptr;
	}

	void Refresh()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		std::unordered_map<RE::TESBoundObject*, int> counts;
		bool needCounts = false;
		for (int r = 0; r < 4; r++)
			for (int n = 0; n < 4; n++) {
				auto& s = g_rows[r][n];
				s = SlotInfo{};
				s.form = r == 0 ? VanillaHotkey(n) : RE::TESForm::LookupByID(SpellChords::Slot(r - 1, n));
				if (!s.form) continue;
				s.icon = IconFor(s.form);
				auto nm = s.form->GetName();
				s.name = nm ? nm : "";
				if (s.form->Is(RE::FormType::AlchemyItem, RE::FormType::Scroll, RE::FormType::Ammo, RE::FormType::Ingredient, RE::FormType::SoulGem, RE::FormType::Light))
					needCounts = true;
			}
		if (needCounts && player) {
			for (auto& [obj, data] : player->GetInventory())
				counts[obj] = data.first;
			for (auto& row : g_rows)
				for (auto& s : row)
					if (s.form && s.form->Is(RE::FormType::AlchemyItem, RE::FormType::Scroll, RE::FormType::Ammo, RE::FormType::Ingredient, RE::FormType::SoulGem, RE::FormType::Light)) {
						auto it = counts.find(s.form->As<RE::TESBoundObject>());
						s.count = it == counts.end() ? 0 : it->second;
					}
		}
	}

	// ---------- visibility ----------
	float g_alpha = 0.0f;  // eased overall alpha

	bool WantVisible()
	{
		if (!g_bar.enabled) return false;
		auto player = RE::PlayerCharacter::GetSingleton();
		auto ui = RE::UI::GetSingleton();
		auto main = RE::Main::GetSingleton();
		if (!player || !ui || !player->Is3DLoaded() || (main && !main->GetRuntimeData().gameActive)) return false;
		if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) return false;
		static const std::array<std::string_view, 16> hideIn{ RE::DialogueMenu::MENU_NAME, RE::BarterMenu::MENU_NAME,
			RE::ContainerMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME, RE::BookMenu::MENU_NAME,
			RE::TrainingMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::MapMenu::MENU_NAME, RE::JournalMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME, RE::Console::MENU_NAME, RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME,
			RE::LoadingMenu::MENU_NAME, RE::RaceSexMenu::MENU_NAME };
		bool favorites = ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME);
		if (!favorites) {
			if (ui->GameIsPaused()) return false;
			for (auto m : hideIn)
				if (ui->IsMenuOpen(m)) return false;
			if (auto hud = ui->GetMenu<RE::HUDMenu>(); !hud || !hud->uiMovie || !hud->uiMovie->GetVisible()) return false;
			if (g_bar.showWhen == 1 && !player->AsActorState()->IsWeaponDrawn() && !player->IsInCombat()) return false;
			if (g_bar.showWhen == 2 && !player->IsInCombat()) return false;
		}
		return true;
	}

	bool Equipped(RE::PlayerCharacter* p, RE::TESForm* f)
	{
		if (!f) return false;
		if (p->GetEquippedObject(true) == f || p->GetEquippedObject(false) == f) return true;
		if (p->GetActorRuntimeData().selectedPower == f) return true;
		if (f->Is(RE::FormType::Armor)) {
			auto changes = p->GetInventoryChanges();
			if (changes && changes->entryList)
				for (auto* e : *changes->entryList)
					if (e && e->object == f) return e->IsWorn();
		}
		return false;
	}

	void __stdcall Draw()
	{
		bool want = WantVisible();
		auto& io = *GetIO();
		float dt = io.DeltaTime > 0 ? io.DeltaTime : 1.0f / 60.0f;
		g_alpha += ((want ? 1.0f : 0.0f) - g_alpha) * std::min(1.0f, dt * 10.0f);
		if (g_alpha < 0.02f) return;

		auto now = std::chrono::steady_clock::now();
		if (now >= g_nextRefresh) {
			Refresh();
			g_nextRefresh = now + std::chrono::milliseconds(200);
		}
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) return;

		float scale = io.DisplaySize.y / 1080.0f;
		float sz = g_bar.iconSize * scale, gap = g_bar.gap * scale, labelW = 44.0f * scale;
		int held = SpellChords::HeldModifier();

		// rows to show, bottom to top
		std::vector<int> rows;
		if (g_bar.layout == 1) {
			rows.push_back(held >= 0 ? held + 1 : 0);
		} else {
			rows.push_back(0);
			for (int r = 1; r < 4; r++) {
				bool any = false;
				for (auto& s : g_rows[r]) any |= s.form != nullptr;
				if (any || !g_bar.hideEmptyRows || held == r - 1) rows.push_back(r);
			}
		}
		float nameH = g_bar.showNames ? 14.0f * scale : 0.0f;
		float rowH = sz + gap + nameH;
		float totalW = labelW + 4 * sz + 3 * gap;
		float x0 = io.DisplaySize.x * g_bar.posX - totalW * 0.5f * static_cast<float>(g_bar.anchor);
		float yEdge = io.DisplaySize.y * g_bar.posY;

		auto dl = GetForegroundDrawList();
		auto font = GetFont();
		float fs = GetFontSize();
		float magicka = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
		float voice = player->GetVoiceRecoveryTime();

		for (std::size_t i = 0; i < rows.size(); i++) {
			int r = rows[i];
			// bottom anchor: the 1-4 row at the bottom, modifier rows above; top anchor: the 1-4 row at the top, rows below
			float y = g_bar.anchorY == 1 ? yEdge + i * rowH : yEdge - (i + 1) * rowH;
			bool active = g_bar.layout == 1 || (held < 0 ? r == 0 : r == held + 1);
			float a = g_alpha * g_bar.opacity * (active ? 1.0f : g_bar.dimInactive);
			auto col = [&](ImU32 c, float mul = 1.0f) {
				int al = static_cast<int>(((c >> IM_COL32_A_SHIFT) & 0xFF) * a * mul);
				return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(al, 0, 255)) << IM_COL32_A_SHIFT);
			};
			// row label
			const char* label = r == 0 ? "1-4" : SpellChords::ModifierName(r - 1);
			ImVec2 ls = CalcTextSize(label, nullptr, false, -1.0f);
			ImDrawListManager::AddText(dl, font, fs * scale, ImVec2(x0 + labelW - ls.x * scale - 8 * scale, y + (sz - fs * scale) / 2),
				col(active ? IM_COL32(255, 230, 160, 255) : IM_COL32(220, 220, 220, 255)), label);

			for (int n = 0; n < 4; n++) {
				auto& s = g_rows[r][n];
				ImVec2 p0(x0 + labelW + n * (sz + gap), y), p1(p0.x + sz, p0.y + sz);
				if (!s.form && !g_bar.showEmpty) continue;
				float rounding = 6.0f * scale;
				ImDrawListManager::AddRectFilled(dl, p0, p1, col(IM_COL32(12, 12, 14, 200), s.form ? 1.0f : 0.5f), rounding, 0);
				if (s.form) {
					// icon
					float pad = sz * 0.18f;
					if (s.icon.svg) {
						if (auto t = Svg(s.icon.svg))
							ImDrawListManager::AddImage(dl, t, ImVec2(p0.x + pad, p0.y + pad), ImVec2(p1.x - pad, p1.y - pad), ImVec2(0, 0), ImVec2(1, 1), col(s.icon.color));
					} else {
						FontAwesome::PushSolid();
						auto fa = GetFont();
						float gsz = sz * 0.56f;
						auto g = Utf8(s.icon.glyph);
						ImVec2 gs = ImFontManger::CalcTextSizeA(fa, gsz, FLT_MAX, 0.0f, g.c_str(), nullptr, nullptr);
						ImDrawListManager::AddText(dl, fa, gsz, ImVec2(p0.x + (sz - gs.x) / 2, p0.y + (sz - gs.y) / 2), col(s.icon.color), g.c_str());
						FontAwesome::Pop();
					}
					// can't afford (spells) / cooldown (shouts) / none left (consumables)
					if (auto sp = s.form->As<RE::SpellItem>(); sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell &&
						sp->CalculateMagickaCost(player) > magicka)
						ImDrawListManager::AddRectFilled(dl, p0, p1, col(IM_COL32(160, 20, 20, 110)), rounding, 0);
					if (s.form->Is(RE::FormType::Shout) && voice > 0.05f) {
						ImDrawListManager::AddRectFilled(dl, p0, p1, col(IM_COL32(0, 0, 0, 150)), rounding, 0);
						auto t = fmt::format("{:.0f}", std::ceil(voice));
						ImVec2 ts = CalcTextSize(t.c_str(), nullptr, false, -1.0f);
						float tsz = fs * scale * 1.3f;
						ImDrawListManager::AddText(dl, font, tsz, ImVec2(p0.x + (sz - ts.x * 1.3f * scale) / 2, p0.y + (sz - tsz) / 2), col(IM_COL32(255, 255, 255, 255)), t.c_str());
					}
					if (s.count == 0)
						ImDrawListManager::AddRectFilled(dl, p0, p1, col(IM_COL32(0, 0, 0, 140)), rounding, 0);
					if (s.count >= 0) {
						auto t = "x" + std::to_string(s.count);  // "x12"
						ImVec2 ts = CalcTextSize(t.c_str(), nullptr, false, -1.0f);
						ImDrawListManager::AddText(dl, font, fs * scale * 0.85f, ImVec2(p1.x - ts.x * 0.85f * scale - 3 * scale, p1.y - fs * scale * 0.85f - 2 * scale), col(IM_COL32(255, 255, 255, 255)), t.c_str());
					}
				}
				// frame: gold when equipped
				bool eq = s.form && Equipped(player, s.form);
				ImDrawListManager::AddRect(dl, p0, p1, col(eq ? IM_COL32(255, 205, 90, 255) : IM_COL32(255, 255, 255, s.form ? 70 : 30)), rounding, 0, (eq ? 2.2f : 1.0f) * scale);
				// key number, top left
				auto k = std::to_string(n + 1);
				ImDrawListManager::AddText(dl, font, fs * scale * 0.8f, ImVec2(p0.x + 3 * scale, p0.y + 1 * scale), col(IM_COL32(255, 255, 255, 200)), k.c_str());
				if (g_bar.showNames && s.form) {
					std::string nm = s.name.size() > 9 ? s.name.substr(0, 8) + "." : s.name;
					ImVec2 ts = CalcTextSize(nm.c_str(), nullptr, false, -1.0f);
					float nsz = fs * scale * 0.72f;
					ImDrawListManager::AddText(dl, font, nsz, ImVec2(p0.x + (sz - ts.x * 0.72f * scale) / 2, p1.y + 1 * scale), col(IM_COL32(230, 230, 230, 255)), nm.c_str());
				}
			}
		}
	}

	// ---------- F1 settings page ----------
	void __stdcall SettingsPage()
	{
		bool changed = false;
		changed |= Checkbox("Show the spell bar", &g_bar.enabled);
		const char* layouts[] = { "All rows (plain + modifier rows)", "One row, pages to the held modifier" };
		changed |= Combo("Layout", &g_bar.layout, layouts, 2, -1);
		const char* when[] = { "Always", "Weapon/magic drawn or in combat", "In combat only" };
		changed |= Combo("Show when", &g_bar.showWhen, when, 3, -1);
		changed |= SliderFloat("Icon size", &g_bar.iconSize, 24.0f, 96.0f, "%.0f px", 0);
		changed |= SliderFloat("Gap", &g_bar.gap, 0.0f, 20.0f, "%.0f px", 0);
		const char* presets[kPresetCount + 1];
		for (int i = 0; i < kPresetCount; i++) presets[i] = kPresets[i].name;
		presets[kPresetCount] = "Custom (sliders below)";
		int preset = CurrentPreset();
		int shown = preset < 0 ? kPresetCount : preset;
		if (Combo("Position", &shown, presets, kPresetCount + 1, -1) && shown < kPresetCount) {
			ApplyPreset(shown);
			changed = true;
		}
		const char* anchors[] = { "Left edge", "Centre", "Right edge" };
		changed |= Combo("Position X places the bar's", &g_bar.anchor, anchors, 3, -1);
		changed |= SliderFloat("Position X", &g_bar.posX, 0.0f, 1.0f, "%.3f", 0);
		const char* anchorsY[] = { "Bottom edge (rows stack upward)", "Top edge (rows stack downward)" };
		changed |= Combo("Position Y places the bar's", &g_bar.anchorY, anchorsY, 2, -1);
		changed |= SliderFloat("Position Y", &g_bar.posY, 0.0f, 1.0f, "%.3f", 0);
		changed |= SliderFloat("Opacity", &g_bar.opacity, 0.1f, 1.0f, "%.2f", 0);
		changed |= SliderFloat("Inactive rows", &g_bar.dimInactive, 0.0f, 1.0f, "%.2f", 0);
		changed |= Checkbox("Show names under icons", &g_bar.showNames);
		changed |= Checkbox("Show empty slots", &g_bar.showEmpty);
		changed |= Checkbox("Hide modifier rows without slots", &g_bar.hideEmptyRows);
		if (changed) g_nextRefresh = {};
		if (Button("Save to SpellChords.ini", ImVec2(0, 0))) SaveBarSettings();
		SameLine(0.0f, -1.0f);
		if (Button("Reload from ini", ImVec2(0, 0))) LoadBarSettings();
		Text("Assign chords in the Favorites menu: highlight an entry, press Ctrl/Shift/Alt + 1-4.");
	}
}

void SpellBar::Install()
{
	LoadBarSettings();
	if (!SKSEMenuFramework::IsInstalled()) {
		SKSE::log::warn("SKSE Menu Framework not found: no spell bar");
		return;
	}
	auto api = SKSEMenuFramework::GetMenuFrameworkAPIVersion();
	SKSE::log::info("spell bar: SKSE Menu Framework API {}", api);
	SKSEMenuFramework::SetSection("Spell Chords");
	SKSEMenuFramework::AddSectionItem("Spell Bar", SettingsPage);
	SKSEMenuFramework::AddHudElement(Draw);
	g_barAvailable = true;
	SKSE::log::info("spell bar: HUD element registered (enabled {}, layout {}, show when {})", g_bar.enabled, g_bar.layout, g_bar.showWhen);
}

// ---------- Papyrus natives for the MCM (script SpellChords, all global) ----------
namespace
{
	std::int32_t GetBarPosition(RE::StaticFunctionTag*) { return CurrentPreset(); }

	void SetBarPosition(RE::StaticFunctionTag*, std::int32_t a_index)
	{
		if (a_index < 0 || a_index >= kPresetCount) return;
		ApplyPreset(a_index);
		g_nextRefresh = {};
		SaveBarSettings();
	}

	bool BarAvailable(RE::StaticFunctionTag*) { return g_barAvailable; }
}

bool SpellBar::RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
{
	a_vm->RegisterFunction("GetBarPosition", "SpellChords", GetBarPosition, true);
	a_vm->RegisterFunction("SetBarPosition", "SpellChords", SetBarPosition);
	a_vm->RegisterFunction("BarAvailable", "SpellChords", BarAvailable, true);
	SKSE::log::info("Papyrus functions registered (SpellChords.GetBarPosition/SetBarPosition/BarAvailable)");
	return true;
}

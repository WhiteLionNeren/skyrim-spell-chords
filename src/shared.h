#pragma once
// What the spell bar (bar.cpp) reads from the chord code (main.cpp).
#include <RE/Skyrim.h>

namespace SpellChords
{
	RE::FormID Slot(int a_mod, int a_num);  // chord slot form (0 = empty); a_mod 0..2 = Ctrl/Shift/Alt, a_num 0..3 = keys 1-4
	int HeldModifier();                      // chord modifier physically held right now: -1 none, 0..2
	const char* ModifierName(int a_mod);     // "Ctrl" / "Shift" / "Alt"
	// Absolute path under the game's Data folder (from the exe's location, not the working directory: some plugins,
	// e.g. SkyrimNet, change the working directory while they load).
	std::string DataPath(std::string_view a_rel);
}

namespace SpellBar
{
	void Install();  // registers the HUD element and the F1 settings page (needs SKSE Menu Framework 3)
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);  // natives of script SpellChords, for the MCM
}

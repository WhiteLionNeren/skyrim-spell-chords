Scriptname SpellChordsMCM extends SKI_ConfigBase
{Spell Chords MCM: chords on/off, how long a modifier waits for a number, the assign/clear notice, and the spell bar's
position. The values live in SpellChords.dll and its ini (see SpellChords.psc), not in the save.}

int oEnabled
int oHoldMs
int oNotify
int oPosition

Event OnConfigInit()
	ModName = "Spell Chords"
	Pages = new string[1]
	Pages[0] = "Settings"
EndEvent

string[] Function PositionNames()
	string[] n = new string[6]
	n[0] = "Bottom left"
	n[1] = "Bottom center"
	n[2] = "Bottom right"
	n[3] = "Top left"
	n[4] = "Top center"
	n[5] = "Top right"
	return n
EndFunction

string Function PositionName(int a_index)
	If a_index < 0
		return "Custom"
	EndIf
	return PositionNames()[a_index]
EndFunction

Event OnPageReset(string a_page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	AddHeaderOption("Chords", 0)
	oEnabled = AddToggleOption("Chords enabled", SpellChords.GetChordsEnabled(), 0)
	oHoldMs = AddSliderOption("Modifier waits for a number", SpellChords.GetHoldMs() as float, "{0} ms", 0)
	oNotify = AddToggleOption("Notice when a slot changes", SpellChords.GetNotify(), 0)
	AddEmptyOption()
	AddTextOption("Assign in the Favorites menu: highlight", "", OPTION_FLAG_DISABLED)
	AddTextOption("an entry, press Ctrl/Shift/Alt + 1-4.", "", OPTION_FLAG_DISABLED)

	SetCursorPosition(1)
	AddHeaderOption("Spell bar", 0)
	If SpellChords.BarAvailable()
		oPosition = AddMenuOption("Position", PositionName(SpellChords.GetBarPosition()), 0)
		AddTextOption("More bar settings: F1 > Spell Chords.", "", OPTION_FLAG_DISABLED)
	Else
		oPosition = AddMenuOption("Position", PositionName(SpellChords.GetBarPosition()), OPTION_FLAG_DISABLED)
		AddTextOption("The bar needs SKSE Menu Framework 3.", "", OPTION_FLAG_DISABLED)
	EndIf
EndEvent

Event OnOptionSelect(int a_option)
	If a_option == oEnabled
		bool on = !SpellChords.GetChordsEnabled()
		SpellChords.SetChordsEnabled(on)
		SetToggleOptionValue(oEnabled, on, false)
	ElseIf a_option == oNotify
		bool on = !SpellChords.GetNotify()
		SpellChords.SetNotify(on)
		SetToggleOptionValue(oNotify, on, false)
	EndIf
EndEvent

Event OnOptionSliderOpen(int a_option)
	If a_option == oHoldMs
		SetSliderDialogStartValue(SpellChords.GetHoldMs() as float)
		SetSliderDialogDefaultValue(150.0)
		SetSliderDialogRange(50.0, 500.0)
		SetSliderDialogInterval(10.0)
	EndIf
EndEvent

Event OnOptionSliderAccept(int a_option, float a_value)
	If a_option == oHoldMs
		SpellChords.SetHoldMs(a_value as int)
		SetSliderOptionValue(oHoldMs, a_value, "{0} ms", false)
	EndIf
EndEvent

Event OnOptionMenuOpen(int a_option)
	If a_option == oPosition
		int cur = SpellChords.GetBarPosition()
		If cur < 0
			cur = 0
		EndIf
		SetMenuDialogOptions(PositionNames())
		SetMenuDialogStartIndex(cur)
		SetMenuDialogDefaultIndex(0)
	EndIf
EndEvent

Event OnOptionMenuAccept(int a_option, int a_index)
	If a_option == oPosition && a_index >= 0
		SpellChords.SetBarPosition(a_index)
		SetMenuOptionValue(oPosition, PositionName(a_index), false)
	EndIf
EndEvent

Event OnOptionDefault(int a_option)
	If a_option == oEnabled
		SpellChords.SetChordsEnabled(true)
		SetToggleOptionValue(oEnabled, true, false)
	ElseIf a_option == oHoldMs
		SpellChords.SetHoldMs(150)
		SetSliderOptionValue(oHoldMs, 150.0, "{0} ms", false)
	ElseIf a_option == oNotify
		SpellChords.SetNotify(true)
		SetToggleOptionValue(oNotify, true, false)
	ElseIf a_option == oPosition
		SpellChords.SetBarPosition(0)
		SetMenuOptionValue(oPosition, PositionName(0), false)
	EndIf
EndEvent

Event OnOptionHighlight(int a_option)
	If a_option == oEnabled
		SetInfoText("Turns the Ctrl/Shift/Alt + 1-4 chords on or off. Off: those keys behave exactly as without the mod. Default: on.")
	ElseIf a_option == oHoldMs
		SetInfoText("When you press Ctrl, Shift or Alt, the mod waits this long for a number key before passing the press to the game (so a held run or sprint starts this much later). Shorter feels snappier; longer gives you more time to press the number. Default: 150 ms.")
	ElseIf a_option == oNotify
		SetInfoText("Shows a notice when you assign or clear a chord slot in the Favorites menu. Default: on.")
	ElseIf a_option == oPosition
		SetInfoText("Where the spell bar sits on screen. Default: bottom left. \"Custom\" means it was moved with the sliders in F1 > Spell Chords > Spell Bar.")
	EndIf
EndEvent

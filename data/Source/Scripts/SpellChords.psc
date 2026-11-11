Scriptname SpellChords Hidden
{Spell Chords: settings functions implemented in SpellChords.dll. Every Set* also writes SKSE/Plugins/SpellChords.ini.}

; Chords on (true) or off.
bool Function GetChordsEnabled() global native
Function SetChordsEnabled(bool a_on) global native

; How long (ms) Ctrl/Shift/Alt waits for a number key before the game gets the press. 50-1000.
int Function GetHoldMs() global native
Function SetHoldMs(int a_ms) global native

; Notice when a chord slot is assigned or cleared.
bool Function GetNotify() global native
Function SetNotify(bool a_on) global native

; Spell bar position preset: 0 bottom left, 1 bottom center, 2 bottom right, 3 top left, 4 top center, 5 top right.
; GetBarPosition returns -1 when the position was set by hand (F1 sliders or the ini).
int Function GetBarPosition() global native
Function SetBarPosition(int a_preset) global native

; True when SKSE Menu Framework 3 is installed, so the spell bar can be drawn.
bool Function BarAvailable() global native

#pragma once

// Control identifiers for the configuration utility's three dialogs.
//
// The numbers matter to nothing but the resource file and the code that reads
// it; what matters here is that every control that carries information has one,
// so it can be labelled, read and reached from the keyboard.

#define IDI_APP 100

#define IDD_MAIN 200
#define IDD_VOICE 201
#define IDD_ENGINE 202

// --- the main window -------------------------------------------------------
#define IDC_VOICE_LIST 1001
#define IDC_NEW 1002
#define IDC_CHANGE 1003
#define IDC_DUPLICATE 1004
#define IDC_RENAME 1005
#define IDC_DELETE 1006
#define IDC_PREVIEW 1007
#define IDC_STOP 1008
#define IDC_DETAILS 1009
#define IDC_SCOPE 1010
#define IDC_INIPATH 1011
#define IDC_STATUS 1012
#define IDC_ENGINE_SETTINGS 1013
#define IDC_PUBLISH 1014
#define IDC_SAVE 1015
#define IDC_HELP_BUTTON 1016
#define IDC_CLOSE 1017

// --- the voice editor ------------------------------------------------------
#define IDC_V_NAME 1101
#define IDC_V_BASEDON 1102
#define IDC_V_LANGUAGE 1103
#define IDC_V_PITCH 1104
#define IDC_V_PITCH_SPIN 1105
#define IDC_V_PITCH_HZ 1106
#define IDC_V_DYNAMIC 1107
#define IDC_V_DYNAMIC_SPIN 1108
#define IDC_V_ASPIRATION 1109
#define IDC_V_ASPIRATION_SPIN 1110
#define IDC_V_FORMANT 1111
#define IDC_V_GENDER 1112
#define IDC_V_AGE 1113
#define IDC_V_AGE_SPIN 1114
#define IDC_V_SPEAKER_NAME 1115
#define IDC_V_SPEAKER_STYLE 1116
#define IDC_V_LANGUAGE_FILE 1117
#define IDC_V_LANGUAGE_ID 1118
#define IDC_V_LCID 1119
#define IDC_V_LIBRARY_FILE 1120
#define IDC_V_PHSYM_FILE 1121
#define IDC_V_DIPHONE_FILE 1122
#define IDC_V_MAPPING_FILE 1123
#define IDC_V_PREVIEW 1124
#define IDC_V_HINT 1125

// --- engine settings -------------------------------------------------------
#define IDC_E_TRIM 1201
#define IDC_E_THRESHOLD 1202
#define IDC_E_THRESHOLD_SPIN 1203
#define IDC_E_WORDS 1204
#define IDC_E_SENTENCES 1205
#define IDC_E_TIMEOUT 1206
#define IDC_E_TIMEOUT_PER_CHAR 1207
#define IDC_E_LOGLEVEL 1208
#define IDC_E_RATE_MIN 1209
#define IDC_E_RATE_MAX 1210
#define IDC_E_RATE_DEFAULT 1211
#define IDC_E_PITCH_MIN 1212
#define IDC_E_PITCH_MAX 1213
#define IDC_E_PITCH_DEFAULT 1214
#define IDC_E_PREVIEW_TEXT 1215
#define IDC_E_DEFAULTS 1216
#define IDC_E_HINT 1217

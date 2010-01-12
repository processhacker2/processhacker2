#ifndef SETTINGS_H
#define SETTINGS_H

#include <ph.h>

typedef enum _PH_SETTING_TYPE
{
    StringSettingType,
    IntegerSettingType,
    IntegerPairSettingType
} PH_SETTING_TYPE, PPH_SETTING_TYPE;

typedef struct _PH_INTEGER_PAIR
{
    LONG X;
    LONG Y;
} PH_INTEGER_PAIR, *PPH_INTEGER_PAIR;

typedef struct _PH_SETTING
{
    PH_SETTING_TYPE Type;
    PPH_STRING Name;
    PPH_STRING DefaultValue;

    PVOID Value;
} PH_SETTING, *PPH_SETTING;

VOID PhSettingsInitialization();

BOOLEAN PhLoadSettings(
    __in PWSTR FileName
    );

#endif

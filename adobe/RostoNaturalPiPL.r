#include "AEConfig.h"
#include "AE_EffectVers.h"
#ifndef AE_OS_WIN
#include "AE_General.r"
#endif
#include "AE_Effect.h"
#include "AE_GeneralPlug.h"

resource 'PiPL' (16000) {
  {
    Kind { AEEffect },
    Name { "Rosto Natural" },
    Category { "Lucas" },
    CodeMacARM64 { "EffectMain" },
    AE_PiPL_Version { 2, 0 },
    AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
    AE_Effect_Version { 0x000B0000 },
    AE_Effect_Info_Flags { 0 },
    AE_Effect_Global_OutFlags { 0 },
    AE_Effect_Global_OutFlags_2 { 0 },
    AE_Effect_Match_Name { "com.lucas.RostoNatural" },
    AE_Reserved_Info { 0 }
  }
};

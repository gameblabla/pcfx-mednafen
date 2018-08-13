// This file should only be included *ONCE* from drivers/input.cpp!!!


#define GPZ()   {MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ(), MKZ()}
static const ButtConfig PCFXPadConfig[2][15]=
{
        /* Gamepad 1 */
        {
			MK(LCTRL), MK(LALT), MK(LSHIFT), MK(SPACE), MK(KP5), MK(KP6), MK(ESCAPE), MK(RETURN),
			MK(UP), MK(RIGHT), MK(DOWN), MK(LEFT),
			MK(TAB), MK(BACKSPACE),
        },

        /* Gamepad 2 */
	GPZ(),
};
#undef GPZ


static const ButtConfig PCFXMouseConfig[2] =
{
 { BUTTC_MOUSE, 0, 0, 0 },
 { BUTTC_MOUSE, 0, 2, 0 },
};


typedef struct
{
 const char *base_name;
 const ButtConfig *bc;
 int num;
} DefaultSettingsMeow;

static const DefaultSettingsMeow defset[] =
{
 { "pcfx.input.port1.gamepad", PCFXPadConfig[0], sizeof(PCFXPadConfig[0]) / sizeof(ButtConfig)  },
 { "pcfx.input.port2.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port3.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port4.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port5.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port6.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port7.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },
 { "pcfx.input.port8.gamepad", PCFXPadConfig[1], sizeof(PCFXPadConfig[1]) / sizeof(ButtConfig)  },


 { "pcfx.input.port1.mouse", PCFXMouseConfig, sizeof(PCFXMouseConfig) / sizeof(ButtConfig) },
 { "pcfx.input.port2.mouse", PCFXMouseConfig, sizeof(PCFXMouseConfig) / sizeof(ButtConfig) },
};


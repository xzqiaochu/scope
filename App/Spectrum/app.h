#ifndef __SPECTRUM_INTERFACE_H__
#define __SPECTRUM_INTERFACE_H__

#include "os.h"
#include "settings.h"
#include "global.h"

#include "sample.h"
#include "UI.h"
#include "operate.h"

void Spectrum_Init(void);
void Spectrum_DeInit(void);
uint8_t Spectrum_Loop(void);

#endif

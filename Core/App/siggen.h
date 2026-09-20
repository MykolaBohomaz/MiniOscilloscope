/**
 * @file    siggen.h
 * @brief   Built-in test signal on PA4 (DAC1 OUT1, TIM7-paced, DMA2 Ch4).
 *
 * Jumper PA4 -> PA0 to test acquisition, trigger, measurements and the PC
 * protocol end-to-end without any analog front end. The buffered DAC output
 * does not reach the rails and is slew-limited: it is a functional test
 * source, NOT a bandwidth or accuracy reference.
 */
#ifndef SIGGEN_H
#define SIGGEN_H

#include "scope_settings.h"

#define SIGGEN_POINTS   100u      /* samples per period                      */
#define SIGGEN_LO_MV    400u      /* keep clear of the DAC buffer's rails    */
#define SIGGEN_HI_MV    2900u

void siggen_apply(gen_wave_t wave, uint8_t freq_index, float vdda_mv);

#endif /* SIGGEN_H */

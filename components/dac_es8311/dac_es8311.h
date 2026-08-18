#pragma once

#include "dac.h"

/**
 * ES8311 DAC driver ops — register with dac_register() before calling
 * dac_init().
 */
extern const dac_ops_t dac_es8311_ops;

/**
 * Debug: hex-dump the sound-deciding registers ("01=3F 02=00 ...") into out.
 * Read-only and safe any time; returns chars written. For the field flight
 * recorder at /api/debug/audio.
 */
int dac_es8311_reg_dump(char *out, size_t cap);

/* Full codec soft-reset + register reprogram (the register-level power
 * cycle). Run before a mic capture: clears the wedged analog-input state
 * that records as full-scale hiss while registers read back healthy
 * (W-068). Returns 0 on success, -1 if the codec isn't up. */
int dac_es8311_capture_reset(void);

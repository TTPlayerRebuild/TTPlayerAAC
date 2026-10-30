/* TTPlayer FAAD2 adaptation, 2026-10-30. GPL-2.0-or-later. */
#ifndef TTP_FAAD_COUPLING_H
#define TTP_FAAD_COUPLING_H
#include "common.h"
#include "structs.h"

typedef struct ttp_cce_target {
    uint8_t pair, tag, select, gain;
} ttp_cce_target;

typedef struct ttp_cce_output {
    real_t overlap[1024], pcm[1024];
    uint8_t previous_shape, ready;
} ttp_cce_output;

typedef struct ttp_cce_frame {
    NeAACDecStruct *decoder;
    ttp_cce_output *output[MAX_CHANNELS];
    element element;
    int16_t quantized[1024];
    real_t spectrum[1024];
    real_t gain[16][8][51];
    ttp_cce_target target[8];
    uint8_t present, point, targets, lists;
} ttp_cce_frame;

typedef struct ttp_cce_element {
    element element;
    int16_t quantized[2][1024];
    drc_info drc;
    uint8_t type, index;
} ttp_cce_element;

typedef struct ttp_cce_context {
    ttp_cce_element *elements[MAX_SYNTAX_ELEMENTS];
    ttp_cce_frame *sources[16];
    uint8_t count, rendering;
} ttp_cce_context;

uint8_t ttp_cce_begin(NeAACDecStruct *decoder);
void ttp_cce_clear(NeAACDecStruct *decoder);
uint8_t ttp_cce_queue_single(NeAACDecStruct *, element *, int16_t *);
uint8_t ttp_cce_queue_pair(NeAACDecStruct *, element *, int16_t *, int16_t *);
uint8_t ttp_cce_finish(NeAACDecStruct *);
uint8_t ttp_cce_spectral(NeAACDecStruct *, ic_stream *, uint8_t, uint8_t, real_t *);
uint8_t ttp_cce_time(NeAACDecStruct *, uint8_t, real_t *);
uint8_t ttp_cce_capture(NeAACDecStruct *, const real_t *);
ttp_cce_frame *ttp_cce_new(NeAACDecStruct *, uint8_t);
#endif

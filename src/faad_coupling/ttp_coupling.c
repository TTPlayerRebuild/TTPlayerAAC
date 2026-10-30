/* TTPlayer FAAD2 adaptation, 2026-10-30. GPL-2.0-or-later.
 * Frame staging and CCE reconstruction for AAC Main/LC/LTP. Public ABI unchanged.
 * Coupling syntax and stages follow ISO/IEC 14496-3, subpart 4.
 */
#include "ttp_coupling.h"
#include "specrec.h"
#include "filtbank.h"
#include "drc.h"
#include <float.h>

static uint8_t ttp_cce_enabled(const NeAACDecStruct *d)
{
    return d->object_type == MAIN || d->object_type == LC || d->object_type == LTP;
}

void ttp_cce_clear(NeAACDecStruct *d)
{
    unsigned i, j;
    ttp_cce_context *c = d->ttp_coupling;
    d->ttp_coupling = NULL;
    if (!c) return;
    for (i = 0; i < 16; ++i) if (c->sources[i]) {
        if (c->sources[i]->decoder) NeAACDecClose(c->sources[i]->decoder);
        for (j = 0; j < MAX_CHANNELS; ++j)
            if (c->sources[i]->output[j]) faad_free(c->sources[i]->output[j]);
        faad_free(c->sources[i]);
    }
    for (i = 0; i < MAX_SYNTAX_ELEMENTS; ++i)
        if (c->elements[i]) faad_free(c->elements[i]);
    faad_free(c);
}

uint8_t ttp_cce_begin(NeAACDecStruct *d)
{
    unsigned i, j;
    ttp_cce_context *c;
    if (!ttp_cce_enabled(d)) return 0;
    if (d->frameLength != 1024 && d->frameLength != 960) return 32;
    c = d->ttp_coupling;
    if (!c) {
        c = (ttp_cce_context *)faad_malloc(sizeof(*c));
        if (!c) return 15;
        memset(c, 0, sizeof(*c));
        d->ttp_coupling = c;
    }
    c->count = c->rendering = 0;
    for (i = 0; i < 16; ++i)
        if (c->sources[i]) {
            c->sources[i]->present = 0;
            for (j = 0; j < MAX_CHANNELS; ++j)
                if (c->sources[i]->output[j]) c->sources[i]->output[j]->ready = 0;
        }
    return 0;
}

ttp_cce_frame *ttp_cce_new(NeAACDecStruct *d, uint8_t tag)
{
    ttp_cce_context *c = d->ttp_coupling;
    ttp_cce_frame *f;
    NeAACDecStruct *state;
    ttp_cce_output *output[MAX_CHANNELS];
    if (!c || tag >= 16) return NULL;
    f = c->sources[tag];
    if (f && f->present) return NULL;
    if (!f) {
        f = (ttp_cce_frame *)faad_malloc(sizeof(*f));
        if (!f) return NULL;
        memset(f, 0, sizeof(*f));
        c->sources[tag] = f;
    }
    state = f->decoder;
    memcpy(output, f->output, sizeof(output));
    memset(f, 0, sizeof(*f));
    f->decoder = state;
    memcpy(f->output, output, sizeof(output));
    f->present = 1;
    f->element.element_instance_tag = tag;
    f->element.paired_channel = -1;
    return f;
}

static ttp_cce_element *ttp_cce_append(NeAACDecStruct *d, element *e)
{
    unsigned i;
    ttp_cce_context *c = d->ttp_coupling;
    ttp_cce_element *p;
    uint8_t type = d->element_id[d->fr_ch_ele];
    if (!c || c->count >= MAX_SYNTAX_ELEMENTS) return NULL;
    for (i = 0; i < c->count; ++i)
        if (c->elements[i]->type == type &&
            c->elements[i]->element.element_instance_tag == e->element_instance_tag)
            return NULL;
    p = c->elements[c->count];
    if (!p) {
        p = (ttp_cce_element *)faad_malloc(sizeof(*p));
        if (!p) return NULL;
        c->elements[c->count] = p;
    }
    memset(p, 0, sizeof(*p));
    p->element = *e;
    p->drc = *d->drc;
    p->index = d->fr_ch_ele;
    p->type = type;
    ++c->count;
    return p;
}

uint8_t ttp_cce_queue_single(NeAACDecStruct *d, element *e, int16_t *q)
{
    ttp_cce_element *p;
    uint8_t channels = 1;
    if (!ttp_cce_enabled(d))
        return reconstruct_single_channel(d, &e->ics1, e, q);
    p = ttp_cce_append(d, e);
    if (!p) return 32;
    memcpy(p->quantized[0], q, d->frameLength * sizeof(*q));
#ifdef PS_DEC
    if (d->ps_used[d->fr_ch_ele]) channels = 2;
#endif
    /* decode_sce_lfe needs the output count before the delayed synthesis. */
    if (d->element_output_channels[d->fr_ch_ele] &&
        d->element_output_channels[d->fr_ch_ele] != channels)
        memset(d->element_alloced + d->fr_ch_ele, 0,
               MAX_SYNTAX_ELEMENTS - d->fr_ch_ele);
    d->element_output_channels[d->fr_ch_ele] = channels;
    return 0;
}

uint8_t ttp_cce_queue_pair(NeAACDecStruct *d, element *e, int16_t *a, int16_t *b)
{
    ttp_cce_element *p;
    if (!ttp_cce_enabled(d))
        return reconstruct_channel_pair(d, &e->ics1, &e->ics2, e, a, b);
    p = ttp_cce_append(d, e);
    if (!p) return 32;
    memcpy(p->quantized[0], a, d->frameLength * sizeof(*a));
    memcpy(p->quantized[1], b, d->frameLength * sizeof(*b));
    return 0;
}

static ttp_cce_element *ttp_cce_find(ttp_cce_context *c, const ttp_cce_target *t)
{
    unsigned i;
    uint8_t type = t->pair ? ID_CPE : ID_SCE;
    for (i = 0; i < c->count; ++i)
        if (c->elements[i]->type == type &&
            c->elements[i]->element.element_instance_tag == t->tag)
            return c->elements[i];
    return NULL;
}

static uint8_t ttp_cce_source(NeAACDecStruct *d, ttp_cce_frame *f)
{
    NeAACDecStruct *s = f->decoder;
    if (s && (s->object_type != d->object_type || s->sf_index != d->sf_index ||
              s->frameLength != d->frameLength)) {
        NeAACDecClose(s); s = f->decoder = NULL;
    }
    if (!s) {
        s = (NeAACDecStruct *)NeAACDecOpen();
        if (!s) return 15;
        f->decoder = s;
        s->object_type = d->object_type;
        s->sf_index = d->sf_index;
        s->frameLength = d->frameLength;
        s->fb = filter_bank_init(s->frameLength);
        if (!s->fb) { NeAACDecClose(s); f->decoder = NULL; return 15; }
        s->channelConfiguration = 1;
        s->element_id[0] = ID_SCE;
    }
    s->ttp_core_rate = d->ttp_core_rate;
    s->frame = d->frame;
    s->postSeekResetFlag = d->postSeekResetFlag;
    s->ttp_coupling_source = f;
    return reconstruct_single_channel(s, &f->element.ics1, &f->element, f->quantized);
}

uint8_t ttp_cce_finish(NeAACDecStruct *d)
{
    unsigned i, j;
    uint8_t result = 0, saved = d->fr_ch_ele;
    drc_info final_drc = *d->drc;
    ttp_cce_context *c = d->ttp_coupling;
    if (!ttp_cce_enabled(d) || !c) return 0;
    /* Validate every target before advancing any synthesis state. */
    for (i = 0; i < 16; ++i) if (c->sources[i] && c->sources[i]->present) {
        ttp_cce_frame *f = c->sources[i];
#ifdef SBR_DEC
        if (d->sbr_present_flag == 1 || d->forceUpSampling == 1) return 6;
#endif
        for (j = 0; j < f->targets; ++j) {
            ttp_cce_element *e = ttp_cce_find(c, &f->target[j]);
            if (!e) return 32;
            if (f->point != 3) {
                ic_stream *a = &f->element.ics1;
                ic_stream *b = &e->element.ics1;
                if (f->target[j].select != 1 &&
                    (a->window_sequence == EIGHT_SHORT_SEQUENCE) !=
                    (b->window_sequence == EIGHT_SHORT_SEQUENCE)) return 32;
                if (f->target[j].pair && f->target[j].select != 2 &&
                    (a->window_sequence == EIGHT_SHORT_SEQUENCE) !=
                    (e->element.ics2.window_sequence == EIGHT_SHORT_SEQUENCE)) return 32;
            }
        }
    }
    /* Coupling sources are decoded first; they are not playback channels. */
    for (i = 0; i < 16; ++i) if (c->sources[i] && c->sources[i]->present) {
        result = ttp_cce_source(d, c->sources[i]);
        if (result) return result;
    }
    c->rendering = 1;
    for (i = 0; i < c->count && !result; ++i) {
        ttp_cce_element *p = c->elements[i];
        element *e = &p->element;
        d->fr_ch_ele = p->index;
        *d->drc = p->drc;
        if (p->type == ID_CPE)
            result = reconstruct_channel_pair(d, &e->ics1, &e->ics2, e,
                                               p->quantized[0], p->quantized[1]);
        else
            result = reconstruct_single_channel(d, &e->ics1, e, p->quantized[0]);
    }
    d->fr_ch_ele = saved;
    *d->drc = final_drc;
    c->rendering = 0;
    return result;
}

uint8_t ttp_cce_capture(NeAACDecStruct *d, const real_t *spectrum)
{
    ttp_cce_frame *f = d->ttp_coupling_source;
    if (!f) return 0;
    memcpy(f->spectrum, spectrum, d->frameLength * sizeof(*spectrum));
    /* Independent sources use a separate overlap per target: its DRC exclusion
       and per-element metadata can differ from another target of this CCE.
       LTP must additionally finish the source's own unscaled synthesis and
       prediction-history update. Stopping here loses all later predictions. */
    return d->object_type != LTP;
}

static int ttp_cce_gain_index(const ttp_cce_target *t, const element *e, uint8_t channel)
{
    if (channel == e->channel && t->select != 1) return t->gain;
    if (t->pair && channel == e->paired_channel && t->select != 2)
        return t->gain + (t->select == 3);
    return -1;
}

static uint8_t ttp_cce_mix(real_t *dst, const real_t *src, real_t gain, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        real_t value = dst[i] + gain * src[i];
        if (value != value || value > FLT_MAX || value < -FLT_MAX) return 32;
        dst[i] = value;
    }
    return 0;
}

static uint8_t ttp_cce_render_output(NeAACDecStruct *d, ttp_cce_frame *f, uint8_t channel)
{
    ttp_cce_output *out = f->output[channel];
    if (!out) {
        out = (ttp_cce_output *)faad_malloc(sizeof(*out));
        if (!out) return 15;
        memset(out, 0, sizeof(*out));
        f->output[channel] = out;
    }
    if (!out->ready) {
        /* DRC's fixed 1024-bin table may also touch the unused tail of a
           960-sample frame, as in FAAD's original zeroed spectral buffers. */
        ALIGN real_t spectrum[1024] = {0};
        ic_stream *ics = &f->element.ics1;
        memcpy(spectrum, f->spectrum, d->frameLength * sizeof(real_t));
#if APPLY_DRC
        if (d->drc->present && (!d->drc->excluded_chns_present ||
            !d->drc->exclude_mask[channel])) drc_decode(d->drc, spectrum);
#endif
        ifilter_bank(f->decoder->fb, ics->window_sequence, ics->window_shape,
            out->previous_shape, spectrum, out->pcm, out->overlap,
            d->object_type, d->frameLength);
        out->previous_shape = ics->window_shape;
        out->ready = 1;
    }
    return 0;
}

uint8_t ttp_cce_spectral(NeAACDecStruct *d, ic_stream *target_ics,
                       uint8_t channel, uint8_t point, real_t *dst)
{
    unsigned n, t, g, b, w, start;
    uint8_t result;
    ttp_cce_context *c = d->ttp_coupling;
    (void)target_ics;
    if (!c || !c->rendering) return 0;
    for (n = 0; n < 16; ++n) {
        ttp_cce_frame *f = c->sources[n];
        ic_stream *ics;
        if (!f || !f->present || f->point != point) continue;
        ics = &f->element.ics1;
        for (t = 0; t < f->targets; ++t) {
            ttp_cce_element *e = ttp_cce_find(c, &f->target[t]);
            int index;
            if (!e) return 32;
            index = ttp_cce_gain_index(&f->target[t], &e->element, channel);
            if (index < 0) continue;
            start = 0;
            for (g = 0; g < ics->num_window_groups; ++g) {
                for (b = 0; b < ics->max_sfb; ++b) if (ics->sfb_cb[g][b] != ZERO_HCB) {
                    unsigned first = ics->swb_offset[b], last = ics->swb_offset[b + 1];
                    unsigned stride = ics->window_sequence == EIGHT_SHORT_SEQUENCE ? d->frameLength / 8 : d->frameLength;
                    if (first > last || last > stride) return 32;
                    for (w = 0; w < ics->window_group_length[g]; ++w) {
                        unsigned at = (start + w) * stride + first;
                        if (at + last - first > d->frameLength) return 32;
                        result = ttp_cce_mix(dst + at, f->spectrum + at,
                                            f->gain[index][g][b], last - first);
                        if (result) return result;
                    }
                }
                start += ics->window_group_length[g];
            }
        }
    }
    return 0;
}

uint8_t ttp_cce_time(NeAACDecStruct *d, uint8_t channel, real_t *dst)
{
    unsigned n, t;
    uint8_t result;
    ttp_cce_context *c = d->ttp_coupling;
    if (!c || !c->rendering) return 0;
    for (n = 0; n < 16; ++n) {
        ttp_cce_frame *f = c->sources[n];
        ttp_cce_output *out;
        if (!f || !f->present || f->point != 3) continue;
        /* Advance every playback channel's CCE overlap, even when this frame
           routes the CCE only to another channel. Routing may change next time. */
        if (channel >= MAX_CHANNELS || !f->decoder) return 32;
        result = ttp_cce_render_output(d, f, channel);
        if (result) return result;
        for (t = 0; t < f->targets; ++t) {
            ttp_cce_element *e = ttp_cce_find(c, &f->target[t]);
            int index;
            if (!e || !f->decoder || channel >= MAX_CHANNELS) return 32;
            index = ttp_cce_gain_index(&f->target[t], &e->element, channel);
            if (index < 0) continue;
            out = f->output[channel];
            result = ttp_cce_mix(dst, out->pcm,
                                f->gain[index][0][0], d->frameLength);
            if (result) return result;
        }
    }
    return 0;
}

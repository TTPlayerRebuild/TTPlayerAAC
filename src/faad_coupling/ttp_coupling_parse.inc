/* TTPlayer FAAD2 adaptation, 2026-10-30. GPL-2.0-or-later.
 * Included by syntax.c after its private parser declarations.
 * ISO/IEC 14496-3, coupling_channel_element / coupling_gain_element.
 */
static uint8_t ttp_cce_gain(uint8_t scale, int value, real_t *out)
{
    static const double steps[4] = {0.125, 0.25, 0.5, 1.0};
    double gain = pow(2.0, -steps[scale] * value);
    if (gain != gain || gain > FLT_MAX) return 4;
    *out = (real_t)gain;
    return 0;
}

static uint8_t ttp_parse_cce(NeAACDecStruct *d, bitfile *ld)
{
    unsigned i, j, g, b, lists = 0;
    uint8_t tag, independent, domain, sign, scale, result;
    ttp_cce_frame *f;
    ic_stream *ics;
    if (d->object_type != MAIN && d->object_type != LC && d->object_type != LTP) return 6;
    tag = (uint8_t)faad_getbits(ld, 4);
    f = ttp_cce_new(d, tag);
    if (!f) return 32;
    independent = faad_get1bit(ld);
    /* ISO/IEC 14496-3 4.6.7.4.3 forbids dependent CCE with LTP. */
    if (d->object_type == LTP && !independent) return 32;
    f->targets = 1 + (uint8_t)faad_getbits(ld, 3);
    for (i = 0; i < f->targets; ++i) {
        ttp_cce_target *t = &f->target[i];
        t->pair = faad_get1bit(ld);
        t->tag = (uint8_t)faad_getbits(ld, 4);
        /* A CCE may target a given SCE/CPE only once per frame. Distinct
           element namespaces may legitimately reuse the same numeric tag. */
        for (j = 0; j < i; ++j)
            if (f->target[j].pair == t->pair && f->target[j].tag == t->tag)
                return 32;
        t->select = t->pair ? (uint8_t)faad_getbits(ld, 2) : 2;
        t->gain = (uint8_t)lists;
        lists += 1 + (t->select == 3);
    }
    f->lists = (uint8_t)lists;
    domain = faad_get1bit(ld);
    f->point = independent ? 3 : domain;
    sign = faad_get1bit(ld);
    scale = (uint8_t)faad_getbits(ld, 2);
    ics = &f->element.ics1;
    result = individual_channel_stream(d, &f->element, ld, ics, 0, f->quantized);
    if (result) return result;
    if (ics->is_used || ics->max_sfb > 51 || ics->num_window_groups > 8) return 32;
    for (i = 0; i < lists; ++i) {
        int gain = 0;
        uint8_t common = 1;
        real_t cached = REAL_CONST(1.0);
        if (i) {
            common = independent ? 1 : faad_get1bit(ld);
            if (common) {
                gain = huffman_scale_factor(ld) - 60;
                if ((result = ttp_cce_gain(scale, gain, &cached)) != 0) return result;
            }
        }
        if (independent) {
            f->gain[i][0][0] = cached;
        } else for (g = 0; g < ics->num_window_groups; ++g) {
            for (b = 0; b < ics->max_sfb; ++b) if (ics->sfb_cb[g][b] != ZERO_HCB) {
                if (!common) {
                    int delta = huffman_scale_factor(ld) - 60;
                    if (delta) {
                        int exponent, negative = 0;
                        gain += delta;
                        exponent = gain;
                        if (sign) { negative = gain & 1; exponent >>= 1; }
                        if ((result = ttp_cce_gain(scale, exponent, &cached)) != 0) return result;
                        if (negative) cached = -cached;
                    }
                }
                f->gain[i][g][b] = cached;
            }
        }
    }
    if (ld->error || (uint64_t)faad_get_processed_bits(ld) > (uint64_t)ld->buffer_size * 8)
        return 14;
    return 0;
}

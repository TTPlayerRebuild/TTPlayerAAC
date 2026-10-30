/* TTPlayer SSR reconstruction. ISO/IEC 14496-3 section 4.6.12.
   GPL-2.0-or-later, same terms as the containing FAAD2 decoder.
   Gain equations independently checked against the MIT oxideav-aac
   reference (Karpeles Lab Inc., 2026); see retained third-party notice. */
#include "common.h"
#include "structs.h"
#ifdef SSR_DEC
#include "ssr.h"
#include "ssr_fb.h"
#include "ssr_ipqf.h"
#include "syntax.h"

/* The syntax sends up to seven level changes and no heap storage is needed.
   FMD interpolates exponents, rather than linearly interpolating amplitudes. */
uint8_t ttp_ssr_validate(const ssr_info *s, uint8_t seq)
{
    int b,w,i,windows;
    if (s->max_band>3 || seq>LONG_STOP_SEQUENCE) return 30;
    windows=seq==EIGHT_SHORT_SEQUENCE?8:seq==ONLY_LONG_SEQUENCE?1:2;
    for (b=1;b<=s->max_band;++b) for(w=0;w<windows;++w) {
        int limit=seq==EIGHT_SHORT_SEQUENCE?32:seq==ONLY_LONG_SEQUENCE?256:w==0?112:seq==LONG_START_SEQUENCE?32:256;
        int previous=-1, count=s->adjust_num[b][w];
        if(count>7)return 30;
        for(i=0;i<count;++i) {
            int loc=8*s->aloccode[b][w][i];
            if(loc<previous || loc>=limit || s->alevcode[b][w][i]>15)return 30;
            previous=loc;
        }
    }
    return 0;
}

static real_t fragment(const ssr_info *s, int band, int win, int count,
                       int length, real_t *out)
{
    int loc[9], lev[9], i, j, m;
    loc[0] = 0;
    for (i=1; i<=count; ++i) {
        loc[i] = 8*s->aloccode[band][win][i-1];
        lev[i] = (int)s->alevcode[band][win][i-1]-4;
    }
    lev[0] = count ? lev[1] : 0;
    loc[count+1] = length; lev[count+1] = 0;
    m = 0;
    for (j=0; j<length; ++j) {
        while (m<count && loc[m+1]<=j) ++m;
        i = j-loc[m];
        out[j] = (real_t)pow(2.0, i<8 ? ((8-i)*lev[m]+i*lev[m+1])/8.0 : (double)lev[m+1]);
    }
    return (real_t)ldexp(1.0,lev[0]);
}

static void gain(const ssr_info *s, int band, int seq, real_t *u, real_t *previous)
{
    real_t f[8][256], a[8];
    int w,j,windows = seq==EIGHT_SHORT_SEQUENCE ? 8 : seq==ONLY_LONG_SEQUENCE ? 1 : 2;
    if (band==0 || band>s->max_band) {
        for (j=0;j<256;++j) previous[j]=REAL_CONST(1);
        return;
    }
    for(w=0;w<windows;++w) {
        int length=seq==EIGHT_SHORT_SEQUENCE?32:seq==ONLY_LONG_SEQUENCE?256:w==0?112:seq==LONG_START_SEQUENCE?32:256;
        a[w]=fragment(s,band,w,s->adjust_num[band][w],length,f[w]);
    }
    if(seq==EIGHT_SHORT_SEQUENCE) {
        for(w=0;w<8;++w) {
            for(j=0;j<32;++j) {
                u[w*64+j] /= a[w]*(w?f[w-1][j]:previous[j]);
                u[w*64+32+j] /= f[w][j];
            }
        }
        memcpy(previous,f[7],32*sizeof(real_t));
    } else if(seq==ONLY_LONG_SEQUENCE) {
        for(j=0;j<256;++j) {u[j]/=a[0]*previous[j];u[256+j]/=f[0][j];}
        memcpy(previous,f[0],256*sizeof(real_t));
    } else if(seq==LONG_START_SEQUENCE) {
        for(j=0;j<256;++j)u[j]/=a[0]*a[1]*previous[j];
        for(j=0;j<112;++j)u[256+j]/=a[1]*f[0][j];
        for(j=0;j<32;++j)u[368+j]/=f[1][j];
        memcpy(previous,f[1],32*sizeof(real_t));
    } else {
        for(j=0;j<32;++j)u[112+j]/=a[0]*a[1]*previous[j];
        for(j=0;j<112;++j)u[144+j]/=a[1]*f[0][j];
        for(j=0;j<256;++j)u[256+j]/=f[1][j];
        memcpy(previous,f[1],256*sizeof(real_t));
    }
}

void ssr_decode(ssr_info *s, fb_info *fb, uint8_t seq,
                uint8_t shape, uint8_t previous_shape,
                real_t *spectrum, real_t *time, real_t *overlap,
                real_t history[4][24], real_t *prev_fmd, uint16_t frame_len)
{
    real_t input[256], u[512], v[1024];
    int b,w,j;
    for(b=0;b<4;++b) {
        real_t *tail=overlap+b*512;
        if(seq==EIGHT_SHORT_SEQUENCE) {
            for(w=0;w<8;++w)
                for(j=0;j<32;++j)input[w*32+j]=spectrum[w*128+b*32+((b&1)?31-j:j)];
        } else {
            for(j=0;j<256;++j)input[j]=spectrum[b*256+((b&1)?255-j:j)];
        }
        ssr_ifilter_bank(fb,seq,shape,previous_shape,input,u,256);
        gain(s,b,seq,u,prev_fmd+b*512);
        /* Keep a constant 256 band samples per host frame. The flat 112
           samples emitted early by START remain in the overlap queue until
           the next frame. This equals the variable START/STOP fragment
           concatenation in the standard without changing AAC timestamps. */
        if(seq==EIGHT_SHORT_SEQUENCE) {
            /* SSR overlaps only the previous short fragment. In the
               conformance random-sequence case the preceding long tail
               beyond 112+32 must not leak into the following windows. */
            for(j=144;j<256;++j)tail[j]=0;
            for(w=0;w<8;++w)for(j=0;j<64;++j)tail[112+32*w+j]+=u[64*w+j];
        } else {
            for(j=0;j<512;++j)tail[j]+=u[j];
        }
        for(j=0;j<256;++j){v[b*256+j]=tail[j];tail[j]=tail[j+256];tail[j+256]=0;}
    }
    ssr_ipqf(s,v,time,history,1024,4);
}
#endif

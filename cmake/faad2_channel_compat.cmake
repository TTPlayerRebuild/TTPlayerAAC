# TTPlayer Rebuild, 2026-10-30: activate ADTS channel_configuration=0 PCEs.
# MPEG-4 ASC already defines its layout; in-band PCEs must not overwrite it.
faad_compat_replace("libfaad/syntax.c" [=[                program_config_element(pce, ld);
                //if ((hInfo->error = program_config_element(pce, ld)) > 0)
                //    return;
                //hDecoder->pce_set = 1;]=] [=[                {
                    program_config parsed;
                    hInfo->error = program_config_element(&parsed, ld);
                    if (hInfo->error) return;
                    if (hDecoder->adts_header_present && !hDecoder->channelConfiguration) {
                        if (!parsed.channels || parsed.channels > MAX_CHANNELS ||
                            parsed.object_type + 1 != hDecoder->object_type ||
                            parsed.sf_index != hDecoder->sf_index) {
                            hInfo->error = 22;
                            return;
                        }
                        *pce = parsed;
                        hDecoder->pce_set = 1;
                    }
                }]=])

# SCE and LFE have separate element-tag namespaces. An LFE with tag zero
# must not overwrite the front-centre SCE zero in a PCE channel map.
faad_compat_replace("libfaad/structs.h" "    uint8_t sce_channel[16];"
  "    uint8_t lfe_channel[16];\n    uint8_t sce_channel[16];")
faad_compat_replace("libfaad/syntax.c"
  "pce->sce_channel[pce->lfe_element_tag_select[i]] = pce->channels;"
  "pce->lfe_channel[pce->lfe_element_tag_select[i]] = pce->channels;")
faad_compat_replace("libfaad/syntax.c"
  "hDecoder->internal_channel[hDecoder->pce.sce_channel[tag]] = channels;"
  [=[hDecoder->internal_channel[id_syn_ele == ID_LFE ?
                hDecoder->pce.lfe_channel[tag] : hDecoder->pce.sce_channel[tag]] = channels;]=])

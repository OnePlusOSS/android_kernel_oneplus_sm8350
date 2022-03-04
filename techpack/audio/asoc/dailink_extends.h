/************************************************************************************
** File: -
**
** Copyright (C), 2020-2025, Oplus Holdings Corp., Ltd.
**
** Description:
**     Add for audio FE extends dailink
** Version: 1.0
** --------------------------- Revision History: --------------------------------
**               <author>                                <date>          <desc>
** Jianfeng.Qiu@MULTIMEDIA.AUDIODRIVER                 2020/08/03     Add this file
**
************************************************************************************/

#ifndef __DAILINK_EXTENDS_H
#define __DAILINK_EXTENDS_H

SND_SOC_DAILINK_DEFS(pri_mi2s_tx_hostless,
	DAILINK_COMP_ARRAY(COMP_CPU("PRI_MI2S_TX_HOSTLESS")),
	DAILINK_COMP_ARRAY(COMP_CODEC("snd-soc-dummy", "snd-soc-dummy-dai")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("msm-pcm-hostless")));

SND_SOC_DAILINK_DEFS(tx4_cdcdma_hostless,
	DAILINK_COMP_ARRAY(COMP_CPU("TX4_CDC_DMA_HOSTLESS")),
	DAILINK_COMP_ARRAY(COMP_CODEC("snd-soc-dummy", "snd-soc-dummy-dai")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("msm-pcm-hostless")));

#define MI2S_TX_HOSTLESS_DAILINK(_name, _stream_name, _hostless_name) \
{                                                           \
	.name = _name,                                      \
	.stream_name = _stream_name,                        \
	.dynamic = 1,                                       \
	.dpcm_capture = 1,                                  \
	.trigger = {SND_SOC_DPCM_TRIGGER_POST,              \
			SND_SOC_DPCM_TRIGGER_POST},         \
	.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,           \
	.ignore_suspend = 1,                                \
	.ignore_pmdown_time = 1,                            \
	SND_SOC_DAILINK_REG(_hostless_name),              \
}                                                           \

#define TX_CDC_DMA_HOSTLESS_DAILINK(_name, _stream_name, _hostless_name) \
{                                                           \
	.name = _name,                                      \
	.stream_name = _stream_name,                        \
	.dynamic = 1,                                       \
	.dpcm_playback = 1,                                 \
	.dpcm_capture = 1,                                  \
	.trigger = {SND_SOC_DPCM_TRIGGER_POST,              \
			SND_SOC_DPCM_TRIGGER_POST},         \
	.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,           \
	.ignore_suspend = 1,                                \
	SND_SOC_DAILINK_REG(_hostless_name),                   \
}                                                           \

#endif /* __DAILINK_EXTENDS_H */

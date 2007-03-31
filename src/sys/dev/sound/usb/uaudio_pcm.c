/* $FreeBSD: src/sys/dev/sound/usb/uaudio_pcm.c,v 1.20 2007/01/26 19:14:41 ariff Exp $ */

/*-
 * Copyright (c) 2000-2002 Hiroyuki Aizu <aizu@navi.org>
 * Copyright (c) 2006 Hans Petter Selasky
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <sys/soundcard.h>
#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>

#include <dev/sound/usb/uaudio.h>

#include "mixer_if.h"

/************************************************************/
static void *
ua_chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	return uaudio_chan_init(devinfo, b, c, dir);
}

static int
ua_chan_free(kobj_t obj, void *data)
{
	return uaudio_chan_free(data);
}

static int
ua_chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	/*
	 * At this point, no need to query as we 
	 * shouldn't select an unsorted format
	 */
	return uaudio_chan_set_param_format(data,format);
}

static int
ua_chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	return uaudio_chan_set_param_speed(data, speed);
}

static int
ua_chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	return uaudio_chan_set_param_blocksize(data, blocksize);
}

static int
ua_chan_trigger(kobj_t obj, void *data, int go)
{
	if ((go == PCMTRIG_EMLDMAWR) || 
	    (go == PCMTRIG_EMLDMARD)) {
	    return 0;
	}

	if (go == PCMTRIG_START) {
	    return uaudio_chan_start(data);
	} else {
	    return uaudio_chan_stop(data);
	}
}

static int
ua_chan_getptr(kobj_t obj, void *data)
{
	return uaudio_chan_getptr(data);
}

static struct pcmchan_caps *
ua_chan_getcaps(kobj_t obj, void *data)
{
	return uaudio_chan_getcaps(data);
}

static kobj_method_t ua_chan_methods[] = {
	KOBJMETHOD(channel_init,		ua_chan_init),
	KOBJMETHOD(channel_free,                ua_chan_free),
	KOBJMETHOD(channel_setformat,		ua_chan_setformat),
	KOBJMETHOD(channel_setspeed,		ua_chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	ua_chan_setblocksize),
	KOBJMETHOD(channel_trigger,		ua_chan_trigger),
	KOBJMETHOD(channel_getptr,		ua_chan_getptr),
	KOBJMETHOD(channel_getcaps,		ua_chan_getcaps),
	{ 0, 0 }
};

CHANNEL_DECLARE(ua_chan);

/************************************************************/
static int
ua_mixer_init(struct snd_mixer *m)
{
	return uaudio_mixer_init_sub(mix_getdevinfo(m), m);
}

static int
ua_mixer_set(struct snd_mixer *m, unsigned type, unsigned left, unsigned right)
{
	uaudio_mixer_set(mix_getdevinfo(m), type, left, right);
	return (left | (right << 8));
}

static int
ua_mixer_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return uaudio_mixer_setrecsrc(mix_getdevinfo(m), src);
}

static int
ua_mixer_uninit(struct snd_mixer *m)
{
	return uaudio_mixer_uninit_sub(mix_getdevinfo(m));
}

static kobj_method_t ua_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		ua_mixer_init),
	KOBJMETHOD(mixer_uninit,	ua_mixer_uninit),
	KOBJMETHOD(mixer_set,		ua_mixer_set),
	KOBJMETHOD(mixer_setrecsrc,	ua_mixer_setrecsrc),

	{ 0, 0 }
};
MIXER_DECLARE(ua_mixer);
/************************************************************/


static int
ua_probe(device_t dev)
{
	struct sndcard_func *func;

	/* the parent device has already been probed */

	func = device_get_ivars(dev);

	if ((func == NULL) || 
	    (func->func != SCF_PCM)) {
	    return ENXIO;
	}

	device_set_desc(dev, "USB audio");

	return BUS_PROBE_DEFAULT;
}

static int
ua_attach(device_t dev)
{
	return uaudio_attach_sub(dev, &ua_mixer_class, &ua_chan_class);
}

static int
ua_detach(device_t dev)
{
	return uaudio_detach_sub(dev);
}

/************************************************************/

static device_method_t ua_pcm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ua_probe),
	DEVMETHOD(device_attach,	ua_attach),
	DEVMETHOD(device_detach,	ua_detach),

	{ 0, 0 }
};

static driver_t ua_pcm_driver = {
	"pcm",
	ua_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(ua_pcm, uaudio, ua_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(ua_pcm, uaudio, 1, 1, 1);
MODULE_DEPEND(ua_pcm, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(ua_pcm, 1);

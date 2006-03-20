/*-
 * Copyright (c) 2006 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * NOTE: This is a lite implementation of "FreeBSD/src/sys/kern/kern_timeout.c"
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <sys/freebsd_compat.h>

static void
__callout_cb(void *arg)
{
    struct __callout *c = (struct __callout *)arg;

    mtx_lock(c->mtx);

    if(c->func)
    {
        (c->func)(c->arg);
    }

    if(!(c->flags & CALLOUT_RETURNUNLOCKED))
    {
        mtx_unlock(c->mtx);
    }
    return;
}

void
__callout_init_mtx(struct __callout *c, struct mtx *mtx, u_int32_t flags)
{
    bzero(c, sizeof(*c));

    if(mtx == NULL)
    {
       mtx = &Giant;
    }
    c->mtx = mtx;
    c->flags = (flags & CALLOUT_RETURNUNLOCKED);

#ifdef __NetBSD__
    callout_init(&c->c_old);
#elif defined(__OpenBSD__)
#else
#error "Unknown operating system"
#endif
    return;
}

void
__callout_reset(struct __callout *c, u_int32_t to_ticks, 
		void (*func)(void *), void *arg)
{
    mtx_assert(c->mtx, MA_OWNED);

    __callout_stop(c);

    c->func = func;
    c->arg = arg;

#ifdef __NetBSD__
    callout_reset(&c->c_old, to_ticks, &__callout_cb, c);
#elif defined(__OpenBSD__)
    timeout(&__callout_cb, c, to_ticks);
#else
#error "Unknown operating system"
#endif
    return;
}

void
__callout_stop(struct __callout *c)
{
    mtx_assert(c->mtx, MA_OWNED);

#ifdef __NetBSD__
    callout_stop(&c->c_old);
#elif defined(__OpenBSD__)
    untimeout(&__callout_cb, c);
#else
#error "Unknown operating system"
#endif
    c->func = NULL;
    c->arg = NULL;
    return;
}

u_int8_t
__callout_pending(struct __callout *c)
{
    u_int8_t retval;

    mtx_assert(c->mtx, MA_OWNED);

#ifdef __NetBSD__
    retval = (callout_pending(&c->c_old) != 0);
#else
#error "Unknown operating system"
#endif

    return retval;
}
/*-
 * Copyright (c) 2007, 2010-2013 Steven G. Kargl
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * s_sinl.c and s_cosl.c merged by Steven G. Kargl.
 */

#include <sys/cdefs.h>
#if 0
__FBSDID("$FreeBSD: head/lib/msun/src/s_sincosl.c 319047 2017-05-28 06:13:38Z mmel $");
#endif
#if defined(LIBM_SCCS) && !defined(lint)
__RCSID("$NetBSD: s_sincosl.c,v 1.3 2024/02/24 15:16:53 christos Exp $");
#endif

#include "namespace.h"
#include <float.h>
#ifdef __i386__
#include <ieeefp.h>
#endif

#include "math.h"
#include "math_private.h"

#ifdef __weak_alias
__weak_alias(sincosl,_sincosl)
#endif

#ifdef __HAVE_LONG_DOUBLE

#include "k_sincosl.h"

#if LDBL_MANT_DIG == 64
#include "../ld80/e_rem_pio2l.h"
#elif LDBL_MANT_DIG == 113
#include "../ld128/e_rem_pio2l.h"
#else
#error "Unsupported long double format"
#endif

void
sincosl(long double x, long double *sn, long double *cs)
{
	union ieee_ext_u z;
	int e0;
	long double y[2];

	z.extu_ld = x;
	z.extu_sign = 0;

	ENTERV();

	/* Optimize the case where x is already within range. */
	if (z.extu_ld < M_PI_4) {
		/*
		 * If x = +-0 or x is a subnormal number, then sin(x) = x and
		 * cos(x) = 1.
		 */
		if (z.extu_exp == 0) {
			*sn = x;
			*cs = 1;
		} else
			__kernel_sincosl(x, 0, 0, sn, cs);
		RETURNV();
	}

	/* If x = NaN or Inf, then sin(x) and cos(x) are NaN. */
	if (z.extu_exp == 32767) {
		*sn = x - x;
		*cs = x - x;
		RETURNV();
	}

	/* Range reduction. */
	e0 = __ieee754_rem_pio2l(x, y);

	switch (e0 & 3) {
	case 0:
		__kernel_sincosl(y[0], y[1], 1, sn, cs);
		break;
	case 1:
		__kernel_sincosl(y[0], y[1], 1, cs, sn);
		*cs = -*cs;
		break;
	case 2:
		__kernel_sincosl(y[0], y[1], 1, sn, cs);
		*sn = -*sn;
		*cs = -*cs;
		break;
	default:
		__kernel_sincosl(y[0], y[1], 1, cs, sn);
		*sn = -*sn;
	}

	RETURNV();
}

#else

void
sincosl(long double x, long double *sn, long double *cs)
{
	double snn, css;
	sincos(x, &snn, &css); 
	*sn = snn;
	*cs = css;
}

#endif

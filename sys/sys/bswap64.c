/*  $NetBSD: bswap64.c,v 1.5 2003/12/04 13:57:31 keihan Exp $    */

/*
 * Written by Manuel Bouyer <bouyer@NetBSD.org>.
 * Public domain.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: bswap64.c,v 1.5 2003/12/04 13:57:31 keihan Exp $");
#endif /* LIBC_SCCS and not lint */

#include <sys/types.h>

#include <machine/bswap.h>
#undef bswap64

#ifndef bswap32
static __inline__ u_int32_t bswap32(u_int32_t var)
{
	__asm__ __volatile (
		"rorw #8, %0; swap %0; rorw #8, %0" : "=d" (var) : "0" (var));
	return (var);
}
#endif

u_int64_t
bswap64(x)
	u_int64_t x;
{
#ifdef _LP64
	/*
	 * Assume we have wide enough registers to do it without touching
	 * memory.
	 */
	return  ( (x << 56) & 0xff00000000000000UL ) |
		( (x << 40) & 0x00ff000000000000UL ) |
		( (x << 24) & 0x0000ff0000000000UL ) |
		( (x <<  8) & 0x000000ff00000000UL ) |
		( (x >>  8) & 0x00000000ff000000UL ) |
		( (x >> 24) & 0x0000000000ff0000UL ) |
		( (x >> 40) & 0x000000000000ff00UL ) |
		( (x >> 56) & 0x00000000000000ffUL );
#else
	/*
	 * Split the operation in two 32bit steps.
	 */
	u_int32_t tl, th;

	th = bswap32((u_int32_t)(x & 0x00000000ffffffffULL));
	tl = bswap32((u_int32_t)((x >> 32) & 0x00000000ffffffffULL));
	return ((u_int64_t)th << 32) | tl;
#endif
}
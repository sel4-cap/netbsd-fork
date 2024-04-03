#ifdef __GNUC__
# define __ASMNAME(cname)  __XSTRING (__USER_LABEL_PREFIX__) cname
#endif

#if defined(_HAVE_ALIAS_ATTRIBUTE)
#define	__strong_reference(sym,aliassym)	\
    extern __typeof (sym) aliassym __attribute__ ((__alias__ (__STRING(sym))))
#endif


#if defined(__STDC__) || defined(__cplusplus)
#define	__P(protos)	protos		/* full-blown ANSI C */
#define	__STRING(x)	#x		/* stringify without expanding x */
#define	__XSTRING(x)	__STRING(x)	/* expand x, then stringify */

#define	__const		const		/* define reserved names to standard */
#define	__signed	signed
#define	__volatile	volatile
#if defined(__cplusplus)
#define	__inline	inline		/* convert to C++ keyword */
#else
#if !(defined(__CC_SUPPORTS___INLINE))
#define	__inline			/* delete GCC keyword */
#endif /* ! __CC_SUPPORTS___INLINE */
#endif /* !__cplusplus */
#else	/* !(__STDC__ || __cplusplus) */
#define	__P(protos)	()		/* traditional C preprocessor */
#define	__CONCAT(x,y)	x/**/y
#define	__STRING(x)	"x"

#if !defined(__CC_SUPPORTS___INLINE)
#define	__const				/* delete pseudo-ANSI C keywords */
#define	__inline
#define	__signed
#define	__volatile
/*
 * In non-ANSI C environments, new programs will want ANSI-only C keywords
 * deleted from the program and old programs will want them left alone.
 * When using a compiler other than gcc, programs using the ANSI C keywords
 * const, inline etc. as normal identifiers should define -DNO_ANSI_KEYWORDS.
 * When using "gcc -traditional", we assume that this is the intent; if
 * __GNUC__ is defined but __STDC__ is not, we leave the new keywords alone.
 */
#ifndef	NO_ANSI_KEYWORDS
#define	const				/* delete ANSI C keywords */
#define	inline
#define	signed
#define	volatile
#endif	/* !NO_ANSI_KEYWORDS */
#endif	/* !__CC_SUPPORTS___INLINE */
#endif	/* !(__STDC__ || __cplusplus) */


#ifndef __GNUC_PREREQ
# if defined __GNUC__ && defined __GNUC_MINOR__
#  define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
# else
#  define __GNUC_PREREQ(maj, min) 0
# endif
#endif /* __GNUC_PREREQ */
/* Version with trailing underscores for BSD compatibility. */
#define	__GNUC_PREREQ__(ma, mi)	__GNUC_PREREQ(ma, mi)

#if __GNUC_PREREQ__(2, 96)
# if __GNUC_PREREQ__(11, 0)
#  define       __malloc_like_with_free(_f,_a) __attribute__((__malloc__, __malloc__(_f,_a)))
# else
#  define       __malloc_like_with_free(_f,_a) __attribute__((__malloc__))
# endif
#else
#define __malloc_like_with_free(free,free_arg)
#endif

#if __GNUC_PREREQ__(3, 4)
#define	__fastcall	__attribute__((__fastcall__))
#define	__result_use_check	__attribute__((__warn_unused_result__))
#else
#define	__fastcall
#define	__result_use_check
#endif

#ifdef _HAVE_ALLOC_SIZE
#define	__alloc_size(x)	__attribute__((__alloc_size__(x)))
#define	__alloc_size2(n, x)	__attribute__((__alloc_size__(n, x)))
#else
#define	__alloc_size(x)
#define	__alloc_size2(n, x)
#endif
#if __GNUC_PREREQ__(4, 9) || __has_attribute(__alloc_align__)
#define	__alloc_align(x)	__attribute__((__alloc_align__(x)))
#else
#define	__alloc_align(x)
#endif

#if __GNUC_PREREQ__(4,5) || defined(__clang__)
#define __picolibc_deprecated(m) __attribute__((__deprecated__(m)))
#else
#define __picolibc_deprecated(m) _ATTRIBUTE(__deprecated__)
#endif

#if defined(__clang__) && defined(__nonnull)
/* Clang has a builtin macro __nonnull for the _Nonnull qualifier */
#undef __nonnull
#endif
#if __GNUC_PREREQ__(3, 3)
#define	__nonnull(x)	__attribute__((__nonnull__ x))
#define	__nonnull_all	__attribute__((__nonnull__))
#else
#define	__nonnull(x)
#define	__nonnull_all
#endif

#if !__GNUC_PREREQ__(2, 5)
#define	__dead2
#define	__pure2
#define	__unused
#endif
#if __GNUC__ == 2 && __GNUC_MINOR__ >= 5 && __GNUC_MINOR__ < 7
#define	__dead2		__attribute__((__noreturn__))
#define	__pure2		__attribute__((__const__))
#define	__unused
/* XXX Find out what to do for __packed, __aligned and __section */
#endif
#if __GNUC_PREREQ__(2, 7)
#define	__dead2		__attribute__((__noreturn__))
#define	__pure2		__attribute__((__const__))
#define	__unused	__attribute__((__unused__))
#define	__used		__attribute__((__used__))
#define	__packed	__attribute__((__packed__))
#define	__aligned(x)	__attribute__((__aligned__(x)))
#define	__section(x)	__attribute__((__section__(x)))
#endif

#define	__malloc_like __malloc_like_with_free(free, 1)
#ifndef __GNUC__
#pragma once
#endif
#ifndef __XR_TYPES_H__
#define __XR_TYPES_H__

#include <cstdint>
#include <cinttypes>

#if defined(__MINGW32__)
#define PRIuSIZET	"Iu"
#endif

const uint32_t BAD_IDX = UINT32_MAX;

#include <cstddef>
#include <cassert>

namespace xray_re {

class xr_error {};

template<typename T> inline bool equivalent(T a, T b, T e = T(1e-6))
{
	return (a < b) ? (b - a < e) : (a - b < e);
}

void die(const char* msg, const char* file, unsigned line);
void msg(const char* format, ...);
void dbg(const char* format, ...);

} // end of namespace xray_re

#ifdef NDEBUG
#define xr_assert(expr)		while (!(expr)) { xray_re::die("assertion failed", __FILE__, __LINE__); break; }
#define xr_not_implemented()	xray_re::die("unimplemented code path", __FILE__, __LINE__)
#define xr_not_expected()	xray_re::die("unexpected code path", __FILE__, __LINE__)
#else
#define xr_assert(expr) assert(expr)
#define xr_not_implemented() assert(0)
#define xr_not_expected() assert(0)
#endif

#define xr_dim(x)	sizeof(x)/sizeof((x)[0])

#define TYPEDEF_STD_VECTOR(type) \
	typedef std::vector< type > type##_vec; \
	typedef std::vector< type >::iterator type##_vec_it; \
	typedef std::vector< type >::const_iterator type##_vec_cit;

#define TYPEDEF_STD_VECTOR_PTR(type) \
	typedef std::vector< type* > type##_vec; \
	typedef std::vector< type* >::iterator type##_vec_it; \
	typedef std::vector< type* >::const_iterator type##_vec_cit;

#endif

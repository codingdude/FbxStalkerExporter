#include <algorithm>
#include "xr_envelope.h"
#include "xr_reader.h"
#include "xr_writer.h"
#include "xr_utils.h"

using namespace xray_re;

void xr_key::load_1(xr_reader& r)
{
	value = r.r_float();
	time = r.r_float();
	shape = uint8_t(r.r_u32());
	tension = r.r_float();
	continuity = r.r_float();
	bias = r.r_float();
	r.r_cseq(4, param);
}

void xr_key::load_2(xr_reader& r)
{
	value = r.r_float();
	time = r.r_float();
	if ((shape = r.r_u8()) != SHAPE_STEP) {
		tension = r.r_float_q16(-32.f, 32.f);
		continuity = r.r_float_q16(-32.f, 32.f);
		bias = r.r_float_q16(-32.f, 32.f);
		for (uint_fast32_t i = 0; i != 4; ++i)
			param[i] = r.r_float_q16(-32.f, 32.f);
	}
}

void xr_key::save(xr_writer& w) const
{
	w.w_float(value);
	w.w_float(time);
	w.w_u8(shape);
	if (shape != SHAPE_STEP) {
		w.w_float_q16(tension, -32.f, 32.f);
		w.w_float_q16(continuity, -32.f, 32.f);
		w.w_float_q16(bias, -32.f, 32.f);
		for (uint_fast32_t i = 0; i != 4; ++i)
			w.w_float_q16(param[i], -32.f, 32.f);
	}
}

xr_envelope::~xr_envelope()
{
	delete_elements(m_keys);
}

void xr_envelope::insert_key(xr_key* key)
{
	m_keys.push_back(key);
}

void xr_envelope::insert_key(float time, float value)
{
	xr_key* key = new xr_key;
	key->time = time;
	key->value = value;
	key->shape = xr_key::SHAPE_STEP;
	m_keys.push_back(key);
}

void xr_envelope::load_1(xr_reader& r)
{
	m_behaviour0 = uint8_t(r.r_u32() & UINT8_MAX);
	m_behaviour1 = uint8_t(r.r_u32() & UINT8_MAX);
	r.r_seq(r.r_u32(), m_keys, &xr_key::load_1);
}

void xr_envelope::load_2(xr_reader& r)
{
	m_behaviour0 = r.r_u8();
	m_behaviour1 = r.r_u8();
	r.r_seq(r.r_u16(), m_keys, &xr_key::load_2);
}

void xr_envelope::save(xr_writer& w) const
{
	w.w_u8(m_behaviour0);
	w.w_u8(m_behaviour1);
	w.w_size_u16(m_keys.size());
	w.w_seq(m_keys, &xr_key::save);
}

void xr_envelope::rebuild()
{
	auto pred = [](const xr_key* lhs, const xr_key* rhs)
	{
		return lhs->time < rhs->time;
	};

	auto is_twisted = [](xr_key_vec::const_iterator prev,
		xr_key_vec::const_iterator next)
	{
		const auto ang0 = (*prev)->value;
		const auto ang1 = (*next)->value;
		if (std::fabs(ang0 + ang1) < M_PI / 4) {
			const auto abs0 = std::fabs(ang0);
			const auto abs1 = std::fabs(ang1);
			if (M_PI - abs0 < M_PI / 4
				&& M_PI - abs1 < M_PI / 4) {
				return true;
			}
		}
		return false;
	};

	auto is_mirrored = [](xr_key_vec::const_iterator prev,
		xr_key_vec::const_iterator next)
	{
		const auto ang0 = (*prev)->value;
		const auto ang1 = (*next)->value;
		if (std::abs(std::fabs(ang0) - M_PI) <= DBL_EPSILON) {
			if (std::signbit(ang0) != std::signbit(ang1)) {
				return true;
			}
		}
		return false;
	};

	auto reverse_keys = [](xr_key_vec::iterator begin,
		xr_key_vec::iterator end)
	{
		for (auto it = begin; it != end; ++it) {
			if (std::signbit((*it)->value)) {
				(*it)->value += static_cast<float>(M_PI * 2);
			}
			else {
				(*it)->value -= static_cast<float>(M_PI * 2);
			}
		}
	};

	auto fix_mirrored = [](xr_key_vec::const_iterator it)
	{
		(*it)->value *= -1.f;
	};

	std::stable_sort(m_keys.begin(), m_keys.end(), pred);

	if (m_type == ROTATION) {
		auto prev = m_keys.begin();
		for (auto it = prev; it != m_keys.end(); ++it) {
			if (is_mirrored(prev, it)) {
				fix_mirrored(prev);
			}
			if (is_twisted(prev, it)) {
				reverse_keys(it, m_keys.end());
			}
			prev = it;
		}
	}
}

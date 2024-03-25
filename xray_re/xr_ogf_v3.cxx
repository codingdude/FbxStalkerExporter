#include <algorithm>
#include "xr_envelope.h"
#include "xr_ogf_v3.h"
#include "xr_file_system.h"
#include "xr_ini_file.h"
#include "xr_string_utils.h"

using namespace xray_re;

struct xr_ogf_v3::bone_io: public xr_bone {
	void	import(xr_reader& r);
};

struct xr_ogf_v3::partition_io: public xr_partition {
	void	import(xr_reader& r, xr_bone_vec& all_bones);
	void	import(const xr_ini_file& ini, const char* part_name, xr_ogf_v3& ogf);
};

struct xr_ogf_v3::bone_motion_io: public xr_ogf::bone_motion_io {
	void	import(xr_reader& r, uint_fast32_t num_keys);
};

struct xr_ogf_v3::motion_io: public xr_skl_motion {
			motion_io();
	void		import_bone_motions(xr_reader& r, xr_bone_vec& all_bones);
	uint16_t	import_params(xr_reader& r);
	void		import_params(const xr_ini_file& ini, ogf3_motion_type motion_type,
					const char* section, const char* name, xr_ogf_v3& ogf);
};

inline xr_ogf_v3::motion_io::motion_io() { m_fps = OGF3_MOTION_FPS; }

////////////////////////////////////////////////////////////////////////////////

xr_ogf_v3::xr_ogf_v3(): xr_ogf(OGF3_VERSION),
	m_vsplits(0), m_fix_faces(0),
	m_ext_vb_index(0), m_ext_vb_offset(0), m_ext_vb_size(0) {}

xr_ogf_v3::~xr_ogf_v3()
{
	delete[] m_vsplits;
	delete[] m_fix_faces;
}

void xr_ogf_v3::clear()
{
	xr_ogf::clear();
	delete[] m_vsplits; m_vsplits = 0;
	delete[] m_fix_faces; m_fix_faces = 0;
	m_ib.clear();
}

bool xr_ogf_v3::hierarchical() const
{
	switch (m_model_type) {
	case MT3_HIERRARHY:
	case MT3_SKELETON_ANIM:
		return true;
	default:
		return false;
	}
}

bool xr_ogf_v3::skeletal() const { return m_model_type == MT3_SKELETON_ANIM; }

bool xr_ogf_v3::animated() const { return m_model_type == MT3_SKELETON_ANIM; }

bool xr_ogf_v3::progressive() const
{
	switch (m_model_type) {
	case MT3_PROGRESSIVE:
	case MT3_SKELETON_GEOMDEF_PM:
		return true;
	default:
		return false;
	}
}

bool xr_ogf_v3::versioned() const { return false; }

void xr_ogf_v3::setup_ib0()
{
	m_ib0 = m_ib;
	size_t fix_idx = 0;
	uint16_t active_vb_size = uint16_t(m_min_vertices & UINT16_MAX);
	for (ogf3_vsplit *p = m_vsplits, *end = p + m_vb.size() - m_min_vertices; p != end; ++p) {
		for (uint_fast32_t end_idx = static_cast<uint_fast32_t>(fix_idx + p->fix_faces); fix_idx < end_idx; ++fix_idx)
			m_ib0[m_fix_faces[fix_idx]] = active_vb_size;
		++active_vb_size;
	}
	assert(active_vb_size == m_vb.size());
}

void xr_ogf_v3::set_ext_geom(const xr_vbuf_vec& ext_vbufs)
{
	if (is_chunk_loaded(OGF3_VCONTAINER))
		m_vb.proxy(ext_vbufs.at(m_ext_vb_index), m_ext_vb_offset, m_ext_vb_size);
}

////////////////////////////////////////////////////////////////////////////////

void xr_ogf_v3::load_header(xr_reader& r)
{
	m_version = static_cast<ogf_version>(r.r_u8());
	m_model_type = static_cast<ogf_model_type>(r.r_u8());
	uint16_t unused = r.r_u16();
	xr_assert(unused == 0);
	set_chunk_loaded(OGF_HEADER);
}

inline void xr_ogf_v3::load_texture(xr_reader& r)
{
	xr_ogf::load_texture(r);
	set_chunk_loaded(OGF3_TEXTURE);
}

inline void xr_ogf_v3::load_texture_l(xr_reader& r)
{
	m_texture_l = r.r_u32();
	m_shader_l = r.r_u32();
	set_chunk_loaded(OGF3_TEXTURE_L);
}

void xr_ogf_v3::load_child_refs(xr_reader& r)
{
	assert(m_children.empty());
	std::string folder, name;
	xr_file_system::split_path(m_path, &folder);
	xr_file_system& fs = xr_file_system::instance();
	for (uint_fast32_t n = r.r_u32(); n; --n) {
		r.r_sz(name);
		xr_reader* s = fs.r_open(folder + name);
		if (!s)
			continue;
		xr_ogf_v3* ogf = new xr_ogf_v3;
		ogf->load_ogf(*s);
		m_children.push_back(ogf);
		fs.r_close(s);
	}
	set_chunk_loaded(OGF3_CHILD_REFS);
}

inline void xr_ogf_v3::load_bbox(xr_reader& r)
{
	r.r(m_bbox);
	set_chunk_loaded(OGF3_BBOX);
}

void xr_ogf_v3::load_vertices(xr_reader& r)
{
	ogf_vertex_format fmt = static_cast<ogf_vertex_format>(r.r_u32());
	size_t n = r.r_u32();
	m_vb.load_ogf3(r, n, fmt);
	r.debug_find_chunk();
	set_chunk_loaded(OGF3_VERTICES);
}

void xr_ogf_v3::load_indices(xr_reader& r)
{
	size_t n = r.r_u32();
	m_ib.load(r, n);
	r.debug_find_chunk();
	set_chunk_loaded(OGF3_INDICES);
}

inline void xr_ogf_v3::load_loddata(xr_reader& r)
{
	if (!r.find_chunk(OGF3_HOPPE_HEADER))
		xr_not_expected();
	m_min_vertices = r.r_u32();
	m_min_indices = r.r_u32();
	r.debug_find_chunk();

	if (!r.find_chunk(OGF3_HOPPE_VERT_SPLITS))
		xr_not_expected();
	xr_assert(is_chunk_loaded(OGF3_VERTICES));
	size_t num_vsplits = m_vb.size() - m_min_vertices;
	m_vsplits = new ogf3_vsplit[num_vsplits];
	r.r_cseq(num_vsplits, m_vsplits);
	r.debug_find_chunk();

	if (!r.find_chunk(OGF3_HOPPE_FIX_FACES))
		xr_not_expected();
	m_num_fix_faces = r.r_u32();
	m_fix_faces = new uint16_t[m_num_fix_faces];
	r.r_cseq(m_num_fix_faces, m_fix_faces);
	r.debug_find_chunk();

	setup_ib0();

	set_chunk_loaded(OGF3_LODDATA);
}

inline void xr_ogf_v3::load_vcontainer(xr_reader& r)
{
	m_ext_vb_index = r.r_u32();
	m_ext_vb_offset = r.r_u32();
	m_ext_vb_size = r.r_u32();
	set_chunk_loaded(OGF3_VCONTAINER);
}

inline void xr_ogf_v3::load_bsphere(xr_reader& r)
{
	r.r(m_bsphere);
	set_chunk_loaded(OGF3_BSPHERE);
}

void xr_ogf_v3::load_children_l(xr_reader& r)
{
	r.r_seq(r.r_u32(), m_children_l);
	set_chunk_loaded(OGF3_CHILDREN_L);
}

void xr_ogf_v3::load_dpatch(xr_reader& r)
{
	xr_not_implemented();
	set_chunk_loaded(OGF3_DPATCH);
}

void xr_ogf_v3::load_lods(xr_reader& r)
{
	assert(m_lods.empty());
	xr_reader* s;
	for (uint32_t id = 0; (s = r.open_chunk(id)); ++id) {
		xr_ogf_v3* ogf = new xr_ogf_v3;
		ogf->load_ogf(*s);
		m_lods.push_back(ogf);
		r.close_chunk(s);
	}
	set_chunk_loaded(OGF3_LODS);
}

void xr_ogf_v3::load_children(xr_reader& r)
{
	assert(m_children.empty());
	xr_reader* s;
	for (uint32_t id = 0; (s = r.open_chunk(id)); ++id) {
		xr_ogf_v3* ogf = new xr_ogf_v3;
		ogf->load_ogf(*s);
		m_children.push_back(ogf);
		r.close_chunk(s);
	}
	set_chunk_loaded(OGF3_CHILDREN);
}

inline void xr_ogf_v3::bone_motion_io::import(xr_reader& r, uint_fast32_t num_keys)
{
	create_envelopes();
	for (uint_fast32_t i = 0; i != num_keys; ++i) {
		float time = float(i)/OGF3_MOTION_FPS;
		insert_key(time, r.skip<ogf_key_qr>());
		insert_key(time, r.skip<fvector3>());
	}
	for (uint_fast32_t i = 0; i < 6; ++i) {
		m_envelopes[i]->rebuild();
	}
}

inline void xr_ogf_v3::motion_io::import_bone_motions(xr_reader& r, xr_bone_vec& all_bones)
{
	uint_fast32_t num_keys = r.r_u32();
	m_frame_start = 0;
	m_frame_end = int32_t(num_keys & INT32_MAX);

	assert(m_bone_motions.empty());
	m_bone_motions.reserve(all_bones.size());
	for (xr_bone_vec_it it = all_bones.begin(), end = all_bones.end(); it != end; ++it) {
		xr_ogf_v3::bone_motion_io* bm = new xr_ogf_v3::bone_motion_io;
		bm->name() = (*it)->name();
		bm->import(r, num_keys);
		m_bone_motions.push_back(bm);
	}
}

void xr_ogf_v3::load_s_motions(xr_reader& r)
{
	if (!r.find_chunk(0))
		xr_not_expected();
	size_t num_motions = r.r_u32();
	xr_assert(m_motions.size() == num_motions);
	for (uint32_t id = 1; id <= num_motions; ++id) {
		if (!r.find_chunk(id))
			xr_not_expected();

		const char* name = r.skip_sz();
		motion_io* smotion = static_cast<motion_io*>(find_motion(name));
		if (smotion == 0) {
			msg("unknown motion %s", name);
			throw xr_error();
		}
		smotion->import_bone_motions(r, m_bones);

		r.debug_find_chunk();
	}
	set_chunk_loaded(OGF3_S_MOTIONS);
}

inline void xr_ogf_v3::partition_io::import(xr_reader& r, xr_bone_vec& all_bones)
{
	r.r_sz(m_name);
	for (uint_fast32_t n = r.r_u16(); n; --n)
		m_bones.push_back(all_bones.at(r.r_u32())->name());
}

inline uint16_t xr_ogf_v3::motion_io::import_params(xr_reader& r)
{
	r.r_sz(m_name);
	m_flags = (r.r_u8() == SMT_FX) ? SMF_FX : 0;
	m_bone_or_part = r.r_u16();
	uint16_t motion_id = r.r_u16();
	m_speed = r.r_float();
	m_power = r.r_float();
	m_accrue = r.r_float();
	m_falloff = r.r_float();
	if (r.r_bool())
		m_flags |= SMF_STOP_AT_END;
	return motion_id;
}

struct read_partition_v3 {
	xr_bone_vec& all_bones;
	read_partition_v3(xr_bone_vec& _all_bones): all_bones(_all_bones) {}
	void operator()(xr_partition*& _part, xr_reader& r) {
		xr_ogf_v3::partition_io* part = new xr_ogf_v3::partition_io;
		_part = part;
		part->import(r, all_bones);
	}
};

void xr_ogf_v3::load_s_smparams(xr_reader& r)
{
	assert(m_partitions.empty());
	r.r_seq(r.r_u16(), m_partitions, read_partition_v3(m_bones));
	setup_partitions();

	assert(m_motions.empty());
	size_t num_motions = r.r_u16();
	m_motions.resize(num_motions);
	for (; num_motions; --num_motions) {
		motion_io* smotion = new xr_ogf_v3::motion_io;
		m_motions.at(smotion->import_params(r)) = smotion;
	}
	assert(std::find(m_motions.begin(), m_motions.end(), static_cast<xr_skl_motion*>(0)) == m_motions.end());

	set_chunk_loaded(OGF3_S_SMPARAMS);
}

inline void xr_ogf_v3::partition_io::import(const xr_ini_file& ini, const char* part_name, xr_ogf_v3& ogf)
{
	size_t num_bones = ini.line_count(part_name);
	if (num_bones == 0) {
		msg("empty partition section %s", part_name);
		throw xr_error();
	}

	m_name = part_name;
	m_bones.reserve(num_bones);
	for (size_t i = 0; i != num_bones; ++i) {
		const char* bone_name;
		ini.r_line(part_name, i, &bone_name, 0);
		if (ogf.find_bone(bone_name) == 0) {
			msg("unknown bone %s in partition %s", bone_name, part_name);
			throw xr_error();
		}
		m_bones.push_back(bone_name);
	}
}

inline void xr_ogf_v3::motion_io::import_params(const xr_ini_file& ini, ogf3_motion_type motion_type,
		const char* section, const char* name, xr_ogf_v3& ogf)
{
	if (motion_type == SMT_CYCLE) {
		const char* part_name = ini.r_string(section, "part");
		if (strstr(part_name, "--none--") == 0) {
			xr_partition* part = ogf.find_partition(part_name);
			if (part == 0) {
				msg("unknown partition %s in motion %s", part_name, name);
				throw xr_error();
			}
			m_bone_or_part = part->id();
		} else {
			m_bone_or_part = ALL_PARTITIONS;
		}
		m_flags = 0;
	} else {
		const char* bone_name = ini.r_string(section, "bone");
		xr_bone* bone = ogf.find_bone(bone_name);
		if (bone == 0) {
			msg("unknown bone %s in motion %s", bone_name, name);
			throw xr_error();
		}
		m_bone_or_part = bone->id();
		m_flags = SMF_FX;
	}
	m_speed = ini.r_float(section, "speed");
	m_power = ini.r_float(section, "power");
	m_accrue = ini.r_float(section, "accrue");
	m_falloff = ini.r_float(section, "falloff");
	if (ini.r_bool(section, "stop@end"))
		m_flags |= SMF_STOP_AT_END;
//	else
//		m_flags &= ~SMF_STOP_AT_END;
	m_name = name;
}

void xr_ogf_v3::load_motion_defs(xr_ini_file& ini,
		ogf3_motion_type motion_type, const char* motion_type_name)
{
	if (!ini.section_exist(motion_type_name)) {
		msg("empty motion defs section %s", motion_type_name);
		throw xr_error();
	}
	size_t num_motions = ini.line_count(motion_type_name);
	m_motions.reserve(num_motions + m_motions.size());
	for (size_t i = 0; i != num_motions; ++i) {
		const char* name;
		const char* section_name;
		ini.r_line(motion_type_name, i, &name, &section_name);
		// sometimes there is no right side
		if (section_name && section_name[0] == '\0')
			section_name = name;
		xr_assert(xr_stricmp(ini.r_string(section_name, "motion"), name) == 0);
		motion_io* smotion = new xr_ogf_v3::motion_io;
		smotion->import_params(ini, motion_type, section_name, name, *this);
		m_motions.push_back(smotion);
	}
}

void xr_ogf_v3::load_s_smparams()
{
	assert(!m_path.empty());
	std::string folder, name, ltx_name;
	xr_file_system::split_path(m_path, &folder, &name);

	xr_ini_file ini;
	if (!ini.load(ltx_name.append(folder).append(name).append(".ltx"))) {
		msg("cannot open %s", ltx_name.c_str());
		throw xr_error();
	}

	size_t num_parts = ini.line_count("partition");
	if (num_parts == 0) {
		msg("empty partition section");
		throw xr_error();
	}

	assert(m_partitions.empty());
	m_partitions.reserve(num_parts);
	for (size_t i = 0; i != num_parts; ++i) {
		const char* part_name;
		ini.r_line("partition", i, &part_name, 0);
		xr_ogf_v3::partition_io* part = new xr_ogf_v3::partition_io;
		part->import(ini, part_name, *this);
		m_partitions.push_back(part);
	}
	setup_partitions();

	assert(m_motions.empty());
	load_motion_defs(ini, SMT_CYCLE, "cycle");
	load_motion_defs(ini, SMT_FX, "fx");
}

inline void xr_ogf_v3::bone_io::import(xr_reader& r)
{
	r.r_sz(m_name);
	r.r_sz(m_parent_name);
	m_vmap_name = m_name;

	m_shape.type = ST_BOX;
	m_shape.flags = 0;
	r.r(m_shape.box);

	// FIXME: reconstruct bind pose here
	m_bind_offset.set();
	m_bind_rotate.set();
	m_bind_length = 0.5f;
}

struct read_bone_v3 {
	xr_ogf_v3& ogf;
	read_bone_v3(xr_ogf_v3& _ogf): ogf(_ogf) {}
	void operator()(xr_bone*& _bone, xr_reader& r) {
		xr_ogf_v3::bone_io* bone = new xr_ogf_v3::bone_io;
		_bone = bone;
		bone->import(r);
	}
};

void xr_ogf_v3::load_s_bone_names(xr_reader& r)
{
	assert(m_bones.empty());
	r.r_seq(r.r_u32(), m_bones, read_bone_v3(*this));
	setup_bones();
	set_chunk_loaded(OGF3_S_BONE_NAMES);
}

////////////////////////////////////////////////////////////////////////////////

void xr_ogf_v3::load_render_visual(xr_reader& r)
{
	// header is already loaded

	if (!r.find_chunk(OGF3_BBOX))
		xr_not_expected();
	load_bbox(r);
	r.debug_find_chunk();

	if (r.find_chunk(OGF3_BSPHERE)) {
		load_bsphere(r);
		r.debug_find_chunk();
	}

	if (r.find_chunk(OGF3_TEXTURE_L)) {
		load_texture_l(r);
		r.debug_find_chunk();
	} else if (r.find_chunk(OGF3_TEXTURE)) {
		load_texture(r);
		r.debug_find_chunk();
	}
}

void xr_ogf_v3::load_visual(xr_reader& r)
{
	load_render_visual(r);
	if (r.find_chunk(OGF3_VCONTAINER)) {
		load_vcontainer(r);
		r.debug_find_chunk();
	} else {
		if (!r.find_chunk(OGF3_VERTICES))
			xr_not_expected();
		load_vertices(r);
		r.debug_find_chunk();
	}
	if (!r.find_chunk(OGF3_INDICES))
		xr_not_expected();
	load_indices(r);
	r.debug_find_chunk();
}

void xr_ogf_v3::load_hierrarhy_visual(xr_reader& r)
{
	load_render_visual(r);
	if (r.find_chunk(OGF3_CHILDREN_L)) {
		load_children_l(r);
		r.debug_find_chunk();
	} else {
		xr_reader* s = r.open_chunk(OGF3_CHILDREN);
		if (s) {
			load_children(*s);
			r.close_chunk(s);
		} else {
			if (!r.find_chunk(OGF3_CHILD_REFS))
				xr_not_expected();
			load_child_refs(r);
			r.debug_find_chunk();
		}
	}
}

void xr_ogf_v3::load_progressive_fixed_visual(xr_reader& r)
{
	load_visual(r);

	xr_reader* s = r.open_chunk(OGF3_LODDATA);
	assert(s);
	load_loddata(*s);
	r.close_chunk(s);
}

void xr_ogf_v3::load_kinematics(xr_reader& r)
{
	load_hierrarhy_visual(r);

	if (!r.find_chunk(OGF3_S_BONE_NAMES))
		xr_not_expected();
	load_s_bone_names(r);
	r.debug_find_chunk();

	xr_reader* s = r.open_chunk(OGF3_S_SMPARAMS);
	if (s) {
		load_s_smparams(*s);
		xr_assert(s->eof());
		r.close_chunk(s);
	} else {
		load_s_smparams();
	}

	s = r.open_chunk(OGF3_S_MOTIONS);
	xr_assert(s != 0);
	load_s_motions(*s);
	assert(s->eof());
	r.close_chunk(s);
}

inline void xr_ogf_v3::load_skeletonx(xr_reader& r)
{
	xr_assert(r.find_chunk(OGF3_VERTICES));
	xr_assert(r.r_u32() == OGF3_VERTEXFORMAT_FVF_1L);
}

void xr_ogf_v3::load_skeletonx_pm(xr_reader& r)
{
	load_skeletonx(r);
	load_progressive_fixed_visual(r);
}

void xr_ogf_v3::load_skeletonx_st(xr_reader& r)
{
	load_skeletonx(r);
	load_visual(r);
}

void xr_ogf_v3::load_detail_patch(xr_reader& r)
{
	load_render_visual(r);
	if (!r.find_chunk(OGF3_DPATCH))
		xr_not_expected();
	load_dpatch(r);
	r.debug_find_chunk();
}

void xr_ogf_v3::load_cached(xr_reader& r)
{
	load_render_visual(r);
	if (!r.find_chunk(OGF3_VERTICES))
		xr_not_expected();
	load_vertices(r);
	r.debug_find_chunk();

	if (!r.find_chunk(OGF3_INDICES))
		xr_not_expected();
	load_indices(r);
	r.debug_find_chunk();
}

inline void xr_ogf_v3::load_particle(xr_reader& r)
{
	load_render_visual(r);
}

void xr_ogf_v3::load_progressive(xr_reader& r)
{
	load_render_visual(r);
	xr_reader* s = r.open_chunk(OGF3_LODS);
	assert(s);
	load_lods(*s);
	r.close_chunk(s);
}

////////////////////////////////////////////////////////////////////////////////

void xr_ogf_v3::load_ogf(xr_reader& r)
{
	if (!r.find_chunk(OGF_HEADER))
		xr_not_expected();
	load_header(r);
	r.debug_find_chunk();

	switch (m_model_type) {
	case MT3_NORMAL:
		load_visual(r);
		break;
	case MT3_HIERRARHY:
		load_hierrarhy_visual(r);
		break;
	case MT3_PROGRESSIVE:
		load_progressive_fixed_visual(r);
		m_flags = EOF_PROGRESSIVE;
		break;
	case MT3_SKELETON_ANIM:
		load_kinematics(r);
		m_flags = EOF_DYNAMIC;
		break;
	case MT3_SKELETON_GEOMDEF_PM:
		load_skeletonx_pm(r);
		m_flags = EOF_PROGRESSIVE;
		break;
	case MT3_DETAIL_PATCH:
		load_detail_patch(r);
		break;
	case MT3_SKELETON_GEOMDEF_ST:
		load_skeletonx_st(r);
		break;
	case MT3_CACHED:
		load_cached(r);
		break;
	case MT3_PARTICLE:
		load_particle(r);
		break;
	case MT3_PROGRESSIVE2:
		load_progressive(r);
		m_flags = EOF_PROGRESSIVE;
		break;
	default:
		xr_not_expected();
		break;
	}
	check_unhandled_chunks(r);
}

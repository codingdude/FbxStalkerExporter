#include "xr_scene_groups.h"
#include "xr_scene.h"
#include "xr_reader.h"
#include "xr_writer.h"

using namespace xray_re;

xr_group_object::xr_group_object(xr_scene& scene):
	xr_custom_object(scene, TOOLS_CLASS_GROUP),
	m_flags(0) {}

xr_group_object::~xr_group_object() {}

void xr_group_object::load(xr_reader& r)
{
	uint16_t version;
	if (!r.r_chunk<uint16_t>(GROUPOBJ_CHUNK_VERSION, version))
		xr_not_expected();
	xr_assert(version == GROUPOBJ_VERSION);
	xr_custom_object::load(r);
	r.r_chunk<uint32_t>(GROUPOBJ_CHUNK_FLAGS, m_flags);
	if (m_flags & GOF_OPEN) {
		if (r.find_chunk(GROUPOBJ_CHUNK_OPEN_OBJECT_LIST))
			r.r_seq(r.r_u32(), m_open_objects, [](std::string& s, xr_reader& r) { r.r_sz(s); });
	} else {
		scene().load_objects(r, GROUPOBJ_CHUNK_OBJECTS, m_objects);
	}
	xr_assert(!m_objects.empty() || !m_open_objects.empty());
	if (r.find_chunk(GROUPOBJ_CHUNK_REFERENCE)) {
		r.r_sz(m_reference);
		r.debug_find_chunk();
	}
}

void xr_group_object::save(xr_writer& w) const
{
	xr_custom_object::save(w);
	w.w_chunk<uint16_t>(GROUPOBJ_CHUNK_VERSION, GROUPOBJ_VERSION);
	w.w_chunk<uint32_t>(GROUPOBJ_CHUNK_FLAGS, m_flags);
	if (m_flags & GOF_OPEN) {
		w.open_chunk(GROUPOBJ_CHUNK_OPEN_OBJECT_LIST);
		w.w_size_u32(m_open_objects.size());
		w.w_seq(m_open_objects, [](const std::string& s, xr_writer& w) { w.w_sz(s); });
		w.close_chunk();
	} else {
		scene().save_objects(w, GROUPOBJ_CHUNK_OBJECTS, m_objects);
	}
	w.w_chunk(GROUPOBJ_CHUNK_REFERENCE, m_reference);
}

////////////////////////////////////////////////////////////////////////////////

xr_scene_groups::xr_scene_groups(xr_scene& scene):
	xr_scene_objects(scene, "group.part", SCENE_CHUNK_GROUPS) {}

xr_scene_groups::~xr_scene_groups() {}

void xr_scene_groups::load(xr_reader& r)
{
	uint16_t version = 0;
	r.r_chunk<uint16_t>(TOOLS_CHUNK_VERSION, version);
	xr_assert(version == 0);
	xr_scene_objects::load(r);
}

void xr_scene_groups::save(xr_writer& w) const
{
	xr_scene_objects::save(w);
	w.w_chunk<uint16_t>(TOOLS_CHUNK_VERSION, 0);
}

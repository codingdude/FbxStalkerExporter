#include "xr_scene_visuals.h"
#include "xr_reader.h"
#include "xr_writer.h"

using namespace xray_re;

xr_visual_object::xr_visual_object(xr_scene& scene):
	xr_custom_object(scene, TOOLS_CLASS_SCENE_OBJECT),
	m_file_version(0), m_flags(0) {}

xr_visual_object::~xr_visual_object() {}

void xr_visual_object::load(xr_reader& r)
{
	uint16_t version;
	if (!r.r_chunk<uint16_t>(SCENEOBJ_CHUNK_VERSION, version))
		xr_not_expected();
	xr_assert(version == SCENEOBJ_VERSION_16 || version == SCENEOBJ_VERSION);

	if (version == SCENEOBJ_VERSION_16) {
		if (!r.find_chunk(SCENEOBJ_CHUNK_PLACEMENT))
			xr_not_expected();
		r.r_fvector3(co_position());
		r.r_fvector3(co_rotation());
		r.r_fvector3(co_scale());
		r.debug_find_chunk();
	}

	xr_custom_object::load(r);

	if (!r.find_chunk(SCENEOBJ_CHUNK_REFERENCE))
		xr_not_expected();
	m_file_version = r.r_u32();
	r.r_u32();
	r.r_sz(m_reference);
	r.debug_find_chunk();

	r.r_chunk<uint32_t>(SCENEOBJ_CHUNK_FLAGS, m_flags);
}

void xr_visual_object::save(xr_writer& w) const
{
	xr_custom_object::save(w);
	w.w_chunk<uint16_t>(SCENEOBJ_CHUNK_VERSION, SCENEOBJ_VERSION);
	w.open_chunk(SCENEOBJ_CHUNK_REFERENCE);
	w.w_u32(m_file_version);
	w.w_u32(0);
	w.w_sz(m_reference);
	w.close_chunk();
	w.w_chunk<uint32_t>(SCENEOBJ_CHUNK_FLAGS, m_flags);
}

////////////////////////////////////////////////////////////////////////////////

xr_scene_visuals::xr_scene_visuals(xr_scene& scene):
	xr_scene_objects(scene, "scene_object.part", SCENE_CHUNK_SCENE_OBJECTS),
	m_flags(0)
{
	m_min_scale.set(1.f, 1.f, 1.f);
	m_max_scale.set(1.f, 1.f, 1.f);
	m_min_rotate.set();
	m_max_rotate.set();
}

xr_scene_visuals::~xr_scene_visuals() {}

void xr_scene_visuals::load(xr_reader& r)
{
	uint16_t version = 0;
	r.r_chunk<uint16_t>(TOOLS_CHUNK_VERSION, version);
	xr_assert(version == 0);
	xr_scene_objects::load(r);
	r.r_chunk<uint32_t>(SCENEOBJS_CHUNK_COMMON_FLAGS, m_flags);
	if (r.find_chunk(SCENEOBJS_CHUNK_COMMON_PARAMS)) {
		r.r_fvector3(m_min_scale);
		r.r_fvector3(m_max_scale);
		r.r_fvector3(m_min_rotate);
		r.r_fvector3(m_max_rotate);
		r.r_seq(r.r_u32(), m_snap_objects, [](std::string& s, xr_reader& r) { r.r_sz(s); });
	}
}

void xr_scene_visuals::save(xr_writer& w) const
{
	xr_scene_objects::save(w);

	w.w_chunk<uint16_t>(TOOLS_CHUNK_VERSION, 0);
	w.w_chunk<uint32_t>(SCENEOBJS_CHUNK_COMMON_FLAGS, m_flags);

	w.open_chunk(SCENEOBJS_CHUNK_COMMON_PARAMS);
	w.w_fvector3(m_min_scale);
	w.w_fvector3(m_max_scale);
	w.w_fvector3(m_min_rotate);
	w.w_fvector3(m_max_rotate);
	w.w_size_u32(m_snap_objects.size());
	w.w_seq(m_snap_objects, [](const std::string& s, xr_writer& w) { w.w_sz(s); });
	w.close_chunk();
}

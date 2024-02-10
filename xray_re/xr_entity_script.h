#ifndef __GNUC__
#pragma once
#endif
#ifndef __XR_ENTITY_SCRIPT_H__
#define __XR_ENTITY_SCRIPT_H__

#include "xr_entity.h"

namespace xray_re {

class se_artefact: public cse_alife_item_artefact {};
class se_car: public cse_alife_car {};
class se_heli: public cse_alife_helicopter {};
class se_weapon: public cse_alife_item_weapon {};
class se_weapon_shotgun: public cse_alife_item_weapon_shotgun {};
class se_weapon_magazined: public cse_alife_item_weapon_magazined {};
class se_weapon_magazined_w_gl: public cse_alife_item_weapon_magazined_w_gl {};
class se_item: public cse_alife_item {};
class se_item_torch: public cse_alife_item_torch {};
class se_outfit: public cse_alife_item_custom_outfit {};
class se_physic: public cse_alife_object_physic {};
class se_restrictor: public cse_alife_space_restrictor {};
class se_trader: public cse_alife_trader {};
class se_smart_cover: public cse_smart_cover {};

class se_actor: public cse_alife_creature_actor {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
};

class se_monster: public cse_alife_monster_base {
public:
			se_monster();
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
protected:
	uint8_t		m_job_online;
	bool		m_was_in_smart_terrain;

	// clear sky additions
	std::string	m_squad_id;
	bool		m_sim_forced_online;
};

class se_stalker: public cse_alife_human_stalker {
public:
			se_stalker();
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
protected:
	uint8_t		m_job_online;
	bool		m_was_in_smart_terrain;
	bool		m_death_dropped;

	// clear sky additions
	std::string	m_squad_id;
	bool		m_sim_forced_online;
};

class se_respawn: public cse_alife_smart_zone {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
protected:
	std::vector<uint16_t>	m_spawned_obj;
};

class se_smart_terrain: public cse_alife_smart_zone {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
};

class se_sim_faction: public cse_alife_smart_zone {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
};

class se_zone_anom: public cse_alife_anomalous_zone {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
};

class se_zone_visual: public cse_alife_zone_visual {
public:
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
};

class se_level_changer: public cse_alife_level_changer {
public:
			se_level_changer();
	virtual void	state_read(xr_packet& packet, uint16_t size);
	virtual void	state_write(xr_packet& packet);
protected:
	bool		m_enabled;
	std::string	m_hint;
};

} // end of namespace xray_re

#endif

/* $Id$ */
/*
   Copyright (C) 2004 - 2007 by Philippe Plantier <ayin@anathas.org>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include "global.hpp"

#include "array.hpp"
#include "builder.hpp"
#include "config.hpp"
#include "log.hpp"
#include "pathutils.hpp"
#include "terrain.hpp"
#include "util.hpp"
#include "wassert.hpp"
#include "serialization/string_utils.hpp"
#include <climits>

#define ERR_NG LOG_STREAM(err, engine)
#define DEBUG_NG LOG_STREAM(info, engine) 

/** The tile width used when using basex and basey. This is not,
 * necessarily, the tile width in pixels, this is totally
 * arbitrary. However, it will be set to 72 for convenience.
 */
static const int TILEWIDTH = 72;
/** The position of unit graphics in a tile. Graphics whose y
 * position is below this value are considered background for
 * this tile; graphics whose y position is above this value are
 * considered foreground.
 */
static const int UNITPOS = 36 + 18;
/** The allowed interval for the base-y position. The possible values are from 
 * -BASE_Y_INTERVAL to BASE_Y_INTERVAL-1
 */
static const int BASE_Y_INTERVAL = 100000;

terrain_builder::rule_image::rule_image(int layer, int x, int y, bool global_image) :
	layer(layer), basex(x), basey(y), global_image(global_image)
{}

terrain_builder::tile::tile() : last_tod("invalid_tod")
{
	memset(adjacents, 0, sizeof(adjacents));
}

void terrain_builder::tile::add_image_to_cache(const std::string &tod, ordered_ri_list::const_iterator itor)
{
	rule_image_variantlist::const_iterator tod_variant =
		itor->second->variants.find(tod);

	if(tod_variant == itor->second->variants.end())
		tod_variant = itor->second->variants.find("");

	if(tod_variant != itor->second->variants.end()) {
		//calculate original y-value and layer from list index
		int layer = itor->first / BASE_Y_INTERVAL;
		int basey = itor->first % BASE_Y_INTERVAL;

		if (basey < 0)
			basey += BASE_Y_INTERVAL/2;
		else
			basey -= BASE_Y_INTERVAL/2;

		if(layer < 0 || (layer == 0 && basey < UNITPOS)) {
			images_background.push_back(tod_variant->second.image);
		} else {
			images_foreground.push_back(tod_variant->second.image);
		}
	}
}

void terrain_builder::tile::rebuild_cache(const std::string &tod)
{
	images_background.clear();
	images_foreground.clear();

	ordered_ri_list::const_iterator itor;
	for(itor = images.begin(); itor != images.end(); ++itor) {
		add_image_to_cache(tod, itor);
	}
}

void terrain_builder::tile::clear()
{
	flags.clear();
	images.clear();
	images_foreground.clear();
	images_background.clear();
	last_tod = "invalid_tod";
	memset(adjacents, 0, sizeof(adjacents));
}

void terrain_builder::tilemap::reset()
{
	for(std::vector<tile>::iterator it = map_.begin(); it != map_.end(); ++it)
		it->clear();
}

bool terrain_builder::tilemap::on_map(const gamemap::location &loc) const
{
	if(loc.x < -1 || loc.y < -1 || loc.x > x_ || loc.y > y_)
		return false;

	return true;
}

terrain_builder::tile& terrain_builder::tilemap::operator[](const gamemap::location &loc)
{
	wassert(on_map(loc));

	return map_[(loc.x+1) + (loc.y+1)*(x_+2)];
}

const terrain_builder::tile& terrain_builder::tilemap::operator[] (const gamemap::location &loc) const
{
	wassert(on_map(loc));

	return map_[(loc.x+1) + (loc.y+1)*(x_+2)];
}

terrain_builder::terrain_builder(const config& cfg, const config& level, 
	const gamemap& map, const std::string& offmap_image) :
		map_(map), tile_map_(map.w(), map.h())
{
	// make sure there's nothing left in the cache since it might
	// give problems
	image::flush_cache();

	parse_config(cfg);
	parse_config(level);
	add_off_map_rule(offmap_image);

	build_terrains();
}

const terrain_builder::imagelist *terrain_builder::get_terrain_at(const gamemap::location &loc,
		const std::string &tod, const ADJACENT_TERRAIN_TYPE terrain_type)
{
	if(!tile_map_.on_map(loc))
		return NULL;

	tile& tile_at = tile_map_[loc];

	if(tod != tile_at.last_tod) {
		tile_at.rebuild_cache(tod);
		tile_at.last_tod = tod;
	}

	if(terrain_type == ADJACENT_BACKGROUND) {
		if(!tile_at.images_background.empty())
			return &tile_at.images_background;
	}

	if(terrain_type == ADJACENT_FOREGROUND) {
		if(!tile_at.images_foreground.empty())
			return &tile_at.images_foreground;
	}

	return NULL;
}

bool terrain_builder::update_animation(const gamemap::location &loc)
{
	if(!tile_map_.on_map(loc))
		return false;

	imagelist& bg = tile_map_[loc].images_background;
	imagelist& fg = tile_map_[loc].images_foreground;
	bool changed = false;

	imagelist::iterator itor = bg.begin();
	for(; itor != bg.end(); ++itor) {
		if(itor->need_update())
			changed = true;
		itor->update_last_draw_time();
	}

	itor = fg.begin();
	for(; itor != fg.end(); ++itor) {
		if(itor->need_update())
			changed = true;
		itor->update_last_draw_time();
	}

	return changed;
}

// TODO: rename this function
void terrain_builder::rebuild_terrain(const gamemap::location &loc)
{
	if (tile_map_.on_map(loc)) {
		tile& btile = tile_map_[loc];
		// btile.images.clear();
		btile.images_foreground.clear();
		btile.images_background.clear();
		const std::string filename =
			map_.get_terrain_info(map_.get_terrain(loc)).symbol_image();
		animated<image::locator> img_loc;
		img_loc.add_frame(100,image::locator("terrain/" + filename + ".png"));
		img_loc.start_animation(0, true);
		btile.images_background.push_back(img_loc);
	}
}

void terrain_builder::rebuild_all()
{
	tile_map_.reset();
	terrain_by_type_.clear();
	terrain_by_type_border_.clear();
	build_terrains();
}

bool terrain_builder::rule_valid(const building_rule &rule) const
{
	//if the rule has no constraints, it is invalid
	if(rule.constraints.empty())
		return false;

	//checks if all the images referenced by the current rule are valid.
	//if not, this rule will not match.
	rule_imagelist::const_iterator image;
	constraint_set::const_iterator constraint;
	rule_image_variantlist::const_iterator variant;

	for(constraint = rule.constraints.begin();
			constraint != rule.constraints.end(); ++constraint) {
		for(image = constraint->second.images.begin();
				image != constraint->second.images.end();
				++image) {

			for(variant = image->variants.begin(); variant != image->variants.end(); ++variant) {
				std::string s = variant->second.image_string;
				s = s.substr(0, s.find_first_of(",:"));

				if(!image::exists("terrain/" + s + ".png"))
					return false;
			}
		}
	}

	return true;
}

bool terrain_builder::start_animation(building_rule &rule)
{
	rule_imagelist::iterator image;
	constraint_set::iterator constraint;
	rule_image_variantlist::iterator variant;

	for(constraint = rule.constraints.begin();
			constraint != rule.constraints.end(); ++constraint) {

		for(image = constraint->second.images.begin();
				image != constraint->second.images.end();
				++image) {

			for(variant = image->variants.begin(); variant != image->variants.end(); ++variant) {

				animated<image::locator>::anim_description image_vector;
				std::vector<std::string> items = utils::split(variant->second.image_string);
				std::vector<std::string>::const_iterator itor = items.begin();
				for(; itor != items.end(); ++itor) {
					const std::vector<std::string>& items = utils::split(*itor, ':');
					std::string str;
					int time;

					if(items.size() > 1) {
						str = items.front();
						time = atoi(items.back().c_str());
					} else {
						str = *itor;
						time = 100;
					}
					if(image->global_image) {
						image_vector.push_back(animated<image::locator>::frame_description(time,image::locator("terrain/" + str + ".png",constraint->second.loc)));
					} else {
						image_vector.push_back(animated<image::locator>::frame_description(time,image::locator("terrain/" + str + ".png")));
					}

				}

				animated<image::locator> th(image_vector);

				variant->second.image = th;
				variant->second.image.start_animation(0, true);
			}
		}
	}

	return true;
}

terrain_builder::terrain_constraint terrain_builder::rotate(const terrain_builder::terrain_constraint &constraint, int angle)
{
	static const struct { int ii; int ij; int ji; int jj; }  rotations[6] =
		{ {  1, 0, 0,  1 }, {  1,  1, -1, 0 }, { 0,  1, -1, -1 },
		  { -1, 0, 0, -1 }, { -1, -1,  1, 0 }, { 0, -1,  1,  1 } };

	// The following array of matrices is intended to rotate the (x,y)
	// coordinates of a point in a wesnoth hex (and wesnoth hexes are not
	// regular hexes :) ).
	// The base matrix for a 1-step rotation with the wesnoth tile shape
	// is:
	//
	// r = s^-1 * t * s
	//
	// with s = [[ 1   0         ]
	//           [ 0   -sqrt(3)/2 ]]
	//
	// and t =  [[ -1/2       sqrt(3)/2 ]
	//           [ -sqrt(3)/2  1/2        ]]
	//
	// With t being the rotation matrix (pi/3 rotation), and s a matrix
	// that transforms the coordinates of the wesnoth hex to make them
	// those of a regular hex.
	//
	// (demonstration left as an exercise for the reader)
	//
	// So we have
	//
	// r = [[ 1/2  -3/4 ]
	//      [ 1    1/2  ]]
	//
	// And the following array contains I(2), r, r^2, r^3, r^4, r^5 (with
	// r^3 == -I(2)), which are the successive rotations.
	static const struct {
		double xx;
		double xy;
		double yx;
		double yy;
	} xyrotations[6] = {
		{ 1.,         0.,  0., 1.    },
		{ 1./2. , -3./4.,  1., 1./2. },
		{ -1./2., -3./4.,   1, -1./2.},
		{ -1.   ,     0.,  0., -1.   },
		{ -1./2.,  3./4., -1., -1./2.},
		{ 1./2. ,  3./4., -1., 1./2. },
	};

	wassert(angle >= 0);

	angle %= 6;
	terrain_constraint ret = constraint;

	// Vector i is going from n to s, vector j is going from ne to sw.
	int vi = ret.loc.y - ret.loc.x/2;
	int vj = ret.loc.x;

	int ri = rotations[angle].ii * vi + rotations[angle].ij * vj;
	int rj = rotations[angle].ji * vi + rotations[angle].jj * vj;

	ret.loc.x = rj;
	ret.loc.y = ri + (rj >= 0 ? rj/2 : (rj-1)/2);

	for (rule_imagelist::iterator itor = ret.images.begin();
			itor != ret.images.end(); ++itor) {

		double vx, vy, rx, ry;

		vx = double(itor->basex) - double(TILEWIDTH)/2;
		vy = double(itor->basey) - double(TILEWIDTH)/2;

		rx = xyrotations[angle].xx * vx + xyrotations[angle].xy * vy;
		ry = xyrotations[angle].yx * vx + xyrotations[angle].yy * vy;

		itor->basex = int(rx + TILEWIDTH/2);
		itor->basey = int(ry + TILEWIDTH/2);

		//std::cerr << "Rotation: from " << vx << ", " << vy << " to " << itor->basex <<
		//	", " << itor->basey << "\n";
	}

	return ret;
}

void terrain_builder::replace_token(std::string &s, const std::string &token, const std::string &replacement)
{
	size_t pos;

	if(token.empty()) {
		ERR_NG << "empty token in replace_token\n";
		return;
	}
	while((pos = s.find(token)) != std::string::npos) {
		s.replace(pos, token.size(), replacement);
	}
}

void terrain_builder::replace_token(terrain_builder::rule_image &image, const std::string &token, const std::string &replacement)
{
	rule_image_variantlist::iterator itor;

	for(itor = image.variants.begin(); itor != image.variants.end(); ++itor) {
		replace_token(itor->second, token, replacement);
	}
}

void terrain_builder::replace_token(terrain_builder::rule_imagelist &list, const std::string &token, const std::string &replacement)
{
	rule_imagelist::iterator itor;

	for(itor = list.begin(); itor != list.end(); ++itor) {
		replace_token(*itor, token, replacement);
	}
}

void terrain_builder::replace_token(terrain_builder::building_rule &rule, const std::string &token, const std::string& replacement)
{
	constraint_set::iterator cons;

	for(cons = rule.constraints.begin(); cons != rule.constraints.end(); ++cons) {
		//Transforms attributes
		std::vector<std::string>::iterator flag;

		for(flag = cons->second.set_flag.begin(); flag != cons->second.set_flag.end(); flag++) {
			replace_token(*flag, token, replacement);
		}
		for(flag = cons->second.no_flag.begin(); flag != cons->second.no_flag.end(); flag++) {
			replace_token(*flag, token, replacement);
		}
		for(flag = cons->second.has_flag.begin(); flag != cons->second.has_flag.end(); flag++) {
			replace_token(*flag, token, replacement);
		}
		replace_token(cons->second.images, token, replacement);
	}

	//replace_token(rule.images, token, replacement);
}

terrain_builder::building_rule terrain_builder::rotate_rule(const terrain_builder::building_rule &rule,
	int angle, const std::vector<std::string>& rot)
{
	building_rule ret;
	if(rot.size() != 6) {
		ERR_NG << "invalid rotations\n";
		return ret;
	}
	ret.location_constraints = rule.location_constraints;
	ret.probability = rule.probability;
	ret.precedence = rule.precedence;

	constraint_set tmp_cons;
	constraint_set::const_iterator cons;
	for(cons = rule.constraints.begin(); cons != rule.constraints.end(); ++cons) {
		const terrain_constraint &rcons = rotate(cons->second, angle);

		tmp_cons[rcons.loc] = rcons;
	}

	// Normalize the rotation, so that it starts on a positive location
	int minx = INT_MAX;
	int miny = INT_MAX;

	constraint_set::iterator cons2;
	for(cons2 = tmp_cons.begin(); cons2 != tmp_cons.end(); ++cons2) {
		minx = minimum<int>(cons2->second.loc.x, minx);
		miny = minimum<int>(2*cons2->second.loc.y + (cons2->second.loc.x & 1), miny);
	}

	if((miny & 1) && (minx & 1) && (minx < 0))
		miny += 2;
	if(!(miny & 1) && (minx & 1) && (minx > 0))
		miny -= 2;

	for(cons2 = tmp_cons.begin(); cons2 != tmp_cons.end(); ++cons2) {
		//Adjusts positions
		cons2->second.loc += gamemap::location(-minx, -((miny-1)/2));
		ret.constraints[cons2->second.loc] = cons2->second;
	}

	for(int i = 0; i < 6; ++i) {
		int a = (angle+i) % 6;
		std::string token = "@R";
		push_back(token,'0' + i);
		replace_token(ret, token, rot[a]);
	}

	return ret;
}

void terrain_builder::add_images_from_config(rule_imagelist& images, const config &cfg, bool global, int dx, int dy)
{
	const config::child_list& cimages = cfg.get_children("image");


	for(config::child_list::const_iterator img = cimages.begin(); img != cimages.end(); ++img) {

		const std::string &name = (**img)["name"];
		const int layer = lexical_cast_default<int>((**img)["layer"], 0);

		int basex = 0, basey = 0;
		if((**img)["base"].empty()) {
			basex = TILEWIDTH / 2 + dx;
			basey = TILEWIDTH / 2 + dy;
		} else {
			std::vector<std::string> base = utils::split((**img)["base"]);

			if(base.size() >= 2) {
				basex = atoi(base[0].c_str());
				basey = atoi(base[1].c_str());
			}
		}

		images.push_back(rule_image(layer, basex - dx, basey - dy, global));


		// Adds the main (default) variant of the image, if present
		images.back().variants.insert(std::pair<std::string, rule_image_variant>("", rule_image_variant(name,"")));

		// Adds the other variants of the image
		const config::child_list& variants = (**img).get_children("variant");

		for(config::child_list::const_iterator variant = variants.begin();
				variant != variants.end(); ++variant) {
			const std::string &name = (**variant)["name"];
			const std::string &tod = (**variant)["tod"];

			images.back().variants.insert(std::pair<std::string, rule_image_variant>(tod, rule_image_variant(name,tod)));

		}
	}
}

void terrain_builder::add_constraints(
		terrain_builder::constraint_set& constraints,
		const gamemap::location& loc,
		const t_translation::t_match& type, const config& global_images)
{
	if(constraints.find(loc) == constraints.end()) {
		//the terrain at the current location did not exist, so create it
		constraints[loc] = terrain_constraint(loc);
	}

	if(!type.terrain.empty()) {
		constraints[loc].terrain_types_match = type;
	}

	int x = loc.x * TILEWIDTH * 3 / 4;
	int y = loc.y * TILEWIDTH + (loc.x % 2) * TILEWIDTH / 2;
	add_images_from_config(constraints[loc].images, global_images, true, x, y);
}

void terrain_builder::add_constraints(terrain_builder::constraint_set &constraints, 
		const gamemap::location& loc, const config& cfg, const config& global_images)

{
	add_constraints(constraints, loc, t_translation::t_match(cfg["type"], t_translation::WILDCARD), global_images);

	terrain_constraint& constraint = constraints[loc];

	std::vector<std::string> item_string = utils::split(cfg["set_flag"]);
	constraint.set_flag.insert(constraint.set_flag.end(),
			item_string.begin(), item_string.end());

	item_string = utils::split(cfg["has_flag"]);
	constraint.has_flag.insert(constraint.has_flag.end(),
			item_string.begin(), item_string.end());

	item_string = utils::split(cfg["no_flag"]);
	constraint.no_flag.insert(constraint.no_flag.end(),
			item_string.begin(), item_string.end());

	add_images_from_config(constraint.images, cfg, false);
}

void terrain_builder::parse_mapstring(const std::string &mapstring,
		struct building_rule &br, anchormap& anchors,
		const config& global_images)
{

	const t_translation::t_map map = t_translation::read_builder_map(mapstring);
	
	// if there is an empty map leave directly
	// determine after conversion, since a non empty
	// string can return an empty map
	if(map.empty()) {
		return;
	}

	int lineno = (map[0][0] == t_translation::NONE_TERRAIN) ? 1 : 0;
	int x = lineno;
	int y = 0;
	for(size_t y_off = 0; y_off < map.size(); ++y_off) {
		for(size_t x_off = x; x_off < map[y_off].size(); ++x_off) {

			const t_translation::t_letter terrain = map[y_off][x_off];

			if(terrain.base == t_translation::TB_DOT) {
				// Dots are simple placeholders, which do not
				// represent actual terrains.
			} else if (terrain.overlay != 0 ) {
				anchors.insert(std::pair<int, gamemap::location>(terrain.overlay, gamemap::location(x, y)));
			} else if (terrain.base == t_translation::TB_STAR) {
				add_constraints(br.constraints, gamemap::location(x, y), t_translation::STAR, global_images);
			} else {
					ERR_NG << "Invalid terrain (" << t_translation::write_letter(terrain) << ") in builder map\n";
					wassert(false);
			}
		x += 2;
		}

		if(lineno % 2 == 1) {
			++y;
			x = 0;
		} else {
			x = 1;
		}
		++lineno;
	}
}

void terrain_builder::add_rule(building_ruleset& rules, building_rule &rule)
{
	if(rule_valid(rule)) {
		start_animation(rule);
		rules.insert(std::pair<int, building_rule>(rule.precedence, rule));
	}
}

void terrain_builder::add_rotated_rules(building_ruleset& rules, building_rule& tpl, const std::string &rotations) 
{
	if(rotations.empty()) {
		// Adds the parsed built terrain to the list

		add_rule(rules, tpl);
	} else {
		const std::vector<std::string>& rot = utils::split(rotations, ',');

		for(size_t angle = 0; angle < rot.size(); angle++) {
			building_rule rule = rotate_rule(tpl, angle, rot);
			add_rule(rules, rule);
		}
	}
}

void terrain_builder::parse_config(const config &cfg)
{
	log_scope("terrain_builder::parse_config");

	//Parses the list of building rules (BRs)
	const config::child_list& brs = cfg.get_children("terrain_graphics");

	for(config::child_list::const_iterator br = brs.begin(); br != brs.end(); ++br) {
		building_rule pbr; // Parsed Building rule

		// add_images_from_config(pbr.images, **br);

		if(!((**br)["x"].empty() || (**br)["y"].empty()))
			pbr.location_constraints = gamemap::location(atoi((**br)["x"].c_str()), atoi((**br)["y"].c_str()));

		pbr.probability = (**br)["probability"].empty() ? -1 : atoi((**br)["probability"].c_str());
		pbr.precedence = (**br)["precedence"].empty() ? 0 : atoi((**br)["precedence"].c_str());

		//Mapping anchor indices to anchor locations.
		anchormap anchors;

		// Parse the map= , if there is one (and fill the anchors list)
		parse_mapstring((**br)["map"], pbr, anchors, **br);

		// Parses the terrain constraints (TCs)
		config::child_list tcs((*br)->get_children("tile"));

		for(config::child_list::const_iterator tc = tcs.begin(); tc != tcs.end(); tc++) {
			//Adds the terrain constraint to the current built
			//terrain's list of terrain constraints, if it does not
			//exist.
			gamemap::location loc;
			if((**tc)["x"].size()) {
				loc.x = atoi((**tc)["x"].c_str());
			}
			if((**tc)["y"].size()) {
				loc.y = atoi((**tc)["y"].c_str());
			}
			if(!(**tc)["loc"].empty()) {
				std::vector<std::string> sloc = utils::split((**tc)["loc"]);
				if(sloc.size() == 2) {
					loc.x = atoi(sloc[0].c_str());
					loc.y = atoi(sloc[1].c_str());
				}
			}
			if(loc.valid()) {
				add_constraints(pbr.constraints, loc, **tc, **br);
			}
			if((**tc)["pos"].size()) {
				int pos = atoi((**tc)["pos"].c_str());
				if(anchors.find(pos) == anchors.end()) {
					LOG_STREAM(warn, engine) << "Invalid anchor!\n";
					continue;
				}

				std::pair<anchormap::const_iterator, anchormap::const_iterator> range =
					anchors.equal_range(pos);

				for(; range.first != range.second; range.first++) {
					loc = range.first->second;
					add_constraints(pbr.constraints, loc, **tc, **br);
				}
			}
		}

		const std::string global_set_flag = (**br)["set_flag"];
		const std::string global_no_flag = (**br)["no_flag"];
		const std::string global_has_flag = (**br)["has_flag"];

		for(constraint_set::iterator constraint = pbr.constraints.begin(); constraint != pbr.constraints.end();
		    constraint++) {

			if(global_set_flag.size())
				constraint->second.set_flag.push_back(global_set_flag);

			if(global_no_flag.size())
				constraint->second.no_flag.push_back(global_no_flag);

			if(global_has_flag.size())
				constraint->second.has_flag.push_back(global_has_flag);

		}

		// Handles rotations
		const std::string rotations = (**br)["rotations"];

		add_rotated_rules(building_rules_, pbr, rotations);

	}

// debug output for the terrain rules	
#if 0 
	std::cerr << "Built terrain rules: \n";

	building_ruleset::const_iterator rule;
	for(rule = building_rules_.begin(); rule != building_rules_.end(); ++rule) {
		std::cerr << ">> New rule: image_background = " /* << rule->second.image_background << " , image_foreground = "<< rule->second.image_foreground */ << "\n";
		for(constraint_set::const_iterator constraint = rule->second.constraints.begin();
		    constraint != rule->second.constraints.end(); ++constraint) {

			std::cerr << ">>>> New constraint: location = (" << constraint->second.loc
			          << "), terrain types = '" << t_translation::write_list(constraint->second.terrain_types_match.terrain) << "'\n";

			std::vector<std::string>::const_iterator flag;

			for(flag  = constraint->second.set_flag.begin(); flag != constraint->second.set_flag.end(); ++flag) {
				std::cerr << ">>>>>> Set_flag: " << *flag << "\n";
			}

			for(flag = constraint->second.no_flag.begin(); flag != constraint->second.no_flag.end(); ++flag) {
				std::cerr << ">>>>>> No_flag: " << *flag << "\n";
			}
		}

	}
#endif

}

void terrain_builder::add_off_map_rule(const std::string& image)
{
	// build a config object
	config cfg;

	cfg.add_child("terrain_graphics");
	config *item = cfg.child("terrain_graphics");

	(*item).add_child("tile");
	config *tile = (*item).child("tile");
	(*tile)["x"] = "0";
	(*tile)["y"] = "0";
	(*tile)["type"] = t_translation::write_letter(t_translation::OFF_MAP_USER);

	(*tile).add_child("image");
	config *tile_image = (*tile).child("image");
	(*tile_image)["layer"] = "-1000";
	(*tile_image)["name"] = image;

	(*item)["probability"] = "100";
	(*item)["no_flag"] = "base";
	(*item)["set_flag"] = "base";

	// parse the object
	parse_config(cfg);
}

bool terrain_builder::rule_matches(const terrain_builder::building_rule &rule, 
		const gamemap::location &loc, const int rule_index, const bool check_loc) const
{
	if(rule.location_constraints.valid() && rule.location_constraints != loc) {
		return false;
	}

	if(check_loc) {
		for(constraint_set::const_iterator cons = rule.constraints.begin();
				cons != rule.constraints.end(); ++cons) {

			// translated location
			const gamemap::location tloc = loc + cons->second.loc;

			if(!tile_map_.on_map(tloc)) {
				return false;
			}
            //std::cout << "testing..." << builder_letter(map_.get_terrain(tloc))
			if(!terrain_matches(map_.get_terrain(tloc), cons->second.terrain_types_match)) {
				return false;
			}
		}
	}

	if(rule.probability != -1) {
		unsigned int a = (loc.x + 92872973) ^ 918273;
		unsigned int b = (loc.y + 1672517) ^ 128123;
		unsigned int c = (rule_index + 127390) ^ 13923787;
		unsigned int random = a*b*c + a*b + b*c + a*c + a + b + c;

		random %= 100;

		if(random > (unsigned int)rule.probability) {
			return false;
		}
	}

	for(constraint_set::const_iterator cons = rule.constraints.begin();
			cons != rule.constraints.end(); ++cons) {

		const gamemap::location tloc = loc + cons->second.loc;

		if(!tile_map_.on_map(tloc)) {
			return false;
		}
		const tile& btile = tile_map_[tloc];

		std::vector<std::string>::const_iterator itor;
		for(itor = cons->second.no_flag.begin(); itor != cons->second.no_flag.end(); ++itor) {

			//If a flag listed in "no_flag" is present, the rule does not match
			if(btile.flags.find(*itor) != btile.flags.end()) {
				return false;
			}
		}
		for(itor = cons->second.has_flag.begin(); itor != cons->second.has_flag.end(); ++itor) {

			//If a flag listed in "has_flag" is not present, this rule does not match
			if(btile.flags.find(*itor) == btile.flags.end()) {
				return false;
			}
		}
	}

	return true;
}

void terrain_builder::apply_rule(const terrain_builder::building_rule &rule, const gamemap::location &loc)
{
	for(constraint_set::const_iterator constraint = rule.constraints.begin();
			constraint != rule.constraints.end(); ++constraint) {

		rule_imagelist::const_iterator img;
		const gamemap::location tloc = loc + constraint->second.loc;
		if(!tile_map_.on_map(tloc))
			return;

		tile& btile = tile_map_[tloc];

		// We want to order the images by layer first and base-y second, so we sort by 
		// layer*BASE_Y_INTERVAL + BASE_Y_INTERVAL/2 + basey
		// Thus, allowed values for basey are from -50000 to 49999
		for(img = constraint->second.images.begin(); img != constraint->second.images.end(); ++img) {
			btile.images.insert(std::pair<int, const rule_image*>(
									img->layer*BASE_Y_INTERVAL + BASE_Y_INTERVAL/2 + img->basey, &*img));
		}

		// Sets flags
		for(std::vector<std::string>::const_iterator itor = constraint->second.set_flag.begin();
				itor != constraint->second.set_flag.end(); itor++) {
			btile.flags.insert(*itor);
		}

	}
}

int terrain_builder::get_constraint_adjacents(const building_rule& rule, const gamemap::location& loc)
{
	int res = 0;

	gamemap::location adj[6];
	int i;
	get_adjacent_tiles(loc, adj);

	for(i = 0; i < 6; ++i) {
		if(rule.constraints.find(adj[i]) != rule.constraints.end()) {
			res++;
		}
	}
	return res;
}

//returns the "size" of a constraint: that is, the number of map tiles on which
//this constraint may possibly match. INT_MAX means "I don't know / all of them".
int terrain_builder::get_constraint_size(const building_rule& rule, const terrain_constraint& constraint, bool& border)
{
	const t_translation::t_list& types = constraint.terrain_types_match.terrain;

	if(types.empty()) {
		return INT_MAX;
	}
	if(types.front() == t_translation::NOT) {
		return INT_MAX;
	}
	if(std::find(types.begin(), types.end(), t_translation::STAR) != types.end()) { 
		return INT_MAX;
	}
	// as soon as the list has 1 wildcard we bail out
	// it might be better to try some more testing
	// before bailing out.
	if(constraint.terrain_types_match.has_wildcard) {
		return INT_MAX;
	}

	gamemap::location adj[6];
	get_adjacent_tiles(constraint.loc, adj);

	border = false;

	//if the current constraint only applies to a non-isolated tile,
	//the "border" flag can be set.
	for(int i = 0; i < 6; ++i) {
		if(rule.constraints.find(adj[i]) != rule.constraints.end()) {
			const t_translation::t_list& atypes = 
				rule.constraints.find(adj[i])->second.terrain_types_match.terrain;
			
			t_translation::t_list::const_iterator itor = types.begin();
			for(; itor != types.end(); ++itor) {
				if(!terrain_matches(*itor, atypes)) { 
					border = true;
					break;
				}

			}
		}
		if(border == true) {
			break;
		}
	}

	int constraint_size = 0;

	for(t_translation::t_list::const_iterator itor = types.begin();
			itor != types.end(); ++itor) {
		if(border) {
			constraint_size += terrain_by_type_border_[*itor].size();
		} else {
			constraint_size += terrain_by_type_[*itor].size();
		}
	}

	return constraint_size;
}

void terrain_builder::build_terrains()
{
	log_scope("terrain_builder::build_terrains");

	//builds the terrain_by_type_ cache
	for(int x = -1; x <= map_.w(); ++x) {
		for(int y = -1; y <= map_.h(); ++y) {
			const gamemap::location loc(x,y);
			const t_translation::t_letter t = map_.get_terrain(loc);

			terrain_by_type_[t].push_back(loc);
			gamemap::location adj[6];
			int i;
			bool border = false;

			get_adjacent_tiles(loc, adj);

			tile_map_[loc].adjacents[0] = t;
			for(i = 0; i < 6; ++i) {
				//updates the list of adjacents for this tile
				tile_map_[loc].adjacents[i+1] = map_.get_terrain(adj[i]);

				//determines if this tile is a border tile
				if(map_.get_terrain(adj[i]) != t) {
					border = true;
				}

			}
			if(border)
				terrain_by_type_border_[t].push_back(loc);
		}
	}

	int rule_index = 0;
	building_ruleset::const_iterator rule;
	
	for(rule = building_rules_.begin(); rule != building_rules_.end(); ++rule) {

		if (rule->second.location_constraints.valid()) {
			apply_rule(rule->second, rule->second.location_constraints);
			continue;
		}

		constraint_set::const_iterator constraint;

		//find the constraint that contains the less terrain of all terrain rules.
		constraint_set::const_iterator smallest_constraint;
		constraint_set::const_iterator constraint_most_adjacents;
		int smallest_constraint_size = INT_MAX;
		int biggest_constraint_adjacent = -1;
		bool smallest_constraint_border = false;

		for(constraint = rule->second.constraints.begin();
		    constraint != rule->second.constraints.end(); ++constraint) {

			bool border;
			int size = get_constraint_size(rule->second, constraint->second, border);
			if(size < smallest_constraint_size) {
				smallest_constraint_size = size;
				smallest_constraint = constraint;
				smallest_constraint_border = border;
			}

			int nadjacents = get_constraint_adjacents(rule->second, constraint->second.loc);
			if(nadjacents > biggest_constraint_adjacent) {
				biggest_constraint_adjacent = nadjacents;
				constraint_most_adjacents = constraint;
			}
		}

		util::array<t_translation::t_list, 7> adjacent_types;

		if(biggest_constraint_adjacent > 0) {
			gamemap::location loc[7];
			loc[0] = constraint_most_adjacents->second.loc;
			get_adjacent_tiles(loc[0], loc+1);
			for(int i = 0; i < 7; ++i) {
				constraint_set::const_iterator cons = rule->second.constraints.find(loc[i]) ;
				if(cons != rule->second.constraints.end()) {
					adjacent_types[i] = cons->second.terrain_types_match.terrain;
				} else {
					adjacent_types[i] = t_translation::read_list("", t_translation::WILDCARD);
				}
			}

		}
		if(smallest_constraint_size != INT_MAX) {
			const t_translation::t_list& types = smallest_constraint->second.terrain_types_match.terrain;
			const gamemap::location loc = smallest_constraint->second.loc;
			const gamemap::location aloc = constraint_most_adjacents->second.loc;

			for(t_translation::t_list::const_iterator c = types.begin(); 
					c != types.end(); ++c) {

				const std::vector<gamemap::location>* locations;
				if(smallest_constraint_border) {
					locations = &terrain_by_type_border_[*c];
				} else {
					locations = &terrain_by_type_[*c];
				}

				for(std::vector<gamemap::location>::const_iterator itor = locations->begin();
						itor != locations->end(); ++itor) {

					if(biggest_constraint_adjacent > 0) {
						const gamemap::location pos = (*itor - loc) + aloc;
						if(!tile_map_.on_map(pos))
							continue;

						const t_translation::t_letter *adjacents = tile_map_[pos].adjacents;
						int i;

						for(i = 0; i < 7; ++i) {
							if(!terrain_matches(adjacents[i], adjacent_types[i])) {
								break;
							}
						}
						// propagates the break
						if (i < 7)
							continue;
					}

					if(rule_matches(rule->second, *itor - loc, rule_index, 
								(size_t)(biggest_constraint_adjacent + 1) != rule->second.constraints.size())) {
						apply_rule(rule->second, *itor - loc);
					}
				}
			}
		} else {
			// some overlays fail but (probably their map size) this is fixed by 
			// changing the start position from -1 to -2. So it's no real fix but
			// a hack, so still need to figure out the best number -- Mordante
			for(int x = -2; x <= map_.w(); ++x) {
				for(int y = -2; y <= map_.h(); ++y) {
					const gamemap::location loc(x,y);
					if(rule_matches(rule->second, loc, rule_index, true))
						apply_rule(rule->second, loc);
				}
			}
		}

		rule_index++;
	}
}

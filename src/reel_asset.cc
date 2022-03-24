/*
    Copyright (C) 2014-2022 Carl Hetherington <cth@carlh.net>

    This file is part of libdcp.

    libdcp is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    libdcp is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libdcp.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations
    including the two.

    You must obey the GNU General Public License in all respects
    for all of the code used other than OpenSSL.  If you modify
    file(s) with this exception, you may extend this exception to your
    version of the file(s), but you are not obligated to do so.  If you
    do not wish to do so, delete this exception statement from your
    version.  If you delete this exception statement from all source
    files in the program, then also delete it here.
*/


/** @file  src/reel_asset.cc
 *  @brief ReelAsset class
 */


#include "asset.h"
#include "compose.hpp"
#include "dcp_assert.h"
#include "raw_convert.h"
#include "reel_asset.h"
#include "warnings.h"
#include <libcxml/cxml.h>
LIBDCP_DISABLE_WARNINGS
#include <libxml++/libxml++.h>
LIBDCP_ENABLE_WARNINGS


using std::make_pair;
using std::pair;
using std::shared_ptr;
using std::string;
using boost::optional;
using namespace dcp;


ReelAsset::ReelAsset (string id, Fraction edit_rate, int64_t intrinsic_duration, optional<int64_t> entry_point)
	: Object (id)
	, _intrinsic_duration (intrinsic_duration)
	, _edit_rate (edit_rate)
	, _entry_point (entry_point)
{
	if (_entry_point) {
		_duration = intrinsic_duration - *_entry_point;
	}

	DCP_ASSERT (!_entry_point || *_entry_point <= _intrinsic_duration);
}


ReelAsset::ReelAsset (shared_ptr<const cxml::Node> node)
	: Object (remove_urn_uuid (node->string_child ("Id")))
	, _intrinsic_duration (node->number_child<int64_t> ("IntrinsicDuration"))
	, _duration (node->optional_number_child<int64_t>("Duration"))
	, _annotation_text (node->optional_string_child("AnnotationText"))
	, _edit_rate (Fraction (node->string_child ("EditRate")))
	, _entry_point (node->optional_number_child<int64_t>("EntryPoint"))
{

}


xmlpp::Node*
ReelAsset::write_to_cpl (xmlpp::Node* node, Standard standard) const
{
	auto a = node->add_child (cpl_node_name (standard));
	auto const attr = cpl_node_attribute (standard);
	if (!attr.first.empty ()) {
		a->set_attribute (attr.first, attr.second);
	}
	auto const ns = cpl_node_namespace ();
	if (!ns.first.empty()) {
		a->set_namespace_declaration (ns.first, ns.second);
	}
	a->add_child("Id")->add_child_text ("urn:uuid:" + _id);
	/* Empty <AnnotationText> tags cause refusal to play on some Sony SRX320 / LMT3000 systems (DoM bug #2124) */
	if (_annotation_text && !_annotation_text->empty()) {
		a->add_child("AnnotationText")->add_child_text(*_annotation_text);
	}
	a->add_child("EditRate")->add_child_text (_edit_rate.as_string());
	a->add_child("IntrinsicDuration")->add_child_text (raw_convert<string> (_intrinsic_duration));
	if (_entry_point) {
		a->add_child("EntryPoint")->add_child_text(raw_convert<string>(*_entry_point));
	}
	if (_duration) {
		a->add_child("Duration")->add_child_text(raw_convert<string>(*_duration));
	}
	return a;
}


pair<string, string>
ReelAsset::cpl_node_attribute (Standard) const
{
	return make_pair ("", "");
}


pair<string, string>
ReelAsset::cpl_node_namespace () const
{
	return make_pair ("", "");
}


template <class T>
string
optional_to_string (optional<T> o)
{
	return o ? raw_convert<string>(*o) : "[none]";
}


bool
ReelAsset::asset_equals (shared_ptr<const ReelAsset> other, EqualityOptions opt, NoteHandler note) const
{
	auto const node = cpl_node_name(Standard::SMPTE);

	if (_annotation_text != other->_annotation_text) {
		string const s = String::compose("Reel %1: annotation texts differ (%2 vs %3)", node, optional_to_string(_annotation_text), optional_to_string(other->_annotation_text));
		if (!opt.reel_annotation_texts_can_differ) {
			note (NoteType::ERROR, s);
			return false;
		} else {
			note (NoteType::NOTE, s);
		}
	}

	if (_edit_rate != other->_edit_rate) {
		note (
			NoteType::ERROR,
			String::compose("Reel %1: edit rates differ (%2 vs %3)", node, _edit_rate.as_string(), other->_edit_rate.as_string())
		     );
		return false;
	}

	if (_intrinsic_duration != other->_intrinsic_duration) {
		note (
			NoteType::ERROR,
			String::compose("Reel %1: intrinsic durations differ (%2 vs %3)", node, _intrinsic_duration, other->_intrinsic_duration)
		     );
		return false;
	}

	if (_entry_point != other->_entry_point) {
		note (
			NoteType::ERROR,
			String::compose("Reel %1: entry points differ (%2 vs %3)", node, optional_to_string(_entry_point), optional_to_string(other->_entry_point))
		     );
		return false;
	}

	if (_duration != other->_duration) {
		note (
			NoteType::ERROR,
			String::compose("Reel %1: durations differ (%2 vs %3)", node, optional_to_string(_duration), optional_to_string(other->_duration))
		     );
		return false;
	}

	return true;
}


int64_t
ReelAsset::actual_duration () const
{
	if (_duration) {
		return *_duration;
	}

	return _intrinsic_duration - _entry_point.get_value_or(0);
}

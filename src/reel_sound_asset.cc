/*
    Copyright (C) 2014-2015 Carl Hetherington <cth@carlh.net>

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

/** @file  src/reel_sound_asset.cc
 *  @brief ReelSoundAsset class.
 */

#include "reel_sound_asset.h"
#include "dcp_assert.h"
#include <libcxml/cxml.h>
#include <libxml++/libxml++.h>

using std::string;
using boost::shared_ptr;
using namespace dcp;

ReelSoundAsset::ReelSoundAsset (shared_ptr<SoundAsset> asset, int64_t entry_point)
	: ReelAsset (asset->id(), asset->edit_rate(), asset->intrinsic_duration(), entry_point)
	, ReelMXF (asset, asset->key_id())
{

}

ReelSoundAsset::ReelSoundAsset (shared_ptr<const cxml::Node> node)
	: ReelAsset (node)
	, ReelMXF (node)
{
	node->ignore_child ("Language");
	node->done ();
}

string
ReelSoundAsset::cpl_node_name (Standard) const
{
	return "MainSound";
}

string
ReelSoundAsset::key_type () const
{
	return "MDAK";
}

xmlpp::Node *
ReelSoundAsset::write_to_cpl (xmlpp::Node* node, Standard standard) const
{
	xmlpp::Node* asset = write_to_cpl_base (node, standard, hash());

        if (key_id ()) {
		/* Find <Hash> */
		xmlpp::Node* hash = find_child (asset, "Hash");
		asset->add_child_before(hash, "KeyId")->add_child_text("urn:uuid:" + key_id().get());
        }

	return asset;
}

bool
ReelSoundAsset::equals (shared_ptr<const ReelSoundAsset> other, EqualityOptions opt, NoteHandler note) const
{
	if (!asset_equals (other, opt, note)) {
		return false;
	}
	if (!mxf_equals (other, opt, note)) {
		return false;
	}

	return true;
}

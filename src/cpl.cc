/*
    Copyright (C) 2012-2018 Carl Hetherington <cth@carlh.net>

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

#include "cpl.h"
#include "util.h"
#include "reel.h"
#include "metadata.h"
#include "certificate_chain.h"
#include "xml.h"
#include "reel_picture_asset.h"
#include "reel_sound_asset.h"
#include "reel_subtitle_asset.h"
#include "reel_closed_caption_asset.h"
#include "reel_atmos_asset.h"
#include "local_time.h"
#include "dcp_assert.h"
#include "compose.hpp"
#include "raw_convert.h"
#include <libxml/parser.h>
#include <libxml++/libxml++.h>
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

using std::string;
using std::list;
using std::pair;
using std::make_pair;
using std::cout;
using std::set;
using std::vector;
using boost::shared_ptr;
using boost::optional;
using boost::dynamic_pointer_cast;
using namespace dcp;


static string const cpl_interop_ns = "http://www.digicine.com/PROTO-ASDCP-CPL-20040511#";
static string const cpl_smpte_ns   = "http://www.smpte-ra.org/schemas/429-7/2006/CPL";
static string const cpl_metadata_ns = "http://www.smpte-ra.org/schemas/429-16/2014/CPL-Metadata";


CPL::CPL (string annotation_text, ContentKind content_kind)
	/* default _content_title_text to annotation_text */
	: _issuer ("libdcp" LIBDCP_VERSION)
	, _creator ("libdcp" LIBDCP_VERSION)
	, _issue_date (LocalTime().as_string())
	, _annotation_text (annotation_text)
	, _content_title_text (annotation_text)
	, _content_kind (content_kind)
{
	ContentVersion cv;
	cv.label_text = cv.id + LocalTime().as_string();
	_content_versions.push_back (cv);
}

/** Construct a CPL object from a XML file */
CPL::CPL (boost::filesystem::path file)
	: Asset (file)
	, _content_kind (FEATURE)
{
	cxml::Document f ("CompositionPlaylist");
	f.read_file (file);

	if (f.namespace_uri() == cpl_interop_ns) {
		_standard = INTEROP;
	} else if (f.namespace_uri() == cpl_smpte_ns) {
		_standard = SMPTE;
	} else {
		boost::throw_exception (XMLError ("Unrecognised CPL namespace " + f.namespace_uri()));
	}

	_id = remove_urn_uuid (f.string_child ("Id"));
	_annotation_text = f.optional_string_child("AnnotationText").get_value_or("");
	_issuer = f.optional_string_child("Issuer").get_value_or("");
	_creator = f.optional_string_child("Creator").get_value_or("");
	_issue_date = f.string_child ("IssueDate");
	_content_title_text = f.string_child ("ContentTitleText");
	_content_kind = content_kind_from_string (f.string_child ("ContentKind"));
	shared_ptr<cxml::Node> content_version = f.optional_node_child ("ContentVersion");
	if (content_version) {
		/* XXX: SMPTE should insist that Id is present */
		_content_versions.push_back (
			ContentVersion (
				content_version->optional_string_child("Id").get_value_or(""),
				content_version->string_child("LabelText")
				)
			);
		content_version->done ();
	} else if (_standard == SMPTE) {
		/* ContentVersion is required in SMPTE */
		throw XMLError ("Missing ContentVersion tag in CPL");
	}
	cxml::ConstNodePtr rating_list = f.node_child ("RatingList");
	if (rating_list) {
		BOOST_FOREACH (cxml::ConstNodePtr i, rating_list->node_children("Rating")) {
			_ratings.push_back (Rating(i));
		}
	}
	_reels = type_grand_children<Reel> (f, "ReelList", "Reel");

	cxml::ConstNodePtr reel_list = f.node_child ("ReelList");
	if (reel_list) {
		list<cxml::NodePtr> reels = reel_list->node_children("Reel");
		if (!reels.empty()) {
			cxml::ConstNodePtr asset_list = reels.front()->node_child("AssetList");
			cxml::ConstNodePtr metadata = asset_list->optional_node_child("CompositionMetadataAsset");
			if (metadata) {
				read_composition_metadata_asset (metadata);
			}
		}
	}


	f.ignore_child ("Issuer");
	f.ignore_child ("Signer");
	f.ignore_child ("Signature");

	f.done ();
}

/** Add a reel to this CPL.
 *  @param reel Reel to add.
 */
void
CPL::add (boost::shared_ptr<Reel> reel)
{
	_reels.push_back (reel);
}

/** Write an CompositonPlaylist XML file.
 *
 *  @param file Filename to write.
 *  @param standard INTEROP or SMPTE.
 *  @param signer Signer to sign the CPL, or 0 to add no signature.
 */
void
CPL::write_xml (boost::filesystem::path file, Standard standard, shared_ptr<const CertificateChain> signer) const
{
	xmlpp::Document doc;
	xmlpp::Element* root;
	if (standard == INTEROP) {
		root = doc.create_root_node ("CompositionPlaylist", cpl_interop_ns);
	} else {
		root = doc.create_root_node ("CompositionPlaylist", cpl_smpte_ns);
	}

	root->add_child("Id")->add_child_text ("urn:uuid:" + _id);
	root->add_child("AnnotationText")->add_child_text (_annotation_text);
	root->add_child("IssueDate")->add_child_text (_issue_date);
	root->add_child("Issuer")->add_child_text (_issuer);
	root->add_child("Creator")->add_child_text (_creator);
	root->add_child("ContentTitleText")->add_child_text (_content_title_text);
	root->add_child("ContentKind")->add_child_text (content_kind_to_string (_content_kind));
	DCP_ASSERT (!_content_versions.empty());
	_content_versions[0].as_xml (root);

	xmlpp::Element* rating_list = root->add_child("RatingList");
	BOOST_FOREACH (Rating i, _ratings) {
		i.as_xml (rating_list->add_child("Rating"));
	}

	xmlpp::Element* reel_list = root->add_child ("ReelList");

	bool first = true;
	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		xmlpp::Element* asset_list = i->write_to_cpl (reel_list, standard);
		if (first && standard == dcp::SMPTE) {
			maybe_write_composition_metadata_asset (asset_list);
			first = false;
		}
	}

	indent (root, 0);

	if (signer) {
		signer->sign (root, standard);
	}

	doc.write_to_file_formatted (file.string(), "UTF-8");

	set_file (file);
}


void
CPL::read_composition_metadata_asset (cxml::ConstNodePtr node)
{
	cxml::ConstNodePtr fctt = node->node_child("FullContentTitleText");
	_full_content_title_text = fctt->content();
	_full_content_title_text_language = fctt->optional_string_attribute("language");

	_release_territory = node->optional_string_child("ReleaseTerritory");

	cxml::ConstNodePtr vn = node->optional_node_child("VersionNumber");
	if (vn) {
		_version_number = raw_convert<int>(vn->content());
		/* I decided to check for this number being non-negative on being set, and in the verifier, but not here */
		optional<string> vn_status = vn->optional_string_attribute("status");
		if (vn_status) {
			_status = string_to_status (*vn_status);
		}
	}

	_chain = node->optional_string_child("Chain");
	_distributor = node->optional_string_child("Distributor");
	_facility = node->optional_string_child("Facility");

	cxml::ConstNodePtr acv = node->optional_node_child("AlternateContentVersionList");
	if (acv) {
		BOOST_FOREACH (cxml::ConstNodePtr i, acv->node_children("ContentVersion")) {
			_content_versions.push_back (ContentVersion(i));
		}
	}

	cxml::ConstNodePtr lum = node->optional_node_child("Luminance");
	if (lum) {
		_luminance = Luminance (lum);
	}

	_main_sound_configuration = node->string_child("MainSoundConfiguration");

	string sr = node->string_child("MainSoundSampleRate");
	vector<string> sr_bits;
	boost::split (sr_bits, sr, boost::is_any_of(" "));
	DCP_ASSERT (sr_bits.size() == 2);
	_main_sound_sample_rate = raw_convert<int>(sr_bits[0]);

	_main_picture_stored_area = dcp::Size (
		node->node_child("MainPictureStoredArea")->number_child<int>("Width"),
		node->node_child("MainPictureStoredArea")->number_child<int>("Height")
		);

	_main_picture_active_area = dcp::Size (
		node->node_child("MainPictureActiveArea")->number_child<int>("Width"),
		node->node_child("MainPictureActiveArea")->number_child<int>("Height")
		);

	optional<string> sll = node->optional_string_child("MainSubtitleLanguageList");
	if (sll) {
		vector<string> sll_split;
		boost::split (sll_split, *sll, boost::is_any_of(" "));
		DCP_ASSERT (!sll_split.empty());

		/* If the first language on SubtitleLanguageList is the same as the language of the first subtitle we'll ignore it */
		size_t first = 0;
		if (!_reels.empty()) {
			shared_ptr<dcp::ReelSubtitleAsset> sub = _reels.front()->main_subtitle();
			if (sub) {
				optional<dcp::LanguageTag> lang = sub->language();
				if (lang && lang->to_string() == sll_split[0]) {
					first = 1;
				}
			}
		}

		for (size_t i = first; i < sll_split.size(); ++i) {
			_additional_subtitle_languages.push_back (sll_split[i]);
		}
	}
}


/** Write a CompositionMetadataAsset node as a child of @param node provided
 *  the required metadata is stored in the object.  If any required metadata
 *  is missing this method will do nothing.
 */
void
CPL::maybe_write_composition_metadata_asset (xmlpp::Element* node) const
{
	if (
		!_main_sound_configuration ||
		!_main_sound_sample_rate ||
		!_main_picture_stored_area ||
		!_main_picture_active_area ||
		_reels.empty() ||
		!_reels.front()->main_picture()) {
		return;
	}

	xmlpp::Element* meta = node->add_child("meta:CompositionMetadataAsset");
	meta->set_namespace_declaration (cpl_metadata_ns, "meta");

	meta->add_child("Id")->add_child_text("urn:uuid:" + make_uuid());

	shared_ptr<dcp::ReelPictureAsset> mp = _reels.front()->main_picture();
	meta->add_child("EditRate")->add_child_text(mp->edit_rate().as_string());
	meta->add_child("IntrinsicDuration")->add_child_text(raw_convert<string>(mp->intrinsic_duration()));

	xmlpp::Element* fctt = meta->add_child("FullContentTitleText", "meta");
	if (_full_content_title_text) {
		fctt->add_child_text (*_full_content_title_text);
	}
	if (_full_content_title_text_language) {
		fctt->set_attribute("language", *_full_content_title_text_language);
	}

	if (_release_territory) {
		meta->add_child("ReleaseTerritory", "meta")->add_child_text(*_release_territory);
	}

	if (_version_number) {
		xmlpp::Element* vn = meta->add_child("VersionNumber", "meta");
		vn->add_child_text(raw_convert<string>(*_version_number));
		if (_status) {
			vn->set_attribute("status", status_to_string(*_status));
		}
	}

	if (_chain) {
		meta->add_child("Chain", "meta")->add_child_text(*_chain);
	}

	if (_distributor) {
		meta->add_child("Distributor", "meta")->add_child_text(*_distributor);
	}

	if (_facility) {
		meta->add_child("Facility", "meta")->add_child_text(*_facility);
	}

	if (_content_versions.size() > 1) {
		xmlpp::Element* vc = meta->add_child("AlternateContentVersionList", "meta");
		for (size_t i = 1; i < _content_versions.size(); ++i) {
			_content_versions[i].as_xml (vc);
		}
	}

	if (_luminance) {
		_luminance->as_xml (meta, "meta");
	}

	meta->add_child("MainSoundConfiguration", "meta")->add_child_text(*_main_sound_configuration);
	meta->add_child("MainSoundSampleRate", "meta")->add_child_text(raw_convert<string>(*_main_sound_sample_rate) + " 1");

	xmlpp::Element* stored = meta->add_child("MainPictureStoredArea", "meta");
	stored->add_child("Width", "meta")->add_child_text(raw_convert<string>(_main_picture_stored_area->width));
	stored->add_child("Height", "meta")->add_child_text(raw_convert<string>(_main_picture_stored_area->height));

	xmlpp::Element* active = meta->add_child("MainPictureActiveArea", "meta");
	active->add_child("Width", "meta")->add_child_text(raw_convert<string>(_main_picture_active_area->width));
	active->add_child("Height", "meta")->add_child_text(raw_convert<string>(_main_picture_active_area->height));

	optional<dcp::LanguageTag> first_subtitle_language;
	BOOST_FOREACH (shared_ptr<const Reel> i, _reels) {
		if (i->main_subtitle()) {
			first_subtitle_language = i->main_subtitle()->language();
			if (first_subtitle_language) {
				break;
			}
		}
	}

	if (first_subtitle_language || !_additional_subtitle_languages.empty()) {
		string lang;
		if (first_subtitle_language) {
			lang = first_subtitle_language->to_string();
		}
		BOOST_FOREACH (dcp::LanguageTag const& i, _additional_subtitle_languages) {
			if (!lang.empty()) {
				lang += " ";
			}
			lang += i.to_string();
		}
		meta->add_child("MainSubtitleLanguageList")->add_child_text(lang);
	}
}


list<shared_ptr<ReelMXF> >
CPL::reel_mxfs ()
{
	list<shared_ptr<ReelMXF> > c;

	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		if (i->main_picture ()) {
			c.push_back (i->main_picture());
		}
		if (i->main_sound ()) {
			c.push_back (i->main_sound());
		}
		if (i->main_subtitle ()) {
			c.push_back (i->main_subtitle());
		}
		BOOST_FOREACH (shared_ptr<ReelClosedCaptionAsset> j, i->closed_captions()) {
			c.push_back (j);
		}
		if (i->atmos ()) {
			c.push_back (i->atmos());
		}
	}

	return c;
}

list<shared_ptr<const ReelMXF> >
CPL::reel_mxfs () const
{
	list<shared_ptr<const ReelMXF> > c;

	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		if (i->main_picture ()) {
			c.push_back (i->main_picture());
		}
		if (i->main_sound ()) {
			c.push_back (i->main_sound());
		}
		if (i->main_subtitle ()) {
			c.push_back (i->main_subtitle());
		}
		BOOST_FOREACH (shared_ptr<ReelClosedCaptionAsset> j, i->closed_captions()) {
			c.push_back (j);
		}
		if (i->atmos ()) {
			c.push_back (i->atmos());
		}
	}

	return c;
}

bool
CPL::equals (shared_ptr<const Asset> other, EqualityOptions opt, NoteHandler note) const
{
	shared_ptr<const CPL> other_cpl = dynamic_pointer_cast<const CPL> (other);
	if (!other_cpl) {
		return false;
	}

	if (_annotation_text != other_cpl->_annotation_text && !opt.cpl_annotation_texts_can_differ) {
		string const s = "CPL: annotation texts differ: " + _annotation_text + " vs " + other_cpl->_annotation_text + "\n";
		note (DCP_ERROR, s);
		return false;
	}

	if (_content_kind != other_cpl->_content_kind) {
		note (DCP_ERROR, "CPL: content kinds differ");
		return false;
	}

	if (_reels.size() != other_cpl->_reels.size()) {
		note (DCP_ERROR, String::compose ("CPL: reel counts differ (%1 vs %2)", _reels.size(), other_cpl->_reels.size()));
		return false;
	}

	list<shared_ptr<Reel> >::const_iterator a = _reels.begin ();
	list<shared_ptr<Reel> >::const_iterator b = other_cpl->_reels.begin ();

	while (a != _reels.end ()) {
		if (!(*a)->equals (*b, opt, note)) {
			return false;
		}
		++a;
		++b;
	}

	return true;
}

/** @return true if we have any encrypted content */
bool
CPL::encrypted () const
{
	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		if (i->encrypted ()) {
			return true;
		}
	}

	return false;
}

/** Add a KDM to this CPL.  If the KDM is for any of this CPLs assets it will be used
 *  to decrypt those assets.
 *  @param kdm KDM.
 */
void
CPL::add (DecryptedKDM const & kdm)
{
	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		i->add (kdm);
	}
}

void
CPL::resolve_refs (list<shared_ptr<Asset> > assets)
{
	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		i->resolve_refs (assets);
	}
}

string
CPL::pkl_type (Standard standard) const
{
	return static_pkl_type (standard);
}

string
CPL::static_pkl_type (Standard standard)
{
	switch (standard) {
	case INTEROP:
		return "text/xml;asdcpKind=CPL";
	case SMPTE:
		return "text/xml";
	default:
		DCP_ASSERT (false);
	}
}

int64_t
CPL::duration () const
{
	int64_t d = 0;
	BOOST_FOREACH (shared_ptr<Reel> i, _reels) {
		d += i->duration ();
	}
	return d;
}


void
CPL::set_version_number (int v)
{
	if (v < 0) {
		throw BadSettingError ("CPL version number cannot be negative");
	}

	_version_number = v;
}


void
CPL::set_content_versions (vector<ContentVersion> v)
{
	set<string> ids;
	BOOST_FOREACH (ContentVersion i, v) {
		if (!ids.insert(i.id).second) {
			throw DuplicateIdError ("Duplicate ID in ContentVersion list");
		}
	}

	_content_versions = v;
}


ContentVersion
CPL::content_version () const
{
	DCP_ASSERT (!_content_versions.empty());
	return _content_versions[0];
}


void
CPL::set_additional_subtitle_languages (vector<dcp::LanguageTag> const& langs)
{
	_additional_subtitle_languages.clear ();
	BOOST_FOREACH (dcp::LanguageTag const& i, langs) {
		_additional_subtitle_languages.push_back (i.to_string());
	}
}

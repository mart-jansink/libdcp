/*
    Copyright (C) 2012-2020 Carl Hetherington <cth@carlh.net>

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

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE libdcp_test
#include "cpl.h"
#include "dcp.h"
#include "file.h"
#include "mono_picture_asset.h"
#include "picture_asset_writer.h"
#include "reel.h"
#include "reel_mono_picture_asset.h"
#include "reel_sound_asset.h"
#include "sound_asset.h"
#include "sound_asset_writer.h"
#include "test.h"
#include "util.h"
#include <asdcp/KM_util.h>
#include <asdcp/KM_prng.h>
#include <sndfile.h>
#include <libxml++/libxml++.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <iostream>

using std::string;
using std::min;
using std::list;
using boost::shared_ptr;

boost::filesystem::path private_test;
boost::filesystem::path xsd_test = "build/test/xsd with spaces";

struct TestConfig
{
	TestConfig()
	{
		dcp::init ();
		if (boost::unit_test::framework::master_test_suite().argc >= 2) {
			private_test = boost::unit_test::framework::master_test_suite().argv[1];
		}

		using namespace boost::filesystem;
		boost::system::error_code ec;
		remove_all (xsd_test, ec);
		boost::filesystem::create_directory (xsd_test);
		for (directory_iterator i = directory_iterator("xsd"); i != directory_iterator(); ++i) {
			copy_file (*i, xsd_test / i->path().filename());
		}
	}
};

void
check_xml (xmlpp::Element* ref, xmlpp::Element* test, list<string> ignore)
{
	BOOST_CHECK_EQUAL (ref->get_name (), test->get_name ());
	BOOST_CHECK_EQUAL (ref->get_namespace_prefix (), test->get_namespace_prefix ());

	if (find (ignore.begin(), ignore.end(), ref->get_name()) != ignore.end ()) {
		return;
	}

	xmlpp::Element::NodeList ref_children = ref->get_children ();
	xmlpp::Element::NodeList test_children = test->get_children ();
	BOOST_REQUIRE_MESSAGE (
		ref_children.size () == test_children.size (),
		"child counts of " << ref->get_name() << " differ; ref has " << ref_children.size() << ", test has " << test_children.size()
		);

	xmlpp::Element::NodeList::iterator k = ref_children.begin ();
	xmlpp::Element::NodeList::iterator l = test_children.begin ();
	while (k != ref_children.end ()) {

		/* XXX: should be doing xmlpp::EntityReference, xmlpp::XIncludeEnd, xmlpp::XIncludeStart */

		xmlpp::Element* ref_el = dynamic_cast<xmlpp::Element*> (*k);
		xmlpp::Element* test_el = dynamic_cast<xmlpp::Element*> (*l);
		BOOST_CHECK ((ref_el && test_el) || (!ref_el && !test_el));
		if (ref_el && test_el) {
			check_xml (ref_el, test_el, ignore);
		}

		xmlpp::ContentNode* ref_cn = dynamic_cast<xmlpp::ContentNode*> (*k);
		xmlpp::ContentNode* test_cn = dynamic_cast<xmlpp::ContentNode*> (*l);
		BOOST_CHECK ((ref_cn && test_cn) || (!ref_cn && !test_cn));
		if (ref_cn && test_cn) {
			BOOST_CHECK_EQUAL (ref_cn->get_content(), test_cn->get_content ());
		}

		++k;
		++l;
	}

	xmlpp::Element::AttributeList ref_attributes = ref->get_attributes ();
	xmlpp::Element::AttributeList test_attributes = test->get_attributes ();
	BOOST_CHECK_EQUAL (ref_attributes.size(), test_attributes.size ());

	xmlpp::Element::AttributeList::const_iterator m = ref_attributes.begin();
	xmlpp::Element::AttributeList::const_iterator n = test_attributes.begin();
	while (m != ref_attributes.end ()) {
		BOOST_CHECK_EQUAL ((*m)->get_name(), (*n)->get_name());
		BOOST_CHECK_EQUAL ((*m)->get_value(), (*n)->get_value());

		++m;
		++n;
	}
}

void
check_xml (string ref, string test, list<string> ignore)
{
	xmlpp::DomParser* ref_parser = new xmlpp::DomParser ();
	ref_parser->parse_memory (ref);
	xmlpp::Element* ref_root = ref_parser->get_document()->get_root_node ();
	xmlpp::DomParser* test_parser = new xmlpp::DomParser ();
	test_parser->parse_memory (test);
	xmlpp::Element* test_root = test_parser->get_document()->get_root_node ();

	check_xml (ref_root, test_root, ignore);
}

void
check_file (boost::filesystem::path ref, boost::filesystem::path check)
{
	uintmax_t N = boost::filesystem::file_size (ref);
	BOOST_CHECK_EQUAL (N, boost::filesystem::file_size (check));
	FILE* ref_file = dcp::fopen_boost (ref, "rb");
	BOOST_CHECK (ref_file);
	FILE* check_file = dcp::fopen_boost (check, "rb");
	BOOST_CHECK (check_file);

	int const buffer_size = 65536;
	uint8_t* ref_buffer = new uint8_t[buffer_size];
	uint8_t* check_buffer = new uint8_t[buffer_size];

	string error;
	error = "File " + check.string() + " differs from reference " + ref.string();

	while (N) {
		uintmax_t this_time = min (uintmax_t (buffer_size), N);
		size_t r = fread (ref_buffer, 1, this_time, ref_file);
		BOOST_CHECK_EQUAL (r, this_time);
		r = fread (check_buffer, 1, this_time, check_file);
		BOOST_CHECK_EQUAL (r, this_time);

		BOOST_CHECK_MESSAGE (memcmp (ref_buffer, check_buffer, this_time) == 0, error);
		if (memcmp (ref_buffer, check_buffer, this_time)) {
			break;
		}

		N -= this_time;
	}

	delete[] ref_buffer;
	delete[] check_buffer;

	fclose (ref_file);
	fclose (check_file);
}


RNGFixer::RNGFixer ()
{
	Kumu::cth_test = true;
	Kumu::FortunaRNG().Reset();
}


RNGFixer::~RNGFixer ()
{
	Kumu::cth_test = false;
}


shared_ptr<dcp::DCP>
make_simple (boost::filesystem::path path)
{
	/* Some known metadata */
	dcp::XMLMetadata xml_meta;
	xml_meta.annotation_text = "A Test DCP";
	xml_meta.issuer = "OpenDCP 0.0.25";
	xml_meta.creator = "OpenDCP 0.0.25";
	xml_meta.issue_date = "2012-07-17T04:45:18+00:00";
	dcp::MXFMetadata mxf_meta;
	mxf_meta.company_name = "OpenDCP";
	mxf_meta.product_name = "OpenDCP";
	mxf_meta.product_version = "0.0.25";

	/* We're making build/test/DCP/dcp_test1 */
	boost::filesystem::remove_all (path);
	boost::filesystem::create_directories (path);
	shared_ptr<dcp::DCP> d (new dcp::DCP (path));
	shared_ptr<dcp::CPL> cpl (new dcp::CPL ("A Test DCP", dcp::FEATURE));
	cpl->set_content_version_id ("urn:uuid:75ac29aa-42ac-1234-ecae-49251abefd11");
	cpl->set_content_version_label_text ("content-version-label-text");
	cpl->set_metadata (xml_meta);

	shared_ptr<dcp::MonoPictureAsset> mp (new dcp::MonoPictureAsset (dcp::Fraction (24, 1), dcp::SMPTE));
	mp->set_metadata (mxf_meta);
	shared_ptr<dcp::PictureAssetWriter> picture_writer = mp->start_write (path / "video.mxf", false);
	dcp::File j2c ("test/data/32x32_red_square.j2c");
	for (int i = 0; i < 24; ++i) {
		picture_writer->write (j2c.data (), j2c.size ());
	}
	picture_writer->finalize ();

	shared_ptr<dcp::SoundAsset> ms (new dcp::SoundAsset (dcp::Fraction (24, 1), 48000, 1, dcp::SMPTE));
	ms->set_metadata (mxf_meta);
	shared_ptr<dcp::SoundAssetWriter> sound_writer = ms->start_write (path / "audio.mxf");

	SF_INFO info;
	info.format = 0;
	SNDFILE* sndfile = sf_open ("test/data/1s_24-bit_48k_silence.wav", SFM_READ, &info);
	BOOST_CHECK (sndfile);
	float buffer[4096*6];
	float* channels[1];
	channels[0] = buffer;
	while (true) {
		sf_count_t N = sf_readf_float (sndfile, buffer, 4096);
		sound_writer->write (channels, N);
		if (N < 4096) {
			break;
		}
	}

	sound_writer->finalize ();

	cpl->add (shared_ptr<dcp::Reel> (
			  new dcp::Reel (
				  shared_ptr<dcp::ReelMonoPictureAsset> (new dcp::ReelMonoPictureAsset (mp, 0)),
				  shared_ptr<dcp::ReelSoundAsset> (new dcp::ReelSoundAsset (ms, 0))
				  )
			  ));

	d->add (cpl);
	return d;
}


BOOST_GLOBAL_FIXTURE (TestConfig);

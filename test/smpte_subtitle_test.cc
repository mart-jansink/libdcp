/*
    Copyright (C) 2018-2021 Carl Hetherington <cth@carlh.net>

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


#include "smpte_load_font_node.h"
#include "smpte_subtitle_asset.h"
#include "stream_operators.h"
#include "subtitle_image.h"
#include "test.h"
#include "types.h"
#include <boost/optional/optional_io.hpp>
#include <boost/test/unit_test.hpp>


using std::make_shared;
using std::string;
using std::shared_ptr;
using std::dynamic_pointer_cast;
using std::vector;
using boost::optional;


BOOST_AUTO_TEST_CASE (smpte_subtitle_id_test)
{
	dcp::SMPTESubtitleAsset subs;
	subs.add(
		make_shared<dcp::SubtitleString>(
			optional<string>(),
			false, false, false,
			dcp::Colour(),
			64,
			1,
			dcp::Time(0, 1, 2, 3, 24),
			dcp::Time(0, 2, 2, 3, 24),
			0.5,
			dcp::HAlign::CENTER,
			0.5,
			dcp::VAlign::CENTER,
			dcp::Direction::LTR,
			"Hello",
			dcp::Effect::NONE,
			dcp::Colour(),
			dcp::Time(0, 0, 0, 0, 24),
			dcp::Time(0, 0, 0, 0, 24),
			0
			)
		);
	subs.write("build/test/smpte_subtitle_id_test.mxf");

	dcp::SMPTESubtitleAsset check("build/test/smpte_subtitle_id_test.mxf");
	BOOST_CHECK(check.id() != check.xml_id());
}


/** Check reading of a SMPTE subtitle file */
BOOST_AUTO_TEST_CASE (read_smpte_subtitle_test)
{
	dcp::SMPTESubtitleAsset sc (
		private_test /
		"data" /
		"JourneyToJah_TLR-1_F_EN-DE-FR_CH_51_2K_LOK_20140225_DGL_SMPTE_OV" /
		"8b48f6ae-c74b-4b80-b994-a8236bbbad74_sub.mxf"
		);

	BOOST_CHECK_EQUAL (sc.id(), "8b48f6ae-c74b-4b80-b994-a8236bbbad74");
	BOOST_CHECK_EQUAL (sc.content_title_text(), "Journey to Jah");
	BOOST_REQUIRE (sc.annotation_text());
	BOOST_CHECK_EQUAL (sc.annotation_text().get(), "Journey to Jah");
	BOOST_CHECK_EQUAL (sc.issue_date(), dcp::LocalTime ("2014-02-25T11:22:48.000-00:00"));
	BOOST_REQUIRE (sc.reel_number());
	BOOST_CHECK_EQUAL (sc.reel_number().get(), 1);
	BOOST_REQUIRE (sc.language ());
	BOOST_CHECK_EQUAL (sc.language().get (), "de");
	BOOST_CHECK_EQUAL (sc.edit_rate(), dcp::Fraction (25, 1));
	BOOST_CHECK_EQUAL (sc.time_code_rate(), 25);
	BOOST_CHECK_EQUAL (sc.start_time(), dcp::Time (0, 0, 0, 0, 25));
	auto lfn = sc.load_font_nodes ();
	BOOST_REQUIRE_EQUAL (lfn.size(), 1);
	shared_ptr<dcp::SMPTELoadFontNode> smpte_lfn = dynamic_pointer_cast<dcp::SMPTELoadFontNode> (lfn.front ());
	BOOST_REQUIRE (smpte_lfn);
	BOOST_CHECK_EQUAL (smpte_lfn->id, "theFontId");
	BOOST_CHECK_EQUAL (smpte_lfn->urn, "9118bbce-4105-4a05-b37c-a5a6f75e1fea");
	BOOST_REQUIRE_EQUAL (sc.subtitles().size(), 63);
	BOOST_REQUIRE (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().front()));
	BOOST_CHECK_EQUAL (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().front())->text(), "Noch mal.");
	BOOST_CHECK_EQUAL (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().front())->space_before(), 0.0f);
	BOOST_CHECK_EQUAL (sc.subtitles().front()->in(), dcp::Time (0, 0, 25, 12, 25));
	BOOST_CHECK_EQUAL (sc.subtitles().front()->out(), dcp::Time (0, 0, 26, 4, 25));
	BOOST_REQUIRE (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().back()));
	BOOST_CHECK_EQUAL (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().back())->text(), "Prochainement");
	BOOST_CHECK_EQUAL (dynamic_pointer_cast<const dcp::SubtitleString>(sc.subtitles().back())->space_before(), 0.0f);
	BOOST_CHECK_EQUAL (sc.subtitles().back()->in(), dcp::Time (0, 1, 57, 17, 25));
	BOOST_CHECK_EQUAL (sc.subtitles().back()->out(), dcp::Time (0, 1, 58, 12, 25));
}


/** And another one featuring <Font> within <Text> and some <Space> */
BOOST_AUTO_TEST_CASE (read_smpte_subtitle_test2)
{
	dcp::SMPTESubtitleAsset sc (private_test / "olsson.xml");

	auto subs = sc.subtitles();
	BOOST_REQUIRE_EQUAL (subs.size(), 6);
	auto i = 0;
	auto is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), "Testing is ");
	BOOST_CHECK (!is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 0, 0.1);
	++i;
	is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), "really");
	BOOST_CHECK (is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 0, 0.1);
	++i;
	is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), " fun!");
	BOOST_CHECK (!is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 5, 0.1);
	++i;
	is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), "This is the ");
	BOOST_CHECK (!is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 0, 0.1);
	++i;
	is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), "second");
	BOOST_CHECK (is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 0, 0.1);
	++i;
	is = dynamic_pointer_cast<const dcp::SubtitleString>(subs[i]);
	BOOST_REQUIRE (is);
	BOOST_CHECK_EQUAL (is->text(), " line!");
	BOOST_CHECK (!is->italic());
	BOOST_CHECK_CLOSE (is->space_before(), 0, 0.1);
}


/* Write some subtitle content as SMPTE XML and check that it is right */
BOOST_AUTO_TEST_CASE (write_smpte_subtitle_test)
{
	dcp::SMPTESubtitleAsset c;
	c.set_reel_number (1);
	c.set_language (dcp::LanguageTag("en"));
	c.set_content_title_text ("Test");
	c.set_issue_date (dcp::LocalTime ("2016-04-01T03:52:00+00:00"));

	c.add (
		make_shared<dcp::SubtitleString> (
			string ("Frutiger"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 4,  9, 22, 24),
			dcp::Time (0, 4, 11, 22, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"Hello world",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			boost::optional<string> (),
			true,
			true,
			true,
			dcp::Colour (128, 0, 64),
			91,
			1.0,
			dcp::Time (5, 41,  0, 21, 24),
			dcp::Time (6, 12, 15, 21, 24),
			0,
			dcp::HAlign::CENTER,
			0.4,
			dcp::VAlign::BOTTOM,
			dcp::Direction::RTL,
			"What's going ",
			dcp::Effect::BORDER,
			dcp::Colour (1, 2, 3),
			dcp::Time (1, 2, 3, 4, 24),
			dcp::Time (5, 6, 7, 8, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			boost::optional<string> (),
			true,
			true,
			true,
			dcp::Colour (128, 0, 64),
			91,
			1.0,
			dcp::Time (5, 41,  0, 21, 24),
			dcp::Time (6, 12, 15, 21, 24),
			0,
			dcp::HAlign::CENTER,
			0.4,
			dcp::VAlign::BOTTOM,
			dcp::Direction::RTL,
			"on",
			dcp::Effect::BORDER,
			dcp::Colour (1, 2, 3),
			dcp::Time (1, 2, 3, 4, 24),
			dcp::Time (5, 6, 7, 8, 24),
			4.2
			)
		);

	c._xml_id = "a6c58cff-3e1e-4b38-acec-a42224475ef6";

	check_xml (
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SubtitleReel xmlns=\"http://www.smpte-ra.org/schemas/428-7/2010/DCST\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
		  "<Id>urn:uuid:a6c58cff-3e1e-4b38-acec-a42224475ef6</Id>"
		  "<ContentTitleText>Test</ContentTitleText>"
		  "<IssueDate>2016-04-01T03:52:00.000+00:00</IssueDate>"
		  "<ReelNumber>1</ReelNumber>"
		  "<Language>en</Language>"
		  "<EditRate>24 1</EditRate>"
		  "<TimeCodeRate>24</TimeCodeRate>"
		  "<SubtitleList>"
		    "<Font AspectAdjust=\"1.0\" Color=\"FFFFFFFF\" Effect=\"none\" EffectColor=\"FF000000\" ID=\"Frutiger\" Italic=\"no\" Script=\"normal\" Size=\"48\" Underline=\"no\" Weight=\"normal\">"
		      "<Subtitle SpotNumber=\"1\" TimeIn=\"00:04:09:22\" TimeOut=\"00:04:11:22\" FadeUpTime=\"00:00:00:00\" FadeDownTime=\"00:00:00:00\">"
		        "<Text Valign=\"top\" Vposition=\"80\">Hello world</Text>"
		      "</Subtitle>"
		    "</Font>"
		    "<Font AspectAdjust=\"1.0\" Color=\"FF800040\" Effect=\"border\" EffectColor=\"FF010203\" Italic=\"yes\" Script=\"normal\" Size=\"91\" Underline=\"yes\" Weight=\"bold\">"
		      "<Subtitle SpotNumber=\"2\" TimeIn=\"05:41:00:21\" TimeOut=\"06:12:15:21\" FadeUpTime=\"01:02:03:04\" FadeDownTime=\"05:06:07:08\">"
		        "<Text Valign=\"bottom\" Vposition=\"40\" Direction=\"rtl\">What's going <Space Size=\"4.2\"/>on</Text>"
		      "</Subtitle>"
		    "</Font>"
		  "</SubtitleList>"
		"</SubtitleReel>",
		c.xml_as_string (),
		vector<string>()
		);
}

/* Write some subtitle content as SMPTE XML and check that it is right.
   This includes in-line font changes.
*/
BOOST_AUTO_TEST_CASE (write_smpte_subtitle_test2)
{
	dcp::SMPTESubtitleAsset c;
	c.set_reel_number (1);
	c.set_language (dcp::LanguageTag("en"));
	c.set_content_title_text ("Test");
	c.set_issue_date (dcp::LocalTime ("2016-04-01T03:52:00+00:00"));

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"Testing is ",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			true,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"really",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			" fun",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.9,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"This is the ",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			true,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.9,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"second",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.9,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			" line",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c._xml_id = "a6c58cff-3e1e-4b38-acec-a42224475ef6";

	check_xml (
		c.xml_as_string(),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SubtitleReel xmlns=\"http://www.smpte-ra.org/schemas/428-7/2010/DCST\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
		  "<Id>urn:uuid:a6c58cff-3e1e-4b38-acec-a42224475ef6</Id>"
		  "<ContentTitleText>Test</ContentTitleText>"
		  "<IssueDate>2016-04-01T03:52:00.000+00:00</IssueDate>"
		  "<ReelNumber>1</ReelNumber>"
		  "<Language>en</Language>"
		  "<EditRate>24 1</EditRate>"
		  "<TimeCodeRate>24</TimeCodeRate>"
		  "<SubtitleList>"
		    "<Font AspectAdjust=\"1.0\" Color=\"FFFFFFFF\" Effect=\"none\" EffectColor=\"FF000000\" ID=\"Arial\" Script=\"normal\" Size=\"48\" Underline=\"no\" Weight=\"normal\">"
		      "<Subtitle SpotNumber=\"1\" TimeIn=\"00:00:01:00\" TimeOut=\"00:00:09:00\" FadeUpTime=\"00:00:00:00\" FadeDownTime=\"00:00:00:00\">"
		        "<Text Valign=\"top\" Vposition=\"80\">"
		          "<Font Italic=\"no\">Testing is </Font>"
		          "<Font Italic=\"yes\">really</Font>"
		          "<Font Italic=\"no\"> fun</Font>"
		        "</Text>"
		        "<Text Valign=\"top\" Vposition=\"90\">"
		          "<Font Italic=\"no\">This is the </Font>"
		          "<Font Italic=\"yes\">second</Font>"
		          "<Font Italic=\"no\"> line</Font>"
		        "</Text>"
		      "</Subtitle>"
		    "</Font>"
		  "</SubtitleList>"
		"</SubtitleReel>",
		vector<string>()
		);
}

/* Write some subtitle content as SMPTE using bitmaps and check that it is right */
BOOST_AUTO_TEST_CASE (write_smpte_subtitle_test3)
{
	dcp::SMPTESubtitleAsset c;
	c.set_reel_number (1);
	c.set_language (dcp::LanguageTag("en"));
	c.set_content_title_text ("Test");
	c.set_start_time (dcp::Time());

	boost::filesystem::path const sub_image = "test/data/sub.png";

	c.add (
		make_shared<dcp::SubtitleImage>(
			dcp::ArrayData(sub_image),
			dcp::Time (0, 4,  9, 22, 24),
			dcp::Time (0, 4, 11, 22, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24)
			)
	      );

	c._id = "a6c58cff-3e1e-4b38-acec-a42224475ef6";

	boost::filesystem::path path = "build/test/write_smpte_subtitle_test3";
	boost::filesystem::create_directories (path);
	c.write (path / "subs.mxf");

	dcp::SMPTESubtitleAsset read_back (path / "subs.mxf");
	auto subs = read_back.subtitles ();
	BOOST_REQUIRE_EQUAL (subs.size(), 1U);
	auto image = dynamic_pointer_cast<const dcp::SubtitleImage>(subs[0]);
	BOOST_REQUIRE (image);

	BOOST_CHECK (image->png_image() == dcp::ArrayData(sub_image));
	BOOST_CHECK (image->in() == dcp::Time(0, 4, 9, 22, 24));
	BOOST_CHECK (image->out() == dcp::Time(0, 4, 11, 22, 24));
	BOOST_CHECK_CLOSE (image->h_position(), 0.0, 1);
	BOOST_CHECK (image->h_align() == dcp::HAlign::CENTER);
	BOOST_CHECK_CLOSE (image->v_position(), 0.8, 1);
	BOOST_CHECK (image->v_align() == dcp::VAlign::TOP);
	BOOST_CHECK (image->fade_up_time() == dcp::Time(0, 0, 0, 0, 24));
	BOOST_CHECK (image->fade_down_time() == dcp::Time(0, 0, 0, 0, 24));
}


/* Some closed caption systems require the <Text> elements to be written in order of their
 * vertical position (see DoM bug #2106).
 */
BOOST_AUTO_TEST_CASE (write_subtitles_in_vertical_order_with_top_alignment)
{
	dcp::SMPTESubtitleAsset c;
	c.set_reel_number (1);
	c.set_language (dcp::LanguageTag("en"));
	c.set_content_title_text ("Test");
	c.set_issue_date (dcp::LocalTime ("2016-04-01T03:52:00+00:00"));

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"Top line",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.9,
			dcp::VAlign::TOP,
			dcp::Direction::LTR,
			"Bottom line",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c._xml_id = "a6c58cff-3e1e-4b38-acec-a42224475ef6";

	check_xml (
		c.xml_as_string(),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SubtitleReel xmlns=\"http://www.smpte-ra.org/schemas/428-7/2010/DCST\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
		  "<Id>urn:uuid:a6c58cff-3e1e-4b38-acec-a42224475ef6</Id>"
		  "<ContentTitleText>Test</ContentTitleText>"
		  "<IssueDate>2016-04-01T03:52:00.000+00:00</IssueDate>"
		  "<ReelNumber>1</ReelNumber>"
		  "<Language>en</Language>"
		  "<EditRate>24 1</EditRate>"
		  "<TimeCodeRate>24</TimeCodeRate>"
		  "<SubtitleList>"
		    "<Font AspectAdjust=\"1.0\" Color=\"FFFFFFFF\" Effect=\"none\" EffectColor=\"FF000000\" ID=\"Arial\" Italic=\"no\" Script=\"normal\" Size=\"48\" Underline=\"no\" Weight=\"normal\">"
		      "<Subtitle SpotNumber=\"1\" TimeIn=\"00:00:01:00\" TimeOut=\"00:00:09:00\" FadeUpTime=\"00:00:00:00\" FadeDownTime=\"00:00:00:00\">"
		        "<Text Valign=\"top\" Vposition=\"80\">Top line</Text>"
		        "<Text Valign=\"top\" Vposition=\"90\">Bottom line</Text>"
		      "</Subtitle>"
		    "</Font>"
		  "</SubtitleList>"
		"</SubtitleReel>",
		{}
		);
}


/* See the test above */
BOOST_AUTO_TEST_CASE (write_subtitles_in_vertical_order_with_bottom_alignment)
{
	dcp::SMPTESubtitleAsset c;
	c.set_reel_number (1);
	c.set_language (dcp::LanguageTag("en"));
	c.set_content_title_text ("Test");
	c.set_issue_date (dcp::LocalTime ("2016-04-01T03:52:00+00:00"));

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.8,
			dcp::VAlign::BOTTOM,
			dcp::Direction::LTR,
			"Top line",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c.add (
		make_shared<dcp::SubtitleString>(
			string ("Arial"),
			false,
			false,
			false,
			dcp::Colour (255, 255, 255),
			48,
			1.0,
			dcp::Time (0, 0, 1, 0, 24),
			dcp::Time (0, 0, 9, 0, 24),
			0,
			dcp::HAlign::CENTER,
			0.7,
			dcp::VAlign::BOTTOM,
			dcp::Direction::LTR,
			"Bottom line",
			dcp::Effect::NONE,
			dcp::Colour (0, 0, 0),
			dcp::Time (0, 0, 0, 0, 24),
			dcp::Time (0, 0, 0, 0, 24),
			0
			)
		);

	c._xml_id = "a6c58cff-3e1e-4b38-acec-a42224475ef6";

	check_xml (
		c.xml_as_string(),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<SubtitleReel xmlns=\"http://www.smpte-ra.org/schemas/428-7/2010/DCST\" xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
		  "<Id>urn:uuid:a6c58cff-3e1e-4b38-acec-a42224475ef6</Id>"
		  "<ContentTitleText>Test</ContentTitleText>"
		  "<IssueDate>2016-04-01T03:52:00.000+00:00</IssueDate>"
		  "<ReelNumber>1</ReelNumber>"
		  "<Language>en</Language>"
		  "<EditRate>24 1</EditRate>"
		  "<TimeCodeRate>24</TimeCodeRate>"
		  "<SubtitleList>"
		    "<Font AspectAdjust=\"1.0\" Color=\"FFFFFFFF\" Effect=\"none\" EffectColor=\"FF000000\" ID=\"Arial\" Italic=\"no\" Script=\"normal\" Size=\"48\" Underline=\"no\" Weight=\"normal\">"
		      "<Subtitle SpotNumber=\"1\" TimeIn=\"00:00:01:00\" TimeOut=\"00:00:09:00\" FadeUpTime=\"00:00:00:00\" FadeDownTime=\"00:00:00:00\">"
		        "<Text Valign=\"bottom\" Vposition=\"80\">Top line</Text>"
		        "<Text Valign=\"bottom\" Vposition=\"70\">Bottom line</Text>"
		      "</Subtitle>"
		    "</Font>"
		  "</SubtitleList>"
		"</SubtitleReel>",
		{}
		);
}


/*
    Copyright (C) 2012-2014 Carl Hetherington <cth@carlh.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef LIBDCP_SUBTITLE_CONTENT_H
#define LIBDCP_SUBTITLE_CONTENT_H

#include "content.h"
#include "dcp_time.h"
#include "subtitle_string.h"
#include <libcxml/cxml.h>

namespace dcp
{

class SubtitleString;	
class Font;
class Text;
class Subtitle;
class LoadFont;

/** @class SubtitleContent
 *  @brief A representation of an XML or MXF file containing subtitles.
 *
 *  XXX: perhaps this should inhert from MXF, or there should be different
 *  classes for XML and MXF subs.
 */
class SubtitleContent : public Content
{
public:
	/** Construct a SubtitleContent.
	 *  @param file Filename.
	 *  @param mxf true if the file is an MXF file, false for XML.
	 */
	SubtitleContent (boost::filesystem::path file, bool mxf);
	SubtitleContent (Fraction edit_rate, std::string movie_title, std::string language);

	bool equals (
		boost::shared_ptr<const Content>,
		EqualityOptions,
		boost::function<void (NoteType, std::string)> note
		) const {
		/* XXX */
		note (DCP_ERROR, "subtitle content not compared yet");
		return true;
	}

	std::string language () const {
		return _language;
	}

	std::list<SubtitleString> subtitles_at (Time t) const;
	std::list<SubtitleString> const & subtitles () const {
		return _subtitles;
	}

	void add (SubtitleString);

	void write_xml (boost::filesystem::path) const;
	Glib::ustring xml_as_string () const;

protected:
	std::string pkl_type (Standard) const {
		return "text/xml";
	}

	std::string asdcp_kind () const {
		return "Subtitle";
	}
	
private:
	std::string font_id_to_name (std::string id) const;

	struct ParseState {
		std::list<boost::shared_ptr<Font> > font_nodes;
		std::list<boost::shared_ptr<Text> > text_nodes;
		std::list<boost::shared_ptr<Subtitle> > subtitle_nodes;
	};

	void maybe_add_subtitle (std::string text, ParseState const & parse_state);
	
	void examine_font_nodes (
		boost::shared_ptr<const cxml::Node> xml,
		std::list<boost::shared_ptr<Font> > const & font_nodes,
		ParseState& parse_state
		);
	
	void examine_text_nodes (
		boost::shared_ptr<const cxml::Node> xml,
		std::list<boost::shared_ptr<Text> > const & text_nodes,
		ParseState& parse_state
		);

	boost::optional<std::string> _movie_title;
	/* strangely, this is sometimes a string */
	std::string _reel_number;
	std::string _language;
	std::list<boost::shared_ptr<LoadFont> > _load_font_nodes;

	std::list<SubtitleString> _subtitles;
	bool _need_sort;
};

}

#endif

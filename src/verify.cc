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

#include "verify.h"
#include "dcp.h"
#include "cpl.h"
#include "reel.h"
#include "reel_closed_caption_asset.h"
#include "reel_picture_asset.h"
#include "reel_sound_asset.h"
#include "reel_subtitle_asset.h"
#include "interop_subtitle_asset.h"
#include "mono_picture_asset.h"
#include "mono_picture_frame.h"
#include "stereo_picture_asset.h"
#include "stereo_picture_frame.h"
#include "exceptions.h"
#include "compose.hpp"
#include "raw_convert.h"
#include "reel_markers_asset.h"
#include "smpte_subtitle_asset.h"
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/parsers/AbstractDOMParser.hpp>
#include <xercesc/sax/HandlerBase.hpp>
#include <xercesc/dom/DOMImplementation.hpp>
#include <xercesc/dom/DOMImplementationLS.hpp>
#include <xercesc/dom/DOMImplementationRegistry.hpp>
#include <xercesc/dom/DOMLSParser.hpp>
#include <xercesc/dom/DOMException.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMNodeList.hpp>
#include <xercesc/dom/DOMError.hpp>
#include <xercesc/dom/DOMLocator.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>
#include <xercesc/dom/DOMAttr.hpp>
#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/framework/LocalFileInputSource.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <boost/noncopyable.hpp>
#include <boost/algorithm/string.hpp>
#include <map>
#include <vector>
#include <iostream>

using std::list;
using std::vector;
using std::string;
using std::cout;
using std::map;
using std::max;
using std::set;
using std::shared_ptr;
using std::make_shared;
using boost::optional;
using boost::function;
using std::dynamic_pointer_cast;

using namespace dcp;
using namespace xercesc;

static
string
xml_ch_to_string (XMLCh const * a)
{
	char* x = XMLString::transcode(a);
	string const o(x);
	XMLString::release(&x);
	return o;
}

class XMLValidationError
{
public:
	XMLValidationError (SAXParseException const & e)
		: _message (xml_ch_to_string(e.getMessage()))
		, _line (e.getLineNumber())
		, _column (e.getColumnNumber())
		, _public_id (e.getPublicId() ? xml_ch_to_string(e.getPublicId()) : "")
		, _system_id (e.getSystemId() ? xml_ch_to_string(e.getSystemId()) : "")
	{

	}

	string message () const {
		return _message;
	}

	uint64_t line () const {
		return _line;
	}

	uint64_t column () const {
		return _column;
	}

	string public_id () const {
		return _public_id;
	}

	string system_id () const {
		return _system_id;
	}

private:
	string _message;
	uint64_t _line;
	uint64_t _column;
	string _public_id;
	string _system_id;
};


class DCPErrorHandler : public ErrorHandler
{
public:
	void warning(const SAXParseException& e)
	{
		maybe_add (XMLValidationError(e));
	}

	void error(const SAXParseException& e)
	{
		maybe_add (XMLValidationError(e));
	}

	void fatalError(const SAXParseException& e)
	{
		maybe_add (XMLValidationError(e));
	}

	void resetErrors() {
		_errors.clear ();
	}

	list<XMLValidationError> errors () const {
		return _errors;
	}

private:
	void maybe_add (XMLValidationError e)
	{
		/* XXX: nasty hack */
		if (
			e.message().find("schema document") != string::npos &&
			e.message().find("has different target namespace from the one specified in instance document") != string::npos
			) {
			return;
		}

		_errors.push_back (e);
	}

	list<XMLValidationError> _errors;
};

class StringToXMLCh : public boost::noncopyable
{
public:
	StringToXMLCh (string a)
	{
		_buffer = XMLString::transcode(a.c_str());
	}

	~StringToXMLCh ()
	{
		XMLString::release (&_buffer);
	}

	XMLCh const * get () const {
		return _buffer;
	}

private:
	XMLCh* _buffer;
};

class LocalFileResolver : public EntityResolver
{
public:
	LocalFileResolver (boost::filesystem::path xsd_dtd_directory)
		: _xsd_dtd_directory (xsd_dtd_directory)
	{
		/* XXX: I'm not clear on what things need to be in this list; some XSDs are apparently, magically
		 * found without being here.
		 */
		add("http://www.w3.org/2001/XMLSchema.dtd", "XMLSchema.dtd");
		add("http://www.w3.org/2001/03/xml.xsd", "xml.xsd");
		add("http://www.w3.org/TR/2002/REC-xmldsig-core-20020212/xmldsig-core-schema.xsd", "xmldsig-core-schema.xsd");
		add("http://www.digicine.com/schemas/437-Y/2007/Main-Stereo-Picture-CPL.xsd", "Main-Stereo-Picture-CPL.xsd");
		add("http://www.digicine.com/PROTO-ASDCP-CPL-20040511.xsd", "PROTO-ASDCP-CPL-20040511.xsd");
		add("http://www.digicine.com/PROTO-ASDCP-PKL-20040311.xsd", "PROTO-ASDCP-PKL-20040311.xsd");
		add("http://www.digicine.com/PROTO-ASDCP-AM-20040311.xsd", "PROTO-ASDCP-AM-20040311.xsd");
		add("http://www.digicine.com/PROTO-ASDCP-CC-CPL-20070926#", "PROTO-ASDCP-CC-CPL-20070926.xsd");
		add("interop-subs", "DCSubtitle.v1.mattsson.xsd");
		add("http://www.smpte-ra.org/schemas/428-7/2010/DCST.xsd", "SMPTE-428-7-2010-DCST.xsd");
		add("http://www.smpte-ra.org/schemas/429-16/2014/CPL-Metadata", "SMPTE-429-16.xsd");
		add("http://www.dolby.com/schemas/2012/AD", "Dolby-2012-AD.xsd");
		add("http://www.smpte-ra.org/schemas/429-10/2008/Main-Stereo-Picture-CPL", "SMPTE-429-10-2008.xsd");
	}

	InputSource* resolveEntity(XMLCh const *, XMLCh const * system_id)
	{
		if (!system_id) {
			return 0;
		}
		auto system_id_str = xml_ch_to_string (system_id);
		auto p = _xsd_dtd_directory;
		if (_files.find(system_id_str) == _files.end()) {
			p /= system_id_str;
		} else {
			p /= _files[system_id_str];
		}
		StringToXMLCh ch (p.string());
		return new LocalFileInputSource(ch.get());
	}

private:
	void add (string uri, string file)
	{
		_files[uri] = file;
	}

	std::map<string, string> _files;
	boost::filesystem::path _xsd_dtd_directory;
};


static void
parse (XercesDOMParser& parser, boost::filesystem::path xml)
{
	parser.parse(xml.string().c_str());
}


static void
parse (XercesDOMParser& parser, string xml)
{
	xercesc::MemBufInputSource buf(reinterpret_cast<unsigned char const*>(xml.c_str()), xml.size(), "");
	parser.parse(buf);
}


template <class T>
void
validate_xml (T xml, boost::filesystem::path xsd_dtd_directory, vector<VerificationNote>& notes)
{
	try {
		XMLPlatformUtils::Initialize ();
	} catch (XMLException& e) {
		throw MiscError ("Failed to initialise xerces library");
	}

	DCPErrorHandler error_handler;

	/* All the xerces objects in this scope must be destroyed before XMLPlatformUtils::Terminate() is called */
	{
		XercesDOMParser parser;
		parser.setValidationScheme(XercesDOMParser::Val_Always);
		parser.setDoNamespaces(true);
		parser.setDoSchema(true);

		vector<string> schema;
		schema.push_back("xml.xsd");
		schema.push_back("xmldsig-core-schema.xsd");
		schema.push_back("SMPTE-429-7-2006-CPL.xsd");
		schema.push_back("SMPTE-429-8-2006-PKL.xsd");
		schema.push_back("SMPTE-429-9-2007-AM.xsd");
		schema.push_back("Main-Stereo-Picture-CPL.xsd");
		schema.push_back("PROTO-ASDCP-CPL-20040511.xsd");
		schema.push_back("PROTO-ASDCP-PKL-20040311.xsd");
		schema.push_back("PROTO-ASDCP-AM-20040311.xsd");
		schema.push_back("DCSubtitle.v1.mattsson.xsd");
		schema.push_back("DCDMSubtitle-2010.xsd");
		schema.push_back("PROTO-ASDCP-CC-CPL-20070926.xsd");
		schema.push_back("SMPTE-429-16.xsd");
		schema.push_back("Dolby-2012-AD.xsd");
		schema.push_back("SMPTE-429-10-2008.xsd");
		schema.push_back("xlink.xsd");
		schema.push_back("SMPTE-335-2012.xsd");
		schema.push_back("SMPTE-395-2014-13-1-aaf.xsd");
		schema.push_back("isdcf-mca.xsd");
		schema.push_back("SMPTE-429-12-2008.xsd");

		/* XXX: I'm not especially clear what this is for, but it seems to be necessary.
		 * Schemas that are not mentioned in this list are not read, and the things
		 * they describe are not checked.
		 */
		string locations;
		for (auto i: schema) {
			locations += String::compose("%1 %1 ", i, i);
		}

		parser.setExternalSchemaLocation(locations.c_str());
		parser.setValidationSchemaFullChecking(true);
		parser.setErrorHandler(&error_handler);

		LocalFileResolver resolver (xsd_dtd_directory);
		parser.setEntityResolver(&resolver);

		try {
			parser.resetDocumentPool();
			parse(parser, xml);
		} catch (XMLException& e) {
			throw MiscError(xml_ch_to_string(e.getMessage()));
		} catch (DOMException& e) {
			throw MiscError(xml_ch_to_string(e.getMessage()));
		} catch (...) {
			throw MiscError("Unknown exception from xerces");
		}
	}

	XMLPlatformUtils::Terminate ();

	for (auto i: error_handler.errors()) {
		notes.push_back ({
			VerificationNote::VERIFY_ERROR,
			VerificationNote::INVALID_XML,
			i.message(),
			boost::trim_copy(i.public_id() + " " + i.system_id()),
			i.line()
		});
	}
}


enum VerifyAssetResult {
	VERIFY_ASSET_RESULT_GOOD,
	VERIFY_ASSET_RESULT_CPL_PKL_DIFFER,
	VERIFY_ASSET_RESULT_BAD
};


static VerifyAssetResult
verify_asset (shared_ptr<const DCP> dcp, shared_ptr<const ReelMXF> reel_mxf, function<void (float)> progress)
{
	auto const actual_hash = reel_mxf->asset_ref()->hash(progress);

	auto pkls = dcp->pkls();
	/* We've read this DCP in so it must have at least one PKL */
	DCP_ASSERT (!pkls.empty());

	auto asset = reel_mxf->asset_ref().asset();

	optional<string> pkl_hash;
	for (auto i: pkls) {
		pkl_hash = i->hash (reel_mxf->asset_ref()->id());
		if (pkl_hash) {
			break;
		}
	}

	DCP_ASSERT (pkl_hash);

	auto cpl_hash = reel_mxf->hash();
	if (cpl_hash && *cpl_hash != *pkl_hash) {
		return VERIFY_ASSET_RESULT_CPL_PKL_DIFFER;
	}

	if (actual_hash != *pkl_hash) {
		return VERIFY_ASSET_RESULT_BAD;
	}

	return VERIFY_ASSET_RESULT_GOOD;
}


void
verify_language_tag (string tag, vector<VerificationNote>& notes)
{
	try {
		LanguageTag test (tag);
	} catch (LanguageTagError &) {
		notes.push_back (VerificationNote(VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_LANGUAGE, tag));
	}
}


enum VerifyPictureAssetResult
{
	VERIFY_PICTURE_ASSET_RESULT_GOOD,
	VERIFY_PICTURE_ASSET_RESULT_FRAME_NEARLY_TOO_LARGE,
	VERIFY_PICTURE_ASSET_RESULT_BAD,
};


int
biggest_frame_size (shared_ptr<const MonoPictureFrame> frame)
{
	return frame->size ();
}

int
biggest_frame_size (shared_ptr<const StereoPictureFrame> frame)
{
	return max(frame->left()->size(), frame->right()->size());
}


template <class A, class R, class F>
optional<VerifyPictureAssetResult>
verify_picture_asset_type (shared_ptr<const ReelMXF> reel_mxf, function<void (float)> progress)
{
	auto asset = dynamic_pointer_cast<A>(reel_mxf->asset_ref().asset());
	if (!asset) {
		return optional<VerifyPictureAssetResult>();
	}

	int biggest_frame = 0;
	auto reader = asset->start_read ();
	auto const duration = asset->intrinsic_duration ();
	for (int64_t i = 0; i < duration; ++i) {
		shared_ptr<const F> frame = reader->get_frame (i);
		biggest_frame = max(biggest_frame, biggest_frame_size(frame));
		progress (float(i) / duration);
	}

	static const int max_frame =   rint(250 * 1000000 / (8 * asset->edit_rate().as_float()));
	static const int risky_frame = rint(230 * 1000000 / (8 * asset->edit_rate().as_float()));
	if (biggest_frame > max_frame) {
		return VERIFY_PICTURE_ASSET_RESULT_BAD;
	} else if (biggest_frame > risky_frame) {
		return VERIFY_PICTURE_ASSET_RESULT_FRAME_NEARLY_TOO_LARGE;
	}

	return VERIFY_PICTURE_ASSET_RESULT_GOOD;
}


static VerifyPictureAssetResult
verify_picture_asset (shared_ptr<const ReelMXF> reel_mxf, function<void (float)> progress)
{
	auto r = verify_picture_asset_type<MonoPictureAsset, MonoPictureAssetReader, MonoPictureFrame>(reel_mxf, progress);
	if (!r) {
		r = verify_picture_asset_type<StereoPictureAsset, StereoPictureAssetReader, StereoPictureFrame>(reel_mxf, progress);
	}

	DCP_ASSERT (r);
	return *r;
}


static void
verify_main_picture_asset (
	shared_ptr<const DCP> dcp,
	shared_ptr<const ReelPictureAsset> reel_asset,
	function<void (string, optional<boost::filesystem::path>)> stage,
	function<void (float)> progress,
	vector<VerificationNote>& notes
	)
{
	auto asset = reel_asset->asset();
	auto const file = *asset->file();
	stage ("Checking picture asset hash", file);
	auto const r = verify_asset (dcp, reel_asset, progress);
	switch (r) {
		case VERIFY_ASSET_RESULT_BAD:
			notes.push_back (
				VerificationNote(
					VerificationNote::VERIFY_ERROR, VerificationNote::INCORRECT_PICTURE_HASH, file
					)
				);
			break;
		case VERIFY_ASSET_RESULT_CPL_PKL_DIFFER:
			notes.push_back (
				VerificationNote(
					VerificationNote::VERIFY_ERROR, VerificationNote::MISMATCHED_PICTURE_HASHES, file
					)
				);
			break;
		default:
			break;
	}
	stage ("Checking picture frame sizes", asset->file());
	auto const pr = verify_picture_asset (reel_asset, progress);
	switch (pr) {
		case VERIFY_PICTURE_ASSET_RESULT_BAD:
			notes.push_back ({
				VerificationNote::VERIFY_ERROR, VerificationNote::INVALID_PICTURE_FRAME_SIZE_IN_BYTES, file
			});
			break;
		case VERIFY_PICTURE_ASSET_RESULT_FRAME_NEARLY_TOO_LARGE:
			notes.push_back ({
				VerificationNote::VERIFY_WARNING, VerificationNote::NEARLY_INVALID_PICTURE_FRAME_SIZE_IN_BYTES, file
			});
			break;
		default:
			break;
	}

	/* Only flat/scope allowed by Bv2.1 */
	if (
		asset->size() != Size(2048, 858) &&
		asset->size() != Size(1998, 1080) &&
		asset->size() != Size(4096, 1716) &&
		asset->size() != Size(3996, 2160)) {
		notes.push_back(
			VerificationNote(
				VerificationNote::VERIFY_BV21_ERROR,
				VerificationNote::INVALID_PICTURE_SIZE_IN_PIXELS,
				String::compose("%1x%2", asset->size().width, asset->size().height),
				file
				)
			);
	}

	/* Only 24, 25, 48fps allowed for 2K */
	if (
		(asset->size() == Size(2048, 858) || asset->size() == Size(1998, 1080)) &&
		(asset->edit_rate() != Fraction(24, 1) && asset->edit_rate() != Fraction(25, 1) && asset->edit_rate() != Fraction(48, 1))
	   ) {
		notes.push_back(
			VerificationNote(
				VerificationNote::VERIFY_BV21_ERROR,
				VerificationNote::INVALID_PICTURE_FRAME_RATE_FOR_2K,
				String::compose("%1/%2", asset->edit_rate().numerator, asset->edit_rate().denominator),
				file
				)
			);
	}

	if (asset->size() == Size(4096, 1716) || asset->size() == Size(3996, 2160)) {
		/* Only 24fps allowed for 4K */
		if (asset->edit_rate() != Fraction(24, 1)) {
			notes.push_back(
				VerificationNote(
					VerificationNote::VERIFY_BV21_ERROR,
					VerificationNote::INVALID_PICTURE_FRAME_RATE_FOR_4K,
					String::compose("%1/%2", asset->edit_rate().numerator, asset->edit_rate().denominator),
					file
					)
				);
		}

		/* Only 2D allowed for 4K */
		if (dynamic_pointer_cast<const StereoPictureAsset>(asset)) {
			notes.push_back(
				VerificationNote(
					VerificationNote::VERIFY_BV21_ERROR,
					VerificationNote::INVALID_PICTURE_ASSET_RESOLUTION_FOR_3D,
					String::compose("%1/%2", asset->edit_rate().numerator, asset->edit_rate().denominator),
					file
					)
				);

		}
	}

}


static void
verify_main_sound_asset (
	shared_ptr<const DCP> dcp,
	shared_ptr<const ReelSoundAsset> reel_asset,
	function<void (string, optional<boost::filesystem::path>)> stage,
	function<void (float)> progress,
	vector<VerificationNote>& notes
	)
{
	auto asset = reel_asset->asset();
	stage ("Checking sound asset hash", asset->file());
	auto const r = verify_asset (dcp, reel_asset, progress);
	switch (r) {
		case VERIFY_ASSET_RESULT_BAD:
			notes.push_back (
				VerificationNote(
					VerificationNote::VERIFY_ERROR, VerificationNote::INCORRECT_SOUND_HASH, *asset->file()
					)
				);
			break;
		case VERIFY_ASSET_RESULT_CPL_PKL_DIFFER:
			notes.push_back (
				VerificationNote(
					VerificationNote::VERIFY_ERROR, VerificationNote::MISMATCHED_SOUND_HASHES, *asset->file()
					)
				);
			break;
		default:
			break;
	}

	stage ("Checking sound asset metadata", asset->file());

	verify_language_tag (asset->language(), notes);
	if (asset->sampling_rate() != 48000) {
		notes.push_back (
			VerificationNote(
				VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_SOUND_FRAME_RATE, raw_convert<string>(asset->sampling_rate()), *asset->file()
				)
			);
	}
}


static void
verify_main_subtitle_reel (shared_ptr<const ReelSubtitleAsset> reel_asset, vector<VerificationNote>& notes)
{
	/* XXX: is Language compulsory? */
	if (reel_asset->language()) {
		verify_language_tag (*reel_asset->language(), notes);
	}

	if (!reel_asset->entry_point()) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_SUBTITLE_ENTRY_POINT, reel_asset->id() });
	} else if (reel_asset->entry_point().get()) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INCORRECT_SUBTITLE_ENTRY_POINT, reel_asset->id() });
	}
}


static void
verify_closed_caption_reel (shared_ptr<const ReelClosedCaptionAsset> reel_asset, vector<VerificationNote>& notes)
{
	/* XXX: is Language compulsory? */
	if (reel_asset->language()) {
		verify_language_tag (*reel_asset->language(), notes);
	}

	if (!reel_asset->entry_point()) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_CLOSED_CAPTION_ENTRY_POINT, reel_asset->id() });
	} else if (reel_asset->entry_point().get()) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INCORRECT_CLOSED_CAPTION_ENTRY_POINT, reel_asset->id() });
	}
}


struct State
{
	boost::optional<string> subtitle_language;
};



void
verify_smpte_subtitle_asset (
	shared_ptr<const SMPTESubtitleAsset> asset,
	vector<VerificationNote>& notes,
	State& state
	)
{
	if (asset->language()) {
		auto const language = *asset->language();
		verify_language_tag (language, notes);
		if (!state.subtitle_language) {
			state.subtitle_language = language;
		} else if (state.subtitle_language != language) {
			notes.push_back ({ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISMATCHED_SUBTITLE_LANGUAGES });
		}
	} else {
		notes.push_back ({ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_SUBTITLE_LANGUAGE, *asset->file() });
	}
	auto const size = boost::filesystem::file_size(asset->file().get());
	if (size > 115 * 1024 * 1024) {
		notes.push_back (
			{ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_TIMED_TEXT_SIZE_IN_BYTES, raw_convert<string>(size), *asset->file() }
			);
	}
	/* XXX: I'm not sure what Bv2.1_7.2.1 means when it says "the font resource shall not be larger than 10MB"
	 * but I'm hoping that checking for the total size of all fonts being <= 10MB will do.
	 */
	auto fonts = asset->font_data ();
	int total_size = 0;
	for (auto i: fonts) {
		total_size += i.second.size();
	}
	if (total_size > 10 * 1024 * 1024) {
		notes.push_back ({ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_TIMED_TEXT_FONT_SIZE_IN_BYTES, raw_convert<string>(total_size), asset->file().get() });
	}

	if (!asset->start_time()) {
		notes.push_back ({ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_SUBTITLE_START_TIME, asset->file().get() });
	} else if (asset->start_time() != Time()) {
		notes.push_back ({ VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_SUBTITLE_START_TIME, asset->file().get() });
	}
}


static void
verify_subtitle_asset (
	shared_ptr<const SubtitleAsset> asset,
	function<void (string, optional<boost::filesystem::path>)> stage,
	boost::filesystem::path xsd_dtd_directory,
	vector<VerificationNote>& notes,
	State& state
	)
{
	stage ("Checking subtitle XML", asset->file());
	/* Note: we must not use SubtitleAsset::xml_as_string() here as that will mean the data on disk
	 * gets passed through libdcp which may clean up and therefore hide errors.
	 */
	validate_xml (asset->raw_xml(), xsd_dtd_directory, notes);

	auto smpte = dynamic_pointer_cast<const SMPTESubtitleAsset>(asset);
	if (smpte) {
		verify_smpte_subtitle_asset (smpte, notes, state);
	}
}


static void
verify_closed_caption_asset (
	shared_ptr<const SubtitleAsset> asset,
	function<void (string, optional<boost::filesystem::path>)> stage,
	boost::filesystem::path xsd_dtd_directory,
	vector<VerificationNote>& notes,
	State& state
	)
{
	verify_subtitle_asset (asset, stage, xsd_dtd_directory, notes, state);

	if (asset->raw_xml().size() > 256 * 1024) {
		notes.push_back (
			VerificationNote(
				VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_CLOSED_CAPTION_XML_SIZE_IN_BYTES, raw_convert<string>(asset->raw_xml().size()), *asset->file()
				)
			);
	}
}


static
void
check_text_timing (
	vector<shared_ptr<Reel>> reels,
	optional<int> picture_frame_rate,
	vector<VerificationNote>& notes,
	std::function<bool (shared_ptr<Reel>)> check,
	std::function<string (shared_ptr<Reel>)> xml,
	std::function<int64_t (shared_ptr<Reel>)> duration
	)
{
	/* end of last subtitle (in editable units) */
	optional<int64_t> last_out;
	auto too_short = false;
	auto too_close = false;
	auto too_early = false;
	/* current reel start time (in editable units) */
	int64_t reel_offset = 0;

	std::function<void (cxml::ConstNodePtr, int, int, bool)> parse;
	parse = [&parse, &last_out, &too_short, &too_close, &too_early, &reel_offset](cxml::ConstNodePtr node, int tcr, int pfr, bool first_reel) {
		if (node->name() == "Subtitle") {
			Time in (node->string_attribute("TimeIn"), tcr);
			Time out (node->string_attribute("TimeOut"), tcr);
			if (first_reel && in < Time(0, 0, 4, 0, tcr)) {
				too_early = true;
			}
			auto length = out - in;
			if (length.as_editable_units(pfr) < 15) {
				too_short = true;
			}
			if (last_out) {
				/* XXX: this feels dubious - is it really what Bv2.1 means? */
				auto distance = reel_offset + in.as_editable_units(pfr) - *last_out;
				if (distance >= 0 && distance < 2) {
					too_close = true;
				}
			}
			last_out = reel_offset + out.as_editable_units(pfr);
		} else {
			for (auto i: node->node_children()) {
				parse(i, tcr, pfr, first_reel);
			}
		}
	};

	for (auto i = 0U; i < reels.size(); ++i) {
		if (!check(reels[i])) {
			continue;
		}

		/* We need to look at <Subtitle> instances in the XML being checked, so we can't use the subtitles
		 * read in by libdcp's parser.
		 */

		auto doc = make_shared<cxml::Document>("SubtitleReel");
		doc->read_string (xml(reels[i]));
		auto const tcr = doc->number_child<int>("TimeCodeRate");
		parse (doc, tcr, picture_frame_rate.get_value_or(24), i == 0);
		reel_offset += duration(reels[i]);
	}

	if (too_early) {
		notes.push_back({
			VerificationNote::VERIFY_WARNING, VerificationNote::INVALID_SUBTITLE_FIRST_TEXT_TIME
		});
	}

	if (too_short) {
		notes.push_back ({
			VerificationNote::VERIFY_WARNING, VerificationNote::INVALID_SUBTITLE_DURATION
		});
	}

	if (too_close) {
		notes.push_back ({
			VerificationNote::VERIFY_WARNING, VerificationNote::INVALID_SUBTITLE_SPACING
		});
	}
}


struct LinesCharactersResult
{
	bool warning_length_exceeded = false;
	bool error_length_exceeded = false;
	bool line_count_exceeded = false;
};


static
void
check_text_lines_and_characters (
	shared_ptr<SubtitleAsset> asset,
	int warning_length,
	int error_length,
	LinesCharactersResult* result
	)
{
	class Event
	{
	public:
		Event (Time time_, float position_, int characters_)
			: time (time_)
			, position (position_)
			, characters (characters_)
		{}

		Event (Time time_, shared_ptr<Event> start_)
			: time (time_)
			, start (start_)
		{}

		Time time;
		int position; //< position from 0 at top of screen to 100 at bottom
		int characters;
		shared_ptr<Event> start;
	};

	vector<shared_ptr<Event>> events;

	auto position = [](shared_ptr<const SubtitleString> sub) {
		switch (sub->v_align()) {
		case VALIGN_TOP:
			return lrintf(sub->v_position() * 100);
		case VALIGN_CENTER:
			return lrintf((0.5f + sub->v_position()) * 100);
		case VALIGN_BOTTOM:
			return lrintf((1.0f - sub->v_position()) * 100);
		}

		return 0L;
	};

	for (auto j: asset->subtitles()) {
		auto text = dynamic_pointer_cast<const SubtitleString>(j);
		if (text) {
			auto in = make_shared<Event>(text->in(), position(text), text->text().length());
			events.push_back(in);
			events.push_back(make_shared<Event>(text->out(), in));
		}
	}

	std::sort(events.begin(), events.end(), [](shared_ptr<Event> const& a, shared_ptr<Event>const& b) {
		return a->time < b->time;
	});

	map<int, int> current;
	for (auto i: events) {
		if (current.size() > 3) {
			result->line_count_exceeded = true;
		}
		for (auto j: current) {
			if (j.second >= warning_length) {
				result->warning_length_exceeded = true;
			}
			if (j.second >= error_length) {
				result->error_length_exceeded = true;
			}
		}

		if (i->start) {
			/* end of a subtitle */
			DCP_ASSERT (current.find(i->start->position) != current.end());
			if (current[i->start->position] == i->start->characters) {
				current.erase(i->start->position);
			} else {
				current[i->start->position] -= i->start->characters;
			}
		} else {
			/* start of a subtitle */
			if (current.find(i->position) == current.end()) {
				current[i->position] = i->characters;
			} else {
				current[i->position] += i->characters;
			}
		}
	}
}


static
void
check_text_timing (vector<shared_ptr<Reel>> reels, vector<VerificationNote>& notes)
{
	if (reels.empty()) {
		return;
	}

	optional<int> picture_frame_rate;
	if (reels[0]->main_picture()) {
		picture_frame_rate = reels[0]->main_picture()->frame_rate().numerator;
	}

	if (reels[0]->main_subtitle()) {
		check_text_timing (reels, picture_frame_rate, notes,
			[](shared_ptr<Reel> reel) {
				return static_cast<bool>(reel->main_subtitle());
			},
			[](shared_ptr<Reel> reel) {
				return reel->main_subtitle()->asset()->raw_xml();
			},
			[](shared_ptr<Reel> reel) {
				return reel->main_subtitle()->actual_duration();
			}
		);
	}

	for (auto i = 0U; i < reels[0]->closed_captions().size(); ++i) {
		check_text_timing (reels, picture_frame_rate, notes,
			[i](shared_ptr<Reel> reel) {
				return i < reel->closed_captions().size();
			},
			[i](shared_ptr<Reel> reel) {
				return reel->closed_captions()[i]->asset()->raw_xml();
			},
			[i](shared_ptr<Reel> reel) {
				return reel->closed_captions()[i]->actual_duration();
			}
		);
	}
}


void
check_extension_metadata (shared_ptr<CPL> cpl, vector<VerificationNote>& notes)
{
	DCP_ASSERT (cpl->file());
	cxml::Document doc ("CompositionPlaylist");
	doc.read_file (cpl->file().get());

	auto missing = false;
	string malformed;

	if (auto reel_list = doc.node_child("ReelList")) {
		auto reels = reel_list->node_children("Reel");
		if (!reels.empty()) {
			if (auto asset_list = reels[0]->optional_node_child("AssetList")) {
				if (auto metadata = asset_list->optional_node_child("CompositionMetadataAsset")) {
					if (auto extension_list = metadata->optional_node_child("ExtensionMetadataList")) {
						missing = true;
						for (auto extension: extension_list->node_children("ExtensionMetadata")) {
							if (extension->optional_string_attribute("scope").get_value_or("") != "http://isdcf.com/ns/cplmd/app") {
								continue;
							}
							missing = false;
							if (auto name = extension->optional_node_child("Name")) {
								if (name->content() != "Application") {
									malformed = "<Name> should be 'Application'";
								}
							}
							if (auto property_list = extension->optional_node_child("PropertyList")) {
								if (auto property = property_list->optional_node_child("Property")) {
									if (auto name = property->optional_node_child("Name")) {
										if (name->content() != "DCP Constraints Profile") {
											malformed = "<Name> property should be 'DCP Constraints Profile'";
										}
									}
									if (auto value = property->optional_node_child("Value")) {
										if (value->content() != "SMPTE-RDD-52:2020-Bv2.1") {
											malformed = "<Value> property should be 'SMPTE-RDD-52:2020-Bv2.1'";
										}
									}
								}
							}
						}
					} else {
						missing = true;
					}
				}
			}
		}
	}

	if (missing) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_EXTENSION_METADATA, cpl->id(), cpl->file().get()});
	} else if (!malformed.empty()) {
		notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_EXTENSION_METADATA, malformed, cpl->file().get()});
	}
}


bool
pkl_has_encrypted_assets (shared_ptr<DCP> dcp, shared_ptr<PKL> pkl)
{
	vector<string> encrypted;
	for (auto i: dcp->cpls()) {
		for (auto j: i->reel_mxfs()) {
			if (j->asset_ref().resolved()) {
				/* It's a bit surprising / broken but Interop subtitle assets are represented
				 * in reels by ReelSubtitleAsset which inherits ReelMXF, so it's possible for
				 * ReelMXFs to have assets which are not MXFs.
				 */
				if (auto asset = dynamic_pointer_cast<MXF>(j->asset_ref().asset())) {
					if (asset->encrypted()) {
						encrypted.push_back(j->asset_ref().id());
					}
				}
			}
		}
	}

	for (auto i: pkl->asset_list()) {
		if (find(encrypted.begin(), encrypted.end(), i->id()) != encrypted.end()) {
			return true;
		}
	}

	return false;
}


vector<VerificationNote>
dcp::verify (
	vector<boost::filesystem::path> directories,
	function<void (string, optional<boost::filesystem::path>)> stage,
	function<void (float)> progress,
	boost::filesystem::path xsd_dtd_directory
	)
{
	xsd_dtd_directory = boost::filesystem::canonical (xsd_dtd_directory);

	vector<VerificationNote> notes;
	State state;

	vector<shared_ptr<DCP>> dcps;
	for (auto i: directories) {
		dcps.push_back (shared_ptr<DCP> (new DCP (i)));
	}

	for (auto dcp: dcps) {
		stage ("Checking DCP", dcp->directory());
		try {
			dcp->read (&notes);
		} catch (ReadError& e) {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::FAILED_READ, string(e.what())));
		} catch (XMLError& e) {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::FAILED_READ, string(e.what())));
		} catch (MXFFileError& e) {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::FAILED_READ, string(e.what())));
		} catch (cxml::Error& e) {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::FAILED_READ, string(e.what())));
		}

		if (dcp->standard() != SMPTE) {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_STANDARD));
		}

		for (auto cpl: dcp->cpls()) {
			stage ("Checking CPL", cpl->file());
			validate_xml (cpl->file().get(), xsd_dtd_directory, notes);

			if (cpl->any_encrypted() && !cpl->all_encrypted()) {
				notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::PARTIALLY_ENCRYPTED});
			}

			for (auto const& i: cpl->additional_subtitle_languages()) {
				verify_language_tag (i, notes);
			}

			if (cpl->release_territory()) {
				if (!cpl->release_territory_scope() || cpl->release_territory_scope().get() != "http://www.smpte-ra.org/schemas/429-16/2014/CPL-Metadata#scope/release-territory/UNM49") {
					auto terr = cpl->release_territory().get();
					/* Must be a valid region tag, or "001" */
					try {
						LanguageTag::RegionSubtag test (terr);
					} catch (...) {
						if (terr != "001") {
							notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_LANGUAGE, terr});
						}
					}
				}
			}

			if (dcp->standard() == SMPTE) {
				if (!cpl->annotation_text()) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_CPL_ANNOTATION_TEXT, cpl->id(), cpl->file().get()});
				} else if (cpl->annotation_text().get() != cpl->content_title_text()) {
					notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::MISMATCHED_CPL_ANNOTATION_TEXT, cpl->id(), cpl->file().get()});
				}
			}

			for (auto i: dcp->pkls()) {
				/* Check that the CPL's hash corresponds to the PKL */
				optional<string> h = i->hash(cpl->id());
				if (h && make_digest(ArrayData(*cpl->file())) != *h) {
					notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::MISMATCHED_CPL_HASHES, cpl->id(), cpl->file().get()));
				}

				/* Check that any PKL with a single CPL has its AnnotationText the same as the CPL's ContentTitleText */
				optional<string> required_annotation_text;
				for (auto j: i->asset_list()) {
					/* See if this is a CPL */
					for (auto k: dcp->cpls()) {
						if (j->id() == k->id()) {
							if (!required_annotation_text) {
								/* First CPL we have found; this is the required AnnotationText unless we find another */
								required_annotation_text = cpl->content_title_text();
							} else {
								/* There's more than one CPL so we don't care what the PKL's AnnotationText is */
								required_annotation_text = boost::none;
							}
						}
					}
				}

				if (required_annotation_text && i->annotation_text() != required_annotation_text) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISMATCHED_PKL_ANNOTATION_TEXT_WITH_CPL, i->id(), i->file().get()});
				}
			}

			/* set to true if any reel has a MainSubtitle */
			auto have_main_subtitle = false;
			/* set to true if any reel has no MainSubtitle */
			auto have_no_main_subtitle = false;
			/* fewest number of closed caption assets seen in a reel */
			size_t fewest_closed_captions = SIZE_MAX;
			/* most number of closed caption assets seen in a reel */
			size_t most_closed_captions = 0;
			map<Marker, Time> markers_seen;

			for (auto reel: cpl->reels()) {
				stage ("Checking reel", optional<boost::filesystem::path>());

				for (auto i: reel->assets()) {
					if (i->duration() && (i->duration().get() * i->edit_rate().denominator / i->edit_rate().numerator) < 1) {
						notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::INVALID_DURATION, i->id()));
					}
					if ((i->intrinsic_duration() * i->edit_rate().denominator / i->edit_rate().numerator) < 1) {
						notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::INVALID_INTRINSIC_DURATION, i->id()));
					}
					auto mxf = dynamic_pointer_cast<ReelMXF>(i);
					if (mxf && !mxf->hash()) {
						notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_HASH, i->id()});
					}
				}

				if (dcp->standard() == SMPTE) {
					boost::optional<int64_t> duration;
					for (auto i: reel->assets()) {
						if (!duration) {
							duration = i->actual_duration();
						} else if (*duration != i->actual_duration()) {
							notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISMATCHED_ASSET_DURATION});
							break;
						}
					}
				}

				if (reel->main_picture()) {
					/* Check reel stuff */
					auto const frame_rate = reel->main_picture()->frame_rate();
					if (frame_rate.denominator != 1 ||
					    (frame_rate.numerator != 24 &&
					     frame_rate.numerator != 25 &&
					     frame_rate.numerator != 30 &&
					     frame_rate.numerator != 48 &&
					     frame_rate.numerator != 50 &&
					     frame_rate.numerator != 60 &&
					     frame_rate.numerator != 96)) {
						notes.push_back ({
							VerificationNote::VERIFY_ERROR,
							VerificationNote::INVALID_PICTURE_FRAME_RATE,
							String::compose("%1/%2", frame_rate.numerator, frame_rate.denominator)
						});
					}
					/* Check asset */
					if (reel->main_picture()->asset_ref().resolved()) {
						verify_main_picture_asset (dcp, reel->main_picture(), stage, progress, notes);
					}
				}

				if (reel->main_sound() && reel->main_sound()->asset_ref().resolved()) {
					verify_main_sound_asset (dcp, reel->main_sound(), stage, progress, notes);
				}

				if (reel->main_subtitle()) {
					verify_main_subtitle_reel (reel->main_subtitle(), notes);
					if (reel->main_subtitle()->asset_ref().resolved()) {
						verify_subtitle_asset (reel->main_subtitle()->asset(), stage, xsd_dtd_directory, notes, state);
					}
					have_main_subtitle = true;
				} else {
					have_no_main_subtitle = true;
				}

				for (auto i: reel->closed_captions()) {
					verify_closed_caption_reel (i, notes);
					if (i->asset_ref().resolved()) {
						verify_closed_caption_asset (i->asset(), stage, xsd_dtd_directory, notes, state);
					}
				}

				if (reel->main_markers()) {
					for (auto const& i: reel->main_markers()->get()) {
						markers_seen.insert (i);
					}
				}

				fewest_closed_captions = std::min (fewest_closed_captions, reel->closed_captions().size());
				most_closed_captions = std::max (most_closed_captions, reel->closed_captions().size());
			}

			if (dcp->standard() == SMPTE) {

				if (have_main_subtitle && have_no_main_subtitle) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_MAIN_SUBTITLE_FROM_SOME_REELS});
				}

				if (fewest_closed_captions != most_closed_captions) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISMATCHED_CLOSED_CAPTION_ASSET_COUNTS});
				}

				if (cpl->content_kind() == FEATURE) {
					if (markers_seen.find(Marker::FFEC) == markers_seen.end()) {
						notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_FFEC_IN_FEATURE});
					}
					if (markers_seen.find(Marker::FFMC) == markers_seen.end()) {
						notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_FFMC_IN_FEATURE});
					}
				}

				auto ffoc = markers_seen.find(Marker::FFOC);
				if (ffoc == markers_seen.end()) {
					notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::MISSING_FFOC});
				} else if (ffoc->second.e != 1) {
					notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::INCORRECT_FFOC, raw_convert<string>(ffoc->second.e)});
				}

				auto lfoc = markers_seen.find(Marker::LFOC);
				if (lfoc == markers_seen.end()) {
					notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::MISSING_LFOC});
				} else {
					auto lfoc_time = lfoc->second.as_editable_units(lfoc->second.tcr);
					if (lfoc_time != (cpl->reels().back()->duration() - 1)) {
						notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::INCORRECT_LFOC, raw_convert<string>(lfoc_time)});
					}
				}

				check_text_timing (cpl->reels(), notes);

				LinesCharactersResult result;
				for (auto reel: cpl->reels()) {
					if (reel->main_subtitle() && reel->main_subtitle()->asset()) {
						check_text_lines_and_characters (reel->main_subtitle()->asset(), 52, 79, &result);
					}
				}

				if (result.line_count_exceeded) {
					notes.push_back ({VerificationNote::VERIFY_WARNING, VerificationNote::INVALID_SUBTITLE_LINE_COUNT});
				}
				if (result.error_length_exceeded) {
					notes.push_back (VerificationNote(VerificationNote::VERIFY_WARNING, VerificationNote::INVALID_SUBTITLE_LINE_LENGTH));
				} else if (result.warning_length_exceeded) {
					notes.push_back (VerificationNote(VerificationNote::VERIFY_WARNING, VerificationNote::NEARLY_INVALID_SUBTITLE_LINE_LENGTH));
				}

				result = LinesCharactersResult();
				for (auto reel: cpl->reels()) {
					for (auto i: reel->closed_captions()) {
						if (i->asset()) {
							check_text_lines_and_characters (i->asset(), 32, 32, &result);
						}
					}
				}

				if (result.line_count_exceeded) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_CLOSED_CAPTION_LINE_COUNT});
				}
				if (result.error_length_exceeded) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::INVALID_CLOSED_CAPTION_LINE_LENGTH});
				}

				if (!cpl->full_content_title_text()) {
					/* Since FullContentTitleText is assumed always to exist if there's a CompositionMetadataAsset we
					 * can use it as a proxy for CompositionMetadataAsset's existence.
					 */
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_CPL_METADATA, cpl->id(), cpl->file().get()});
				} else if (!cpl->version_number()) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::MISSING_CPL_METADATA_VERSION_NUMBER, cpl->id(), cpl->file().get()});
				}

				check_extension_metadata (cpl, notes);

				if (cpl->any_encrypted()) {
					cxml::Document doc ("CompositionPlaylist");
					DCP_ASSERT (cpl->file());
					doc.read_file (cpl->file().get());
					if (!doc.optional_node_child("Signature")) {
						notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::UNSIGNED_CPL_WITH_ENCRYPTED_CONTENT, cpl->id(), cpl->file().get()});
					}
				}
			}
		}

		for (auto pkl: dcp->pkls()) {
			stage ("Checking PKL", pkl->file());
			validate_xml (pkl->file().get(), xsd_dtd_directory, notes);
			if (pkl_has_encrypted_assets(dcp, pkl)) {
				cxml::Document doc ("PackingList");
				doc.read_file (pkl->file().get());
				if (!doc.optional_node_child("Signature")) {
					notes.push_back ({VerificationNote::VERIFY_BV21_ERROR, VerificationNote::UNSIGNED_PKL_WITH_ENCRYPTED_CONTENT, pkl->id(), pkl->file().get()});
				}
			}
		}

		if (dcp->asset_map_path()) {
			stage ("Checking ASSETMAP", dcp->asset_map_path().get());
			validate_xml (dcp->asset_map_path().get(), xsd_dtd_directory, notes);
		} else {
			notes.push_back (VerificationNote(VerificationNote::VERIFY_ERROR, VerificationNote::MISSING_ASSETMAP));
		}
	}

	return notes;
}

string
dcp::note_to_string (VerificationNote note)
{
	/** These strings should say what is wrong, incorporating any extra details (ID, filenames etc.).
   	 *
	 *  e.g. "ClosedCaption asset has no <EntryPoint> tag.",
	 *  not "ClosedCaption assets must have an <EntryPoint> tag."
	 *
	 *  It's OK to use XML tag names where they are clear.
	 *  If both ID and filename are available, use only the ID.
	 *  End messages with a full stop.
	 *  Messages should not mention whether or not their errors are a part of Bv2.1.
	 */
	switch (note.code()) {
	case VerificationNote::FAILED_READ:
		return *note.note();
	case VerificationNote::MISMATCHED_CPL_HASHES:
		return String::compose("The hash of the CPL %1 in the PKL does not agree with the CPL file.", note.note().get());
	case VerificationNote::INVALID_PICTURE_FRAME_RATE:
		return String::compose("The picture in a reel has an invalid frame rate %1.", note.note().get());
	case VerificationNote::INCORRECT_PICTURE_HASH:
		return String::compose("The hash of the picture asset %1 does not agree with the PKL file.", note.file()->filename());
	case VerificationNote::MISMATCHED_PICTURE_HASHES:
		return String::compose("The PKL and CPL hashes differ for the picture asset %1.", note.file()->filename());
	case VerificationNote::INCORRECT_SOUND_HASH:
		return String::compose("The hash of the sound asset %1 does not agree with the PKL file.", note.file()->filename());
	case VerificationNote::MISMATCHED_SOUND_HASHES:
		return String::compose("The PKL and CPL hashes differ for the sound asset %1.", note.file()->filename());
	case VerificationNote::EMPTY_ASSET_PATH:
		return "The asset map contains an empty asset path.";
	case VerificationNote::MISSING_ASSET:
		return String::compose("The file %1 for an asset in the asset map cannot be found.", note.file()->filename());
	case VerificationNote::MISMATCHED_STANDARD:
		return "The DCP contains both SMPTE and Interop parts.";
	case VerificationNote::INVALID_XML:
		return String::compose("An XML file is badly formed: %1 (%2:%3)", note.note().get(), note.file()->filename(), note.line().get());
	case VerificationNote::MISSING_ASSETMAP:
		return "No ASSETMAP or ASSETMAP.xml was found.";
	case VerificationNote::INVALID_INTRINSIC_DURATION:
		return String::compose("The intrinsic duration of the asset %1 is less than 1 second long.", note.note().get());
	case VerificationNote::INVALID_DURATION:
		return String::compose("The duration of the asset %1 is less than 1 second long.", note.note().get());
	case VerificationNote::INVALID_PICTURE_FRAME_SIZE_IN_BYTES:
		return String::compose("The instantaneous bit rate of the picture asset %1 is larger than the limit of 250Mbit/s in at least one place.", note.file()->filename());
	case VerificationNote::NEARLY_INVALID_PICTURE_FRAME_SIZE_IN_BYTES:
		return String::compose("The instantaneous bit rate of the picture asset %1 is close to the limit of 250Mbit/s in at least one place.", note.file()->filename());
	case VerificationNote::EXTERNAL_ASSET:
		return String::compose("The asset %1 that this DCP refers to is not included in the DCP.  It may be a VF.", note.note().get());
	case VerificationNote::INVALID_STANDARD:
		return "This DCP does not use the SMPTE standard.";
	case VerificationNote::INVALID_LANGUAGE:
		return String::compose("The DCP specifies a language '%1' which does not conform to the RFC 5646 standard.", note.note().get());
	case VerificationNote::INVALID_PICTURE_SIZE_IN_PIXELS:
		return String::compose("The size %1 of picture asset %2 is not allowed.", note.note().get(), note.file()->filename());
	case VerificationNote::INVALID_PICTURE_FRAME_RATE_FOR_2K:
		return String::compose("The frame rate %1 of picture asset %2 is not allowed for 2K DCPs.", note.note().get(), note.file()->filename());
	case VerificationNote::INVALID_PICTURE_FRAME_RATE_FOR_4K:
		return String::compose("The frame rate %1 of picture asset %2 is not allowed for 4K DCPs.", note.note().get(), note.file()->filename());
	case VerificationNote::INVALID_PICTURE_ASSET_RESOLUTION_FOR_3D:
		return "3D 4K DCPs are not allowed.";
	case VerificationNote::INVALID_CLOSED_CAPTION_XML_SIZE_IN_BYTES:
		return String::compose("The size %1 of the closed caption asset %2 is larger than the 256KB maximum.", note.note().get(), note.file()->filename());
	case VerificationNote::INVALID_TIMED_TEXT_SIZE_IN_BYTES:
		return String::compose("The size %1 of the timed text asset %2 is larger than the 115MB maximum.", note.note().get(), note.file()->filename());
	case VerificationNote::INVALID_TIMED_TEXT_FONT_SIZE_IN_BYTES:
		return String::compose("The size %1 of the fonts in timed text asset %2 is larger than the 10MB maximum.", note.note().get(), note.file()->filename());
	case VerificationNote::MISSING_SUBTITLE_LANGUAGE:
		return String::compose("The XML for the SMPTE subtitle asset %1 has no <Language> tag.", note.file()->filename());
	case VerificationNote::MISMATCHED_SUBTITLE_LANGUAGES:
		return "Some subtitle assets have different <Language> tags than others";
	case VerificationNote::MISSING_SUBTITLE_START_TIME:
		return String::compose("The XML for the SMPTE subtitle asset %1 has no <StartTime> tag.", note.file()->filename());
	case VerificationNote::INVALID_SUBTITLE_START_TIME:
		return String::compose("The XML for a SMPTE subtitle asset %1 has a non-zero <StartTime> tag.", note.file()->filename());
	case VerificationNote::INVALID_SUBTITLE_FIRST_TEXT_TIME:
		return "The first subtitle or closed caption is less than 4 seconds from the start of the DCP.";
	case VerificationNote::INVALID_SUBTITLE_DURATION:
		return "At least one subtitle lasts less than 15 frames.";
	case VerificationNote::INVALID_SUBTITLE_SPACING:
		return "At least one pair of subtitles is separated by less than 2 frames.";
	case VerificationNote::INVALID_SUBTITLE_LINE_COUNT:
		return "There are more than 3 subtitle lines in at least one place in the DCP.";
	case VerificationNote::NEARLY_INVALID_SUBTITLE_LINE_LENGTH:
		return "There are more than 52 characters in at least one subtitle line.";
	case VerificationNote::INVALID_SUBTITLE_LINE_LENGTH:
		return "There are more than 79 characters in at least one subtitle line.";
	case VerificationNote::INVALID_CLOSED_CAPTION_LINE_COUNT:
		return "There are more than 3 closed caption lines in at least one place.";
	case VerificationNote::INVALID_CLOSED_CAPTION_LINE_LENGTH:
		return "There are more than 32 characters in at least one closed caption line.";
	case VerificationNote::INVALID_SOUND_FRAME_RATE:
		return String::compose("The sound asset %1 has a sampling rate of %2", note.file()->filename(), note.note().get());
	case VerificationNote::MISSING_CPL_ANNOTATION_TEXT:
		return String::compose("The CPL %1 has no <AnnotationText> tag.", note.note().get());
	case VerificationNote::MISMATCHED_CPL_ANNOTATION_TEXT:
		return String::compose("The CPL %1 has an <AnnotationText> which differs from its <ContentTitleText>", note.note().get());
	case VerificationNote::MISMATCHED_ASSET_DURATION:
		return "All assets in a reel do not have the same duration.";
	case VerificationNote::MISSING_MAIN_SUBTITLE_FROM_SOME_REELS:
		return "At least one reel contains a subtitle asset, but some reel(s) do not";
	case VerificationNote::MISMATCHED_CLOSED_CAPTION_ASSET_COUNTS:
		return "At least one reel has closed captions, but reels have different numbers of closed caption assets.";
	case VerificationNote::MISSING_SUBTITLE_ENTRY_POINT:
		return String::compose("The subtitle asset %1 has no <EntryPoint> tag.", note.note().get());
	case VerificationNote::INCORRECT_SUBTITLE_ENTRY_POINT:
		return String::compose("The subtitle asset %1 has an <EntryPoint> other than 0.", note.note().get());
	case VerificationNote::MISSING_CLOSED_CAPTION_ENTRY_POINT:
		return String::compose("The closed caption asset %1 has no <EntryPoint> tag.", note.note().get());
	case VerificationNote::INCORRECT_CLOSED_CAPTION_ENTRY_POINT:
		return String::compose("The closed caption asset %1 has an <EntryPoint> other than 0.", note.note().get());
	case VerificationNote::MISSING_HASH:
		return String::compose("The asset %1 has no <Hash> tag in the CPL.", note.note().get());
	case VerificationNote::MISSING_FFEC_IN_FEATURE:
		return "The DCP is marked as a Feature but there is no FFEC (first frame of end credits) marker";
	case VerificationNote::MISSING_FFMC_IN_FEATURE:
		return "The DCP is marked as a Feature but there is no FFMC (first frame of moving credits) marker";
	case VerificationNote::MISSING_FFOC:
		return "There should be a FFOC (first frame of content) marker";
	case VerificationNote::MISSING_LFOC:
		return "There should be a LFOC (last frame of content) marker";
	case VerificationNote::INCORRECT_FFOC:
		return String::compose("The FFOC marker is %1 instead of 1", note.note().get());
	case VerificationNote::INCORRECT_LFOC:
		return String::compose("The LFOC marker is %1 instead of 1 less than the duration of the last reel.", note.note().get());
	case VerificationNote::MISSING_CPL_METADATA:
		return String::compose("The CPL %1 has no <CompositionMetadataAsset> tag.", note.note().get());
	case VerificationNote::MISSING_CPL_METADATA_VERSION_NUMBER:
		return String::compose("The CPL %1 has no <VersionNumber> in its <CompositionMetadataAsset>.", note.note().get());
	case VerificationNote::MISSING_EXTENSION_METADATA:
		return String::compose("The CPL %1 has no <ExtensionMetadata> in its <CompositionMetadataAsset>.", note.note().get());
	case VerificationNote::INVALID_EXTENSION_METADATA:
		return String::compose("The CPL %1 has a malformed <ExtensionMetadata> (%2).", note.file()->filename(), note.note().get());
	case VerificationNote::UNSIGNED_CPL_WITH_ENCRYPTED_CONTENT:
		return String::compose("The CPL %1, which has encrypted content, is not signed.", note.note().get());
	case VerificationNote::UNSIGNED_PKL_WITH_ENCRYPTED_CONTENT:
		return String::compose("The PKL %1, which has encrypted content, is not signed.", note.note().get());
	case VerificationNote::MISMATCHED_PKL_ANNOTATION_TEXT_WITH_CPL:
		return String::compose("The PKL %1 has only one CPL but its <AnnotationText> does not match the CPL's <ContentTitleText>", note.note().get());
	case VerificationNote::PARTIALLY_ENCRYPTED:
		return "Some assets are encrypted but some are not";
	}

	return "";
}


bool
dcp::operator== (dcp::VerificationNote const& a, dcp::VerificationNote const& b)
{
	return a.type() == b.type() && a.code() == b.code() && a.note() == b.note() && a.file() == b.file() && a.line() == b.line();
}

std::ostream&
dcp::operator<< (std::ostream& s, dcp::VerificationNote const& note)
{
	s << note_to_string (note);
	if (note.note()) {
		s << " [" << note.note().get() << "]";
	}
	if (note.file()) {
		s << " [" << note.file().get() << "]";
	}
	if (note.line()) {
		s << " [" << note.line().get() << "]";
	}
	return s;
}


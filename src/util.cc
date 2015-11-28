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

/** @file  src/util.cc
 *  @brief Utility methods.
 */

#include "util.h"
#include "exceptions.h"
#include "types.h"
#include "certificate.h"
#include "openjpeg_image.h"
#include "dcp_assert.h"
#include "compose.hpp"
#include "KM_util.h"
#include "KM_fileio.h"
#include "AS_DCP.h"
#include <openjpeg.h>
#include <xmlsec/xmldsig.h>
#include <xmlsec/dl.h>
#include <xmlsec/app.h>
#include <xmlsec/crypto.h>
#include <libxml++/nodes/element.h>
#include <libxml++/document.h>
#include <openssl/sha.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <iomanip>

using std::string;
using std::wstring;
using std::cout;
using std::stringstream;
using std::min;
using std::max;
using std::list;
using std::setw;
using std::setfill;
using std::ostream;
using boost::shared_ptr;
using boost::shared_array;
using boost::optional;
using boost::function;
using boost::algorithm::trim;
using namespace dcp;

/** Create a UUID.
 *  @return UUID.
 */
string
dcp::make_uuid ()
{
	char buffer[64];
	Kumu::UUID id;
	Kumu::GenRandomValue (id);
	id.EncodeHex (buffer, 64);
	return string (buffer);
}


/** Create a digest for a file.
 *  @param filename File name.
 *  @param progress Optional progress reporting function.  The function will be called
 *  with a progress value between 0 and 1.
 *  @return Digest.
 */
string
dcp::make_digest (boost::filesystem::path filename, function<void (float)> progress)
{
	Kumu::FileReader reader;
	Kumu::Result_t r = reader.OpenRead (filename.string().c_str ());
	if (ASDCP_FAILURE (r)) {
		boost::throw_exception (FileError ("could not open file to compute digest", filename, r));
	}

	SHA_CTX sha;
	SHA1_Init (&sha);

	int const buffer_size = 65536;
	Kumu::ByteString read_buffer (buffer_size);

	Kumu::fsize_t done = 0;
	Kumu::fsize_t const size = reader.Size ();
	while (1) {
		ui32_t read = 0;
		Kumu::Result_t r = reader.Read (read_buffer.Data(), read_buffer.Capacity(), &read);

		if (r == Kumu::RESULT_ENDOFFILE) {
			break;
		} else if (ASDCP_FAILURE (r)) {
			boost::throw_exception (FileError ("could not read file to compute digest", filename, r));
		}

		SHA1_Update (&sha, read_buffer.Data(), read);

		if (progress) {
			progress (float (done) / size);
			done += read;
		}
	}

	byte_t byte_buffer[SHA_DIGEST_LENGTH];
	SHA1_Final (byte_buffer, &sha);

	char digest[64];
	return Kumu::base64encode (byte_buffer, SHA_DIGEST_LENGTH, digest, 64);
}

/** Convert a content kind to a string which can be used in a
 *  &lt;ContentKind&gt; node.
 *  @param kind ContentKind.
 *  @return string.
 */
string
dcp::content_kind_to_string (ContentKind kind)
{
	switch (kind) {
	case FEATURE:
		return "feature";
	case SHORT:
		return "short";
	case TRAILER:
		return "trailer";
	case TEST:
		return "test";
	case TRANSITIONAL:
		return "transitional";
	case RATING:
		return "rating";
	case TEASER:
		return "teaser";
	case POLICY:
		return "policy";
	case PUBLIC_SERVICE_ANNOUNCEMENT:
		return "psa";
	case ADVERTISEMENT:
		return "advertisement";
	}

	DCP_ASSERT (false);
}

/** Convert a string from a &lt;ContentKind&gt; node to a libdcp ContentKind.
 *  Reasonably tolerant about varying case.
 *  @param kind Content kind string.
 *  @return libdcp ContentKind.
 */
dcp::ContentKind
dcp::content_kind_from_string (string kind)
{
	transform (kind.begin(), kind.end(), kind.begin(), ::tolower);

	if (kind == "feature") {
		return FEATURE;
	} else if (kind == "short") {
		return SHORT;
	} else if (kind == "trailer") {
		return TRAILER;
	} else if (kind == "test") {
		return TEST;
	} else if (kind == "transitional") {
		return TRANSITIONAL;
	} else if (kind == "rating") {
		return RATING;
	} else if (kind == "teaser") {
		return TEASER;
	} else if (kind == "policy") {
		return POLICY;
	} else if (kind == "psa") {
		return PUBLIC_SERVICE_ANNOUNCEMENT;
	} else if (kind == "advertisement") {
		return ADVERTISEMENT;
	}

	DCP_ASSERT (false);
}

shared_ptr<dcp::OpenJPEGImage>
dcp::decompress_j2k (Data data, int reduce)
{
	return dcp::decompress_j2k (data.data.get(), data.size, reduce);
}

class ReadBuffer
{
public:
	ReadBuffer (uint8_t* data, int64_t size)
		: _data (data)
		, _size (size)
		, _offset (0)
	{}

	OPJ_SIZE_T read (void* buffer, OPJ_SIZE_T nb_bytes)
	{
		int64_t N = min (nb_bytes, _size - _offset);
		memcpy (buffer, _data + _offset, N);
		_offset += N;
		return N;
	}

private:
	uint8_t* _data;
	OPJ_SIZE_T _size;
	OPJ_SIZE_T _offset;
};

static OPJ_SIZE_T
read_function (void* buffer, OPJ_SIZE_T nb_bytes, void* data)
{
	return reinterpret_cast<ReadBuffer*>(data)->read (buffer, nb_bytes);
}

static void
read_free_function (void* data)
{
	delete reinterpret_cast<ReadBuffer*>(data);
}

/** Decompress a JPEG2000 image to a bitmap.
 *  @param data JPEG2000 data.
 *  @param size Size of data in bytes.
 *  @param reduce A power of 2 by which to reduce the size of the decoded image;
 *  e.g. 0 reduces by (2^0 == 1), ie keeping the same size.
 *       1 reduces by (2^1 == 2), ie halving the size of the image.
 *  This is useful for scaling 4K DCP images down to 2K.
 *  @return OpenJPEGImage.
 */
shared_ptr<dcp::OpenJPEGImage>
dcp::decompress_j2k (uint8_t* data, int64_t size, int reduce)
{
	uint8_t const jp2_magic[] = {
		0x00,
		0x00,
		0x00,
		0x0c,
		'j',
		'P',
		0x20,
		0x20
	};

	OPJ_CODEC_FORMAT format = OPJ_CODEC_J2K;
	if (size >= int (sizeof (jp2_magic)) && memcmp (data, jp2_magic, sizeof (jp2_magic)) == 0) {
		format = OPJ_CODEC_JP2;
	}

	opj_codec_t* decoder = opj_create_decompress (format);
	if (!decoder) {
		boost::throw_exception (DCPReadError ("could not create JPEG2000 decompresser"));
	}
	opj_dparameters_t parameters;
	opj_set_default_decoder_parameters (&parameters);
	parameters.cp_reduce = reduce;
	opj_setup_decoder (decoder, &parameters);

	opj_stream_t* stream = opj_stream_default_create (OPJ_TRUE);
	if (!stream) {
		throw MiscError ("could not create JPEG2000 stream");
	}

	opj_stream_set_read_function (stream, read_function);
	ReadBuffer* buffer = new ReadBuffer (data, size);
	opj_stream_set_user_data (stream, buffer, read_free_function);
	opj_stream_set_user_data_length (stream, size);

	opj_image_t* image = 0;
	opj_read_header (stream, decoder, &image);
	if (opj_decode (decoder, stream, image) == OPJ_FALSE) {
		opj_destroy_codec (decoder);
		opj_stream_destroy (stream);
		if (format == OPJ_CODEC_J2K) {
			boost::throw_exception (DCPReadError (String::compose ("could not decode JPEG2000 codestream of %1 bytes.", size)));
		} else {
			boost::throw_exception (DCPReadError (String::compose ("could not decode JP2 file of %1 bytes.", size)));
		}
	}

	opj_destroy_codec (decoder);
	opj_stream_destroy (stream);

	image->x1 = rint (float(image->x1) / pow (2, reduce));
	image->y1 = rint (float(image->y1) / pow (2, reduce));
	return shared_ptr<OpenJPEGImage> (new OpenJPEGImage (image));
}

class WriteBuffer
{
public:
/* XXX: is there a better strategy for this? */
#define MAX_J2K_SIZE (1024 * 1024 * 2)
	WriteBuffer ()
		: _data (shared_array<uint8_t> (new uint8_t[MAX_J2K_SIZE]), MAX_J2K_SIZE)
		, _offset (0)
	{}

	OPJ_SIZE_T write (void* buffer, OPJ_SIZE_T nb_bytes)
	{
		DCP_ASSERT ((_offset + nb_bytes) < MAX_J2K_SIZE);
		memcpy (_data.data.get() + _offset, buffer, nb_bytes);
		_offset += nb_bytes;
		return nb_bytes;
	}

	Data data () const {
		return _data;
	}

private:
	Data _data;
	OPJ_SIZE_T _offset;
};

static OPJ_SIZE_T
write_function (void* buffer, OPJ_SIZE_T nb_bytes, void* data)
{
	return reinterpret_cast<WriteBuffer*>(data)->write (buffer, nb_bytes);
}

static void
write_free_function (void* data)
{
	delete reinterpret_cast<WriteBuffer*>(data);
}

Data
dcp::compress_j2k (shared_ptr<const OpenJPEGImage> xyz, int bandwidth, int frames_per_second, bool threed, bool fourk)
{
	/* XXX: should probably use opj_set_*_handler */

	/* Set the max image and component sizes based on frame_rate */
	int max_cs_len = ((float) bandwidth) / 8 / frames_per_second;
	if (threed) {
		/* In 3D we have only half the normal bandwidth per eye */
		max_cs_len /= 2;
	}
	int const max_comp_size = max_cs_len / 1.25;

	/* get a J2K compressor handle */
	opj_codec_t* encoder = opj_create_compress (OPJ_CODEC_J2K);
	if (encoder == 0) {
		throw MiscError ("could not create JPEG2000 encoder");
	}

	/* Set encoding parameters to default values */
	opj_cparameters_t parameters;
	opj_set_default_encoder_parameters (&parameters);

	/* Set default cinema parameters */
	parameters.tile_size_on = OPJ_FALSE;
	parameters.cp_tdx = 1;
	parameters.cp_tdy = 1;

	/* Tile part */
	parameters.tp_flag = 'C';
	parameters.tp_on = 1;

	/* Tile and Image shall be at (0,0) */
	parameters.cp_tx0 = 0;
	parameters.cp_ty0 = 0;
	parameters.image_offset_x0 = 0;
	parameters.image_offset_y0 = 0;

	/* Codeblock size = 32x32 */
	parameters.cblockw_init = 32;
	parameters.cblockh_init = 32;
	parameters.csty |= 0x01;

	/* The progression order shall be CPRL */
	parameters.prog_order = OPJ_CPRL;

	/* No ROI */
	parameters.roi_compno = -1;

	parameters.subsampling_dx = 1;
	parameters.subsampling_dy = 1;

	/* 9-7 transform */
	parameters.irreversible = 1;

	parameters.tcp_rates[0] = 0;
	parameters.tcp_numlayers++;
	parameters.cp_disto_alloc = 1;
	parameters.cp_rsiz = fourk ? OPJ_CINEMA4K : OPJ_CINEMA2K;
	if (fourk) {
		parameters.numpocs = 2;
		parameters.POC[0].tile = 1;
		parameters.POC[0].resno0 = 0;
		parameters.POC[0].compno0 = 0;
		parameters.POC[0].layno1 = 1;
		parameters.POC[0].resno1 = parameters.numresolution - 1;
		parameters.POC[0].compno1 = 3;
		parameters.POC[0].prg1 = OPJ_CPRL;
		parameters.POC[1].tile = 1;
		parameters.POC[1].resno0 = parameters.numresolution - 1;
		parameters.POC[1].compno0 = 0;
		parameters.POC[1].layno1 = 1;
		parameters.POC[1].resno1 = parameters.numresolution;
		parameters.POC[1].compno1 = 3;
		parameters.POC[1].prg1 = OPJ_CPRL;
	}

	parameters.cp_comment = strdup ("libdcp");
	parameters.cp_cinema = fourk ? OPJ_CINEMA4K_24 : OPJ_CINEMA2K_24;

	/* 3 components, so use MCT */
	parameters.tcp_mct = 1;

	/* set max image */
	parameters.max_comp_size = max_comp_size;
	parameters.tcp_rates[0] = ((float) (3 * xyz->size().width * xyz->size().height * 12)) / (max_cs_len * 8);

	/* Setup the encoder parameters using the current image and user parameters */
	opj_setup_encoder (encoder, &parameters, xyz->opj_image());

	opj_stream_t* stream = opj_stream_default_create (OPJ_FALSE);
	if (!stream) {
		throw MiscError ("could not create JPEG2000 stream");
	}

	opj_stream_set_write_function (stream, write_function);
	WriteBuffer* buffer = new WriteBuffer ();
	opj_stream_set_user_data (stream, buffer, write_free_function);

	if (!opj_start_compress (encoder, xyz->opj_image(), stream)) {
		throw MiscError ("could not start JPEG2000 encoding");
	}

	if (!opj_encode (encoder, stream)) {
		opj_destroy_codec (encoder);
		opj_stream_destroy (stream);
		throw MiscError ("JPEG2000 encoding failed");
	}

	if (!opj_end_compress (encoder, stream)) {
		throw MiscError ("could not end JPEG2000 encoding");
	}

	Data enc (buffer->data ());

	free (parameters.cp_comment);
	opj_destroy_codec (encoder);
	opj_stream_destroy (stream);

	return enc;
}

/** @param s A string.
 *  @return true if the string contains only space, newline or tab characters, or is empty.
 */
bool
dcp::empty_or_white_space (string s)
{
	for (size_t i = 0; i < s.length(); ++i) {
		if (s[i] != ' ' && s[i] != '\n' && s[i] != '\t') {
			return false;
		}
	}

	return true;
}

/** Set up various bits that the library needs.  Should be called one
 *  by client applications.
 */
void
dcp::init ()
{
	if (xmlSecInit() < 0) {
		throw MiscError ("could not initialise xmlsec");
	}

#ifdef XMLSEC_CRYPTO_DYNAMIC_LOADING
	if (xmlSecCryptoDLLoadLibrary(BAD_CAST XMLSEC_CRYPTO) < 0) {
		throw MiscError ("unable to load default xmlsec-crypto library");
	}
#endif

	if (xmlSecCryptoAppInit(0) < 0) {
		throw MiscError ("could not initialise crypto");
	}

	if (xmlSecCryptoInit() < 0) {
		throw MiscError ("could not initialise xmlsec-crypto");
	}
}

bool dcp::operator== (dcp::Size const & a, dcp::Size const & b)
{
	return (a.width == b.width && a.height == b.height);
}

bool dcp::operator!= (dcp::Size const & a, dcp::Size const & b)
{
	return !(a == b);
}

ostream& dcp::operator<< (ostream& s, dcp::Size const & a)
{
	s << a.width << "x" << a.height;
	return s;
}

/** Decode a base64 string.  The base64 decode routine in KM_util.cpp
 *  gives different values to both this and the command-line base64
 *  for some inputs.  Not sure why.
 *
 *  @param in base64-encoded string.
 *  @param out Output buffer.
 *  @param out_length Length of output buffer.
 *  @return Number of characters written to the output buffer.
 */
int
dcp::base64_decode (string const & in, unsigned char* out, int out_length)
{
	BIO* b64 = BIO_new (BIO_f_base64 ());

	/* This means the input should have no newlines */
	BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

	/* Copy our input string, removing newlines */
	char in_buffer[in.size() + 1];
	char* p = in_buffer;
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] != '\n' && in[i] != '\r') {
			*p++ = in[i];
		}
	}

	BIO* bmem = BIO_new_mem_buf (in_buffer, p - in_buffer);
	bmem = BIO_push (b64, bmem);
	int const N = BIO_read (bmem, out, out_length);
	BIO_free_all (bmem);

	return N;
}

/** @param p Path to open.
 *  @param t mode flags, as for fopen(3).
 *  @return FILE pointer or 0 on error.
 *
 *  Apparently there is no way to create an ofstream using a UTF-8
 *  filename under Windows.  We are hence reduced to using fopen
 *  with this wrapper.
 */
FILE *
dcp::fopen_boost (boost::filesystem::path p, string t)
{
#ifdef LIBDCP_WINDOWS
        wstring w (t.begin(), t.end());
	/* c_str() here should give a UTF-16 string */
        return _wfopen (p.c_str(), w.c_str ());
#else
        return fopen (p.c_str(), t.c_str ());
#endif
}

optional<boost::filesystem::path>
dcp::relative_to_root (boost::filesystem::path root, boost::filesystem::path file)
{
	boost::filesystem::path::const_iterator i = root.begin ();
	boost::filesystem::path::const_iterator j = file.begin ();

	while (i != root.end() && j != file.end() && *i == *j) {
		++i;
		++j;
	}

	if (i != root.end ()) {
		return optional<boost::filesystem::path> ();
	}

	boost::filesystem::path rel;
	while (j != file.end ()) {
		rel /= *j++;
	}

	return rel;
}

bool
dcp::ids_equal (string a, string b)
{
	transform (a.begin(), a.end(), a.begin(), ::tolower);
	transform (b.begin(), b.end(), b.begin(), ::tolower);
	trim (a);
	trim (b);
	return a == b;
}

string
dcp::file_to_string (boost::filesystem::path p, uintmax_t max_length)
{
	uintmax_t len = boost::filesystem::file_size (p);
	if (len > max_length) {
		throw MiscError ("Unexpectedly long file");
	}

	FILE* f = fopen_boost (p, "r");
	if (!f) {
		throw FileError ("could not open file", p, errno);
	}

	char* c = new char[len];
	/* This may read less than `len' if we are on Windows and we have CRLF in the file */
	int const N = fread (c, 1, len, f);
	fclose (f);

	string s (c, N);
	delete[] c;

	return s;
}

/** @param key RSA private key in PEM format (optionally with -----BEGIN... / -----END...)
 *  @return SHA1 fingerprint of key
 */
string
dcp::private_key_fingerprint (string key)
{
	boost::replace_all (key, "-----BEGIN RSA PRIVATE KEY-----\n", "");
	boost::replace_all (key, "\n-----END RSA PRIVATE KEY-----\n", "");

	unsigned char buffer[4096];
	int const N = base64_decode (key, buffer, sizeof (buffer));

	SHA_CTX sha;
	SHA1_Init (&sha);
	SHA1_Update (&sha, buffer, N);
	uint8_t digest[20];
	SHA1_Final (digest, &sha);

	char digest_base64[64];
	return Kumu::base64encode (digest, 20, digest_base64, 64);
}

xmlpp::Node *
dcp::find_child (xmlpp::Node const * node, string name)
{
	xmlpp::Node::NodeList c = node->get_children ();
	xmlpp::Node::NodeList::iterator i = c.begin();
	while (i != c.end() && (*i)->get_name() != name) {
		++i;
	}

	DCP_ASSERT (i != c.end ());
	return *i;
}

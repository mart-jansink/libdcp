/*
    Copyright (C) 2012-2017 Carl Hetherington <cth@carlh.net>

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

#include "sound_frame.h"
#include <asdcp/AS_DCP.h>
#include <iostream>

using std::cout;
using namespace dcp;

SoundFrame::SoundFrame (ASDCP::PCM::MXFReader* reader, int n, boost::shared_ptr<const DecryptionContext> c)
	: Frame (reader, n, c)
{
	ASDCP::PCM::AudioDescriptor desc;
	reader->FillAudioDescriptor (desc);
	_channels = desc.ChannelCount;
}

int32_t
SoundFrame::get (int channel, int frame) const
{
	uint8_t const * d = data() + (frame * _channels * 3) + (channel * 3);
	return d[0] | (d[1] << 8) | (d[2] << 16);
}

int
SoundFrame::samples () const
{
	return size() / (_channels * 3);
}

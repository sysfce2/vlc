/*
 * Streams.hpp
 *****************************************************************************
 * Copyright (C) 2014 - VideoLAN authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifndef STREAM_HPP
#define STREAM_HPP

#include <string>
#include "StreamsType.hpp"

namespace dash
{
    namespace Streams
    {
        class AbstractStreamOutput;

        class Stream
        {
            public:
                Stream(const std::string &mime);
                Stream(const Type);
                bool operator==(const Stream &) const;
                static Type mimeToType(const std::string &mime);

            private:
                Type type;
        };

    }
}
#endif // STREAMS_HPP

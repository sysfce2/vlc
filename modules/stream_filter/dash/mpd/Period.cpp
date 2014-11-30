/*
 * Period.cpp
 *****************************************************************************
 * Copyright (C) 2010 - 2011 Klagenfurt University
 *
 * Created on: Aug 10, 2010
 * Authors: Christopher Mueller <christopher.mueller@itec.uni-klu.ac.at>
 *          Christian Timmerer  <christian.timmerer@itec.uni-klu.ac.at>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "Period.h"

#include <vlc_common.h>
#include <vlc_arrays.h>

using namespace dash::mpd;

Period::Period()
{
}

Period::~Period ()
{
    vlc_delete_all( this->adaptationSets );
}

const std::vector<AdaptationSet*>&  Period::getAdaptationSets() const
{
    return this->adaptationSets;
}

const std::vector<AdaptationSet*>   Period::getAdaptationSets(Streams::Type type) const
{
    std::vector<AdaptationSet*> list;
    std::vector<AdaptationSet*>::const_iterator it;
    for(it = adaptationSets.begin(); it!= adaptationSets.end(); it++)
    {
        if( Streams::Stream::mimeToType((*it)->getMimeType()) == type )
            list.push_back(*it);
    }
    return list;
}

void                                Period::addAdaptationSet(AdaptationSet *adaptationSet)
{
    if ( adaptationSet != NULL )
        this->adaptationSets.push_back(adaptationSet);
}

AdaptationSet * Period::getAdaptationSet(Streams::Type type) const
{
    std::vector<AdaptationSet *>::const_iterator it;
    for(it = adaptationSets.begin(); it != adaptationSets.end(); it++)
    {
        if ( Streams::Stream::mimeToType((*it)->getMimeType()) == type )
            return *it;
    }
    return NULL;
}

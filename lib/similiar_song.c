/***********************************************************
* This file is part of glyr
* + a commnadline tool and library to download various sort of musicrelated metadata.
* + Copyright (C) [2011]  [Christopher Pahl]
* + Hosted at: https://github.com/sahib/glyr
*
* glyr is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* glyr is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with glyr. If not, see <http://www.gnu.org/licenses/>.
**************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "core.h"
#include "types.h"
#include "stringlib.h"

#include "similiar_song/lastfm.h"

// Add your's here
GlyPlugin similiar_song_providers[] =
{
//  full name       key  coloredname          use?   parser callback           geturl callback         free url?
    {"lastfm",      "l", "last"C_R"."C_"fm",  false,  {similiar_song_lastfm_parse,  similiar_song_lastfm_url,  NULL, false}, GRP_SAFE | GRP_FAST},
    {NULL,          NULL, NULL,               false,  {NULL,                        NULL,                      NULL, false}, GRP_NONE | GRP_NONE}
};

GlyPlugin * glyr_get_similiar_song_providers(void)
{
    return copy_table(similiar_song_providers,sizeof(similiar_song_providers));
}

static GlyCacheList * similiar_song_finalize(GlyCacheList * result, GlyQuery * settings)
{
    return generic_finalizer(result,settings,TYPE_SIMILIAR_SONG);
}

bool vdt_similar_song(GlyQuery * settings)
{
    if(settings && settings->artist && settings->title)
    {
		return true;
    }
    return false;
}

/* PlugStruct */
MetaDataFetcher glyrFetcher_similar_song = {
	.name = "similar",
	.type = GET_SIMILIAR_SONGS,
	.validate = vdt_similar_song,
	/* CTor | DTor */
	.init    = NULL,
	.destroy = NULL,
	.finalize = similiar_song_finalize
};
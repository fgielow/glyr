/***********************************************************
 * This file is part of glyr
 * + a commnandline tool and library to download various sort of musicrelated metadata.
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

/* Test functions to help making test cases. You should not use these */

#include "core.h"
#include "register_plugins.h"

/*-----------------------------------*/

static MetaDataSource * get_metadata_struct(const char * provider_name, GLYR_GET_TYPE type)
{
    MetaDataSource * result = NULL;
    if(provider_name && type !=  GLYR_GET_UNSURE)
    {
        gsize name_len = strlen(provider_name);
        GList * source_list = r_getSList();
        for(GList * elem = source_list; elem; elem = elem->next)
        {
            MetaDataSource * src = elem->data;
            if(src != NULL)
            {
                if(g_ascii_strncasecmp(provider_name,src->name,name_len) == 0 && src->type == type)
                {
                    result = src;
                    break;
                }
            }
        }
    }
    return result;
}

/*-----------------------------------*/

const char * glyr_testing_call_url(const char * provider_name, GLYR_GET_TYPE type, GlyrQuery * query)
{
    const char * result = NULL;
    if(query != NULL)
    {
        MetaDataSource * src = get_metadata_struct(provider_name,type);
        if(src != NULL)
        {
            const char * url = src->get_url(query);
            result = (src->free_url ? url : g_strdup(url));
        }
    }
    return result;
}

/*-----------------------------------*/

GlyrMemCache * glyr_testing_call_parser(const char * provider_name, GLYR_GET_TYPE type, GlyrQuery * query, GlyrMemCache * cache)
{
    GlyrMemCache * result = NULL;
    if(query && cache)
    {
        MetaDataSource * src = get_metadata_struct(provider_name,type);
        if(src != NULL)
        {
            cb_object fake;
            fake.cache = cache;
            fake.s     = query;
            GList * result_list = src->parser(&fake);

            if(result_list != NULL)
            {
                result = result_list->data;
            }

            for(GList * elem = result_list; elem; elem = elem->next)
            {
                GlyrMemCache * item = elem->data;
                if(item != NULL)
                {
                    item->prev = (elem->prev) ? (elem->prev->data) : NULL;
                    item->next = (elem->next) ? (elem->next->data) : NULL;
                } 
            }
        }
    }
    return result; 
}

/*-----------------------------------*/
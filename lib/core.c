#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

// Handle non-standardized systems:
#ifndef WIN32
#include <unistd.h>
#else
#include <time.h>
#endif

// All curl stuff we need:
#include <curl/multi.h>

// Own headers:
#include "stringop.h"
#include "core.h"

// Get user agent
#include "config.h"

/*--------------------------------------------------------*/

#define remove_color( string, color )             \
	if(string && color) {                     \
	char * x = strreplace(string,color,NULL); \
	free(string); string = x;}                \
 
int glyr_message(int v, GlyQuery * s, FILE * stream, const char * fmt, ...)
{
    int r = -1;
    if(s || v == -1)
    {
        if(v == -1 || v <= s->verbosity)
        {
            va_list param;
            char * tmp = NULL;
            if(stream && fmt)
            {
                va_start(param,fmt);
                r = x_vasprintf (&tmp, fmt, param);
                va_end(param);

                if(r != -1 && tmp)
                {

// Remove any color if desired
#ifdef USE_COLOR
                    if(s && !s->color_output)
                    {
                        remove_color(tmp,C_B);
                        remove_color(tmp,C_M);
                        remove_color(tmp,C_C);
                        remove_color(tmp,C_R);
                        remove_color(tmp,C_G);
                        remove_color(tmp,C_Y);
                        remove_color(tmp,C_ );
                    }
#endif
                    r = fprintf(stream,"%s%s",(v>2)?C_B"DEBUG: "C_:"",tmp);
                    free(tmp);
                    tmp = NULL;
                }
            }
        }
    }
    return r;
}

#undef remove_color

/*--------------------------------------------------------*/

// Sleep usec microseconds
static int DL_usleep(long usec)
{
#ifndef WIN32
    struct timespec req= { 0 };
    time_t sec = (int) (usec / 1e6L);

    usec = usec - (sec*1e6L);
    req.tv_sec  = (sec);
    req.tv_nsec = (usec*1e3L);

    while(nanosleep(&req , &req) == -1)
    {
        continue;
    }
#else // Windooze
    Sleep(usec / 1e3L);
#endif
    return 0;
}

/*--------------------------------------------------------*/

GlyMemCache * DL_copy(GlyMemCache * src)
{
    GlyMemCache * dest = NULL;
    if(src)
    {
        dest = DL_init();
        if(dest)
        {
            if(src->data)
            {
                dest->data = calloc(src->size+1,sizeof(char));
                memcpy(dest->data,src->data,src->size);
            }
            if(src->dsrc)
            {
                dest->dsrc = strdup(src->dsrc);
            }
            dest->size = src->size;
        }
    }
    return dest;
}

/*--------------------------------------------------------*/

// cache incoming data in a MemChunk_t
static size_t DL_buffer(void *puffer, size_t size, size_t nmemb, void *cache)
{
    size_t realsize = size * nmemb;
    struct GlyMemCache *mem = (struct GlyMemCache *)cache;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if (mem->data)
    {
        memcpy(&(mem->data[mem->size]), puffer, realsize);
        mem->size += realsize;
        mem->data[mem->size] = 0;
    }
    else
    {
        glyr_message(-1,NULL,stderr,"Caching failed: Out of memory.\n");
    }
    return realsize;
}

/*--------------------------------------------------------*/

// cleanup internal buffer if no longer used
void DL_free(GlyMemCache *cache)
{
    if(cache)
    {
        if(cache->size && cache->data)
        {
            free(cache->data);
            cache->data   = NULL;
        }
        if(cache->dsrc)
        {
            free(cache->dsrc);
            cache->dsrc   = NULL;
        }

        cache->size   = 0;

        free(cache);
        cache = NULL;
    }
}

/*--------------------------------------------------------*/

// Use this to init the internal buffer
GlyMemCache* DL_init(void)
{
    GlyMemCache *cache = malloc(sizeof(GlyMemCache));

    if(cache)
    {
        cache->size   = 0;
        cache->data   = NULL;
        cache->dsrc   = NULL;
	cache->error  = 0;
    }
    else
    {
        glyr_message(-1,NULL,stderr,"Warning: empty dlcache...\n");
    }
    return cache;
}

/*--------------------------------------------------------*/

GlyCacheList * DL_new_lst(void)
{
    GlyCacheList * c = calloc(1,sizeof(GlyCacheList));
    if(c != NULL)
    {
        c->size = 0;
        c->list = calloc(1,sizeof(GlyMemCache*));
        if(!c->list)
        {
            glyr_message(-1,NULL,stderr,"calloc() returned NULL!");
        }
    }
    else
    {
        glyr_message(-1,NULL,stderr,"c is still empty?");
    }

    return c;
}

/*--------------------------------------------------------*/

void DL_add_to_list(GlyCacheList * l, GlyMemCache * c)
{
    if(l && c)
    {
        l->list = realloc(l->list, sizeof(GlyMemCache *) * (l->size+2));
        l->list[  l->size] = c;
        l->list[++l->size] = NULL;
    }
}

/*--------------------------------------------------------*/

void DL_push_sublist(GlyCacheList * l, GlyCacheList * s)
{
    if(l && s)
    {
        size_t i;
        for(i = 0; i < s->size; i++)
        {
            DL_add_to_list(l,s->list[i]);
        }
    }
}

/*--------------------------------------------------------*/

// Free list itself and all caches stored in there
void DL_free_lst(GlyCacheList * c)
{
    if(c)
    {
        size_t i;
        for(i = 0; i < c->size ; i++)
            DL_free(c->list[i]);

        DL_free_container(c);
    }
}

/*--------------------------------------------------------*/

// only free list, caches are left intact
void DL_free_container(GlyCacheList * c)
{
    if(c!=NULL)
    {
        if(c->list)
            free(c->list);

        memset(c,0,sizeof(GlyCacheList));
        free(c);
    }
}

/*--------------------------------------------------------*/

// Init an easyhandler with all relevant options
static void DL_setopt(CURL *eh, GlyMemCache * cache, const char * url, GlyQuery * s, void * magic_private_ptr, long timeout)
{
    if(!s) return;

    // Set options (see 'man curl_easy_setopt')
    curl_easy_setopt(eh, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1L);

    // last.fm and discogs require an useragent (wokrs without too)
    curl_easy_setopt(eh, CURLOPT_USERAGENT, glyr_VERSION_NAME );
    curl_easy_setopt(eh, CURLOPT_HEADER, 0L);

    // Pass vars to curl
    curl_easy_setopt(eh, CURLOPT_URL, url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, magic_private_ptr);
    curl_easy_setopt(eh, CURLOPT_VERBOSE, (s->verbosity == 4));
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, DL_buffer);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, (void *)cache);

    // amazon plugin requires redirects
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(eh, CURLOPT_MAXREDIRS, s->redirects);

    // Discogs equires gzip compression
    curl_easy_setopt(eh, CURLOPT_ENCODING,"gzip");

    // Don't save cookies - I had some quite embarassing moments
    // when amazon's startpage showed me "Hooray for Boobies",
    // because I searched for the Bloodhoundgang album... :(
    // (because I have it already! :-) )
    curl_easy_setopt(eh, CURLOPT_COOKIEJAR ,"");
}

/*--------------------------------------------------------*/

// Init a callback object and a curl_easy_handle
static GlyMemCache * handle_init(CURLM * cm, cb_object * capo, GlyQuery *s, long timeout)
{
    // You did sth. wrong..
    if(!capo || !capo->url)
        return NULL;

    // Init handle
    CURL *eh = curl_easy_init();

    // Init cache
    GlyMemCache *dlcache = DL_init();

    // Set handle
    capo->handle = eh;

    // Configure curl
    DL_setopt(eh, dlcache, capo->url, s,(void*)capo,timeout);

    // Add handle to multihandle
    curl_multi_add_handle(cm, eh);
    return dlcache;
}

/*--------------------------------------------------------*/

// return a memcache only with error field set (for error report)
GlyMemCache * DL_error(int eid)
{
	GlyMemCache * ec = DL_init();
	if(ec != NULL)
	{
		ec->error = eid;
	}
	return ec;
}

/*--------------------------------------------------------*/

bool continue_search(int iter, GlyQuery * s)
{
	if(s != NULL)
	{
		if((iter + s->itemctr) < s->number && iter < s->plugmax)
		{
			glyr_message(4,s,stderr,"\ncontinue! iter=%d && s->number %d && plugmax=%d && items=%d\n",iter,s->number,s->plugmax,s->itemctr);
			return true;
		}
	}
	glyr_message(4,s,stderr,"\nSTOP! iter=%d && s->number %d && plugmax=%d && items=%d\n",iter,s->number,s->plugmax,s->itemctr);
	return false;
}

/*--------------------------------------------------------*/

int flag_double_urls(GlyCacheList * result, GlyQuery * s)
{
	size_t i = 0, dp = 0;
	for(i = 0; i < result->size; i++)
	{
		size_t j = 0;
		if(result->list[i]->error == 0)
		{
			for(j = 0; j < result->size; j++)
			{
				if(i != j && result->list[i]->error == 0)
				{
					if(!strcmp(result->list[i]->data,result->list[j]->data))
					{
						result->list[j]->error = 1;
						dp++;
					}
				}
			}
		}
	}

	if(dp != 0)
	{
		glyr_message(2,s,stderr,C_R"*"C_" Ignoring %d URL%sthat occure twice.\n\n",dp,dp>=1 ? " " : "s ");
		s->itemctr -= dp;
	}
	return dp;
}

/*--------------------------------------------------------*/

int flag_blacklisted_urls(GlyCacheList * result, const char ** URLblacklist, GlyQuery * s)
{
	size_t i = 0, ctr = 0;
	for(i = 0; i < result->size; i++)
	{
		if(result->list[i]->error == 0)
		{
			size_t j = 0;
			for(j = 0; URLblacklist[j]; j++)
			{
				if(result->list[j]->error == 0)
				{
					if(!strcmp(result->list[i]->data,URLblacklist[j]))
					{
						result->list[i]->error = BLACKLISTED; 
						ctr++;
					}
				}
			}
		}
	}
	if(ctr != 0)
	{
		glyr_message(2,s,stderr,C_R"*"C_" Ignoring %d blacklisted URL%s.\n\n",ctr,ctr>=1 ? " " : "s ");
		s->itemctr -= ctr;
	}
}

/*--------------------------------------------------------*/

// Download a singe file NOT in parallel
GlyMemCache * download_single(const char* url, GlyQuery * s)
{
    if(url==NULL)
    {
        return NULL;
    }

    //fprintf(stderr,"Downloading <%s>\n",url);

    CURL *curl = NULL;
    CURLcode res = 0;

    if(curl_global_init(CURL_GLOBAL_ALL))
    {
        glyr_message(-1,NULL,stderr,"?? libcurl failed to init.\n");
    }

    // Init handles
    curl = curl_easy_init();
    GlyMemCache * dldata = DL_init();

    if(curl != NULL)
    {
        // Configure curl
        DL_setopt(curl,dldata,url,s,NULL,s->timeout);

        // Perform transaction
        res = curl_easy_perform(curl);

        // Better check again
        if(res != CURLE_OK)
        {
            glyr_message(-1,NULL,stderr,C_"\n:: %s\n", curl_easy_strerror(res));
            DL_free(dldata);
            dldata = NULL;
        }

        // Handle without any use - clean up
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return dldata;
    }

    // Free mem
    curl_global_cleanup();
    DL_free(dldata);
    return NULL;
}

/*--------------------------------------------------------*/
#define ALIGN(m)                      \
if(m > 0) {                           \
     int i = 0;                       \
     int d = ABS(20-m);               \
     glyr_message(2,s,stderr,C_);     \
     for(;i<d;i++) {                  \
        glyr_message(2,s,stderr,"."); \
     }  }                             \

GlyCacheList * invoke(cb_object *oblist, long CNT, long parallel, long timeout, GlyQuery * s)
{
    // curl multi handles
    CURLM *cm;
    CURLMsg *msg;

    // select()
    long L;
    unsigned int C = 0;
    int M, Q, U = -1;
    fd_set R, W, E;
    struct timeval T;

    // true when exiting early
    bool do_exit = false;
    GlyCacheList * result_lst = NULL;

    // Wake curl up..
    if(curl_global_init(CURL_GLOBAL_ALL) != 0)
    {
        glyr_message(-1,NULL,stderr,"Initializing libcurl failed!");
        return NULL;
    }

    // Init the multi handler (=~ container for easy handlers)
    cm = curl_multi_init();

    // we can optionally limit the total amount of connections this multi handle uses
    curl_multi_setopt(cm, CURLMOPT_MAXCONNECTS, (long)parallel);
    curl_multi_setopt(cm, CURLMOPT_PIPELINING,  1L);

    // Set up all fields of cb_object (except callback and url)
    for (C = 0; C < CNT; C++)
    {
        char * track = oblist[C].url;

        // format url
        oblist[C].url = prepare_url(oblist[C].url,oblist[C].s->artist,oblist[C].s->album,oblist[C].s->title);

        // init cache and the CURL handle assicoiated with it
        oblist[C].cache  = handle_init(cm,&oblist[C],s,timeout);

        // old url will be freed as it's always strdup'd
        if(track)
        {
            free(track);
        }

        // > huh?-case
        if(oblist[C].cache == NULL)
        {
            glyr_message(-1,NULL,stderr,"[Internal Error:] Empty callback or empty url!\n");
            glyr_message(-1,NULL,stderr,"[Internal Error:] Call your local C-h4x0r!\n");
            return NULL;
        }
    }

    // counter of plugins tried
    int n_sources = 0;

    while (U != 0 &&  do_exit == false)
    {
        // Store number of handles still being transferred in U
        CURLMcode merr;
        if( (merr = curl_multi_perform(cm, &U)) != CURLM_OK)
        {
            glyr_message(-1,NULL,stderr,"E: (%s) in dlqueue\n",curl_multi_strerror(merr));
            return NULL;
        }
        if (U != 0)
        {
            FD_ZERO(&R);
            FD_ZERO(&W);
            FD_ZERO(&E);

            if (curl_multi_fdset(cm, &R, &W, &E, &M))
            {
                glyr_message(-1,NULL,stderr, "E: curl_multi_fdset\n");
                return NULL;
            }
            if (curl_multi_timeout(cm, &L))
            {
                glyr_message(-1,NULL,stderr, "E: curl_multi_timeout\n");
                return NULL;
            }
            if (L == -1)
            {
                L = 100;
            }
            if (M == -1)
            {
                DL_usleep(L * 100);
            }
            else
            {
                T.tv_sec = L/1000;
                T.tv_usec = (L%1000)*1000;

                if (0 > select(M+1, &R, &W, &E, &T))
                {
                    glyr_message(-1,NULL,stderr, "E: select(%i,,,,%li): %i: %s\n",M+1, L, errno, strerror(errno));
                    return NULL;
                }
            }
        }

        while (do_exit == false && (msg = curl_multi_info_read(cm, &Q)))
        {
            // We're done
            if (msg->msg == CURLMSG_DONE)
            {
                // Get the callback object associated with the curl handle
                // for some odd reason curl requires a char * pointer
                const char * p_pass = NULL;
                CURL *e = msg->easy_handle;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &p_pass);

                // Cast to actual object
                cb_object * capo = (cb_object *) p_pass;

		int align_msg = 0;
                // The stage is yours <name>
                if(capo->name) 
		{
			align_msg = strlen(capo->name);
			glyr_message(2,s,stderr,"Query: %s",capo->color);
		}

                if(capo && capo->cache && capo->cache->data && msg->data.result == CURLE_OK)
                {
                    // Here is where the actual callback shall be executed
                    if(capo->parser_callback)
                    {
                        // Now try to parse what we downloaded
                        GlyCacheList * cl = capo->parser_callback(capo);
                        if(cl && cl->list[0]->data)
                        {
                            if(capo->name)
                            {
				ALIGN(align_msg);
                                glyr_message(2,s,stderr,C_G"found!"C_" ["C_R"%d item%c"C_"]\n",cl->size,cl->size == 1 ? ' ' : 's');
				s->itemctr += cl->size;
                            }
                            else
                            {
                                char c = 0;
                                switch(n_sources % 4)
                                {
                                case 0:
                                    c ='\\';
                                    break;
                                case 1:
                                    c ='-';
                                    break;
                                case 2:
                                    c ='/';
                                    break;
                                case 3:
                                    c ='|';
                                    break;
                                }
                                glyr_message(2,s,stderr,C_"Downloading [%c] [#%d]\r",c,n_sources+1);
                            }

                            if(!result_lst)
                            {
                                result_lst = DL_new_lst();
                            }

                            // push all pointers from sublist to resultlist
                            DL_push_sublist(result_lst,cl);

                            // free the old container
                            DL_free_container(cl);

                            glyr_message(3,s,stderr,"Tried sources: %d (of max. %d) | Buffers in line: %d\n",n_sources,s->number,result_lst->size);

                            // Are we finally done?
                            if(n_sources >= CNT || (capo->name ? s->number <= s->itemctr : s->number <= result_lst->size))
                            {
                                do_exit = true;
                            }
                        }
                        else if(capo->name)
                        {
			    ALIGN(align_msg);
			
			    int err = (!cl) ? -1 : cl->list[0]->error;
			    switch(err)
			    {
				    case NO_BEGIN_TAG:
					 glyr_message(2,s,stderr,C_R"No begin tag found.\n"C_);
					 break;
				    case NO_ENDIN_TAG:
					 glyr_message(2,s,stderr,C_R"No begin tag found.\n"C_);
					 break;
				    default:
					 glyr_message(2,s,stderr,C_R"failed.\n"C_);
			    }

			    if(cl != NULL) DL_free_lst(cl);
                        }
                    }
                    else
                    {
			ALIGN(align_msg);
                        glyr_message(1,s,stderr,"WARN: Unable to exec callback (=NULL)\n");
                    }

                    // delete cache
                    if(capo->cache)
                    {
                        DL_free(capo->cache);
                        capo->cache = NULL;
                    }

                }
                else if(msg->data.result != CURLE_OK)
                {
	    	    ALIGN(align_msg);
                    const char * curl_err = curl_easy_strerror(msg->data.result);
                    glyr_message(1,s,stderr,"%s - [%d]\n",curl_err ? curl_err : "Unknown Error",msg->data.result);
                }
                else if(capo->cache->data == NULL && capo->name)
                {
		    ALIGN(align_msg);
		    switch(capo->cache->error)
		    {

                    	default:  glyr_message(2,s,stderr,C_R"page not reachable.\n"C_);
		    }
                }

                curl_multi_remove_handle(cm,e);
                if(capo->url)
                {
                    free(capo->url);
                    capo->url = NULL;
                }

                capo->handle = NULL;
                curl_easy_cleanup(e);
            }
            else
            {
                glyr_message(1,s,stderr, "E: CURLMsg (%d)\n", msg->msg);
            }

            if (C <  CNT)
            {
                // Get a new handle and new cache
                oblist[C].cache = handle_init(cm,&oblist[C],s,timeout);

                C++;
                U++;
            }

            // amount of tried plugins
            n_sources++;
        }
    }

    // erase "downloading [.] message"
    if(!oblist[0].name)
        glyr_message(2,s,stderr,"%-25c\n",1);

    // Job done - clean up what we did to the memory..
    size_t I = 0;
    for(; I < C; I++)
    {
        DL_free(oblist[I].cache);

        // Free handle
        if(oblist[I].handle != NULL)
            curl_easy_cleanup(oblist[I].handle);

        // Free the prepared URL
        if(oblist[I].url != NULL)
            free(oblist[I].url);

        // erase everything
        memset(&oblist[I],0,sizeof(cb_object));
    }

    curl_multi_cleanup(cm);
    curl_global_cleanup();
    return result_lst;
}

/*--------------------------------------------------------*/

void plugin_init(cb_object *ref, const char *url, GlyCacheList * (callback)(cb_object*), GlyQuery * s, const char *name, const char * color, void * custom)
{
    // always dup the url
    ref->url = url ? strdup(url) : NULL;

    // Set callback
    ref->parser_callback = callback;

    // empty cache
    ref->cache  = NULL;

    // custom pointer passed to finalize()
    ref->custom = custom;

    // name of plugin (for display)
    ref->name  = name;
    ref->color = color;

    // ptr to settings
    ref->s = s;
}

/*--------------------------------------------------------*/

GlyCacheList * register_and_execute(GlyQuery * settings, GlyCacheList * (*finalizer)(GlyCacheList *, GlyQuery *))
{
    size_t iterator = 0;
    size_t i_plugin = 0;

    GlyPlugin * providers = settings->providers;
    GlyCacheList * result_lst = NULL;

    // Assume a max. of 10 plugins, just set it higher if you need to.
    cb_object *plugin_list = calloc(sizeof(cb_object), WHATEVER_MAX_PLUGIN);

    while(providers && providers[iterator].name)
    {
        // end of group -> execute
        if(providers[iterator].key == NULL)
        {
	    if(i_plugin)
	    {
            	glyr_message(2,settings,stderr,"Now switching to group "C_Y"%s"C_":\n",providers[iterator].name);
	    }

            // Get the raw result of one jobgroup
            GlyCacheList * invoked_list = invoke(plugin_list, i_plugin, settings->parallel, settings->timeout, settings);
            if(invoked_list)
            {
                // "finalize" this list
                GlyCacheList * sub_list = finalizer(invoked_list,settings);
                if(sub_list)
                {
                    if(!result_lst) result_lst = DL_new_lst();
                    // Store pointers to final caches
                    DL_push_sublist(result_lst,sub_list);

                    // Free all old container
                    DL_free_container(sub_list);
                }

                DL_free_lst(invoked_list);

                glyr_message(3,settings,stderr,"Result_list_iterator: %d (up to n=%d)\n",(int)result_lst->size, settings->number);

                if(result_lst != NULL && settings->number <= result_lst->size+1)
                {
                    // We are done.
                    glyr_message(3,settings,stderr,"register_and_execute() Breaking out.\n");
                    break;
                }
                else
                {
                    // Not enough data -> search again
                    glyr_message(3,settings,stderr,"register_and_execute() (Re)-initializing list.\n");
                    goto RE_INIT_LIST;
                }
            }
            else
            {
RE_INIT_LIST:
                {
                    // Get ready for next group
                    memset(plugin_list,0,WHATEVER_MAX_PLUGIN * sizeof(cb_object));
                    i_plugin = 0;
                }
            }
        }

        if(providers[iterator].use && providers[iterator].plug.url_callback)
        {
            // Call the Url getter of the plugin
            char * url = (char*)providers[iterator].plug.url_callback(settings);
            glyr_message(3,settings,stderr,"URL callback return <%s>\n",url);
            if(url)
            {
                glyr_message(3,settings,stderr,"Registering plugin '%s'\n",providers[iterator].name);
                plugin_init( &plugin_list[i_plugin++], url, providers[iterator].plug.parser_callback,settings,providers[iterator].name,providers[iterator].color,NULL);
                if(providers[iterator].plug.free_url)
                {
                    free(url);
                }
            }
        }
        iterator++;
    }

    // Thanks for serving dear list
    if (plugin_list)
    {
        free(plugin_list);
        plugin_list = NULL;
    }

    return result_lst;
}

/*--------------------------------------------------------*/

GlyPlugin * copy_table(const GlyPlugin * o, size_t size)
{
    GlyPlugin * d = malloc(size);
    memcpy(d,o,size);
    return d;
}

/*--------------------------------------------------------*/
#ifndef AMAZON_H
#define AMAZON_H

#include "../core.h"

const char * cover_amazon_url(GlyQuery * sets);
GlyCacheList * cover_amazon_parse(cb_object *capo);

#endif
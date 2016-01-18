#ifndef MIME_H
#define MIME_H
const char *mime_get_name(const char *mimetype);

const char *mime_get_mimetype(const char *name,char *need_charset);

const char *mime_get_path(const char *name,char *need_charset);

#endif
                                                                 

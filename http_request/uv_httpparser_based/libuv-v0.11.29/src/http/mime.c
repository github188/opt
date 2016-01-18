#include "mime.h"
#include <stdio.h>
#include <string.h>


#define NEED_CHARSET      	1
#define NOT_NEED_CHARSET	0
typedef struct{
	const char *mime;
	const char *name;
	const char need_charset;
}mime_t;

static mime_t default_mime={"text/plain",".txt",NEED_CHARSET};


static const mime_t mime[]= {
	{"text/html",						".html",		NEED_CHARSET},
	{"text/html",						".htm",		NEED_CHARSET},
	{"text/html",						".shtml",	NEED_CHARSET},
	{"text/css",						".css",		NEED_CHARSET},
	{"text/xml",						".xml",		NEED_CHARSET},
	{"image/gif",						".gif",		NOT_NEED_CHARSET},
	{"image/jpeg",						".jpeg",		NOT_NEED_CHARSET},
	{"image/jpeg",						".jpg",		NOT_NEED_CHARSET},
	{"application/javascript",			".js",		NEED_CHARSET},
	{"application/atom+xml",			".atom",		NEED_CHARSET},
	{"application/rss+xml",				".rss",		NEED_CHARSET},

	{"text/mathml",						".mml",		NEED_CHARSET},
	{"text/plain",						".txt",		NEED_CHARSET},
	{"text/vnd.sun.j2me.app-descriptor",".jad",		NOT_NEED_CHARSET},
	{"text/vnd.wap.wml",				".wml",		NOT_NEED_CHARSET},
	{"text/x-component",				".htc",		NOT_NEED_CHARSET},
	{"image/png",						".png",		NOT_NEED_CHARSET},
	{"image/tiff",						".tif",		NOT_NEED_CHARSET},
	{"image/tiff",						".tiff",		NOT_NEED_CHARSET},
	{"image/vnd.wap.wbmp",				".wbmp",		NOT_NEED_CHARSET},
	{"image/x-icon",					".ico",		NOT_NEED_CHARSET},
	{"image/x-jng",						".jng",		NOT_NEED_CHARSET},
	{"image/x-ms-bmp",					".bmp",		NOT_NEED_CHARSET},
	{"image/svg+xml",					".svgz",		NOT_NEED_CHARSET},
	{"image/svg+xml",					".svg",		NOT_NEED_CHARSET},
	{"image/webp",						".webp",		NOT_NEED_CHARSET},

	{"application/font-woff",			".woff",		NOT_NEED_CHARSET},
	{"application/java-archive",		".jar",		NOT_NEED_CHARSET},
	{"application/java-archive",		".war",		NOT_NEED_CHARSET},
	{"application/java-archive",		".ear",		NOT_NEED_CHARSET},
	{"application/json",				".json",		NEED_CHARSET},
	{"application/mac-binhex40",		".hqx",		NOT_NEED_CHARSET},
	{"application/msword",				".doc",		NOT_NEED_CHARSET},
	{"application/pdf",					".pdf",		NOT_NEED_CHARSET},
	{"application/postscript",			".ps",		NOT_NEED_CHARSET},
	{"application/postscript",			".eps",		NOT_NEED_CHARSET},
	{"application/postscript",			".ai",		NOT_NEED_CHARSET},
	{"application/rtf",					".rtf",		NOT_NEED_CHARSET},
	{"application/vnd.apple.mpegurl",	".m3u8",		NEED_CHARSET},
	{"application/vnd.ms-excel",		".xls",		NOT_NEED_CHARSET},
	{"application/vnd.ms-fontobject",	".eot",		NOT_NEED_CHARSET},
	{"application/vnd.ms-powerpoint",	".ppt",		NOT_NEED_CHARSET},
	{"application/vnd.wap.wmlc",		".wmlc",		NOT_NEED_CHARSET},
	{"application/vnd.google-earth.kml+xml",".kml",	NOT_NEED_CHARSET},
	{"application/vnd.google-earth.kmz",".kmz",		NOT_NEED_CHARSET},
	{"application/x-7z-compressed",		".7z",		NOT_NEED_CHARSET},
	{"application/x-cocoa",				".cco",		NOT_NEED_CHARSET},
	{"application/x-java-archive-diff",	".jardiff",	NOT_NEED_CHARSET},
	{"application/x-java-jnlp-file",	".jnlp",		NOT_NEED_CHARSET},
	{"application/x-makeself",			".run",		NOT_NEED_CHARSET},
	{"application/x-perl",				".pl",		NOT_NEED_CHARSET},
	{"application/x-perl",				".pm",		NOT_NEED_CHARSET},
	{"application/x-pilot",				".prc",		NOT_NEED_CHARSET},
	{"application/x-pilot",				".pdb",		NOT_NEED_CHARSET},
	{"application/x-rar-compressed",	".rar",		NOT_NEED_CHARSET},
	{"application/x-redhat-package-manager",".rpm",	NOT_NEED_CHARSET},
	{"application/x-sea",				".sea",		NOT_NEED_CHARSET},
	{"application/x-shockwave-flash",	".swf",		NOT_NEED_CHARSET},
	{"application/x-stuffit",			".sit",		NOT_NEED_CHARSET},
	{"application/x-tcl",				".tcl",		NOT_NEED_CHARSET},
	{"application/x-tcl",				".tk",		NOT_NEED_CHARSET},
	{"application/x-x509-ca-cert",		".der",		NOT_NEED_CHARSET},
	{"application/x-x509-ca-cert",		".pem",		NOT_NEED_CHARSET},
	{"application/x-x509-ca-cert",		".crt",		NOT_NEED_CHARSET},
	{"application/x-xpinstall",			".xpi",		NOT_NEED_CHARSET},
	{"application/xhtml+xml",			".xhtml",	NOT_NEED_CHARSET},
	{"application/xspf+xml",			".xspf",		NOT_NEED_CHARSET},
	{"application/zip",					".zip",		NOT_NEED_CHARSET},

	{"application/octet-stream",		".bin",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".exe",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".dll",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".deb",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".dmg",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".iso",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".img",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".msi",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".msm",		NOT_NEED_CHARSET},
	{"application/octet-stream",		".msp",		NOT_NEED_CHARSET},


	{"application/vnd.openxmlformats-officedocument.wordprocessingml.document",		".docx",		NOT_NEED_CHARSET},
	{"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",			".xlsx",		NOT_NEED_CHARSET},
	{"application/vnd.openxmlformats-officedocument.presentationml.presentation",	".pptx",		NOT_NEED_CHARSET},

	{"audio/midi",					".mid",		NOT_NEED_CHARSET},
	{"audio/midi",					".kar",		NOT_NEED_CHARSET},
	{"audio/midi",					".midi",		NOT_NEED_CHARSET},
	{"audio/mpeg",					".mp3",		NOT_NEED_CHARSET},
	{"audio/ogg",					".ogg",		NOT_NEED_CHARSET},
	{"audio/x-m4a",					".m4a",		NOT_NEED_CHARSET},
	{"audio/x-realaudio",			".ra",		NOT_NEED_CHARSET},

	{"video/3gpp",					".3gp",		NOT_NEED_CHARSET},
	{"video/3gpp",					".3gpp",		NOT_NEED_CHARSET},
	{"video/mp2t",					".ts",		NOT_NEED_CHARSET},
	{"video/mp4",					".mp4",		NOT_NEED_CHARSET},
	{"video/mpeg",					".mpeg",		NOT_NEED_CHARSET},
	{"video/mpeg",					".mpg",		NOT_NEED_CHARSET},
	{"video/quicktime",				".mov",		NOT_NEED_CHARSET},
	{"video/webm",					".webm",		NOT_NEED_CHARSET},
	{"video/x-flv",					".flv",		NOT_NEED_CHARSET},
	{"video/x-m4v",					".m4v",		NOT_NEED_CHARSET},
	{"video/x-mng",					".mng",		NOT_NEED_CHARSET},
	{"video/x-ms-asf",				".asf",		NOT_NEED_CHARSET},
	{"video/x-ms-asf",				".asx",		NOT_NEED_CHARSET},
	{"video/x-ms-wmv",				".wmv",		NOT_NEED_CHARSET},
	{"video/x-msvideo",				".avi",		NOT_NEED_CHARSET},
};

const char *mime_get_name(const char *mimetype)
{
	int i=0;
	int cnt= sizeof(mime)/sizeof(mime_t);
	for(i=0;i<cnt;i++){
		if(strcmp((const char *)mime[i].mime,(const char *)mimetype)==0)
		{
			return mime[i].name;
		}
	}
	return NULL;
}

const char *mime_get_mimetype(const char *name,char *need_charset)
{
	int i=0;
	int cnt= sizeof(mime)/sizeof(mime_t);
	for(i=0;i<cnt;i++){
		if(strcmp((const char *)mime[i].name,(const char *)name)==0)
		{
			*need_charset = mime[i].need_charset;
			return mime[i].mime;
		}
	}
	*need_charset = default_mime.need_charset;
	return default_mime.mime;


}

static const char *extname(const char *filename)
{
  const char *slash = strrchr(filename, '/');
  const char *loc = strrchr(slash? slash : filename, '.');
  return loc? loc : "";
}

const char *mime_get_path(const char *name,char *need_charset)
{
	const char *ext=NULL;
	ext=extname(name);
	return mime_get_mimetype(ext,need_charset);
}

#ifdef MIME_TEST

int main(int argc,char **argv)
{
	char need_charset;
	printf("%s mime is  [%s\t\t\t]\n",argv[1],mime_get_path(argc[1],&need_charset));
	printf("%s  charset\n",need_charset? "need":"not need");

}
#endif

/* @(#)highwire/http.c
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "defs.h"
#include "Location.h"
#include "mime.h"
#include "http.h"
#include "inet.h"
#include "token.h"  /* needed for and before scanner.h */
#include "scanner.h"


#define R_BLK 16


/*============================================================================*/
ENCODING
http_charset (const char * beg, size_t len, MIMETYPE * p_type)
{
	MIMETYPE type = 0;
	ENCODING cset = ENCODING_Unknown;
	
	if (!p_type) {
		const char * p = memchr (beg, ';', len);
		if (p) {
			len -= p - beg;
			beg  = p;
			type = MIME_TEXT;
		} else {
			len = 0;
			beg = "";
		}
	
	} else {
		const char * end = beg;
		*p_type = type = mime_byString (beg, &end);
		if ((long)(len -= end - beg) < 0) {
			len = 0;
		} else {
			beg = end;
			while (len > 0 && isspace(*beg)) {
				len--;
				beg++;
			}
		}
		if (len <= 0) beg = "";
	}
	
	if (MIME_Major(type) == MIME_TEXT && *beg == ';') {
		while (--len > 0 && isspace (*(++beg)));
		if (len > 8 && strnicmp (beg, "charset=", 8) == 0) {
			beg += 8;
			len -= 8;
			cset = scan_encoding (beg, cset);
		}
	}
	
	return cset;
}


/*----------------------------------------------------------------------------*/
static BOOL
content_type (const char * beg, long len, HTTP_HDR * hdr)
{
	const char token[] = "Content-Type:";
	BOOL       found;
	
	if (len >= sizeof(token) && strnicmp (beg, token, sizeof(token)-1) == 0) {
		MIMETYPE type;
		ENCODING cset;
		beg += sizeof(token)-1;
		len -= sizeof(token)-1;
		while (isspace (*beg) && len-- > 0) beg++;
		if (len > 0) {
			cset = http_charset (beg, len, &type);
			if (type) {
				hdr->MimeType = type;
				if (cset) {
					hdr->Encoding = cset;
				}
			}
		}
		found = TRUE;
	} else {
		found = FALSE;
	}
	return found;
}

/*----------------------------------------------------------------------------*/
static BOOL
content_length (const char * beg, long len, HTTP_HDR * hdr)
{
	const char token[] = "Content-Length:";
	BOOL       found;
	
	if (len >= sizeof(token) && strnicmp (beg, token, sizeof(token)-1) == 0) {
		long size = atol (beg + sizeof(token)-1);
		if (size > 0) {
			hdr->Size = size;
		}
		found = TRUE;
	} else {
		found = FALSE;
	}
	return found;
}

/*----------------------------------------------------------------------------*/
static BOOL
redirect (const char * beg, long len, HTTP_HDR * hdr)
{
	const char token[] = "Location:";
	BOOL       found;
	
	if (len >= sizeof(token) && strnicmp (beg, token, sizeof(token)-1) == 0) {
		beg += sizeof(token)-1;
		len -= sizeof(token)-1;
		while (isspace (*beg) && len-- > 0) beg++;
		if (len > 0) {
			beg += len;
			while (isspace (*(--beg)) && --len);
			if (len) {
				char * p = strchr (hdr->Tail, '\0');
				do {
					*(--p) = *(beg--);
				} while (--len);
				hdr->Rdir = p;
			}
		}
		found = TRUE;
	} else {
		found = FALSE;
	}
	return found;
}

/*----------------------------------------------------------------------------*/
static BOOL
transfer_enc (const char * beg, long len, HTTP_HDR * hdr)
{
	const char token[] = "Transfer-Encoding:";
	BOOL       found;
	
	if (len >= sizeof(token) && strnicmp (beg, token, sizeof(token)-1) == 0) {
		beg += sizeof(token)-1;
		len -= sizeof(token)-1;
		while (isspace (*beg) && len-- > 0) beg++;
		if (strnicmp (beg, "chunked", 7) == 0) {
			hdr->Chunked = TRUE;
		}
		found = TRUE;
	} else {
		found = FALSE;
	}
	return found;
}

/*============================================================================*/
#ifdef USE_INET
short
http_header (LOCATION loc, HTTP_HDR * hdr, short * keep_alive, size_t blk_size)
{
	static char buffer[2048];
	size_t left  = sizeof(buffer) -4;
	int    reply = 0;
	
	char * ln_beg = buffer, * ln_end = ln_beg, * ln_brk = NULL;
	
	const char * name = NULL;
	int sock = location_open (loc, &name);
	
	hdr->Version  = 0x0000;
	hdr->Date     = -1;
	hdr->Size     = -1;
	hdr->MimeType = -1;
	hdr->Encoding = ENCODING_Unknown;
	hdr->Chunked  = FALSE;
	hdr->Rdir     = NULL;
	hdr->Head     = buffer;
	hdr->Tail     = buffer + sizeof(buffer) -1;
	hdr->Tlen     = 0;
	buffer[sizeof(buffer) -1] = '\0';
	
	if (sock < 0) {
		if (sock == -ETIMEDOUT) {
			strcpy (buffer, "Connection timeout!\n");
		} else if (sock < -1) {
			sprintf (buffer, "Error: %s\n", strerror(-sock));
		} else {
			strcpy (buffer, "No route to  host!\n");
		}
		return sock;
	
	} else {
		const char * stack = inet_info();
		size_t len = sprintf (buffer,
		     "%s %s%s HTTP/1.1\r\n"
		     "HOST: %s\r\n"
		     "User-Agent: Mozilla 2.0 (compatible; Atari %s/%i.%i.%i %s)\r\n"
		     "\r\n",
		     (keep_alive ? "GET" : "HEAD"), location_Path (loc, NULL), loc->File,
		     name,
		     _HIGHWIRE_FULLNAME_, _HIGHWIRE_MAJOR_, _HIGHWIRE_MINOR_,
		     _HIGHWIRE_REVISION_, (stack ? stack : ""));
		if ((len = inet_send (sock, buffer, len)) < 0) {
			if (len < -1) {
				sprintf (buffer, "Error: %s\n", strerror((int)-len));
			} else {
				strcpy (buffer, "Connection error!\n");
			}
			inet_close (sock);
			
			return (short)len;
		}
	}
	
	do {
		long n = inet_recv (sock, ln_end, (left <= blk_size ? left : blk_size));
		
		if (n < 0) { /* connection broken */
			if (reply) {
				ln_brk = ln_end;   /* seems to be a wonky server */
				*(ln_end++) = '\n';
				*(ln_end++) = '\n';
				*(ln_end)   = '\0';
			} else {
				reply = (short)n;
			}
			inet_close (sock);
			sock = -1;
			break;
		
		} else if (!n) { /* no data available yet */
			continue;
		
		} else {
			ln_brk  = memchr (ln_end, '\n', n);
			ln_end += n;
			left   -= n;
			if (!ln_brk) {
				continue;
			} else {
				*ln_end = '\0';
			}
		}
		
		if (!reply) {
			unsigned int major, minor;
			if (sscanf (ln_beg, "HTTP/%u.%u %i", &major, &minor, &reply) < 3) {
				reply = -1;
				inet_close (sock);
				sock = -1;
				break;
			
			} else {
				hdr->Version = (major <<8) | minor;
				ln_beg = ln_brk +1;
				n      = ln_end - ln_beg +1;
				ln_brk = (n ? memchr (ln_beg, '\n', n) : NULL);
			}
		}
		while (ln_brk) {
			if (ln_beg == ln_brk || (ln_beg[0] == '\r' && ln_beg[1] == '\n')) {
				*ln_brk = '\0';
				ln_beg = ln_brk +1;
				hdr->Tail = ln_beg;
				hdr->Tlen = (ln_end > ln_beg ? ln_end - ln_beg : 0);
				left = 0;
				break;
			}
			while (isspace (*ln_beg) && ++ln_beg < ln_brk);
			if ((n = ln_brk - ln_beg) > 0
				 && !content_length (ln_beg, n, hdr)
				 && !content_type   (ln_beg, n, hdr)
				 && !redirect       (ln_beg, n, hdr)
				 && !transfer_enc   (ln_beg, n, hdr)
				) {
				/* something else? */
			}
			ln_beg = ln_brk +1;
			n      = ln_end - ln_beg +1;
			ln_brk = (n ? memchr (ln_beg, '\n', n) : NULL);
		}
	} while (left);
	
	if (reply <= 0) {
		strcpy (buffer, (reply == -ECONNRESET
		                 ? "Connection reset by peer." : "Protocoll Error!\n"));
	}
	
	if (keep_alive) { /* keep_alive */
		*keep_alive = sock;
	
	} else {
		inet_close (sock);
	}
	return reply;
}
#endif /* USE_INET */

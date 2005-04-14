/* @(#)highwire/parser.c
 *
 * parser.c -- Parser functions for HTML expressions.
 *
 * AltF4 - Jan 14, 2002
 *
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#ifdef __PUREC__
# define CONTAINR struct s_containr *
#endif

#include "global.h"
#include "token.h"
#include "scanner.h"
#include "Loader.h"
#include "cache.h"
#include "parser.h"
#include "Containr.h"
#include "Location.h"
#include "fontbase.h"


#ifdef LATTICE   /* get rid of compiler bitfield bug */
# define BF16(t,n)   UWORD n
#else
# define BF16(t,n)   t     n :16
#endif
typedef struct { /* array to store KEY=VALUE pairs found while a parse() call */
	BF16(HTMLKEY,  Key);
	BF16(unsigned, Len);
	const char   * Value;
} KEYVALUE;

typedef struct s_style * STYLE;

typedef struct s_parser_priv {
	const char * Stack[5];
	struct s_own_mem {
		struct s_own_mem * Next;
		char             * Mem;
	}        OwnMem;
	struct s_style {
		STYLE    Next;
		STYLE    Link;
		KEYVALUE Css;
		char     ClassId; /* '.'Class '#'Id */
		char     Ident[1];
	}      * Styles;
	KEYVALUE KeyValTab[25];
	UWORD    KeyNum;
	WCHAR    Buffer[505]; /* 500 is enough for 124 UTF-8 characters, */
} * PARSPRIV;            /* if the encoding is unrecognized, parsed */
                         /* as single byte characters in PRE mode.  */
#define ParserPriv(p) ((PARSPRIV)(p +1))


/*============================================================================*/
PARSER
new_parser (LOADER loader)
{
	PARSER parser = malloc (sizeof (struct s_parser) +
	                        sizeof (struct s_parser_priv));
	PARSPRIV prsdata = ParserPriv(parser);
	TEXTBUFF current = &parser->Current;
	parser->Loader   = loader;
	parser->Target   = loader->Target;
	parser->hasStyle = FALSE;
	parser->ResumePtr = loader->Data;
	parser->ResumeSub = NULL;
	parser->ResumeFnc = NULL;
	parser->ResumeErr = E_OK;
	memset (prsdata->Stack, 0, sizeof(prsdata->Stack));
	prsdata->Styles = NULL;
	prsdata->KeyNum = 0;
	prsdata->OwnMem.Next = NULL;
	prsdata->OwnMem.Mem  = NULL;
	
	memset (current, 0, sizeof (parser->Current));
	current->font     = fontstack_setup (&current->fnt_stack, -1);
	current->text     = current->buffer = prsdata->Buffer;
	parser->Watermark = current->buffer + numberof(prsdata->Buffer) -6;
	
	parser->Frame  = new_frame (loader->Location, current,
	                            loader->Encoding, loader->MimeType,
	                            loader->MarginW, loader->MarginH);
	
	if (loader->ScrollV > 0) {
		parser->Frame->v_bar.on     = TRUE;
		parser->Frame->v_bar.scroll = loader->ScrollV;
		parser->Frame->Page.Rect.H  = parser->Frame->clip.g_h +1024;
	}
	if (loader->ScrollH > 0) {
		parser->Frame->h_bar.on     = TRUE;
		parser->Frame->h_bar.scroll = loader->ScrollH;
		parser->Frame->Page.Rect.W  = parser->Frame->clip.g_w +1024;
	}
	parser->Frame->Container = parser->Target;
	
	containr_clear (parser->Target);
	if (!loader->notified) {
		char buf[1024];
		location_FullName (loader->Location, buf, sizeof(buf));
		loader->notified = containr_notify (loader->Target, HW_PageStarted, buf);
	}
	
	return parser;
}

/*============================================================================*/
void
delete_parser (PARSER parser)
{
	CONTAINR cont   = parser->Target;
	FRAME    frame  = parser->Frame;
	
	if (!cont->Mode) {
		frame_finish (frame, parser, &parser->Current);
		containr_setup (cont, frame, frame->Location->Anchor);
		if (parser->Loader->notified) {
			containr_notify (cont, HW_PageFinished, &cont->Area);
			parser->Loader->notified = FALSE;
		}
	} else {
		fontstack_clear (&parser->Current.fnt_stack);
		delete_frame (&frame);
		containr_calculate (cont, NULL);
	}
	delete_loader (&parser->Loader);
	
	if (ParserPriv(parser)->OwnMem.Mem) {
		struct s_own_mem * own = &ParserPriv(parser)->OwnMem;
		do {
			struct s_own_mem * next = own->Next;
			free (own->Mem);
			if (next) {
				own->Next = next->Next;
				own->Mem  = next->Mem;
				free (next);
			} else {
				break;
			}
		} while (own->Mem);
	}
	if (ParserPriv(parser)->Styles) {
		STYLE style = ParserPriv(parser)->Styles, next;
		do {
			while (style->Link) {
				STYLE link = style->Link;
				style->Link = link->Link;
				free (link);
			}
			next = style->Next;
			free (style);
		} while ((style = next) != NULL);
	}
	free (parser);
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
static int
resume_job (void * arg, long invalidated)
{
	LOADER loader = arg;
	PARSER parser = loader->FreeArg;
	if (loader->Error) {
		char buf[1024];
		location_FullName (loader->Location, buf, sizeof(buf));
		printf ("not found: '%s'\n", buf);
		if (!invalidated) {
			if (!parser->ResumeSub) {
				parser->ResumeFnc = NULL;
			}
			parser->ResumeErr = loader->Error;
		}
	} else {
		parser->ResumeErr = E_OK;
	}
	delete_loader (&loader);
	return FALSE;
}

/*============================================================================*/
int
parser_resume (PARSER parser, void * func, const char * ptr_sub, LOCATION loc)
{
	if (func) {
		parser->ResumeFnc = func;
		parser->ResumePtr = ptr_sub;
		if (loc) {
			parser->ResumeSub = NULL;
		}
	} else {
		parser->ResumeFnc = NULL;
		parser->ResumePtr = NULL;
		parser->ResumeSub = ptr_sub;
	}
	if (!loc || !ptr_sub) {
		parser->ResumeErr = E_OK;
	} else if (parser->ResumeErr == 2/*EBUSY*/) {
		puts ("parser_resume(): busy");
	} else {
		start_objc_load (parser->Target, NULL, loc, resume_job, parser);
		parser->ResumeErr = 2/*EBUSY*/;
	}
	
	return -2; /*JOB_NOOP */
}


/*----------------------------------------------------------------------------*/
static KEYVALUE *
find_key (PARSER parser, HTMLKEY key)
{
	PARSPRIV prsdata = ParserPriv(parser);
	UWORD        num = prsdata->KeyNum;
	KEYVALUE   * ent = prsdata->KeyValTab + num;
	while (num--) {
		if ((--ent)->Key == key) {
			return ent;
		}
	}
	return NULL;
}


/*==============================================================================
 * Finds the VALUE of 'key' that was read while the last parse() call.
 * If successful the VALUE will be copied to 'output' up to 'max_len' character
 * (including the trailing '\0') and a TRUE will be returned.
 * Else a FALSE will be returned.
 */
BOOL
get_value (PARSER parser, HTMLKEY key, char * output, const size_t max_len)
{
	KEYVALUE * ent = find_key (parser, key);
	BOOL     found = (ent != NULL);
	
	if (output) {
		if (!found) {
			output[0] = '\0';
		} else if (!ent->Len || ent->Len >= max_len) {
			found = FALSE;
			output[0] = '\0';
		} else {
			memcpy (output, ent->Value, ent->Len);
			output[ent->Len] = '\0';
		}
	}
	return found;
}


/*==============================================================================
 * Returns the VALUE of 'key' that was read while the last parse() call as a
 * malloc'ed zero terminated character string.
 * If not successful the result is a NULL pointer.
 */
char *
get_value_str (PARSER parser, HTMLKEY key)
{
	KEYVALUE * ent = find_key (parser, key);
	char * found;
	
	if (!ent) {
		found = NULL;
	
	} else {
		found = malloc (ent->Len +1);
		memcpy (found, ent->Value, ent->Len);
		found[ent->Len] = '\0';
	}
	return found;
}


/*==============================================================================
 * Returns the first character of the VALUE of 'key' that was read while the
 * last parse() call.
 * If not successful a '\0' will be returned.
 */
char
get_value_char (PARSER parser, HTMLKEY key)
{
	KEYVALUE * ent = find_key (parser, key);
	return (ent && ent->Len ? ent->Value[0] : '\0');
}


/*==============================================================================
 * Returns the VALUE of 'key' that was read while the last parse() call as a
 * unsigned short.
 * If not successful the value of 'dflt' will be returned instead, which may
 * also be negative.
 */
WORD
get_value_unum (PARSER parser, HTMLKEY key, WORD dflt)
{
	KEYVALUE * ent = find_key (parser, key);
	
	if (ent && ent->Len) {
		char * tail;
		long   value = strtol (ent->Value, &tail, 10);
		if (value >= 0 && value <= 0x7FFF && tail != ent->Value) {
			dflt = (WORD)value;
		}
	}
	return dflt;
}


/*==============================================================================
 * Returns the VALUE of 'key' that was read while the last parse() call as a
 * signed short.
 * On success the return value is either a positive absolute number or a
 * negative fractional of -1024 if a trailing '%' was found.
 * If not successful a zero will be returned.
 */
WORD
get_value_size (PARSER parser, HTMLKEY key)
{
	KEYVALUE * ent = find_key (parser, key);
	WORD      size = 0;
	
	if (ent && ent->Len) {
		char * tail;
		long   val = strtol (ent->Value, &tail, 10);
		if (val > 0 && val < 10000 && tail != ent->Value) {
			if (*tail != '%') {
				size = val;
			} else if (val < 99) {
				size = -((val *1024 +50) /100);
			} else {
				size = -1024;
			}
		}
	}
	return size;
}


/*==============================================================================
 * Returns the VDI color VALUE of 'key' that was read while the last parse()
 * call.
 * If not successful a negative number will be returned.
 */
WORD
get_value_color (PARSER parser, HTMLKEY key)
{
	KEYVALUE * ent = find_key (parser, key);
	WORD     color = -1;

	if (ent && ent->Len) {
		long value = scan_color (ent->Value, ent->Len);
		if (value >= 0) {
			color = remap_color (value);
		}
	}
	return color;
}


/*----------------------------------------------------------------------------*/
static KEYVALUE *
css_values (PARSER parser, const char * line, size_t len)
{
	PARSPRIV   prsdata = ParserPriv(parser);
	KEYVALUE * entry   = prsdata->KeyValTab + prsdata->KeyNum;
	while (len) {
		const char * ptr = line;
		short        css = scan_css (&line, len);
		const char * val = (*line == ':' ? ++line : NULL);
		KEYVALUE   * ent = NULL;
		len -= line - ptr;
		
		if (val) {
			while (len && isspace(*val)) {
				len--;
				val++;
			}
			line = val;
		}
		while (len && *line != ';') {
			len--;
			line++;
		}
		if (val) {
			ptr = line;
			while (--ptr >= val && isspace(*ptr));
			if (ptr < val) val = NULL;
		}
		if (val && ((css < CSS_Unknown
		             && (ent = find_key (parser, (HTMLKEY)css)) == NULL)
		            || css != CSS_Unknown)
		        && (prsdata->KeyNum < numberof(prsdata->KeyValTab))) {
			ent = entry++;
			prsdata->KeyNum++;
		}
		if (ent) {
			ent->Key   = css;
			ent->Value = val;
			ent->Len   = (unsigned)(ptr - val +1);
		}
		if (len && *line == ';') {
			len--;
			line++;
		}
		while (len && isspace(*line)) {
			len--;
			line++;
		}
	}
	return entry;
}

/*----------------------------------------------------------------------------*/
static KEYVALUE *
css_filter (PARSER parser, HTMLTAG tag, char class_id, KEYVALUE * keyval)
{
	PARSPRIV   prsdata = ParserPriv(parser);
	KEYVALUE * entry   = prsdata->KeyValTab + prsdata->KeyNum;
	STYLE      style   = prsdata->Styles;
	while (style) {
		BOOL match;
		if ((style->Css.Key && style->Css.Key != tag) ||
		    (class_id != style->ClassId)              ||
		    (keyval && (strncmp (style->Ident, keyval->Value, keyval->Len)
	                         || style->Ident[keyval->Len]))) {
			match = FALSE;
		} else {
			STYLE    link = style->Link;
			DOMBOX * box  = parser->Current.parentbox;
			while (link && box) {
				if (*link->Css.Value == '>') {
					/* exact: <parent><tag> */
				} else if (*link->Css.Value == '*') {
					/* exact: <parent><*><tag> */
					if ((box = box->Parent) == NULL) break;
				} else if (*link->Css.Value == '+') {
					/* exact: </sibling><tag> */
					if ((box = box->ChildBeg) == NULL) break;
					while (box->Sibling && box->Sibling->Sibling) box = box->Sibling;
				}
				if (link->ClassId == '.') {
					if (!box->ClName || strcmp (box->ClName, link->Ident)) {
						box = (!*link->Css.Value ? box->Parent : NULL);
						continue;
					}
				} else if (link->ClassId == '#') {
					if (!box->IdName || strcmp (box->IdName, link->Ident)) {
						box = (!*link->Css.Value ? box->Parent : NULL);
						continue;
					}
				}
				if (link->Css.Key && link->Css.Key != box->HtmlCode) {
					box = (!*link->Css.Value ? box->Parent : NULL);
					continue;
				
				} else {
					link = link->Link;
					box  = box->Parent;
				}
			}
			match = (link == NULL);
		}
		if (match) {
			parser->hasStyle = TRUE;
			entry = css_values (parser, style->Css.Value, style->Css.Len);
		}
		style = style->Next;
	}
	return entry;
}

/*==============================================================================
 * Parses a html TAG expression of the forms
 *    <TAG>  |  <TAG KEY ...>  |  <TAG KEY=VALUE ...>
 * The 'pptr' content must point to the first character after the leading '<'
 * and a possibly '/'.  After processing it is set to first character behind the
 * expression, either behind the closing '>' or to a trailing '\0'.
 * If successful the found known KEYs are stored with their VALUEs internally
 * and a TAG enum is returned.
 * Else the symbol TAG_Unknown is returned.
 * 
 * also note that val_num is the number of total keys for this tag present
 */
HTMLTAG
parse_tag (PARSER parser, const char ** pptr)
{
	const char * line = *pptr;
	PARSPRIV     prsdata;
	KEYVALUE   * entry;
	HTMLTAG      tag;
	BOOL         lookup;
	
	if (parser) {
		prsdata = ParserPriv(parser);
		parser->hasStyle = FALSE;
		prsdata->KeyNum  = 0;
		entry   = prsdata->KeyValTab;
		lookup  = TRUE;
	} else {
		prsdata = NULL;
		entry   = NULL;
		lookup  = FALSE;
	}
	
	/* first check for comment
	 */
	if (*line == '!') {
		line++;
		if (line[0] == '-' && line[1] == '-') {
			const char * end = strstr (line + 2, "--");

			if (end)
				line = end;
		}
		while (*line && *(line++) != '>')
			;
		*pptr = line;

		return TAG_Unknown;
	}

	if ((tag = scan_tag (&line)) == TAG_Unknown) {
		lookup = FALSE;
	
	} else if (lookup && prsdata->Styles) {
		entry = css_filter (parser, tag, '\0', NULL);
	}

	/*** if the tag is known or not, in every case we have to go through
	 *   the list of variables to avoid the parser from becoming confused
	*/
	while (isspace(*line)) line++;

	while (*line  &&  *line != '>') {
		const char  * val = line;
		HTMLKEY key   = scan_key (&line, lookup);
		BOOL    rhs   = (val == line);
		char    delim = '\0';

		while (isspace(*line)) line++;
		if (*line == '=') {
			while (isspace(*(++line)));
			rhs = TRUE;
		}
		if (rhs) {
			val = line;
			if      (*val == 39)  line = strchr (++val, (delim = 39));
			else if (*val == '"') line = strchr (++val, (delim = '"'));
			else                  line = strpbrk (val, " >\t\r\n");
			if (!line) line = strchr (val, (delim = '\0'));
		} else {
			val = NULL;
		}
		if (lookup) {
			unsigned len = (unsigned)(val ? line - val : 0ul);
			while (len && isspace (*val)) {
				val++;
				len--;
			}
			while (len && isspace (val[len -1])) {
				len--;
			}
			if (key == KEY_STYLE) {
				if (val && len) {
					parser->hasStyle = TRUE;
					entry = css_values (parser, val, len);
				}
			} else if (prsdata->KeyNum < numberof(prsdata->KeyValTab)) {
				entry->Key = key;
				if (val && len) {
					entry->Value = val;
					entry->Len   = len;
				} else {
					entry->Value = NULL;
					entry->Len   = 0;
				}
				entry++;
				prsdata->KeyNum++;
				if (val && len && prsdata->Styles) {
					if (key == KEY_CLASS) {
						entry = css_filter (parser, tag, '.', entry -1);
					} else if (key == KEY_ID) {
						entry = css_filter (parser, tag, '#', entry -1);
					}
				}
			}
		}
		if (delim && delim == *line) line++;
		while (isspace(*line)) line++;
	}

	*pptr = (*line ? ++line : line);
	
	return tag;
}


/*----------------------------------------------------------------------------*/
static const char *
css_import (PARSER parser, const char * ptr)
{
	PARSPRIV prsdata = ParserPriv(parser);
	LOCATION     loc = NULL;
	const char * p   = ptr +7;
	
	while (isspace (*p)) p++;
	if (strnicmp (p, "url", 3) == 0) {
		p += 3;
		while (isspace (*p)) p++;
		if (*p != '(') {
			p = NULL;
		} else {
			while (isspace (*(++p)));
		}
	}
	if (!p) {
		return ptr;
		
	} else {
		const char * e = (*p == '"'  ? strchr (++p, '"')  :
		                  *p == '\'' ? strchr (++p, '\'') : strchr (p, ')'));
		if (e && e > p && !prsdata->Stack[numberof(prsdata->Stack)-1]) {
			char   buf[1024];
			size_t len = min (e - p, sizeof(buf) -1);
			((char*)memcpy (buf, p, len))[len] = '\0';
			loc = new_location (buf, parser->Frame->BaseHref);
		}
		p = (e ? strchr (++e, ';') : e);
		p = (p ? ++p : e ? e : strchr (ptr, '\0'));
	}
	if (!loc) {
		ptr = p; /* invalid syntax or stack exceeded, skip it */
	
	} else {
		BOOL   push;
		size_t size = 0;
		char * file = NULL;
		if (PROTO_isLocal (loc->Proto)) {
			file = load_file (loc, &size, &size);
			push = TRUE;
			ptr  = p;
		} else {
			struct s_cache_info info;
			CRESULT res = cache_query (loc, 0, &info);
			if (res & CR_LOCAL) {
				file = load_file (info.Local, &size, &size);
				ptr  = p;
			} else {
				parser_resume (parser, NULL, ptr, (res & CR_BUSY ? NULL : loc));
				ptr  = NULL;
			}
			push = (prsdata->Stack[0] != p);
		}
		if (file) {
			struct s_own_mem * own = NULL;
			if (size > 0) {
				if (!prsdata->OwnMem.Mem) {
					own = &prsdata->OwnMem;
				} else if ((own = malloc (sizeof(struct s_own_mem))) != NULL) {
					own->Mem  = prsdata->OwnMem.Mem;
					own->Next = prsdata->OwnMem.Next;
					prsdata->OwnMem.Next = own;
				}
			}
			if (own) {
				ptr = prsdata->OwnMem.Mem = file;
			} else {
				free (file);
				push = FALSE;
			}
		}
		if (push) {
			short i = (short)numberof(prsdata->Stack) -1;
			do {
				prsdata->Stack[i] = prsdata->Stack[i -1];
			} while (--i);
			prsdata->Stack[0] = p;
		}
		free_location (&loc);
	}
	return ptr;
}

/*============================================================================*/
static char next (const char ** pp) {
	const char * p = *pp; while (isspace(*p)) p++; return *(*pp = p);
}
/*- - - - - - - - - - - - - - - - - - - - - - - -*/
const char *
parse_css (PARSER parser, const char * p, char * takeover)
{
	PARSPRIV prsdata = ParserPriv(parser);
	STYLE  * p_style = &prsdata->Styles;
	BOOL     err     = FALSE;
	
	if (takeover) {
		if (prsdata->OwnMem.Mem) {
			struct s_own_mem * own = malloc (sizeof(struct s_own_mem));
			if (!own) {
				free (takeover);
				return NULL;
			}
			own->Mem  = prsdata->OwnMem.Mem;
			own->Next = prsdata->OwnMem.Next;
			prsdata->OwnMem.Next = own;
		}
		p = prsdata->OwnMem.Mem = takeover;
	
	} else if (!p) { /* error case, continue with next in stack */
		short i;
		if ((p = prsdata->Stack[0]) == NULL) {
			puts ("CSS stack underflow!");
			return parser->ResumePtr;
		}
		for (i = 0; i < numberof(prsdata->Stack) -1; i++) {
			prsdata->Stack[i] = prsdata->Stack[i +1];
		}
		prsdata->Stack[numberof(prsdata->Stack)-1] = NULL;
	}
	
	while (*p_style) { /* jump to the end of previous stored style sets */
		p_style = &(*p_style)->Next;
	}
	
	do {
		const char * beg = NULL, * end = NULL, * tok = p;
		STYLE style = NULL;
		STYLE b_lnk = NULL;
		BOOL  skip  = FALSE;
		
		while (*p) {
			WORD key = TAG_Unknown;
			char cid = '\0';
			
			if (next(&p) == '/') { /*........................ comment */
				const char * q = p;
				if (*(++q) == '/') {        /* C++ style */
					q = strchr (q +1, '\n');
				} else if (*(q++) == '*') { /* C style */
					while (((q = strchr (q, '*')) != NULL) && *(++q) != '/');
				} else {
					q = NULL; /* syntax */
				}
				if ((err = (q == NULL)) == TRUE) break;
				p = q +1;
				continue;
			}
			
			tok = p;
			
			if (*p == '@') { /*............................... special */
				const char * q = p;
				if (strnicmp (q +1, "import", 6) == 0) {
					if ((q = css_import (parser, q)) == NULL) {
						return NULL;
					}
				} else {
					while (isalpha (*(++q)));
					while (isspace (*(++q)));
					q = (*q == '{' ? strchr (q +1, '}') : NULL);
					if (q) q++;
				}
				if ((err = (q == NULL)) == TRUE) break;
				p = q;
				continue;
			}
			
			if (*p == '*') { /*................................ joker */
				if (*(++p) == '.') {
					key = TAG_LastDefined; /* matches all */
				} else if (isspace (*p)) {
					/* ignore IE6 nonsense */
					while (isspace (*(++p)));
					continue;
				} else {
					err = TRUE;
					break;
				}
			
			} else if (isalpha (*p)) { /*........................ tag */
				key = scan_tag (&p);
			}
			
			if (*p == '.' || *p == '#') { /*............. class or id */
				cid = *(p++);
				if ((err = (!isalpha (*p))) == TRUE) break;
				beg = p;
				while (isalnum (*(++p)) || *p == '-' || *p == '_');
				end = p;
				if (beg == end) cid = '\0';
			
			} else {
				beg = end = NULL;
			}
			
			if (*p == ':') { /*........................ pseudo format */
				if (key == TAG_A
				    && strnicmp (p +1, "link", 4) == 0 && !isalpha (*(p +5))) {
					p += 5; /* ignore */
				} else {
					while (isalpha (*(++p)) || *p == ':' || *p == '-');
					skip = TRUE; /* ignore */
				}
			}
			
			if (*p == '[') { /*..................... conditional rule */
				while (*(++p) && *p != ']');
				if (*p) p++;
				skip = TRUE; /* ignore */
			}
			
			if (key > TAG_Unknown || cid) { /* store */
				size_t len = end - beg;
				STYLE  tmp = malloc (sizeof (struct s_style) + len);
				if (len) memcpy (tmp->Ident, beg, len);
				tmp->Ident[len] = '\0';
				tmp->ClassId    = cid;
				tmp->Css.Key    = (key == TAG_LastDefined ? TAG_Unknown : key);
				tmp->Css.Value  = NULL;
				tmp->Next    = NULL;
				if (!*p_style) {
					tmp->Link   = NULL;
					*p_style    = tmp;
				} else if (!style->Css.Value) {
					b_lnk       = style;
					tmp->Link   = NULL;
					style->Next = tmp;
				} else if (!b_lnk) {
					tmp->Link   = style;
					*p_style    = tmp;
				} else {
					tmp->Link   = b_lnk->Next;
					b_lnk->Next = tmp;
				}
				style = tmp;
			}
			
			/*............. look one ahead for selector concatenation */
			if (isalpha (next(&p)) || *p == '.' || *p == '#') {
				if (style) style->Css.Value = "";
				continue;
			} else if (*p == '>' || *p == '+' || *p == '*') {
				if (style) style->Css.Value = p;
				p++;
				continue;
			} else if (*p == ':') { /* pseudo format, to be ignored */
				continue;
			}
			
			if (skip) {
				if (style) {
					while (style->Link) {
						STYLE link = style->Link;
						style->Link = link->Link;
						free (link);
					}
					free (style);
				}
				if (*p_style == style) {
					*p_style = NULL;
					style    = NULL;
				} else {
					b_lnk->Next = NULL;
					style       = b_lnk;
				}
				skip = FALSE;
			}
			if (next(&p) == '/') continue;
			else if (*p  == ',') p++;
			else                 break;
		}
		
		if (err || *p != '{') {
			p         = tok;
			beg = end = NULL;
			err       = TRUE;
			
		} else /* if (*p == '{') */ {
			while (isspace (*(++p)));
			beg = p;
			while (*p && *p != '}') {
				if (*p == '\'' || *p == '"') {
					char q = *p;
					while (*(++p) && *p != q && !(*p == '\\' && !*(++p)));
					if (*p) p++;
					end = NULL;
					continue;
				}
				if (!isspace (*p)) {
					end = NULL;
				} else if (!end) {
					end = p;
				}
				p++;
			}
			if (!end) {
				end = p;
			}
			if (end[-1] == ';') {
				end--; /* cut off trailing semicolon to detect empty rules */
			}
			if (*p) while (isspace (*(++p)));
		}
		
		if (style) {
			style = *p_style;
			
			if (end > beg) {   /* setup all styles to the rule string */
				unsigned len = (unsigned)(end - beg);
				do {
					style->Css.Len   = len;
					style->Css.Value = beg;
					p_style = &style->Next;
				} while ((style = *p_style) != NULL);
			
			} else do {   /* no rule string, delete styles */
				while (style->Link) {
					STYLE link = style->Link;
					style->Link = link->Link;
					free (link);
				}
				*p_style = style->Next;
				free (style);
			} while ((style = *p_style) != NULL);
		}
		
		if (!*p || err) {
			if (err && *p != '-' && *p != '<') {
				int n  = 0, ln = 0, cn = 0;
				while (p[n]) {
					if (p[n] == '\n') {
						if (++ln == 2) {
							n--;
							break;
						} else {
							cn = 0;
						}
					} else {
						if (++cn == 79) {
							break;
						}
					}
					n++;
				}
				printf("parse_css(): stopped at\n%.*s\n", n, p);
			}
			if (prsdata->Stack[0]) {
				short i;
				err = FALSE;
				p   = prsdata->Stack[0];
				for (i = 0; i < numberof(prsdata->Stack) -1; i++) {
					prsdata->Stack[i] = prsdata->Stack[i +1];
				}
				prsdata->Stack[numberof(prsdata->Stack)-1] = NULL;
			}
		}
	} while (*p && !err);
	
	return p;
}

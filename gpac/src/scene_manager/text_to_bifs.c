/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) Jean Le Feuvre 2000-2005 
 *					All rights reserved
 *
 *  This file is part of GPAC / Scene Management sub-project
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */



#include <gpac/constants.h>
#include <gpac/utf.h>
#include <gpac/xml.h>
#include <gpac/internal/media_dev.h>
#include <gpac/scene_manager.h>

enum
{
	GF_TEXT_IMPORT_NONE = 0,
	GF_TEXT_IMPORT_SRT,
	GF_TEXT_IMPORT_SUB,
	GF_TEXT_IMPORT_TTXT,
	GF_TEXT_IMPORT_TEXML,
};

#define REM_TRAIL_MARKS(__str, __sep) while (1) {	\
		u32 _len = strlen(__str);		\
		if (!_len) break;	\
		_len--;				\
		if (strchr(__sep, __str[_len])) __str[_len] = 0;	\
		else break;	\
	}	\


static GF_Err gf_text_guess_format(char *filename, u32 *fmt)
{
	char szLine[2048], szTest[10];
	u32 val;
	FILE *test = fopen(filename, "rt");
	if (!test) return GF_URL_ERROR;

	while (fgets(szLine, 2048, test) != NULL) {
		REM_TRAIL_MARKS(szLine, "\r\n\t ")

		if (strlen(szLine)) break;
	}
	*fmt = GF_TEXT_IMPORT_NONE;
	if ((szLine[0]=='{') && strstr(szLine, "}{")) *fmt = GF_TEXT_IMPORT_SUB;
	else if (sscanf(szLine, "%d", &val)==1) {
		sprintf(szTest, "%d", val);
		if (!strcmp(szTest, szLine)) *fmt = GF_TEXT_IMPORT_SRT;
	}
	else if (!strnicmp(szLine, "<?xml ", 6)) {
		char *ext = strrchr(filename, '.');
		if (!strnicmp(ext, ".ttxt", 5)) *fmt = GF_TEXT_IMPORT_TTXT;
		ext = strstr(szLine, "?>");
		if (ext) ext += 2;
		if (!ext[0]) fgets(szLine, 2048, test);
		if (strstr(szLine, "x-quicktime-tx3g")) *fmt = GF_TEXT_IMPORT_TEXML;
	}
	fclose(test);
	return GF_OK;
}

static GF_Err gf_text_import_srt_bifs(GF_SceneManager *ctx, GF_ESD *src, GF_MuxInfo *mux)
{
	GF_Err e;
	GF_Node *text, *font;
	GF_StreamContext *srt;
	FILE *srt_in;
	GF_FieldInfo string, style;
	u32 sh, sm, ss, sms, eh, em, es, ems, start, end;
	GF_AUContext *au;
	GF_Command *com;
	SFString *sfstr;
	GF_CommandField *inf;
	Bool italic, underlined, bold;
	u32 state, curLine, line, i, len;
	char szLine[2048], szText[2048], *ptr;
	GF_StreamContext *sc = NULL;

	if (!ctx->scene_graph) {
		fprintf(stdout, "Error importing SRT: base scene not assigned\n");
		return GF_BAD_PARAM;
	}
	for (i=0; i<gf_list_count(ctx->streams); i++) {
		sc = gf_list_get(ctx->streams, i);
		if (sc->streamType==GF_STREAM_SCENE) break;
		sc = NULL;
	}

	if (!sc) {
		fprintf(stdout, "Error importing SRT: Cannot locate base scene\n");
		return GF_BAD_PARAM;
	}
	if (!mux->textNode) {
		fprintf(stdout, "Error importing SRT: Target text node unspecified\n");
		return GF_BAD_PARAM;
	}
	text = gf_sg_find_node_by_name(ctx->scene_graph, mux->textNode);
	if (!text) {
		fprintf(stdout, "Error importing SRT: Cannot find target text node %s\n", mux->textNode);
		return GF_BAD_PARAM;
	}
	if (gf_node_get_field_by_name(text, "string", &string) != GF_OK) {
		fprintf(stdout, "Error importing SRT: Target text node %s doesn't look like text\n", mux->textNode);
		return GF_BAD_PARAM;
	}

	font = NULL;
	if (mux->fontNode) {
		font = gf_sg_find_node_by_name(ctx->scene_graph, mux->fontNode);
		if (!font) {
			fprintf(stdout, "Error importing SRT: Cannot find target font node %s\n", mux->fontNode);
			return GF_BAD_PARAM;
		}
		if (gf_node_get_field_by_name(font, "style", &style) != GF_OK) {
			fprintf(stdout, "Error importing SRT: Target font node %s doesn't look like font\n", mux->fontNode);
			return GF_BAD_PARAM;
		}
	}

	srt_in = fopen(mux->file_name, "rt");
	if (!srt_in) {
		fprintf(stdout, "Cannot open input SRT %s\n", mux->file_name);
		return GF_URL_ERROR;
	}

	srt = gf_sm_stream_new(ctx, src->ESID, GF_STREAM_SCENE, 1);
	if (!srt) return GF_OUT_OF_MEM;

	if (!src->slConfig) src->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	src->slConfig->timestampResolution = 1000;
	if (!src->decoderConfig) src->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	src->decoderConfig->streamType = GF_STREAM_SCENE;
	src->decoderConfig->objectTypeIndication = 1;

	e = GF_OK;
	state = end = 0;
	curLine = 0;
	au = NULL;
	com = NULL;
	italic = underlined = bold = 0;
	inf = NULL;

	while (1) {
		char *sOK = fgets(szLine, 2048, srt_in);

		if (sOK) REM_TRAIL_MARKS(szLine, "\r\n\t ")

		if (!sOK || !strlen(szLine)) {
			state = 0;
			if (au) {
				/*if italic or underscore do it*/
				if (font && (italic || underlined || bold)) {
					com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
					com->node = font;
					gf_node_register(font, NULL);
					inf = gf_sg_command_field_new(com);
					inf->fieldIndex = style.fieldIndex;
					inf->fieldType = style.fieldType;
					sfstr = inf->field_ptr = gf_sg_vrml_field_pointer_new(style.fieldType);
					if (bold && italic && underlined) sfstr->buffer = strdup("BOLDITALIC UNDERLINED");
					else if (italic && underlined) sfstr->buffer = strdup("ITALIC UNDERLINED");
					else if (bold && underlined) sfstr->buffer = strdup("BOLD UNDERLINED");
					else if (underlined) sfstr->buffer = strdup("UNDERLINED");
					else if (bold && italic) sfstr->buffer = strdup("BOLDITALIC");
					else if (bold) sfstr->buffer = strdup("BOLD");
					else sfstr->buffer = strdup("ITALIC");
					gf_list_add(au->commands, com);
				}

				au = gf_sm_stream_au_new(srt, end, 0, 1);
				com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
				com->node = text;
				gf_node_register(text, NULL);
				inf = gf_sg_command_field_new(com);
				inf->fieldIndex = string.fieldIndex;
				inf->fieldType = string.fieldType;
				inf->field_ptr = gf_sg_vrml_field_pointer_new(string.fieldType);
				gf_list_add(au->commands, com);
				/*reset font styles so that all AUs are true random access*/
				if (font) {
					com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
					com->node = font;
					gf_node_register(font, NULL);
					inf = gf_sg_command_field_new(com);
					inf->fieldIndex = style.fieldIndex;
					inf->fieldType = style.fieldType;
					inf->field_ptr = gf_sg_vrml_field_pointer_new(style.fieldType);
					gf_list_add(au->commands, com);
				}
				au = NULL;
			}
			inf = NULL;
			if (!sOK) break;
			continue;
		}

		switch (state) {
		case 0:
			if (sscanf(szLine, "%d", &line) != 1) {
				fprintf(stdout, "Bad SRT format\n");
				e = GF_CORRUPTED_DATA;
				goto exit;
			}
			if (line != curLine + 1) {
				fprintf(stdout, "Error importing SRT frame (previous %d, current %d)\n", curLine, line);
				e = GF_CORRUPTED_DATA;
				goto exit;
			}
			curLine = line;
			state = 1;
			break;
		case 1:
			if (sscanf(szLine, "%d:%d:%d,%d --> %d:%d:%d,%d", &sh, &sm, &ss, &sms, &eh, &em, &es, &ems) != 8) {
				fprintf(stdout, "Error importing SRT frame %d\n", curLine);
				e = GF_CORRUPTED_DATA;
				goto exit;
			}
			start = (3600*sh + 60*sm + ss)*1000 + sms;
			if (start<end) {
				fprintf(stdout, "WARNING: corrupted SRT frame starts before end of previous one (SRT Frame %d) - adjusting time stamps\n", curLine);
				start = end;
			}
			end = (3600*eh + 60*em + es)*1000 + ems;
			/*make stream start at 0 by inserting a fake AU*/
			if ((curLine==1) && start>0) {
				au = gf_sm_stream_au_new(srt, 0, 0, 1);
				com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
				com->node = text;
				gf_node_register(text, NULL);
				inf = gf_sg_command_field_new(com);
				inf->fieldIndex = string.fieldIndex;
				inf->fieldType = string.fieldType;
				inf->field_ptr = gf_sg_vrml_field_pointer_new(string.fieldType);
				gf_list_add(au->commands, com);
			}

			au = gf_sm_stream_au_new(srt, start, 0, 1);
			com = NULL;
			state = 2;			
			italic = underlined = bold = 0;
			break;

		default:
			ptr = szLine;
			/*FIXME - other styles posssibles ??*/
			while (1) {
				if (!strnicmp(ptr, "<i>", 3)) {
					italic = 1;
					ptr += 3;
				}
				else if (!strnicmp(ptr, "<u>", 3)) {
					underlined = 1;
					ptr += 3;
				}
				else if (!strnicmp(ptr, "<b>", 3)) {
					bold = 1;
					ptr += 3;
				}
				else
					break;
			}
			/*if style remove end markers*/
			while ((strlen(ptr)>4) && (ptr[strlen(ptr) - 4] == '<') && (ptr[strlen(ptr) - 1] == '>')) {
				ptr[strlen(ptr) - 4] = 0;
			}

			if (!com) {
				com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
				com->node = text;
				gf_node_register(text, NULL);
				inf = gf_sg_command_field_new(com);
				inf->fieldIndex = string.fieldIndex;
				inf->fieldType = string.fieldType;
				inf->field_ptr = gf_sg_vrml_field_pointer_new(string.fieldType);
				gf_list_add(au->commands, com);
			}
			assert(inf);
			gf_sg_vrml_mf_append(inf->field_ptr, GF_SG_VRML_MFSTRING, (void **) &sfstr);
			len = 0;
			for (i=0; i<strlen(ptr); i++) {
				/*FIXME - UTF8 support & BOMs !!*/
#if 0
				if (ptr[i] & 0x80) {
					szText[len] = 0xc0 | ( (ptr[i] >> 6) & 0x3 );
					len++;
					ptr[i] &= 0xbf;
				}
#endif
				szText[len] = ptr[i];
				len++;
			}
			szText[len] = 0;
			sfstr->buffer = strdup(szText);
			break;
		}
	}

exit:
	if (e) gf_sm_stream_del(ctx, srt);
	fclose(srt_in);
	return e;
}

static GF_Err gf_text_import_sub_bifs(GF_SceneManager *ctx, GF_ESD *src, GF_MuxInfo *mux)
{
	GF_Err e;
	GF_Node *text, *font;
	GF_StreamContext *srt;
	FILE *sub_in;
	GF_FieldInfo string, style;
	u32 start, end, line, i, j, len;
	GF_AUContext *au;
	GF_Command *com;
	SFString *sfstr;
	GF_CommandField *inf;
	Bool first_samp;
	Double fps;
	char szLine[2048], szTime[30], szText[2048];
	GF_StreamContext *sc = NULL;

	if (!ctx->scene_graph) {
		fprintf(stdout, "Error importing SUB: base scene not assigned\n");
		return GF_BAD_PARAM;
	}
	for (i=0; i<gf_list_count(ctx->streams); i++) {
		sc = gf_list_get(ctx->streams, i);
		if (sc->streamType==GF_STREAM_SCENE) break;
		sc = NULL;
	}

	if (!sc) {
		fprintf(stdout, "Error importing SUB: Cannot locate base scene\n");
		return GF_BAD_PARAM;
	}
	if (!mux->textNode) {
		fprintf(stdout, "Error importing SUB: Target text node unspecified\n");
		return GF_BAD_PARAM;
	}
	text = gf_sg_find_node_by_name(ctx->scene_graph, mux->textNode);
	if (!text) {
		fprintf(stdout, "Error importing SUB: Cannot find target text node %s\n", mux->textNode);
		return GF_BAD_PARAM;
	}
	if (gf_node_get_field_by_name(text, "string", &string) != GF_OK) {
		fprintf(stdout, "Error importing SUB: Target text node %s doesn't look like text\n", mux->textNode);
		return GF_BAD_PARAM;
	}

	font = NULL;
	if (mux->fontNode) {
		font = gf_sg_find_node_by_name(ctx->scene_graph, mux->fontNode);
		if (!font) {
			fprintf(stdout, "Error importing SUB: Cannot find target font node %s\n", mux->fontNode);
			return GF_BAD_PARAM;
		}
		if (gf_node_get_field_by_name(font, "style", &style) != GF_OK) {
			fprintf(stdout, "Error importing SUB: Target font node %s doesn't look like font\n", mux->fontNode);
			return GF_BAD_PARAM;
		}
	}

	sub_in = fopen(mux->file_name, "rt");
	if (!sub_in) {
		fprintf(stdout, "Cannot open input SUB %s\n", mux->file_name);
		return GF_URL_ERROR;
	}

	srt = gf_sm_stream_new(ctx, src->ESID, GF_STREAM_SCENE, 1);
	if (!srt) return GF_OUT_OF_MEM;

	if (!src->slConfig) src->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	src->slConfig->timestampResolution = 1000;
	if (!src->decoderConfig) src->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	src->decoderConfig->streamType = GF_STREAM_SCENE;
	src->decoderConfig->objectTypeIndication = 1;

	e = GF_OK;
	start = end = 0;
	au = NULL;
	com = NULL;
	inf = NULL;

	fps = 25.0;
	if (mux->frame_rate) fps = mux->frame_rate;

	line = 0;
	first_samp = 1;
	while (1) {
		char *sOK = fgets(szLine, 2048, sub_in);
		if (!sOK) break;
		REM_TRAIL_MARKS(szLine, "\r\n\t ")

		line++;
		len = strlen(szLine); 
		if (!len) continue;

		i=0;
		if (szLine[i] != '{') {
			fprintf(stdout, "Bad SUB file (line %d): expecting \"{\" got \"%c\"\n", line, szLine[i]);
			e = GF_NON_COMPLIANT_BITSTREAM;
			break;
		}
		while (szLine[i+1] && szLine[i+1]!='}') { szTime[i] = szLine[i+1]; i++; }
		szTime[i] = 0;
		start = atoi(szTime);
		if (start<end) {
			fprintf(stdout, "WARNING: corrupted SUB frame (line %d) - starts (at %d ms) before end of previous one (%d ms) - adjusting time stamps\n", line, start, end);
			start = end;
		}
		j=i+2;
		i=0;
		if (szLine[i+j] != '{') {
			fprintf(stdout, "Bad SUB file - expecting \"{\" got \"%c\"\n", szLine[i]);
			e = GF_NON_COMPLIANT_BITSTREAM;
			break;
		}
		while (szLine[i+1+j] && szLine[i+1+j]!='}') { szTime[i] = szLine[i+1+j]; i++; }
		szTime[i] = 0;
		end = atoi(szTime);
		j+=i+2;

		if (start>end) {
			fprintf(stdout, "WARNING: corrupted SUB frame (line %d) - ends (at %d ms) before start of current frame (%d ms) - skipping\n", line, end, start);
			continue;
		}

		if (start && first_samp) {
			au = gf_sm_stream_au_new(srt, 0, 0, 1);
			com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
			com->node = text;
			gf_node_register(text, NULL);
			inf = gf_sg_command_field_new(com);
			inf->fieldIndex = string.fieldIndex;
			inf->fieldType = string.fieldType;
			inf->field_ptr = gf_sg_vrml_field_pointer_new(string.fieldType);
			gf_list_add(au->commands, com);
		}

		for (i=j; i<len; i++) {
			if (szLine[i]=='|') {
				szText[i-j] = '\n';
			} else {
				szText[i-j] = szLine[i];
			}
		}
		szText[i-j] = 0;

		com = gf_sg_command_new(ctx->scene_graph, GF_SG_FIELD_REPLACE);
		com->node = text;
		gf_node_register(text, NULL);
		inf = gf_sg_command_field_new(com);
		inf->fieldIndex = string.fieldIndex;
		inf->fieldType = string.fieldType;
		inf->field_ptr = gf_sg_vrml_field_pointer_new(string.fieldType);
		gf_list_add(au->commands, com);

		gf_sg_vrml_mf_append(inf->field_ptr, GF_SG_VRML_MFSTRING, (void **) &sfstr);
		sfstr->buffer = strdup(szText);
	}

	if (e) gf_sm_stream_del(ctx, srt);
	fclose(sub_in);
	return e;
}

GF_Err gf_sm_import_bifs_subtitle(GF_SceneManager *ctx, GF_ESD *src, GF_MuxInfo *mux)
{
	GF_Err e;
	u32 fmt;
	e = gf_text_guess_format(mux->file_name, &fmt);
	if (e) return e;
	if (!fmt || (fmt>=3)) return GF_NOT_SUPPORTED;

	if (fmt==1) return gf_text_import_srt_bifs(ctx, src, mux);
	else return gf_text_import_sub_bifs(ctx, src, mux);
}


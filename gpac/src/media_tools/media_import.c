/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) Jean Le Feuvre 2000-2005
 *					All rights reserved
 *
 *  This file is part of GPAC / Media Tools sub-project
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



#include <gpac/internal/media_dev.h>
#include <gpac/internal/avilib.h>
#include <gpac/internal/ogg.h>
#include <gpac/xml.h>
#include <gpac/constants.h>

#ifndef GPAC_READ_ONLY

#define GF_IMPORT_DEFAULT_FPS	25.0


GF_Err gf_import_message(GF_MediaImporter *import, GF_Err e, char *format, ...)
{
	va_list args;
	va_start(args, format);
	if (import->import_message) {
		char szMsg[1024];
		vsprintf(szMsg, format, args);
		import->import_message(import, e, szMsg);
	} else {
		vfprintf(stdout,format,args);
		if (e) fprintf(stderr, " (%s)", gf_error_to_string(e));
		fprintf(stdout, "\n");
	}
	va_end(args);
	return e;
}

void gf_import_progress(GF_MediaImporter *import, u32 cur_samp, u32 count)
{
	if (import->import_progress) {
		import->import_progress(import, cur_samp, count);
	} else {
		gf_cbk_on_progress("Importing", cur_samp, count);
	}
}

static GF_Err gf_media_update_par(GF_ISOFile *file, u32 track)
{
	u32 tk_w, tk_h, stype;
	GF_Err e;

	e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
	if (e) return e;

	stype = gf_isom_get_media_subtype(file, track, 1);
	if (stype==GF_ISOM_SUBTYPE_AVC_H264) {
		s32 par_n, par_d;
		GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, 1);
		GF_AVCConfigSlot *slc = gf_list_get(avcc->sequenceParameterSets, 0);
		gf_avc_get_sps_info(slc->data, slc->size, NULL, NULL, &par_n, &par_d);

		if ((par_n>1) && (par_d>1)) 
			tk_w = tk_w * par_n / par_d;
	} 
	else if ((stype==GF_ISOM_SUBTYPE_MPEG4) || (stype==GF_ISOM_SUBTYPE_MPEG4_CRYP) ) {
		GF_M4VDecSpecInfo dsi;
		GF_ESD *esd = gf_isom_get_esd(file, track, 1);
		if (!esd || !esd->decoderConfig || (esd->decoderConfig->streamType!=4) || (esd->decoderConfig->objectTypeIndication!=0x20)) {
			if (esd) gf_odf_desc_del((GF_Descriptor *) esd);
			return GF_NOT_SUPPORTED;
		}
		gf_m4v_get_config(esd->decoderConfig->decoderSpecificInfo->data, esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
		if (esd) gf_odf_desc_del((GF_Descriptor *) esd);

		if ((dsi.par_num>1) && (dsi.par_num>1)) 
			tk_w = dsi.width * dsi.par_num / dsi.par_den;
	} else {
		return GF_OK;
	}
	return gf_isom_set_track_layout_info(file, track, tk_w<<16, tk_h<<16, 0, 0, 0);
}

static void MP4T_RecomputeBitRate(GF_ISOFile *file, u32 track)
{
	u32 i, count, timescale;
	u64 time_wnd, rate, max_rate, avg_rate;
	Double br;
	GF_ESD *esd;

	esd = gf_isom_get_esd(file, track, 1);
	if (!esd) return;

	esd->decoderConfig->avgBitrate = 0;
	esd->decoderConfig->maxBitrate = 0;
	rate = max_rate = avg_rate = time_wnd = 0;

	timescale = gf_isom_get_media_timescale(file, track);
	count = gf_isom_get_sample_count(file, track);
	for (i=0; i<count; i++) {
		GF_ISOSample *samp = gf_isom_get_sample_info(file, track, i+1, NULL, NULL);
		
		if (samp->dataLength>esd->decoderConfig->bufferSizeDB) esd->decoderConfig->bufferSizeDB = samp->dataLength;

		if (esd->decoderConfig->bufferSizeDB < samp->dataLength) esd->decoderConfig->bufferSizeDB = samp->dataLength;
		avg_rate += samp->dataLength;
		rate += samp->dataLength;
		if (samp->DTS > time_wnd + timescale) {
			if (rate > max_rate) max_rate = rate;
			time_wnd = samp->DTS;
			rate = 0;
		}

		gf_isom_sample_del(&samp);
	}
	
	br = (Double) (s64) gf_isom_get_media_duration(file, track);
	br /= timescale;
	esd->decoderConfig->avgBitrate = (u32) ((Double) (s64)avg_rate / br);
	/*move to bps*/
	esd->decoderConfig->avgBitrate *= 8;
	esd->decoderConfig->maxBitrate = (u32) (max_rate*8);

	gf_isom_change_mpeg4_description(file, track, 1, esd);
	gf_odf_desc_del((GF_Descriptor *)esd);
}

static void get_video_timing(Double fps, u32 *timescale, u32 *dts_inc)
{
	u32 fps_1000 = (u32) (fps*1000 + 0.5);
	/*handle all drop-frame formats*/
	if (fps_1000==29970) {
		*timescale = 30000;
		*dts_inc = 1001;
	}
	else if (fps_1000==23976) {
		*timescale = 24000;
		*dts_inc = 1001;
	}
	else if (fps_1000==59940) {
		*timescale = 60000;
		*dts_inc = 1001;
	} else {
		*timescale = fps_1000;
		*dts_inc = 1000;
	}
}



static GF_Err gf_import_still_image(GF_MediaImporter *import)
{
	GF_BitStream *bs;
	GF_Err e;
	Bool destroy_esd;
	u32 size, track, di, w, h;
	GF_ISOSample *samp;
	u8 OTI;
	unsigned char *data;
	FILE *src;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_VISUAL;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_NO_DURATION;
		import->nb_tracks = 1;
		return GF_OK;
	}

	src = fopen(import->in_name, "rb");
	if (!src) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	fseek(src, 0, SEEK_END);
	size = ftell(src);
	fseek(src, 0, SEEK_SET);
	data = malloc(sizeof(unsigned char)*size);
	fread(data, sizeof(unsigned char)*size, 1, src);
	fclose(src);

	/*get image size*/
	bs = gf_bs_new(data, size, GF_BITSTREAM_READ);
	gf_img_parse(bs, &OTI, &w, &h);
	gf_bs_del(bs);

	if (!w || !h) {
		free(data);
		return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Invalid %s file", (OTI==0x6C) ? "JPEG" : "PNG");
	}

	e = GF_OK;
	destroy_esd = 0;
	if (!import->esd) {
		import->esd = gf_odf_desc_esd_new(2);
		destroy_esd = 1;
	}
	/*update stream type/oti*/
	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->decoderConfig->streamType = GF_STREAM_VISUAL;
	import->esd->decoderConfig->objectTypeIndication = OTI;
	import->esd->decoderConfig->bufferSizeDB = size;
	import->esd->decoderConfig->avgBitrate = 8*size;
	import->esd->decoderConfig->maxBitrate = 8*size;
	import->esd->slConfig->timestampResolution = 1000;
	
	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_VISUAL, 1000);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;

	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	if (e) goto exit;
	gf_isom_set_visual_info(import->dest, track, di, w, h);
	samp = gf_isom_sample_new();
	samp->IsRAP = 1;
	samp->dataLength = size;

	gf_import_message(import, GF_OK, "%s import - size %d x %d", (OTI==0x6C) ? "JPEG" : "PNG", w, h);

	gf_import_progress(import, 0, 1);
	if (import->flags & GF_IMPORT_USE_DATAREF) {
		e = gf_isom_add_sample_reference(import->dest, track, di, samp, (u64) 0);
	} else {
		samp->data = data;
		e = gf_isom_add_sample(import->dest, track, di, samp);
		samp->data = NULL;
	}
	gf_import_progress(import, 1, 1);

	gf_isom_sample_del(&samp);

exit:
	free(data);
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	return e;
}


GF_Err gf_import_mp3(GF_MediaImporter *import)
{
	u8 oti;
	Bool destroy_esd;
	GF_Err e;
	u16 sr;
	u32 nb_chan;
	FILE *in;
	u32 hdr, size, max_size, track, di, tot_size, done, offset, duration;
	GF_ISOSample *samp;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_AUDIO;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF;
		import->nb_tracks = 1;
		return GF_OK;
	}

	in = fopen(import->in_name, "rb");
	if (!in) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	hdr = gf_mp3_get_next_header(in);
	if (!hdr) {
		fclose(in);
		return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Audio isn't MPEG-1/2 audio");
	}
	sr = gf_mp3_sampling_rate(hdr);
	oti = gf_mp3_object_type_indication(hdr);
	if (!oti) {
		fclose(in);
		return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Audio isn't MPEG-1/2 audio");
	}

	e = GF_OK;
	destroy_esd = 0;
	if (!import->esd) {
		import->esd = gf_odf_desc_esd_new(2);
		destroy_esd = 1;
	}
	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	/*update stream type/oti*/
	import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	import->esd->decoderConfig->objectTypeIndication = oti;
	import->esd->decoderConfig->bufferSizeDB = 20;
	import->esd->slConfig->timestampResolution = sr;

	samp = NULL;
	nb_chan = gf_mp3_num_channels(hdr);
	gf_import_message(import, GF_OK, "MP3 import - sample rate %d - %s audio - %d channel%s", sr, (oti==0x6B) ? "MPEG-1" : "MPEG-2", nb_chan, (nb_chan>1) ? "s" : "");

	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_AUDIO, sr);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;
	if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
	import->esd->decoderConfig->decoderSpecificInfo = NULL;
	gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	gf_isom_set_audio_info(import->dest, track, di, sr, nb_chan, 16);

	fseek(in, 0, SEEK_END);
	tot_size = ftell(in);
	fseek(in, 0, SEEK_SET);

	e = GF_OK;
	samp = gf_isom_sample_new();
	samp->IsRAP = 1;

	duration = import->duration*sr;
	duration /= 1000;

	max_size = 0;
	done = 0;
	while (tot_size > done) {
		/* get the next MP3 frame header */
		hdr = gf_mp3_get_next_header(in);
		/*MP3 stream truncated*/
		if (!hdr) break;

		offset = ftell(in) - 4;
		size = gf_mp3_frame_size(hdr);
		if (size>max_size) {
			samp->data = realloc(samp->data, sizeof(char) * size);
			max_size = size;
		}
		samp->data[0] = (hdr >> 24) & 0xFF;
		samp->data[1] = (hdr >> 16) & 0xFF;
		samp->data[2] = (hdr >> 8) & 0xFF;
		samp->data[3] = hdr & 0xFF;
		samp->dataLength = size;

		/* read the frame data into the buffer */
		if (fread(&samp->data[4], 1, size - 4, in) != size - 4) break;

		if (import->flags & GF_IMPORT_USE_DATAREF) {
			gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			gf_isom_add_sample(import->dest, track, di, samp);
		}

		gf_import_progress(import, done, tot_size);

		samp->DTS += gf_mp3_window_size(hdr);
		done += samp->dataLength;
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	MP4T_RecomputeBitRate(import->dest, track);
	gf_import_progress(import, tot_size, tot_size);

exit:
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	if (samp) gf_isom_sample_del(&samp);
	fclose(in);
	return e;
}

typedef struct
{
	Bool is_mp2, no_crc;
	u32 profile, sr_idx, nb_ch, frame_size;
} ADTSHeader;

static Bool ADTS_SyncFrame(GF_BitStream *bs, ADTSHeader *hdr)
{
	u32 val, hdr_size, pos;
	while (gf_bs_available(bs)) {
		val = gf_bs_read_u8(bs);
		if (val!=0xFF) continue;
		val = gf_bs_read_int(bs, 4);
		if (val != 0x0F) {
			gf_bs_read_int(bs, 4);
			continue;
		}
		hdr->is_mp2 = gf_bs_read_int(bs, 1);
		gf_bs_read_int(bs, 2);
		hdr->no_crc = gf_bs_read_int(bs, 1);
		pos = (u32) gf_bs_get_position(bs) - 2;

		hdr->profile = 1 + gf_bs_read_int(bs, 2);
		hdr->sr_idx = gf_bs_read_int(bs, 4);
		gf_bs_read_int(bs, 1);
		hdr->nb_ch = gf_bs_read_int(bs, 3);
		gf_bs_read_int(bs, 4);
		hdr->frame_size = gf_bs_read_int(bs, 13);
		gf_bs_read_int(bs, 11);
		gf_bs_read_int(bs, 2);
		hdr_size = hdr->no_crc ? 7 : 9;
		if (!hdr->no_crc) gf_bs_read_int(bs, 16);
		if (hdr->frame_size < hdr_size) {
			gf_bs_seek(bs, pos+1);
			continue;
		}
		hdr->frame_size -= hdr_size;
		if (gf_bs_available(bs) == hdr->frame_size) return 1;

		gf_bs_skip_bytes(bs, hdr->frame_size);
		val = gf_bs_read_u8(bs);
		if (val!=0xFF) {
			gf_bs_seek(bs, pos+1);
			continue;
		}
		val = gf_bs_read_int(bs, 4);
		if (val!=0x0F) {
			gf_bs_read_int(bs, 4);
			gf_bs_seek(bs, pos+2);
			continue;
		}
		gf_bs_seek(bs, pos+hdr_size);
		return 1;
	}
	return 0;
}

GF_Err gf_import_aac_adts(GF_MediaImporter *import)
{
	u8 oti;
	Bool destroy_esd;
	GF_Err e;
	Bool sync_frame;
	u16 sr, sbr_sr, sbr_sr_idx;
	GF_BitStream *bs, *dsi;
	ADTSHeader hdr;
	GF_M4ADecSpecInfo acfg;
	FILE *in;
	u64 offset;
	u32 max_size, track, di, tot_size, done, duration, prof, i;
	GF_ISOSample *samp;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_AUDIO;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_SBR_IMPLICIT | GF_IMPORT_SBR_EXPLICIT;
		import->nb_tracks = 1;
		return GF_OK;
	}

	in = fopen(import->in_name, "rb");
	if (!in) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	bs = gf_bs_from_file(in, GF_BITSTREAM_READ);

	sync_frame = ADTS_SyncFrame(bs, &hdr);
	if (!sync_frame) {
		gf_bs_del(bs);
		fclose(in);
		return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Audio isn't MPEG-2/4 AAC with ADTS");
	}
	/*keep MPEG-2 AAC OTI even for HE-SBR (that's correct according to latest MPEG-4 audio spec)*/
	oti = hdr.is_mp2 ? hdr.profile+0x66-1 : 0x40;
	sr = GF_M4ASampleRates[hdr.sr_idx];


	dsi = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);

	sbr_sr = sr;
	sbr_sr_idx = hdr.sr_idx;
	for (i=0; i<16; i++) {
		if (GF_M4ASampleRates[i] == (u32) 2*sr) {
			sbr_sr_idx = i;
			sbr_sr = 2*sr;
			break;
		}
	}
	/*no provision for explicit indication of MPEG-2 AAC through MPEG-4 PLs, so force implicit*/
	if (hdr.is_mp2) {
		if (import->flags & GF_IMPORT_SBR_EXPLICIT) {
			import->flags &= ~GF_IMPORT_SBR_EXPLICIT;
			import->flags |= GF_IMPORT_SBR_IMPLICIT;
		}
	}

	memset(&acfg, 0, sizeof(GF_M4ADecSpecInfo));
	acfg.base_object_type = hdr.profile;
	acfg.base_sr = sr;
	acfg.nb_chan = hdr.nb_ch;
	acfg.sbr_object_type = 0;
	if (import->flags & GF_IMPORT_SBR_EXPLICIT) {
		acfg.has_sbr = 1;
		acfg.base_object_type = 5;
		acfg.sbr_object_type = hdr.profile;
	} else if (import->flags & GF_IMPORT_SBR_IMPLICIT) {
		acfg.has_sbr = 1;
	}
	acfg.audioPL = gf_m4a_get_profile(&acfg);
	/*explicit SBR signal (non backward-compatible)*/
	if (import->flags & GF_IMPORT_SBR_EXPLICIT) {
		gf_bs_write_int(dsi, 5, 5);
		gf_bs_write_int(dsi, hdr.sr_idx, 4);
		gf_bs_write_int(dsi, hdr.nb_ch, 4);
		gf_bs_write_int(dsi, sbr_sr ? sbr_sr_idx : hdr.sr_idx, 4);
		gf_bs_write_int(dsi, hdr.profile, 5);
	} else {
		/*regular AAC*/
		gf_bs_write_int(dsi, hdr.profile, 5);
		gf_bs_write_int(dsi, hdr.sr_idx, 4);
		gf_bs_write_int(dsi, hdr.nb_ch, 4);
		gf_bs_align(dsi);
		/*implicit AAC SBR signal*/
		if (import->flags & GF_IMPORT_SBR_IMPLICIT) {
			gf_bs_write_int(dsi, 0x2b7, 11); /*sync extension type*/
			gf_bs_write_int(dsi, 5, 5);	/*SBR objectType*/
			gf_bs_write_int(dsi, 1, 1);	/*SBR present flag*/
			gf_bs_write_int(dsi, sbr_sr_idx, 4);
		}
	}
	/*not MPEG4 tool*/
	if (0 && hdr.is_mp2) acfg.audioPL = 0xFE;

	gf_bs_align(dsi);
	prof = hdr.profile;

	e = GF_OK;
	destroy_esd = 0;
	if (!import->esd) {
		import->esd = gf_odf_desc_esd_new(2);
		destroy_esd = 1;
	}
	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	import->esd->decoderConfig->objectTypeIndication = oti;
	import->esd->decoderConfig->bufferSizeDB = 20;
	import->esd->slConfig->timestampResolution = sr;
	if (!import->esd->decoderConfig->decoderSpecificInfo) import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
	if (import->esd->decoderConfig->decoderSpecificInfo->data) free(import->esd->decoderConfig->decoderSpecificInfo->data);
	gf_bs_get_content(dsi, (unsigned char **) &import->esd->decoderConfig->decoderSpecificInfo->data, &import->esd->decoderConfig->decoderSpecificInfo->dataLength);
	gf_bs_del(dsi);

	samp = NULL;
	gf_import_message(import, GF_OK, "AAC import %s- sample rate %d - %s audio - %d channel%s", (import->flags & GF_IMPORT_SBR_IMPLICIT) ? "SBR (implicit) " : ((import->flags & GF_IMPORT_SBR_EXPLICIT) ? "SBR (explicit) " : ""), sr, (oti==0x40) ? "MPEG-4" : "MPEG-2", hdr.nb_ch, (hdr.nb_ch>1) ? "s" : "");

	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_AUDIO, sr);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;
	gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	gf_isom_set_audio_info(import->dest, track, di, sr, (hdr.nb_ch>1) ? 2 : 1, 16);

	e = GF_OK;
	/*add first sample*/
	samp = gf_isom_sample_new();
	samp->IsRAP = 1;
	max_size = samp->dataLength = hdr.frame_size;
	samp->data = malloc(sizeof(char)*hdr.frame_size);
	offset = gf_bs_get_position(bs);
	gf_bs_read_data(bs, samp->data, hdr.frame_size);

	if (import->flags & GF_IMPORT_USE_DATAREF) {
		gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
	} else {
		gf_isom_add_sample(import->dest, track, di, samp);
	}
	samp->DTS+=1024;

	duration = import->duration*sr;
	duration /= 1000;

	tot_size = (u32) gf_bs_get_size(bs);
	done = 0;
	while (gf_bs_available(bs) ) {
		sync_frame = ADTS_SyncFrame(bs, &hdr);
		if (!sync_frame) break;
		if (hdr.frame_size>max_size) {
			samp->data = realloc(samp->data, sizeof(char) * hdr.frame_size);
			max_size = hdr.frame_size;
		}
		samp->dataLength = hdr.frame_size;

		offset = gf_bs_get_position(bs);
		gf_bs_read_data(bs, samp->data, hdr.frame_size);
		if (import->flags & GF_IMPORT_USE_DATAREF) {
			gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			gf_isom_add_sample(import->dest, track, di, samp);
		}

		gf_import_progress(import, done, tot_size);
		samp->DTS += 1024;
		done += samp->dataLength;
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	MP4T_RecomputeBitRate(import->dest, track);
	gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_AUDIO, acfg.audioPL);
	gf_import_progress(import, tot_size, tot_size);

exit:
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	if (samp) gf_isom_sample_del(&samp);
	gf_bs_del(bs);
	fclose(in);
	return e;
}

GF_M4VParser *M4V_NewParserBitStream(GF_BitStream *bs);

GF_Err gf_import_cmp(GF_MediaImporter *import)
{
	GF_Err e;
	Double FPS;
	FILE *mdia;
	GF_ISOSample *samp;
	u32 nb_samp, i, timescale, max_size, samp_offset, track, di, PL, max_b, nbI, nbP, nbB, nbNotCoded, tot_size, done_size, dts_inc, ref_frame, b_frames;
	Bool is_vfr, enable_vfr, erase_pl, has_cts_offset, is_packed, destroy_esd, do_vfr, forced_packed;
	GF_M4VDecSpecInfo dsi;
	GF_M4VParser *vparse;
	GF_BitStream *bs;
	u64 pos;
	u32 duration;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_VISUAL;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_NO_FRAME_DROP | GF_IMPORT_OVERRIDE_FPS | GF_IMPORT_FORCE_PACKED;
		import->nb_tracks = 1;
		return GF_OK;
	}

	mdia = fopen(import->in_name, "rb");
	if (!mdia) return gf_import_message(import, GF_URL_ERROR, "Opening %s failed", import->in_name);
	bs = gf_bs_from_file(mdia, GF_BITSTREAM_READ);

	tot_size = (u32) gf_bs_get_size(bs);
	done_size = 0;
	destroy_esd = 0;
	FPS = GF_IMPORT_DEFAULT_FPS;
	if (import->video_fps) FPS = (Double) import->video_fps;
	get_video_timing(FPS, &timescale, &dts_inc);

	duration = (u32) (import->duration*FPS);

	is_packed = 0;
	nbNotCoded = nbI = nbP = nbB = max_b = 0;
	enable_vfr = is_vfr = erase_pl = 0;
	samp = gf_isom_sample_new();
	max_size = 4096;
	samp->data = malloc(sizeof(char)*max_size);

	vparse = M4V_NewParserBitStream(bs);
	e = gf_m4v_parse_config(vparse, &dsi);
	if (e) {
		gf_import_message(import, e, "Cannot load MPEG-4 decoder config");
		goto exit;
	}
	PL = dsi.VideoPL;
	if (!PL) {
		PL = 0x01;
		erase_pl = 1;
	}
	samp_offset = gf_m4v_get_object_start(vparse);
	if (!import->esd) {
		import->esd = gf_odf_desc_esd_new(0);
		destroy_esd = 1;
	}
	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_VISUAL, timescale);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->slConfig->timestampResolution = timescale;
	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
	import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
	import->esd->decoderConfig->streamType = GF_STREAM_VISUAL;
	import->esd->decoderConfig->objectTypeIndication = 0x20;
	import->esd->decoderConfig->decoderSpecificInfo->data = malloc(sizeof(char) * samp_offset);
	import->esd->decoderConfig->decoderSpecificInfo->dataLength = samp_offset;
	pos = gf_bs_get_position(bs);
	gf_bs_seek(bs, 0);
	gf_bs_read_data(bs, import->esd->decoderConfig->decoderSpecificInfo->data, samp_offset);
	gf_bs_seek(bs, pos);

	/*remove packed flag if any (VOSH user data)*/
	forced_packed = 0;
	i=0;
	while (1) {
		char *frame = import->esd->decoderConfig->decoderSpecificInfo->data;
		while ((i+3<samp_offset)  && ((frame[i]!=0) || (frame[i+1]!=0) || (frame[i+2]!=1))) i++;
		if (i+4>=samp_offset) break;
		if (strncmp(frame+i+4, "DivX", 4)) {
			i += 4;
			continue;
		}
		frame = import->esd->decoderConfig->decoderSpecificInfo->data + i + 4;
		frame = strchr(frame, 'p');
		if (frame) {
			forced_packed = 1;
			frame[0] = 'n';
		}
		break;
	}

	if (import->flags & GF_IMPORT_FORCE_PACKED) forced_packed = 1;

	gf_isom_set_cts_packing(import->dest, track, 1);

	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name: NULL, NULL, &di);
	if (e) goto exit;
	gf_isom_set_visual_info(import->dest, track, di, dsi.width, dsi.height);
	gf_import_message(import, GF_OK, "MPEG-4 Video import - %d x %d @ %02.4f FPS\nIndicated Profile: %s", dsi.width, dsi.height, FPS, gf_m4v_get_profile_name((u8) PL));

	gf_media_update_par(import->dest, track);

	has_cts_offset = 0;
	nb_samp = b_frames = ref_frame = 0;
	do_vfr = !(import->flags & GF_IMPORT_NO_FRAME_DROP);

	while (gf_bs_available(bs)) {
		u8 ftype;
		u32 tinc, frame_start;
		Bool is_coded;
		pos = gf_m4v_get_object_start(vparse);
		e = gf_m4v_parse_frame(vparse, dsi, &ftype, &tinc, &samp->dataLength, &frame_start, &is_coded);
		if (e==GF_EOS) e = GF_OK;
		if (e) goto exit;

		if (!is_coded) {
			nbNotCoded ++;
			/*if prev is B and we're parsing a packed bitstream discard n-vop*/
			if (forced_packed && b_frames) {
				is_packed = 1;
				continue;
			}
			/*policy is to import at variable frame rate, skip*/
			if (do_vfr) {
				is_vfr = 1;
				samp->DTS += dts_inc;
				continue;
			}
			/*policy is to keep non coded frame (constant frame rate), add*/
		} 
		samp->IsRAP = 0;

		if (ftype==2) {
			b_frames++;
			nbB++;
			/*adjust CTS*/
			if (!has_cts_offset) {
				u32 i;
				for (i=0; i<gf_isom_get_sample_count(import->dest, track); i++) {
					gf_isom_modify_cts_offset(import->dest, track, i+1, dts_inc);
				}
				has_cts_offset = 1;
			}
		} else {
			if (ftype==0) {
				samp->IsRAP = 1;
				nbI++;
			} else {
				nbP++;
			}
			/*even if no out-of-order frames we MUST adjust CTS if cts_offset is present is */
			if (ref_frame && has_cts_offset) 
				gf_isom_modify_cts_offset(import->dest, track, ref_frame, (1+b_frames)*dts_inc);

			ref_frame = nb_samp+1;
			if (max_b<b_frames) max_b = b_frames;
			b_frames = 0;
		}
		if (!nb_samp) samp->DTS = 0;

		if (import->flags & GF_IMPORT_USE_DATAREF) {
			samp->data = NULL;
			e = gf_isom_add_sample_reference(import->dest, track, di, samp, frame_start);
		} else {
			if (samp->dataLength>max_size) {
				max_size = samp->dataLength;
				samp->data = realloc(samp->data, sizeof(char)*max_size);
			}
			gf_bs_seek(bs, frame_start);
			gf_bs_read_data(bs, samp->data, samp->dataLength);
			e = gf_isom_add_sample(import->dest, track, di, samp);
		}
		samp->DTS += dts_inc;
		nb_samp++;
		gf_import_progress(import, done_size, tot_size);
		done_size += samp->dataLength;
		if (e) break;
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;

	}
	/*final flush*/
	if (ref_frame && has_cts_offset) 
		gf_isom_modify_cts_offset(import->dest, track, ref_frame, (1+b_frames)*dts_inc);

	gf_import_progress(import, nb_samp, nb_samp);
	if (has_cts_offset) {
		gf_import_message(import, GF_OK, "Has B-Frames (%d max consecutive B-VOPs)", max_b);
		gf_isom_set_cts_packing(import->dest, track, 0);
		/*this is plain ugly but since some encoders (divx) don't use the video PL correctly 
		we force the system video_pl to ASP@L5 since we have I, P, B in base layer*/
		if (PL<=3) {
			PL = 0xF5;
			erase_pl = 1;
			gf_import_message(import, GF_OK, "WARNING: indicated profile doesn't include B-VOPs - forcing %s", gf_m4v_get_profile_name((u8) PL));
		}
		gf_import_message(import, GF_OK, "Import results: %d VOPs (%d Is - %d Ps - %d Bs)", nb_samp, nbI, nbP, nbB);
	} else {
		/*no B-frames, remove CTS offsets*/
		gf_isom_remove_cts_info(import->dest, track);
		gf_import_message(import, GF_OK, "Import results: %d VOPs (%d Is - %d Ps)", nb_samp, nbI, nbP);
	}
	if (erase_pl) {
		gf_m4v_rewrite_pl((unsigned char **) &import->esd->decoderConfig->decoderSpecificInfo->data, &import->esd->decoderConfig->decoderSpecificInfo->dataLength, (u8) PL);
		gf_isom_change_mpeg4_description(import->dest, track, 1, import->esd);
	}
	MP4T_RecomputeBitRate(import->dest, track);

	if (is_vfr) {
		if (!nbB) {
			if (do_vfr) {
				gf_import_message(import, GF_OK, "import using Variable Frame Rate - Removed %d N-VOPs", nbNotCoded);
			} else {
				if (nbNotCoded) gf_import_message(import, GF_OK, "Stream has %d N-VOPs", nbNotCoded);
			}
			nbNotCoded = 0;
		}
	}

	if (nbNotCoded) gf_import_message(import, GF_OK, "Removed %d N-VOPs%s", nbNotCoded,is_packed ? " (Packed Bitstream)" : "");
	gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_VISUAL, (u8) PL);

exit:
	gf_isom_sample_del(&samp);
	if (destroy_esd) gf_odf_desc_del((GF_Descriptor *) import->esd);
	gf_m4v_parser_del(vparse);
	fclose(mdia);
	return e;
}

GF_Err gf_import_avi_video(GF_MediaImporter *import)
{
	GF_Err e;
	Double FPS;
	FILE *test;
	GF_ISOSample *samp;
	u32 i, num_samples, timescale, size, max_size, samp_offset, track, di, PL, max_b, nb_f, ref_frame, b_frames;
	u32 nbI, nbP, nbB, nbDummy, nbNotCoded, dts_inc, cur_samp;
	Bool is_vfr, enable_vfr, erase_pl;
	GF_M4VDecSpecInfo dsi;
	GF_M4VParser *vparse;
	s32 key;
	u32 duration;
	Bool destroy_esd, is_packed, is_init, has_cts_offset;
	char *comp, *frame;
	avi_t *in;

	if (import->trackID>1) return GF_OK;

	test = fopen(import->in_name, "rb");
	if (!test) return gf_import_message(import, GF_URL_ERROR, "Opening %s failed", import->in_name);
	fclose(test);
	in = AVI_open_input_file(import->in_name, 1);
	if (!in) return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Unsupported avi file");

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		char *comp;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_VISUAL;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_NO_FRAME_DROP | GF_IMPORT_OVERRIDE_FPS;
		import->tk_info[0].FPS = AVI_frame_rate(in);
		import->tk_info[0].width = AVI_video_width(in);
		import->tk_info[0].height = AVI_video_height(in);
		comp = AVI_video_compressor(in);
		import->tk_info[0].media_type = GF_FOUR_CHAR_INT(comp[0], comp[1], comp[2], comp[3]);

		import->nb_tracks = 1;
		for (i=0; i<(u32) AVI_audio_tracks(in); i++) {
			import->tk_info[i+1].track_num = i+2;
			import->tk_info[i+1].type = GF_ISOM_MEDIA_AUDIO;
			import->tk_info[i+1].flags = GF_IMPORT_USE_DATAREF;
			import->nb_tracks ++;
		}
		AVI_close(in);
		return GF_OK;
	}

	destroy_esd = 0;
	frame = NULL;
	AVI_seek_start(in);

	erase_pl = 0;
	comp = AVI_video_compressor(in);
	if (!comp) {
		e = GF_NOT_SUPPORTED;
		goto exit;
	}

	/*these are/should be OK*/
	if (!stricmp(comp, "DIVX") || !stricmp(comp, "DX50")	/*DivX*/
		|| !stricmp(comp, "XVID") /*XviD*/
		|| !stricmp(comp, "3iv2") /*3ivX*/
		|| !stricmp(comp, "fvfw") /*ffmpeg*/
		|| !stricmp(comp, "NDIG") /*nero*/
		|| !stricmp(comp, "MP4V") /*!! not tested*/
		|| !stricmp(comp, "M4CC") /*Divio - not tested*/
		|| !stricmp(comp, "PVMM") /*PacketVideo - not tested*/
		|| !stricmp(comp, "SEDG") /*Samsung - not tested*/
		|| !stricmp(comp, "RMP4") /*Sigma - not tested*/
		) {
		e = GF_OK;
	}
	else if (!stricmp(comp, "DIV3") || !stricmp(comp, "DIV4")) {
		gf_import_message(import, GF_NOT_SUPPORTED, "Video format %s not compliant with MPEG-4 Visual - please recompress the file first", comp);
		e = GF_NOT_SUPPORTED;
		goto exit;
	} else if (!stricmp(comp, "H264") || !stricmp(comp, "X264")) {
		gf_import_message(import, GF_NOT_SUPPORTED, "H264/AVC Video format not supported in AVI - please extract to raw format first", comp);
		e = GF_NOT_SUPPORTED;
		goto exit;
	} else {
		gf_import_message(import, GF_NOT_SUPPORTED, "Video format %s not supported - recompress the file first", comp);
		e = GF_NOT_SUPPORTED;
		goto exit;
	}

	FPS = AVI_frame_rate(in);
	if (import->video_fps) FPS = (Double) import->video_fps;
	get_video_timing(FPS, &timescale, &dts_inc);
	duration = (u32) (import->duration*FPS);

	e = GF_OK;
	max_size = 0;
	samp_offset = 0;
	frame = NULL;
	num_samples = AVI_video_frames(in);
	samp = gf_isom_sample_new();
	PL = 0;
	track = 0;
	is_vfr = 0;

	is_packed = 0;
	nbDummy = nbNotCoded = nbI = nbP = nbB = max_b = 0;
	enable_vfr = 0;
	has_cts_offset = 0;
	cur_samp = b_frames = ref_frame = 0;

	is_init = 0;

	for (i=0; i<num_samples; i++) {
		size = AVI_frame_size(in, i);
		if (!size) {
			AVI_read_frame(in, NULL, &key);
			continue;
		}

		if (size > max_size) {
			frame = realloc(frame, sizeof(char) * size);
			max_size = size;
		}
		AVI_read_frame(in, frame, &key);

		/*get DSI*/
		if (!is_init) {
			is_init = 1;
			vparse = gf_m4v_parser_new(frame, size);
			e = gf_m4v_parse_config(vparse, &dsi);
			PL = dsi.VideoPL;
			if (!PL) {
				PL = 0x01;
				erase_pl = 1;
			}
			samp_offset = gf_m4v_get_object_start(vparse);
			gf_m4v_parser_del(vparse);
			if (e) {
				gf_import_message(import, e, "Cannot import decoder config in first frame");
				goto exit;
			}

			if (!import->esd) {
				import->esd = gf_odf_desc_esd_new(0);
				destroy_esd = 1;
			}
			track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_VISUAL, timescale);
			if (!track) {
				e = gf_isom_last_error(import->dest);
				goto exit;
			}
			gf_isom_set_track_enabled(import->dest, track, 1);
			if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
			import->final_trackID = gf_isom_get_track_id(import->dest, track);
			
			if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
			import->esd->slConfig->timestampResolution = timescale;

			if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
			if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
			import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
			import->esd->decoderConfig->streamType = GF_STREAM_VISUAL;
			import->esd->decoderConfig->objectTypeIndication = 0x20;
			import->esd->decoderConfig->decoderSpecificInfo->data = malloc(sizeof(char) * samp_offset);
			memcpy(import->esd->decoderConfig->decoderSpecificInfo->data, frame, sizeof(char) * samp_offset);
			import->esd->decoderConfig->decoderSpecificInfo->dataLength = samp_offset;

			gf_isom_set_cts_packing(import->dest, track, 1);

			/*remove packed flag if any (VOSH user data)*/
			while (1) {
				char *divx_mark;
				while ((i+3<samp_offset)  && ((frame[i]!=0) || (frame[i+1]!=0) || (frame[i+2]!=1))) i++;
				if (i+4>=samp_offset) break;

				if (strncmp(frame+i+4, "DivX", 4)) {
					i += 4;
					continue;
				}
				divx_mark = import->esd->decoderConfig->decoderSpecificInfo->data + i + 4;
				divx_mark = strchr(divx_mark, 'p');
				if (divx_mark) divx_mark[0] = 'n';
				break;
			}
			i = 0;
			
			e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name: NULL, NULL, &di);
			if (e) goto exit;
			gf_isom_set_visual_info(import->dest, track, di, dsi.width, dsi.height);
			gf_import_message(import, GF_OK, "AVI %s video import - %d x %d @ %02.4f FPS - %d Frames\nIndicated Profile: %s", comp, dsi.width, dsi.height, FPS, num_samples, gf_m4v_get_profile_name((u8) PL));

			gf_media_update_par(import->dest, track);
		}


		if (size > samp_offset) {
			u8 ftype;
			u32 tinc, framesize, frame_start;
			u64 file_offset;
			Bool is_coded;

			size -= samp_offset;
			file_offset = (u64) AVI_get_video_position(in, i);

			vparse = gf_m4v_parser_new(frame + samp_offset, size);

			samp->dataLength = 0;
			/*removing padding frames*/
			if (size<4) {
				nbDummy ++;
				size = 0;
			}

			nb_f=0;
			while (size) {
				GF_Err e = gf_m4v_parse_frame(vparse, dsi, &ftype, &tinc, &framesize, &frame_start, &is_coded);
				if (e<0) goto exit;

				if (!is_coded) {
					if (!gf_m4v_is_valid_object_type(vparse)) gf_import_message(import, GF_OK, "WARNING: AVI frame %d doesn't look like MPEG-4 Visual", i+1);
					nbNotCoded ++;
					if (!is_packed) {
						is_vfr = 1;
						/*policy is to import at constant frame rate from AVI*/
						if (import->flags & GF_IMPORT_NO_FRAME_DROP) goto proceed;
						/*policy is to import at variable frame rate from AVI*/
						samp->DTS += dts_inc;
					}
					/*assume this is packed bitstream n-vop and discard it.*/
				} else {
proceed:
					if (e==GF_EOS) size = 0;
					else is_packed = 1;
					nb_f++;

					samp->IsRAP = 0;

					if (ftype==2) {
						b_frames ++;
						nbB++;
						/*adjust CTS*/
						if (!has_cts_offset) {
							u32 i;
							for (i=0; i<gf_isom_get_sample_count(import->dest, track); i++) {
								gf_isom_modify_cts_offset(import->dest, track, i+1, dts_inc);
							}
							has_cts_offset = 1;
						}
					} else {
						if (!ftype) {
							samp->IsRAP = 1;
							nbI++;
						} else {
							nbP++;
						}
						/*even if no out-of-order frames we MUST adjust CTS if cts_offset is present is */
						if (ref_frame && has_cts_offset) 
							gf_isom_modify_cts_offset(import->dest, track, ref_frame, (1+b_frames)*dts_inc);

						ref_frame = cur_samp+1;
						if (max_b<b_frames) max_b = b_frames;
						b_frames = 0;
					}
					/*frame_start indicates start of VOP (eg we always remove VOL from each I)*/
					samp->data = frame + samp_offset + frame_start;
					samp->dataLength = framesize;

					if (import->flags & GF_IMPORT_USE_DATAREF) {
						samp->data = NULL;
						e = gf_isom_add_sample_reference(import->dest, track, di, samp, file_offset + samp_offset + frame_start);
					} else {
						e = gf_isom_add_sample(import->dest, track, di, samp);
					}
					cur_samp++;
					samp->DTS += dts_inc;
					if (e) gf_import_message(import, GF_OK, "Error importing AVI frame %d", i+1);
				}
				if (!size || (size == framesize + frame_start)) break;
			}
			gf_m4v_parser_del(vparse);
			if (nb_f>2) gf_import_message(import, GF_OK, "Warning: more than 2 frames packed together");
		}
		samp_offset = 0;
		gf_import_progress(import, i, num_samples);
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) 
			break;
	}

	/*final flush*/
	if (ref_frame && has_cts_offset) 
		gf_isom_modify_cts_offset(import->dest, track, ref_frame, (1+b_frames)*dts_inc);

	gf_import_progress(import, num_samples, num_samples);


	num_samples = gf_isom_get_sample_count(import->dest, track);
	if (has_cts_offset) {
		gf_import_message(import, GF_OK, "Has B-Frames (%d max consecutive B-VOPs%s)", max_b, is_packed ? " - packed bitstream" : "");
		/*repack CTS tables and adjust offsets for B-frames*/
		gf_isom_set_cts_packing(import->dest, track, 0);
		/*this is plain ugly but since some encoders (divx) don't use the video PL correctly 
		we force the system video_pl to ASP@L5 since we have I, P, B in base layer*/
		if (PL<=3) {
			PL = 0xF5;
			erase_pl = 1;
			gf_import_message(import, GF_OK, "WARNING: indicated profile doesn't include B-VOPs - forcing %s", gf_m4v_get_profile_name((u8) PL));
		}
		gf_import_message(import, GF_OK, "Import results: %d VOPs (%d Is - %d Ps - %d Bs)", num_samples, nbI, nbP, nbB);
	} else {
		/*no B-frames, remove CTS offsets*/
		gf_isom_remove_cts_info(import->dest, track);
		gf_import_message(import, GF_OK, "Import results: %d VOPs (%d Is - %d Ps)", num_samples, nbI, nbP);
	}

	samp->data = NULL;
	gf_isom_sample_del(&samp);

	if (erase_pl) {
		gf_m4v_rewrite_pl((unsigned char **) &import->esd->decoderConfig->decoderSpecificInfo->data, &import->esd->decoderConfig->decoderSpecificInfo->dataLength, (u8) PL);
		gf_isom_change_mpeg4_description(import->dest, track, 1, import->esd);
	}
	MP4T_RecomputeBitRate(import->dest, track);

	if (is_vfr) {
		if (nbB) {
			if (is_packed) gf_import_message(import, GF_OK, "Warning: Mix of non-coded frames: packed bitstream and encoder skiped - unpredictable timing");
		} else {
			if (import->flags & GF_IMPORT_NO_FRAME_DROP) {
				if (nbNotCoded) gf_import_message(import, GF_OK, "Stream has %d N-VOPs", nbNotCoded);
			} else {
				gf_import_message(import, GF_OK, "import using Variable Frame Rate - Removed %d N-VOPs", nbNotCoded);
			}
			nbNotCoded = 0;
		}
	}

	if (nbDummy || nbNotCoded) gf_import_message(import, GF_OK, "Removed Frames: %d VFW delay frames - %d N-VOPs", nbDummy, nbNotCoded);
	gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_VISUAL, (u8) PL);

exit:
	if (destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	if (frame) free(frame);
	AVI_close(in);
	return e;
}




/*credits to CISCO MPEG4/IP for MP3 parsing*/
GF_Err gf_import_avi_audio(GF_MediaImporter *import)
{
	GF_Err e;
	FILE *test;
	u32 hdr, di, track, i, tot_size, duration;
	s64 offset;
	s32 size, max_size, done;
	u16 sampleRate;
	Double dur;
	Bool is_cbr;
	u8 oti;
	GF_ISOSample *samp;
	unsigned char *frame;
	Bool destroy_esd;
	s32 continuous;
	unsigned char temp[4];
	avi_t *in;

	if (import->flags & GF_IMPORT_PROBE_ONLY) return GF_OK;

	/*video only, ignore*/
	if (import->trackID==1) return GF_OK;

	test = fopen(import->in_name, "rb");
	if (!test) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);
	fclose(test);
	in = AVI_open_input_file(import->in_name, 1);
	if (!in) return gf_import_message(import, GF_NOT_SUPPORTED, "Unsupported avi file");

	AVI_seek_start(in);

	e = GF_OK;
	if (import->trackID)  AVI_set_audio_track(in, import->trackID-2);

	if (AVI_read_audio(in, (char *) temp, 4, &continuous) != 4) {
		AVI_close(in);
		return gf_import_message(import, GF_OK, "No audio track found");
	}
	hdr = GF_FOUR_CHAR_INT(temp[0], temp[1], temp[2], temp[3]);
	if ((hdr &0xFFE00000) != 0xFFE00000) {
		AVI_close(in);
		return gf_import_message(import, GF_NOT_SUPPORTED, "Unsupported AVI audio format");
	}

	sampleRate = gf_mp3_sampling_rate(hdr);
	oti = gf_mp3_object_type_indication(hdr);
	if (!oti || !sampleRate) {
		AVI_close(in);
		return gf_import_message(import, GF_NOT_SUPPORTED, "Error: invalid MPEG Audio track");
	}
		
	frame = NULL;
	destroy_esd = 0;
	if (!import->esd) {
		destroy_esd = 1;
		import->esd = gf_odf_desc_esd_new(0);
	}
	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_AUDIO, sampleRate);
	if (!track) goto exit;
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;

	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->slConfig->timestampResolution = sampleRate;
	if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
	import->esd->decoderConfig->decoderSpecificInfo = NULL;
	import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	import->esd->decoderConfig->objectTypeIndication = oti;
	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	if (e) goto exit;

	gf_import_message(import, GF_OK, "AVI Audio import - sample rate %d - %s audio - %d channel%s", sampleRate, (oti==0x6B) ? "MPEG-1" : "MPEG-2", gf_mp3_num_channels(hdr), (gf_mp3_num_channels(hdr)>1) ? "s" : "");

	AVI_seek_start(in);
	AVI_set_audio_position(in, 0);

	i = 0;
	tot_size = max_size = 0;
	while ((size = AVI_audio_size(in, i) )>0) {
		if (max_size<size) max_size=size;
		tot_size += size;
		i++;
	}

	frame = malloc(sizeof(char) * max_size);
	AVI_seek_start(in);
	AVI_set_audio_position(in, 0);

	dur = import->duration;
	dur *= sampleRate;
	dur /= 1000;
	duration = (u32) dur;

	samp = gf_isom_sample_new();
	done=max_size=0;
	is_cbr = 1;
	while (1) {
		if (AVI_read_audio(in, frame, 4, &continuous) != 4) break;
		offset = gf_f64_tell(in->fdes) - 4;
		hdr = GF_FOUR_CHAR_INT(frame[0], frame[1], frame[2], frame[3]);

		size = gf_mp3_frame_size(hdr);
		if (size>max_size) {
			frame = realloc(frame, sizeof(char) * size);
			if (max_size) is_cbr = 0;
			max_size = size;
		}
		size = 4 + AVI_read_audio(in, (char*) &frame[4], size - 4, &continuous);

		if ((import->flags & GF_IMPORT_USE_DATAREF) && !continuous) {
			gf_import_message(import, GF_IO_ERR, "Cannot use media references, splited input audio frame found");
			e = GF_IO_ERR;
			goto exit;
		}
		samp->IsRAP = 1;
		samp->data = frame;
		samp->dataLength = size;
		if (import->flags & GF_IMPORT_USE_DATAREF) {
			e = gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			e = gf_isom_add_sample(import->dest, track, di, samp);
		}

		samp->DTS += gf_mp3_window_size(hdr);
		gf_import_progress(import, done, tot_size);

		done += size;
		if (duration && (samp->DTS > duration) ) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	
	gf_import_progress(import, tot_size, tot_size);

	gf_import_message(import, GF_OK, "Import done - %s bit rate MP3 detected", is_cbr ? "constant" : "variable");
	samp->data = NULL;
	gf_isom_sample_del(&samp);

	MP4T_RecomputeBitRate(import->dest, track);

	gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_AUDIO, 0xFE);

	
exit:
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	if (frame) free(frame);
	AVI_close(in);
	return e;
}

GF_Err gf_import_isomedia(GF_MediaImporter *import)
{
	GF_Err e;
	u64 offset, sampDTS;
	u32 track, di, trackID, track_in, i, num_samples, mtype, stype, w, h, duration, sr, sbr_sr, ch, mstype;
	u8 bps;
	char lang[4];
	const char *url, *urn;
	Bool sbr, is_clone;
	GF_ISOSample *samp;
	GF_ESD *origin_esd;
	GF_InitialObjectDescriptor *iod;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		for (i=0; i<gf_isom_get_track_count(import->orig); i++) {
			import->tk_info[i].track_num = gf_isom_get_track_id(import->orig, i+1);
			import->tk_info[i].type = gf_isom_get_media_type(import->orig, i+1);
			import->tk_info[i].flags = GF_IMPORT_USE_DATAREF;
			import->nb_tracks ++;
		}
		return GF_OK;
	}

	trackID = import->trackID;
	if (!trackID) {
		if (gf_isom_get_track_count(import->orig) != 1) return gf_import_message(import, GF_BAD_PARAM, "Several tracks in MP4 - please indicate track to import");
		trackID = gf_isom_get_track_id(import->orig, 1);
	}
	track_in = gf_isom_get_track_by_id(import->orig, trackID);
	if (!track_in) return gf_import_message(import, GF_URL_ERROR, "Cannot find track ID %d in file", trackID);

	origin_esd = gf_isom_get_esd(import->orig, track_in, 1);

	e = GF_OK;
	if (import->esd && origin_esd) {
		origin_esd->OCRESID = import->esd->OCRESID;
		/*there may be other things to import...*/
	}
	sbr = 0;
	sbr_sr = 0;
	iod = (GF_InitialObjectDescriptor *) gf_isom_get_root_od(import->orig);
	if (iod && (iod->tag != GF_ODF_IOD_TAG)) {
		gf_odf_desc_del((GF_Descriptor *) iod);
		iod = NULL;
	}
	mtype = gf_isom_get_media_type(import->orig, track_in);
	if (mtype==GF_ISOM_MEDIA_VISUAL) {
		u8 PL = iod ? iod->visual_profileAndLevel : 0xFE;
		w = h = 0;
		gf_isom_get_visual_info(import->orig, track_in, 1, &w, &h);
		/*for MPEG-4 visual, always check size (don't trust input file)*/
		if (origin_esd && (origin_esd->decoderConfig->objectTypeIndication==0x20)) {
			GF_M4VDecSpecInfo dsi;
			gf_m4v_get_config(origin_esd->decoderConfig->decoderSpecificInfo->data, origin_esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
			w = dsi.width;
			h = dsi.height;
			PL = dsi.VideoPL;
		}
		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_VISUAL, PL);
	}
	else if (mtype==GF_ISOM_MEDIA_AUDIO) {
		u8 PL = iod ? iod->audio_profileAndLevel : 0xFE;
		bps = 16;
		sr = ch = sbr_sr = 0;
		sbr = 0;
		gf_isom_get_audio_info(import->orig, track_in, 1, &sr, &ch, &bps);
		if (origin_esd && (origin_esd->decoderConfig->objectTypeIndication==0x40)) {
			GF_M4ADecSpecInfo dsi;
			gf_m4a_get_config(origin_esd->decoderConfig->decoderSpecificInfo->data, origin_esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
			sr = dsi.base_sr;
			if (dsi.has_sbr) sbr_sr = dsi.sbr_sr;
			ch = dsi.nb_chan;
			PL = dsi.audioPL;
			sbr = dsi.has_sbr;
		}
		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_AUDIO, PL);
	} 

	gf_odf_desc_del((GF_Descriptor *) iod);
	
	/*check if MPEG-4 or not*/
	is_clone = 0;
	stype = gf_isom_get_media_subtype(import->orig, track_in, 1);
	if ((stype==GF_ISOM_SUBTYPE_MPEG4) || (stype==GF_ISOM_SUBTYPE_MPEG4_CRYP)) {
		track = gf_isom_new_track(import->dest, import->esd ? import->esd->ESID : 0, gf_isom_get_media_type(import->orig, track_in), gf_isom_get_media_timescale(import->orig, track_in));
		if (!track) {
			e = gf_isom_last_error(import->dest);
			goto exit;
		}
		gf_isom_set_track_enabled(import->dest, track, 1);
		if (import->esd && !import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
		/*setup data ref*/
		urn = url = NULL;
		if (import->flags & GF_IMPORT_USE_DATAREF) {
			url = gf_isom_get_filename(import->orig);
			if (!gf_isom_is_self_contained(import->orig, track_in, 1)) {
				e = gf_isom_get_data_reference(import->orig, track_in, 1, &url, &urn);
				if (e) goto exit;
			}
		}
		e = gf_isom_new_mpeg4_description(import->dest, track, origin_esd, (char *) url, (char *) urn, &di);
		if (e) goto exit;
		/*copy over language*/
		lang[3] = 0;
		gf_isom_get_media_language(import->orig, track_in, lang);
		gf_isom_set_media_language(import->dest, track, lang);

	} else {
		if (! (import->flags & GF_IMPORT_KEEP_ALL_TRACKS) ) {
			mstype = gf_isom_get_media_subtype(import->orig, track_in, 1);
			switch (mstype) {
			case GF_ISOM_SUBTYPE_MPEG4:
			case GF_ISOM_SUBTYPE_MPEG4_CRYP:
			case GF_ISOM_SUBTYPE_AVC_H264:
			case GF_ISOM_SUBTYPE_3GP_H263:
			case GF_ISOM_SUBTYPE_3GP_AMR:
			case GF_ISOM_SUBTYPE_3GP_AMR_WB:
			case GF_ISOM_SUBTYPE_3GP_EVRC:
			case GF_ISOM_SUBTYPE_3GP_QCELP:
			case GF_ISOM_SUBTYPE_3GP_SMV:
				break;
			default:
				switch (mtype) {
				case GF_ISOM_MEDIA_HINT:
				case GF_ISOM_MEDIA_TEXT:
					break;
				default:
					return gf_import_message(import, GF_OK, "IsoMedia import - skipping track ID %d (unknown type \'%s\')", trackID, gf_4cc_to_str(mstype));
				}
			}

		}
		e = gf_isom_clone_track(import->orig, track_in, import->dest, (import->flags & GF_IMPORT_USE_DATAREF), &track);
		is_clone = 1;
		di = 1;
		if (e) goto exit;
	}
	if (e) goto exit;
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	switch (mtype) {
	case GF_ISOM_MEDIA_VISUAL:
		if (!is_clone) {
			gf_isom_set_visual_info(import->dest, track, di, w, h);
			gf_media_update_par(import->dest, track);
		}
		gf_import_message(import, GF_OK, "IsoMedia import - track ID %d - Video (size %d x %d)", trackID, w, h);
		break;
	case GF_ISOM_MEDIA_AUDIO:
	{
		if (!is_clone) gf_isom_set_audio_info(import->dest, track, di, sr, (ch>1) ? 2 : 1, bps);
		if (sbr) {
			gf_import_message(import, GF_OK, "IsoMedia import - track ID %d - HE-AAC (SR %d - SBR-SR %d - %d channels)", trackID, sr, sbr_sr, ch);
		} else {
			gf_import_message(import, GF_OK, "IsoMedia import - track ID %d - Audio (SR %d - %d channels)", trackID, sr, ch);
		}
	}
		break;
	default:
	{
		char szT[5];
		mstype = gf_isom_get_mpeg4_subtype(import->orig, track_in, di);
		if (!mstype) mstype = gf_isom_get_media_subtype(import->orig, track_in, di);
		strcpy(szT, gf_4cc_to_str(mtype));
		gf_import_message(import, GF_OK, "IsoMedia import - track ID %d - media type \"%s:%s\"", 
			trackID, szT, gf_4cc_to_str(mstype));
	}
		break;
	}

	duration = (u32) (((Double)import->duration * gf_isom_get_media_timescale(import->orig, track_in)) / 1000);

	num_samples = gf_isom_get_sample_count(import->orig, track_in);
	for (i=0; i<num_samples; i++) {
		if (import->flags & GF_IMPORT_USE_DATAREF) {
			samp = gf_isom_get_sample_info(import->orig, track_in, i+1, &di, &offset);
			if (!samp) {
				e = gf_isom_last_error(import->orig);
				goto exit;
			}
			e = gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			samp = gf_isom_get_sample(import->orig, track_in, i+1, &di);
			e = gf_isom_add_sample(import->dest, track, di, samp);
		}
		sampDTS = samp->DTS;
		gf_isom_sample_del(&samp);
		gf_import_progress(import, i+1, num_samples);
		if (duration && (sampDTS > duration) ) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
		if (e) break;
	}

	if (import->esd) {
		if (!import->esd->slConfig) {
			import->esd->slConfig = origin_esd ? origin_esd->slConfig : NULL;
			if (origin_esd) origin_esd->slConfig = NULL;
		}
		if (!import->esd->decoderConfig) {
			import->esd->decoderConfig = origin_esd ? origin_esd->decoderConfig : NULL;
			if (origin_esd) origin_esd->decoderConfig = NULL;
		}
	}		

	MP4T_RecomputeBitRate(import->dest, track);

exit:
	if (origin_esd) gf_odf_desc_del((GF_Descriptor *) origin_esd);
	return e;
}

#include "mpeg2_ps.h"

GF_Err gf_import_mpeg_ps_video(GF_MediaImporter *import)
{
	GF_Err e;
	mpeg2ps_t *ps;
	Double FPS;
	char *buf;
	u8 ftype;
	u32 track, di, streamID, mtype, w, h, nb_streams, buf_len, frames, ref_frame, timescale, duration, file_size, dts_inc, last_pos;
	Bool destroy_esd;

	if (import->flags & GF_IMPORT_USE_DATAREF) 
		return gf_import_message(import, GF_NOT_SUPPORTED, "Cannot use data referencing with MPEG-1/2 files");

	ps = mpeg2ps_init(import->in_name);
	if (!ps) return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Failed to open MPEG file %s", import->in_name);

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		u32 i, nb_v_str;
		import->nb_tracks = 0;
		nb_v_str = nb_streams = mpeg2ps_get_video_stream_count(ps);
		for (i=0; i<nb_streams; i++) {
			import->tk_info[import->nb_tracks].track_num = i+1;
			import->tk_info[import->nb_tracks].type = GF_ISOM_MEDIA_VISUAL;
			import->tk_info[import->nb_tracks].flags = GF_IMPORT_OVERRIDE_FPS;

			import->tk_info[import->nb_tracks].FPS = mpeg2ps_get_video_stream_framerate(ps, i);
			import->tk_info[import->nb_tracks].width = mpeg2ps_get_video_stream_width(ps, i);
			import->tk_info[import->nb_tracks].height = mpeg2ps_get_video_stream_height(ps, i);

			import->tk_info[import->nb_tracks].media_type = GF_FOUR_CHAR_INT('M', 'P', 'G', '1');
			if (mpeg2ps_get_video_stream_type(ps, i) == MPEG_VIDEO_MPEG2) import->tk_info[import->nb_tracks].media_type ++;

			import->nb_tracks++;
		}
		nb_streams = mpeg2ps_get_audio_stream_count(ps);
		for (i=0; i<nb_streams; i++) {
			import->tk_info[import->nb_tracks].track_num = nb_v_str + i+1;
			import->tk_info[import->nb_tracks].type = GF_ISOM_MEDIA_AUDIO;
			import->nb_tracks ++;
		}
		mpeg2ps_close(ps);
		return GF_OK;
	}


	streamID = 0;
	nb_streams = mpeg2ps_get_video_stream_count(ps);
	if ((nb_streams>1) && !import->trackID) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_BAD_PARAM, "%d video tracks in MPEG file - please indicate track to import", nb_streams);
	}
	/*audio*/
	if (import->trackID>nb_streams) {
		mpeg2ps_close(ps);
		return GF_OK;
	}
	if (import->trackID) streamID = import->trackID - 1;

	if (streamID>=nb_streams) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_BAD_PARAM, "Desired video track not found in MPEG file (%d visual streams)", nb_streams);
	}
	w = mpeg2ps_get_video_stream_width(ps, streamID);
	h = mpeg2ps_get_video_stream_height(ps, streamID);
	mtype = (mpeg2ps_get_video_stream_type(ps, streamID) == MPEG_VIDEO_MPEG2) ? 0x61 : 0x6A;
	FPS = mpeg2ps_get_video_stream_framerate(ps, streamID);
	if (import->video_fps) FPS = (Double) import->video_fps;
	get_video_timing(FPS, &timescale, &dts_inc);

	duration = import->duration*timescale;
	duration /= 1000;

	destroy_esd = 0;
	if (!import->esd) {
		destroy_esd = 1;
		import->esd = gf_odf_desc_esd_new(0);
	}
	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_VISUAL, timescale);
	e = gf_isom_last_error(import->dest);
	if (!track) goto exit;
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;

	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->slConfig->timestampResolution = timescale;
	if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
	import->esd->decoderConfig->decoderSpecificInfo = NULL;
	import->esd->decoderConfig->streamType = GF_STREAM_VISUAL;
	import->esd->decoderConfig->objectTypeIndication = mtype;
	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, NULL, NULL, &di);
	if (e) goto exit;

	gf_import_message(import, GF_OK, "%s Video import - Resolution %d x %d @ %02.4f FPS", (mtype==0x6A) ? "MPEG-1" : "MPEG-2", w, h, FPS);
	gf_isom_set_visual_info(import->dest, track, di, w, h);

	gf_isom_set_cts_packing(import->dest, track, 1);

	file_size = (u32) mpeg2ps_get_ps_size(ps);
	last_pos = 0;
	frames = 1;
	ref_frame = 1;
	while (mpeg2ps_get_video_frame(ps, streamID, (u8 **) &buf, &buf_len, &ftype, TS_90000, NULL)) {
		GF_ISOSample *samp;
		if ((buf[buf_len - 4] == 0) && (buf[buf_len - 3] == 0) && (buf[buf_len - 2] == 1)) buf_len -= 4;
		samp = gf_isom_sample_new();
		samp->data = buf;
		samp->dataLength = buf_len;
		samp->DTS = dts_inc*(frames-1);
		samp->IsRAP = (ftype==1) ? 1 : 0;
		samp->CTS_Offset = 0;
		gf_isom_add_sample(import->dest, track, di, samp);
		samp->data = NULL;
		gf_isom_sample_del(&samp);

		last_pos = (u32) mpeg2ps_get_video_pos(ps, streamID);
		gf_import_progress(import, last_pos, file_size);

		if (ftype != 3) {
			gf_isom_modify_cts_offset(import->dest, track, ref_frame, (frames-ref_frame)*dts_inc);
			ref_frame = frames;
		}
		frames++;

		if (duration && (dts_inc*(frames-1) >= duration) ) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	gf_isom_set_cts_packing(import->dest, track, 0);
	if (last_pos!=file_size) gf_import_progress(import, frames, frames);

	MP4T_RecomputeBitRate(import->dest, track);

exit:
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	mpeg2ps_close(ps);
	return e;
}

GF_Err gf_import_mpeg_ps_audio(GF_MediaImporter *import)
{
	GF_Err e;
	mpeg2ps_t *ps;
	u8 *buf;
	u32 track, di, streamID, mtype, sr, nb_ch, nb_streams, buf_len, frames, hdr, duration, file_size, last_pos;
	Bool destroy_esd;
	GF_ISOSample *samp;

	if (import->flags & GF_IMPORT_PROBE_ONLY) return GF_OK;

	if (import->flags & GF_IMPORT_USE_DATAREF) 
		return gf_import_message(import, GF_NOT_SUPPORTED, "Cannot use data referencing with MPEG-1/2 files");

	ps = mpeg2ps_init(import->in_name);
	if (!ps) return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Failed to open MPEG file %s", import->in_name);


	streamID = 0;
	nb_streams = mpeg2ps_get_audio_stream_count(ps);
	if ((nb_streams>1) && !import->trackID) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_BAD_PARAM, "%d audio tracks in MPEG file - please indicate track to import", nb_streams);
	}

	if (import->trackID) {
		u32 nb_v = mpeg2ps_get_video_stream_count(ps);
		/*video*/
		if (import->trackID<=nb_v) {
			mpeg2ps_close(ps);
			return GF_OK;
		}
		streamID = import->trackID - 1 - nb_v;
	}

	if (streamID>=nb_streams) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_BAD_PARAM, "Desired audio track not found in MPEG file (%d audio streams)", nb_streams);
	}
	
	mtype = mpeg2ps_get_audio_stream_type(ps, streamID);
	if (mtype != MPEG_AUDIO_MPEG) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_NOT_SUPPORTED, "Audio format not supported in MP4");
	}

	if (mpeg2ps_get_audio_frame(ps, streamID, &buf, &buf_len, TS_90000, NULL, NULL) == 0) {
		mpeg2ps_close(ps);
		return gf_import_message(import, GF_IO_ERR, "Cannot fetch audio frame from MPEG file");
	}
    
	hdr = GF_FOUR_CHAR_INT(buf[0],buf[1],buf[2],buf[3]);
	mtype = gf_mp3_object_type_indication(hdr);
	sr = gf_mp3_sampling_rate(hdr);
	nb_ch = gf_mp3_num_channels(hdr);

	destroy_esd = 0;
	if (!import->esd) {
		destroy_esd = 1;
		import->esd = gf_odf_desc_esd_new(0);
	}
	track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_AUDIO, sr);
	e = gf_isom_last_error(import->dest);
	if (!track) goto exit;
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;

	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
	import->esd->slConfig->timestampResolution = sr;
	if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
	import->esd->decoderConfig->decoderSpecificInfo = NULL;
	import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	import->esd->decoderConfig->objectTypeIndication = mtype;
	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, NULL, NULL, &di);
	if (e) goto exit;

	gf_isom_set_audio_info(import->dest, track, di, sr, nb_ch, 16);
	gf_import_message(import, GF_OK, "%s Audio import - sample rate %d - %d channel%s", (mtype==0x6B) ? "MPEG-1" : "MPEG-2", sr, nb_ch, (nb_ch>1) ? "s" : "");


	duration = (u32) ((Double)import->duration/1000.0 * sr);

	samp = gf_isom_sample_new();
	samp->IsRAP = 1;
	samp->DTS = 0;

	file_size = (u32) mpeg2ps_get_ps_size(ps);
	last_pos = 0;
	frames = 0;
	do {
		samp->data = buf;
		samp->dataLength = buf_len;
		gf_isom_add_sample(import->dest, track, di, samp);
		samp->DTS += gf_mp3_window_size(hdr);
		last_pos = (u32) mpeg2ps_get_audio_pos(ps, streamID);
		gf_import_progress(import, last_pos, file_size);
		frames++;
		if (duration && (samp->DTS>=duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}  while (mpeg2ps_get_audio_frame(ps, streamID, &buf, &buf_len, TS_90000, NULL, NULL));

	samp->data = NULL;
	gf_isom_sample_del(&samp);
	if (last_pos!=file_size) gf_import_progress(import, frames, frames);
	MP4T_RecomputeBitRate(import->dest, track);

exit:
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	mpeg2ps_close(ps);
	return e;
}

/*FIXME - need LARGE FILE support in NHNT - add a new version*/
GF_Err gf_import_nhnt(GF_MediaImporter *import)
{
	GF_Err e;
	Bool destroy_esd;
	u32 track, di, mtype, max_size, duration, count, w, h;
	GF_ISOSample *samp;
	s64 media_size, media_done, offset;
	GF_BitStream *bs;
	FILE *nhnt, *mdia, *info;
	char *ext, szName[1000], szMedia[1000], szNhnt[1000];

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = 0;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF;
		return GF_OK;
	}

	strcpy(szName, import->in_name);
	ext = strrchr(szName, '.');
	if (ext) ext[0] = 0;

	strcpy(szMedia, szName);
	strcat(szMedia, ".nhnt");
	nhnt = fopen(szMedia, "rb");
	if (!nhnt) return gf_import_message(import, GF_URL_ERROR, "Cannot find NHNT file %s", szMedia);

	strcpy(szMedia, szName);
	strcat(szMedia, ".media");
	mdia = gf_f64_open(szMedia, "rb");
	if (!mdia) {
		fclose(nhnt);
		return gf_import_message(import, GF_URL_ERROR, "Cannot find MEDIA file %s", szMedia);
	}
	

	e = GF_OK;
	destroy_esd = 0;
	if (!import->esd) {
		import->esd = gf_odf_desc_esd_new(2);
		destroy_esd = 1;
	}
	/*update stream type/oti*/
	if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
	if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);


	strcpy(szNhnt, szName);
	strcat(szNhnt, ".info");
	info = fopen(szNhnt, "rb");
	if (info) {
		if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
		import->esd->decoderConfig->decoderSpecificInfo = NULL;
		import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
		fseek(info, 0, SEEK_END);
		import->esd->decoderConfig->decoderSpecificInfo->dataLength = (u32) ftell(info);
		import->esd->decoderConfig->decoderSpecificInfo->data = malloc(sizeof(char) * import->esd->decoderConfig->decoderSpecificInfo->dataLength);
		fseek(info, 0, SEEK_SET);
		fread(import->esd->decoderConfig->decoderSpecificInfo->data, import->esd->decoderConfig->decoderSpecificInfo->dataLength, 1, info);
		fclose(info);
	}
	/*keep parsed dsi (if any) if no .info file exists*/

	bs = gf_bs_from_file(nhnt, GF_BITSTREAM_READ);
	if ((gf_bs_read_u8(bs)!='N') || (gf_bs_read_u8(bs) != 'H') || (gf_bs_read_u8(bs) != 'n')|| (gf_bs_read_u8(bs) != 't')) {
		gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Invalid NHNT signature");
		e = GF_NON_COMPLIANT_BITSTREAM;
		goto exit;
	}
	/*version*/
	gf_bs_read_u8(bs);
	import->esd->decoderConfig->streamType = gf_bs_read_u8(bs);
	import->esd->decoderConfig->objectTypeIndication = gf_bs_read_u8(bs);
	gf_bs_read_u16(bs);
	import->esd->decoderConfig->bufferSizeDB = gf_bs_read_u24(bs);
	import->esd->decoderConfig->avgBitrate = gf_bs_read_u32(bs);
	import->esd->decoderConfig->maxBitrate = gf_bs_read_u32(bs);
	import->esd->slConfig->timestampResolution = gf_bs_read_u32(bs);
	
	w = h = 0;
	switch (import->esd->decoderConfig->streamType) {
	case GF_STREAM_SCENE:
		mtype = GF_ISOM_MEDIA_SCENE;
		/*we don't know PLs from NHNT...*/
		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_SCENE, 0xFE);
		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_GRAPHICS, 0xFE);
		break;
/* WARNING: Code added for Bandwidth estimation in DANAE */
	case 32:
/**/
	case GF_STREAM_VISUAL:
		mtype = GF_ISOM_MEDIA_VISUAL;
		if (import->esd->decoderConfig->objectTypeIndication==0x20) {
			GF_M4VDecSpecInfo dsi;
			if (!import->esd->decoderConfig->decoderSpecificInfo) {
				e = GF_NON_COMPLIANT_BITSTREAM;
				goto exit;
			}
			e = gf_m4v_get_config(import->esd->decoderConfig->decoderSpecificInfo->data, import->esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
			if (e) goto exit;
			w = dsi.width;
			h = dsi.height;
		}
		break;
	case GF_STREAM_AUDIO:
		mtype = GF_ISOM_MEDIA_AUDIO;
		break;
	case GF_STREAM_MPEG7:
		mtype = GF_ISOM_MEDIA_MPEG7;
		break;
	case GF_STREAM_IPMP:
		mtype = GF_ISOM_MEDIA_IPMP;
		break;
	case GF_STREAM_OCI:
		mtype = GF_ISOM_MEDIA_OCI;
		break;
	case GF_STREAM_MPEGJ:
		mtype = GF_ISOM_MEDIA_MPEGJ;
		break;
	/*note we cannot import OD from NHNT*/
	case GF_STREAM_OD:
		e = GF_NOT_SUPPORTED;
		goto exit;
	case GF_STREAM_INTERACT:
		mtype = GF_ISOM_MEDIA_SCENE;
		break;
	default:
		mtype = GF_ISOM_MEDIA_ESM;
		break;
	}
	track = gf_isom_new_track(import->dest, import->esd->ESID, mtype, import->esd->slConfig->timestampResolution);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;
	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? szMedia : NULL, NULL, &di);
	if (e) goto exit;

	if (w && h) {
		gf_isom_set_visual_info(import->dest, track, di, w, h);
		gf_media_update_par(import->dest, track);
	}

	gf_import_message(import, GF_OK, "NHNT import - Stream Type %s - ObjectTypeIndication 0x%02x", gf_odf_stream_type_name(import->esd->decoderConfig->streamType), import->esd->decoderConfig->objectTypeIndication);

	duration = (u32) ( ((Double) import->duration)/ 1000 * import->esd->slConfig->timestampResolution);

	samp = gf_isom_sample_new();
	samp->data = malloc(sizeof(char) * 1024);
	max_size = 1024;
	count = 0;
	fseek(mdia, 0, SEEK_END);
	media_size = ftell(mdia);
	fseek(mdia, 0, SEEK_SET);
	media_done = 0;

	while (!feof(nhnt)) {
		samp->dataLength = gf_bs_read_u24(bs);
		samp->IsRAP = gf_bs_read_int(bs, 1);
		gf_bs_read_int(bs, 7);
		offset = gf_bs_read_u32(bs);
		samp->CTS_Offset = gf_bs_read_u32(bs);
		samp->DTS = gf_bs_read_u32(bs);
		samp->CTS_Offset -= (u32) samp->DTS;
		if (!count && samp->DTS) samp->DTS = 0;
		count++;

		if (import->flags & GF_IMPORT_USE_DATAREF) {
			gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			if (samp->dataLength>max_size) {
				samp->data = realloc(samp->data, sizeof(char) * samp->dataLength);
				max_size = samp->dataLength;
			}
			gf_f64_seek(mdia, offset, SEEK_SET);
			fread( samp->data, samp->dataLength, 1, mdia); 
			gf_isom_add_sample(import->dest, track, di, samp);
		}
		media_done += samp->dataLength;
		gf_import_progress(import, (u32) media_done, (u32) media_size);
		if (!gf_bs_available(bs)) break;
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	if (media_done!=media_size) gf_import_progress(import, (u32) media_size, (u32) media_size);
	gf_isom_sample_del(&samp);	
	MP4T_RecomputeBitRate(import->dest, track);

exit:
	gf_bs_del(bs);
	fclose(nhnt);
	fclose(mdia);
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	return e;
}

GF_Err gf_import_sample_from_xml(GF_MediaImporter *import, GF_ISOSample *samp, char *xml_file, char *xmlFrom, char *xmlTo)
{
	XMLParser xml_sample_parser;
	char *str, *xml_sample_att;
	char *elt_name = NULL;
	GF_Err e;
	GF_List *elt_stack = NULL;
	u32 prev_end_pos, prev_start_pos, sample_start_pos, sample_end_pos;
	Bool isFromAStart, isToAStart, foundFrom, foundTo;
	char *tmp;
	FILE *f;		

	foundFrom = 0;
	foundTo = 0;
	sample_start_pos = 0;
	sample_end_pos = 0;
	prev_end_pos = 0;
	prev_start_pos = 0;

	if (!xml_file || !xmlFrom || !xmlTo) return GF_BAD_PARAM;

	f = gf_f64_open(xml_file, "rb");
	if (!f) {
		e = gf_import_message(import, GF_BAD_PARAM, "NHML import failure: file %s not found", xml_file);
		goto exit;
	}

	if (strstr(xmlFrom, ".start")) isFromAStart = 1;
	else isFromAStart = 0;
	tmp = strchr(xmlFrom, '.');
	*tmp = 0;

	if (strstr(xmlTo, ".start")) isToAStart = 1;
	else isToAStart = 0;
	tmp = strchr(xmlTo, '.');
	*tmp = 0;

	elt_stack = gf_list_new();

	e = xml_init_parser(&xml_sample_parser, xml_file);
	if (e) goto exit;

	if (strcmp(xmlFrom, "doc")) {
		while (1) {
			str = xml_get_element(&xml_sample_parser);
			if (str == NULL) {
				char *last_name = gf_list_last(elt_stack);
				if (xml_element_done(&xml_sample_parser, last_name)) {
					gf_list_rem_last(elt_stack);
					if (!strcmp(last_name, "svg")) e = GF_IO_ERR;
					free(last_name);
					if (e) goto exit;
					prev_end_pos = xml_sample_parser.file_pos;
				}
			} else {
				prev_start_pos = prev_end_pos;
				elt_name = strdup(str);
				gf_list_add(elt_stack, elt_name);
				while (xml_has_attributes(&xml_sample_parser)) {
					xml_sample_att = xml_get_attribute(&xml_sample_parser);
					if (xml_sample_att == NULL) break;
					if (!strcmp(xml_sample_att, "id")) {						
						if (!strcmp(xml_sample_parser.value_buffer, xmlFrom)) {							
							xml_skip_attributes(&xml_sample_parser);
							foundFrom = 1;
							break;
						}
					}
				}
				if (foundFrom) {
					if(isFromAStart) {
						sample_start_pos = prev_start_pos;
					} else {
						while (!xml_element_done(&xml_sample_parser, elt_name)) {
							char *tmp = xml_get_element(&xml_sample_parser);
							xml_skip_element(&xml_sample_parser, tmp);
						}
						sample_start_pos = xml_sample_parser.file_pos;
					}
					break;
				}
			}				
		}
	} else {
		sample_start_pos = 0; // beginning of the document
	}
	if (strcmp(xmlTo, "doc")) {
		if (!isToAStart) {
			if (!strcmp(xml_sample_parser.value_buffer, xmlTo)) {
				foundTo = 1;
				while (!xml_element_done(&xml_sample_parser, elt_name)) {
					char *tmp = xml_get_element(&xml_sample_parser);
					xml_skip_element(&xml_sample_parser, tmp);
				}
				sample_end_pos = xml_sample_parser.file_pos;
			}
		}
		while (!foundTo) {
			str = xml_get_element(&xml_sample_parser);
			if (str == NULL) {
				char *last_name = gf_list_last(elt_stack);
				if (xml_element_done(&xml_sample_parser, last_name)) {
					gf_list_rem_last(elt_stack);
					if (!strcmp(last_name, "svg")) e = GF_IO_ERR;
					free(last_name);
					if (e) goto exit;
					prev_end_pos = xml_sample_parser.file_pos;
				}
			} else {
				prev_start_pos = prev_end_pos;
				elt_name = strdup(str);
				gf_list_add(elt_stack, elt_name);
				while (xml_has_attributes(&xml_sample_parser)) {
					xml_sample_att = xml_get_attribute(&xml_sample_parser);
					if (xml_sample_att == NULL) break;
					if (!strcmp(xml_sample_att, "id")) {						
						if (!strcmp(xml_sample_parser.value_buffer, xmlTo)) {
							xml_skip_attributes(&xml_sample_parser);
							foundTo = 1;
						}
					}
					if (foundTo) {
						if (isToAStart) {
							sample_end_pos = prev_start_pos;
						} else {
							while (!xml_element_done(&xml_sample_parser, elt_name)) {
								char *tmp = xml_get_element(&xml_sample_parser);
								xml_skip_element(&xml_sample_parser, tmp);
							}
							sample_end_pos = xml_sample_parser.file_pos;
						}
						break;
					}
				}
			}				
		}
	} else {
		sample_end_pos = xml_sample_parser.file_size;
	}
exit:
	if ((sample_start_pos || sample_end_pos) && (sample_start_pos < sample_end_pos)) {
		if ((sample_end_pos - sample_start_pos)>samp->dataLength) {
			samp->dataLength = sample_end_pos - sample_start_pos;
			samp->data = realloc(samp->data, sizeof(char) * samp->dataLength);
		} else {
			samp->dataLength = sample_end_pos - sample_start_pos;
		}
		gf_f64_seek(f, sample_start_pos, SEEK_SET);
		fread(samp->data, samp->dataLength, 1, f); 
	} else {
		e = GF_IO_ERR;
	}
	if (f) fclose(f);
	if (elt_stack) gf_list_del(elt_stack);
	xml_reset_parser(&xml_sample_parser);
	return e;
}

#define ZLIB_COMPRESS_SAFE	4

static GF_Err compress_sample_data(GF_ISOSample *samp, u32 *max_size, char **dict)
{
    z_stream stream;
    int err;
	char *dest = malloc(sizeof(char)*samp->dataLength*ZLIB_COMPRESS_SAFE);
    stream.next_in = (Bytef*)samp->data;
    stream.avail_in = (uInt)samp->dataLength;
    stream.next_out = dest;
    stream.avail_out = (uInt)samp->dataLength*ZLIB_COMPRESS_SAFE;
    stream.zalloc = (alloc_func)NULL;
    stream.zfree = (free_func)NULL;
    stream.opaque = (voidpf)NULL;

    err = deflateInit(&stream, 9);
    if (err != Z_OK) {
		free(dest);
		return GF_IO_ERR;
	}
	if (dict && *dict) {
		err = deflateSetDictionary(&stream, *dict, strlen(*dict));
		if (err != Z_OK) {
			fprintf(stdout, "Error assigning dictionary\n");
			deflateEnd(&stream);
			free(dest);
			return GF_IO_ERR;
		}
	}
    err = deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
		free(dest);
        return GF_IO_ERR;
    }
    if (samp->dataLength<stream.total_out) {
		fprintf(stdout, "Warning: compressed data (%d) bigger than input data (%d)\n", (u32) stream.total_out, (u32) samp->dataLength);
	}
	if (dict) {
		if (*dict) free(*dict);
		*dict = malloc(sizeof(char)*samp->dataLength);
		memcpy(*dict, samp->data, samp->dataLength);
	}
	if (*max_size < stream.total_out) {
		*max_size = samp->dataLength*ZLIB_COMPRESS_SAFE;
		free(samp->data);
		samp->data = dest;
	} else {
		memcpy(samp->data, dest, sizeof(char)*stream.total_out);
		samp->dataLength = stream.total_out;
		free(dest);
	}
    deflateEnd(&stream);
    return GF_OK;
}

/*FIXME - need LARGE FILE support in NHNT - add a new version*/
GF_Err gf_import_nhml(GF_MediaImporter *import)
{
	GF_Err e;
	XMLParser parser;
	Bool destroy_esd, inRootOD, do_compress, use_dict;
	u32 track, tkID, di, mtype, max_size, duration, count, streamType, oti, timescale, specInfoSize, dts_inc, par_den, par_num;
	GF_ISOSample *samp;
	s64 media_size, media_done, offset;
	FILE *nhml, *mdia, *info;
	char *str, *dictionary;
	char *ext, szName[1000], szMedia[1000], szMediaTemp[1000], szInfo[1000], szXmlFrom[1000], szXmlTo[1000], *specInfo;
	GF_GenericSampleDescription sdesc;
	

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = 0;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF;
		return GF_OK;
	}

	strcpy(szName, import->in_name);
	ext = strrchr(szName, '.');
	if (ext) ext[0] = 0;

	strcpy(szMedia, szName);
	strcat(szMedia, ".nhml");
	nhml = fopen(szMedia, "rt");
	if (!nhml) return gf_import_message(import, GF_URL_ERROR, "Cannot find NHML file %s", szMedia);

	strcpy(szMedia, szName);
	strcat(szMedia, ".media");

	e = xml_init_parser(&parser, import->in_name);
	if (e) return gf_import_message(import, e, "Cannot open file %s", import->in_name);
	str = xml_get_element(&parser);
	mdia = NULL;
	destroy_esd = 0;
	dts_inc = 0;
	inRootOD = 0;
	do_compress = 0;
	specInfo = NULL;

	if (!str || stricmp(str, "NHNTStream")) { 
		e = gf_import_message(import, GF_BAD_PARAM, "Error parsing NHML file - \"NHNTStream\" expected (line %d)", parser.line);
		goto exit; 
	}
	strcpy(szInfo, szName);
	strcat(szInfo, ".info");
	dictionary = NULL;
	use_dict = 0;

	memset(&sdesc, 0, sizeof(GF_GenericSampleDescription));
	tkID = mtype = streamType = oti = timescale = par_den = par_num = 0;
	while (xml_has_attributes(&parser)) {
		str = xml_get_attribute(&parser);
		if (!strcmp(str, "streamType")) streamType = atoi(parser.value_buffer);
		else if (!strcmp(str, "mediaType") && (strlen(parser.value_buffer)==4)) {
			mtype = GF_FOUR_CHAR_INT(parser.value_buffer[0], parser.value_buffer[1], parser.value_buffer[2], parser.value_buffer[3]);
		}
		else if (!strcmp(str, "mediaSubType") && (strlen(parser.value_buffer)==4)) {
			sdesc.codec_tag = GF_FOUR_CHAR_INT(parser.value_buffer[0], parser.value_buffer[1], parser.value_buffer[2], parser.value_buffer[3]);
		}
		else if (!strcmp(str, "objectTypeIndication")) oti = atoi(parser.value_buffer);
		else if (!strcmp(str, "timeScale")) timescale = atoi(parser.value_buffer);
		else if (!strcmp(str, "width")) sdesc.width = atoi(parser.value_buffer);
		else if (!strcmp(str, "height")) sdesc.height = atoi(parser.value_buffer);
		else if (!strcmp(str, "parNum")) par_num = atoi(parser.value_buffer);
		else if (!strcmp(str, "parDen")) par_den = atoi(parser.value_buffer);
		else if (!strcmp(str, "sampleRate")) sdesc.samplerate = atoi(parser.value_buffer);
		else if (!strcmp(str, "numChannels")) sdesc.nb_channels = atoi(parser.value_buffer);
		else if (!strcmp(str, "baseMediaFile")) strcpy(szMedia, parser.value_buffer);
		else if (!strcmp(str, "specificInfoFile")) strcpy(szInfo, parser.value_buffer);
		else if (!strcmp(str, "trackID")) tkID = atoi(parser.value_buffer);
		else if (!strcmp(str, "inRootOD")) inRootOD = !strcmp(parser.value_buffer, "yes");
		else if (!strcmp(str, "DTS_increment")) dts_inc = atoi(parser.value_buffer);
		else if (!strcmp(str, "gzipSamples")) do_compress = (!strcmp(parser.value_buffer, "true") || !strcmp(parser.value_buffer, "yes")) ? 1 : 0;
		else if (!strcmp(str, "gzipDictionary")) {
			u32 d_size;
			if (strcmp(parser.value_buffer, "self")) {
				FILE *d = fopen(parser.value_buffer, "rb");
				if (!d) {
					gf_import_message(import, GF_IO_ERR, "Cannot open dictionary file %s", parser.value_buffer);
					continue;
				}
				fseek(d, 0, SEEK_END);
				d_size = ftell(d);
				dictionary = malloc(sizeof(char)*(d_size+1));
				fseek(d, 0, SEEK_SET);
				fread(dictionary, 1, d_size, d);
				dictionary[d_size]=0;
			}
			use_dict = 1;
		}
		/*unknow desc related*/
		else if (!strcmp(str, "compressorName")) strcpy(sdesc.compressor_name, parser.value_buffer);
		else if (!strcmp(str, "codecVersion")) sdesc.version = atoi(parser.value_buffer);
		else if (!strcmp(str, "codecRevision")) sdesc.revision = atoi(parser.value_buffer);
		else if (!strcmp(str, "codecVendor") && (strlen(parser.value_buffer)==4)) {
			sdesc.vendor_code = GF_FOUR_CHAR_INT(parser.value_buffer[0], parser.value_buffer[1], parser.value_buffer[2], parser.value_buffer[3]);
		}
		else if (!strcmp(str, "temporalQuality")) sdesc.temporal_quality = atoi(parser.value_buffer);
		else if (!strcmp(str, "spatialQuality")) sdesc.spacial_quality = atoi(parser.value_buffer);
		else if (!strcmp(str, "horizontalResolution")) sdesc.h_res = atoi(parser.value_buffer);
		else if (!strcmp(str, "verticalResolution")) sdesc.v_res = atoi(parser.value_buffer);
		else if (!strcmp(str, "bitDepth")) sdesc.depth = atoi(parser.value_buffer);
		else if (!strcmp(str, "bitsPerSample")) sdesc.bits_per_sample = atoi(parser.value_buffer);
	}
	if (sdesc.samplerate && !timescale) timescale = sdesc.samplerate;
	if (!sdesc.bits_per_sample) sdesc.bits_per_sample = 16;

	mdia = gf_f64_open(szMedia, "rb");

	specInfoSize = 0;
	if (!streamType && !mtype && !sdesc.codec_tag) {
		e = gf_import_message(import, GF_NOT_SUPPORTED, "Error parsing NHML file - StreamType or MediaType not specified");
		goto exit; 
	}

	info = fopen(szInfo, "rb");
	if (info) {
		fseek(info, 0, SEEK_END);
		specInfoSize = (u32) ftell(info);
		specInfo = malloc(sizeof(char) * specInfoSize);
		fseek(info, 0, SEEK_SET);
		fread(specInfo, specInfoSize, 1, info);
		fclose(info);
	}
	/*compressing samples, remove data ref*/
	if (do_compress) import->flags &= ~GF_IMPORT_USE_DATAREF;

	if (streamType)  {
		if (!import->esd) {
			import->esd = gf_odf_desc_esd_new(2);
			destroy_esd = 1;
			import->esd->ESID = tkID;
		}
		/*update stream type/oti*/
		if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
		if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
		import->esd->decoderConfig->streamType = streamType;
		import->esd->decoderConfig->objectTypeIndication = oti;

		if (import->esd->decoderConfig->decoderSpecificInfo) gf_odf_desc_del((GF_Descriptor *) import->esd->decoderConfig->decoderSpecificInfo);
		import->esd->decoderConfig->decoderSpecificInfo = NULL;

		import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
		import->esd->decoderConfig->decoderSpecificInfo->dataLength = specInfoSize;
		import->esd->decoderConfig->decoderSpecificInfo->data = specInfo;
		specInfo = NULL;
		specInfoSize = 0;
		import->esd->slConfig->timestampResolution = timescale;
	
	
		switch (import->esd->decoderConfig->streamType) {
		case GF_STREAM_SCENE: mtype = GF_ISOM_MEDIA_SCENE; break;
		case GF_STREAM_VISUAL:
			mtype = GF_ISOM_MEDIA_VISUAL;
			if (import->esd->decoderConfig->objectTypeIndication==0x20) {
				GF_M4VDecSpecInfo dsi;
				if (!import->esd->decoderConfig->decoderSpecificInfo) {
					e = GF_NON_COMPLIANT_BITSTREAM;
					goto exit;
				}
				e = gf_m4v_get_config(import->esd->decoderConfig->decoderSpecificInfo->data, import->esd->decoderConfig->decoderSpecificInfo->dataLength, &dsi);
				sdesc.width = dsi.width;
				sdesc.height = dsi.height;
				if (e) goto exit;
			}
			break;
		case GF_STREAM_AUDIO: 
			mtype = GF_ISOM_MEDIA_AUDIO; 
			if (!sdesc.samplerate) sdesc.samplerate = 44100;
			if (!sdesc.nb_channels) sdesc.nb_channels = 2;
			break;
		case GF_STREAM_MPEG7: mtype = GF_ISOM_MEDIA_MPEG7; break;
		case GF_STREAM_IPMP: mtype = GF_ISOM_MEDIA_IPMP; break;
		case GF_STREAM_OCI: mtype = GF_ISOM_MEDIA_OCI; break;
		case GF_STREAM_MPEGJ: mtype = GF_ISOM_MEDIA_MPEGJ; break;
		/*note we cannot import OD from NHNT*/
		case GF_STREAM_OD: e = GF_NOT_SUPPORTED; goto exit;
		case GF_STREAM_INTERACT: mtype = GF_ISOM_MEDIA_SCENE; break;
		default: mtype = GF_ISOM_MEDIA_ESM; break;
		}

		track = gf_isom_new_track(import->dest, import->esd->ESID, mtype, timescale);
		if (!track) { e = gf_isom_last_error(import->dest); goto exit; }
		e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? szMedia : NULL, NULL, &di);
		if (e) goto exit;

		gf_import_message(import, GF_OK, "NHML import - Stream Type %s - ObjectTypeIndication 0x%02x", gf_odf_stream_type_name(import->esd->decoderConfig->streamType), import->esd->decoderConfig->objectTypeIndication);

	} else {
		char szT[5];
		sdesc.extension_buf = specInfo;
		sdesc.extension_buf_size = specInfoSize;
		if (!sdesc.vendor_code) sdesc.vendor_code = GF_FOUR_CHAR_INT('G', 'P', 'A', 'C');

		track = gf_isom_new_track(import->dest, tkID, mtype, timescale);
		if (!track) { e = gf_isom_last_error(import->dest); goto exit; }

		e = gf_isom_new_generic_sample_description(import->dest, track, (import->flags & GF_IMPORT_USE_DATAREF) ? szMedia : NULL, NULL, &sdesc, &di);
		if (e) goto exit;

		strcpy(szT, gf_4cc_to_str(mtype));
		gf_import_message(import, GF_OK, "NHML import - MediaType \"%s\" - SubType \"%s\"", szT, gf_4cc_to_str(sdesc.codec_tag));
	}

	gf_isom_set_track_enabled(import->dest, track, 1);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);
	if (import->esd && !import->esd->ESID) import->esd->ESID = import->final_trackID;
	
	if (sdesc.width && sdesc.height) {
		gf_isom_set_visual_info(import->dest, track, di, sdesc.width, sdesc.height);
		if (par_den && par_num) {
			gf_media_change_par(import->dest, track, par_num, par_den);
		} else {
			gf_media_update_par(import->dest, track);
		}
	}
	else if (sdesc.samplerate && sdesc.nb_channels) {
		gf_isom_set_audio_info(import->dest, track, di, sdesc.samplerate, sdesc.nb_channels, (u8) sdesc.bits_per_sample);
	}

	duration = (u32) ( ((Double) import->duration)/ 1000 * timescale);

	samp = gf_isom_sample_new();
	samp->data = malloc(sizeof(char) * 1024);
	max_size = 1024;
	count = 0;
	media_size = 0;
	if (mdia) {
		fseek(mdia, 0, SEEK_END);
		media_size = ftell(mdia);
		fseek(mdia, 0, SEEK_SET);
	}
	media_done = 0;

	samp->IsRAP = 1;
	while (!xml_element_done(&parser, "NHNTStream") && !parser.done) {
		str = xml_get_element(&parser);
		if (!str) { e = GF_IO_ERR; goto exit; }
		if (stricmp(str, "NHNTSample")) {
			xml_skip_element(&parser, str);
			continue;
		}
		strcpy(szMediaTemp, "");
		strcpy(szXmlFrom, "");
		strcpy(szXmlTo, "");

		/*by default handle all samples as contigous*/
		offset = 0;

		samp->dataLength = 0;

		while (xml_has_attributes(&parser)) {
			str = xml_get_attribute(&parser);
			if (!strcmp(str, "DTS")) {
				u32 h, m, s, ms;
				if (sscanf(parser.value_buffer, "%d:%d:%d.%d", &h, &m, &s, &ms) == 4) {
					samp->DTS = (u64) ( (Double) ( ((h*3600 + m*60 + s)*1000 + ms) / 1000.0) * timescale );
				} else {
					samp->DTS = atoi(parser.value_buffer);
				}
			}
			else if (!strcmp(str, "CTSOffset")) samp->CTS_Offset = atoi(parser.value_buffer);
			else if (!strcmp(str, "isRAP")) samp->IsRAP = !stricmp(parser.value_buffer, "yes") ? 1 : 0;
			else if (!strcmp(str, "isSyncShadow")) samp->IsRAP = !stricmp(parser.value_buffer, "yes") ? 2 : 0;
			else if (!strcmp(str, "mediaOffset")) offset = (s64) atof(parser.value_buffer) ;
			else if (!strcmp(str, "dataLength")) samp->dataLength = atoi(parser.value_buffer);
			else if (!strcmp(str, "mediaFile")) strcpy(szMediaTemp, parser.value_buffer);
			else if (!strcmp(str, "xmlFrom")) strcpy(szXmlFrom, parser.value_buffer);
			else if (!strcmp(str, "xmlTo")) strcpy(szXmlTo, parser.value_buffer);
		}
		xml_element_done(&parser, "NHNTSample");

		if (!count && samp->DTS) samp->DTS = 0;
		count++;

		if (import->flags & GF_IMPORT_USE_DATAREF) {
			if (offset) offset = media_done;
			e = gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else if (!strlen(szXmlFrom) || !strlen(szXmlTo)) {
			Bool close = 0;
			FILE *f = mdia;
			if (strlen(szMediaTemp)) {
				f = gf_f64_open(szMediaTemp, "rb");
				close = 1;
			} else {
				if (!offset) offset = media_done;
			}
			if (!f) {
				e = gf_import_message(import, GF_BAD_PARAM, "NHML import failure: file %s not found", close ? szMediaTemp : szMedia);
				goto exit;
			}

			if (!samp->dataLength) {
				u64 cur_pos = gf_f64_tell(f);
				gf_f64_seek(f, 0, SEEK_END);
				samp->dataLength = (u32) gf_f64_tell(f);
				gf_f64_seek(f, cur_pos, SEEK_SET);
			}

			if (samp->dataLength>max_size) {
				samp->data = realloc(samp->data, sizeof(char) * samp->dataLength);
				max_size = samp->dataLength;
			}
			gf_f64_seek(f, offset, SEEK_SET);
			fread( samp->data, samp->dataLength, 1, f); 
			if (close) fclose(f);
		} else {
			char *xml_file;
			if (strlen(szMediaTemp)) xml_file = szMediaTemp;
			else xml_file = szMedia;
			samp->dataLength = max_size;
			gf_import_sample_from_xml(import, samp, xml_file, szXmlFrom, szXmlTo);
		}
		if (e) goto exit;

		if (do_compress) {
			e = compress_sample_data(samp, &max_size, use_dict ? &dictionary : NULL);
			if (e) goto exit;
		}

		if (samp->IsRAP==2) {
			e = gf_isom_add_sample_shadow(import->dest, track, samp);
		} else {
			e = gf_isom_add_sample(import->dest, track, di, samp);
		}
		samp->IsRAP = 0;
		samp->DTS += dts_inc;
		media_done += samp->dataLength;
		gf_import_progress(import, (u32) media_done, (u32) (media_size ? media_size : media_done+1) );
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	if (media_done!=media_size) gf_import_progress(import, (u32) media_size, (u32) media_size);
	gf_isom_sample_del(&samp);	
	MP4T_RecomputeBitRate(import->dest, track);

	if (inRootOD) gf_isom_add_track_to_root_od(import->dest, track);

exit:
	fclose(nhml);
	if (mdia) fclose(mdia);
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	xml_reset_parser(&parser);
	if (specInfo) free(specInfo);
	if (dictionary) free(dictionary);
	return e;
}


GF_Err gf_import_amr_evrc_smv(GF_MediaImporter *import)
{
	GF_Err e;
	u32 track, trackID, di, max_size, duration, sample_rate, block_size, i;
	GF_ISOSample *samp;
	char magic[20];
	Bool delete_esd, update_gpp_cfg;
	u32 media_size, media_done, offset, mtype, oti, nb_frames;
	GF_3GPConfig gpp_cfg;
	FILE *mdia;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_AUDIO;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_FORCE_MPEG4 | GF_IMPORT_3GPP_AGGREGATION;
		return GF_OK;
	}

	mdia = fopen(import->in_name, "rb");
	if (!mdia) return gf_import_message(import, GF_URL_ERROR, "Cannot find file %s", import->in_name);
	
	update_gpp_cfg = 0;
	oti = mtype = 0;
	sample_rate = 8000;
	block_size = 160;
	fread(magic, 1, 20, mdia);
	if (!strnicmp(magic, "#!AMR\n", 6)) {
		gf_import_message(import, GF_OK, "Importing AMR Audio");
		fseek(mdia, 6, SEEK_SET);
		mtype = GF_ISOM_SUBTYPE_3GP_AMR;
		update_gpp_cfg = 1;
	}
	else if (!strnicmp(magic, "#!EVRC\n", 7)) {
		gf_import_message(import, GF_OK, "Importing EVRC Audio");
		fseek(mdia, 7, SEEK_SET);
		mtype = GF_ISOM_SUBTYPE_3GP_EVRC;
		oti = 0xA0;
	}
	else if (!strnicmp(magic, "#!SMV\n", 6)) {
		gf_import_message(import, GF_OK, "Importing SMV Audio");
		fseek(mdia, 6, SEEK_SET);
		mtype = GF_ISOM_SUBTYPE_3GP_SMV;
		oti = 0xA1;
	}
	else if (!strnicmp(magic, "#!AMR_MC1.0\n", 12)) {
		fclose(mdia);
		return gf_import_message(import, GF_NOT_SUPPORTED, "Multichannel AMR Audio Not Supported");
	}
	else if (!strnicmp(magic, "#!AMR-WB\n", 9)) {
		gf_import_message(import, GF_OK, "Importing AMR WideBand Audio");
		fseek(mdia, 9, SEEK_SET);
		mtype = GF_ISOM_SUBTYPE_3GP_AMR_WB;
		sample_rate = 16000;
		block_size = 320;
		update_gpp_cfg = 1;
	}
	else if (!strnicmp(magic, "#!AMR-WB_MC1.0\n", 15)) {
		fclose(mdia);
		return gf_import_message(import, GF_NOT_SUPPORTED, "Multichannel AMR WideBand Audio Not Supported");
	}
	else {
		fclose(mdia);
		return gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Corrupted AMR file header (expecting '#!AMR...')");
	}

	delete_esd = 0;
	trackID = 0;
	e = GF_OK;
	if (import->esd) trackID = import->esd->ESID;

	track = gf_isom_new_track(import->dest, trackID, GF_ISOM_MEDIA_AUDIO, sample_rate);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (import->esd && !import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	memset(&gpp_cfg, 0, sizeof(GF_3GPConfig));
	gpp_cfg.type = mtype;
	gpp_cfg.frames_per_sample = import->frames_per_sample;
	if (!gpp_cfg.frames_per_sample) gpp_cfg.frames_per_sample  = 1;
	else if (gpp_cfg.frames_per_sample >15) gpp_cfg.frames_per_sample = 15;
	
	if (import->flags & GF_IMPORT_USE_DATAREF) gpp_cfg.frames_per_sample  = 1;


	if (oti && (import->flags & GF_IMPORT_FORCE_MPEG4)) {
		if (!import->esd) {
			delete_esd = 1;
			import->esd = gf_odf_desc_esd_new(2);
			import->esd->ESID = trackID;
		}
		import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
		import->esd->decoderConfig->objectTypeIndication = oti;
		e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
		if (e) goto exit;
	} else {
		import->flags &= ~GF_IMPORT_FORCE_MPEG4;
		gpp_cfg.vendor = GF_FOUR_CHAR_INT('G', 'P', 'A', 'C');
		e = gf_isom_3gp_config_new(import->dest, track, &gpp_cfg, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
		if (e) goto exit;
	}
	gf_isom_set_audio_info(import->dest, track, di, sample_rate, 1, 8);
	duration = import->duration * sample_rate;
	duration /= 1000;

	samp = gf_isom_sample_new();
	samp->data = malloc(sizeof(char) * 200);
	samp->IsRAP = 1;
	max_size = 200;
	offset = ftell(mdia);
	fseek(mdia, 0, SEEK_END);
	media_size = ftell(mdia) - offset;
	fseek(mdia, offset, SEEK_SET);

	media_done = 0;
	nb_frames = 0;

	while (!feof(mdia)) {
		u8 ft, toc;
		
		offset = ftell(mdia);
		toc = fgetc(mdia);
		switch (gpp_cfg.type) {
		case GF_ISOM_SUBTYPE_3GP_AMR:
		case GF_ISOM_SUBTYPE_3GP_AMR_WB:
			ft = (toc >> 3) & 0x0F;
			/*update mode set (same mechanism for both AMR and AMR-WB*/
			gpp_cfg.AMR_mode_set |= (1<<ft);
			if (gpp_cfg.type==GF_ISOM_SUBTYPE_3GP_AMR_WB) {
				samp->dataLength = GF_AMR_WB_FRAME_SIZE[ft];
			} else {
				samp->dataLength = GF_AMR_FRAME_SIZE[ft];
			}
			samp->data[0] = toc;
			break;
		case GF_ISOM_SUBTYPE_3GP_EVRC:
		case GF_ISOM_SUBTYPE_3GP_SMV:
			for (i=0; i<GF_SMV_EVRC_RATE_TO_SIZE_NB; i++) {
				if (GF_SMV_EVRC_RATE_TO_SIZE[2*i]==toc) {
					/*remove rate_type byte*/
					samp->dataLength = GF_SMV_EVRC_RATE_TO_SIZE[2*i+1] - 1;
					break;
				}
			}
			samp->data[0] = toc;
			break;
		}

		if (samp->dataLength) 
			fread( samp->data + 1, samp->dataLength, 1, mdia);
		
		samp->dataLength += 1;
		/*if last frame is "no data", abort - this happens in many files with constant mode (ie constant files), where
		adding this last frame will result in a non-compact version of the stsz table, hence a bigger file*/
		if ((samp->dataLength==1) && feof(mdia))
			break;


		if (import->flags & GF_IMPORT_USE_DATAREF) {
			gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else if (!nb_frames) {
			gf_isom_add_sample(import->dest, track, di, samp);
		} else {
			gf_isom_append_sample_data(import->dest, track, samp->data, samp->dataLength);
		}
		nb_frames++;
		if (nb_frames==gpp_cfg.frames_per_sample) nb_frames=0;

		samp->DTS += block_size;
		media_done += samp->dataLength;
		gf_import_progress(import, (u32) media_done, (u32) media_size);
		if (duration && (samp->DTS > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;
	}
	gf_isom_sample_del(&samp);	
	gf_isom_refresh_size_info(import->dest, track);

	if (import->flags & GF_IMPORT_FORCE_MPEG4) MP4T_RecomputeBitRate(import->dest, track);

	if (update_gpp_cfg) gf_isom_3gp_config_update(import->dest, track, &gpp_cfg, 1);

exit:
	if (delete_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	fclose(mdia);
	return e;
}

/*QCP codec GUIDs*/
static const unsigned char *QCP_QCELP_GUID_1 = "\x41\x6D\x7F\x5E\x15\xB1\xD0\x11\xBA\x91\x00\x80\x5F\xB4\xB9\x7E";
static const unsigned char *QCP_QCELP_GUID_2 = "\x42\x6D\x7F\x5E\x15\xB1\xD0\x11\xBA\x91\x00\x80\x5F\xB4\xB9\x7E";
static const unsigned char *QCP_EVRC_GUID = "\x8D\xD4\x89\xE6\x76\x90\xB5\x46\x91\xEF\x73\x6A\x51\x00\xCE\xB4";
static const unsigned char *QCP_SMV_GUID = "\x75\x2B\x7C\x8D\x97\xA7\x46\xED\x98\x5E\xD5\x3C\x8C\xC7\x5F\x84";

GF_Err gf_import_qcp(GF_MediaImporter *import)
{
	GF_Err e;
	u32 track, trackID, di, i, nb_pck, max_size, duration, riff_size, chunk_size, major, minor, version, avg_bps, pck_size, block_size, bps, samplerate, vrat_rate_flag, size_in_packets, nb_frames;
	GF_BitStream *bs;
	GF_ISOSample *samp;
	unsigned char magic[12], GUID[16], name[81], fmt[162];
	u32 media_size, media_done, offset, rtable_cnt;
	Bool has_pad;
	QCPRateTable rtable[8];
	Bool delete_esd;
	GF_3GPConfig gpp_cfg;
	FILE *mdia;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_AUDIO;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_FORCE_MPEG4 | GF_IMPORT_3GPP_AGGREGATION;
		return GF_OK;
	}

	memset(&gpp_cfg, 0, sizeof(GF_3GPConfig));
	delete_esd = 0;

	mdia = fopen(import->in_name, "rb");
	if (!mdia) return gf_import_message(import, GF_URL_ERROR, "Cannot find file %s", import->in_name);
	
	bs = gf_bs_from_file(mdia, GF_BITSTREAM_READ);
	gf_bs_read_data(bs, magic, 4);
	if (strnicmp(magic, "RIFF", 4)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Broken QCP file: RIFF header not found");
		goto exit;
	}
	riff_size = gf_bs_read_u32_le(bs);
	gf_bs_read_data(bs, fmt, 162);
	gf_bs_seek(bs, 8);
	gf_bs_read_data(bs, magic, 4);
	if (strnicmp(magic, "QLCM", 4)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Broken QCP file: QLCM header not found");
		goto exit;
	}
	max_size = (u32) gf_bs_get_size(bs);
	if (max_size != riff_size+8) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Broken QCP file: Expecting RIFF-Size %d got %d", max_size-8, riff_size);
		goto exit;
	}
	/*fmt*/
	gf_bs_read_data(bs, magic, 4);
	if (strnicmp(magic, "fmt ", 4)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Broken QCP file: FMT not found");
		goto exit;
	}
	chunk_size = gf_bs_read_u32_le(bs);
	has_pad = (chunk_size%2) ? 1 : 0;
	major = gf_bs_read_u8(bs);
	minor = gf_bs_read_u8(bs);
	chunk_size -= 2;
	/*codec info*/
	gf_bs_read_data(bs, GUID, 16);
	version = gf_bs_read_u16_le(bs);
	chunk_size -= 18;
	gf_bs_read_data(bs, name, 80);
	name[80]=0;
	chunk_size -= 80;
	avg_bps = gf_bs_read_u16_le(bs);
	pck_size = gf_bs_read_u16_le(bs);
	block_size = gf_bs_read_u16_le(bs);
	samplerate = gf_bs_read_u16_le(bs);
	bps = gf_bs_read_u16_le(bs);
	rtable_cnt = gf_bs_read_u32_le(bs);
	chunk_size -= 14;
	/*skip var rate*/
	for (i=0; i<8; i++) {
		rtable[i].pck_size = gf_bs_read_u8(bs);
		rtable[i].rate_idx = gf_bs_read_u8(bs);
	}
	chunk_size -= 16;
	gf_bs_skip_bytes(bs, 5*4);/*reserved*/
	chunk_size -= 20;
	gf_bs_skip_bytes(bs, chunk_size);
	if (has_pad) gf_bs_read_u8(bs);

	if (!strncmp(GUID, QCP_QCELP_GUID_1, 16) || !strncmp(GUID, QCP_QCELP_GUID_2, 16)) {
		gpp_cfg.type = GF_ISOM_SUBTYPE_3GP_QCELP;
		strcpy(name, "QCELP-13K");
	} else if (!strncmp(GUID, QCP_EVRC_GUID, 16)) {
		gpp_cfg.type = GF_ISOM_SUBTYPE_3GP_EVRC;
		strcpy(name, "EVRC");
	} else if (!strncmp(GUID, QCP_SMV_GUID, 16)) {
		gpp_cfg.type = GF_ISOM_SUBTYPE_3GP_SMV;
		strcpy(name, "SMV");
	} else {
		gpp_cfg.type = 0;
	}
	/*vrat*/
	gf_bs_read_data(bs, magic, 4);
	if (strnicmp(magic, "vrat", 4)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Broken QCP file: VRAT not found");
		goto exit;
	}
	chunk_size = gf_bs_read_u32_le(bs);
	has_pad = (chunk_size%2) ? 1 : 0;
	vrat_rate_flag = gf_bs_read_u32_le(bs);
	size_in_packets = gf_bs_read_u32_le(bs);
	chunk_size -= 8;
	gf_bs_skip_bytes(bs, chunk_size);
	if (has_pad) gf_bs_read_u8(bs);

	if (!gpp_cfg.type) {
		e = gf_import_message(import, GF_NOT_SUPPORTED, "Unknown QCP file codec %s", name);
		goto exit;
	}

	gf_import_message(import, GF_OK, "Importing %s Audio - SampleRate %d", name, samplerate);

	trackID = 0;
	e = GF_OK;
	if (import->esd) trackID = import->esd->ESID;

	track = gf_isom_new_track(import->dest, trackID, GF_ISOM_MEDIA_AUDIO, samplerate);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (import->esd && !import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	if (import->flags & GF_IMPORT_FORCE_MPEG4) {
		if (!import->esd) {
			import->esd = gf_odf_desc_esd_new(2);
			delete_esd = 1;
		}
		if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig*)gf_odf_desc_new(GF_ODF_DCD_TAG);
		import->esd->decoderConfig->streamType = 0x05;
		switch (gpp_cfg.type) {
		case GF_ISOM_SUBTYPE_3GP_QCELP:
			import->esd->decoderConfig->objectTypeIndication = 0xE1;
			/*DSI is fmt*/
			if (!import->esd->decoderConfig->decoderSpecificInfo) import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
			if (import->esd->decoderConfig->decoderSpecificInfo->data) free(import->esd->decoderConfig->decoderSpecificInfo->data);
			import->esd->decoderConfig->decoderSpecificInfo->dataLength = 162;
			import->esd->decoderConfig->decoderSpecificInfo->data = malloc(sizeof(char)*162);
			memcpy(import->esd->decoderConfig->decoderSpecificInfo->data, fmt, 162);
			break;
		case GF_ISOM_SUBTYPE_3GP_EVRC:
			import->esd->decoderConfig->objectTypeIndication = 0xA0;
			break;
		case GF_ISOM_SUBTYPE_3GP_SMV:
			import->esd->decoderConfig->objectTypeIndication = 0xA1;
			break;
		}
		e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
		if (e) goto exit;
	} else {
		if (import->frames_per_sample<=1) import->frames_per_sample=1;
		else if (import->frames_per_sample>15) import->frames_per_sample=15;
		gpp_cfg.frames_per_sample = import->frames_per_sample;
		e = gf_isom_3gp_config_new(import->dest, track, &gpp_cfg, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
		if (e) goto exit;
	}
	gf_isom_set_audio_info(import->dest, track, di, samplerate, 1, (u8) bps);

	duration = import->duration * samplerate;
	duration /= 1000;

	samp = gf_isom_sample_new();
	samp->data = malloc(sizeof(char) * 200);
	samp->IsRAP = 1;
	max_size = 200;
	offset = ftell(mdia);
	fseek(mdia, 0, SEEK_END);
	media_size = ftell(mdia) - offset;
	fseek(mdia, offset, SEEK_SET);

	nb_pck = 0;
	media_done = 0;
	nb_frames = 0;
	while (gf_bs_available(bs)) {
		gf_bs_read_data(bs, magic, 4);
		chunk_size = gf_bs_read_u32_le(bs);
		has_pad = (chunk_size%2) ? 1 : 0;
		/*process chunk by chunk*/
		if (!strnicmp(magic, "data", 4)) {

			while (chunk_size) {
				u32 idx = 0;
				u32 size = 0;

				offset = (u32) gf_bs_get_position(bs);
				/*get frame rate idx*/
				if (vrat_rate_flag) {
					idx = gf_bs_read_u8(bs);
					chunk_size-=1;
					for (i=0; i<rtable_cnt; i++) {
						if (rtable[i].rate_idx==idx) {size = rtable[i].pck_size; break;}
					}
					samp->dataLength = size+1;
				} else {
					size = pck_size;
					samp->dataLength = size;
				}
				if (max_size<samp->dataLength) {
					samp->data = realloc(samp->data, sizeof(char)*samp->dataLength);
					max_size=samp->dataLength;
				}
				if (import->flags & GF_IMPORT_USE_DATAREF) {
					gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
					gf_bs_skip_bytes(bs, size);
				} else {
					if (vrat_rate_flag) {
						samp->data[0] = idx;
						gf_bs_read_data(bs, &samp->data[1], size);
					} else {
						gf_bs_read_data(bs, samp->data, size);
					}
					if (!nb_frames) {
						gf_isom_add_sample(import->dest, track, di, samp);
					} else {
						gf_isom_append_sample_data(import->dest, track, samp->data, samp->dataLength);
					}
					nb_frames++;
					if (nb_frames==import->frames_per_sample) nb_frames=0;
				}
				chunk_size -= size;
				samp->DTS += block_size;
				if (size_in_packets) {
					gf_import_progress(import, (u32) nb_pck, (u32) size_in_packets);
				} else {
					gf_import_progress(import, (u32) media_done, (u32) media_size);
				}
				if (nb_pck==1504) 
					nb_pck=1504;

				nb_pck++;
				media_done += samp->dataLength;
				if (duration && (samp->DTS > duration)) break;
				if (import->flags & GF_IMPORT_DO_ABORT) break;
			}
		} else if (!strnicmp(magic, "labl", 4)) {
		} else if (!strnicmp(magic, "offs", 4)) {
		} else if (!strnicmp(magic, "cnfg", 4)) {
		} else if (!strnicmp(magic, "text", 4)) {
		}
		gf_bs_skip_bytes(bs, chunk_size);
		if (has_pad) gf_bs_read_u8(bs);
	}
	gf_isom_sample_del(&samp);	
	gf_isom_set_brand_info(import->dest, GF_ISOM_BRAND_3G2A, 65536);
	if (import->flags & GF_IMPORT_FORCE_MPEG4) MP4T_RecomputeBitRate(import->dest, track);
	gf_import_progress(import, size_in_packets, size_in_packets);

exit:
	if (delete_esd && import->esd) {
		gf_odf_desc_del((GF_Descriptor *)import->esd);
		import->esd = NULL;
	}
	gf_bs_del(bs);
	fclose(mdia);
	return e;
}

/*read that amount of data at each IO access rather than fetching byte by byte...*/
Bool H263_IsStartCode(GF_BitStream *bs)
{
	u32 c;
	c = gf_bs_peek_bits(bs, 22, 0);
	if (c==0x20) return 1;
	return 0;
}

#define H263_CACHE_SIZE	4096
u32 H263_NextStartCode(GF_BitStream *bs)
{
	u32 v, bpos;
	unsigned char h263_cache[H263_CACHE_SIZE];
	u64 end, cache_start, load_size;
	u64 start = gf_bs_get_position(bs);
	
	/*skip 16b header*/
	gf_bs_read_u16(bs);
	bpos = 0;
	load_size = 0;
	cache_start = 0;
	end = 0;
	v = 0xffffffff;
	while (!end) {
		/*refill cache*/
		if (bpos == (u32) load_size) {
			if (!gf_bs_available(bs)) break;
			load_size = gf_bs_available(bs);
			if (load_size>H263_CACHE_SIZE) load_size=H263_CACHE_SIZE;
			bpos = 0;
			cache_start = gf_bs_get_position(bs);
			gf_bs_read_data(bs, h263_cache, (u32) load_size);
		}
		v = (v<<8) | h263_cache[bpos];
		bpos++;
        if ((v >> (32-22)) == 0x20) end = cache_start+bpos-4;
	}
	gf_bs_seek(bs, start);
	if (!end) end = gf_bs_get_size(bs);
	return (u32) (end-start);
}
static void h263_get_pic_size(GF_BitStream *bs, u32 fmt, u32 *w, u32 *h)
{
	switch (fmt) {
	case 1: *w = 128; *h = 96; break;
    case 2: *w = 176; *h = 144; break;
    case 3: *w = 352; *h = 288; break;
    case 4: *w = 704; *h = 576; break;
    case 5: *w = 1409; *h = 1152 ; break;
    default: *w = *h = 0; break;
    }
}

GF_Err gf_import_h263(GF_MediaImporter *import)
{
	GF_Err e;
	u32 track, trackID, di, max_size, timescale, duration, w, h, fmt, nb_samp, dts_inc;
	u64 offset;
	GF_ISOSample *samp;
	char *samp_data;
	u32 media_size, media_done;
	GF_3GPConfig gpp_cfg;
	Double FPS;
	FILE *mdia;
	GF_BitStream *bs;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_VISUAL;
		import->tk_info[0].flags = GF_IMPORT_USE_DATAREF | GF_IMPORT_OVERRIDE_FPS;
		return GF_OK;
	}

	mdia = fopen(import->in_name, "rb");
	if (!mdia) return gf_import_message(import, GF_URL_ERROR, "Cannot find file %s", import->in_name);

	bs = gf_bs_from_file(mdia, GF_BITSTREAM_READ);
	if (!H263_IsStartCode(bs)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Cannot find H263 Picture Start Code");
		goto exit;
	}
	FPS = (Double) import->video_fps;
	/*for H263 we use 15 fps by default!!*/
	if (!FPS) FPS = 15;
	get_video_timing(FPS, &timescale, &dts_inc);

	/*parse header*/
	gf_bs_read_int(bs, 22);
	gf_bs_read_int(bs, 8);
	/*spare+0+split_screen_indicator+document_camera_indicator+freeze_picture_release*/
	gf_bs_read_int(bs, 5);

	fmt = gf_bs_read_int(bs, 3);
	h263_get_pic_size(bs, fmt, &w, &h);
	if (!w || !h) {
		e = gf_import_message(import, GF_NOT_SUPPORTED, "Unsupported H263 frame header");
		goto exit;
	}
	trackID = 0;
	e = GF_OK;
	if (import->esd) {
		trackID = import->esd->ESID;
		if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig*) gf_odf_desc_new(GF_ODF_SLC_TAG);
	}
	track = gf_isom_new_track(import->dest, trackID, GF_ISOM_MEDIA_VISUAL, timescale);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (import->esd && !import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	memset(&gpp_cfg, 0, sizeof(GF_3GPConfig));
	gpp_cfg.type = GF_ISOM_SUBTYPE_3GP_H263;
	gpp_cfg.vendor = GF_FOUR_CHAR_INT('G','P','A','C');
	/*FIXME - we need more in-depth parsing of the bitstream to detect P3@L10 (streaming wireless)*/
	gpp_cfg.H263_profile = 0;
	gpp_cfg.H263_level = 10;
	e = gf_isom_3gp_config_new(import->dest, track, &gpp_cfg, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	if (e) goto exit;
	gf_isom_set_visual_info(import->dest, track, di, w, h);
	gf_import_message(import, GF_OK, "Importing H263 video - %d x %d @ %02.4f", w, h, FPS);

	samp = gf_isom_sample_new();

	duration = (u32) ( ((Double)import->duration) * timescale / 1000.0);
	media_size = (u32) gf_bs_get_size(bs);
	nb_samp = media_done = 0;

	max_size = 4096;
	samp_data = malloc(sizeof(char)*max_size);
	gf_bs_seek(bs, 0);
	offset = 0;
	while (gf_bs_available(bs)) {
		samp->dataLength = H263_NextStartCode(bs);
		if (samp->dataLength>max_size) {
			max_size = samp->dataLength;
			samp_data = realloc(samp_data, sizeof(char)*max_size);
		}
		gf_bs_read_data(bs, samp_data, samp->dataLength);
		/*we ignore pict number and import at const FPS*/
		samp->IsRAP = (samp_data[4]&0x02) ? 0 : 1;
		samp->data = samp_data;
		if (import->flags & GF_IMPORT_USE_DATAREF) {
			gf_isom_add_sample_reference(import->dest, track, di, samp, offset);
		} else {
			gf_isom_add_sample(import->dest, track, di, samp);
		}
		samp->data = NULL;
		samp->DTS += dts_inc;
		nb_samp ++;
		offset += samp->dataLength;
		gf_import_progress(import, media_done, media_size);
		media_done += samp->dataLength;

		if ((duration && (samp->DTS > duration) ) || (import->flags & GF_IMPORT_DO_ABORT)) {
			break;
		}
	}
	free(samp_data);
	gf_isom_sample_del(&samp);
	gf_import_progress(import, nb_samp, nb_samp);
	gf_isom_modify_alternate_brand(import->dest, GF_FOUR_CHAR_INT('3','g','g','6'), 1);
	gf_isom_modify_alternate_brand(import->dest, GF_FOUR_CHAR_INT('3','g','g','5'), 1);

exit:
	gf_bs_del(bs);
	fclose(mdia);
	return e;
}
static void avc_rewrite_samples(GF_ISOFile *file, u32 track, u32 prev_size, u32 new_size)
{
	u32 i, count, di, remain, msize;
	char *buffer;

	msize = 4096;
	buffer = malloc(sizeof(char)*msize);
	count = gf_isom_get_sample_count(file, track);
	for (i=0; i<count;i++) {
		GF_ISOSample *samp = gf_isom_get_sample(file, track, i+1, &di);
		GF_BitStream *oldbs = gf_bs_new(samp->data, samp->dataLength, GF_BITSTREAM_READ);
		GF_BitStream *newbs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
		remain = samp->dataLength;
		while (remain) {
			u32 size = gf_bs_read_int(oldbs, prev_size);
			gf_bs_write_int(newbs, size, new_size);
			remain -= prev_size/8;
			if (size>msize) {
				msize = size;
				buffer = realloc(buffer, sizeof(char)*msize);
			}
			gf_bs_read_data(oldbs, buffer, size);
			gf_bs_write_data(newbs, buffer, size);
			remain -= size;
		}
		gf_bs_del(oldbs);
		free(samp->data);
		samp->data = NULL;
		samp->dataLength = 0;
		gf_bs_get_content(newbs, (unsigned char **) &samp->data, &samp->dataLength);
		gf_bs_del(newbs);
		gf_isom_update_sample(file, track, i+1, samp, 1);
		gf_isom_sample_del(&samp);
	}
	free(buffer);
}

GF_Err gf_import_h264(GF_MediaImporter *import)
{
	u64 nal_start, nal_end;
	u32 nal_size, track, trackID, di, cur_samp, tot_size, nb_i, nb_idr, nb_p, nb_b, nb_sp, nb_si, nb_sei, max_w, max_h, duration, max_delay, max_total_delay;
	s32 idx;
	u8 nal_type;
	GF_Err e;
	FILE *mdia;
	AVCState avc;
	GF_AVCConfigSlot *slc;
	GF_AVCConfig *avccfg;
	GF_BitStream *bs;
	GF_BitStream *sample_data;
	Bool flush_sample, sample_is_rap, first_nal, slice_is_ref, has_cts_offset;
	u32 b_frames, ref_frame, pred_frame, timescale, copy_size, size_length, dts_inc;
	s32 last_poc, max_last_poc, max_last_b_poc, poc_diff, prev_last_poc, min_poc, poc_shift;
	Double FPS;
	char *buffer;
	u32 max_size = 4096;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->nb_tracks = 1;
		import->tk_info[0].track_num = 1;
		import->tk_info[0].type = GF_ISOM_MEDIA_VISUAL;
		import->tk_info[0].flags = GF_IMPORT_OVERRIDE_FPS;
		return GF_OK;
	}

	mdia = fopen(import->in_name, "rb");
	if (!mdia) return gf_import_message(import, GF_URL_ERROR, "Cannot find file %s", import->in_name);
	
	FPS = (Double) import->video_fps;
	if (!FPS) FPS = GF_IMPORT_DEFAULT_FPS;
	get_video_timing(FPS, &timescale, &dts_inc);

	memset(&avc, 0, sizeof(AVCState));
	avccfg = gf_odf_avc_cfg_new();
	buffer = malloc(sizeof(char) * max_size);
	sample_data = NULL;

	bs = gf_bs_from_file(mdia, GF_BITSTREAM_READ);
	if (!AVC_IsStartCode(bs)) {
		e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Cannot find H264 start code");
		goto exit;
	}

	/*if import in edit mode, use smallest NAL size and adjust on the fly*/
	if (gf_isom_get_mode(import->dest)!=GF_ISOM_OPEN_WRITE) size_length = 8;
	else size_length = 32;
	
	trackID = 0;
	e = GF_OK;
	if (import->esd) trackID = import->esd->ESID;

	track = gf_isom_new_track(import->dest, trackID, GF_ISOM_MEDIA_VISUAL, timescale);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (import->esd && !import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = gf_isom_get_track_id(import->dest, track);

	e = gf_isom_avc_config_new(import->dest, track, avccfg, NULL, NULL, &di);
	if (e) goto exit;

	sample_data = NULL;
	sample_is_rap = 0;
	cur_samp = 0;
	tot_size = (u32) gf_bs_get_size(bs);
	nal_start = gf_bs_get_position(bs);
	duration = (u32) ( ((Double)import->duration) * timescale / 1000.0);
	
	nb_i = nb_idr = nb_p = nb_b = nb_sp = nb_si = nb_sei = 0;
	max_w = max_h = 0;
	first_nal = 1;
	b_frames = ref_frame = pred_frame = 0;
	last_poc = max_last_poc = max_last_b_poc = prev_last_poc = 0;
	max_total_delay = max_delay = 0;

	gf_isom_set_cts_packing(import->dest, track, 1);
	has_cts_offset = 0;
	poc_diff = 0;
	min_poc = 0;
	poc_shift = 0;

	while (gf_bs_available(bs)) {
		u8 nal_hdr;
		nal_size = AVC_NextStartCode(bs);

		if (nal_size>max_size) {
			buffer = realloc(buffer, sizeof(char)*nal_size);
			max_size = nal_size;
		}
		gf_bs_read_data(bs, buffer, nal_size);
		gf_bs_seek(bs, nal_start);

		nal_hdr = gf_bs_read_u8(bs);
		nal_type = nal_hdr & 0x1F;

		copy_size = flush_sample = 0;
		switch (AVC_ParseNALU(bs, nal_hdr, &avc)) {
		case 1:
			flush_sample = 1;
			break;
		case -1:
			e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Error parsing NAL unit");
			goto exit;
		default:
			break;
		}

		if (AVC_NALUIsSlice(nal_type)) {
			copy_size = nal_size;
			switch (avc.s_info.slice_type) {
			case GF_AVC_TYPE_P: case GF_AVC_TYPE2_P: nb_p++; break;
			case GF_AVC_TYPE_I: case GF_AVC_TYPE2_I: nb_i++; break;
			case GF_AVC_TYPE_B: case GF_AVC_TYPE2_B: nb_b++; break;
			case GF_AVC_TYPE_SP: case GF_AVC_TYPE2_SP: nb_sp++; break;
			case GF_AVC_TYPE_SI: case GF_AVC_TYPE2_SI: nb_si++; break;
			}
		} else {
			switch (nal_type) {
			case GF_AVC_NALU_SEQ_PARAM:
				idx = AVC_ReadSeqInfo(bs, &avc, NULL);
				if (idx<0) {
					e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Error parsing SeqInfo");
					goto exit;
				}
				if (avc.sps[idx].status==1) {
					avc.sps[idx].status = 2;
					avccfg->configurationVersion = 1;
					avccfg->profile_compatibility = avc.sps[idx].prof_compat;
					avccfg->AVCProfileIndication = avc.sps[idx].profile_idc;
					avccfg->AVCLevelIndication = avc.sps[idx].level_idc;
					slc = malloc(sizeof(GF_AVCConfigSlot));
					slc->size = nal_size;
					slc->data = malloc(sizeof(char)*slc->size);
					memcpy(slc->data, buffer, sizeof(char)*slc->size);
					gf_list_add(avccfg->sequenceParameterSets, slc);
					if (!idx) {
						gf_import_message(import, GF_OK, "AVC-H264 import - frame size %d x %d at %02.4f FPS", avc.sps[idx].width, avc.sps[idx].height, FPS);
					}
					if ((max_w <= avc.sps[idx].width) && (max_h <= avc.sps[idx].height)) {
						max_w = avc.sps[idx].width;
						max_h = avc.sps[idx].height;
					}
				}
				break;
			case GF_AVC_NALU_PIC_PARAM:
				idx = AVC_ReadPictParamSet(bs, &avc);
				if (idx<0) {
					e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Error parsing Picture Param");
					goto exit;
				}
				if (avc.pps[idx].status==1) {
					avc.pps[idx].status = 2;
					slc = malloc(sizeof(GF_AVCConfigSlot));
					slc->size = nal_size;
					slc->data = malloc(sizeof(char)*slc->size);
					memcpy(slc->data, buffer, sizeof(char)*slc->size);
					gf_list_add(avccfg->pictureParameterSets, slc);
				}
				break;
			case GF_AVC_NALU_SEI:
				copy_size = AVC_ReformatSEI_NALU(buffer, nal_size, &avc);
				if (copy_size) nb_sei++;
				break;
			/*remove*/
			case GF_AVC_NALU_ACCESS_UNIT:
			case GF_AVC_NALU_FILLER_DATA:
			case GF_AVC_NALU_END_OF_SEQ:
			case GF_AVC_NALU_END_OF_STREAM:
				break;
			default:
				gf_import_message(import, GF_OK, "WARNING: NAL Unit type %d not handled - adding", nal_type);
				copy_size = nal_size;
				break;
			}
		}
		if (!nal_size) break;

		if (flush_sample && sample_data) {
			GF_ISOSample *samp = gf_isom_sample_new();
			samp->DTS = dts_inc*cur_samp;
			samp->IsRAP = sample_is_rap;
			gf_bs_get_content(sample_data, (unsigned char **) &samp->data, &samp->dataLength);
			gf_bs_del(sample_data);
			sample_data = NULL;
			/*CTS recomuting is much trickier than with MPEG-4 ASP due to b-slice used as references - we therefore 
			store the poc as the CTS offset and update the whole table at the end*/
			samp->CTS_Offset = last_poc - poc_shift;
			assert(samp->CTS_Offset>=0);
			gf_isom_add_sample(import->dest, track, di, samp);
			gf_isom_sample_del(&samp);
			cur_samp++;
			gf_import_progress(import, (u32) nal_start, tot_size);
			first_nal = 1;

			if (min_poc > last_poc) 
				min_poc = last_poc;
		}

		if (copy_size) {
			if ((size_length<32) && ( (u32) (1<<size_length)-1 < copy_size)) {
				u32 diff_size = 8;
				while ((size_length<32) && ( (u32) (1<<(size_length+diff_size))-1 < copy_size)) diff_size+=8;

				gf_import_message(import, GF_OK, "Adjusting AVC SizeLength to %d bits", size_length+diff_size);
				avc_rewrite_samples(import->dest, track, size_length, size_length+diff_size);
				
				/*rewrite current sample*/
				if (sample_data) {
					char *sd;
					u32 sd_l;
					GF_BitStream *prev_sd;
					gf_bs_get_content(sample_data, (unsigned char **) &sd, &sd_l);
					gf_bs_del(sample_data);
					sample_data = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
					prev_sd = gf_bs_new(sd, sd_l, GF_BITSTREAM_READ);
					while (gf_bs_available(prev_sd)) {
						char *buf;
						u32 s = gf_bs_read_int(prev_sd, size_length);
						gf_bs_write_int(sample_data, s, size_length+diff_size);
						buf = malloc(sizeof(char)*s);
						gf_bs_read_data(prev_sd, buf, s);
						gf_bs_write_data(sample_data, buf, s);
						free(buf);
					}
					gf_bs_del(prev_sd);
					free(sd);
				}
				size_length+=diff_size;

			}
			if (!sample_data) sample_data = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			gf_bs_write_int(sample_data, copy_size, size_length);
			gf_bs_write_data(sample_data, buffer, copy_size);
			if (AVC_NALUIsSlice(nal_type)) {
				if (first_nal) {
					first_nal = 0;
					/*we only indicate TRUE IDRs for sync samples (cf AVC file format spec).
					SEI recovery should be used to build sampleToGroup & RollRecovery tables*/
					avc.sei.recovery_point.valid = 0;

					sample_is_rap = AVC_SliceIsIDR(&avc);
				}
				slice_is_ref = (avc.s_info.nal_unit_type==GF_AVC_NALU_IDR_SLICE);
				if (slice_is_ref) nb_idr++;

				if (avc.s_info.poc<poc_shift) {
					u32 j;
					for (j=ref_frame; j<=cur_samp; j++) {
						GF_ISOSample *samp = gf_isom_get_sample_info(import->dest, track, j, NULL, NULL);
						samp->CTS_Offset += poc_shift;
						samp->CTS_Offset -= avc.s_info.poc;
						gf_isom_modify_cts_offset(import->dest, track, j, samp->CTS_Offset);
						gf_isom_sample_del(&samp);
					}
					poc_shift = avc.s_info.poc;
				}

				/*if #pics, compute smallest POC increase*/
				if (avc.s_info.poc != last_poc) {
					if (!poc_diff || (poc_diff > abs(avc.s_info.poc-last_poc))) 
						poc_diff = abs(avc.s_info.poc-last_poc);
					last_poc = avc.s_info.poc;
				}
				/*ref slice, reset poc*/
				if (slice_is_ref) {
					ref_frame = cur_samp+1;
					max_last_poc = last_poc = max_last_b_poc = 0;
					pred_frame = 0;
					b_frames = 0;
					max_delay = 0;
					poc_shift = 0;
				}
				/*strictly less - this is a new P slice*/
				else if (max_last_poc<last_poc) {
					max_delay = 0;
					max_last_b_poc = 0;
					prev_last_poc = max_last_poc;
					max_last_poc = last_poc;
					b_frames = 0;
					max_delay = 0;
				}
				/*stricly greater*/
				else if (max_last_poc>last_poc) {
					/*need to store TS offsets*/
					has_cts_offset = 1;
					switch (avc.s_info.slice_type) {
					case GF_AVC_TYPE_B: 
					case GF_AVC_TYPE2_B:
						if (!max_last_b_poc) {
							b_frames ++;
							max_last_b_poc = last_poc;
						}
						/*we had a B-slice reference*/
						else if (last_poc<max_last_b_poc) {
							b_frames ++;
							if (max_delay<b_frames) {
								max_delay = b_frames;
								if (max_total_delay<max_delay) max_total_delay = max_delay;
							}
							b_frames = 0;
						}
						/*if same poc than last max, this is a B-slice*/

						max_last_b_poc = last_poc;
						break;
					}
				}
			}
		}

		gf_bs_align(bs);
		nal_end = gf_bs_get_position(bs);
		if (nal_end != nal_start + nal_size) gf_bs_seek(bs, nal_start + nal_size);

		if (!gf_bs_available(bs)) break;
		if (duration && (dts_inc*cur_samp > duration)) break;
		if (import->flags & GF_IMPORT_DO_ABORT) break;

		/*consume next start code*/
		nal_start = AVC_IsStartCode(bs);
		assert(nal_start);
		nal_start = gf_bs_get_position(bs);
	}

	/*final flush*/
	if (sample_data) {
		GF_ISOSample *samp = gf_isom_sample_new();
		samp->DTS = dts_inc*cur_samp;
		samp->IsRAP = sample_is_rap;
		samp->CTS_Offset = last_poc - poc_shift;
		gf_bs_get_content(sample_data, (unsigned char **) &samp->data, &samp->dataLength);
		gf_bs_del(sample_data);
		sample_data = NULL;
		gf_isom_add_sample(import->dest, track, di, samp);
		gf_isom_sample_del(&samp);
		gf_import_progress(import, (u32) cur_samp, cur_samp+1);
		cur_samp++;
	}

	/*recompute all CTS offsets*/
	if (has_cts_offset) {
		u32 i, last_cts_samp;
		u64 last_dts, max_cts;
		if (!poc_diff) poc_diff = 1;
		/*no b-frame references, no need to cope with negative poc*/
		if (!max_total_delay) {
			min_poc=0;
			max_total_delay = 1;
		}
		cur_samp = gf_isom_get_sample_count(import->dest, track);
		min_poc *= -1;
		last_dts = 0;
		max_cts = 0;
		last_cts_samp = 0;

		for (i=0; i<cur_samp; i++) {
			/*not using descIdx and data_offset will only fecth DTS, CTS and RAP which is all we need*/
			GF_ISOSample *samp = gf_isom_get_sample_info(import->dest, track, i+1, NULL, NULL);
			/*poc re-init (RAP and POC to 0, otherwise that's SEI recovery), update base DTS*/
			if (samp->IsRAP /*&& !samp->CTS_Offset*/) last_dts = samp->DTS;

			/*CTS offset is frame POC (refers to last IDR)*/
			samp->CTS_Offset = (min_poc + (s32) samp->CTS_Offset) * dts_inc/poc_diff + (u32) (last_dts + max_total_delay*dts_inc - samp->DTS);

			if (max_cts < samp->DTS + samp->CTS_Offset) {
				max_cts = samp->DTS + samp->CTS_Offset;
				last_cts_samp = i;
			}
			/*this should never happen, however some streams seem to do weird POC increases (cf sorenson streams, last 2 frames),
			this should hopefully take care of some bugs and ensure proper CTS...*/
			if ((s32)samp->CTS_Offset<0) {
				u32 j, k;
				samp->CTS_Offset = 0;
				gf_isom_modify_cts_offset(import->dest, track, i+1, samp->CTS_Offset);
				for (j=last_cts_samp; j<i; j++) {
					GF_ISOSample *asamp = gf_isom_get_sample_info(import->dest, track, j+1, NULL, NULL);
					for (k=j+1; k<=i; k++) {
						GF_ISOSample *bsamp = gf_isom_get_sample_info(import->dest, track, k+1, NULL, NULL);
						if (asamp->CTS_Offset+asamp->DTS==bsamp->CTS_Offset+bsamp->DTS) {
							max_cts += dts_inc;
							bsamp->CTS_Offset = (u32) (max_cts - bsamp->DTS);
							gf_isom_modify_cts_offset(import->dest, track, k+1, bsamp->CTS_Offset);
						}
						gf_isom_sample_del(&bsamp);
					}
					gf_isom_sample_del(&asamp);
				}
				max_cts = samp->DTS + samp->CTS_Offset;
			} else {
				gf_isom_modify_cts_offset(import->dest, track, i+1, samp->CTS_Offset);
			}
			gf_isom_sample_del(&samp);
		}
		/*and repack table*/
		gf_isom_set_cts_packing(import->dest, track, 0);
	} else {
		gf_isom_remove_cts_info(import->dest, track);
	}

	gf_import_progress(import, (u32) cur_samp, cur_samp);

	gf_isom_set_visual_info(import->dest, track, di, max_w, max_h);
	avccfg->nal_unit_size = size_length/8;
	gf_isom_avc_config_update(import->dest, track, 1, avccfg);
	gf_media_update_par(import->dest, track);
	MP4T_RecomputeBitRate(import->dest, track);

	gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_VISUAL, 0x15);
	gf_isom_modify_alternate_brand(import->dest, GF_ISOM_BRAND_AVC1, 1);

	if (nb_sp || nb_si) {
		gf_import_message(import, GF_OK, "Import results: %d samples - Slices: %d I %d P %d B %d SP %d SI - %d SEI - %d IDR", 
			cur_samp, nb_i, nb_p, nb_b, nb_sp, nb_si, nb_sei, nb_idr);
	} else {
		gf_import_message(import, GF_OK, "Import results: %d samples - Slices: %d I %d P %d B - %d SEI - %d IDR", 
			cur_samp, nb_i, nb_p, nb_b, nb_sei, nb_idr);
	}

	if (max_total_delay>1) {
		gf_import_message(import, GF_OK, "\tStream uses B-slice references - max frame delay %d", max_total_delay);
	}

	/*rewrite ESD*/
	if (import->esd) {
		if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig*) gf_odf_desc_new(GF_ODF_SLC_TAG);
		import->esd->slConfig->predefined = 2;
		import->esd->slConfig->timestampResolution = timescale;
		if (import->esd->decoderConfig) gf_odf_desc_del((GF_Descriptor *)import->esd->decoderConfig);
		import->esd->decoderConfig = gf_isom_get_decoder_config(import->dest, track, 1);
		gf_isom_change_mpeg4_description(import->dest, track, 1, import->esd);
	}

exit:
	if (sample_data) gf_bs_del(sample_data);
	gf_odf_avc_cfg_del(avccfg);
	free(buffer);
	gf_bs_del(bs);
	fclose(mdia);
	return e;
}
#define OGG_BUFFER_SIZE 4096

Bool OGG_ReadPage(FILE *f_in, ogg_sync_state *oy, ogg_page *oggpage)
{
	if (feof(f_in)) return 0;
	while (ogg_sync_pageout(oy, oggpage ) != 1 ) {
		char *buffer = ogg_sync_buffer(oy, OGG_BUFFER_SIZE);
		u32 bytes = fread(buffer, 1, OGG_BUFFER_SIZE, f_in);
		ogg_sync_wrote(oy, bytes);
		if (feof(f_in)) return 1;
	}
	return 1;
}

static u32 get_ogg_serial_no_for_stream(char *fileName, u32 stream_num, Bool is_video)
{
    ogg_sync_state oy;
	u32 track, serial_no;
	ogg_page oggpage;
	ogg_packet oggpacket;
	ogg_stream_state os;
	FILE *f_in;

	/*means first one*/
	if (!stream_num) return 0;

	f_in = gf_f64_open(fileName, "rb");
	if (!f_in) return 0;

	track = 0;
	serial_no = 0;
	ogg_sync_init(&oy);
	while (1) {
		if (!OGG_ReadPage(f_in, &oy, &oggpage)) break;
		if (!ogg_page_bos(&oggpage)) break;
		track ++;
		if (track != stream_num) continue;

		serial_no = ogg_page_serialno(&oggpage);
		ogg_stream_init(&os, serial_no);
		ogg_stream_pagein(&os, &oggpage);
		ogg_stream_packetpeek(&os, &oggpacket);

		if (is_video && (oggpacket.bytes >= 7) && !strncmp(&oggpacket.packet[1], "theora", 6)) {
			ogg_stream_clear(&os);		
			break;
		}
		if (!is_video && (oggpacket.bytes >= 7) && !strncmp(&oggpacket.packet[1], "vorbis", 6)) {
			ogg_stream_clear(&os);		
			break;
		}
		ogg_stream_clear(&os);
		serial_no = 0;
	}
	ogg_sync_clear(&oy);
	fclose(f_in);
	return serial_no;
}

GF_Err gf_import_ogg_video(GF_MediaImporter *import)
{
	GF_Err e;
    ogg_sync_state oy;
	u32 di, track, done, tot_size, duration;
	u32 w, h, fps_num, fps_den, keyframe_freq_force, theora_kgs, flag, dts_inc, timescale;
	Double FPS;
	Bool destroy_esd, go;
	u32 serial_no, sno, num_headers;
	ogg_packet oggpacket;
	ogg_page oggpage;
	ogg_stream_state os;
	oggpack_buffer opb;
	GF_BitStream *bs;
	FILE *f_in;
	GF_ISOSample *samp;

	/*assume audio or simple AV file*/
	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		f_in = gf_f64_open(import->in_name, "rb");
		if (!f_in) return GF_URL_ERROR;

		import->nb_tracks = 0;
		go = 1;
	    ogg_sync_init(&oy);
		while (go) {
			if (!OGG_ReadPage(f_in, &oy, &oggpage)) break;

			if (!ogg_page_bos(&oggpage)) {
				go = 0;
				continue;
			}
			serial_no = ogg_page_serialno(&oggpage);
			ogg_stream_init(&os, serial_no);
			ogg_stream_pagein(&os, &oggpage);
			ogg_stream_packetpeek(&os, &oggpacket);

			import->tk_info[import->nb_tracks].track_num = import->nb_tracks+1;
			if ((oggpacket.bytes >= 7) && !strncmp(&oggpacket.packet[1], "theora", 6)) {
				import->tk_info[import->nb_tracks].type = GF_ISOM_MEDIA_VISUAL;
				import->tk_info[import->nb_tracks].flags = GF_IMPORT_OVERRIDE_FPS;

				bs = gf_bs_new(oggpacket.packet, oggpacket.bytes, GF_BITSTREAM_READ);
				gf_bs_read_int(bs, 80);
				import->tk_info[import->nb_tracks].width = gf_bs_read_u16(bs) << 4;
				import->tk_info[import->nb_tracks].height = gf_bs_read_u16(bs) << 4;
				gf_bs_read_int(bs, 64);
				fps_num = gf_bs_read_u32(bs);
				fps_den = gf_bs_read_u32(bs);
				gf_bs_del(bs);
				import->tk_info[import->nb_tracks].FPS = fps_num;
				import->tk_info[import->nb_tracks].FPS /= fps_den;
				import->tk_info[import->nb_tracks].media_type = GF_FOUR_CHAR_INT('t','h','e','o');
			} else if ((oggpacket.bytes >= 7) && !strncmp(&oggpacket.packet[1], "vorbis", 6)) {
				import->tk_info[import->nb_tracks].type = GF_ISOM_MEDIA_AUDIO;
				import->tk_info[import->nb_tracks].flags = 0;
			}
			ogg_stream_clear(&os);
			import->nb_tracks++;
		}
	    ogg_sync_clear(&oy);
		fclose(f_in);
		return GF_OK;
	}

	if (import->flags & GF_IMPORT_USE_DATAREF) return gf_import_message(import, GF_NOT_SUPPORTED, "Cannot use data referencing with OGG files");


	sno = get_ogg_serial_no_for_stream(import->in_name, import->trackID, 1);
	/*not our stream*/
	if (!sno && import->trackID) return GF_OK;

	f_in = gf_f64_open(import->in_name, "rb");
	if (!f_in) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	e = GF_OK;
	done = 0;
	fseek(f_in, 0, SEEK_END);
	tot_size = ftell(f_in);
	fseek(f_in, 0, SEEK_SET);


	destroy_esd = 0;
	samp = gf_isom_sample_new();
	
	/*avoids gcc warnings*/
	duration = 0;
	FPS = 0;
	num_headers = w = h = track = 0;

    ogg_sync_init(&oy);

	bs = NULL;
	serial_no = 0;
	go = 1;
	while (go) {
		if (!OGG_ReadPage(f_in, &oy, &oggpage)) break;

		if (ogg_page_bos(&oggpage)) {
			if (serial_no) continue;
			serial_no = ogg_page_serialno(&oggpage);
			ogg_stream_init(&os, serial_no);
			ogg_stream_pagein(&os, &oggpage);
			ogg_stream_packetpeek(&os, &oggpacket);

			/*not our stream*/
			if (sno && (sno!=serial_no)) {
				ogg_stream_clear(&os);
				serial_no = 0;
				continue;
			}
			if ((oggpacket.bytes < 7) || strncmp(&oggpacket.packet[1], "theora", 6)) {
				ogg_stream_clear(&os);
				serial_no = 0;
				continue;
			}
			/*that's ogg-theora*/
			bs = gf_bs_new(oggpacket.packet, oggpacket.bytes, GF_BITSTREAM_READ);
			gf_bs_read_int(bs, 80);
			w = gf_bs_read_u16(bs) << 4;
			h = gf_bs_read_u16(bs) << 4;
			gf_bs_read_int(bs, 64);
			fps_num = gf_bs_read_u32(bs);
			fps_den = gf_bs_read_u32(bs);
			gf_bs_read_int(bs, 80);
			gf_bs_read_int(bs, 6);
			keyframe_freq_force = 1 << gf_bs_read_int(bs, 5);
			theora_kgs = 0;
			keyframe_freq_force--;
			while (keyframe_freq_force) {
				theora_kgs ++;
				keyframe_freq_force >>= 1;
			}
			gf_bs_del(bs);

		    FPS = ((Double)fps_num) / fps_den;
			/*note that we don't rewrite theora headers (just like in MPEG-4 video, systems timing overrides stream one)*/
			if (import->video_fps) FPS = import->video_fps;
			num_headers = 0;

			gf_import_message(import, GF_OK, "OGG Theora import - %2.4f FPS - Resolution %d x %d", FPS, w, h);
			bs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			continue;
		}
		/*FIXME - check end of stream for concatenated files?*/

		/*not our stream*/
        if (ogg_stream_pagein(&os, &oggpage) != 0) continue;
	


		while (ogg_stream_packetout(&os, &oggpacket ) > 0 ) {
			if (num_headers<3) {
				if(!w || !h) {
					e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Couldn't find Theora main header");
					goto exit;
				}
				/*copy headers*/
				gf_bs_write_u16(bs, oggpacket.bytes);
				gf_bs_write_data(bs, oggpacket.packet, oggpacket.bytes);
				num_headers++;

				/*let's go, create the track*/
				if (num_headers==3) {
					if (!import->esd) {
						destroy_esd = 1;
						import->esd = gf_odf_desc_esd_new(0);
					}
					get_video_timing(FPS, &timescale, &dts_inc);
					track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_VISUAL, timescale);
					if (!track) goto exit;
					gf_isom_set_track_enabled(import->dest, track, 1);
					if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
					import->final_trackID = import->esd->ESID;
					if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
					if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
					import->esd->slConfig->timestampResolution = timescale;
					if (!import->esd->decoderConfig->decoderSpecificInfo) import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
					gf_bs_get_content(bs, (unsigned char **) &import->esd->decoderConfig->decoderSpecificInfo->data, &import->esd->decoderConfig->decoderSpecificInfo->dataLength);
					gf_bs_del(bs);
					bs = NULL;
					import->esd->decoderConfig->streamType = GF_STREAM_VISUAL;
					import->esd->decoderConfig->objectTypeIndication = 0xDF;

					e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, NULL, NULL, &di);
					if (e) goto exit;
					gf_isom_set_visual_info(import->dest, track, di, w, h);

					{
						Double d = import->duration;
						d *= import->esd->slConfig->timestampResolution;
						d /= 1000;
						duration = (u32) d;
					}
				}
				
				continue;
			}
			
			/*we don't need adedicated parser for theora, just check it's a theora frame and get its key type 
			WATCHOUT theora bitsteram is in BE*/
			oggpackB_readinit(&opb, oggpacket.packet, oggpacket.bytes);
			flag = oggpackB_read(&opb, 1);
			if (flag==0) {
				/*add packet*/
				samp->IsRAP = oggpackB_read(&opb, 1) ? 0 : 1;
				samp->data = oggpacket.packet;
				samp->dataLength = oggpacket.bytes;
				e = gf_isom_add_sample(import->dest, track, di, samp);
				samp->DTS += dts_inc;
			}

			gf_import_progress(import, done, tot_size);
			done += oggpacket.bytes;
			if ((duration && (samp->DTS > duration) ) || (import->flags & GF_IMPORT_DO_ABORT)) {
				go = 0;
				break;
			}
		}
	}
	gf_import_progress(import, tot_size, tot_size);

	if (!serial_no) {
		gf_import_message(import, GF_OK, "OGG: No supported video found");
	} else {
		MP4T_RecomputeBitRate(import->dest, track);

		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_VISUAL, 0xFE);
		gf_import_progress(import, tot_size, tot_size);
	}
	
exit:
	if (bs) gf_bs_del(bs);
	samp->data = NULL;
	gf_isom_sample_del(&samp);
    ogg_sync_clear(&oy);
	if (serial_no) ogg_stream_clear(&os);
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	fclose(f_in);
	return e;
}

GF_Err gf_import_ogg_audio(GF_MediaImporter *import)
{
	GF_Err e;
    ogg_sync_state oy;
	u32 di, track, done, tot_size, duration;
	s32 block_size;
	GF_ISOSample *samp;
	Bool destroy_esd, go;
	u32 serial_no, sno, num_headers;
	ogg_packet oggpacket;
	ogg_page oggpage;
	ogg_stream_state os;
	GF_VorbisParser vp;
	GF_BitStream *vbs;
	FILE *f_in;

	if (import->flags & GF_IMPORT_PROBE_ONLY) return GF_OK;

	if (import->flags & GF_IMPORT_USE_DATAREF) return gf_import_message(import, GF_NOT_SUPPORTED, "Cannot use data referencing with OGG files");

	sno = get_ogg_serial_no_for_stream(import->in_name, import->trackID, 0);
	/*not our stream*/
	if (!sno && import->trackID) return GF_OK;

	f_in = gf_f64_open(import->in_name, "rb");
	if (!f_in) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	e = GF_OK;

	done = 0;
	fseek(f_in, 0, SEEK_END);
	tot_size = ftell(f_in);
	fseek(f_in, 0, SEEK_SET);

	destroy_esd = 0;
	samp = gf_isom_sample_new();
	/*avoids gcc warnings*/
	track = num_headers = duration = 0;

    ogg_sync_init(&oy);

	vbs = NULL;
	serial_no = 0;
	go = 1;
	while (go) {
		if (!OGG_ReadPage(f_in, &oy, &oggpage)) break;

		if (ogg_page_bos(&oggpage)) {
			if (serial_no) continue;
			serial_no = ogg_page_serialno(&oggpage);
			ogg_stream_init(&os, serial_no);
			ogg_stream_pagein(&os, &oggpage);
			ogg_stream_packetpeek(&os, &oggpacket);
			/*not our stream*/
			if (sno && (sno!=serial_no)) {
				ogg_stream_clear(&os);
				serial_no = 0;
				continue;
			}
			if ((oggpacket.bytes < 7) || strncmp(&oggpacket.packet[1], "vorbis", 6)) {
				ogg_stream_clear(&os);
				serial_no = 0;
				continue;
			}
			num_headers = 0;
			memset(&vp, 0, sizeof(GF_VorbisParser));
			vbs = gf_bs_new(NULL, 0, GF_BITSTREAM_WRITE);
			continue;
		}
		/*FIXME - check end of stream for concatenated files?*/

		/*not our stream*/
        if (ogg_stream_pagein(&os, &oggpage) != 0) continue;
	


		while (ogg_stream_packetout(&os, &oggpacket ) > 0 ) {
			if (num_headers<3) {
				if (!gf_vorbis_parse_header(&vp, oggpacket.packet, oggpacket.bytes)) {
					e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Corrupted OGG Vorbis header");
					goto exit;					
				}

				/*copy headers*/
				gf_bs_write_u16(vbs, oggpacket.bytes);
				gf_bs_write_data(vbs, oggpacket.packet, oggpacket.bytes);
				num_headers++;

				/*let's go, create the track*/
				if (num_headers==3) {
					if (!vp.is_init) {
						e = gf_import_message(import, GF_NON_COMPLIANT_BITSTREAM, "Corrupted OGG Vorbis headers found");
						goto exit;					
					}

					gf_import_message(import, GF_OK, "OGG Vorbis import - sample rate %d - %d channel%s", vp.sample_rate, vp.channels, (vp.channels>1) ? "s" : "");

					if (!import->esd) {
						destroy_esd = 1;
						import->esd = gf_odf_desc_esd_new(0);
					}
					track = gf_isom_new_track(import->dest, import->esd->ESID, GF_ISOM_MEDIA_AUDIO, vp.sample_rate);
					if (!track) goto exit;
					gf_isom_set_track_enabled(import->dest, track, 1);
					if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
					import->final_trackID = import->esd->ESID;
					if (!import->esd->decoderConfig) import->esd->decoderConfig = (GF_DecoderConfig *) gf_odf_desc_new(GF_ODF_DCD_TAG);
					if (!import->esd->slConfig) import->esd->slConfig = (GF_SLConfig *) gf_odf_desc_new(GF_ODF_SLC_TAG);
					import->esd->slConfig->timestampResolution = vp.sample_rate;
					if (!import->esd->decoderConfig->decoderSpecificInfo) import->esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *) gf_odf_desc_new(GF_ODF_DSI_TAG);
					gf_bs_get_content(vbs, (unsigned char **) &import->esd->decoderConfig->decoderSpecificInfo->data, &import->esd->decoderConfig->decoderSpecificInfo->dataLength);
					gf_bs_del(vbs);
					vbs = NULL;
					import->esd->decoderConfig->streamType = GF_STREAM_AUDIO;
					import->esd->decoderConfig->avgBitrate = vp.avg_r;
					import->esd->decoderConfig->maxBitrate = (vp.max_r>0) ? vp.max_r : vp.avg_r;
					import->esd->decoderConfig->objectTypeIndication = GPAC_OGG_MEDIA_OTI;

					e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, NULL, NULL, &di);
					if (e) goto exit;
					gf_isom_set_audio_info(import->dest, track, di, vp.sample_rate, (vp.channels>1) ? 2 : 1, 16);

					{
						Double d = import->duration;
						d *= vp.sample_rate;
						d /= 1000;
						duration = (u32) d;
					}
				}
				continue;
			}
		
			block_size = gf_vorbis_check_frame(&vp, oggpacket.packet, oggpacket.bytes);
			if (!block_size) continue;

			/*add packet*/
			samp->IsRAP = 1;
			samp->data = oggpacket.packet;
			samp->dataLength = oggpacket.bytes;
			e = gf_isom_add_sample(import->dest, track, di, samp);
			samp->DTS += block_size;

			gf_import_progress(import, done, tot_size);
			done += oggpacket.bytes;
			if ((duration && (samp->DTS > duration) ) || (import->flags & GF_IMPORT_DO_ABORT)) {
				go = 0;
				break;
			}
		}
	}
	gf_import_progress(import, tot_size, tot_size);

	if (!serial_no) {
		gf_import_message(import, GF_OK, "OGG: No supported audio found");
	} else {
		samp->data = NULL;
		gf_isom_set_pl_indication(import->dest, GF_ISOM_PL_AUDIO, 0xFE);
		gf_import_progress(import, tot_size, tot_size);

		MP4T_RecomputeBitRate(import->dest, track);
	}
	
exit:
	gf_isom_sample_del(&samp);
	if (vbs) gf_bs_del(vbs);
	if (serial_no) ogg_stream_clear(&os);
    ogg_sync_clear(&oy);
	if (import->esd && destroy_esd) {
		gf_odf_desc_del((GF_Descriptor *) import->esd);
		import->esd = NULL;
	}
	fclose(f_in);
	return e;
}

GF_Err gf_import_raw_unit(GF_MediaImporter *import)
{
	GF_Err e;
	GF_ISOSample *samp;
	u32 mtype, track, di, timescale;
	FILE *src;

	if (import->flags & GF_IMPORT_PROBE_ONLY) {
		import->flags |= GF_IMPORT_USE_DATAREF;
		return GF_OK;
	}

	if (!import->esd || !import->esd->decoderConfig) {
		return gf_import_message(import, GF_BAD_PARAM, "Raw stream needs ESD and DecoderConfig for import");
	}

	src = fopen(import->in_name, "rb");
	if (!src) return gf_import_message(import, GF_URL_ERROR, "Opening file %s failed", import->in_name);

	switch (import->esd->decoderConfig->streamType) {
	case GF_STREAM_SCENE: mtype = GF_ISOM_MEDIA_SCENE; break;
	case GF_STREAM_VISUAL: mtype = GF_ISOM_MEDIA_VISUAL; break;
	case GF_STREAM_AUDIO: mtype = GF_ISOM_MEDIA_AUDIO; break;
	case GF_STREAM_TEXT: mtype = GF_ISOM_MEDIA_TEXT; break;
	case GF_STREAM_MPEG7: mtype = GF_ISOM_MEDIA_MPEG7; break;
	case GF_STREAM_IPMP: mtype = GF_ISOM_MEDIA_IPMP; break;
	case GF_STREAM_OCI: mtype = GF_ISOM_MEDIA_OCI; break;
	case GF_STREAM_MPEGJ: mtype = GF_ISOM_MEDIA_MPEGJ; break;
	case GF_STREAM_INTERACT: mtype = GF_STREAM_SCENE; break;
	/*not sure about this one...*/
	case GF_STREAM_IPMP_TOOL: mtype = GF_ISOM_MEDIA_IPMP; break;
	/*not sure about this one...*/
	case GF_STREAM_FONT: mtype = GF_ISOM_MEDIA_MPEGJ; break;
	default: mtype = GF_ISOM_MEDIA_ESM;
	}
	timescale = import->esd->slConfig ? import->esd->slConfig->timestampResolution : 1000;
	track = gf_isom_new_track(import->dest, import->esd->ESID, mtype, timescale);
	if (!track) {
		e = gf_isom_last_error(import->dest);
		goto exit;
	}
	gf_isom_set_track_enabled(import->dest, track, 1);
	if (!import->esd->ESID) import->esd->ESID = gf_isom_get_track_id(import->dest, track);
	import->final_trackID = import->esd->ESID;
	e = gf_isom_new_mpeg4_description(import->dest, track, import->esd, (import->flags & GF_IMPORT_USE_DATAREF) ? import->in_name : NULL, NULL, &di);
	if (e) goto exit;

	gf_import_message(import, GF_OK, "Raw Access Unit import (StreamType %s)", gf_odf_stream_type_name(import->esd->decoderConfig->streamType));

	samp = gf_isom_sample_new();
	fseek(src, 0, SEEK_END);
	samp->dataLength = ftell(src);
	fseek(src, 0, SEEK_SET);
	samp->IsRAP = 1;
	samp->data = malloc(sizeof(char)*samp->dataLength);
	fread(samp->data, samp->dataLength, 1, src);
	e = gf_isom_add_sample(import->dest, track, di, samp);
	gf_isom_sample_del(&samp);
	MP4T_RecomputeBitRate(import->dest, track);
exit:
	fclose(src);
	return e;
}

GF_Err gf_media_import(GF_MediaImporter *importer)
{
	GF_Err gf_import_timed_text(GF_MediaImporter *import);
	GF_Err e;
	char *ext;
	char *fmt = "";
	if (!importer || (!importer->dest && (importer->flags!=GF_IMPORT_PROBE_ONLY)) || (!importer->in_name && !importer->orig) ) return GF_BAD_PARAM;

	if (importer->orig) return gf_import_isomedia(importer);

	ext = strrchr(importer->in_name, '.');
	
	if (importer->streamFormat) fmt = importer->streamFormat;

	if (!strnicmp(ext, ".avi", 4) || !stricmp(fmt, "AVI") ) {
		e = gf_import_avi_video(importer);
		if (e) return e;
		return gf_import_avi_audio(importer);
	}
	else if (!strnicmp(ext, ".ogg", 4) || !stricmp(fmt, "OGG")) {
		e = gf_import_ogg_video(importer);
		if (e) return e;
		return gf_import_ogg_audio(importer);
	}
	else if (!strnicmp(ext, ".mpg", 4) || !strnicmp(ext, ".mpeg", 5) 
		|| !strnicmp(ext, ".vob", 4) || !strnicmp(ext, ".vcd", 4) || !strnicmp(ext, ".svcd", 5)
		|| !stricmp(fmt, "MPEG1") || !stricmp(fmt, "MPEG-PS")  || !stricmp(fmt, "MPEG2-PS") 
		) {
		e = gf_import_mpeg_ps_video(importer);
		if (e) return e;
		return gf_import_mpeg_ps_audio(importer);
	}

	else if (!strnicmp(ext, ".mp3", 4) || !stricmp(fmt, "MP3") || !stricmp(fmt, "MPEG-AUDIO") ) 
		return gf_import_mp3(importer);
	else if (!strnicmp(ext, ".media", 5) || !strnicmp(ext, ".info", 5) || !strnicmp(ext, ".nhnt", 5) || !stricmp(fmt, "NHNT") )
		return gf_import_nhnt(importer);
	else if (!strnicmp(ext, ".nhml", 5) || !stricmp(fmt, "NHML") )
		return gf_import_nhml(importer);
	else if (!strnicmp(ext, ".jpg", 4) || !strnicmp(ext, ".jpeg", 5) || !strnicmp(ext, ".png", 4) || !stricmp(fmt, "JPEG") || !stricmp(fmt, "PNG") ) 
		return gf_import_still_image(importer);
	else if (!strnicmp(ext, ".aac", 4) || !stricmp(fmt, "AAC") || !stricmp(fmt, "MPEG4-AUDIO") ) 
		return gf_import_aac_adts(importer);
	else if (!strnicmp(ext, ".amr", 4) || !strnicmp(ext, ".awb", 4) || !strnicmp(ext, ".smv", 4) || !strnicmp(ext, ".evc", 4)
		|| !stricmp(fmt, "AMR") || !stricmp(fmt, "EVRC") || !stricmp(fmt, "SMV") ) 
		return gf_import_amr_evrc_smv(importer);
	else if (!strnicmp(ext, ".qcp", 4) || !stricmp(fmt, "QCELP") ) 
		return gf_import_qcp(importer);
	else if (!strnicmp(ext, ".cmp", 4) || !strnicmp(ext, ".m4v", 4) || !stricmp(fmt, "CMP") || !stricmp(fmt, "MPEG4-Video") ) 
		return gf_import_cmp(importer);
	else if (!strnicmp(ext, ".263", 4) || !strnicmp(ext, ".h263", 5) || !stricmp(fmt, "H263") ) 
		return gf_import_h263(importer);
	else if (!strnicmp(ext, ".h264", 5) || !strnicmp(ext, ".264", 4)
		|| !strnicmp(ext, ".h26L", 5) || !strnicmp(ext, ".26l", 4)
		|| !stricmp(fmt, "AVC") || !stricmp(fmt, "H264") ) 
		return gf_import_h264(importer);

	else if (!strnicmp(ext, ".srt", 4) || !strnicmp(ext, ".sub", 4) || !strnicmp(ext, ".ttxt", 5) 
		|| !stricmp(fmt, "SRT") || !stricmp(fmt, "SUB") || !stricmp(fmt, "TEXT") ) 
		return gf_import_timed_text(importer);

	if (!stricmp(fmt, "RAW")) {
		return gf_import_raw_unit(importer);
	}
	/*try with MP4*/
	if (gf_isom_probe_file(importer->in_name)) {
		GF_Err res;
		importer->orig = gf_isom_open(importer->in_name, GF_ISOM_OPEN_READ, NULL);
		if (!importer->orig) return gf_isom_last_error(NULL);
		res = gf_import_isomedia(importer);
		gf_isom_delete(importer->orig);
		importer->orig = NULL;
		return res;
	}
	/*XML things*/
	else if (!strnicmp(ext, ".xml", 4) ) {
		e = gf_import_timed_text(importer);
		if (!e) return e;
	}
		
	return gf_import_message(importer, GF_NOT_SUPPORTED, "Unknown input file type");
}

GF_Err gf_media_change_par(GF_ISOFile *file, u32 track, s32 ar_num, s32 ar_den)
{
	u32 tk_w, tk_h, stype;
	GF_Err e;

	e = gf_isom_get_visual_info(file, track, 1, &tk_w, &tk_h);
	if (e) return e;

	stype = gf_isom_get_media_subtype(file, track, 1);
	if (stype==GF_ISOM_SUBTYPE_AVC_H264) {
		GF_AVCConfig *avcc = gf_isom_avc_config_get(file, track, 1);
		AVC_ChangePAR(avcc, ar_num, ar_den);
		e = gf_isom_avc_config_update(file, track, 1, avcc);
		gf_odf_avc_cfg_del(avcc);
		if (e) return e;
	} 
	else if (stype==GF_ISOM_SUBTYPE_MPEG4) {
		GF_ESD *esd = gf_isom_get_esd(file, track, 1);
		if (!esd || !esd->decoderConfig || (esd->decoderConfig->streamType!=4) || (esd->decoderConfig->objectTypeIndication!=0x20)) {
			if (esd)  gf_odf_desc_del((GF_Descriptor *) esd);
			return GF_NOT_SUPPORTED;
		}
		e = gf_m4v_rewrite_par((unsigned char **) &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength, ar_num, ar_den);
		if (!e) e = gf_isom_change_mpeg4_description(file, track, 1, esd);
		gf_odf_desc_del((GF_Descriptor *) esd);
		if (e) return e;
	} else {
		return GF_BAD_PARAM;
	}

	if ((ar_den>=0) && (ar_num>=0)) {
		if (ar_den) tk_w = tk_w * ar_num / ar_den;
		else if (ar_num) tk_h = tk_h * ar_den / ar_num;
	}
	return gf_isom_set_track_layout_info(file, track, tk_w<<16, tk_h<<16, 0, 0, 0);
}

#endif




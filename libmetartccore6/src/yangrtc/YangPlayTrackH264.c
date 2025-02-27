﻿//
// Copyright (c) 2019-2022 yanggaofeng
//
#include <yangrtc/YangPlayTrackH264.h>
#include <yangrtc/YangRtcRtcp.h>
#include <yangrtp/YangRtpConstant.h>
#include <yangrtp/YangRtpFUAPayload2.h>
#include <yangrtp/YangRtpHeader.h>
#include <yangrtp/YangRtpRawPayload.h>
#include <yangrtp/YangRtpSTAPPayload.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangEndian.h>
#include <yangavutil/video/YangMeta.h>
#include <yangrtc/YangPlayTrack.h>



void yang_create_playTrackH264(YangRtcContext *context, YangPlayTrackH264 *track) {

	track->key_frame_ts = -1;
	track->lost_sn = 0;
	track->header_sn = 0;
	track->hasRequestKeyframe = yangfalse;

	track->hasReceiveStap = -1;

	memset(track->cache_video_pkts, 0,
			YANG_RTC_RECV_BUFFER_COUNT * sizeof(YangH264PacketCache));
    track->video_buffer = (char*) calloc(1, YANG_VIDEO_ENCODE_BUFFER_LEN);
    memset(track->video_buffer, 0, YANG_VIDEO_ENCODE_BUFFER_LEN);

	memset(&track->stapData, 0, sizeof(YangRtpSTAPData));
	yang_create_stap(&track->stapData);
	track->uid = context->streamConfig->uid;
	memset(&track->videoFrame, 0, sizeof(YangFrame));

}
void yang_destroy_playTrackH264(YangPlayTrackH264 *track) {
	yang_free(track->video_buffer);
	yang_reset_h2645_stap(&track->stapData);
	yang_destroy_stap(&track->stapData);
}

void yang_playtrackH264_clear_cached_video(YangPlayTrackH264 *track) {
	for (size_t i = 0; i < YANG_RTC_RECV_BUFFER_COUNT; i++) {
		if (track->cache_video_pkts[i].in_use) {
			memset(&track->cache_video_pkts[i], 0,
					sizeof(YangH264PacketCache));
		}
	}
}

void yang_playtrackH264_copy(YangPlayTrackH264 *track, YangRtpPacket *src,
		YangH264PacketCache *pkt) {
	pkt->sn = src->header.sequence;
	pkt->end = src->header.marker;

	pkt->ts = src->header.timestamp;
	pkt->nalu_type = src->nalu_type;

	pkt->payload = src->payload;
	pkt->nb = src->nb;

	if (pkt->nalu_type == kFuA) {

		yang_init_buffer(&track->buf, src->payload, src->nb);
		yang_decode_h264_fua2(&track->buf, &pkt->fua2);
		pkt->payload = track->buf.head;
		pkt->nb = yang_buffer_left(&track->buf);
	}
}

int32_t yang_playtrackH264_put_frame_video(YangRtcContext *context,
		YangPlayTrackH264 *track, char *p, int64_t timestamp, int32_t nb) {
    if(nb>YANG_VIDEO_ENCODE_BUFFER_LEN){
        yang_error("frame size is too large.size=%d",nb);
        return Yang_Ok;
    }
	track->videoFrame.uid = track->uid;
	track->videoFrame.payload = (uint8_t*) p;
	track->videoFrame.nb = nb;
	track->videoFrame.pts = timestamp;
	yang_playtrack_receiveVideo(context,&track->videoFrame);

	return Yang_Ok;
}

int32_t yang_playtrackH264_check_frame_complete(YangPlayTrackH264 *track,
		const uint16_t start, const uint16_t end) {
	uint16_t cnt = (end - start + 1);
	uint16_t fu_s_c = 0;
	uint16_t fu_e_c = 0;
	for (uint16_t i = 0; i < cnt; ++i) {
		int32_t index = yang_cache_index((start + i));
		YangH264PacketCache *pkt = &track->cache_video_pkts[index];
		if (pkt->nalu_type == kFuA) {
			if (pkt->fua2.start) {
				++fu_s_c;
			}

			if (pkt->fua2.end) {
				++fu_e_c;
			}
		}
	}

	return fu_s_c == fu_e_c;
}

int32_t yang_playtrackH264_find_next_lost_sn(YangPlayTrackH264 *track,
		uint16_t current_sn, uint16_t *end_sn) {
	uint32_t last_ts =
			track->cache_video_pkts[yang_cache_index(track->header_sn)].ts;
	for (int32_t i = 0; i < YANG_RTC_RECV_BUFFER_COUNT; ++i) {
		uint16_t lost_sn = current_sn + i;
		int32_t index = yang_cache_index(lost_sn);

		if (!track->cache_video_pkts[index].in_use) {
			return lost_sn;
		}
		//check time first, avoid two small frame mixed case decode fail
		if (last_ts != track->cache_video_pkts[index].ts) {
			*end_sn = lost_sn - 1;
			return -1;
		}

		if (track->cache_video_pkts[index].end) {
			*end_sn = lost_sn;
			return -1;
		}
	}

    yang_error("the cache is mess. the packet count of video frame is more than %u",
			YANG_RTC_RECV_BUFFER_COUNT);
	return -2;
}

int32_t yang_playtrackH264_is_keyframe(YangRtpPacket *pkt) {
	// It's normal H264 video rtp packet
	if (pkt->nalu_type == kStapA) {

		YangAvcNaluType nalu_type = (YangAvcNaluType) ((*(pkt->payload + 3))
				& kNalTypeMask);
		if (nalu_type == YangAvcNaluTypeSPS
				|| nalu_type == YangAvcNaluTypePPS) {
			return yangtrue;
		}
	} else if (pkt->nalu_type == kFuA) {
		uint8_t v = (uint8_t) (*(pkt->payload + 1));
		if (YangAvcNaluTypeIDR == (YangAvcNaluType) (v & kNalTypeMask))
			return yangtrue;

	} else {
		if ((pkt->nalu_type == YangAvcNaluTypeIDR)
				|| (pkt->nalu_type == YangAvcNaluTypeSPS)
				|| (pkt->nalu_type == YangAvcNaluTypePPS)) {
			return yangtrue;
		}
	}
	return yangfalse;
}

int32_t yang_playtrackH264_is_keyframe2(YangH264PacketCache *pkt) {
	// It's normal H264 video rtp packet
	if (pkt->nalu_type == kStapA) {

		YangAvcNaluType nalu_type = (YangAvcNaluType) ((*(pkt->payload + 3))
				& kNalTypeMask);
		if (nalu_type == YangAvcNaluTypeSPS
				|| nalu_type == YangAvcNaluTypePPS) {
			return yangtrue;
		}
	} else if (pkt->nalu_type == kFuA) {
			uint8_t v = (uint8_t) (*pkt->payload);
		if (YangAvcNaluTypeIDR == (YangAvcNaluType) (v & kNalTypeMask))
			return yangtrue;

	} else {
		if ((pkt->nalu_type == YangAvcNaluTypeIDR)
				|| (pkt->nalu_type == YangAvcNaluTypeSPS)
				|| (pkt->nalu_type == YangAvcNaluTypePPS)) {
			return yangtrue;
		}
	}
	return yangfalse;
}


int32_t yang_playtrackH264_packet_video(YangRtcContext *context,
		YangPlayTrackH264 *track, const uint16_t start, const uint16_t end) {
	int32_t err = Yang_Ok;
	uint16_t cnt = end - start + 1;
	YangH264PacketCache *header = &track->cache_video_pkts[yang_cache_index(
			start)];
	int64_t timestamp = header->ts;
	char *p = track->video_buffer;
	if (yang_playtrackH264_is_keyframe2(header)) {
		*p++ = 0x17;
		track->key_frame_ts = -1;
	} else {
		*p++ = 0x27;
	}
	*p++ = 0x01;
	*p++ = 0x00;
	*p++ = 0x00;
	*p++ = 0x00;

	int32_t nalu_len = 0;
	for (uint16_t i = 0; i < cnt; ++i) {
		uint16_t index = yang_cache_index((start + i));
		YangH264PacketCache *pkt = &track->cache_video_pkts[index];
		track->cache_video_pkts[index].in_use = yangfalse;

		if (pkt->nalu_type == kFuA) {
			if (pkt->nb > 0) {
				if (pkt->fua2.start) {
					nalu_len = pkt->nb + 1;
					p += 4;
					*p++ = pkt->fua2.nri | pkt->fua2.nalu_type;
					memcpy(p, pkt->payload, pkt->nb);
					p += pkt->nb;
				} else {
					nalu_len += pkt->nb;
					memcpy(p, pkt->payload, pkt->nb);
					p += pkt->nb;
					if (pkt->fua2.end) {
						//write nalu_len back
#if Yang_Rtp_I_Withmeta
						memset(p - (4 + nalu_len),0,3);
						*(p - (4 + nalu_len)+3)=0x01;
#else
						yang_put_be32(p - (4 + nalu_len), nalu_len);
#endif
					}
				}
				pkt->fua2.start = pkt->fua2.end = yangfalse;
				pkt->fua2.nri = pkt->fua2.nalu_type = (YangAvcNaluType) 0;
				continue;
			}
		}

		if (pkt->nalu_type == kStapA) {

			YangBuffer stap_buf;
			yang_init_buffer(&stap_buf, pkt->payload, pkt->nb);
			yang_reset_h2645_stap(&track->stapData);
			yang_decode_h264_stap(&stap_buf, &track->stapData);


			for (int32_t j = 0; j < track->stapData.nalus.vsize; ++j) {
				YangSample *sample =&track->stapData.nalus.payload[j];
				if (sample->nb > 0) {
#if Yang_Rtp_I_Withmeta
					memset(p,0,3);
					*(p+3)=0x01;
#else
					yang_put_be32(p, sample->nb);
#endif
					p += 4;
					memcpy(p, sample->bytes, sample->nb);
					p += sample->nb;
				}
			}
			yang_reset_h2645_stap(&track->stapData);
			continue;
		}

		if (pkt->payload) {

			if (pkt->nb > 0) {
				yang_put_be32(p, pkt->nb);
				p += 4;
				memcpy(p, pkt->payload, pkt->nb);
				p += pkt->nb;

				pkt->payload = NULL;

				continue;
			}
		}
		memset(&track->cache_video_pkts[i], 0,
				sizeof(YangH264PacketCache));
	}

	if ((err = yang_playtrackH264_put_frame_video(context, track, track->video_buffer,
			timestamp, p - track->video_buffer)) != Yang_Ok) {
		yang_warn("fail to pack video frame");
	}

	track->header_sn = end + 1;
	uint16_t tail_sn = 0;
	int32_t sn = yang_playtrackH264_find_next_lost_sn(track, track->header_sn,
			&tail_sn);
	if (-1 == sn) {
		if (yang_playtrackH264_check_frame_complete(track, track->header_sn,
				tail_sn)) {
			err = yang_playtrackH264_packet_video(context, track, track->header_sn,
					tail_sn);
		}
	} else if (-2 == sn) {
		return yang_error_wrap(ERROR_RTC_RTP_MUXER, "video cache is overflow");
	} else {
		track->lost_sn = sn;
	}

	return err;
}

int32_t yang_playtrackH264_packet_video_key_frame(YangRtcContext *context,
		YangPlayTrackH264 *track, YangRtpPacket *pkt) {
	int32_t err = Yang_Ok;
	uint16_t index = yang_cache_index(pkt->header.sequence);

	if (-1 == track->key_frame_ts) {
		track->key_frame_ts = pkt->header.timestamp;
		track->header_sn = pkt->header.sequence;
		track->lost_sn = track->header_sn + 1;
		// Received key frame and clean cache of old p frame pkts
		yang_playtrackH264_clear_cached_video(track);
	} else if (track->key_frame_ts != pkt->header.timestamp) {
		//new key frame, clean cache

		track->key_frame_ts = pkt->header.timestamp;
		track->header_sn = pkt->header.sequence;
		track->lost_sn = track->header_sn + 1;
		yang_playtrackH264_clear_cached_video(track);

	}

	track->cache_video_pkts[index].in_use = yangtrue;
	yang_playtrackH264_copy(track, pkt, &track->cache_video_pkts[index]);

	int32_t sn = track->lost_sn;
	uint16_t tail_sn = 0;
	if (yang_rtp_seq_distance(track->header_sn, pkt->header.sequence) < 0) {
		// When receive previous pkt in the same frame, update header sn;
		track->header_sn = pkt->header.sequence;
		sn = yang_playtrackH264_find_next_lost_sn(track, track->header_sn, &tail_sn);
	} else if (track->lost_sn == pkt->header.sequence) {
		sn = yang_playtrackH264_find_next_lost_sn(track, track->lost_sn, &tail_sn);
	}

	if (-1 == sn) {
		if (yang_playtrackH264_check_frame_complete(track, track->header_sn,
				tail_sn)) {

			if ((err = yang_playtrackH264_packet_video(context, track,
					track->header_sn, tail_sn)) != Yang_Ok) {
				err = yang_error_wrap(err, "fail to packet key frame");
			}
		}

	} else if (-2 == sn) {
		return yang_error_wrap(ERROR_RTC_RTP_MUXER, "video cache is overflow");
	} else {
		track->lost_sn = (uint16_t) sn;
	}

	return err;
}



int32_t yang_playtrackH264_packet_video_Raw(YangRtcContext *context,
		YangPlayTrackH264 *track, YangRtpPacket *pkt) {
	int err = Yang_Ok;
	char *p = track->video_buffer;
	int64_t timestamp = pkt->header.timestamp;
	*p++ = 0x27;	// type(4 bits): inter frame; code(4bits): avc
	*p++ = 0x01;
	*p++ = 0x00;
	*p++ = 0x00;
	*p++ = 0x00;
	yang_put_be32(p, pkt->nb);
	p += 4;
	memcpy(p, pkt->payload, pkt->nb);
	p += pkt->nb;
	if ((err = yang_playtrackH264_put_frame_video(context, track, track->video_buffer,
			timestamp, p - track->video_buffer)) != Yang_Ok) {
		yang_warn("fail to pack video frame");
	}

	track->header_sn = pkt->header.sequence + 1;
	uint16_t tail_sn = 0;
	int32_t sn = yang_playtrackH264_find_next_lost_sn(track, track->header_sn,	&tail_sn);
	if (-1 == sn) {
		if (yang_playtrackH264_check_frame_complete(track, track->header_sn,tail_sn)) {
			err = yang_playtrackH264_packet_video(context, track, track->header_sn,tail_sn);
		}
	} else if (-2 == sn) {
		return yang_error_wrap(ERROR_RTC_RTP_MUXER, "video cache is overflow");
	} else {
		track->lost_sn = sn;
	}

	return err;
}



int32_t yang_playtrackH264_on_rtp(YangRtcContext *context, YangPlayTrackH264 *track,
		YangRtpPacket *src) {
	int32_t err = Yang_Ok;
	if (!track->hasRequestKeyframe) {
		for(int i=0;i<context->videoTracks.vsize;i++){
			yang_send_rtcp_fb_pli(context, context->videoTracks.payload[i].ssrc);
		}

	}
	src->nalu_type = ((uint8_t) (src->payload[0] & kNalTypeMask));

	if (yang_playtrackH264_is_keyframe(src)) {
		track->hasRequestKeyframe = yangtrue;
		return yang_playtrackH264_packet_video_key_frame(context, track, src);
	}

//    if(src->track->nalu_type<=YangAvcNaluTypeCodedSliceExt){
//        if(src->track->header.marker) {
//            packet_video_Raw(src);
//            return Yang_Ok;
//        }
//
//    }
	// store in cache

	int32_t index = yang_cache_index(src->header.sequence);
	track->cache_video_pkts[index].in_use = yangtrue;
	yang_playtrackH264_copy(track, src, &track->cache_video_pkts[index]);

	// check whether to recovery lost packet and can construct a video frame
	if (track->lost_sn == src->header.sequence) {
		uint16_t tail_sn = 0;
		int32_t sn = yang_playtrackH264_find_next_lost_sn(track, track->lost_sn,&tail_sn);
		if (-1 == sn) {
			if (yang_playtrackH264_check_frame_complete(track, track->header_sn,tail_sn)) {
				if ((err = yang_playtrackH264_packet_video(context, track,track->header_sn, tail_sn)) != Yang_Ok) {
					err = yang_error_wrap(err, "fail to pack video frame");
				}
			}
		} else if (-2 == sn) {
			return yang_error_wrap(ERROR_RTC_RTP_MUXER,
					"video cache is overflow");
		} else {
			track->lost_sn = (uint16_t) sn;
		}
	}

	return err;
}


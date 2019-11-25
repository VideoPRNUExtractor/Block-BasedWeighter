/*
 * Copyright (c) 2012 Stefano Sabatini
 * Copyright (c) 2014 Clément Bœsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <libavutil/motion_vector.h>
#include <libavformat/avformat.h>
#include <sqlite3.h>
#include <unistd.h>
#include "extract_mvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
//#define DATABASE_FILE "/home/enes/Desktop/stbDeneme/tekrar/dbdeneme.db"
#define SQL_DDL_FILE "DDL.txt"

static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static char src_filename[256];
static char *src_path = NULL;

static int video_stream_idx = -1;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int video_frame_count = 0;
static int isDB;
//SQlite database variable
static sqlite3 *db;
static sqlite3_stmt *stmt_mv_insert;
static sqlite3_stmt *stmt_mb_insert;
static sqlite3_stmt *stmt_fr_insert;
static sqlite3_stmt *stmt_vid_insert;

static int callback(void *data, int argc, char **argv, char **azColName) {
	int i;
	fprintf(stderr, "%s: ", (const char*) data);
	for (i = 0; i < argc; i++) {
		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	printf("\n");
	return 0;
}
int begin_transaction_db(void) {
	char *zErrMsg = 0;
	int rc;
	rc = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &zErrMsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);

	} else {
		printf("Database transaction is started successfully\n");
	}
	return 0;
}
int end_transaction_db(void) {
	char *zErrMsg = 0;
	int rc;
	rc = sqlite3_exec(db, "END TRANSACTION", NULL, NULL, &zErrMsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);

	} else {
		printf("Database transaction is ended successfully\n");
	}
	return 0;
}
int prepare_insert_statements_db(void) {
	int rc;
	char sql[2048];
	char *zErrMsg = 0;
	sprintf(sql,
			"INSERT INTO MVs (MVX,MVY,MVscale, direction, subMBw, subMBh, subMBx, subMBy, parentMBID, parentFrameID) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt_mv_insert, 0);
	if (rc != SQLITE_OK)
		return rc;

	sprintf(sql,
			"INSERT INTO MBs (FrameID, MBno, MBx, MBy, MBw, MBh, MBtype, partMode, qp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt_mb_insert, 0);
	if (rc != SQLITE_OK)
		return rc;
	sprintf(sql,
			"INSERT INTO FRAMEs (CodedPictureNumber, FrameNumber, PictureType, Width, Height, MinBlockSize, MinBlockWidth, MinBlockHeight) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt_fr_insert, 0);
	if (rc != SQLITE_OK)
		return rc;
	sprintf(sql,
			"INSERT INTO VIDEO (CodecID, CodecNameShort, CodecNameLong, GOPSize, AspectRatioNum, AspectRatioDen) VALUES (?, ?, ?, ?, ?, ?)");
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt_vid_insert, 0);
	if (rc != SQLITE_OK)
		return rc;

	return SQLITE_OK;
}
int insert_fr_db(int CodedPictureNumber, int FrameNumber, int PictureType,
		int Width, int Height, int MinBlockSize, int MinBlockWidth,
		int MinBlockHeight) {
	int step;
	sqlite3_bind_int(stmt_fr_insert, 1, CodedPictureNumber);
	sqlite3_bind_int(stmt_fr_insert, 2, FrameNumber);
	sqlite3_bind_int(stmt_fr_insert, 3, PictureType);
	sqlite3_bind_int(stmt_fr_insert, 4, Width);
	sqlite3_bind_int(stmt_fr_insert, 5, Height);
	sqlite3_bind_int(stmt_fr_insert, 6, MinBlockSize);
	sqlite3_bind_int(stmt_fr_insert, 7, MinBlockWidth);
	sqlite3_bind_int(stmt_fr_insert, 8, MinBlockHeight);
	step = sqlite3_step(stmt_fr_insert);
	sqlite3_clear_bindings(stmt_fr_insert); /* Clear bindings */
	sqlite3_reset(stmt_fr_insert); /* Reset VDBE */
	return 0;
}
int insert_mb_db(int FrameID, int MBno, int MBx, int MBy, int MBw, int MBh,
		int MBtype, int partMode, int mvx, int mvy, int direction, int MVscale,
		int subMBw, int subMBh, int subMBx, int subMBy, int parentMBID,
		int parentFrameID, int mb_qp) {

	int step;
	static int lastMBno = -1;
	if (lastMBno != MBno) { //prevents duplicate rows in DB.
		//MB insert
		sqlite3_bind_int(stmt_mb_insert, 1, FrameID);
		sqlite3_bind_int(stmt_mb_insert, 2, MBno);
		sqlite3_bind_int(stmt_mb_insert, 3, MBx);
		sqlite3_bind_int(stmt_mb_insert, 4, MBy);
		sqlite3_bind_int(stmt_mb_insert, 5, MBw);
		sqlite3_bind_int(stmt_mb_insert, 6, MBh);
		sqlite3_bind_int(stmt_mb_insert, 7, MBtype);
		sqlite3_bind_int(stmt_mb_insert, 8, partMode);
		sqlite3_bind_int(stmt_mb_insert, 9, mb_qp);
		step = sqlite3_step(stmt_mb_insert);
		sqlite3_clear_bindings(stmt_mb_insert); /* Clear bindings */
		sqlite3_reset(stmt_mb_insert); /* Reset VDBE */
		lastMBno = MBno;
	}
	//MV insert
	sqlite3_bind_int(stmt_mv_insert, 1, mvx);
	sqlite3_bind_int(stmt_mv_insert, 2, mvy);
	sqlite3_bind_int(stmt_mv_insert, 3, MVscale);
	sqlite3_bind_int(stmt_mv_insert, 4, direction);
	sqlite3_bind_int(stmt_mv_insert, 5, subMBw);
	sqlite3_bind_int(stmt_mv_insert, 6, subMBh);
	sqlite3_bind_int(stmt_mv_insert, 7, subMBx);
	sqlite3_bind_int(stmt_mv_insert, 8, subMBy);
	sqlite3_bind_int(stmt_mv_insert, 9, parentMBID);
	sqlite3_bind_int(stmt_mv_insert, 10, parentFrameID);
	step = sqlite3_step(stmt_mv_insert);
	sqlite3_clear_bindings(stmt_mv_insert); /* Clear bindings */
	sqlite3_reset(stmt_mv_insert); /* Reset VDBE */
	return 0;
}

int insert_vid_db(int codec_id, char *codec_name_short, char *codec_name_long,
		int gop_size, int aspect_ratio_num, int aspect_ratio_den) {
	int step;
	sqlite3_bind_int(stmt_vid_insert, 1, codec_id);
	sqlite3_bind_text(stmt_vid_insert, 2, codec_name_short, -1, 0);
	sqlite3_bind_text(stmt_vid_insert, 3, codec_name_long, -1, 0);
	sqlite3_bind_int(stmt_vid_insert, 4, gop_size);
	sqlite3_bind_int(stmt_vid_insert, 5, aspect_ratio_num);
	sqlite3_bind_int(stmt_vid_insert, 6, aspect_ratio_den);
	step = sqlite3_step(stmt_vid_insert);
	sqlite3_clear_bindings(stmt_vid_insert); /* Clear bindings */
	sqlite3_reset(stmt_vid_insert); /* Reset VDBE */
	return 0;
}
int WriteJPEG(AVCodecContext *pCodecCtx, AVFrame *pFrame, int FrameNo) {
	AVCodecContext *pOCodecCtx;
	AVCodec *pOCodec;
	uint8_t *Buffer;
	int BufSiz;
	int BufSizActual;
	int ImgFmt = AV_PIX_FMT_YUVJ420P; //for thenewer ffmpeg version, this int to pixelformat
	FILE *JPEGFile;
	char JPEGFName[256];

	BufSiz = avpicture_get_size(ImgFmt, pCodecCtx->width, pCodecCtx->height);

	Buffer = (uint8_t *) malloc(BufSiz * 10);
	if (Buffer == NULL)
		return (0);
	memset(Buffer, 0, BufSiz);

	pOCodecCtx = avcodec_alloc_context3(AVMEDIA_TYPE_VIDEO);
	if (!pOCodecCtx) {
		free(Buffer);
		return (0);
	}

	pOCodecCtx->bit_rate = pCodecCtx->bit_rate;
	pOCodecCtx->width = pCodecCtx->width;
	pOCodecCtx->height = pCodecCtx->height;
	pOCodecCtx->pix_fmt = ImgFmt;
	pOCodecCtx->codec_id = AV_CODEC_ID_MJPEG;
	pOCodecCtx->codec_type = pCodecCtx->codec_type; //AVMEDIA_TYPE_VIDEO;
	pOCodecCtx->time_base.num = pCodecCtx->time_base.num;
	pOCodecCtx->time_base.den = pCodecCtx->time_base.den;

	//pOCodecCtx->bit_rate_tolerance=452678328;

	pOCodec = avcodec_find_encoder(pOCodecCtx->codec_id);
	if (!pOCodec) {
		free(Buffer);
		return (0);
	}
	if (avcodec_open2(pOCodecCtx, pOCodec, NULL) < 0) {
		free(Buffer);
		return (0);
	}
	pOCodecCtx->qmin = 1;
	pOCodecCtx->qmax = 1;
	pOCodecCtx->mb_lmin = pOCodecCtx->lmin = pOCodecCtx->qmin * FF_QP2LAMBDA;
	pOCodecCtx->mb_lmax = pOCodecCtx->lmax = pOCodecCtx->qmax * FF_QP2LAMBDA;
	pOCodecCtx->flags = CODEC_FLAG_QSCALE;
	pOCodecCtx->global_quality = pOCodecCtx->qmin * FF_QP2LAMBDA;

	pFrame->pts = 1;
	pFrame->quality = pOCodecCtx->global_quality;

	AVPacket outPacket = { .data = NULL, .size = 0 };
	av_init_packet(&outPacket);
	int gotFram = 0;
	BufSizActual = avcodec_encode_video2(pOCodecCtx, &outPacket, pFrame,
			&gotFram);

	sprintf(JPEGFName, "%sFrame/frame-%04d.jpg", src_filename, FrameNo);
	JPEGFile = fopen(JPEGFName, "wb");
	fwrite(outPacket.data, 1, outPacket.size, JPEGFile);
	fclose(JPEGFile);

	avcodec_close(pOCodecCtx);
	free(Buffer);
	return (BufSizActual);
}

char* ReadFile(char *filename) {
	char *buffer = NULL;
	int string_size, read_size;
	FILE *handler = fopen(filename, "r");

	if (handler) {
		// Seek the last byte of the file
		fseek(handler, 0, SEEK_END);
		// Offset from the first to the last byte, or in other words, filesize
		string_size = ftell(handler);
		// go back to the start of the file
		rewind(handler);

		// Allocate a string that can hold it all
		buffer = (char*) malloc(sizeof(char) * (string_size + 1));

		// Read it all in one operation
		read_size = fread(buffer, sizeof(char), string_size, handler);

		// fread doesn't set it so put a \0 in the last position
		// and buffer is now officially a string
		buffer[string_size] = '\0';

		if (string_size != read_size) {
			// Something went wrong, throw away the memory and set
			// the buffer to NULL
			free(buffer);
			buffer = NULL;
		}

		// Always remember to close the file.
		fclose(handler);
	}
	return buffer;
}
static int decode_packet(int *got_frame, int cached) {
	int decoded = pkt.size;
	static int is_video_info_saved_to_db = 0;
	*got_frame = 0;
	int status = 0;
	if (pkt.stream_index == video_stream_idx) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			// 49 for skip loop filter
			video_dec_ctx->skip_loop_filter = 49;
			int ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame,
					&pkt);

			if (ret < 0) {
				fprintf(stderr, "Error decoding video frame (%s)\n",
						av_err2str(ret));
				return ret;
			}

			if (*got_frame) {
				// HEVCContext *s =video_dec_ctx->priv_data;

				AVFrameSideData *sdFrameInfo;
				sdFrameInfo = av_frame_get_side_data(frame, KSM_AV_FRAME_INFO);
				KSM_AVFrameInfo *fri = NULL;
				//if (sdFrameInfo) {
					fri = (const KSM_AVFrameInfo *) sdFrameInfo->data;
					//if (fri->pict_type == 1) {

						fflush(stdout);
						WriteJPEG(video_dec_ctx, frame, video_frame_count);
						printf("bitti%d\n",video_frame_count);
					//}
				//}
			}
			exit(0);
		} else {
			wait(NULL);
			{
				int ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame,
									&pkt);


				video_frame_count++;

				if (*got_frame&&isDB) {
					int i;
					AVFrameSideData *sd;
					AVFrameSideData *sdMBinfo;
					AVFrameSideData *sdFrameInfo;
					AVFrameSideData *sdVideoInfo;
					AVFrameSideData *sd_mvf;
					AVFrameSideData *sd_cui;
					printf("Frame: %d  ", video_frame_count);
					int min_cb_width = 8;
					sd = av_frame_get_side_data(frame,
							AV_FRAME_DATA_MOTION_VECTORS);
					if (sd) {
						const AVMotionVector *mvs =
								(const AVMotionVector *) sd->data;
						for (i = 0; i < sd->size / sizeof(*mvs); i++) {
							const AVMotionVector *mv = &mvs[i];
						}
					}

					//Get video side data
					if (is_video_info_saved_to_db == 0) {
						sdVideoInfo = av_frame_get_side_data(frame,
								KSM_AV_VIDEO_INFO);
						KSM_AVVideoInfo *vid = NULL;
						if (sdVideoInfo) {
							vid = (const KSM_AVVideoInfo *) sdVideoInfo->data;

							//                printf("%d, %d, %d, %d\n", fri->coded_picture_number, fri->pict_type, fri->width, fri->height, fri->min_block_width );
							//                min_cb_width = fri->min_cb_width;
							//                insert_fr_db(fri->coded_picture_number,fri->pict_type,fri->width, fri->height, fri->min_block_size, fri->min_block_width, fri->min_block_height);
							if (insert_vid_db(vid->codec_id,
									vid->codec_name_short, vid->codec_name_long,
									vid->gop_size, vid->aspect_ratio_num,
									vid->aspect_ratio_den))
								is_video_info_saved_to_db = 0;
							else
								is_video_info_saved_to_db = 1;
						}

					}

					//Get frame side data
					sdFrameInfo = av_frame_get_side_data(frame,
							KSM_AV_FRAME_INFO);
					KSM_AVFrameInfo *fri = NULL;
					if (sdFrameInfo) {
						fri = (const KSM_AVFrameInfo *) sdFrameInfo->data;
						printf("%d, %d, %d, %d\n", fri->coded_picture_number,
								fri->pict_type, fri->width, fri->height,
								fri->min_block_width);
						min_cb_width = fri->min_cb_width;
						insert_fr_db(fri->coded_picture_number,
								fri->frame_number, fri->pict_type, fri->width,
								fri->height, fri->min_block_size,
								fri->min_block_width, fri->min_block_height);

					}

					int frameID = video_frame_count - 1;
					sdMBinfo = av_frame_get_side_data(frame,
							KSM_AV_MACROBLOCK_INFO);
					if (sdMBinfo) {
						const KSM_AVMacroBlockInfo *mbiList =
								(const KSM_AVMacroBlockInfo *) sdMBinfo->data;
						for (i = 0; i < sdMBinfo->size / sizeof(*mbiList);
								i++) {
							const KSM_AVMacroBlockInfo *mbi = &mbiList[i];
							//printf("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",video_frame_count, mbi->macroblock_no, mbi->mb_type, mbi->mb_x, mbi->mb_y, mbi->MV.src_x, mbi->MV.src_y, mbi->MV.dst_x, mbi->MV.dst_y, mbi->MV.motion_scale, mbi->MV.source,  mbi->MV.w, mbi->MV.h, mbi->MV.motion_x, mbi->MV.motion_y );
							/*                   printf("%d,%2d,%2d,%2d,%4d,%4d,%4d,%4d,%0.2f,%0.2f,0x%"PRIx64"\n",
							 video_frame_count, mv->source,
							 mv->w, mv->h, mv->src_x, mv->src_y,
							 mv->dst_x, mv->dst_y, mv->motion_x/(float)mv->motion_scale, mv->motion_y/(float)mv->motion_scale, mv->flags);*/

							insert_mb_db(frameID, mbi->macroblock_no, mbi->mb_x,
									mbi->mb_y, 16, 16, mbi->mb_type, PART_2Nx2N,/*part mode*/
									mbi->MV.motion_x, mbi->MV.motion_y,
									mbi->MV.source, mbi->MV.motion_scale,
									mbi->MV.w, mbi->MV.h, mbi->mb_x, mbi->mb_y,
									mbi->macroblock_no, frameID, mbi->mb_qp);

						}
					}

					sd_mvf = av_frame_get_side_data(frame, KSM_AV_HEVC_PU_INFO);
					if (sd_mvf) {
						const KSM_AV_HEVC_PU_Info *puList =
								(const KSM_AV_HEVC_PU_Info *) sd_mvf->data;
						sd_cui = av_frame_get_side_data(frame,
								KSM_AV_HEVC_CU_INFO);
						if (sd_cui) {
							const KSM_AV_HEVC_CU_Info *cuList =
									(const KSM_AV_HEVC_CU_Info *) sd_cui->data;
							for (i = 0; i < sd_cui->size / sizeof(*cuList);
									i++) {
								const KSM_AV_HEVC_CU_Info *cu = &cuList[i];
								if (cu->pred_mode == -1 || cu->cb_size == -1)
									continue;

								int x_pu_offset = cu->x / fri->min_pu_size;
								int y_pu_offset = cu->y / fri->min_pu_size;
								int index_pu_offset = y_pu_offset
										* fri->min_pu_width + x_pu_offset;
								int length = cu->cb_size / fri->min_pu_size;
								for (int l = 0; l < length; l++) {
									for (int k = 0; k < length; k++) {
										const KSM_AV_HEVC_PU_Info *pu =
												&puList[index_pu_offset
														+ l * fri->min_pu_width
														+ k];
										int pu_x = (x_pu_offset + k)
												* fri->min_pu_size;
										int pu_y = (y_pu_offset + l)
												* fri->min_pu_size;
										if (pu->mvf.pred_flag == PF_INTRA) {
											insert_mb_db(frameID,
													i /*macroblock no*/, cu->x,
													cu->y, cu->cb_size,
													cu->cb_size, cu->pred_mode,
													cu->part_mode, 0, 0, 0,
													4/*TODO: motion scale*/,
													fri->min_pu_size,
													fri->min_pu_size, pu_x,
													pu_y, i /*cu no*/, frameID,
													cu->mb_qp);
										}
										if (pu->mvf.pred_flag & PF_L0) {
											insert_mb_db(frameID,
													i /*macroblock no*/, cu->x,
													cu->y, cu->cb_size,
													cu->cb_size, cu->pred_mode,
													cu->part_mode,
													pu->mvf.mv[0].x,
													pu->mvf.mv[0].y, -1,
													4/*TODO: motion scale*/,
													fri->min_pu_size,
													fri->min_pu_size, pu_x,
													pu_y, i /*cu no*/, frameID,
													cu->mb_qp);
										}
										if (pu->mvf.pred_flag & PF_L1) {
											insert_mb_db(frameID,
													i /*macroblock no*/, cu->x,
													cu->y, cu->cb_size,
													cu->cb_size, cu->pred_mode,
													cu->part_mode,
													pu->mvf.mv[1].x,
													pu->mvf.mv[1].y, 1,
													4/*TODO: motion scale*/,
													fri->min_pu_size,
													fri->min_pu_size, pu_x,
													pu_y, i /*cu no*/, frameID,
													cu->mb_qp);
										}

//								insert_mb_db(int FrameID, int MBno, int MBx, int MBy, int MBw, int MBh, int MBstride, int MBtype,
//								int mvx, int mvy,  int direction, int MVscale, int subMBw, int subMBh, int subMBx, int subMBy, int parentMBID, int parentFrameID)
									}
								}

							}
						}
					}
				}
			}

		}
	}
	return decoded;
}

static int open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx,
		enum AVMediaType type) {
	int ret;
	AVStream *st;
	AVCodecContext *dec_ctx = NULL;
	AVCodec *dec = NULL;
	AVDictionary *opts = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
				av_get_media_type_string(type), src_filename);
		return ret;
	} else {
		*stream_idx = ret;
		st = fmt_ctx->streams[*stream_idx];

		/* find decoder for the stream */
		dec_ctx = st->codec;
		dec = avcodec_find_decoder(dec_ctx->codec_id);
		if (!dec) {
			fprintf(stderr, "Failed to find %s codec\n",
					av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Init the video decoder */
		av_dict_set(&opts, "flags2", "+export_mvs", 0);
		if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
			fprintf(stderr, "Failed to open %s codec\n",
					av_get_media_type_string(type));
			return ret;
		}
	}

	return 0;

}
//Reads content of a text file for DDL SQL codes
static char *read_content(const char *filename) {
	char *fcontent = NULL;
	int fsize = 0;
	FILE *fp;

	fp = fopen(filename, "r");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);
		rewind(fp);

		fcontent = (char*) malloc(sizeof(char) * fsize);
		fread(fcontent, 1, fsize, fp);

		fclose(fp);
	} else {
		fprintf(stderr, "Can't open the file: %s\n", filename);
	}
	return fcontent;
}

int initialize_db(char *path) {
	char zErrMsg[5000];
	int rc;
	char sql[5000];
	const char* data = "Database callback function is called";

	if (access(path, F_OK) != -1) {
		// file exists
		printf("%s file exists\n", path);
		//remove file
		int ret = remove(path);

		if (ret == 0) {
			printf("%s file is deleted successfully\n", path);
		} else {
			printf("Error: unable to delete the file: %s\n", path);
		}
	} else {
		// file doesn't exist
		printf("%s doesnt exist\n", path);
	}
	/* Open database */
	rc = sqlite3_open(path, &db);
	if (rc) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	} else {
		printf("Opened database successfully\n");
	}

	char* tmp_sql = read_content(SQL_DDL_FILE);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, tmp_sql, callback, (void*) data, &zErrMsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);

	} else {
		printf("DB tables are created successfully\n");
	}
	free(tmp_sql);
	sprintf(sql, "INSERT INTO DBinfo (version) VALUES (%d)",
	VCAL_DATABASE_VERSION);
	rc = sqlite3_exec(db, sql, callback, (void*) data, &zErrMsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);

	} else {
		printf("DB version info is inserted: DB version %d\n",
		VCAL_DATABASE_VERSION);
	}

	rc = prepare_insert_statements_db();
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL prepare insert statement error rc: %d\n", rc);
		return rc;

	}

	return SQLITE_OK;
}

int finilize_db(void) {

	sqlite3_finalize(stmt_mv_insert);
	sqlite3_finalize(stmt_mb_insert);
	sqlite3_finalize(stmt_fr_insert);
	sqlite3_close(db);
	sqlite3_close_v2(db);
	return 0;
}

int main(int argc, char **argv) {

	 if (argc != 3) {
	 fprintf(stderr, "Usage: %s <video> <isDB>\n", argv[0]);
	 exit(1);
	 }
	char *src_video = argv[1];
	isDB=atoi(argv[2]);
	int ret = 0, got_frame, rc;
	video_frame_count = 0;

	char src_db[256], src_frame[256];
	sprintf(src_db, "%sdb.db", src_video);
	sprintf(src_frame, "%sFrame", src_video);
	mkdir(src_frame, 0700);
	//av_log_set_level(AV_LOG_TRACE);

	rc = initialize_db(src_db);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL error. Database could not be initialized: %d\n",
				rc);
	} else {
		printf("DB has been initialized.\n");
	}
	
	sprintf(src_filename, "%s", src_video);

	av_register_all();

	if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", src_filename);
		return -1;
	}

	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO)
			>= 0) {
		video_stream = fmt_ctx->streams[video_stream_idx];
		video_dec_ctx = video_stream->codec;
	}

	av_dump_format(fmt_ctx, 0, src_filename, 0);

	if (!video_stream) {
		fprintf(stderr, "Could not find video stream in the input, aborting\n");
		ret = 1;
		goto end;
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		goto end;
	}

	printf("framenum,source,blockw,blockh,srcx,srcy,dstx,dsty,MVx,MVy,flags\n");

	/* initialize packet, set data to NULL, let the demuxer fill it */
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	begin_transaction_db();
	/* read frames from the file */
	while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        	AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;

    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);
    end_transaction_db();
	end: avcodec_close(video_dec_ctx);
	avformat_close_input(&fmt_ctx);
	av_frame_free(&frame);
	finilize_db();
	return ret < 0;
}



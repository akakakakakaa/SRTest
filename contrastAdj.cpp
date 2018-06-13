#include <iostream>
#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
using namespace cv;
using namespace std;

// flickering effect, 즉 명암 값이 유동적으로 변경되어 반짝이는 경우가 발생한다. 따라서
// 이를 해결하기 위해 명암차를 구하여 명암을 되돌리기 위한 작업을 수행하기 위한 코드이다.
// input 영상의 포맷은 YUV420P, opencv의 이미지 포맷은 rgb로 가정한다.
int main() {
	const char* ganFile = "videos/test2_srgan.mp4";
	const char* restoredGanFile = "videos/test2_srgan_restored.avi";
	const char* oriFile = "videos/test2.mp4";
	
	//명암 수정 후, 비디오로 저장하기 위해 ffmpeg을 수행한다.
	AVFormatContext* inFmtCtx = NULL;
	int ret = avformat_open_input(&inFmtCtx, ganFile, NULL, NULL);
	if (ret != 0) {
		cout << "error: cannot open input gan video file. error number is " << ret << endl;
		exit(1);
	}

	ret = avformat_find_stream_info(inFmtCtx, NULL);
	if (ret < 0) {
		cout << "error: cannot find stream info. error number is " << ret << endl;
		exit(1);
	}

	av_dump_format(inFmtCtx, 0, ganFile, 0);

	AVFormatContext* outFmtCtx = NULL;
	ret = avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, restoredGanFile);
	if (!outFmtCtx || ret < 0) {
		cout << "error: cannot alloc avformat context. error number is " << ret << endl;
		exit(1);
	}

	AVPacket* restoredGanPkt = av_packet_alloc();
	if (!restoredGanPkt) {
		cout << "error: cannot alloc packet." << endl;
		exit(1);
	}

	AVFrame* restoredGanFrame = NULL;
	AVStream* outVidStream = NULL;
	AVCodecContext* outCodecCtx = NULL;
	for (int i = 0; i < inFmtCtx->nb_streams; i++) {
		AVStream* inVidStream = inFmtCtx->streams[i];
		if (inVidStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			AVCodec* encoder = avcodec_find_encoder(inVidStream->codecpar->codec_id);
			if (!encoder) {
				cout << "error: codec not found." << endl;
				exit(1);
			}
			outVidStream = avformat_new_stream(outFmtCtx, encoder);

			outCodecCtx = avcodec_alloc_context3(encoder);
			if (!outCodecCtx) {
				cout << "error: cannot allocate video codec context." << endl;
				exit(1);
			}

			AVCodecParameters* parameters = inVidStream->codecpar;
			avcodec_parameters_to_context(outCodecCtx, parameters);
			avcodec_parameters_copy(outVidStream->codecpar, parameters);

			//i don't know why time_base(fps) cannot get from input format
			outVidStream->time_base = outCodecCtx->time_base = { 1, 30 };// inVidStream->time_base;
			restoredGanFrame = av_frame_alloc();
			if (!restoredGanFrame) {
				cout << "error: cannot alloc frame." << endl;
				exit(1);
			}
			restoredGanFrame->format = outCodecCtx->pix_fmt;
			restoredGanFrame->width = outVidStream->codecpar->width;
			restoredGanFrame->height = outVidStream->codecpar->height;

			ret = av_frame_get_buffer(restoredGanFrame, 32);
			if (ret < 0) {
				cout << "error: cannot alloc frame buffer." << endl;
				exit(1);
			}

			ret = avcodec_open2(outCodecCtx, encoder, NULL);
			if (ret < 0) {
				cout << "error: find video encoder fail. error number is " << ret << endl;
				exit(1);
			}
			break;
		}
	}

	if (!outVidStream) {
		cout << "error: cannot find video stream in " << ganFile << endl;
		return 0;
	}

	//opencv에서 RGB 순서는 FFmpeg에서 BGR로 취급된다.
	SwsContext* swsCtx = sws_getContext(outCodecCtx->width, outCodecCtx->height, AV_PIX_FMT_BGR24, outCodecCtx->width, outCodecCtx->height, outCodecCtx->pix_fmt, SWS_BICUBIC, 0, 0, 0);

	ret = avio_open(&outFmtCtx->pb, restoredGanFile, AVIO_FLAG_WRITE);
	if (ret < 0) {
		cout << "error: cannot open output gan video file." << endl;
		exit(1);
	}

	ret = avformat_write_header(outFmtCtx, NULL);
	if (ret < 0) {
		cout << "error: cannot write header." << endl;
		exit(1);
	}

	av_dump_format(outFmtCtx, 0, restoredGanFile, 1);

	// gan 영상의 경우 original 영상에 super resolution만을 적용한 것이기 때문에
	// original 영상과 픽셀의 채널이 같다고 가정한다.
	VideoCapture videoGan(ganFile);
	VideoCapture videoOri(oriFile);

	Mat ganMat;
	Mat oriMat;
	Mat restoredGanMat;
	int frameCount = 0;

	while (true) {
		//FFmpeg format is yuv but if we use this, returned mat format is rgb
		videoGan >> ganMat;
		videoOri >> oriMat;

		if (ganMat.empty()) {
			cout << "end of frame." << endl;
			break;
		}

		ganMat.copyTo(restoredGanMat);
			
		uchar* ganData = ganMat.data;
		uchar* oriData = oriMat.data;
		uchar* restoredGanData = restoredGanMat.data;
		int ganChannel = ganMat.channels();
		int oriChannel = oriMat.channels();

		if (ganChannel != oriChannel) {
			cout << "error: channel number is different." << endl;
			break;
		}

		double* ganAvg = new double[ganChannel];
		double* oriAvg = new double[ganChannel];
		double* diffAvg = new double[ganChannel];

		//initialize avg arrays.
		for (int i = 0; i < ganChannel; i++) {
			ganAvg[i] = 0;
			oriAvg[i] = 0;
		}

		/*
		//add all pixel values to avg array
		int i = 0;
		for (; i < ganMat.rows * ganMat.cols; i++)
			ganAvg[0] += ganData[i];
		ganAvg[0] /= ganMat.rows * ganMat.cols;

		for (; i < ganMat.rows * ganMat.cols*2; i++)
			ganAvg[1] += ganData[i];
		ganAvg[1] /= ganMat.rows * ganMat.cols;

		for (; i < ganMat.rows * ganMat.cols*3; i++)
			ganAvg[2] += ganData[i];
		ganAvg[2] /= ganMat.rows * ganMat.cols;


		i = 0;
		for (; i < oriMat.rows * oriMat.cols; i++)
			oriAvg[0] += oriData[i];
		oriAvg[0] /= oriMat.rows * oriMat.cols;

		for (; i < oriMat.rows * oriMat.cols*2; i++)
			oriAvg[1] += oriData[i];
		oriAvg[1] /= oriMat.rows * oriMat.cols;

		for (; i < oriMat.rows * oriMat.cols*3; i++)
			oriAvg[2] += oriData[i];
		oriAvg[2] /= oriMat.rows * oriMat.cols;

		//calculate diff avg
		diffAvg[0] = oriAvg[0] - ganAvg[0];
		diffAvg[1] = oriAvg[1] - ganAvg[1];
		diffAvg[2] = oriAvg[2] - ganAvg[2];

		i = 0;
		for (; i < ganMat.rows * ganMat.cols; i++) {
			int sum = restoredGanData[i] + diffAvg[0];
			int pixelValue = sum > 255 ? 255 : sum < 0 ? 0 : sum;
			restoredGanData[i] = pixelValue;
		}

		for (; i < ganMat.rows * ganMat.cols*2; i++) {
			int sum = restoredGanData[i] + diffAvg[1];
			int pixelValue = sum > 255 ? 255 : sum < 0 ? 0 : sum;
			restoredGanData[i] = pixelValue;
		}

		for (; i < ganMat.rows * ganMat.cols*3; i++) {
			int sum = restoredGanData[i] + diffAvg[2];
			int pixelValue = sum > 255 ? 255 : sum < 0 ? 0 : sum;
			restoredGanData[i] = pixelValue;
		}

		cout << ganMat.depth() << endl;
		*/
		
		for (int y = 0; y < ganMat.rows; y++) {
			int rows = y * ganMat.cols;
			for (int x = 0; x < ganMat.cols; x++) {
				int cols = (rows + x) * ganChannel;
				for (int channel = 0; channel < ganChannel; channel++)
					ganAvg[channel] += ganData[cols + channel];
			}
		}
		for (int y = 0; y < oriMat.rows; y++) {
			int rows = y * oriMat.cols;
			for (int x = 0; x < oriMat.cols; x++) {
				int cols = (rows + x) * oriChannel;
				for (int channel = 0; channel < oriChannel; channel++)
					oriAvg[channel] += oriData[cols + channel];
			}
		}

		for (int i = 0; i < ganChannel; i++) {
			ganAvg[i] /= ganMat.rows * ganMat.cols;
			oriAvg[i] /= oriMat.rows * oriMat.cols;
			diffAvg[i] = oriAvg[i] / ganAvg[i];
			cout << "Mat " << frameCount << ": Diff of two avg values in channel " << i << " is " << diffAvg[i] << endl;
		}

		//contrast restoration
		for (int y = 0; y < ganMat.rows; y++) {
			int rows = y * ganMat.cols;
			for (int x = 0; x < ganMat.cols; x++) {
				int cols = (rows + x) * ganChannel;
				for (int channel = 0; channel < ganChannel; channel++) {
					int pixelValue = ganData[cols + channel] * diffAvg[channel];
					restoredGanData[cols + channel] = (pixelValue < 0) ? 0 : ((pixelValue > 255) ? 255 : pixelValue);
				}
			}
		}

		//encode to video
		/*
		imshow("gan", ganMat);
		imshow("original", oriMat);
		imshow("restored", restoredGanMat);
		waitKey(0);
		*/

		uint8_t* srcSlice[] = { restoredGanMat.data };
		int srcStride[] = { restoredGanMat.cols * 3 };
		sws_scale(swsCtx, srcSlice, srcStride, 0, restoredGanMat.rows, restoredGanFrame->data, restoredGanFrame->linesize);
		restoredGanFrame->pts = frameCount;

		ret = avcodec_send_frame(outCodecCtx, restoredGanFrame);
		if (ret < 0) {
			cout << "error: send frame to encoder failed." << endl;
			break;
		}

		while (ret >= 0) {
			ret = avcodec_receive_packet(outCodecCtx, restoredGanPkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0) {
				cout << "error: encode frame failed." << endl;
				exit(1);
			}

			restoredGanPkt->pts = frameCount;
			//restoredGanPkt->dts = 33;
			av_interleaved_write_frame(outFmtCtx, restoredGanPkt);
			av_packet_unref(restoredGanPkt);
		}

		delete[] ganAvg;
		delete[] oriAvg;
		delete[] diffAvg;

		string oriName = "images/original/original" + to_string(frameCount) + ".jpg";
		imwrite(oriName, oriMat);
		string ganName = "images/gan/srgan" + to_string(frameCount) + ".jpg";
		imwrite(ganName, ganMat);
		string restoredGan = "images/restored/restored" + to_string(frameCount) + ".jpg";
		imwrite(restoredGan, restoredGanMat);

		frameCount++;
	}

	av_write_trailer(outFmtCtx);
	cout << "process end." << endl;

	avio_close(outFmtCtx->pb);
	avformat_free_context(outFmtCtx);
	avcodec_free_context(&outCodecCtx);
	av_frame_free(&restoredGanFrame);
	av_packet_free(&restoredGanPkt);
	sws_freeContext(swsCtx);

	system("pause");
}

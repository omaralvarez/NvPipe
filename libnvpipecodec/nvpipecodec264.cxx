/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual
 * property and proprietary rights in and to this software,
 * related documentation and any modifications thereto.  Any use,
 * reproduction, disclosure or distribution of this software and
 * related documentation without an express license agreement from
 * NVIDIA Corporation is strictly prohibited.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE
 * IS PROVIDED *AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL
 * WARRANTIES, EITHER EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE
 * LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
 * LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS
 * INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF
 * OR INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGES
 */
#include "libnvpipecodec/nvpipecodec264.h"
#include <cstdio>
#include <limits>

#define NVPIPE_H264_ENCODER_NAME "h264_nvenc"
#define NVPIPE_H264_DECODER_NAME "h264_cuvid"

NvPipeCodec264::NvPipeCodec264() {
    //printf("nv_codec_h264 created\n");

    encoder_context_ = NULL;
    encoder_codec_ = NULL;
    encoder_frame_ = NULL;

    decoder_context_ = NULL;
    decoder_codec_ = NULL;
    decoder_frame_ = NULL;

    encoder_frame_pixel_format_ = AV_PIX_FMT_RGB24; 

    encoder_config_corrupted_ = true;
    decoder_config_corrupted_ = true;

    encoder_conversion_flag_ = NVPIPE_IMAGE_FORMAT_CONVERSION_NULL;
    decoder_conversion_flag_ = NVPIPE_IMAGE_FORMAT_CONVERSION_NULL;

    encoder_converted_image_buffer_ = NULL;
    encoder_converted_image_buffer_size_ = 0;

    // register all available file formats and codecs
    // could be initialized multiple times.
    av_register_all();
}

NvPipeCodec264::~NvPipeCodec264() {
    //printf("nv_codec_h264 destroyed\n");

    if (decoder_codec_ && decoder_context_) {
        avcodec_close(decoder_context_);
    }

    if (decoder_context_) {
        av_free(decoder_context_);
    }

    if (decoder_frame_) {
        av_frame_free(&decoder_frame_);
    }
    
    if ( encoder_converted_image_buffer_ ) {
        free ( encoder_converted_image_buffer_ );
    }

    if (encoder_codec_ && encoder_context_) {
        avcodec_close(encoder_context_);
    }

    if (encoder_context_) {
        av_free(encoder_context_);
    }

    if (encoder_frame_) {
        av_frame_free(&encoder_frame_);
    }

}

void NvPipeCodec264::setImageSize( int width, int height) {
    if ( width != width_ || height != height_ ) {
        encoder_config_corrupted_ = true;
        decoder_config_corrupted_ = true;
    }
    NvPipeCodec::setImageSize(width, height);
}

void NvPipeCodec264::setInputFrameBuffer(   void* frame_buffer, 
                                            size_t buffer_size)
{
    if ( frame_buffer != frame_ ) {
        encoder_config_corrupted_ = true;
        NvPipeCodec::setInputFrameBuffer(frame_buffer, buffer_size);
    } else if ( buffer_size != frame_buffer_size_ ) {
        NvPipeCodec::setInputFrameBuffer(frame_buffer, buffer_size);
    }
}

int NvPipeCodec264::encode( void* buffer, 
                            size_t &output_buffer_size,
                            enum NVPipeImageFormat format) {

    int result = 0;

    if (width_ == 0 || height_ == 0 ) {
            printf("input frame has to be defined \
                    before calling NvPipeCodec264::encoding");
            return -1;
    }

    if (format != encoder_format_) {
        getFormatConversionEnum(    format, true,
                                    encoder_conversion_flag_,
                                    encoder_frame_pixel_format_);
        encoder_config_corrupted_ = true;
        encoder_format_ = format;
    }

    // Check if encoder codec has been initialized
    if (encoder_codec_ == NULL) {
        encoder_codec_ = 
            avcodec_find_encoder_by_name(NVPIPE_H264_ENCODER_NAME);
        if (encoder_codec_ == NULL) {
            printf("cannot find encoder: %s", NVPIPE_H264_ENCODER_NAME);
            return -1;
        }

        encoder_context_ = avcodec_alloc_context3( encoder_codec_ );
        if (encoder_context_ == NULL) {
            printf("cannot allocate codec context");
            return -1;
        }

        encoder_frame_ = av_frame_alloc();
        if (encoder_frame_ == NULL) {
            printf("cannot allocate frame");
            return -1;
        }

        /*
         * setup codecContext
         * Default low latency setup for nvenc
         */
        // put bit_rate
        if ( !bitrate_overwrite_flag_ ) {
            bitrate_ = width_;
            bitrate_ *= height_;
            bitrate_ *= framerate_;
            bitrate_ *= 0.07;
        }
        encoder_context_->bit_rate = bitrate_;
        // frames per second
        encoder_context_->time_base = (AVRational){1,framerate_};
        encoder_context_->gop_size = gop_size_;
        encoder_context_->max_b_frames = 0;
        encoder_context_->width = width_;
        encoder_context_->height = height_;
        encoder_context_->pix_fmt = encoder_frame_pixel_format_;
        // nvenc private setting
        av_opt_set(encoder_context_->priv_data,
                    "preset", "llhq", 0);
        av_opt_set(encoder_context_->priv_data,
                    "rc", "ll_2pass_quality", 0);
        av_opt_set_int(encoder_context_->priv_data, "cbr", 1, 0);    
        av_opt_set_int(encoder_context_->priv_data, "2pass", 1, 0);
        av_opt_set_int(encoder_context_->priv_data, "delay", 0, 0);


        encoder_frame_->format = encoder_frame_pixel_format_;
        encoder_frame_->width = width_;
        encoder_frame_->height = height_;

        if (avcodec_open2(encoder_context_, encoder_codec_, NULL) 
            != 0) {
            printf("cannot open codec\n");
            return -1;
        }
        
        encoder_config_corrupted_ = true;
    }


    // Check if encoder frame parameter has been updated
    if (encoder_config_corrupted_) {
        //printf("encoder configuring...\n");
        if ( !bitrate_overwrite_flag_ ) {
            bitrate_ = width_;
            bitrate_ *= height_;
            bitrate_ *= framerate_;
            bitrate_ *= 0.07;
        }
        encoder_context_->bit_rate = bitrate_;
        encoder_context_->width = width_;
        encoder_context_->height = height_;
        encoder_context_->pix_fmt = encoder_frame_pixel_format_;
        
        encoder_frame_->format = encoder_frame_pixel_format_;
        encoder_frame_->width = width_;
        encoder_frame_->height = height_;

        const uint8_t* frame_image_ptr;
        size_t num_pixels = width_;
        num_pixels *= height_;

        switch ( encoder_conversion_flag_ ) {
            case NVPIPE_IMAGE_FORMAT_CONVERSION_ARGB_TO_NV12:
            case NVPIPE_IMAGE_FORMAT_CONVERSION_RGB_TO_NV12:
                if ( encoder_converted_image_buffer_size_ < 
                                                num_pixels * 3 / 2 ) {
                    if ( encoder_converted_image_buffer_ ) {
                        printf("free format conversion buffer in encoder");
                        free( encoder_converted_image_buffer_ );
                    }

                    encoder_converted_image_buffer_size_=num_pixels*3/2;
                    encoder_converted_image_buffer_ = (void*) 
                                malloc ( sizeof(uint8_t) * 
                                encoder_converted_image_buffer_size_);
                }
                frame_image_ptr =
                    (const uint8_t*) encoder_converted_image_buffer_;
                break;
            default:
                frame_image_ptr = (const uint8_t*) frame_;
                break;
        }

        // setup input data buffer to encoder_frame_
        // Note the allocation of data buffer is done by user
        if ( av_image_fill_arrays ( encoder_frame_->data, 
                                    encoder_frame_->linesize,
                                    frame_image_ptr, 
                                    encoder_frame_pixel_format_,
                                    width_,
                                    height_,
                                    64 ) < 0 ) {
            printf("could not associate image buffer to frame");
            return -1;
        }
        encoder_config_corrupted_ = false;
    }

    switch ( encoder_conversion_flag_ ) {
    case NVPIPE_IMAGE_FORMAT_CONVERSION_ARGB_TO_NV12:
    case NVPIPE_IMAGE_FORMAT_CONVERSION_RGB_TO_NV12:
        formatConversion(width_, height_, 
                        frame_, encoder_converted_image_buffer_,
                        encoder_conversion_flag_);
        break;
    default:
        break;
    }

    av_init_packet(&encoder_packet_);
    encoder_packet_.data = NULL;
    encoder_packet_.size = 0;

    int ret = avcodec_send_frame(encoder_context_, encoder_frame_);
    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_packet went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    ret = avcodec_receive_packet(encoder_context_, &encoder_packet_);
    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_packet went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    if ( encoder_packet_.size < output_buffer_size - 10 ) {
        memcpy(buffer, encoder_packet_.data, encoder_packet_.size);
        appendDummyNAL(buffer, encoder_packet_.size);
        result = -1;
    } else {
        printf("packet size larger than  buffer_size went wrong!\n");
    }

    // output the packet size;
    output_buffer_size = encoder_packet_.size + 10;
    av_packet_unref(&encoder_packet_);

    return result;
}

int NvPipeCodec264::decode( void* output_picture, 
                            int &width, 
                            int &height, 
                            size_t &output_size,
                            enum NVPipeImageFormat format ) {

    enum AVPixelFormat pixel_format;

    // Check if decoder codec has been initialized
    if (decoder_codec_ == NULL) {
        decoder_codec_ = 
            avcodec_find_decoder_by_name(NVPIPE_H264_DECODER_NAME);
        
        if (decoder_codec_ == NULL) {
            printf("cannot find decoder: %s", NVPIPE_H264_DECODER_NAME);
            return -1;
        }

        decoder_context_ = avcodec_alloc_context3(decoder_codec_);
        if (decoder_context_ == NULL) {
            printf("cannot allocate codec context");
            return -1;
        }

        decoder_frame_ = av_frame_alloc();
        if (decoder_frame_ == NULL) {
            printf("cannot allocate frame");
            return -1;
        }

        decoder_context_->delay = 0;

        decoder_config_corrupted_ = true;
    }

    getFormatConversionEnum(format,
                            false,
                            decoder_conversion_flag_,
                            pixel_format);

    if ( decoder_config_corrupted_ ) {
        //printf("decoder configuring...\n");
        if (avcodec_open2(decoder_context_, 
                            decoder_codec_, NULL) < 0) {
            printf("cannot open codec\n");
            return -1;
        }
        decoder_config_corrupted_ = false;
    }

    av_init_packet(&decoder_packet_);
    decoder_packet_.data = (uint8_t *) packet_;
    decoder_packet_.size = packet_buffer_size_;

    if ( avcodec_send_packet(   decoder_context_,
                                &decoder_packet_) != 0 ) {
        printf("send_packet went wrong!\n");
    }

    int ret = avcodec_receive_frame( decoder_context_,
                                    decoder_frame_);
    // debug information (remove before release)
    if ( ret < 0 ){
        printf("\nreceive_frame went wrong! %d\n", ret);
        switch(ret) {
            case AVERROR(EOF):
                printf("eof\n");
                break;
            case AVERROR(EAGAIN):
                printf("EAGAIN\n");
                break;
            case AVERROR(EINVAL):
                printf("EINVAL\n");
                break;
            case AVERROR(ENOMEM):
                printf("ENOMEN\n");
                break;
        }
    }

    size_t frameSize;
    width = decoder_frame_->width;
    height = decoder_frame_->height;

    // should really check the decoder_frame_->format
    switch ( decoder_conversion_flag_ ) {
    case NVPIPE_IMAGE_FORMAT_CONVERSION_NV12_TO_ARGB:
        frameSize = width;
        frameSize *= height;
        frameSize *= 4;
        break;

    case NVPIPE_IMAGE_FORMAT_CONVERSION_NV12_TO_RGB:
        frameSize = width;
        frameSize *= height;
        frameSize *= 3;
        if (frameSize > output_size ) {
            output_size = frameSize;
            av_packet_unref(&decoder_packet_);
            printf("frame size larger than frame_buffer_size_,\
                    something went wrong!\n");
            return -1;
        }
        output_size = frameSize;
        formatConversionAVFrameRGB(decoder_frame_, output_picture);
        av_packet_unref(&decoder_packet_);
        return 0;
        break;

    default:
        frameSize = 0;
        for ( int i = 0; i < AV_NUM_DATA_POINTERS; i++ ) {
            if ( decoder_frame_->linesize[i] > 0 ) {
                frameSize += decoder_frame_->height*decoder_frame_->linesize[i];
            }
        }

        if (frameSize > output_size ) {
            output_size = frameSize;
            av_packet_unref(&decoder_packet_);
            printf("frame size larger than frame_buffer_size_,\
                    something went wrong!\n");
            return -1;
        }
        output_size = frameSize;

        size_t frameSize = 0;
        size_t buffer_offset = 0;
        for ( int i = 0; i < AV_NUM_DATA_POINTERS; i++ ) {
            if ( decoder_frame_->linesize[i] > 0 ) {
                frameSize = decoder_frame_->height*decoder_frame_->linesize[i];
                memcpy(output_picture + buffer_offset, decoder_frame_->data[i], frameSize);
                buffer_offset += frameSize;
                //printf ("iteration: %d  data: %d; linesize: %d, width: %d, height %d\n", i, decoder_frame_->data[i], decoder_frame_->linesize[i], decoder_frame_->width, decoder_frame_->height);
            }
        }

        av_packet_unref(&decoder_packet_);
        return 0;
    }

    // not using this code any more
    if (frameSize > output_size ) {
        output_size = frameSize;
        av_packet_unref(&decoder_packet_);
        printf("frame size larger than frame_buffer_size_,\
                something went wrong!\n");
        return -1;
    }

    formatConversion(width, height, 
                    decoder_frame_->data[0], 
                    output_picture,
                    decoder_conversion_flag_);
    av_packet_unref(&decoder_packet_);
    return 0;
}

int NvPipeCodec264::getFormatConversionEnum(
            enum NVPipeImageFormat format,
            bool encoder_flag,
            enum NVPipeImageFormatConversion &conversion_flag,
            enum AVPixelFormat &pixel_format) 
{
    switch( format ) {
    case NVPIPE_IMAGE_FORMAT_ARGB:
        pixel_format = AV_PIX_FMT_NV12;
        conversion_flag = encoder_flag ?
            NVPIPE_IMAGE_FORMAT_CONVERSION_ARGB_TO_NV12 :
            NVPIPE_IMAGE_FORMAT_CONVERSION_NV12_TO_ARGB;
        break;

    case NVPIPE_IMAGE_FORMAT_RGB:
        pixel_format = AV_PIX_FMT_NV12;
        conversion_flag = encoder_flag ?
            NVPIPE_IMAGE_FORMAT_CONVERSION_RGB_TO_NV12 :
            NVPIPE_IMAGE_FORMAT_CONVERSION_NV12_TO_RGB;
        break;

    case NVPIPE_IMAGE_FORMAT_YUV420P:
        pixel_format = AV_PIX_FMT_YUV420P;
        conversion_flag = NVPIPE_IMAGE_FORMAT_CONVERSION_NULL;
        break;

    case NVPIPE_IMAGE_FORMAT_YUV444P:
        pixel_format = AV_PIX_FMT_YUV444P;
        conversion_flag = NVPIPE_IMAGE_FORMAT_CONVERSION_NULL;
        break;

    default:
        printf("unrecognized pixel format, set to NV12 as default");
    case NVPIPE_IMAGE_FORMAT_NV12:
        pixel_format = AV_PIX_FMT_NV12;
        conversion_flag = NVPIPE_IMAGE_FORMAT_CONVERSION_NULL;
        break;
    }
}

void NvPipeCodec264::appendDummyNAL(void* buffer, size_t offset) {
    uint8_t* pkt_ptr = (uint8_t*)buffer;
    pkt_ptr += offset;
    pkt_ptr[0] = 0;
    pkt_ptr[1] = 0;
    pkt_ptr[2] = 1;
    pkt_ptr[3] = 9;
    pkt_ptr[4] = 0;
    pkt_ptr[5] = 0;
    pkt_ptr[6] = 0;
    pkt_ptr[7] = 1;
    pkt_ptr[8] = 9;
    pkt_ptr[9] = 0;
}
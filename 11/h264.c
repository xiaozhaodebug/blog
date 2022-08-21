#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#define WIDTH 640
#define HEIGHT 480
#define CAMERA_FMT AV_PIX_FMT_YUYV422
#define ENCODE_FMT AV_PIX_FMT_YUV420P
static AVFormatContext* open_dev()
{
    int ret = 0;
    char errors[1024] = {0,};
    char *devicename = "/dev/video0"; //0机子的摄像头
    //ctx
    AVFormatContext *fmt_ctx = NULL;
    AVDictionary *options = NULL;

    avdevice_register_all();
    //av_register_all();
    //get format
    AVInputFormat *iformat = av_find_input_format("video4linux2");

    //option 视频需要参数
    av_dict_set(&options,"video_size","640x480",0);
    // av_dict_set(&options,"-codec","rawvideo",0);
    av_dict_set(&options,"pixel_format","yuyv422",0);
    
    if((ret = avformat_open_input(&fmt_ctx,devicename,iformat,&options)) < 0)
    {
        av_strerror(ret,errors,1024);
        fprintf(stderr,"Failed to open video device ,[%d]%s\n",ret,errors);
        return NULL;
    }
    return fmt_ctx;
}
static int open_encoder(int width, int height, AVCodecContext **enc_ctx)
{
    AVCodec *codec = NULL;
    int ret = 0;
    codec = avcodec_find_encoder_by_name("libx264");
    if(!codec){
        printf("Codec libx264 not found\n");	
        exit(1);
    }

    *enc_ctx = avcodec_alloc_context3(codec);
    if(!enc_ctx){
        printf("Could not allocate video codec contex\n");
        exit(1);
    }
    //SPS /PPS
    (*enc_ctx)->profile = FF_PROFILE_H264_HIGH_444;
    (*enc_ctx)->level = 50;

    (*enc_ctx)->width = width;
    (*enc_ctx)->height = height;
    //GOP IDR帧  延迟性恢复
    (*enc_ctx)->gop_size = 250;
    (*enc_ctx)->keyint_min = 25;

    //设置B帧数据  减少码流
    (*enc_ctx)->max_b_frames = 3;
    (*enc_ctx)->has_b_frames = 1;

    //参考帧的数量 数据越大 处理越慢 但是还原出来效果会更好
    (*enc_ctx)->refs = 3;

    //像素格式
    (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;

    //码流
    (*enc_ctx)->bit_rate = 1000000;//1000

     //码率
     (*enc_ctx)->time_base = (AVRational){1,25};//帧与帧之间的间隔是time_base
     (*enc_ctx)->framerate = (AVRational){25,1};//帧率，每秒 25 帧。

    ret = avcodec_open2((*enc_ctx),codec,NULL);
    if(ret < 0){
        printf("Could not open codec:%s!\n",av_err2str(ret));
        exit(1);
    }
}

static AVFrame* create_frame(int width,int height)
{
    AVFrame* frame = NULL;
    frame = av_frame_alloc();
    int ret = 0;
    if(!frame)
    {
        printf("Error ,No Memory!\n");
        goto __ERROR;
    }

    frame->width = width;

    frame->height = height;
    frame->format = AV_PIX_FMT_YUV420P;

    ret = av_frame_get_buffer(frame ,32);
    if(ret < 0){
        printf("Error,Failed to alloc buffer for Frame\n");
        goto __ERROR;
    }
    return frame;
__ERROR:
    if(frame)
    {
        av_frame_free(&frame);
    }
    return NULL;
}

static void encode(AVCodecContext* enc_ctx, 
                    AVFrame* frame,
                    AVPacket *newpkkt,
                    FILE* outfile){
    int ret = 0;   //over
    if(frame)
    {
             printf("send frame to encoder,pts=%ld",frame->pts);
    }             
   
    ret = avcodec_send_frame(enc_ctx, frame);
    if(ret < 0)
    {
        printf("Error Failth send a frame for encoder!\n");
        exit(1);
    }
    while(ret >= 0)
    {
        ret = avcodec_receive_packet(enc_ctx, newpkkt);

        //如果编码数据不足
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return ;
        }
        else if(ret < 0)
        {
            printf("Error,Failth to encodec\n");
            exit(1);
        }
        fwrite(newpkkt->data, 1,newpkkt->size, outfile);
        av_packet_unref(newpkkt);
    }
}

void rec_video(){
    int ret = 0;
    int flag=-1;
    int Base_num = 0;
    char input_command[128];
    AVCodecContext* enc_ctx = NULL;
    AVFormatContext* fmt_ctx = NULL;
   // AVPacket pkt;//数据存放
    flag=fcntl(0,F_GETFL); //获取当前flag
    av_log_set_level(AV_LOG_DEBUG);

    char* out = "./video.h264";
    char* yuv_file_path = "./video.yuv";
    FILE* outfile = fopen(out,"wb+");
    FILE* yuv_file = fopen(yuv_file_path,"wb+");
    //打开设备
    fmt_ctx = open_dev();
    //打开编码器
    open_encoder(WIDTH, HEIGHT, &enc_ctx);//输入函数放在前面

    AVFrame* frame = create_frame(WIDTH,HEIGHT);

    AVPacket* newpkt = NULL;
    newpkt = av_packet_alloc();
    if(!newpkt)
    {
        printf("Error, Failth to alloc avpacket1\n");
        goto __ERROR;
    }
    struct SwsContext *sws_ctx = NULL;
    // 图像格式转换：CAMERA_FMT --> ENCODE_FMT
    sws_ctx = sws_getContext(WIDTH, HEIGHT, CAMERA_FMT,
                             WIDTH, HEIGHT, ENCODE_FMT, 0, NULL, NULL, NULL);
    uint8_t *yuy2buf[4] = {NULL,};
    int yuy2_linesize[4] = {0,};
    int yuy2_size = av_image_alloc(yuy2buf, yuy2_linesize, WIDTH, HEIGHT, CAMERA_FMT, 1);

    uint8_t *yuv420pbuf[4] = {NULL,};
    int yuv420p_linesize[4] = {0,};
    int yuv420p_size = av_image_alloc(yuv420pbuf, yuv420p_linesize, WIDTH, HEIGHT, ENCODE_FMT, 1);

    flag |=O_NONBLOCK; //设置新falg
    fcntl(0,F_SETFL,flag); //更新flag

    //av_init_packet(&pkt);//数据初始化 干净的空间；
     // 初始化packet，存放编码数据
    AVPacket *camera_packet = av_packet_alloc();

    // 初始化frame，存放原始数据
    int y_size = WIDTH * HEIGHT;
    int frame_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, WIDTH, HEIGHT, 1);

    while((ret = av_read_frame(fmt_ctx,camera_packet)) == 0)
    {
        av_log(NULL,AV_LOG_DEBUG,
                    "packet size is %d(%p)\n",
                    camera_packet->size,camera_packet->data);

  
    //     //YUV数据格式转换YUV420
    //     // YUYV YUYV YUYV YUYV YUYV YUYV YUYV
    //     //YYYYYYYY UU VV
    //    // memcpy(frame->data[0],)

    //     // 图像格式转化
        memcpy(yuy2buf[0], camera_packet->data, camera_packet->size);
        sws_scale(sws_ctx, (const uint8_t **)yuy2buf, yuy2_linesize,
                  0, HEIGHT, frame->data, yuv420p_linesize);

        fwrite(frame->data[0], 1, y_size, yuv_file);
        fwrite(frame->data[1], 1, y_size/4, yuv_file);
        fwrite(frame->data[2], 1, y_size/4, yuv_file);
        fflush(yuv_file);

        //放入数据
        // avcodec_send_frame(fmt_ctx)
        // //得到编码后的数据
        // avcodec_receive_packet()
            //编码
        frame->pts = Base_num++;
        encode(enc_ctx,frame,newpkt,outfile);

        av_packet_unref(camera_packet);//内存清空

        if((ret=read(0,input_command,sizeof(input_command))) > 0)
        {
            if(strncmp(input_command, "over",2) == 0)
            {
                av_log(NULL,AV_LOG_DEBUG,"over\n");
                break;
            }
            else
            {
                av_log(NULL,AV_LOG_DEBUG,"请重新输入\n");
            }
            memset(input_command, 0, sizeof(input_command));
        }
    }
    encode(enc_ctx,NULL,newpkt,outfile);//解决数据不完整
    printf("EEEE\n");
__ERROR:
    
    av_packet_free(&camera_packet);
    if(yuv_file)
    {
        fclose(yuv_file);
    }
    if(outfile)
    {
        fclose(outfile);
    }
    if(fmt_ctx)
    {
        avformat_close_input(&fmt_ctx);
    }

    av_log(NULL, AV_LOG_DEBUG,"finish\n");
}

int main(int argc, char** argv)
{
    rec_video();
    return 0;
}
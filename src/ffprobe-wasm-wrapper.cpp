#include <vector>
#include <string>
#include <vector>
#include <inttypes.h>
#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/bprint.h>
#include <libavutil/imgutils.h>
};

const std::string c_avformat_version() {
    return AV_STRINGIFY(LIBAVFORMAT_VERSION);
}

const std::string c_avcodec_version() {
    return AV_STRINGIFY(LIBAVCODEC_VERSION);
}

const std::string c_avutil_version() {
    return AV_STRINGIFY(LIBAVUTIL_VERSION);
}

typedef struct Tag {
  std::string key;
  std::string value;
} Tag;

typedef struct Stream {
  int index;
  int id;
  float start_time;
  float duration;
  int codec_type;
  std::string codec_name;
  std::string codec_tag_string;
  std::string format;
  float bit_rate;
  std::string profile;
  int level;
  int width;
  int height;
  int channels;
  int sample_rate;
  int frame_size;
  std::string avg_frame_rate;
  std::string r_frame_rate;
  std::string time_base;
  int nb_frames;
  std::vector<Tag> tags;
} Stream;

typedef struct Chapter {
  int id;
  std::string time_base;
  float start;
  float end;
  std::vector<Tag> tags;
} Chapter;

typedef struct FileInfoResponse {
  std::string name;
  std::string filename;
  float bit_rate;
  float duration;
  std::string url;
  int nb_streams;
  int flags;
  int nb_chapters;
  float start_time;
  std::vector<Stream> streams;
  std::vector<Chapter> chapters;
} FileInfoResponse;

typedef struct Frame {
  int frame_number;
  char pict_type;
  int pts;
  int dts;
  int pos;
  int pkt_size;
} Frame;

typedef struct FramesResponse {
  std::vector<Frame> frames;
  int nb_frames;
  int gop_size;
  float duration;
  double time_base;
  double avg_frame_rate;
} FramesResponse;

AVBPrint format_ration_to_string(AVRational r) {
  AVBPrint buf;
  av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
  av_bprintf(&buf, "%d%s%d", r.num, (char *)"/", r.den);
  return buf;
}

FileInfoResponse get_file_info(std::string filename) {
    av_log_set_level(AV_LOG_QUIET); // No logging output for libav.

    FILE *file = fopen(filename.c_str(), "rb");
    if (!file) {
      printf("cannot open file\n");
    }
    fclose(file);

    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
      printf("ERROR: could not allocate memory for Format Context\n");
    }

    // Open the file and read header.
    int ret;
    if ((ret = avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL)) < 0) {
      printf("ERROR opening file: %s\n", av_err2str(ret));
    }

    // Get stream info from format.
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
      printf("ERROR: could not get stream info\n");
    }

    // Initialize response struct with format data.
    FileInfoResponse r = {
      .name = pFormatContext->iformat->name,
      .filename = pFormatContext->url,
      .bit_rate = (float)pFormatContext->bit_rate,
      .duration = (float)(pFormatContext->duration / AV_TIME_BASE),
      .url = pFormatContext->url,
      .nb_streams = (int)pFormatContext->nb_streams,
      .flags = pFormatContext->flags,
      .nb_chapters = (int)pFormatContext->nb_chapters,
      .start_time = (float)pFormatContext->start_time,
    };
    
    // Loop through the streams.
    for (int i = 0; i < pFormatContext->nb_streams; i++) {
      AVCodecParameters *pLocalCodecParameters = NULL;
      pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

      // Convert to char byte array.
      uint32_t n = pLocalCodecParameters->codec_tag;
      char fourcc[5];
      for (int j = 0; j < 4; ++j) {
        fourcc[j] = (n >> (j * 8) & 0xFF);
      }
      fourcc[4] = 0x00; // NULL terminator.

      Stream stream = {
        .index = (int)pFormatContext->streams[i]->index,
        .id = (int)pFormatContext->streams[i]->id,
        .start_time = (float)pFormatContext->streams[i]->start_time,
        .duration = (float)(pFormatContext->streams[i]->duration / AV_TIME_BASE),
        .codec_type = (int)pLocalCodecParameters->codec_type,
        .codec_name = avcodec_get_name(pLocalCodecParameters->codec_id),
        .codec_tag_string = fourcc,
        .format = av_get_pix_fmt_name((AVPixelFormat)pLocalCodecParameters->format),
        .bit_rate = (float)pLocalCodecParameters->bit_rate,
        .profile = avcodec_profile_name(pLocalCodecParameters->codec_id, pLocalCodecParameters->profile),
        .level = (int)pLocalCodecParameters->level,
        .width = (int)pLocalCodecParameters->width,
        .height = (int)pLocalCodecParameters->height,
        .channels = (int)pLocalCodecParameters->channels,
        .sample_rate = (int)pLocalCodecParameters->sample_rate,
        .frame_size = (int)pLocalCodecParameters->frame_size,
        .avg_frame_rate = format_ration_to_string(pFormatContext->streams[i]->avg_frame_rate).str,
        .r_frame_rate = format_ration_to_string(pFormatContext->streams[i]->r_frame_rate).str,
        .time_base = format_ration_to_string(pFormatContext->streams[i]->time_base).str,
        .nb_frames = (int)pFormatContext->streams[i]->nb_frames,
      };

      // Add tags to stream.
      const AVDictionaryEntry *tag = NULL;
      while ((tag = av_dict_get(pFormatContext->streams[i]->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        Tag t = {
          .key = tag->key,
          .value = tag->value,
        };
        stream.tags.push_back(t);
      }

      r.streams.push_back(stream);
    }

    avformat_close_input(&pFormatContext);
    return r;
}

FramesResponse get_frames(std::string filename, int timestamp) {
    av_log_set_level(AV_LOG_QUIET); // No logging output for libav.

    FILE *file = fopen(filename.c_str(), "rb");
    if (!file) {
      printf("cannot open file\n");
    }
    fclose(file);

    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
      printf("ERROR: could not allocate memory for Format Context\n");
    }

    // Open the file and read header.
    int ret;
    if ((ret = avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL)) < 0) {
        printf("ERROR: %s\n", av_err2str(ret));
    }

    // Get stream info from format.
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
      printf("ERROR: could not get stream info\n");
    }

    // Get streams data.
    AVCodec  *pCodec = NULL;
    AVCodecParameters *pCodecParameters = NULL;
    int video_stream_index = -1;
    int nb_frames = 0;

    // Loop through the streams.
    for (int i = 0; i < pFormatContext->nb_streams; i++) {
      AVCodecParameters *pLocalCodecParameters = NULL;
      pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

      // Print out the decoded frame info.
      AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
      if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (video_stream_index == -1) {
          video_stream_index = i;
          nb_frames = pFormatContext->streams[i]->nb_frames;

          // Calculate the nb_frames for MKV/WebM if nb_frames is 0.
          if (nb_frames == 0) {
            nb_frames = (pFormatContext->duration / AV_TIME_BASE) * pFormatContext->streams[i]->avg_frame_rate.num;
          }
          pCodec = pLocalCodec;
          pCodecParameters = pLocalCodecParameters;
        }
      }
    }

    AVRational stream_time_base = pFormatContext->streams[video_stream_index]->time_base;
    AVRational avg_frame_rate = pFormatContext->streams[video_stream_index]->avg_frame_rate;
    // printf("stream_time_base: %d / %d = %.5f\n", stream_time_base.num, stream_time_base.den, av_q2d(stream_time_base));

    FramesResponse r;
    r.nb_frames = nb_frames;
    r.time_base = av_q2d(stream_time_base);
    r.avg_frame_rate = av_q2d(avg_frame_rate);
    r.duration = pFormatContext->streams[video_stream_index]->duration;

    // If the duration value isn't in the stream, get from the FormatContext.
    if (r.duration == 0) {
      r.duration = pFormatContext->duration * r.time_base;
    }

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecContext, pCodecParameters);
    avcodec_open2(pCodecContext, pCodec, NULL);

    AVPacket *pPacket = av_packet_alloc();
    AVFrame *pFrame = av_frame_alloc();

    int max_packets_to_process = 1000;
    int frame_count = 0;
    int key_frames = 0;

    // Seek to frame from the given timestamp.
    av_seek_frame(pFormatContext, video_stream_index, timestamp, AVSEEK_FLAG_ANY);

    // Read video frames.
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
      if (pPacket->stream_index == video_stream_index) {
          int response = 0;
          response = avcodec_send_packet(pCodecContext, pPacket);

          if (response >= 0) {
            response = avcodec_receive_frame(pCodecContext, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
              continue;
            }

            // Track keyframes so we paginate by each GOP.
            if (pFrame->key_frame == 1) key_frames++;

            // Break at the next keyframe found.
            if (key_frames > 1) break;

            Frame f = {
              .frame_number = frame_count,
              .pict_type = (char) av_get_picture_type_char(pFrame->pict_type),
              .pts = (int) pPacket->pts,
              .dts = (int) pPacket->dts,
              .pos = (int) pPacket->pos,
              .pkt_size = pFrame->pkt_size,
            };
            r.frames.push_back(f);

            if (--max_packets_to_process <= 0) break;
          }
        frame_count++;
      }
      av_packet_unref(pPacket);
    }
    r.gop_size = frame_count;

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);

    return r;
}

EMSCRIPTEN_BINDINGS(constants) {
    function("avformat_version", &c_avformat_version);
    function("avcodec_version", &c_avcodec_version);
    function("avutil_version", &c_avutil_version);
}

EMSCRIPTEN_BINDINGS(structs) {
  emscripten::value_object<Tag>("Tag")
  .field("key", &Tag::key)
  .field("value", &Tag::value);
  register_vector<Tag>("Tag");

  emscripten::value_object<Stream>("Stream")
  .field("index", &Stream::index)
  .field("id", &Stream::id)
  .field("start_time", &Stream::start_time)
  .field("duration", &Stream::duration)
  .field("codec_type", &Stream::codec_type)
  .field("codec_name", &Stream::codec_name)
  .field("codec_tag_string", &Stream::codec_tag_string)
  .field("format", &Stream::format)
  .field("bit_rate", &Stream::bit_rate)
  .field("profile", &Stream::profile)
  .field("level", &Stream::level)
  .field("width", &Stream::width)
  .field("height", &Stream::height)
  .field("channels", &Stream::channels)
  .field("sample_rate", &Stream::sample_rate)
  .field("frame_size", &Stream::frame_size)
  .field("tags", &Stream::tags)
  .field("avg_frame_rate", &Stream::avg_frame_rate)
  .field("r_frame_rate", &Stream::r_frame_rate)
  .field("time_base", &Stream::time_base)
  .field("nb_frames", &Stream::nb_frames);
  register_vector<Stream>("Stream");

  emscripten::value_object<Chapter>("Chapter")
  .field("id", &Chapter::id)
  .field("time_base", &Chapter::time_base)
  .field("start", &Chapter::start)
  .field("end", &Chapter::end)
  .field("tags", &Chapter::tags);
  register_vector<Chapter>("Chapter");

  emscripten::value_object<Frame>("Frame")
  .field("frame_number", &Frame::frame_number)
  .field("pict_type", &Frame::pict_type)
  .field("pts", &Frame::pts)
  .field("dts", &Frame::dts)
  .field("pos", &Frame::pos)
  .field("pkt_size", &Frame::pkt_size);
  register_vector<Frame>("Frame");

  emscripten::value_object<FileInfoResponse>("FileInfoResponse")
  .field("name", &FileInfoResponse::name)
  .field("filename", &FileInfoResponse::filename)
  .field("duration", &FileInfoResponse::duration)
  .field("bit_rate", &FileInfoResponse::bit_rate)
  .field("url", &FileInfoResponse::url)
  .field("nb_streams", &FileInfoResponse::nb_streams)
  .field("flags", &FileInfoResponse::flags)
  .field("streams", &FileInfoResponse::streams)
  .field("start_time", &FileInfoResponse::start_time)
  .field("nb_chapters", &FileInfoResponse::nb_chapters)
  .field("chapters", &FileInfoResponse::chapters);
  function("get_file_info", &get_file_info);

  emscripten::value_object<FramesResponse>("FramesResponse")
  .field("frames", &FramesResponse::frames)
  .field("nb_frames", &FramesResponse::nb_frames)
  .field("gop_size", &FramesResponse::gop_size)
  .field("duration", &FramesResponse::duration)
  .field("time_base", &FramesResponse::time_base)
  .field("avg_frame_rate", &FramesResponse::avg_frame_rate);
  function("get_frames", &get_frames);
}

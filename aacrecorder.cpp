#include "aacrecorder.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>

AACRecorder::AACRecorder(QObject *parent)
    : QObject(parent)
{
    // 注册所有编解码器
    avformat_network_init();
}

AACRecorder::~AACRecorder()
{
    close();
}

bool AACRecorder::initialize(const QString &outputFile, int sampleRate, int channels, int bitrate)
{
    QMutexLocker locker(&m_mutex);
    qDebug() << "=== AACRecorder 初始化开始 ===";

    if (m_initialized) {
        close();
    }

    m_outputFile = outputFile;
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_bitrate = bitrate;

    qDebug() << "初始化编码器参数:";
    qDebug() << "  文件:" << outputFile;
    qDebug() << "  采样率:" << sampleRate;
    qDebug() << "  声道数:" << channels;
    qDebug() << "  比特率:" << bitrate;

    // 检查声道数是否有效
    if (channels != 1 && channels != 2) {
        qWarning() << "不支持的声道数:" << channels;
        return false;
    }

    // 检查文件路径是否有效
    QFileInfo fileInfo(outputFile);
    QDir dir = fileInfo.absoluteDir();

    if (!dir.exists()) {
        qDebug() << "创建目录:" << dir.absolutePath();
        if (!dir.mkpath(".")) {
            qWarning() << "无法创建目录:" << dir.absolutePath();
            return false;
        }
    }

    // 初始化编码器
    if (!initEncoder()) {
        cleanup();
        return false;
    }

    // 初始化重采样器
    if (!initResampler()) {
        cleanup();
        return false;
    }

    m_initialized = true;
    m_pts = 0;
    m_inputBuffer.clear();

    qDebug() << "=== AACRecorder 初始化完成 ===";
    return true;
}

bool AACRecorder::initEncoder()
{
    int ret = 0;

    // 创建MP4格式上下文
    ret = avformat_alloc_output_context2(&m_formatCtx, nullptr, "mp4", m_outputFile.toUtf8().constData());
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "无法创建MP4容器:" << errorBuf;
        return false;
    }

    // 查找AAC编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        qWarning() << "未找到AAC编码器";
        return false;
    }

    // 创建音频流
    m_stream = avformat_new_stream(m_formatCtx, codec);
    if (!m_stream) {
        qWarning() << "无法创建音频流";
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qWarning() << "无法分配编码器上下文";
        return false;
    }

    // 设置编码参数
    m_codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_codecCtx->bit_rate = m_bitrate;
    m_codecCtx->sample_rate = m_sampleRate;
    m_codecCtx->time_base = (AVRational){1, m_sampleRate};

    // 设置声道布局
    AVChannelLayout channel_layout;
    if (m_channels == 1) {
        av_channel_layout_default(&channel_layout, 1);
    } else {
        av_channel_layout_default(&channel_layout, 2);
    }
    av_channel_layout_copy(&m_codecCtx->ch_layout, &channel_layout);
    av_channel_layout_uninit(&channel_layout);

    // 打开编码器
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "无法打开编码器:" << errorBuf;
        return false;
    }

    // 复制编码器参数到流
    ret = avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);
    if (ret < 0) {
        qWarning() << "无法复制编码器参数到流";
        return false;
    }

    // 设置流的time_base
    m_stream->time_base = m_codecCtx->time_base;

    // 打开输出文件（FFmpeg统一管理文件操作）
    ret = avio_open(&m_formatCtx->pb, m_outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "FFmpeg无法打开输出文件:" << errorBuf;
        return false;
    }

    // 写入文件头
    ret = avformat_write_header(m_formatCtx, nullptr);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "无法写入文件头:" << errorBuf;
        return false;
    }

    qDebug() << "MP4编码器初始化成功";
    return true;
}

bool AACRecorder::initResampler()
{
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }

    // 设置输入声道布局
    AVChannelLayout in_ch_layout;
    if (m_channels == 1) {
        av_channel_layout_default(&in_ch_layout, 1);
    } else {
        av_channel_layout_default(&in_ch_layout, 2);
    }

    // 设置输出声道布局（与编码器一致）
    AVChannelLayout out_ch_layout;
    if (m_channels == 1) {
        av_channel_layout_default(&out_ch_layout, 1);
    } else {
        av_channel_layout_default(&out_ch_layout, 2);
    }

    // 创建重采样器
    int ret = swr_alloc_set_opts2(&m_swrCtx,
                                 &out_ch_layout,                    // 输出声道布局
                                 m_codecCtx->sample_fmt,            // 输出格式
                                 m_codecCtx->sample_rate,           // 输出采样率
                                 &in_ch_layout,                     // 输入声道布局
                                 m_inputFormat,                     // 输入格式
                                 m_sampleRate,                      // 输入采样率
                                 0, nullptr);

    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);

    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "无法设置重采样器选项:" << errorBuf;
        return false;
    }

    ret = swr_init(m_swrCtx);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "无法初始化重采样器:" << errorBuf;
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
        return false;
    }

    qDebug() << "重采样器初始化成功";
    return true;
}

void AACRecorder::writeAudioData(const uint8_t *data, int size)
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized || !data || size <= 0) {
        return;
    }

    // 将新数据添加到缓冲区
    size_t oldSize = m_inputBuffer.size();
    m_inputBuffer.resize(oldSize + size);
    memcpy(m_inputBuffer.data() + oldSize, data, size);

    // 计算缓冲区中的总样本数
    int bytes_per_sample = av_get_bytes_per_sample(m_inputFormat);
    int total_samples = m_inputBuffer.size() / (m_channels * bytes_per_sample);

    // 编码器期望的帧大小
    int frame_size = m_codecCtx->frame_size;
    if (frame_size <= 0) {
        frame_size = 1024; // 默认值
    }

    // 处理缓冲区中的数据，直到不足一个完整的帧
    while (total_samples >= frame_size) {
        // 编码一个完整的帧
        if (!encodeAudioFrame(m_inputBuffer.data(), frame_size)) {
            qWarning() << "编码音频帧失败";
            break;
        }

        // 移除已处理的数据
        int processed_bytes = frame_size * m_channels * bytes_per_sample;
        if (processed_bytes < m_inputBuffer.size()) {
            // 将剩余数据移到缓冲区开头
            memmove(m_inputBuffer.data(),
                   m_inputBuffer.data() + processed_bytes,
                   m_inputBuffer.size() - processed_bytes);
        }
        m_inputBuffer.resize(m_inputBuffer.size() - processed_bytes);

        total_samples = m_inputBuffer.size() / (m_channels * bytes_per_sample);
    }
}

bool AACRecorder::encodeAudioFrame(const uint8_t *data, int samples)
{
    if (!data || samples <= 0) {
        return false;
    }

    // 分配输入帧
    AVFrame *in_frame = av_frame_alloc();
    if (!in_frame) {
        qWarning() << "无法分配输入帧";
        return false;
    }

    in_frame->nb_samples = samples;
    in_frame->format = m_inputFormat;
    in_frame->sample_rate = m_sampleRate;

    // 设置输入声道布局
    AVChannelLayout in_ch_layout;
    if (m_channels == 1) {
        av_channel_layout_default(&in_ch_layout, 1);
    } else {
        av_channel_layout_default(&in_ch_layout, 2);
    }

    int ret = av_channel_layout_copy(&in_frame->ch_layout, &in_ch_layout);
    av_channel_layout_uninit(&in_ch_layout);

    if (ret < 0) {
        qWarning() << "无法设置输入帧声道布局";
        av_frame_free(&in_frame);
        return false;
    }

    // 分配输入帧缓冲区
    ret = av_frame_get_buffer(in_frame, 0);
    if (ret < 0) {
        qWarning() << "无法分配输入帧缓冲区";
        av_frame_free(&in_frame);
        return false;
    }

    // 复制数据到输入帧
    if (av_sample_fmt_is_planar(m_inputFormat)) {
        for (int ch = 0; ch < m_channels; ch++) {
            int bytes_per_sample = av_get_bytes_per_sample(m_inputFormat);
            int channel_size = samples * bytes_per_sample;
            memcpy(in_frame->data[ch], data + ch * channel_size, channel_size);
        }
    } else {
        int total_size = samples * m_channels * av_get_bytes_per_sample(m_inputFormat);
        memcpy(in_frame->data[0], data, total_size);
    }

    // 分配输出帧
    AVFrame *out_frame = av_frame_alloc();
    if (!out_frame) {
        qWarning() << "无法分配输出帧";
        av_frame_free(&in_frame);
        return false;
    }

    out_frame->nb_samples = samples;
    out_frame->format = m_codecCtx->sample_fmt;
    out_frame->sample_rate = m_codecCtx->sample_rate;

    // 复制输出声道布局
    ret = av_channel_layout_copy(&out_frame->ch_layout, &m_codecCtx->ch_layout);
    if (ret < 0) {
        qWarning() << "无法设置输出帧声道布局";
        av_frame_free(&in_frame);
        av_frame_free(&out_frame);
        return false;
    }

    // 重采样
    ret = swr_convert_frame(m_swrCtx, out_frame, in_frame);
    if (ret < 0) {
        char errorBuf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
        qWarning() << "重采样失败:" << errorBuf;
        av_frame_free(&in_frame);
        av_frame_free(&out_frame);
        return false;
    }

    out_frame->pts = m_pts;
    m_pts += samples;

    // 编码帧
    bool success = encodeFrame(out_frame);

    // 清理
    av_frame_free(&in_frame);
    av_frame_free(&out_frame);

    return success;
}

bool AACRecorder::encodeFrame(AVFrame *frame)
{
    if (!frame) {
        int ret = avcodec_send_frame(m_codecCtx, nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            qWarning() << "发送刷新帧失败，错误代码:" << ret;
            return false;
        }
    } else {
        int ret = avcodec_send_frame(m_codecCtx, frame);
        if (ret < 0) {
            char errorBuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
            qWarning() << "发送帧到编码器失败:" << errorBuf;
            return false;
        }
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        qWarning() << "无法分配AVPacket";
        return false;
    }

    bool success = true;

    while (true) {
        int ret = avcodec_receive_packet(m_codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char errorBuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
            qWarning() << "接收编码包失败:" << errorBuf;
            success = false;
            break;
        }

        // 写入到MP4容器
        pkt->stream_index = m_stream->index;
        av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_stream->time_base);

        ret = av_interleaved_write_frame(m_formatCtx, pkt);
        if (ret < 0) {
            char errorBuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errorBuf, AV_ERROR_MAX_STRING_SIZE, ret);
            qWarning() << "写入帧失败:" << errorBuf;
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return success;
}

void AACRecorder::close()
{
    QMutexLocker locker(&m_mutex);

    if (!m_initialized) {
        return;
    }

    // 处理缓冲区中剩余的数据
    if (!m_inputBuffer.empty()) {
        int bytes_per_sample = av_get_bytes_per_sample(m_inputFormat);
        int remaining_samples = m_inputBuffer.size() / (m_channels * bytes_per_sample);

        if (remaining_samples > 0) {
            encodeAudioFrame(m_inputBuffer.data(), remaining_samples);
        }
        m_inputBuffer.clear();
    }

    // 刷新编码器
    if (m_codecCtx) {
        encodeFrame(nullptr);

        // 写入文件尾
        if (m_formatCtx) {
            av_write_trailer(m_formatCtx);
        }
    }

    // 关闭文件
    if (m_formatCtx && m_formatCtx->pb) {
        avio_closep(&m_formatCtx->pb);
    }

    cleanup();
    m_initialized = false;

    qDebug() << "AAC编码器已关闭";
}

void AACRecorder::cleanup()
{
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
    }

    // 清空缓冲区
    m_inputBuffer.clear();
    m_inputBuffer.shrink_to_fit();

    m_pts = 0;
}

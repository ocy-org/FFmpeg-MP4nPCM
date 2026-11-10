#ifndef AACRECORDER_H
#define AACRECORDER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

class AACRecorder : public QObject
{
    Q_OBJECT

public:
    explicit AACRecorder(QObject *parent = nullptr);
    ~AACRecorder();

    bool initialize(const QString &outputFile, int sampleRate, int channels, int bitrate);
    void writeAudioData(const uint8_t *data, int size);
    void close();
    bool isInitialized() const { return m_initialized; }

private:
    bool initEncoder();
    bool initResampler();
    bool encodeFrame(AVFrame *frame);
    void cleanup();
    bool encodeAudioFrame(const uint8_t *data, int samples);

    QString m_outputFile;

    // FFmpeg相关变量
    AVFormatContext *m_formatCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    AVStream *m_stream = nullptr;
    SwrContext *m_swrCtx = nullptr;

    // 音频参数
    int m_sampleRate = 44100;
    int m_channels = 2;
    int m_bitrate = 128000;
    AVSampleFormat m_inputFormat = AV_SAMPLE_FMT_S16;

    // 状态
    bool m_initialized = false;
    int64_t m_pts = 0;
    QMutex m_mutex;

    // 输入缓冲区
    std::vector<uint8_t> m_inputBuffer;
};

#endif

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QAudioInput>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QTimer>
#include <QFile>
#include "aacrecorder.h"
#include <QDebug>

// UI命名空间声明（由Qt Designer生成）
namespace Ui {
class MainWindow;
}

// 主窗口类定义
class MainWindow : public QMainWindow
{
    Q_OBJECT // Qt元对象系统宏，支持信号槽机制

public:
    explicit MainWindow(QWidget *parent = nullptr); // 构造函数
    ~MainWindow(); // 析构函数

private slots: // 私有槽函数（响应UI事件）
    // 按钮点击槽函数
    void on_btnStart_clicked();
    void on_btnStop_clicked();

    // UI控件值改变槽函数
    void on_cbInputDevices_currentIndexChanged(int index);
    void on_cbSampleRates_currentIndexChanged(int index);
    void on_cbBitrates_currentIndexChanged(int index);
    void on_cbChannels_currentIndexChanged(int index);
    void on_cbFormat_currentIndexChanged(int index);

private:
    // 私有辅助函数
    void updateTimer(); // 更新定时器
    void updateRecordingTime(); // 更新录音时间显示
    void loadAudioDevices(); // 加载音频设备列表
    void initAudioFormat(); // 初始化音频格式
    void startRecording(); // 开始录音
    void stopRecording(); // 停止录音

    // 录音格式枚举
    enum RecordFormat {
        Format_PCM, // PCM/WAV格式
        Format_AAC  // AAC压缩格式
    };

    // 成员变量
    Ui::MainWindow *ui; // UI界面指针
    AACRecorder *m_recorder; // AAC编码器对象
    QAudioInput *m_audioInput = nullptr; // 音频输入对象
    QIODevice *m_audioDevice = nullptr; // 音频设备对象（用于读取数据）
    QAudioFormat m_audioFormat; // 音频格式配置
    QTimer *m_timer; // 定时器（用于更新时间显示）
    qint64 m_recordTime = 0; // 录音时间（秒）
    bool m_isRecording = false; // 录音状态标志
    RecordFormat m_currentFormat = Format_PCM; // 当前录音格式（默认PCM）
    QList<QAudioDeviceInfo> m_inputDevices; // 音频输入设备列表
    QFile *m_pcmFile = nullptr; // PCM文件对象（WAV格式时使用）
};

#endif // MAINWINDOW_H

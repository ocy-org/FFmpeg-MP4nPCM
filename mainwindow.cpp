#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QDebug>
#include <QDataStream>
#include <QStandardPaths>

// 辅助函数：写入标准 WAV 头（仅支持 16-bit PCM）
static void writeWavHeader(QFile &file, const QAudioFormat &format, qint64 dataSize)
{
    // 断言检查格式要求：必须是小端序、有符号整型、16位采样
    Q_ASSERT(format.byteOrder() == QAudioFormat::LittleEndian);
    Q_ASSERT(format.sampleType() == QAudioFormat::SignedInt);
    Q_ASSERT(format.sampleSize() == 16);

    QByteArray header;
    QDataStream ds(&header, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian); // WAV文件使用小端序

    // RIFF header - WAV文件的标准头结构
    ds.writeRawData("RIFF", 4); // 文件标识
    ds << quint32(36 + dataSize); // 整个文件大小 = 36字节头 + 音频数据大小
    ds.writeRawData("WAVE", 4); // 文件类型标识

    // fmt subchunk - 格式描述块
    ds.writeRawData("fmt ", 4); // 格式块标识
    ds << quint32(16); // 格式块大小（固定16字节）
    ds << quint16(1); // 音频格式（1表示PCM）
    ds << quint16(format.channelCount()); // 声道数
    ds << quint32(format.sampleRate()); // 采样率
    ds << quint32(format.sampleRate() * format.channelCount() * format.sampleSize() / 8); // 字节率
    ds << quint16(format.channelCount() * format.sampleSize() / 8); // 块对齐
    ds << quint16(format.sampleSize()); // 位深度

    // data subchunk - 数据块
    ds.writeRawData("data", 4); // 数据块标识
    ds << quint32(dataSize); // 音频数据大小

    file.write(header); // 将头信息写入文件
}

// 构造函数：初始化主窗口
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_recorder(new AACRecorder(this)) // 创建AAC录音器对象
{
    ui->setupUi(this); // 设置UI界面

    setWindowTitle("AAC/PCM 录音机"); // 设置窗口标题
    setMinimumSize(500, 350); // 设置最小窗口尺寸

    // 初始化UI状态
    ui->btnStop->setEnabled(false); // 停止按钮初始不可用

    // 设置样式表美化界面
    ui->lblStatus->setStyleSheet("font-size: 14px; color: #666;");
    ui->lblTime->setStyleSheet("font-size: 24px; font-weight: bold;");

    // 开始按钮样式（绿色）
    ui->btnStart->setStyleSheet("QPushButton {"
                               "padding: 10px 20px;"
                               "font-size: 16px;"
                               "background-color: #4CAF50;"
                               "color: white;"
                               "border: none;"
                               "border-radius: 5px;"
                               "}");

    // 停止按钮样式（红色）
    ui->btnStop->setStyleSheet("QPushButton {"
                              "padding: 10px 20px;"
                              "font-size: 16px;"
                              "background-color: #f44336;"
                              "color: white;"
                              "border: none;"
                              "border-radius: 5px;"
                              "}");

    // 初始化音频设备和格式
    loadAudioDevices(); // 加载可用的音频输入设备
    initAudioFormat(); // 初始化音频格式参数

    // 初始化定时器用于更新录音时间显示
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::updateTimer);
}

// 析构函数：清理资源
MainWindow::~MainWindow()
{
    delete ui; // 删除UI对象
    if (m_isRecording) {
        stopRecording(); // 如果正在录音，先停止
    }
}

// 加载音频输入设备列表
void MainWindow::loadAudioDevices()
{
    // 获取所有可用的音频输入设备
    m_inputDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

    ui->cbInputDevices->clear(); // 清空设备下拉框
    // 遍历所有输入设备并添加到下拉框
    for (const QAudioDeviceInfo &device : m_inputDevices) {
        ui->cbInputDevices->addItem(device.deviceName());
    }

    // 如果没有找到音频设备，显示错误信息
    if (m_inputDevices.isEmpty()) {
        ui->lblStatus->setText("未找到音频输入设备");
        ui->btnStart->setEnabled(false); // 禁用开始按钮
    } else {
        ui->lblStatus->setText("请选择录音设备");
    }
}

// 初始化默认音频格式参数
void MainWindow::initAudioFormat()
{
    m_audioFormat.setSampleRate(44100); // CD音质采样率：44.1kHz
    m_audioFormat.setChannelCount(2); // 立体声：2声道
    m_audioFormat.setSampleSize(16); // 16位采样深度
    m_audioFormat.setSampleType(QAudioFormat::SignedInt); // 有符号整型
    m_audioFormat.setByteOrder(QAudioFormat::LittleEndian); // 小端序
    m_audioFormat.setCodec("audio/pcm"); // PCM编码
}

// 开始按钮点击槽函数
void MainWindow::on_btnStart_clicked()
{
    startRecording();
}

// 停止按钮点击槽函数
void MainWindow::on_btnStop_clicked()
{
    stopRecording();
}

// 开始录音的核心函数
void MainWindow::startRecording()
{
    // 获取桌面路径作为默认保存目录
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (defaultDir.isEmpty()) {
        defaultDir = QDir::homePath(); // 如果桌面路径无效，使用用户主目录
    }

    QString fileName;
    // 根据选择的格式设置文件扩展名和过滤器
    if (m_currentFormat == Format_AAC) {
        fileName = QFileDialog::getSaveFileName(this, "保存 AAC 录音文件",
            defaultDir + "/录音_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".m4a",
            "AAC 音频 (*.m4a *.mp4)");
    } else {
        fileName = QFileDialog::getSaveFileName(this, "保存 PCM 录音文件",
            defaultDir + "/录音_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".wav",
            "WAV 音频 (*.wav)");
    }

    if (fileName.isEmpty()) {
        return; // 用户取消选择文件，直接返回
    }

    // 检查设备选择是否有效
    int currentIndex = ui->cbInputDevices->currentIndex();
    if (currentIndex < 0 || currentIndex >= m_inputDevices.size()) {
        QMessageBox::critical(this, "错误", "请选择有效的音频设备");
        return;
    }

    QAudioDeviceInfo device = m_inputDevices.at(currentIndex);

    // 检查设备是否支持请求的音频格式
    if (!device.isFormatSupported(m_audioFormat)) {
        QMessageBox::warning(this, "警告",
            "请求的音频格式不被支持，将使用最接近的格式");
        m_audioFormat = device.nearestFormat(m_audioFormat); // 使用最接近的格式
    }

    // 调试输出实际使用的音频格式
    qDebug() << "使用的音频格式:";
    qDebug() << "  采样率:" << m_audioFormat.sampleRate();
    qDebug() << "  声道数:" << m_audioFormat.channelCount();
    qDebug() << "  样本大小:" << m_audioFormat.sampleSize();
    qDebug() << "  样本类型:" << m_audioFormat.sampleType();
    qDebug() << "  字节序:" << m_audioFormat.byteOrder();
    qDebug() << "  编解码器:" << m_audioFormat.codec();

    // 初始化后端录音器
    if (m_currentFormat == Format_AAC) {
        // 初始化AAC编码器
        if (!m_recorder->initialize(fileName,
                                    m_audioFormat.sampleRate(),
                                    m_audioFormat.channelCount(),
                                    ui->cbBitrates->currentText().toInt())) {
            QMessageBox::critical(this, "错误", "初始化 AAC 编码器失败");
            return;
        }
    } else {
        // 创建PCM/WAV文件
        m_pcmFile = new QFile(fileName);
        if (!m_pcmFile->open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "错误", "无法创建 WAV 文件");
            delete m_pcmFile;
            m_pcmFile = nullptr;
            return;
        }
        // 写入44字节的占位头（后续会替换为真实的WAV头）
        m_pcmFile->write(QByteArray(44, 0));
    }

    // 创建音频输入对象并开始录音
    m_audioInput = new QAudioInput(device, m_audioFormat, this);
    m_audioDevice = m_audioInput->start(); // 开始录音，返回音频设备对象

    if (!m_audioDevice) {
        QMessageBox::critical(this, "错误", "无法开始录音");
        // 清理资源
        if (m_currentFormat == Format_AAC) {
            m_recorder->close();
        } else {
            m_pcmFile->close();
            delete m_pcmFile;
            m_pcmFile = nullptr;
        }
        delete m_audioInput;
        m_audioInput = nullptr;
        return;
    }

    // 连接音频数据就绪信号，当有音频数据可读时触发
    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        QByteArray data = m_audioDevice->readAll(); // 读取所有可用的音频数据
        if (!data.isEmpty()) {
            if (m_currentFormat == Format_AAC) {
                // 将PCM数据编码为AAC
                m_recorder->writeAudioData((const uint8_t*)data.constData(), data.size());
            } else {
                // 直接写入PCM数据到文件
                m_pcmFile->write(data);
            }
        }
    });

    // 更新录音状态
    m_isRecording = true;
    m_recordTime = 0;
    ui->btnStart->setEnabled(false);
    ui->btnStop->setEnabled(true);
    ui->lblStatus->setText(m_currentFormat == Format_AAC ? "正在录制 AAC..." : "正在录制 PCM...");
    ui->lblStatus->setStyleSheet("font-size: 14px; color: #f44336; font-weight: bold;");
    m_timer->start(1000); // 启动定时器，每秒更新一次时间显示
}

// 停止录音的核心函数
void MainWindow::stopRecording()
{
    // 停止音频输入
    if (m_audioInput) {
        m_audioInput->stop();
        delete m_audioInput;
        m_audioInput = nullptr;
    }

    // 根据格式处理文件
    if (m_currentFormat == Format_AAC) {
        m_recorder->close(); // 关闭AAC编码器
    } else if (m_pcmFile && m_pcmFile->isOpen()) {
        // 对于WAV文件，需要写入正确的文件头
        qint64 dataSize = m_pcmFile->size() - 44; // 计算实际的音频数据大小
        if (dataSize > 0) {
            m_pcmFile->seek(0); // 回到文件开头
            writeWavHeader(*m_pcmFile, m_audioFormat, dataSize); // 写入正确的WAV头
        }
        m_pcmFile->close();
        delete m_pcmFile;
        m_pcmFile = nullptr;
    }

    // 更新UI状态
    m_isRecording = false;
    ui->btnStart->setEnabled(true);
    ui->btnStop->setEnabled(false);
    ui->lblStatus->setText("录音完成");
    ui->lblStatus->setStyleSheet("font-size: 14px; color: #4CAF50; font-weight: bold;");
    m_timer->stop(); // 停止定时器

    QMessageBox::information(this, "提示", "录音文件已保存");
}

// 定时器超时槽函数：更新录音时间
void MainWindow::updateTimer()
{
    m_recordTime++; // 增加录音时间（秒）
    updateRecordingTime(); // 更新显示
}

// 更新录音时间显示（格式化为HH:MM:SS）
void MainWindow::updateRecordingTime()
{
    int hours = m_recordTime / 3600;
    int minutes = (m_recordTime % 3600) / 60;
    int seconds = m_recordTime % 60;

    QString timeStr = QString("%1:%2:%3")
                     .arg(hours, 2, 10, QLatin1Char('0')) // 小时，2位，补0
                     .arg(minutes, 2, 10, QLatin1Char('0')) // 分钟，2位，补0
                     .arg(seconds, 2, 10, QLatin1Char('0')); // 秒，2位，补0
    ui->lblTime->setText(timeStr);
}

// 设备选择改变槽函数
void MainWindow::on_cbInputDevices_currentIndexChanged(int index)
{
    if (m_isRecording) {
        stopRecording(); // 如果正在录音，停止录音
    }
}

// 采样率改变槽函数
void MainWindow::on_cbSampleRates_currentIndexChanged(int index)
{
    m_audioFormat.setSampleRate(ui->cbSampleRates->currentText().toInt());
    if (m_isRecording) {
        stopRecording(); // 如果正在录音，停止录音
    }
}

// 比特率改变槽函数（仅AAC格式有效）
void MainWindow::on_cbBitrates_currentIndexChanged(int index)
{
    if (m_isRecording) {
        stopRecording(); // 如果正在录音，停止录音
    }
}

// 声道数改变槽函数
void MainWindow::on_cbChannels_currentIndexChanged(int index)
{
    m_audioFormat.setChannelCount(ui->cbChannels->currentText().toInt());
    if (m_isRecording) {
        stopRecording(); // 如果正在录音，停止录音
    }
}

// 录音格式改变槽函数（PCM/AAC）
void MainWindow::on_cbFormat_currentIndexChanged(int index)
{
    m_currentFormat = (index == 0) ? Format_PCM : Format_AAC;
}

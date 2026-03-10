#pragma once

#include "ApplicationServer.h"
#include "util/EnhancedConfigReader.h"

#ifdef ENABLE_VISION_SERVER
#include <opencv2/opencv.hpp>
#include <mqtt/async_client.h>
#endif

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>

/**
 * @brief 视频流来源类型
 */
enum class VideoSourceType {
    CAMERA = 0,      // 摄像头（笔记本/USB相机）
    RTSP = 1,        // RTSP网络流
    VIDEO_FILE = 2,  // 视频文件
    IMAGE_DIR = 3    // 图片序列目录
};

/**
 * @brief 检测算法类型
 */
enum class DetectionAlgorithm {
    MOTION_DETECTION = 0,    // 运动检测（帧间差分）
    CONTOUR_DETECTION = 1,   // 轮廓检测
    TEMPLATE_MATCH = 2,      // 模板匹配
    COLOR_DETECTION = 3      // 颜色检测
};

/**
 * @brief 检测事件
 */
struct DetectionEvent {
    int64_t timestamp;           // 时间戳
    std::string event_type;      // 事件类型：motion, defect, alarm
    std::string description;     // 事件描述
    double confidence;           // 置信度 0-1
    int x, y, width, height;    // 检测区域坐标
    std::string image_path;      // 截图保存路径（如果启用）
};

/**
 * @brief 视频处理统计信息
 */
struct VisionStats {
    int64_t frames_processed;    // 已处理帧数
    int64_t events_detected;     // 检测到的事件数
    double current_fps;          // 当前帧率
    double avg_process_time_ms;  // 平均处理时间（毫秒）
    int64_t last_event_time;     // 最后一次事件时间
    bool is_recording;           // 是否正在录像
};

/**
 * @brief 工业视觉检测服务器
 * 
 * 功能：
 * 1. 视频流采集（摄像头/RTSP/文件）
 * 2. 实时图像处理（运动检测、缺陷识别）
 * 3. HTTP API（状态查询、配置、截图）
 * 4. MQTT事件推送
 * 5. 录像和截图保存
 */
class VisionServer : public ApplicationServer {
public:
    VisionServer(const std::string& ip, int port, 
                 IOMultiplexer::IOType io_type,
                 EnhancedConfigReader* config);
    
    virtual ~VisionServer();
    
    bool start() override;
    void stop() override;

protected:
    // 实现 ApplicationServer 的纯虚函数
    void initializeProtocolRouter() override;
    std::string handleHttpRequest(const std::string& request, int clientFd) override;
    std::string handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) override;
    bool parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) override;
    
    // 覆盖数据接收处理，直接处理HTTP请求
    void onDataReceived(int clientFd, const char* data, size_t len) override;
    
    void onClientConnected(int clientFd) override;
    void onClientDisconnected(int clientFd) override;

private:
    // 初始化
    bool initMQTT();
    bool initVideoSource();
    bool initDetectionAlgorithm();
    
    // 视频处理线程
    void videoProcessingThread();
    
    // 图像处理函数
#ifdef ENABLE_VISION_SERVER
    cv::Mat preprocessFrame(const cv::Mat& frame);
    std::vector<DetectionEvent> detectMotion(const cv::Mat& current, const cv::Mat& previous);
    std::vector<DetectionEvent> detectContours(const cv::Mat& frame);
    std::vector<DetectionEvent> detectByTemplate(const cv::Mat& frame);
    std::vector<DetectionEvent> detectByColor(const cv::Mat& frame);
#endif
    
    // 事件处理
    void handleDetectionEvent(const DetectionEvent& event);
    void publishEventToMQTT(const DetectionEvent& event);
    void saveEventSnapshot(const DetectionEvent& event);
    
    // HTTP API处理
    std::string handleGetStatus();
    std::string handleGetConfig();
    std::string handleSetConfig(const std::vector<std::string>& args);
    std::string handleGetSnapshot();
    std::string handleStartRecording();
    std::string handleStopRecording();
    std::string handleGetEvents(int limit);
    
    // 录像相关
    void startRecording();
    void stopRecording();
    
    // 工具函数
    std::string getTimestamp();
    std::string saveFrameToFile(const std::string& frame_data, const std::string& prefix);

private:
    EnhancedConfigReader* config_;
    
    // MQTT配置
    std::string mqtt_broker_;
    std::string mqtt_client_id_;
    std::string mqtt_topic_prefix_;
    int mqtt_qos_;
#ifdef ENABLE_VISION_SERVER
    std::unique_ptr<mqtt::async_client> mqtt_client_;
#else
    void* mqtt_client_ = nullptr;
#endif
    
    // 视频源配置
    VideoSourceType source_type_;
    std::string source_path_;      // 摄像头ID/RTSP地址/文件路径
    int frame_width_;
    int frame_height_;
    int target_fps_;
    
    // 检测配置
    DetectionAlgorithm algorithm_;
    double detection_threshold_;    // 检测阈值
    int min_contour_area_;         // 最小轮廓面积
    bool enable_recording_;        // 是否启用自动录像
    int recording_duration_;       // 自动录像持续时间（秒）
    bool enable_snapshot_;         // 是否启用截图
    std::string output_dir_;       // 输出根目录
    std::string snapshot_dir_;     // 截图保存目录
    std::string recording_dir_;    // 录像保存目录
    
    // 运行状态
    std::atomic<bool> running_;
    std::thread processing_thread_;
    
    // OpenCV对象
#ifdef ENABLE_VISION_SERVER
    cv::VideoCapture video_capture_;
    cv::VideoWriter video_writer_;
    cv::Mat previous_frame_;
    std::mutex frame_mutex_;
    cv::Mat latest_frame_;         // 最新帧（用于HTTP截图）
#endif
    
    // 统计信息
    VisionStats stats_;
    std::mutex stats_mutex_;
    
    // 事件队列
    std::queue<DetectionEvent> event_queue_;
    std::mutex event_mutex_;
    std::vector<DetectionEvent> recent_events_;  // 最近的事件（用于API查询）
    static constexpr size_t MAX_RECENT_EVENTS = 100;
    
    // 录像状态
    std::atomic<bool> is_recording_;
    std::string current_video_file_;
    std::chrono::steady_clock::time_point recording_start_time_;  // 录像开始时间
    std::mutex recording_mutex_;    // 录像状态互斥锁
    
    // 事件节流（防止频繁触发）
    std::chrono::steady_clock::time_point last_event_time_;
    int event_cooldown_ms_;  // 事件冷却时间（毫秒）
};




# VisionServer - 工业视觉质检系统

基于OpenCV的实时视频监控和缺陷检测系统，适用于生产线质量检测场景。

## 🚀 快速开始

```bash
# 一键启动（Windows）
.\quick_start_vision.bat

# 一键启动（Linux/Mac）
./quick_start_vision.sh

# 测试API
python test_vision_api.py

# 监控MQTT事件
python test_vision_mqtt.py
```

## 📦 功能特性

- ✅ 多种视频源支持（摄像头/RTSP/视频文件）
- ✅ 实时图像处理（运动检测/轮廓检测）
- ✅ HTTP RESTful API
- ✅ MQTT事件推送
- ✅ 自动截图和录像
- ✅ Docker一键部署

## 📖 文档

- [快速开始指南](../QUICK_START_VISION.md)
- [技术文档](../VISION_SERVER_GUIDE.md)
- [项目总结](../VISION_PROJECT_SUMMARY.md)

## 🏗️ 文件结构

```
plugins/vision/
├── VisionServer.h              # 头文件
├── VisionServer.cpp            # 实现文件
├── VisionServerPlugin.cpp      # 插件注册
└── README.md                   # 本文件
```

## 🔧 API端点

- `GET /status` - 查询服务状态
- `GET /config` - 查询配置信息
- `GET /snapshot` - 获取实时截图
- `GET /events/{limit}` - 查询检测事件
- `GET /recording/start` - 开始录像
- `GET /recording/stop` - 停止录像

## 📊 性能指标

- 处理帧率: 25-30 FPS (640x480)
- 平均延迟: <15 ms/帧
- 内存占用: <200 MB
- CPU占用: 20-30% (4核)

## 🎯 应用场景

- 产线运动检测
- 零件外形检测
- 质量缺陷识别
- 异常行为监控

## 💡 技术栈

- C++17
- OpenCV 4.x
- Paho MQTT C++
- JsonCpp
- Docker

---

**作者**: NetBox Team  
**GitHub**: https://github.com/sgly123/netbox




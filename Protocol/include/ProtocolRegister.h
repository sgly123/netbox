#pragma once
#include "ProtocolFactory.h"

/**
 * @brief 协议自动注册宏
 * 
 * 使用方法：
 * REGISTER_PROTOCOL(SimpleHeaderProtocol);
 * 
 * 这个宏会在程序启动时自动注册协议类型到ProtocolFactory
 */
#define REGISTER_PROTOCOL(PROTOCOL_CLASS) \
    namespace { \
        struct PROTOCOL_CLASS##Register { \
            PROTOCOL_CLASS##Register() { \
                ProtocolFactory::registerProtocol(PROTOCOL_CLASS::ID, [](){ \
                    return std::make_unique<PROTOCOL_CLASS>(); \
                }); \
            } \
        }; \
        static PROTOCOL_CLASS##Register global_##PROTOCOL_CLASS##_register; \
    }
/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2016-2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/
/**
***********************************************************************************************************************
* @file devDriverServer.cpp
* @brief Class definition for DevDriverServer.
***********************************************************************************************************************
*/

#include "ddPlatform.h"
#include "messageChannel.h"
#include "devDriverServer.h"
#include "protocolServer.h"
#include "protocols/loggingServer.h"
#include "protocols/settingsServer.h"
#include "protocols/driverControlServer.h"
#include "protocols/rgpServer.h"
#include "protocols/typemap.h"

// The local transport implementation is only available on Windows.
#include "socketMsgTransport.h"

namespace DevDriver
{
    DevDriverServer::DevDriverServer(const AllocCb&          allocCb,
                                     const ServerCreateInfo& createInfo)
        : m_pMsgChannel(nullptr)
        , m_allocCb(allocCb)
        , m_createInfo(createInfo)
    {
    }

    DevDriverServer::~DevDriverServer()
    {
        Destroy();
    }

    Result DevDriverServer::Initialize()
    {
        Result result = Result::Error;

        if (m_createInfo.connectionInfo.type == TransportType::Local)
        {
            using MsgChannelSocket = MessageChannel<SocketMsgTransport>;
            m_pMsgChannel = DD_NEW(MsgChannelSocket, m_allocCb)(m_allocCb,
                                                                m_createInfo,
                                                                m_createInfo.connectionInfo);
        }
        else
        {
            // Invalid transport type
            DD_ALERT_REASON("Invalid transport type specified");
        }

        if (m_pMsgChannel != nullptr)
        {
            result = m_pMsgChannel->Register(kInfiniteTimeout);

            if (result == Result::Success)
            {
                result = InitializeProtocols();

                if (result != Result::Success)
                {
                    // Unregister the message channel since we failed to initialize the protocols.
                    m_pMsgChannel->Unregister();
                }
            }

            if (result != Result::Success)
            {
                // We failed to initialize so we need to destroy the message channel.
                DD_DELETE(m_pMsgChannel, m_allocCb);
                m_pMsgChannel = nullptr;
            }
        }
        return result;
    }

    void DevDriverServer::Finalize()
    {
        // The driver control protocol must always be finalized first!
        // It contains the code for supporting the HaltOnStart feature that allows tools to configure
        // options before protocol servers are finalized.
        if (m_createInfo.servers.driverControl)
        {
            FinalizeProtocol(Protocol::DriverControl);
        }

        if (m_createInfo.servers.logging)
        {
            FinalizeProtocol(Protocol::Logging);
        }

        if (m_createInfo.servers.settings)
        {
            FinalizeProtocol(Protocol::Settings);
        }

        if (m_createInfo.servers.rgp)
        {
            FinalizeProtocol(Protocol::RGP);
        }
    }

    void DevDriverServer::Destroy()
    {
        if (m_pMsgChannel != nullptr)
        {
            Result result = m_pMsgChannel->Unregister();
            DD_ASSERT(result == Result::Success);
            DD_UNUSED(result);

            DestroyProtocols();

            DD_DELETE(m_pMsgChannel, m_allocCb);
            m_pMsgChannel = nullptr;
        }
    }

    bool DevDriverServer::IsConnected() const
    {
        if (m_pMsgChannel)
            return m_pMsgChannel->IsConnected();
        return false;
    }

    IMsgChannel* DevDriverServer::GetMessageChannel() const
    {
        return m_pMsgChannel;
    }

    LoggingProtocol::LoggingServer* DevDriverServer::GetLoggingServer()
    {
        return GetServer<Protocol::Logging>();
    }

    SettingsProtocol::SettingsServer* DevDriverServer::GetSettingsServer()
    {
        return GetServer<Protocol::Settings>();
    }

    DriverControlProtocol::DriverControlServer* DevDriverServer::GetDriverControlServer()
    {
        return GetServer<Protocol::DriverControl>();
    }

    RGPProtocol::RGPServer* DevDriverServer::GetRGPServer()
    {
        return GetServer<Protocol::RGP>();
    }

    Result DevDriverServer::InitializeProtocols()
    {
        Result result = Result::Success;

        if (m_createInfo.servers.logging)
        {
            result = RegisterProtocol<Protocol::Logging>();
        }
        if (m_createInfo.servers.settings)
        {
            result = RegisterProtocol<Protocol::Settings>();
        }
        if (m_createInfo.servers.driverControl)
        {
            result = RegisterProtocol<Protocol::DriverControl>();
        }
        if (m_createInfo.servers.rgp)
        {
            result = RegisterProtocol<Protocol::RGP>();
        }
        return result;
    }

    void DevDriverServer::DestroyProtocols()
    {
        if (m_createInfo.servers.logging)
        {
            UnregisterProtocol(Protocol::Logging);
        }

        if (m_createInfo.servers.settings)
        {
            UnregisterProtocol(Protocol::Settings);
        }

        if (m_createInfo.servers.driverControl)
        {
            UnregisterProtocol(Protocol::DriverControl);
        }

        if (m_createInfo.servers.rgp)
        {
            UnregisterProtocol(Protocol::RGP);
        }
    }

    Result DevDriverServer::RegisterProtocol(Protocol protocol)
    {
        Result result = Result::Error;
        switch (protocol)
        {
        case Protocol::Logging:
        {
            result = RegisterProtocol<Protocol::Logging>();
            break;
        }
        case Protocol::Settings:
        {
            result = RegisterProtocol<Protocol::Settings>();
            break;
        }
        case Protocol::DriverControl:
        {
            result = RegisterProtocol<Protocol::DriverControl>();
            break;
        }
        case Protocol::RGP:
        {
            result = RegisterProtocol<Protocol::RGP>();
            break;
        }
        default:
        {
            DD_ALERT_REASON("Invalid protocol specified");
            break;
        }
        }
        return result;
    }

    template <Protocol protocol, class ...Args>
    Result DevDriverServer::RegisterProtocol(Args... args)
    {
        Result result = Result::Error;
        using T = ProtocolServerType<protocol>;
        T* pProtocolServer = nullptr;
        if (m_pMsgChannel->GetProtocolServer(protocol) == nullptr)
        {
            pProtocolServer = DD_NEW(T, m_allocCb)(m_pMsgChannel, args...);
            result = m_pMsgChannel->RegisterProtocolServer(pProtocolServer);
        }
        return result;
    };

    template <Protocol protocol>
    ProtocolServerType<protocol>* DevDriverServer::GetServer()
    {
        return static_cast<ProtocolServerType<protocol>*>(m_pMsgChannel->GetProtocolServer(protocol));
    }

#if !DD_VERSION_SUPPORTS(GPUOPEN_DISTRIBUTED_STATUS_FLAGS_VERSION)
    Result QueryDevDriverStatus(const TransportType type, StatusFlags* pFlags, HostInfo *pHostInfo)
    {
        Result result = Result::Unavailable;

        // pHostInfo is optional, so we default to local host
        const HostInfo& hostInfo = (pHostInfo != nullptr) ? *pHostInfo : kDefaultLocalHost;

        switch (type)
        {
            case TransportType::Local:
                result = SocketMsgTransport::QueryStatus(hostInfo, kQueryStatusTimeoutInMs, pFlags);
                break;
            default:
                DD_ALERT_REASON("Invalid transport type specified");
                break;
        }
        return result;
    }
#endif

    bool DevDriverServer::IsConnectionAvailable(const HostInfo& hostInfo,
                                                uint32          timeout)
    {
        // At this time, we only support machine local connections for the driver
        Result result = Result::Unavailable;
        switch (hostInfo.type)
        {
            case TransportType::Local:
                // On non windows platforms we try to use an AF_UNIX socket for communication
                result = SocketMsgTransport::TestConnection(hostInfo, timeout);
                break;
            default:
                // Invalid value passed to the function
                DD_ALERT_REASON("Invalid transport type specified");
                break;
        }
        return (result == Result::Success);
    }

    void DevDriverServer::UnregisterProtocol(Protocol protocol)
    {
        IProtocolServer *pProtocolServer = m_pMsgChannel->GetProtocolServer(protocol);
        if (pProtocolServer != nullptr)
        {
            const Result result = m_pMsgChannel->UnregisterProtocolServer(pProtocolServer);
            DD_ASSERT(result == Result::Success);
            DD_UNUSED(result);
            DD_DELETE(pProtocolServer, m_allocCb);
        }
    }

    void DevDriverServer::FinalizeProtocol(Protocol protocol)
    {
        IProtocolServer *pProtocolServer = m_pMsgChannel->GetProtocolServer(protocol);
        DD_ASSERT(pProtocolServer != nullptr);

        pProtocolServer->Finalize();
    }

    bool DevDriverServer::ShouldShowOverlay()
    {
        // Note: This function should probably be marked const, but it calls IsTraceRunning which takes the RGPServer
        // mutex to check trace state which is not a const operation.  A read/write lock might solve the problem.
        RGPProtocol::RGPServer* pRgpServer = GetServer<Protocol::RGP>();
        static const char* const pRenderDocAppName = "qrenderdoc.exe";
        char clientName[128] = {};
        Platform::GetProcessName(&clientName[0], sizeof(clientName));
        bool traceInProgress = ((pRgpServer != nullptr) && pRgpServer->IsTraceRunning());
        // We always show the overlay except in two cases:
        // 1) When an RGP trace is actively running.
        // 2) [Temporary] When the active process is RenderDoc. This exception is temporary until a more robust
        //      solution for disabling the overlay is implemented.
        return ((traceInProgress == false) && (strcmp(clientName, pRenderDocAppName) != 0));
    }

#if !DD_VERSION_SUPPORTS(GPUOPEN_CREATE_INFO_CLEANUP_VERSION)
    DevDriverServer::DevDriverServer(const DevDriverServerCreateInfo& createInfo)
        : m_pMsgChannel(nullptr)
        , m_allocCb(createInfo.transportCreateInfo.allocCb)
        , m_createInfo()
    {
        m_createInfo.initialFlags = createInfo.transportCreateInfo.initialFlags;
        m_createInfo.componentType = createInfo.transportCreateInfo.componentType;
        m_createInfo.createUpdateThread = createInfo.transportCreateInfo.createUpdateThread;
        Platform::Strncpy(&m_createInfo.clientDescription[0],
                          &createInfo.transportCreateInfo.clientDescription[0],
                          sizeof(m_createInfo.clientDescription));
        switch (createInfo.transportCreateInfo.type)
        {
            case TransportType::Local:
            {
                m_createInfo.connectionInfo = kDefaultNamedPipe;
                break;
            }
            default:
                DD_ALERT_REASON("Invalid transport type specified");
                break;
        }
        m_createInfo.servers = createInfo.enabledProtocols;
    }

    bool DevDriverServer::IsConnectionAvailable(const TransportType type,
                                                uint32              timeout)
    {
        bool result = false;
        switch (type)
        {
        case TransportType::Local:
            // On non windows platforms we try to use an AF_UNIX socket for communication
            result = IsConnectionAvailable(kDefaultNamedPipe, timeout);
            break;
        default:
            // Invalid value passed to the function
            DD_ALERT_REASON("Invalid transport type specified");
            break;
        }
        return result;
    }
#endif
} // DevDriver

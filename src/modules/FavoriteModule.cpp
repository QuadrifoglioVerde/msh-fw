#include "FavoriteModule.h"

#include "mesh/NodeDB.h"
#include "mesh/MeshService.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

FavoriteModule *favoriteModule;

FavoriteModule::FavoriteModule() : SinglePortModule("favorite", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
}

namespace
{
    constexpr size_t FAVORITE_REPLY_CHUNK_SIZE = 200;
}

bool FavoriteModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return p != nullptr && p->decoded.portnum == ourPortNum;
}

ProcessMessage FavoriteModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    LOG_DEBUG("FavoriteModule: received packet from=0x%08x to=0x%08x self=0x%08x len=%u role=%d", mp.from, mp.to,
              nodeDB->getNodeNum(), mp.decoded.payload.size, config.device.role);

    if (mp.to != nodeDB->getNodeNum())
    {
        LOG_DEBUG("FavoriteModule: ignoring packet because it is not addressed to this node");
        return ProcessMessage::CONTINUE;
    }

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)
    {
        LOG_DEBUG("FavoriteModule: ignoring packet because role=%d is not router/router_late", config.device.role);
        return ProcessMessage::CONTINUE;
    }

    if (mp.decoded.payload.size == 0)
    {
        LOG_DEBUG("FavoriteModule: ignoring empty payload");
        return ProcessMessage::CONTINUE;
    }

    char buf[256];
    const size_t n = std::min<size_t>(mp.decoded.payload.size, sizeof(buf) - 1);
    memcpy(buf, mp.decoded.payload.bytes, n);
    buf[n] = '\0';

    if (strncasecmp(buf, "FAV", 3) != 0)
    {
        LOG_DEBUG("FavoriteModule: ignoring payload that does not start with FAV: '%.*s'", n, buf);
        return ProcessMessage::CONTINUE;
    }

    LOG_DEBUG("FavoriteModule: building favorite list response for source 0x%08x", mp.from);

    const std::string body = buildFavoriteList();
    const size_t totalChunks = (body.size() + FAVORITE_REPLY_CHUNK_SIZE - 1) / FAVORITE_REPLY_CHUNK_SIZE;

    if (totalChunks <= 1)
    {
        auto *reply = allocDataPacket();
        if (!reply)
        {
            LOG_WARN("FavoriteModule: failed to allocate reply packet");
            return ProcessMessage::STOP;
        }

        reply->to = mp.from;
        reply->from = nodeDB->getNodeNum();
        reply->channel = mp.channel;
        reply->want_ack = true;

        const size_t bodyLen = std::min<size_t>(body.size(), sizeof(reply->decoded.payload.bytes));
        reply->decoded.payload.size = bodyLen;
        memcpy(reply->decoded.payload.bytes, body.c_str(), bodyLen);

        LOG_DEBUG("FavoriteModule: sending payload='%s' (%zu bytes)", body.c_str(), bodyLen);
        service->sendToMesh(reply);
        return ProcessMessage::STOP;
    }

    LOG_DEBUG("FavoriteModule: splitting favorite list into %zu chunks", totalChunks);

    for (size_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex)
    {
        auto *reply = allocDataPacket();
        if (!reply)
        {
            LOG_WARN("FavoriteModule: failed to allocate reply packet while chunking favorite list");
            break;
        }

        reply->to = mp.from;
        reply->from = nodeDB->getNodeNum();
        reply->channel = mp.channel;
        reply->want_ack = true;

        const size_t offset = chunkIndex * FAVORITE_REPLY_CHUNK_SIZE;
        const size_t chunkLen = std::min<size_t>(FAVORITE_REPLY_CHUNK_SIZE, body.size() - offset);

        reply->decoded.payload.size = chunkLen;
        memcpy(reply->decoded.payload.bytes, body.c_str() + offset, chunkLen);

        LOG_DEBUG("FavoriteModule: sending chunk %zu/%zu (%zu bytes)", chunkIndex + 1, totalChunks, chunkLen);
        service->sendToMesh(reply);
    }

    return ProcessMessage::STOP;
}

std::string FavoriteModule::buildFavoriteList() const
{
    std::string out = "FAVs: ";
    bool first = true;
    size_t favoriteCount = 0;

    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); ++i)
    {
        const auto *node = nodeDB->getMeshNodeByIndex(i);
        if (node == nullptr || !nodeInfoLiteIsFavorite(node))
        {
            continue;
        }
        if (!first)
        {
            out += ", ";
        }
        char nodeId[10];
        snprintf(nodeId, sizeof(nodeId), "!%08x", static_cast<unsigned int>(node->num));
        out += nodeId;
        first = false;
        ++favoriteCount;
    }

    if (first)
    {
        LOG_DEBUG("FavoriteModule: no favorite nodes in mesh DB");
        return "No favorite nodes";
    }

    LOG_DEBUG("FavoriteModule: favorite list contains %zu node(s)", favoriteCount);
    return out;
}
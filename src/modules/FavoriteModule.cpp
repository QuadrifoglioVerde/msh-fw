#include "FavoriteModule.h"

#include "mesh/NodeDB.h"
#include "mesh/MeshService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

FavoriteModule *favoriteModule;

FavoriteModule::FavoriteModule() : SinglePortModule("favorite", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
}

namespace
{
    constexpr size_t FAVORITE_REPLY_CHUNK_SIZE = 200;

    bool senderIsAuthorizedAdmin(const meshtastic_MeshPacket &mp)
    {
        if (!mp.pki_encrypted || mp.public_key.size != 32)
            return false;

        for (int i = 0; i < 3; ++i)
        {
            if (config.security.admin_key[i].size == 32 &&
                memcmp(mp.public_key.bytes, config.security.admin_key[i].bytes, 32) == 0)
            {
                return true;
            }
        }

        return false;
    }

    bool parseFavoriteTarget(const std::string &value, NodeNum &target)
    {
        std::string normalized = value;

        while (!normalized.empty() && std::isspace(static_cast<unsigned char>(normalized.front())))
        {
            normalized.erase(normalized.begin());
        }

        if (normalized.empty())
            return false;

        if (normalized.front() == '!')
        {
            normalized.erase(normalized.begin());
        }

        if (normalized.empty())
            return false;

        if (normalized.compare(0, 2, "0x") == 0 || normalized.compare(0, 2, "0X") == 0)
        {
            normalized.erase(0, 2);
        }

        if (normalized.empty())
            return false;

        for (char c : normalized)
        {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }

        char *end = nullptr;
        const unsigned long parsed = std::strtoul(normalized.c_str(), &end, 16);

        if (end == nullptr || *end != '\0')
        {
            return false;
        }

        target = static_cast<NodeNum>(parsed);
        return true;
    }
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
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_BASE)
    {
        LOG_DEBUG("FavoriteModule: ignoring packet because role=%d is not R/RL/CB", config.device.role);
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

    std::string rest(buf + 3);
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
    {
        rest.erase(rest.begin());
    }

    if (!rest.empty())
    {
        bool addFavorite = true;
        if (strncasecmp(rest.c_str(), "ADD", 3) == 0 &&
            (rest.size() == 3 || std::isspace(static_cast<unsigned char>(rest[3]))))
        {
            rest.erase(0, 3);
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
            {
                rest.erase(rest.begin());
            }
        }
        else if (strncasecmp(rest.c_str(), "DEL", 3) == 0 &&
                 (rest.size() == 3 || std::isspace(static_cast<unsigned char>(rest[3]))))
        {
            addFavorite = false;
            rest.erase(0, 3);
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
            {
                rest.erase(rest.begin());
            }
        }

        NodeNum target = 0;
        if (!parseFavoriteTarget(rest, target))
        {
            LOG_WARN("FavoriteModule: ignoring malformed favorite command from 0x%08x: '%s'", mp.from, rest.c_str());
            return ProcessMessage::STOP;
        }

        if (!senderIsAuthorizedAdmin(mp))
        {
            LOG_WARN("FavoriteModule: rejecting favorite command from non-admin sender 0x%08x", mp.from);
            return ProcessMessage::STOP;
        }

        if (addFavorite)
        {
            auto *node = nodeDB->getMeshNode(target);
            if (node == nullptr)
            {
                node = nodeDB->getOrCreateMeshNode(target);
            }

            if (node == nullptr)
            {
                LOG_WARN("FavoriteModule: failed to resolve node 0x%08x for favorite command", target);
                return ProcessMessage::STOP;
            }

            if (nodeDB->setProtectedFlag(node, NODEINFO_BITFIELD_IS_FAVORITE_MASK, true))
            {
                LOG_INFO("FavoriteModule: authorized admin 0x%08x added node 0x%08x to favorites", mp.from, target);
            }
            else
            {
                LOG_WARN("FavoriteModule: unable to add node 0x%08x to favorites (protected-node cap)", target);
            }
        }
        else
        {
            auto *node = nodeDB->getMeshNode(target);
            if (node == nullptr)
            {
                LOG_INFO("FavoriteModule: ignoring DEL for unknown node 0x%08x", target);
                return ProcessMessage::STOP;
            }

            if (nodeDB->setProtectedFlag(node, NODEINFO_BITFIELD_IS_FAVORITE_MASK, false))
            {
                LOG_INFO("FavoriteModule: authorized admin 0x%08x removed node 0x%08x from favorites", mp.from, target);
            }
            else
            {
                LOG_INFO("FavoriteModule: authorized admin 0x%08x removed node 0x%08x from favorites (already unset)",
                         mp.from, target);
            }
        }

        return ProcessMessage::STOP;
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

        reply->to = getFrom(&mp);
        reply->channel = mp.channel;
        reply->want_ack = false;
        reply->decoded.want_response = false;
        reply->decoded.request_id = 0;

        const size_t bodyLen = std::min<size_t>(body.size(), sizeof(reply->decoded.payload.bytes));
        reply->decoded.payload.size = bodyLen;
        memcpy(reply->decoded.payload.bytes, body.c_str(), bodyLen);

        LOG_DEBUG("FavoriteModule: sending payload='%s' (%u bytes)", body.c_str(), static_cast<unsigned int>(bodyLen));
        service->sendToMesh(reply);
        return ProcessMessage::STOP;
    }

    LOG_DEBUG("FavoriteModule: splitting favorite list into %u chunks", static_cast<unsigned int>(totalChunks));

    for (size_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex)
    {
        auto *reply = allocDataPacket();
        if (!reply)
        {
            LOG_WARN("FavoriteModule: failed to allocate reply packet while chunking favorite list");
            break;
        }

        reply->to = getFrom(&mp);
        reply->channel = mp.channel;
        reply->want_ack = false;
        reply->decoded.want_response = false;
        reply->decoded.request_id = 0;

        const size_t offset = chunkIndex * FAVORITE_REPLY_CHUNK_SIZE;
        const size_t chunkLen = std::min<size_t>(FAVORITE_REPLY_CHUNK_SIZE, body.size() - offset);

        reply->decoded.payload.size = chunkLen;
        memcpy(reply->decoded.payload.bytes, body.c_str() + offset, chunkLen);

        LOG_DEBUG("FavoriteModule: sending chunk %u/%u (%u bytes)", static_cast<unsigned int>(chunkIndex + 1),
                  static_cast<unsigned int>(totalChunks), static_cast<unsigned int>(chunkLen));
        service->sendToMesh(reply);
    }

    return ProcessMessage::STOP;
}

std::string FavoriteModule::buildFavoriteList() const
{
    std::string out = "";
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

    LOG_DEBUG("FavoriteModule: favorite list contains %u node(s)", static_cast<unsigned int>(favoriteCount));
    return out;
}
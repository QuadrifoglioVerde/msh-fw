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

bool FavoriteModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return p != nullptr && p->decoded.portnum == ourPortNum;
}

ProcessMessage FavoriteModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.to != nodeDB->getNodeNum())
    {
        return ProcessMessage::CONTINUE;
    }

    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)
    {
        return ProcessMessage::CONTINUE;
    }

    if (mp.decoded.payload.size == 0)
    {
        return ProcessMessage::CONTINUE;
    }

    char buf[256];
    const size_t n = std::min<size_t>(mp.decoded.payload.size, sizeof(buf) - 1);
    memcpy(buf, mp.decoded.payload.bytes, n);
    buf[n] = '\0';

    if (strncasecmp(buf, "FAV", 3) != 0)
    {
        return ProcessMessage::CONTINUE;
    }

    auto *reply = allocDataPacket();
    if (!reply)
    {
        return ProcessMessage::STOP;
    }

    reply->to = mp.from;
    reply->from = nodeDB->getNodeNum();
    reply->channel = mp.channel;
    reply->want_ack = true;

    const std::string body = buildFavoriteList();
    const size_t bodyLen = std::min<size_t>(body.size(), sizeof(reply->decoded.payload.bytes));
    reply->decoded.payload.size = bodyLen;
    memcpy(reply->decoded.payload.bytes, body.c_str(), bodyLen);

    service->sendToMesh(reply);
    return ProcessMessage::STOP;
}

std::string FavoriteModule::buildFavoriteList() const
{
    std::string out = "Favorite nodes: ";
    bool first = true;

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
    }

    if (first)
    {
        return "No favorite nodes";
    }

    return out;
}
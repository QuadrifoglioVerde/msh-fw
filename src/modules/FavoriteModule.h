#pragma once

#include "mesh/SinglePortModule.h"

class FavoriteModule : public SinglePortModule
{
  public:
    FavoriteModule();

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    std::string buildFavoriteList() const;
};

extern FavoriteModule *favoriteModule;
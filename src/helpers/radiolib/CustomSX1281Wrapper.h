#pragma once

#include "CustomSX1281.h"
#include "RadioLibWrappers.h"

class CustomSX1281Wrapper : public RadioLibWrapper {
public:
  CustomSX1281Wrapper(CustomSX1281& radio, mesh::MainBoard& board)
    : RadioLibWrapper(radio, board) { }

  bool isReceivingPacket() override {
    return ((CustomSX1281 *)_radio)->isReceiving();
  }

  float getCurrentRSSI() override {
    return ((CustomSX1281 *)_radio)->getRSSI(false);
  }

  float getLastRSSI() const override {
    return ((CustomSX1281 *)_radio)->getRSSI();
  }

  float getLastSNR() const override {
    return ((CustomSX1281 *)_radio)->getSNR();
  }
};

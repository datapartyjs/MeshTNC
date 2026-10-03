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

  bool sleepRadio() override {
    if (_asleep) return true;   // any SPI command (NSS edge) would wake it again
    bool ok = ((CustomSX1281 *)_radio)->sleepRetain();
    _state = 0;   // idle: needs a startReceive() once woken and rebound
    _asleep = ok;
    return ok;
  }

  bool wakeRadio() override {
    _state = 0;
    bool ok = ((CustomSX1281 *)_radio)->wake();
    _asleep = !ok;   // failed wake: treat as asleep so sleepRadio() sends it nothing; wake() retries
    return ok;
  }

protected:
  int readTxDoneFlag() override { return ((CustomSX1281 *)_radio)->isTxDone() ? 1 : 0; }
  int readInTxMode() override { return ((CustomSX1281 *)_radio)->isTransmitting() ? 1 : 0; }
};

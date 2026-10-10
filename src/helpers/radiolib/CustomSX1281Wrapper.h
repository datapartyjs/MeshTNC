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

  // The SX1281's packet status is only valid until the next RX starts, and recvRaw()
  // restarts RX before the Dispatcher asks for these, so they read 0. Latch them as the
  // packet is read instead.
  float getLastRSSI() const override { return _pkt_rssi; }
  float getLastSNR() const override { return _pkt_snr; }

protected:
  float _pkt_rssi = 0, _pkt_snr = 0;

  void onPacketRead() override {
    _pkt_rssi = ((CustomSX1281 *)_radio)->getRSSI();
    _pkt_snr = ((CustomSX1281 *)_radio)->getSNR();
  }

public:
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

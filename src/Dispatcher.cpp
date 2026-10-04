#include "Dispatcher.h"

#if MESH_PACKET_LOGGING
  #include <Arduino.h>
#endif

#include <math.h>

namespace mesh {

#define MAX_RX_DELAY_MILLIS   20  // 20milli seconds

// When the TX-done interrupt hasn't arrived by (1.5 x estimated airtime + margin),
// the chip is asked directly; if it's still transmitting, it's re-checked every
// TX_RECHECK_MILLIS until the hard limit, after which it is forced to standby.
#ifndef TX_TIMEOUT_MARGIN_MILLIS
  #define TX_TIMEOUT_MARGIN_MILLIS   20
#endif
#ifndef TX_RECHECK_MILLIS
  #define TX_RECHECK_MILLIS          5
#endif
#ifndef TX_HARD_TIMEOUT_MARGIN_MILLIS
  #define TX_HARD_TIMEOUT_MARGIN_MILLIS   500
#endif

#ifndef NOISE_FLOOR_CALIB_INTERVAL
  #define NOISE_FLOOR_CALIB_INTERVAL   2000     // 2 seconds
#endif

void Dispatcher::begin() {
  n_sent_flood = n_sent_direct = 0;
  n_recv_flood = n_recv_direct = 0;
  _err_flags = 0;
  radio_nonrx_start = _ms->getMillis();

  _radio->begin();
  prev_isrecv_mode = _radio->isInRecvMode();
}

float Dispatcher::getAirtimeBudgetFactor() const {
  return 2.0;   // default, 33.3%  (1/3rd)
}

int Dispatcher::calcRxDelay(float score, uint32_t air_time) const {
  return (int) ((pow(10, 0.85f - score) - 1.0) * air_time);
}

uint32_t Dispatcher::getCADFailRetryDelay() const {
  return 3;
}
uint32_t Dispatcher::getCADFailMaxDuration() const {
  return 15;   // 60 milli seconds
}

void Dispatcher::loop() {
  if (millisHasNowPassed(next_floor_calib_time)) {
    _radio->triggerNoiseFloorCalibrate(getInterferenceThreshold());
    next_floor_calib_time = futureMillis(NOISE_FLOOR_CALIB_INTERVAL);
  }
  _radio->loop();

  // check for radio 'stuck' in mode other than Rx
  bool is_recv = _radio->isInRecvMode();
  if (is_recv != prev_isrecv_mode) {
    prev_isrecv_mode = is_recv;
    if (!is_recv) {
      radio_nonrx_start = _ms->getMillis();
    }
  }
  if (!is_recv && _ms->getMillis() - radio_nonrx_start > 8000) {   // radio has not been in Rx mode for 8 seconds!
    _err_flags |= ERR_EVENT_STARTRX_TIMEOUT;
  }

  if (outbound) {  // waiting for outbound send to be completed
    int tx_status = _radio->pollSendStatus();   // driven by the radio's TX-done interrupt

    if (tx_status == RADIO_TX_PENDING && millisHasNowPassed(outbound_expiry)) {
      // interrupt is late: ask the chip what it's actually doing
      tx_status = _radio->verifySendStatus();
      if (tx_status == RADIO_TX_DONE) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): TX finished but TX-done IRQ was missed", getLogDateTime());
      } else if (tx_status == RADIO_TX_PENDING) {      // still on the air: keep waiting
        if (millisHasNowPassed(outbound_hard_expiry)) {
          _err_flags |= ERR_EVENT_TX_STUCK;
          MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): WARNING: radio stuck in TX, forcing standby", getLogDateTime());
          tx_status = RADIO_TX_FAILED;
        } else {
          outbound_expiry = futureMillis(TX_RECHECK_MILLIS);
        }
      } else if (tx_status == RADIO_TX_UNKNOWN) {      // radio can't report its state: old behaviour
        MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): WARNING: outbound packed send timed out!", getLogDateTime());
        tx_status = RADIO_TX_FAILED;
      }
    }

    if (tx_status == RADIO_TX_DONE) {
      long t = _ms->getMillis() - outbound_start;
      total_air_time += t;  // keep track of how much air time we are using
      //Serial.print("  airtime="); Serial.println(t);

      // will need radio silence up to next_tx_time
      //next_tx_time = futureMillis(t * getAirtimeBudgetFactor());
      next_tx_time = futureMillis(0);

      _radio->onSendFinished();
      logTx(outbound, 2 + outbound->payload_len);
      n_sent_direct++;

      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else if (tx_status == RADIO_TX_FAILED) {
      _err_flags |= ERR_EVENT_TX_FAIL;
      MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): WARNING: outbound packet send failed", getLogDateTime());

      _radio->onSendFinished();
      logTxFail(outbound, 2 + outbound->payload_len);

      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else {
      return;  // can't do any more radio activity until send is complete or timed out
    }

    // going back into receive mode now...
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  // radio being reconfigured, or disabled: no new RX or TX. Must stay after the
  // outbound block above, so a transmit already in flight is always completed.
  if (getRadioGate() != RADIO_GATE_OPEN) return;

  if (getAGCResetInterval() > 0 && millisHasNowPassed(next_agc_reset_time)) {
    _radio->resetAGC();
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  // check inbound (delayed) queue
  {
    Packet* pkt = _mgr->getNextInbound(_ms->getMillis());
    if (pkt) {
      processRecvPacket(pkt);
    }
  }
  checkRecv();
  checkSend();
}

void Dispatcher::checkRecv() {
  Packet* pkt;
  float score;
  uint32_t air_time;
  {
    uint8_t raw[MAX_TRANS_UNIT+1];
    int len = _radio->recvRaw(raw, MAX_TRANS_UNIT);
    if (len > 0) {
      logRxRaw(_radio->getLastSNR(), _radio->getLastRSSI(), raw, len);

      pkt = _mgr->allocNew();
      if (pkt == NULL) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): WARNING: received data, no unused packets available!", getLogDateTime());
      } else {
        pkt->payload_len = len;
        if (pkt->payload_len > sizeof(pkt->payload)) {
          MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): packet payload too big, payload_len=%d", getLogDateTime(), (uint32_t)pkt->payload_len);
          _mgr->free(pkt);  // put back into pool
          pkt = NULL;  
        } else {
          memcpy(pkt->payload, &raw, pkt->payload_len);

          pkt->_snr = _radio->getLastSNR() * 4.0f;
          score = _radio->packetScore(_radio->getLastSNR(), len);
          air_time = _radio->getEstAirtimeFor(len);
        }
      }
    } else {
      pkt = NULL;
    }
  }
  if (pkt) {
    #if MESH_PACKET_LOGGING
    Serial.print(getLogDateTime());
    Serial.printf(": RX, len=%d payload_len=%d SNR=%d RSSI=%d score=%d", 
            pkt->getRawLength(), pkt->payload_len,
            (int)pkt->getSNR(), (int)_radio->getLastRSSI(), (int)(score*1000));
    Serial.printf("\n");
    #endif
    logRx(pkt, pkt->getRawLength(), score);   // hook for custom logging


    n_recv_direct++;
    processRecvPacket(pkt);
  }
}

void Dispatcher::processRecvPacket(Packet* pkt) {
  DispatcherAction action = onRecvPacket(pkt);
  if (action == ACTION_RELEASE) {
    _mgr->free(pkt);
  } else if (action == ACTION_MANUAL_HOLD) {
    // sub-class is wanting to manually hold Packet instance, and call releasePacket() at appropriate time
  } else {   // ACTION_RETRANSMIT*
    uint8_t priority = (action >> 24) - 1;
    uint32_t _delay = action & 0xFFFFFF;

    _mgr->queueOutbound(pkt, priority, futureMillis(_delay));
  }
}

void Dispatcher::checkSend() {
  if (getRadioGate() != RADIO_GATE_OPEN) return;   // belt and braces: loop() already gates this
  if (_mgr->getOutboundCount(_ms->getMillis()) == 0) return;  // nothing waiting to send
  //if (!millisHasNowPassed(next_tx_time)) return;   // still in 'radio silence' phase (from airtime budget setting)
  if (_radio->isReceiving()) {   // LBT - check if radio is currently mid-receive, or if channel activity
    if (cad_busy_start == 0) {
      cad_busy_start = _ms->getMillis();   // record when CAD busy state started
    }

    if (_ms->getMillis() - cad_busy_start > getCADFailMaxDuration()) {
      _err_flags |= ERR_EVENT_CAD_TIMEOUT;

      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): CAD busy max duration reached!", getLogDateTime());
      // channel activity has gone on too long... (Radio might be in a bad state)
      // force the pending transmit below...
    } else {
      next_tx_time = futureMillis(getCADFailRetryDelay());
      return;
    }
  }
  cad_busy_start = 0;  // reset busy state

  outbound = _mgr->getNextOutbound(_ms->getMillis());
  if (outbound) {
    int len = 0;
    uint8_t raw[MAX_TRANS_UNIT];

    if (len + outbound->payload_len > MAX_TRANS_UNIT) {
      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): FATAL: Invalid packet queued... too long, len=%d", getLogDateTime(), len + outbound->payload_len);
      _mgr->free(outbound);
      outbound = NULL;
    } else {
      memcpy(&raw[len], outbound->payload, outbound->payload_len); len += outbound->payload_len;

      // 1.5x estimated airtime plus a fixed margin: at fast settings (e.g. SX1281
      // SF5/1625kHz) the estimate is only a few ms, and a late TX-done IRQ would
      // otherwise abort the packet mid-air via onSendFinished()
      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2 + TX_TIMEOUT_MARGIN_MILLIS;
      outbound_start = _ms->getMillis();
      bool success = _radio->startSendRaw(raw, len);
      if (!success) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): ERROR: send start failed!", getLogDateTime());
        _err_flags |= ERR_EVENT_TX_FAIL;

        logTxFail(outbound, outbound->getRawLength());
  
        releasePacket(outbound);  // return to pool
        outbound = NULL;
        return;
      }
      outbound_expiry = futureMillis(max_airtime);
      // only reached if the chip keeps reporting TX well past any plausible airtime
      outbound_hard_expiry = futureMillis(_radio->getEstAirtimeFor(len)*2 + TX_HARD_TIMEOUT_MARGIN_MILLIS);

    #if MESH_PACKET_LOGGING
      Serial.print(getLogDateTime());
      Serial.printf(": TX, len=%d payload_len=%d", len, outbound->payload_len);
      Serial.printf("\n");
    #endif
    }
  }
}

Packet* Dispatcher::obtainNewPacket() {
  auto pkt = _mgr->allocNew();  // TODO: zero out all fields
  if (pkt == NULL) {
    _err_flags |= ERR_EVENT_FULL;
  } else {
    pkt->payload_len = 0;
    pkt->_snr = 0;
    pkt->tx_tagged = false;
    pkt->tx_tag = 0;
  }
  return pkt;
}

void Dispatcher::releasePacket(Packet* packet) {
  _mgr->free(packet);
}

void Dispatcher::flushOutbound() {
  Packet* pkt;
  while ((pkt = _mgr->removeOutboundByIdx(0)) != NULL) {
    logTxFail(pkt, pkt->getRawLength());   // never sent (e.g. KISS ACKMODE reports the failure)
    _mgr->free(pkt);
  }
}

void Dispatcher::sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis) {
  if (getRadioGate() == RADIO_GATE_CLOSED) {   // no valid radio config: never queue for TX
    _err_flags |= ERR_EVENT_RADIO_DISABLED;
    logTxFail(packet, packet->getRawLength());
    _mgr->free(packet);
  } else if (packet->payload_len > MAX_PACKET_PAYLOAD) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::sendPacket(): ERROR: invalid packet... payload_len=%d", getLogDateTime(), (uint32_t) packet->payload_len);
    logTxFail(packet, packet->getRawLength());
    _mgr->free(packet);
  } else {
    _mgr->queueOutbound(packet, priority, futureMillis(delay_millis));
  }
}

// Utility function -- handles the case where millis() wraps around back to zero
//   2's complement arithmetic will handle any unsigned subtraction up to HALF the word size (32-bits in this case)
bool Dispatcher::millisHasNowPassed(unsigned long timestamp) const {
  return (long)(_ms->getMillis() - timestamp) > 0;
}

unsigned long Dispatcher::futureMillis(int millis_from_now) const {
  return _ms->getMillis() + millis_from_now;
}

}

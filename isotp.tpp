/*
  MIT License

  Copyright (c) 2018 Antonio Alexander Brewer (tonton81) - https://github.com/tonton81

  Designed and tested for PJRC Teensy 4.0.

  Forum link : https://forum.pjrc.com/threads/56035-FlexCAN_T4-FlexCAN-for-Teensy-4?highlight=flexcan_t4

  Thanks goes to skpang, mjs513, and collin for tech/testing support

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <isotp.h>
#include "Arduino.h"


ISOTP_FUNC void ISOTP_OPT::sendFlowControl(const ISOTP_data &config) {
  CAN_message_t msg;
  msg.id = config.id;
  msg.len = 8;
  msg.flags.extended = config.flags.extended;
  msg.buf[0] = (3U << 4) | constrain(config.flow_control_type, 0, 2);
  msg.buf[1] = config.blockSize;
  uint16_t separation_time = config.separation_time;
  if ( config.flags.separation_uS ) {
    separation_time = constrain(((config.separation_time + 50) / 100 * 100), 100, 900);
    separation_time = map(separation_time, 100, 900, 0xF1, 0xF9);
  }
  else {
    separation_time = constrain(separation_time, 0, 127);
  }
  msg.buf[2] = separation_time;
  _isotp_busToWrite->write(msg);
}


ISOTP_FUNC void ISOTP_OPT::write(const ISOTP_data &config, const uint8_t *buf, uint16_t size) {
  CAN_message_t msg;
  msg.id = config.id;
  msg.len = config.len;
  msg.flags.extended = config.flags.extended;
  msg.buf[0] = (1U << 4) | (uint16_t)size >> 4;
  msg.buf[1] = (uint8_t)size;
  memmove(&msg.buf[2], &buf[0], 6);
  _isotp_busToWrite->write(msg);
  delay(constrain(config.separation_time, 0, 127));
  for ( int sent_bytes = 6, difference = 7, counter = 1; sent_bytes < size; sent_bytes += 7, counter++ ) {
    msg.buf[0] = (2U << 4) | (counter & 0xF);
    difference = constrain((size - sent_bytes), 1, 7);
    memmove(&msg.buf[1], &buf[sent_bytes], difference);
    for ( int i = 0; i < (7 - difference); i++ ) msg.buf[difference + i + 1] = padding_value;
    if ( !config.flags.usePadding && difference < 7 ) msg.len = difference + 1;
    _isotp_busToWrite->write(msg);
    delay(constrain(config.separation_time, 0, 127));
  }
}


ISOTP_FUNC void ISOTP_OPT::_process_frame_data(const CAN_message_t &msg) {
  if ( !isotp_enabled ) return;
  if ( (msg.buf[0] >> 4) == 1 ) { /* first frame */
    if ( (((((uint16_t)msg.buf[0] & 0xF) << 4) | msg.buf[1]) + 9) >= (int)_max_length ) return; /* ISOTP message too large for local buffer */
    uint8_t data[_max_length] = { (uint8_t)(msg.id >> 24), (uint8_t)(msg.id >> 16), (uint8_t)(msg.id >> 8), (uint8_t)msg.id, 0, 6, 0 };
    memmove(data + 7, &msg.buf[0], 8); /* ID, ID, ID, ID, QPOS, QPOS, SEQUENCE: TOTAL 7 */
    _rx_slots.findRemove(data, sizeof(data), 0, 1, 2, 3, 3);
    _rx_slots.push_back(data, sizeof(data));
  } /* first frame */

  if ( (msg.buf[0] >> 4) == 2 ) { /* consecutive frames */
    uint8_t data[_max_length] = { (uint8_t)(msg.id >> 24), (uint8_t)(msg.id >> 16), (uint8_t)(msg.id >> 8), (uint8_t)msg.id };
    if ( _rx_slots.find(data, sizeof(data), 0, 1, 2, 3, 3) ) {
      int pos = (((uint16_t)data[4] & 0xF) | data[5]);
      int new_pos = pos + 7;
      data[4] = new_pos >> 8;
      data[5] = new_pos;
      data[6] += 1;
      if ( data[6] > 15 ) data[6] = 0;
      if ( (msg.buf[0] & 0xF) != data[6] ) { /* sequence match fail */
        _rx_slots.findRemove(data, sizeof(data), 0, 1, 2, 3, 3);
        return;
      }
      data[6] = msg.buf[0] & 0xF;
      memmove(data + 9 + pos, &msg.buf[1], 7);
      _rx_slots.replace(data, sizeof(data), 0, 1, 2, 3, 3);
      if ( (((((uint16_t)data[7] & 0xF) << 4) | data[8]) - pos) <= 7 ) {
        _rx_slots.findRemove(data, sizeof(data), 0, 1, 2, 3, 3);
        ISOTP_data config;
        config.id = ((uint32_t)(data[0] << 24) | data[1] << 16 | data[2] << 8 | data[3]);
        config.len = ((((uint16_t)data[7] & 0xF) << 4) | data[8]);
        config.flags.extended = msg.flags.extended;
        if ( _ISOTP_OBJ->_isotp_handler ) _ISOTP_OBJ->_isotp_handler(config, data + 9);
      }
    }
  } /* consecutive frames */
}

void ext_output2(const CAN_message_t &msg) {
  _ISOTP_OBJ->_process_frame_data(msg);
}
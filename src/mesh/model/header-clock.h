/*
 * header-clock.h
 *
 *  Created on: May 20, 2015
 *      Author: linus
 */

#ifndef SRC_MESH_MODEL_HEADER_CLOCK_H_
#define SRC_MESH_MODEL_HEADER_CLOCK_H_

#include "ns3/ptr.h"
#include "ns3/packet.h"
#include "ns3/header.h"

#include <iostream>
#include <sstream>
#include <fstream>

namespace ns3 {


class MyHeader : public Header
{
public:

  MyHeader ();
  virtual ~MyHeader ();

  void SetData (uint64_t data);
  uint16_t GetData (void) const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  virtual uint32_t GetSerializedSize (void) const;


private:

  uint64_t m_data;

	};

}

#endif /* SRC_MESH_MODEL_HEADER_CLOCK_H_ */
